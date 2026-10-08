// main/kj_model.h —— 把协议 / 界面状态整理成 kj_ui_model_t（纯 C，固件与电脑预览共用）。
#pragma once

#include "kj_flow.h"
#include "kj_server.h"
#include "kj_ui.h"

#include <stdbool.h>
#include <stdint.h>

// 本机与联网信息（每个页面的模型都会带上）。
typedef struct {
    uint8_t conn;           // kj_conn_t
    uint8_t channel;        // 直连模式的无线信道
    uint8_t net;            // kj_net_status_t
    uint32_t ip, hub_ip;    // IPv4（网络字节序）
    const char *ssid;
    const char *my_name;
    const char *fw;
    uint16_t dev_id;
} kj_model_env_t;

// 座位昵称查询：已知返回 true（*name 可为空串 = 没登记 / 电脑选手）；未知返回 false（平台层会去查）。
typedef struct {
    bool (*lookup)(void *ctx, uint16_t room, uint8_t no, const char **name);
    void *ctx;
} kj_names_if_t;

void kj_model_title(kj_ui_model_t *m, const kj_flow_t *f, const kj_model_env_t *env, int battery,
                    uint32_t now_ms);
void kj_model_settings(kj_ui_model_t *m, const kj_flow_t *f, const kj_model_env_t *env, int battery,
                       uint32_t now_ms);
// 电脑服务模式：reg_state 为 kh_reg_state_t（0 = 还没连上电脑服务，此时不显示二维码）；url 为登记网址
//（屏幕上分两行显示：line1 = 主机:端口，line2 = 路径）。直连模式不用这两个参数（页面只做说明）。
void kj_model_register(kj_ui_model_t *m, const kj_flow_t *f, const kj_model_env_t *env, uint8_t reg_state,
                       const char *url, int battery, uint32_t now_ms);
// 热点页：kind 为 kj_prov_kind_t；qr 为加入热点的 WIFI: 串，line1 / line2 为热点名与口令
//（配网连接中时 line2 为目标 Wi-Fi 名）。
void kj_model_provision(kj_ui_model_t *m, const kj_flow_t *f, const kj_model_env_t *env, uint8_t kind,
                        uint8_t prov_state, const char *qr, const char *line1, const char *line2, int battery,
                        uint32_t now_ms);
void kj_model_player(kj_ui_model_t *m, const kj_flow_t *f, const kj_player_ctx_t *ctx, kj_page_t page,
                     const kj_model_env_t *env, const kj_names_if_t *names, int battery);
void kj_model_host(kj_ui_model_t *m, const kj_flow_t *f, const kj_server_t *s, uint32_t now_ms, int battery,
                   uint8_t board, const kj_model_env_t *env, const kj_names_if_t *names);
// 选对手页的第一行（选中项尽量停在第三行），模型与界面共用。
uint8_t kj_model_opp_first(uint8_t sel, uint8_t count);
// IPv4（网络字节序）→ "a.b.c.d"，out 至少 16 字节。
void kj_ip_text(uint32_t ip, char out[16]);
