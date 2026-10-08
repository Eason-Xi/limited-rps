// main/kj_prov.c —— 设备热点网页：配网与（直连模式的）登记昵称（平台层）。
#include "kj_prov.h"
#include "kj_dns.h"
#include "kj_prov_form.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "kj_prov";

#define AP_CHANNEL        6
#define AP_MAX_CONN       2
#define SCAN_MAX          20
#define TRY_TIMEOUT_MS    15000
#define DNS_TASK_STACK    3072
#define HTTP_STACK        5120

extern const char kj_prov_html_start[] asm("_binary_kj_prov_html_start");
extern const char kj_prov_html_end[] asm("_binary_kj_prov_html_end");
extern const char kj_name_html_start[] asm("_binary_kj_name_html_start");
extern const char kj_name_html_end[] asm("_binary_kj_name_html_end");

typedef enum { TRY_IDLE = 0, TRY_RUNNING, TRY_OK, TRY_FAILED } try_state_t;

static kj_prov_cb_t s_cb;
static kj_prov_mode_t s_mode;
static kj_prov_name_check_t s_check;
static bool s_started;
static httpd_handle_t s_http;
static esp_netif_t *s_ap_netif, *s_sta_netif;
static esp_timer_handle_t s_try_timer;
static SemaphoreHandle_t s_lock;
static volatile bool s_dns_run;
static TaskHandle_t s_dns_task;
static char s_ap_ssid[16], s_ap_pass[12];
static uint32_t s_ap_ip;

// 以下由 s_lock 保护（HTTP 任务、事件任务、定时器与应用任务都会读写）
static kj_ap_rec_t s_scan[SCAN_MAX];
static int s_scan_n;
static bool s_scanning;
static kj_prov_form_t s_form;
static volatile try_state_t s_try;
static volatile int s_try_reason;
static char s_name_cur[KJ_NAME_MAX + 1], s_name_new[KJ_NAME_MAX + 1];
static bool s_name_done;

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static void emit(uint8_t ev, int arg)
{
    if (s_cb) s_cb(ev, arg);
}

// ---------------------------------------------------------------------------
// 扫描与试连
// ---------------------------------------------------------------------------

static void start_scan(void)
{
    lock();
    bool busy = s_scanning || s_try == TRY_RUNNING;
    if (!busy) s_scanning = true;
    unlock();
    if (busy) return;
    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, false) != ESP_OK) {
        lock();
        s_scanning = false;
        unlock();
    }
}

static void on_scan_done(void)
{
    uint16_t n = SCAN_MAX;
    static wifi_ap_record_t recs[SCAN_MAX];   // 只在事件任务里用
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) n = 0;
    lock();
    s_scan_n = 0;
    for (int i = 0; i < n && s_scan_n < SCAN_MAX; i++) {
        kj_ap_rec_t *r = &s_scan[s_scan_n++];
        memcpy(r->ssid, recs[i].ssid, sizeof(r->ssid) - 1);
        r->ssid[sizeof(r->ssid) - 1] = '\0';
        r->rssi = recs[i].rssi;
        r->secure = recs[i].authmode != WIFI_AUTH_OPEN;
    }
    s_scanning = false;
    unlock();
}

static void try_finish(bool ok, int reason)
{
    esp_timer_stop(s_try_timer);
    lock();
    bool was = s_try == TRY_RUNNING;
    if (was) {
        s_try = ok ? TRY_OK : TRY_FAILED;
        s_try_reason = reason;
    }
    unlock();
    if (!was) return;
    if (!ok) esp_wifi_disconnect();
    emit(ok ? KJ_PROV_EV_OK : KJ_PROV_EV_FAILED, reason);
}

static void try_timeout(void *arg)
{
    (void)arg;
    try_finish(false, 0);
}

static void try_connect(const kj_prov_form_t *f)
{
    lock();
    s_form = *f;
    s_try = TRY_RUNNING;
    s_try_reason = 0;
    unlock();
    esp_wifi_scan_stop();
    esp_wifi_disconnect();
    wifi_config_t wc = { 0 };
    memcpy(wc.sta.ssid, f->ssid, strnlen(f->ssid, sizeof(wc.sta.ssid)));
    memcpy(wc.sta.password, f->pass, strnlen(f->pass, sizeof(wc.sta.password)));
    wc.sta.threshold.authmode = f->pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_timer_stop(s_try_timer);
    esp_timer_start_once(s_try_timer, (uint64_t)TRY_TIMEOUT_MS * 1000u);
    if (esp_wifi_connect() != ESP_OK) try_finish(false, 0);
    emit(KJ_PROV_EV_TRYING, 0);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_SCAN_DONE: on_scan_done(); break;
        case WIFI_EVENT_AP_STADISCONNECTED: emit(KJ_PROV_EV_PHONE_OUT, 0); break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            const wifi_event_sta_disconnected_t *d = data;
            // 认证失败、找不到网络等明确的原因立即判失败；其他原因等超时（驱动内部会重试一次）
            int reason = d ? d->reason : 0;
            bool fatal = reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_NO_AP_FOUND ||
                         reason == WIFI_REASON_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                         reason == WIFI_REASON_AUTH_EXPIRE || reason == WIFI_REASON_ASSOC_FAIL;
            if (fatal) try_finish(false, reason);
            break;
        }
        default: break;
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_AP_STAIPASSIGNED) emit(KJ_PROV_EV_PHONE_IN, 0);   // 以 DHCP 完成为准
        if (id == IP_EVENT_STA_GOT_IP) try_finish(true, 0);
    }
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t h_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (s_mode == KJ_PROV_MODE_NAME) {
        return httpd_resp_send(req, kj_name_html_start, kj_name_html_end - kj_name_html_start);
    }
    return httpd_resp_send(req, kj_prov_html_start, kj_prov_html_end - kj_prov_html_start);
}

// 读完整个表单正文（cap 含结尾 0）。太长或读失败返回 -1。
static int read_body(httpd_req_t *req, char *body, size_t cap)
{
    if (req->content_len == 0 || req->content_len >= cap) return -1;
    size_t got = 0;
    int tries = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++tries < 3) continue;
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    body[got] = '\0';
    return (int)got;
}

static esp_err_t h_scan(httpd_req_t *req)
{
    char q[24];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK && strstr(q, "refresh=1")) start_scan();
    static char json[1536];   // 只在 HTTP 任务里用（单任务）
    lock();
    kj_prov_scan_json(s_scan, s_scan_n, json, sizeof(json));
    bool scanning = s_scanning;
    unlock();
    httpd_resp_set_hdr(req, "X-KJ-Scanning", scanning ? "1" : "0");
    return send_json(req, json);
}

static esp_err_t h_connect(httpd_req_t *req)
{
    char body[KJ_PROV_FORM_MAX + 1];
    int got = read_body(req, body, sizeof(body));
    if (got < 0) return send_json(req, "{\"ok\":false,\"err\":\"form\"}");
    kj_prov_form_t f;
    const char *err = NULL;
    if (!kj_prov_form_parse(body, (size_t)got, &f, &err)) {
        char resp[48];
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"err\":\"%s\"}", err ? err : "form");
        return send_json(req, resp);
    }
    if (s_try == TRY_RUNNING) return send_json(req, "{\"ok\":false,\"err\":\"busy\"}");
    try_connect(&f);
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t h_status(httpd_req_t *req)
{
    static const char *const names[] = { "idle", "trying", "ok", "fail" };
    char resp[48];
    lock();
    snprintf(resp, sizeof(resp), "{\"state\":\"%s\",\"reason\":%d}", names[s_try], s_try_reason);
    unlock();
    return send_json(req, resp);
}

// ---- 登记昵称（直连模式）----

// 网页打开时预先填好现在的昵称，并显示设备编号（认得出是哪一台）。
static esp_err_t h_info(httpd_req_t *req)
{
    char name[KJ_NAME_MAX * 6 + 1];
    lock();
    bool ok = kj_prov_json_text(s_name_cur, name, sizeof(name));
    unlock();
    char resp[KJ_NAME_MAX * 6 + 48];
    snprintf(resp, sizeof(resp), "{\"name\":\"%s\",\"dev\":\"%.4s\"}", ok ? name : "", s_ap_ssid + 3);
    return send_json(req, resp);
}

static esp_err_t h_name(httpd_req_t *req)
{
    char body[KJ_NAME_FORM_MAX + 1];
    int got = read_body(req, body, sizeof(body));
    if (got < 0) return send_json(req, "{\"ok\":false,\"err\":\"form\"}");
    lock();
    bool done = s_name_done;
    unlock();
    if (done) return send_json(req, "{\"ok\":true}");   // 重复提交：已经登记过了
    char name[KJ_NAME_MAX + 1], bad[48];
    const char *err = NULL;
    if (!s_check || !s_check(body, (size_t)got, name, &err, bad, sizeof(bad))) {
        char text[sizeof(bad) * 6 + 1];
        if (!kj_prov_json_text(bad, text, sizeof(text))) text[0] = '\0';
        char resp[sizeof(text) + 64];
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"err\":\"%s\",\"bad\":\"%s\"}", err ? err : "form", text);
        return send_json(req, resp);
    }
    lock();
    memcpy(s_name_new, name, sizeof(s_name_new));
    s_name_done = true;
    unlock();
    emit(KJ_PROV_EV_NAME_OK, 0);
    return send_json(req, "{\"ok\":true}");
}

// 各系统的联网探测地址、以及其他任何路径：一律重定向到热点网页（弹窗认证）。
static esp_err_t h_redirect(httpd_req_t *req)
{
    char loc[32];
    uint8_t b[4];
    memcpy(b, &s_ap_ip, 4);
    snprintf(loc, sizeof(loc), "http://%u.%u.%u.%u/", b[0], b[1], b[2], b[3]);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", loc);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t start_http(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = HTTP_STACK;
    cfg.max_open_sockets = 4;
    cfg.backlog_conn = 2;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 6;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    esp_err_t err = httpd_start(&s_http, &cfg);
    if (err != ESP_OK) return err;
    const httpd_uri_t wifi_uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = h_index },
        { .uri = "/scan", .method = HTTP_GET, .handler = h_scan },
        { .uri = "/connect", .method = HTTP_POST, .handler = h_connect },
        { .uri = "/status", .method = HTTP_GET, .handler = h_status },
        { .uri = "/*", .method = HTTP_GET, .handler = h_redirect },   // 最后注册：兜底
    };
    const httpd_uri_t name_uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = h_index },
        { .uri = "/info", .method = HTTP_GET, .handler = h_info },
        { .uri = "/name", .method = HTTP_POST, .handler = h_name },
        { .uri = "/*", .method = HTTP_GET, .handler = h_redirect },
    };
    bool name = s_mode == KJ_PROV_MODE_NAME;
    const httpd_uri_t *uris = name ? name_uris : wifi_uris;
    size_t n = name ? sizeof(name_uris) / sizeof(name_uris[0]) : sizeof(wifi_uris) / sizeof(wifi_uris[0]);
    for (size_t i = 0; i < n; i++) httpd_register_uri_handler(s_http, &uris[i]);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// DNS
// ---------------------------------------------------------------------------

static void dns_task(void *arg)
{
    (void)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd >= 0) {
        struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53) };
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 200000 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            close(fd);
            fd = -1;
        }
    }
    static uint8_t q[KJ_DNS_MAX], r[KJ_DNS_MAX];
    while (s_dns_run && fd >= 0) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(fd, q, sizeof(q), 0, (struct sockaddr *)&from, &fl);
        if (n <= 0) continue;
        size_t m = kj_dns_reply(q, (size_t)n, s_ap_ip, r, sizeof(r));
        if (m) sendto(fd, r, m, 0, (struct sockaddr *)&from, fl);
    }
    if (fd >= 0) close(fd);
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// 启停
// ---------------------------------------------------------------------------

void kj_prov_get_ap(char ssid[16], char pass[12], char qr[64])
{
    if (ssid) snprintf(ssid, 16, "%s", s_ap_ssid);
    if (pass) snprintf(pass, 12, "%s", s_ap_pass);
    if (qr) snprintf(qr, 64, "WIFI:T:WPA;S:%s;P:%s;;", s_ap_ssid, s_ap_pass);
}

void kj_prov_get_target(char ssid[33])
{
    lock();
    snprintf(ssid, 33, "%s", s_form.ssid);
    unlock();
}

bool kj_prov_take_name(char out[KJ_NAME_MAX + 1])
{
    lock();
    bool ok = s_name_done;
    if (ok) memcpy(out, s_name_new, sizeof(s_name_new));
    unlock();
    return ok;
}

bool kj_prov_take_result(kj_wifi_cred_t *cred, uint32_t *hub_ip)
{
    lock();
    bool ok = s_try == TRY_OK;
    if (ok) {
        memset(cred, 0, sizeof(*cred));
        snprintf(cred->ssid, sizeof(cred->ssid), "%s", s_form.ssid);
        snprintf(cred->pass, sizeof(cred->pass), "%s", s_form.pass);
        if (hub_ip) *hub_ip = s_form.hub_ip;
    }
    unlock();
    return ok;
}

esp_err_t kj_prov_start(kj_prov_mode_t mode, kj_prov_cb_t cb, const char *current, kj_prov_name_check_t check)
{
    if (s_started) return ESP_OK;
    if (mode == KJ_PROV_MODE_NAME && !check) return ESP_ERR_INVALID_ARG;
    s_cb = cb;
    s_mode = mode;
    s_check = check;
    snprintf(s_name_cur, sizeof(s_name_cur), "%s", current ? current : "");
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "KJ-%02X%02X", mac[4], mac[5]);
    snprintf(s_ap_pass, sizeof(s_ap_pass), "%08lu", (unsigned long)(esp_random() % 100000000u));

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    bool wifi = mode == KJ_PROV_MODE_WIFI;   // 只有配网要用 STA 扫描与试连
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (wifi) s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_ap_netif || (wifi && !s_sta_netif)) return ESP_ERR_NO_MEM;
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_ap_netif, &ip) == ESP_OK) s_ap_ip = ip.ip.addr;

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    wcfg.nvs_enable = 0;
    err = esp_wifi_init(&wcfg);
    if (err != ESP_OK) return err;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    const esp_timer_create_args_t targs = { .callback = try_timeout, .name = "kj_prov_try" };
    err = esp_timer_create(&targs, &s_try_timer);
    if (err != ESP_OK) return err;
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);

    wifi_config_t ap = { 0 };
    memcpy(ap.ap.ssid, s_ap_ssid, strlen(s_ap_ssid));
    ap.ap.ssid_len = (uint8_t)strlen(s_ap_ssid);
    memcpy(ap.ap.password, s_ap_pass, strlen(s_ap_pass));
    ap.ap.channel = AP_CHANNEL;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = AP_MAX_CONN;
    err = esp_wifi_set_mode(wifi ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) return err;
    esp_wifi_set_ps(WIFI_PS_NONE);
    err = start_http();
    if (err != ESP_OK) return err;
    s_dns_run = true;
    if (xTaskCreate(dns_task, "kj_dns", DNS_TASK_STACK, NULL, 4, &s_dns_task) != pdPASS) return ESP_ERR_NO_MEM;
    s_started = true;
    if (wifi) start_scan();   // 先扫一次，网页打开时就有列表
    ESP_LOGI(TAG, "%s AP %s up", wifi ? "provisioning" : "name registration", s_ap_ssid);   // 不打印口令
    return ESP_OK;
}

void kj_prov_stop(void)
{
    if (!s_started) return;
    s_dns_run = false;
    for (int i = 0; i < 20 && s_dns_task; i++) vTaskDelay(pdMS_TO_TICKS(50));
    if (s_http) httpd_stop(s_http);
    s_http = NULL;
    esp_timer_stop(s_try_timer);
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event);
    esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID, on_event);
    esp_wifi_stop();
    esp_wifi_deinit();
    s_started = false;
}
