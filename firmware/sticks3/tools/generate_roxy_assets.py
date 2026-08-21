#!/usr/bin/env python3
"""Generate compact StickS3 Roxy animation assets from a Codex v2 pet atlas.

The firmware stores 96x104 palette-indexed frames using a small PackBits-style
codec.  Generated frames decode to RGB565 at runtime, which keeps the original
pixel-art look while fitting the StickS3 application partition.
"""

from __future__ import annotations

import argparse
import hashlib
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw, ImageFont


ATLAS_WIDTH = 1536
ATLAS_HEIGHT = 2288
CELL_WIDTH = 192
CELL_HEIGHT = 208
FRAME_WIDTH = 96
FRAME_HEIGHT = 104
OPAQUE_ALPHA_THRESHOLD = 64
PALETTE_COLOR_COUNT = 31
SCREEN_BACKGROUND = (5, 6, 8)
EXPECTED_ATLAS_SHA256 = "f88a7e1140a2d540d6703716981e4c715b6e5ffb1c26fac6413a8b6f07f15f7e"


@dataclass(frozen=True)
class AnimationSpec:
    enum_name: str
    symbol_name: str
    display_name: str
    atlas_row: int
    frame_count: int
    duration_ms: int


ANIMATIONS = (
    AnimationSpec("MICROSTICK_ROXY_IDLE", "idle", "IDLE", 0, 6, 360),
    AnimationSpec("MICROSTICK_ROXY_RUNNING", "running", "RUNNING", 7, 6, 150),
    AnimationSpec("MICROSTICK_ROXY_WAITING", "waiting", "APPROVAL", 6, 6, 300),
    AnimationSpec("MICROSTICK_ROXY_DONE", "done", "DONE", 3, 4, 220),
    AnimationSpec("MICROSTICK_ROXY_ERROR", "error", "ERROR", 5, 8, 230),
)


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--atlas",
        type=Path,
        default=Path.home() / ".codex/pets/roxy-pixel/spritesheet.webp",
        help="Roxy v2 spritesheet.webp path",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=script_dir.parent / "generated",
        help="Directory for microstick_roxy_assets.c/.h",
    )
    parser.add_argument(
        "--qa-dir",
        type=Path,
        help="Optional directory for deterministic frame and screen previews",
    )
    parser.add_argument(
        "--preview-font",
        type=Path,
        help="Optional Source Han Sans OTF used for Chinese QA labels",
    )
    parser.add_argument(
        "--allow-atlas-hash-mismatch",
        action="store_true",
        help="Generate from a different v2 atlas after dimensions are validated",
    )
    return parser.parse_args()


def load_atlas(path: Path, allow_hash_mismatch: bool) -> Image.Image:
    raw = path.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != EXPECTED_ATLAS_SHA256 and not allow_hash_mismatch:
        raise SystemExit(
            "Roxy atlas hash mismatch: "
            f"expected {EXPECTED_ATLAS_SHA256}, got {digest}. "
            "Pass --allow-atlas-hash-mismatch only for an intentional replacement."
        )
    atlas = Image.open(path).convert("RGBA")
    if atlas.size != (ATLAS_WIDTH, ATLAS_HEIGHT):
        raise SystemExit(
            f"Expected a {ATLAS_WIDTH}x{ATLAS_HEIGHT} v2 atlas, got {atlas.size[0]}x{atlas.size[1]}"
        )
    print(f"atlas_sha256={digest}")
    return atlas


def extract_frames(atlas: Image.Image) -> dict[str, list[Image.Image]]:
    animations: dict[str, list[Image.Image]] = {}
    for spec in ANIMATIONS:
        frames: list[Image.Image] = []
        for column in range(spec.frame_count):
            left = column * CELL_WIDTH
            top = spec.atlas_row * CELL_HEIGHT
            frame = atlas.crop((left, top, left + CELL_WIDTH, top + CELL_HEIGHT))
            frames.append(frame.resize((FRAME_WIDTH, FRAME_HEIGHT), Image.Resampling.NEAREST))
        animations[spec.symbol_name] = frames
    return animations


def image_data(image: Image.Image):
    if hasattr(image, "get_flattened_data"):
        return image.get_flattened_data()
    return image.getdata()


def build_palette(frames: Iterable[Image.Image]) -> list[tuple[int, int, int]]:
    opaque_pixels: list[tuple[int, int, int]] = []
    for frame in frames:
        for red, green, blue, alpha in image_data(frame):
            if alpha >= OPAQUE_ALPHA_THRESHOLD:
                opaque_pixels.append((red, green, blue))
    if not opaque_pixels:
        raise SystemExit("Roxy atlas did not contain any opaque pixels")

    sample_width = 1024
    sample_height = math.ceil(len(opaque_pixels) / sample_width)
    padded_pixels = opaque_pixels + [opaque_pixels[-1]] * (sample_width * sample_height - len(opaque_pixels))
    sample = Image.new("RGB", (sample_width, sample_height))
    sample.putdata(padded_pixels)
    quantized = sample.quantize(
        colors=PALETTE_COLOR_COUNT,
        method=Image.Quantize.MEDIANCUT,
        dither=Image.Dither.NONE,
    )
    raw_palette = quantized.getpalette()
    palette = [tuple(raw_palette[index : index + 3]) for index in range(0, PALETTE_COLOR_COUNT * 3, 3)]

    return palette


def index_frame(frame: Image.Image, palette: list[tuple[int, int, int]]) -> list[int]:
    nearest_cache: dict[tuple[int, int, int], int] = {}
    indexed: list[int] = []
    for red, green, blue, alpha in image_data(frame):
        if alpha < OPAQUE_ALPHA_THRESHOLD:
            indexed.append(0)
            continue
        color = (red, green, blue)
        palette_index = nearest_cache.get(color)
        if palette_index is None:
            palette_index = min(
                range(len(palette)),
                key=lambda index: sum(
                    (color[channel] - palette[index][channel]) ** 2 for channel in range(3)
                ),
            )
            nearest_cache[color] = palette_index
        indexed.append(palette_index + 1)
    return indexed


def encode_packbits(values: list[int]) -> bytes:
    encoded = bytearray()
    index = 0
    while index < len(values):
        run_end = index + 1
        while run_end < len(values) and values[run_end] == values[index] and run_end - index < 128:
            run_end += 1
        if run_end - index >= 3:
            encoded.append(0x80 | (run_end - index - 1))
            encoded.append(values[index])
            index = run_end
            continue

        literal_start = index
        index += 1
        while index < len(values) and index - literal_start < 128:
            run_end = index + 1
            while run_end < len(values) and values[run_end] == values[index] and run_end - index < 128:
                run_end += 1
            if run_end - index >= 3:
                break
            index += 1
        encoded.append(index - literal_start - 1)
        encoded.extend(values[literal_start:index])
    return bytes(encoded)


def decode_packbits(encoded: bytes) -> list[int]:
    decoded: list[int] = []
    index = 0
    while index < len(encoded):
        control = encoded[index]
        index += 1
        run_length = (control & 0x7F) + 1
        if control & 0x80:
            if index >= len(encoded):
                raise ValueError("truncated repeated run")
            decoded.extend([encoded[index]] * run_length)
            index += 1
        else:
            if index + run_length > len(encoded):
                raise ValueError("truncated literal run")
            decoded.extend(encoded[index : index + run_length])
            index += run_length
    if len(decoded) != FRAME_WIDTH * FRAME_HEIGHT:
        raise ValueError(f"decoded {len(decoded)} pixels; expected {FRAME_WIDTH * FRAME_HEIGHT}")
    return decoded


def rgb565(color: tuple[int, int, int]) -> int:
    red, green, blue = color
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def format_bytes(data: bytes, indent: str = "    ") -> str:
    lines = []
    for offset in range(0, len(data), 16):
        chunk = data[offset : offset + 16]
        lines.append(indent + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    return "\n".join(lines)


def generate_header() -> str:
    enum_lines = [f"    {spec.enum_name}," for spec in ANIMATIONS]
    return f"""// Generated by tools/generate_roxy_assets.py. Do not edit manually.
// The original local Codex atlas is not embedded in the repository or release package.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define MICROSTICK_ROXY_FRAME_WIDTH {FRAME_WIDTH}
#define MICROSTICK_ROXY_FRAME_HEIGHT {FRAME_HEIGHT}
#define MICROSTICK_ROXY_FRAME_PIXELS (MICROSTICK_ROXY_FRAME_WIDTH * MICROSTICK_ROXY_FRAME_HEIGHT)

typedef enum {{
{chr(10).join(enum_lines)}
    MICROSTICK_ROXY_STATE_COUNT,
}} microstick_roxy_state_t;

size_t microstick_roxy_frame_count(microstick_roxy_state_t state);
uint32_t microstick_roxy_frame_duration_ms(microstick_roxy_state_t state);
bool microstick_roxy_decode_frame(microstick_roxy_state_t state, size_t frame_index,
                            uint16_t *destination, size_t destination_pixels);

#ifdef __cplusplus
}}
#endif
"""


def generate_source(
    palette: list[tuple[int, int, int]],
    encoded_animations: dict[str, list[bytes]],
) -> str:
    arrays: list[str] = []
    frame_tables: list[str] = []
    animation_rows: list[str] = []

    for spec in ANIMATIONS:
        frames = encoded_animations[spec.symbol_name]
        for index, encoded in enumerate(frames):
            symbol = f"s_roxy_{spec.symbol_name}_{index}"
            arrays.append(
                f"static const uint8_t {symbol}[] = {{\n{format_bytes(encoded)}\n}};"
            )
        frame_tables.append(
            f"static const roxy_frame_t s_roxy_{spec.symbol_name}_frames[] = {{\n"
            + "\n".join(
                f"    {{s_roxy_{spec.symbol_name}_{index}, sizeof(s_roxy_{spec.symbol_name}_{index})}},"
                for index in range(len(frames))
            )
            + "\n};"
        )
        animation_rows.append(
            f"    [{spec.enum_name}] = {{s_roxy_{spec.symbol_name}_frames, "
            f"sizeof(s_roxy_{spec.symbol_name}_frames) / sizeof(s_roxy_{spec.symbol_name}_frames[0]), "
            f"{spec.duration_ms}}},"
        )

    palette_values = [rgb565(SCREEN_BACKGROUND)] + [rgb565(color) for color in palette]
    palette_lines = []
    for offset in range(0, len(palette_values), 8):
        chunk = palette_values[offset : offset + 8]
        palette_lines.append("    " + ", ".join(f"0x{value:04x}" for value in chunk) + ",")

    return f"""// Generated by tools/generate_roxy_assets.py. Do not edit manually.
// The original local Codex atlas is not embedded in the repository or release package.
#include "microstick_roxy_assets.h"

typedef struct {{
    const uint8_t *data;
    size_t size;
}} roxy_frame_t;

typedef struct {{
    const roxy_frame_t *frames;
    size_t frame_count;
    uint32_t frame_duration_ms;
}} roxy_animation_t;

static const uint16_t s_roxy_palette[] = {{
{chr(10).join(palette_lines)}
}};

{chr(10).join(arrays)}

{chr(10).join(frame_tables)}

static const roxy_animation_t s_roxy_animations[MICROSTICK_ROXY_STATE_COUNT] = {{
{chr(10).join(animation_rows)}
}};

static const roxy_animation_t *animation_for_state(microstick_roxy_state_t state)
{{
    if (state < 0 || state >= MICROSTICK_ROXY_STATE_COUNT) {{
        return NULL;
    }}
    return &s_roxy_animations[state];
}}

size_t microstick_roxy_frame_count(microstick_roxy_state_t state)
{{
    const roxy_animation_t *animation = animation_for_state(state);
    return animation ? animation->frame_count : 0;
}}

uint32_t microstick_roxy_frame_duration_ms(microstick_roxy_state_t state)
{{
    const roxy_animation_t *animation = animation_for_state(state);
    return animation ? animation->frame_duration_ms : 0;
}}

bool microstick_roxy_decode_frame(microstick_roxy_state_t state, size_t frame_index,
                            uint16_t *destination, size_t destination_pixels)
{{
    const roxy_animation_t *animation = animation_for_state(state);
    if (!animation || !destination || destination_pixels < MICROSTICK_ROXY_FRAME_PIXELS ||
        frame_index >= animation->frame_count) {{
        return false;
    }}

    const roxy_frame_t *frame = &animation->frames[frame_index];
    size_t source_index = 0;
    size_t destination_index = 0;
    while (source_index < frame->size && destination_index < MICROSTICK_ROXY_FRAME_PIXELS) {{
        const uint8_t control = frame->data[source_index++];
        const size_t run_length = (control & 0x7f) + 1;
        if (control & 0x80) {{
            if (source_index >= frame->size ||
                destination_index + run_length > MICROSTICK_ROXY_FRAME_PIXELS) {{
                return false;
            }}
            const uint8_t palette_index = frame->data[source_index++];
            if (palette_index >= sizeof(s_roxy_palette) / sizeof(s_roxy_palette[0])) {{
                return false;
            }}
            for (size_t index = 0; index < run_length; ++index) {{
                destination[destination_index++] = s_roxy_palette[palette_index];
            }}
        }} else {{
            if (source_index + run_length > frame->size ||
                destination_index + run_length > MICROSTICK_ROXY_FRAME_PIXELS) {{
                return false;
            }}
            for (size_t index = 0; index < run_length; ++index) {{
                const uint8_t palette_index = frame->data[source_index++];
                if (palette_index >= sizeof(s_roxy_palette) / sizeof(s_roxy_palette[0])) {{
                    return false;
                }}
                destination[destination_index++] = s_roxy_palette[palette_index];
            }}
        }}
    }}
    return source_index == frame->size && destination_index == MICROSTICK_ROXY_FRAME_PIXELS;
}}
"""


def palette_image_from_colors(palette: list[tuple[int, int, int]]) -> Image.Image:
    image = Image.new("P", (1, 1))
    flattened = list(SCREEN_BACKGROUND) + [channel for color in palette for channel in color]
    image.putpalette(flattened + [0] * (768 - len(flattened)))
    return image


def decoded_image(encoded: bytes, palette: list[tuple[int, int, int]]) -> Image.Image:
    image = Image.new("P", (FRAME_WIDTH, FRAME_HEIGHT))
    image.putdata(decode_packbits(encoded))
    image.putpalette(palette_image_from_colors(palette).getpalette())
    return image.convert("RGB")


def write_qa(
    qa_dir: Path,
    palette: list[tuple[int, int, int]],
    encoded_animations: dict[str, list[bytes]],
    preview_font: Path | None,
) -> None:
    qa_dir.mkdir(parents=True, exist_ok=True)
    row_height = FRAME_HEIGHT * 2 + 30
    max_frames = max(spec.frame_count for spec in ANIMATIONS)
    sheet = Image.new("RGB", (max_frames * FRAME_WIDTH * 2, len(ANIMATIONS) * row_height), (5, 6, 8))
    draw = ImageDraw.Draw(sheet)
    for row, spec in enumerate(ANIMATIONS):
        draw.text((8, row * row_height + 7), spec.display_name, fill=(244, 245, 247))
        for column, encoded in enumerate(encoded_animations[spec.symbol_name]):
            frame = decoded_image(encoded, palette).resize(
                (FRAME_WIDTH * 2, FRAME_HEIGHT * 2), Image.Resampling.NEAREST
            )
            sheet.paste(frame, (column * FRAME_WIDTH * 2, row * row_height + 28))
    sheet.save(qa_dir / "roxy-device-frames.png")

    def font(size: int) -> ImageFont.ImageFont:
        if preview_font:
            return ImageFont.truetype(str(preview_font), size=size)
        return ImageFont.load_default()

    font_8 = font(8)
    font_9 = font(9)
    font_10 = font(10)
    font_12 = font(12)
    font_14 = font(14)
    font_18 = font(18)
    white = (243, 244, 246)
    secondary = (157, 163, 173)
    muted = (102, 107, 116)
    blue = (77, 130, 255)
    green = (85, 217, 139)
    background = (5, 6, 8)

    def text_right(draw: ImageDraw.ImageDraw, x: int, y: int, value: str,
                   fill: tuple[int, int, int], text_font: ImageFont.ImageFont) -> None:
        bounds = draw.textbbox((0, 0), value, font=text_font)
        draw.text((x - (bounds[2] - bounds[0]), y), value, fill=fill,
                  font=text_font)

    def top_bar(screen: Image.Image, title: str | None = None,
                usb: bool = False,
                battery_percentage: int = 86) -> ImageDraw.ImageDraw:
        draw = ImageDraw.Draw(screen)
        if title:
            draw.text((8, 4), title, fill=white, font=font_10)
            text_right(draw, 127, 5, "86%", white, font_12)
        else:
            status_color = green if usb else blue
            draw.ellipse((5, 8, 12, 15), fill=status_color)
            draw.text((17, 4), "USB" if usb else "BLE", fill=white,
                      font=font_12)
            text_right(draw, 97, 5, f"{battery_percentage}%", white,
                       font_12)
            draw.rounded_rectangle((101, 5, 127, 18), radius=4, outline=white)
            fill_width = (battery_percentage * 23 + 99) // 100
            if fill_width > 0:
                draw.rounded_rectangle((102, 6, 101 + fill_width, 15),
                                       radius=2, fill=green)
            draw.rectangle((129, 9, 131, 14), fill=white)
        return draw

    def save_preview(screen: Image.Image, name: str) -> None:
        screen.resize((405, 720), Image.Resampling.NEAREST).save(
            qa_dir / f"{name}.png"
        )

    roxy_idle = decoded_image(encoded_animations["idle"][0], palette)
    roxy_waiting = decoded_image(encoded_animations["waiting"][0], palette)

    home = Image.new("RGB", (135, 240), background)
    draw = top_bar(home)
    home.paste(roxy_idle, (19, 31))
    draw.text((8, 140), "AG2·思考中", fill=white, font=font_12)
    text_right(draw, 127, 142, "2/6", secondary, font_10)
    slot_colors = (blue, blue, (59, 64, 73), green, (59, 64, 73),
                   (59, 64, 73))
    for index, color in enumerate(slot_colors):
        x = 14 + index * 20
        draw.ellipse((x - 4, 167, x + 4, 175), fill=color)
        if index == 1:
            draw.ellipse((x - 7, 164, x + 7, 178), outline=white)
    draw.rounded_rectangle((8, 188, 127, 232), radius=8,
                           fill=(14, 16, 20), outline=(34, 37, 43))
    draw.text((15, 195), "7D", fill=white, font=font_10)
    text_right(draw, 120, 195, "41%", white, font_12)
    draw.rounded_rectangle((15, 219, 120, 225), radius=3,
                           fill=(42, 45, 51))
    draw.rounded_rectangle((15, 219, 58, 225), radius=3, fill=blue)
    save_preview(home, "home")

    home_battery_full = home.copy()
    full_draw = ImageDraw.Draw(home_battery_full)
    full_draw.rectangle((0, 0, 134, 29), fill=background)
    top_bar(home_battery_full, battery_percentage=100)
    save_preview(home_battery_full, "home-battery-100")

    home_usb = home.copy()
    usb_draw = ImageDraw.Draw(home_usb)
    usb_draw.rectangle((0, 0, 56, 29), fill=background)
    top_bar(home_usb, usb=True)
    save_preview(home_usb, "home-usb")

    control = Image.new("RGB", (135, 240), background)
    draw = top_bar(control, "控制")
    menu = ("Approve", "Decline", "Fast", "Fork", "Agents", "Navigation",
            "Usage", "Device")
    for index, item in enumerate(menu):
        y = 31 + index * 20
        if index == 0:
            draw.rounded_rectangle((8, y - 2, 127, y + 17), radius=5,
                                   fill=(49, 93, 206))
        draw.text((14, y), item, fill=white if index == 0 else secondary,
                  font=font_9)
    draw.text((17, 220), "蓝键选择 · 侧键执行", fill=muted, font=font_8)
    save_preview(control, "control-center")

    voice = Image.new("RGB", (135, 240), background)
    draw = top_bar(voice)
    voice.paste(roxy_waiting, (19, 31))
    draw.text((45, 142), "正在聆听", fill=white, font=font_10)
    heights = (8, 19, 31, 19, 8)
    for index, height in enumerate(heights):
        x = 39 + index * 14
        draw.rounded_rectangle((x, 202 - height, x + 5, 202), radius=2,
                               fill=white)
    draw.text((48, 219), "松开结束", fill=secondary, font=font_9)
    save_preview(voice, "voice-overlay")

    def list_preview(title: str, items: tuple[str, ...], name: str) -> Image.Image:
        screen = Image.new("RGB", (135, 240), background)
        menu_draw = top_bar(screen, title)
        for index, item in enumerate(items):
            y = 31 + index * 20
            if index == 0:
                menu_draw.rounded_rectangle((8, y - 2, 127, y + 16), radius=5,
                                            fill=(49, 93, 206))
            menu_draw.text((14, y), item,
                           fill=white if index == 0 else secondary,
                           font=font_8)
        menu_draw.text((17, 220), "蓝键选择 · 侧键执行", fill=muted,
                       font=font_8)
        save_preview(screen, name)
        return screen

    agent_menu = list_preview(
        "选择 Agent",
        ("AG1 · 空闲", "AG2 · 思考中", "AG3 · 未分配",
         "AG4 · 已完成", "AG5 · 未分配", "AG6 · 未分配"),
        "agent-menu",
    )
    navigation = list_preview(
        "导航", ("Plan", "Back", "Forward", "Sidebar"), "navigation-menu"
    )

    usage_detail = Image.new("RGB", (135, 240), background)
    draw = top_bar(usage_detail, "用量详情")
    for index, (key, value) in enumerate((("7D 剩余", "41%"),
                                          ("重置", "2天 6时"),
                                          ("最后同步", "刚刚"),
                                          ("状态", "正常"))):
        y = 35 + index * 27
        draw.text((8, y), key, fill=secondary, font=font_8)
        text_right(draw, 127, y, value, white, font_8)
    hint = "长按侧键返回"
    hint_bounds = draw.textbbox((0, 0), hint, font=font_8)
    hint_x = (135 - (hint_bounds[2] - hint_bounds[0])) // 2
    draw.text((hint_x, 219), hint, fill=muted, font=font_8)
    save_preview(usage_detail, "usage-detail")

    device_settings = Image.new("RGB", (135, 240), background)
    draw = top_bar(device_settings, "设备设置")
    for index, (key, value) in enumerate((("Micro BLE", "已连接"),
                                          ("USB 麦克风", "可用"),
                                          ("电量状态", "正常"))):
        y = 35 + index * 27
        draw.text((8, y), key, fill=secondary, font=font_8)
        text_right(draw, 127, y, value, white, font_8)
    draw.text((hint_x, 219), hint, fill=muted, font=font_8)
    save_preview(device_settings, "device-settings")

    decline = Image.new("RGB", (135, 240), background)
    draw = top_bar(decline, "确认")
    draw.text((31, 74), "确认拒绝？", fill=(255, 90, 95), font=font_10)
    draw.text((32, 126), "侧键确认拒绝", fill=white, font=font_8)
    draw.text((45, 172), "蓝键取消", fill=secondary, font=font_9)
    save_preview(decline, "decline-confirm")

    screens = (home, home_usb, control, voice, agent_menu, navigation,
               usage_detail, device_settings, decline)
    contact = Image.new("RGB", (405 * 3, 720 * 3), background)
    for index, screen in enumerate(screens):
        contact.paste(screen.resize((405, 720), Image.Resampling.NEAREST),
                      ((index % 3) * 405, (index // 3) * 720))
    contact.save(qa_dir / "microstick-v1-ui-previews.png")


def main() -> None:
    args = parse_args()
    atlas = load_atlas(args.atlas.expanduser().resolve(), args.allow_atlas_hash_mismatch)
    animations = extract_frames(atlas)
    all_frames = [frame for spec in ANIMATIONS for frame in animations[spec.symbol_name]]
    palette = build_palette(all_frames)

    encoded_animations: dict[str, list[bytes]] = {}
    total_bytes = 0
    for spec in ANIMATIONS:
        encoded_frames = []
        for frame in animations[spec.symbol_name]:
            indexed = index_frame(frame, palette)
            encoded = encode_packbits(indexed)
            if decode_packbits(encoded) != indexed:
                raise SystemExit(f"PackBits verification failed for {spec.symbol_name}")
            encoded_frames.append(encoded)
            total_bytes += len(encoded)
        encoded_animations[spec.symbol_name] = encoded_frames
        print(
            f"animation={spec.symbol_name} frames={len(encoded_frames)} "
            f"bytes={sum(len(frame) for frame in encoded_frames)} duration_ms={spec.duration_ms}"
        )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "microstick_roxy_assets.h").write_text(generate_header(), encoding="utf-8")
    (args.output_dir / "microstick_roxy_assets.c").write_text(
        generate_source(palette, encoded_animations), encoding="utf-8"
    )
    if args.qa_dir:
        write_qa(args.qa_dir.resolve(), palette, encoded_animations,
                 args.preview_font.resolve() if args.preview_font else None)
    print(f"encoded_total_bytes={total_bytes}")
    print(f"generated_dir={args.output_dir.resolve()}")


if __name__ == "__main__":
    main()
