// main/kj_store.h —— NVS 存储：联机方式、上次的角色 / 赌局号、庄家的赌局快照、Wi-Fi 凭据、昵称、上次的 hub 地址，
// 以及只生效一次的开机请求（配网、热点登记昵称、开机直接进入选手）。
// 密码只在本模块与 Wi-Fi 驱动之间传递，绝不写日志。
#pragma once

#include "esp_err.h"
#include "kj_hubproto.h"
#include "kj_net.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

esp_err_t kj_store_init(void);   // nvs_flash_init（不擦除已有数据）

// 联机方式 kj_conn_t（见 kj_flow.h；命名空间 "kjnet"，键 "conn"），没有保存过时是直连。切换后需要重启才生效。
uint8_t kj_store_get_conn(void);
void kj_store_set_conn(uint8_t conn);

uint8_t kj_store_get_role(void); // 0 选手（默认）/ 1 庄家
void kj_store_set_role(uint8_t role);
uint16_t kj_store_get_room(void);
void kj_store_set_room(uint16_t room);
// 庄家赌局快照
esp_err_t kj_store_save_game(const uint8_t *blob, size_t len);
size_t kj_store_load_game(uint8_t *blob, size_t cap);   // 返回长度，没有或失败返回 0

// Wi-Fi 凭据（命名空间 "kjnet"）。只有新凭据连接成功后才调用 set，旧凭据保留到那一刻。
bool kj_store_get_wifi(kj_wifi_cred_t *out);            // 没有返回 false
esp_err_t kj_store_set_wifi(const kj_wifi_cred_t *cred);
// 上次找到的 hub 地址（IPv4 网络字节序，0 = 没有）
uint32_t kj_store_get_hub_hint(void);
void kj_store_set_hub_hint(uint32_t ip);
// 下次开机进入配网模式
void kj_store_request_prov(bool on);
bool kj_store_take_prov_request(void);
// 直连模式：下次开机进入"热点登记昵称"。then_play = 登记完（或取消后）直接进入选手。
void kj_store_request_name_ap(bool then_play);
bool kj_store_take_name_ap_request(bool *then_play);
// 下次开机跳过首页，直接进入选手
void kj_store_request_play(bool on);
bool kj_store_take_play_request(void);

// 本机昵称。电脑服务模式以 hub 的登记表为准（rev 由 hub 分配）；直连模式在设备热点网页里登记（rev + 1）。
void kj_store_get_name(char out[KJ_NAME_MAX + 1], uint32_t *rev);
void kj_store_set_name(const char *name, uint32_t rev);
