// main/kj_prov.h —— 设备热点网页（平台层，依赖 ESP-IDF；只在热点启动模式下运行）。两种用途：
//
// 配网（电脑服务模式）：设备以 APSTA 模式开热点 KJ-XXXX（WPA2，随机 8 位数字口令），屏幕显示 WIFI: 二维码让
// 手机相机扫码加入；通配 DNS + 探测地址重定向把手机带到配网页（没弹出就手动打开 192.168.4.1）。网页提交 Wi-Fi
// 后，先用 STA 实际连一次：连上（拿到地址）才算成功，由调用方保存凭据并重启；失败可在手机上重试。
//
// 登记昵称（直连模式，没有电脑服务）：同样的热点与弹窗，只开 AP；网页里填写昵称，由调用方给的回调校验
//（要查昵称字库，回调运行在 HTTP 任务里、自己持 LVGL 锁），通过后发 KJ_PROV_EV_NAME_OK，由调用方保存并重启。
//
// HTTP 处理函数与 DNS 任务都不碰 LVGL，事件通过回调交给应用任务（回调只能拷贝入队）。
#pragma once

#include "esp_err.h"
#include "kj_net.h"
#include "kj_proto.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    KJ_PROV_MODE_WIFI = 0,     // 配网
    KJ_PROV_MODE_NAME,         // 登记昵称（直连模式）
} kj_prov_mode_t;

typedef enum {
    KJ_PROV_EV_PHONE_IN = 1,   // 手机连上热点并分到地址
    KJ_PROV_EV_PHONE_OUT,      // 手机离开热点
    KJ_PROV_EV_TRYING,         // 收到网页提交，正在连接
    KJ_PROV_EV_OK,             // 连上了：用 kj_prov_take_result 取凭据
    KJ_PROV_EV_FAILED,         // 连不上：arg = wifi_err_reason_t（0 = 超时）
    KJ_PROV_EV_NAME_OK,        // 昵称已通过校验：用 kj_prov_take_name 取
} kj_prov_event_t;

typedef void (*kj_prov_cb_t)(uint8_t ev, int arg);
// 校验网页提交的昵称表单正文（application/x-www-form-urlencoded），约定同 kj_name_form_parse。
typedef bool (*kj_prov_name_check_t)(const char *body, size_t n, char name[KJ_NAME_MAX + 1], const char **err,
                                     char *bad, size_t bad_cap);

// mode = KJ_PROV_MODE_NAME 时 current 为本机现在的昵称（网页里预先填好），check 不能为 NULL。
esp_err_t kj_prov_start(kj_prov_mode_t mode, kj_prov_cb_t cb, const char *current, kj_prov_name_check_t check);
void kj_prov_stop(void);
// 热点名、口令与手机相机可扫的 WIFI: 二维码内容。
void kj_prov_get_ap(char ssid[16], char pass[12], char qr[64]);
// 正在尝试的 Wi-Fi 名（显示用）。
void kj_prov_get_target(char ssid[33]);
// 成功后取出凭据与网页里填的电脑地址（0 = 自动寻找）。
bool kj_prov_take_result(kj_wifi_cred_t *cred, uint32_t *hub_ip);
// 登记昵称成功后取出昵称。
bool kj_prov_take_name(char out[KJ_NAME_MAX + 1]);
