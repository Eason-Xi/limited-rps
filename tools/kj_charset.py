#!/usr/bin/env python3
"""限定猜拳的昵称字符集：设备上的昵称字库 kj_name18 与电脑 hub 的昵称校验共用这一份定义
（直连模式没有电脑 hub，设备在热点登记页里直接查 kj_name18 字库，结果与这里一致）。

字符集 = 可打印 ASCII + 间隔号 · + GB2312 一、二级全部 6763 个汉字 + 一小份常见的人名补充字
（GB2312 没有、但名字里常见的字，例如 喆 堃 玥）。只用 Python 标准库（gb2312 编解码器）。

  python3 tools/kj_charset.py            打印字符数与补充字
  python3 tools/kj_charset.py 小明 Amy   检查昵称里有没有设备显示不了的字
"""

from __future__ import annotations

import sys
from functools import lru_cache

NAME_MAX_BYTES = 24    # 与 main/kj_proto.h 的 KJ_NAME_MAX 一致（UTF-8 字节）
NAME_MAX_UNITS = 16    # 与 main/kj_proto.h 的 KJ_NAME_MAX_UNITS 一致：汉字记 2、ASCII 记 1（约 8 个汉字）

# GB2312 没收、但名字里常见的字。已经在 GB2312 里的会被自动忽略。
EXTRA_NAME_CHARS = (
    "喆堃玥昇祎赟彧垚犇淏珺翀晞镕頔芃昀琤珩玚瑄祐禛燚婳嬿珈珞琬瑀瑭璟甦翊堉暻浠洺澍樾桉歆昶暄煊祉"
    "珏琮琰璋璞瑾瑜璇璐璨骐宸熠焱煜灏沐泓澄湘溪潇瀚栩楠槿棠朔昕曜弈彦恺懿昱晔栀桐梓楷榕歆汐沁沅淼"
    "漪澜炜焕熹玖玟琨禾筝绮芊芷苓茜蕾薰蘅轩逸钧锦飒馥鸿麒骞筠祺禧翎胤芮苒菁蕴隽雯霏靖韬颢骁"
)


def _gb2312_hanzi() -> set[int]:
    points = set()
    for hi in range(0xB0, 0xF8):           # 16~87 区：一级（3755）+ 二级（3008）汉字
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes((hi, lo)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            points.add(ord(ch))
    return points


@lru_cache(maxsize=None)
def gb2312_hanzi() -> frozenset[int]:
    return frozenset(_gb2312_hanzi())


@lru_cache(maxsize=None)
def extra_chars() -> tuple[int, ...]:
    base = gb2312_hanzi()
    return tuple(sorted({ord(c) for c in EXTRA_NAME_CHARS} - base))


@lru_cache(maxsize=None)
def name_charset() -> tuple[int, ...]:
    points = set(range(0x20, 0x7F))
    points.add(0xB7)                       # ·（外文名的间隔号）
    points.update(gb2312_hanzi())
    points.update(extra_chars())
    return tuple(sorted(points))


@lru_cache(maxsize=None)
def _charset_set() -> frozenset[int]:
    return frozenset(name_charset())


def unsupported(name: str) -> list[str]:
    """昵称里设备显示不了的字（去重、保持出现顺序）。"""
    seen: list[str] = []
    allowed = _charset_set()
    for ch in name:
        if ord(ch) not in allowed and ch not in seen:
            seen.append(ch)
    return seen


def display_units(name: str) -> int:
    return sum(1 if ord(ch) < 0x80 else 2 for ch in name)


def main(argv: list[str]) -> int:
    if not argv:
        print(f"name charset: {len(name_charset())} code points "
              f"({len(gb2312_hanzi())} GB2312 hanzi, {len(extra_chars())} extras: "
              f"{''.join(chr(p) for p in extra_chars())})")
        return 0
    bad = 0
    for name in argv:
        missing = unsupported(name)
        if missing:
            bad += 1
            print(f"{name}: unsupported {' '.join(missing)}")
        else:
            print(f"{name}: OK ({len(name.encode('utf-8'))} bytes, {display_units(name)} units)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
