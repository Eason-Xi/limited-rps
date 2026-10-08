#!/usr/bin/env python3
"""限定猜拳的静态约束：固件不再进入 baseline 测试界面、线程模型、看板协议两端一致、网页不外连、
设备与电脑 hub 的线协议常量一致、昵称字符集与字库一致、直连模式（ESP-NOW）的约束、配置约束。"""

from pathlib import Path
import json
import re
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "kj_hub"))
sys.path.insert(0, str(ROOT / "tools"))

import kj_charset  # noqa: E402
import core as hub_core  # noqa: E402
import store as hub_store  # noqa: E402
import wire  # noqa: E402


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", source)
    if not match:
        raise AssertionError(f"function not found: {name}")
    depth = 0
    for index in range(match.end() - 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.end():index]
    raise AssertionError(f"function is unterminated: {name}")


def c_defines(source: str) -> dict[str, int]:
    out = {}
    for name, value in re.findall(r"#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+)\b", source):
        out[name] = int(value, 0)
    for body in re.findall(r"typedef enum\s*\{(.*?)\}", source, re.S):
        value = -1
        for item in re.findall(r"(\w+)\s*(?:=\s*(\w+))?\s*,", body):
            name, explicit = item
            value = int(explicit, 0) if explicit else value + 1
            out[name] = value
    return out


class FirmwareUiContract(unittest.TestCase):
    def test_baseline_test_ui_is_not_built(self):
        cmake = "\n".join(line.split("#", 1)[0] for line in read("main/CMakeLists.txt").splitlines())
        self.assertNotRegex(cmake, r"demo_\w+\.c|ui_pixel")
        self.assertIn('file(GLOB KJ_SOURCES "${CMAKE_CURRENT_LIST_DIR}/kj_*.c")', cmake)
        self.assertRegex(cmake, r'EMBED_TXTFILES "web/kj_prov.html" "web/kj_name.html"')
        main = read("main/main.c")
        for header in ("demo.h", "demo_navigation.h", "ui_pixel.h"):
            self.assertNotIn(f'#include "{header}"', main)
        for path in sorted((ROOT / "main").glob("kj_*.[ch]")):
            self.assertNotRegex(path.read_text(encoding="utf-8"), r"ui_pixel_|demo_navigation",
                                f"{path.name} reuses the baseline demo shell")

    def test_lvgl_only_touched_under_lock(self):
        main = read("main/main.c")
        for match in re.finditer(r"\bkj_ui_(render|init)\(", main):
            before = main[:match.start()]
            lock = before.rfind("bsp_lvgl_lock(")
            unlock = before.rfind("bsp_lvgl_unlock(")
            line = main.count("\n", 0, match.start()) + 1
            self.assertGreater(lock, unlock, f"main.c:{line} touches LVGL without holding the lock")
        # 按键、网络（Wi-Fi 收包任务 / ESP-NOW 回调共用 on_net_frame）、看板命令、热点网页的回调只拷贝入队
        for cb in ("on_key", "on_net_frame", "on_net_ctl", "on_net_event", "on_board_line", "on_prov_event"):
            body = function_body(main, cb)
            self.assertIn("xQueueSend", body, cb)
            self.assertNotRegex(body, r"\blv_|kj_ui_|kj_client_|kj_server_|kj_hubc_|kj_names_|bsp_audio|kj_sound_play|"
                                      r"esp_restart", cb)
        self.assertIn("kj_radio_start(on_net_frame)", main)
        # 热点登记昵称：HTTP 任务里查昵称字库（会写字体的查找缓存），必须持 LVGL 锁
        body = function_body(main, "name_check")
        lock, parse, unlock = (body.find(k) for k in ("bsp_lvgl_lock(", "kj_name_form_parse(", "bsp_lvgl_unlock("))
        self.assertTrue(0 <= lock < parse < unlock, "name_check must hold the LVGL lock while reading fonts")

    def test_platform_modules_stay_out_of_the_ui(self):
        for name in ("kj_net.c", "kj_prov.c", "kj_radio.c"):
            src = read(f"main/{name}")
            self.assertNotRegex(src, r"\blv_\w+\(|\bkj_ui_|bsp_lvgl", f"{name} must not touch LVGL")
        # 只有平台层碰 socket
        for path in sorted((ROOT / "main").glob("*.c")):
            if path.name in ("kj_net.c", "kj_prov.c") or path.name.startswith("demo_"):
                continue
            self.assertNotRegex(path.read_text(encoding="utf-8"), r"\b(socket|sendto|recvfrom)\(", path.name)

    def test_frames_fit(self):
        proto = c_defines(read("main/kj_proto.h"))
        self.assertLessEqual(proto["KJ_FRAME_MAX"], 250)   # 仍可放进一个 ESP-NOW 帧
        hub = c_defines(read("main/kj_hubproto.h"))
        self.assertLessEqual(hub["KH_HDR"] + hub["KH_PAYLOAD_MAX"], 1472)   # 一个以太网 MTU，设备不重组分片
        self.assertGreaterEqual(hub["KH_PAYLOAD_MAX"], proto["KJ_FRAME_MAX"])

    def test_config(self):
        defaults = read("sdkconfig.defaults")
        self.assertIn("CONFIG_BT_ENABLED=n", defaults)
        self.assertRegex(defaults, r"CONFIG_LV_MEM_SIZE_KILOBYTES=\d+")
        self.assertIn("CONFIG_LV_USE_FONT_PLACEHOLDER=y", defaults)
        self.assertIn("CONFIG_LV_USE_QRCODE=y", defaults)
        self.assertNotRegex(defaults, r"(?m)^CONFIG_LV_BIN_DECODER_RAM_LOAD=y")   # 二维码画布会被整幅展开
        self.assertNotRegex(defaults, r"(?m)^CONFIG_KJ_DEV_WIFI_")                 # 开发用的 Wi-Fi 只放本地
        kconfig = read("main/Kconfig.projbuild")
        self.assertRegex(kconfig, r'config KJ_DEV_WIFI_SSID\s+string[^\n]*\n\s+default ""')
        self.assertRegex(kconfig, r'config KJ_DEV_WIFI_PASSWORD\s+string[^\n]*\n\s+default ""')


class DirectModeContract(unittest.TestCase):
    """直连模式：ESP-NOW 只在直连时启动，NAME 帧放得进一个游戏帧，信道可配置。"""

    def test_radio_only_in_direct_mode(self):
        main = read("main/main.c")
        body = function_body(main, "flush_outbox")
        self.assertRegex(body, r"KJ_CONN_DIRECT[\s\S]*kj_radio_send[\s\S]*kj_net_send_frame")
        app_main = function_body(main, "app_main")
        self.assertRegex(app_main, r"else \{\s*start_radio\(\);", "ESP-NOW starts only in direct mode")
        self.assertIn("kj_store_get_conn()", app_main)

    def test_name_frame_fits(self):
        proto = c_defines(read("main/kj_proto.h"))
        self.assertLessEqual(proto["KJ_FRAME_HEADER"] + 3 + proto["KJ_NAME_MAX"], proto["KJ_FRAME_MAX"])
        self.assertEqual(proto["KJ_NAME_MAX_UNITS"], kj_charset.NAME_MAX_UNITS)

    def test_channel_is_configurable(self):
        kconfig = read("main/Kconfig.projbuild")
        self.assertRegex(kconfig, r"config KJ_ESPNOW_CHANNEL\s+int[^\n]*\n\s+range 1 13\n\s+default 1")
        self.assertIn("CONFIG_KJ_ESPNOW_CHANNEL", read("main/kj_radio.h"))


class HubProtocolContract(unittest.TestCase):
    def test_c_and_python_constants_match(self):
        c = c_defines(read("main/kj_proto.h") + read("main/kj_hubproto.h"))
        for name in ("KH_VERSION", "KH_HDR", "KH_PAYLOAD_MAX", "KH_PORT_HUB", "KH_PORT_DEVICE", "KH_PORT_TCP",
                     "KH_PORT_HTTP", "KJ_NAME_MAX", "KJ_REG_TOKEN_LEN", "KH_FW_LEN", "KH_NAMES_MAX", "KH_FLAG_HOST",
                     "KH_OFFER_NAME_VALID", "KH_OFFER_INCOMPATIBLE", "KH_NAME_BOT", "KH_NAME_UNKNOWN",
                     "KH_K_FRAME", "KH_K_DISCOVER", "KH_K_OFFER", "KH_K_REG", "KH_K_REG_STATE", "KH_K_NAME_GET",
                     "KH_K_NAMES", "KH_K_COUNT", "KH_ROLE_NONE", "KH_ROLE_PLAYER", "KH_ROLE_HOST",
                     "KH_REG_INVALID", "KH_REG_WAITING", "KH_REG_OPENED", "KH_REG_DONE"):
            self.assertEqual(c[name], getattr(wire, name), name)
        proto = c_defines(read("main/kj_proto.h"))
        self.assertEqual(proto["KJ_PROTO_VERSION"], wire.KJ_PROTO_VERSION)
        self.assertEqual(proto["KJ_FRAME_MAX"], wire.KJ_FRAME_MAX)
        self.assertEqual(proto["KJ_FRAME_HEADER"], wire.KJ_FRAME_HEADER)
        self.assertEqual(proto["KJ_F_ROOM"], wire.KJ_F_ROOM)
        self.assertEqual(kj_charset.NAME_MAX_BYTES, wire.KJ_NAME_MAX)

    def test_board_vocabulary_matches_hub(self):
        board_c = read("main/kj_board.c")
        table = set(re.findall(r'\{\s*"([a-z+\-]+)",\s*KJ_CMD_\w+\s*\}', board_c))
        self.assertEqual(table, set(hub_core.COMMANDS) | {"kick"})
        statuses = re.findall(r'"(\w+)"', re.search(r"names\[KJ_ST_COUNT\]\s*=\s*\{(.*?)\};", board_c, re.S).group(1))
        self.assertEqual(set(statuses), set(hub_store.STATUS_CN))
        events = re.findall(r'case KJ_EV_\w+: return "(\w+)";', board_c)
        self.assertEqual(set(events), set(hub_store.EVENT_CN))

    def test_name_charset_matches_font(self):
        manifest = json.loads(read("assets/fonts/kj_fonts.manifest.json"))
        covered = set()
        for font in manifest["fonts"]:
            if font["name"] in ("kj_name18a", "kj_name18b"):
                for part in font["ranges"].split(","):
                    lo, _, hi = part.partition("-")
                    covered.update(range(int(lo, 16), int(hi or lo, 16) + 1))
        self.assertEqual(covered, set(kj_charset.name_charset()))


class BoardProtocolContract(unittest.TestCase):
    def setUp(self):
        self.board_c = read("main/kj_board.c")
        self.html = read("tools/kj_board/index.html")

    def test_prefix_matches(self):
        prefix = re.search(r'#define KJ_BOARD_PREFIX\s+"([^"]+)"', read("main/kj_board.h")).group(1)
        self.assertEqual(prefix, "@KJ ")
        self.assertIn('"@KJ {"', self.html)
        self.assertIn("`@KJ ${cmd}\\n`", self.html)

    def test_every_command_is_reachable_from_the_board(self):
        table = re.findall(r'\{\s*"([a-z+\-]+)",\s*KJ_CMD_\w+\s*\}', self.board_c)
        self.assertEqual(set(table), {"start", "end", "new", "reset", "bot+", "bot-", "sync", "kick"})
        for word in table:
            if word == "kick":
                self.assertIn("send(`kick ${", self.html)
            elif word == "sync":
                self.assertIn('send("sync")', self.html)
            else:
                self.assertIn(f'data-cmd="{word}"', self.html)

    def test_board_understands_every_status_and_event(self):
        statuses = re.search(r"names\[KJ_ST_COUNT\]\s*=\s*\{(.*?)\};", self.board_c, re.S).group(1)
        for name in re.findall(r'"(\w+)"', statuses):
            self.assertRegex(self.html, rf"\b{name}:", f"board lacks status {name}")
        events = re.findall(r'case KJ_EV_\w+: return "(\w+)";', self.board_c)
        self.assertGreaterEqual(len(events), 23)
        for name in events:
            self.assertRegex(self.html, rf"\b{name}:\s*(\(\)|e)\s*=>", f"board lacks event text {name}")

    def test_web_pages_are_self_contained(self):
        # 看板、hub 网页、配网网页都不加载外部资源、不连外网：现场常常没有外网，也不把对局数据发出去。
        pages = [ROOT / "tools/kj_board/index.html", ROOT / "main/web/kj_prov.html", ROOT / "main/web/kj_name.html",
                 *sorted((ROOT / "tools/kj_hub/static").glob("*.html"))]
        for page in pages:
            text = page.read_text(encoding="utf-8")
            self.assertNotRegex(text, r"https?://", page.name)
            self.assertNotRegex(text, r"XMLHttpRequest|WebSocket", page.name)
            for call in re.findall(r"(?:fetch|EventSource)\(\s*([^,)]+)", text):
                # 只允许相对路径的字面量，或保存了相对路径的局部变量（path / url）
                self.assertRegex(call.strip(), r'^(["`][A-Za-z0-9_./?=${}+-]*["`]|path|url)$', f"{page.name}: {call}")
        # 浏览器存储只放名字与偏好，读写都包在 try/catch 里
        self.assertIn("try { if (val === undefined) return localStorage.getItem(key)", self.html)
        # 配网页只用 textContent 渲染 SSID（SSID 来自空中，不可信）
        prov = read("main/web/kj_prov.html")
        self.assertIn("name.textContent = ap.s", prov)
        self.assertNotRegex(prov, r"innerHTML\s*=\s*[^\"';]*ap\.")
        # 昵称登记页（直连模式）：设备返回的内容一律按纯文本显示
        name_page = read("main/web/kj_name.html")
        self.assertNotIn("innerHTML", name_page)
        self.assertIn('$("dev").textContent', name_page)
        self.assertIn("m.textContent = text", name_page)


if __name__ == "__main__":
    unittest.main()
