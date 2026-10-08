// main/kj_radio.h —— 直连模式的无线收发：ESP-NOW（Wi-Fi STA 模式，不连任何路由器，固定信道）。
// 平台层，依赖 ESP-IDF。
//
//   * 所有设备在同一个信道（CONFIG_KJ_ESPNOW_CHANNEL，默认 1）上直接收发游戏帧，不需要电脑和路由器。
//   * 广播用于庄家信标与选手的昵称广播；其余（请求、视图、心跳）单播，由 ESP-NOW 在链路层确认与重传。
//   * 接收回调运行在 Wi-Fi 任务里，只能拷贝入队。
//   * 发送只能从应用任务调用（内部维护一个最近使用的对端表，突破 ESP-NOW 20 个对端的上限）。
#pragma once

#include "esp_err.h"
#include "kj_proto.h"
#include "sdkconfig.h"

#include <stdbool.h>
#include <stdint.h>

#define KJ_RADIO_CHANNEL CONFIG_KJ_ESPNOW_CHANNEL

typedef void (*kj_radio_rx_cb_t)(const uint8_t mac[6], int8_t rssi, const uint8_t *data, int len);

esp_err_t kj_radio_start(kj_radio_rx_cb_t cb);
bool kj_radio_started(void);
esp_err_t kj_radio_send(const kj_out_t *it);
uint32_t kj_radio_tx_failures(void);
