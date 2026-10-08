# 限定猜拳固件只编译 main.c 与 kj_*.c。
# baseline 的 demo_*.c / ui_pixel*.c 仍保留在仓库里（主机测试会用到），但不进固件，
# 开机直接进入本应用自己的界面。
file(GLOB KJ_SOURCES "${CMAKE_CURRENT_LIST_DIR}/kj_*.c")

idf_component_register(
    SRCS "main.c" ${KJ_SOURCES}
    INCLUDE_DIRS "."
    REQUIRES bsp esp_driver_usb_serial_jtag esp_event esp_http_server esp_hw_support esp_netif esp_system
             esp_timer esp_wifi heap lwip nvs_flash
    # 设备热点上的网页（手机打开 192.168.4.1 看到的页面）：配网、直连模式登记昵称，见 kj_prov.c
    EMBED_TXTFILES "web/kj_prov.html" "web/kj_name.html"
)

# 中文 / 手势字体子集（tools/gen_kj_fonts.py 生成，见 assets/README）。
file(GLOB KJ_FONT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/kj_*.c")
target_sources(${COMPONENT_LIB} PRIVATE ${KJ_FONT_SOURCES})
