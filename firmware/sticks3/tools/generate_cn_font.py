#!/usr/bin/env python3
"""Regenerate the bounded LVGL font used by the StickS3 product UI."""

from __future__ import annotations

import argparse
import math
import pathlib
import re
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
UI_SOURCE_DIRECTORIES = (
    ROOT / "app" / "ui",
    ROOT / "app" / "input",
    ROOT / "components" / "microstick_state_model",
)

# Keep a small, explicit reserve for a future Chinese Control Center locale.
# The active Control Center is English, but retaining these glyphs avoids
# a missing-glyph firmware if the copy is localized again before regeneration.
CONTROL_LOCALIZATION_RESERVE = (
    "控制中心批准拒绝快速模式创建分支选择智能体页面导航"
    "用量详情设备设置规划返回前进侧栏确认取消"
    "最后同步状态当前过期在线离线可用读取中？"
)

TEXT_WIDTH_LIMITS = {
    12: (
        ("AG6·状态未知", 96),
        ("AG6 · 状态未知", 112),
        ("蓝键选择 · 侧键执行", 125),
        ("侧键确认拒绝", 125),
        ("已选择 Agent 6", 111),
        ("取消不可用", 111),
        ("已切换 Sidebar", 111),
        ("长按侧键返回", 125),
        ("Micro 未连接", 111),
        ("USB 麦克风", 64),
        ("最后同步", 64),
        ("刚刚", 53),
        ("正常", 53),
        ("未连接", 53),
        ("读取中", 53),
        ("ChatGPT 处理中", 125),
    ),
    16: (
        ("AG6·未分配", 96),
        ("AG6·思考中", 96),
        ("AG6·已完成", 96),
        ("AG6·待批准", 96),
        ("AG6·待响应", 96),
        ("Navigation", 112),
        ("选择 Agent", 82),
        ("确认拒绝？", 125),
        ("蓝键取消", 125),
        ("正在识别", 125),
    ),
}


def ui_symbols() -> str:
    symbols: set[str] = set()
    for directory in UI_SOURCE_DIRECTORIES:
        for path in sorted(directory.rglob("*")):
            if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
                continue
            symbols.update(character for character in path.read_text()
                           if ord(character) >= 0x80)
    symbols.update(CONTROL_LOCALIZATION_RESERVE)
    return "".join(sorted(symbols))


def validate_text_widths(generated: str, expected_size: int) -> None:
    glyph_section = re.search(
        r"static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc\[\] = "
        r"\{(.*?)\n\};",
        generated,
        re.DOTALL,
    )
    unicode_list = re.search(
        r"static const uint16_t unicode_list_1\[\] = \{(.*?)\};",
        generated,
        re.DOTALL,
    )
    sparse_cmap = re.search(
        r"\.range_start = (\d+), \.range_length = \d+, "
        r"\.glyph_id_start = (\d+),\s*"
        r"\.unicode_list = unicode_list_1",
        generated,
    )
    if glyph_section is None or unicode_list is None or sparse_cmap is None:
        raise RuntimeError("generated LVGL font cannot be measured")

    advances = [
        int(value)
        for value in re.findall(r"\.adv_w = (\d+)", glyph_section.group(1))
    ]
    glyphs = {codepoint: 1 + codepoint - 0x20
              for codepoint in range(0x20, 0x7F)}
    range_start = int(sparse_cmap.group(1))
    glyph_id_start = int(sparse_cmap.group(2))
    for index, value in enumerate(
        re.findall(r"0x([0-9a-fA-F]+)", unicode_list.group(1))
    ):
        glyphs[range_start + int(value, 16)] = glyph_id_start + index

    for text, limit in TEXT_WIDTH_LIMITS.get(expected_size, ()):
        width = math.ceil(sum(advances[glyphs[ord(character)]]
                              for character in text) / 16)
        if width > limit:
            raise RuntimeError(
                f"{expected_size}px UI label exceeds {limit}px: "
                f"{text!r} measures {width}px"
            )


def validate_generated_font(generated: str, expected_size: int) -> None:
    if f"Size: {expected_size} px" not in generated:
        raise RuntimeError(
            f"generated LVGL font is not the expected {expected_size}px size"
        )
    if ".bitmap_format = 0" not in generated:
        raise RuntimeError("generated LVGL font must use uncompressed bitmaps")

    unicode_list = re.search(
        r"static const uint16_t unicode_list_1\[\] = \{(.*?)\};",
        generated,
        re.DOTALL,
    )
    sparse_cmap = re.search(
        r"\{\s*\.range_start = (\d+),[^{}]*"
        r"\.unicode_list = unicode_list_1",
        generated,
        re.DOTALL,
    )
    if unicode_list is None or sparse_cmap is None:
        raise RuntimeError("generated LVGL font is missing its sparse Unicode map")

    range_start = int(sparse_cmap.group(1))
    codepoints = set(range(0x20, 0x7F))
    codepoints.update(
        range_start + int(value, 16)
        for value in re.findall(r"0x([0-9a-fA-F]+)", unicode_list.group(1))
    )
    missing = [character for character in ui_symbols()
               if ord(character) not in codepoints]
    if missing:
        details = " ".join(f"{character}=U+{ord(character):04X}"
                           for character in missing)
        raise RuntimeError(f"generated LVGL font is missing UI glyphs: {details}")
    validate_text_widths(generated, expected_size)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--font",
                        help="Source Han Sans CN Regular OTF path")
    parser.add_argument("--converter", default="lv_font_conv",
                        help="lv_font_conv 1.5.3 executable")
    parser.add_argument("--size", type=int, default=16,
                        help="Generated font pixel size")
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--check", action="store_true",
                        help="Validate the checked-in font without regenerating it")
    arguments = parser.parse_args()
    if arguments.size <= 0:
        parser.error("--size must be positive")
    output = arguments.output or (
        ROOT / "generated" / f"microstick_cn_{arguments.size}.c"
    )

    if arguments.check:
        validate_generated_font(output.read_text(), arguments.size)
        print(
            f"font_ok size={arguments.size} glyphs={len(ui_symbols())} "
            "bitmap_format=0"
        )
        return
    if not arguments.font:
        parser.error("--font is required unless --check is used")

    with tempfile.NamedTemporaryFile(suffix=".c") as temporary:
        subprocess.run([
            arguments.converter,
            "--size", str(arguments.size),
            "--bpp", "4",
            "--format", "lvgl",
            "--font", arguments.font,
            "--range", "0x20-0x7e",
            "--symbols", ui_symbols(),
            # LVGL is built without LV_USE_FONT_COMPRESSED. Without this flag
            # lv_font_conv emits bitmap_format=1 and LVGL renders every custom
            # glyph as a missing-glyph box at runtime.
            "--no-compress",
            "--no-kerning",
            "--lv-font-name", f"microstick_cn_{arguments.size}",
            "--output", temporary.name,
        ], check=True)
        generated = pathlib.Path(temporary.name).read_text()

    validate_generated_font(generated, arguments.size)

    generated = re.sub(
        r" \* Opts: .*\n",
        " * Source font: Source Han Sans CN Regular, SIL Open Font License 1.1.\n"
        " * Printable ASCII plus the non-ASCII product UI glyphs are generated by\n"
        " * firmware/sticks3/tools/generate_cn_font.py with lv_font_conv 1.5.3.\n",
        generated,
        count=1,
    )
    generated = generated.replace('#include "lvgl/lvgl.h"',
                                  '#include "lvgl.h"')
    output.write_text(generated.rstrip() + "\n")


if __name__ == "__main__":
    main()
