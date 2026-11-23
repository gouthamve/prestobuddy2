#!/usr/bin/env python3
"""Asset compiler: mascot parts, icons and fonts -> LVGL C sources in core/gen/.

Runs inside the toolchain container (`make assets`). The output is committed
so the firmware and simulator build without rerunning it.
"""
import io
import os
import subprocess
import sys
import tempfile

import cairosvg
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import mascot_art  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "core", "gen")
FONT_VF = os.path.join(ROOT, "assets", "fonts", "Nunito-VF.ttf")

# Mascot sizes: fraction of the 240x360 design canvas.
MASCOT_SETS = {"large": 0.80, "small": 0.44}
ORIGIN = (120, 347)  # feet centre on the design canvas

# (name, weight, size, glyph ranges). Latin-1 so goals can carry accents.
TEXT = "0x20-0x7E,0xA0-0xFF,0x2022,0x2026,0x2014,0x2019"
FONTS = [
    ("ui_16", 600, 16, TEXT),
    ("ui_20", 600, 20, TEXT),
    ("ui_24", 700, 24, TEXT),
    ("title_32", 800, 32, TEXT),
    ("clock_48", 800, 48, "0x20,0x2D,0x30-0x3A"),
    ("timer_72", 800, 72, "0x30-0x3A"),
]

W = "#FFFFFF"
ICONS = {
    # 48x48, white on transparent; recoloured at runtime.
    "home": f'<path d="M10 30 Q8 12 16 9 Q24 3 32 9 Q40 12 38 30 Q24 35 10 30 Z" fill="{W}"/>'
            f'<path d="M12 34 Q24 39 36 34 L36 38 Q24 45 12 38 Z" fill="{W}" opacity="0.75"/>',
    "goals": f'<rect x="9" y="8" width="30" height="32" rx="6" fill="none" stroke="{W}" stroke-width="4"/>'
             f'<path d="M15 19 l4 4 l8 -8" stroke="{W}" stroke-width="4" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'
             f'<path d="M16 31 h16" stroke="{W}" stroke-width="4" stroke-linecap="round"/>',
    "focus": f'<circle cx="24" cy="27" r="14" fill="none" stroke="{W}" stroke-width="4"/>'
             f'<path d="M24 27 V18" stroke="{W}" stroke-width="4" stroke-linecap="round"/>'
             f'<path d="M24 27 L30 30" stroke="{W}" stroke-width="4" stroke-linecap="round"/>'
             f'<path d="M19 6 h10" stroke="{W}" stroke-width="4" stroke-linecap="round"/>',
    "agents": f'<rect x="9" y="15" width="30" height="23" rx="7" fill="none" stroke="{W}" stroke-width="4"/>'
              f'<circle cx="18" cy="26" r="3" fill="{W}"/><circle cx="30" cy="26" r="3" fill="{W}"/>'
              f'<path d="M24 15 V8" stroke="{W}" stroke-width="4" stroke-linecap="round"/><circle cx="24" cy="7" r="3" fill="{W}"/>',
    "breathe": f'<path d="M24 40 C10 36 8 22 12 14 C20 16 24 24 24 40 Z" fill="{W}" opacity="0.8"/>'
               f'<path d="M24 40 C38 36 40 22 36 14 C28 16 24 24 24 40 Z" fill="{W}" opacity="0.8"/>'
               f'<path d="M24 40 C17 30 19 14 24 7 C29 14 31 30 24 40 Z" fill="{W}"/>',
    "check": f'<path d="M10 25 l9 9 l19 -20" stroke="{W}" stroke-width="6" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
    "play": f'<path d="M16 9 L38 24 L16 39 Z" fill="{W}" stroke="{W}" stroke-width="4" stroke-linejoin="round"/>',
    "pause": f'<rect x="12" y="9" width="9" height="30" rx="3" fill="{W}"/><rect x="27" y="9" width="9" height="30" rx="3" fill="{W}"/>',
    "stop": f'<rect x="11" y="11" width="26" height="26" rx="6" fill="{W}"/>',
    "wifi_off": f'<path d="M8 20 Q24 6 40 20" stroke="{W}" stroke-width="4" fill="none" stroke-linecap="round" opacity="0.5"/>'
                f'<path d="M15 28 Q24 20 33 28" stroke="{W}" stroke-width="4" fill="none" stroke-linecap="round" opacity="0.5"/>'
                f'<circle cx="24" cy="36" r="3.5" fill="{W}"/><path d="M10 40 L38 8" stroke="{W}" stroke-width="4" stroke-linecap="round"/>',
}
ICON_PX = {"default": 40, "check": 28, "wifi_off": 28}


def render(svg_text):
    return Image.open(io.BytesIO(cairosvg.svg2png(bytestring=svg_text.encode()))).convert("RGBA")


def bleed(rgba, passes=3):
    """Spread colour into fully transparent pixels so scaled/rotated edges
    don't pick up a dark fringe."""
    a = np.asarray(rgba).astype(np.float32)
    rgb, alpha = a[..., :3].copy(), a[..., 3]
    known = alpha > 0
    for _ in range(passes):
        acc = np.zeros_like(rgb)
        cnt = np.zeros(alpha.shape, np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            k = np.roll(np.roll(known, dy, 0), dx, 1)
            c = np.roll(np.roll(rgb, dy, 0), dx, 1)
            acc += c * k[..., None]
            cnt += k
        fill = (~known) & (cnt > 0)
        rgb[fill] = acc[fill] / cnt[fill][:, None]
        known = known | fill
    out = np.dstack([rgb, alpha]).clip(0, 255).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def c_bytes(data):
    lines = []
    for i in range(0, len(data), 24):
        lines.append("    " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    return "\n".join(lines)


def emit_rgb565a8(name, im):
    a = np.asarray(bleed(im)).astype(np.uint16)
    r, g, b, al = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    px = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    data = px.astype("<u2").tobytes() + al.astype(np.uint8).tobytes()
    w, h = im.size
    return (f"static const uint8_t {name}_map[] = {{\n{c_bytes(data)}\n}};\n"
            f"const lv_image_dsc_t {name} = {{\n"
            f"    .header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565A8, .flags = 0,\n"
            f"               .w = {w}, .h = {h}, .stride = {w * 2}}},\n"
            f"    .data_size = sizeof({name}_map), .data = {name}_map,\n}};\n\n")


def emit_a8(name, im):
    al = np.asarray(im)[..., 3].astype(np.uint8)
    w, h = im.size
    return (f"static const uint8_t {name}_map[] = {{\n{c_bytes(al.tobytes())}\n}};\n"
            f"const lv_image_dsc_t {name} = {{\n"
            f"    .header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .flags = 0,\n"
            f"               .w = {w}, .h = {h}, .stride = {w}}},\n"
            f"    .data_size = sizeof({name}_map), .data = {name}_map,\n}};\n\n")


def build_mascot(c_out, h_out):
    parts = mascot_art.parts()
    h_out.append("typedef struct {\n    const lv_image_dsc_t* img;\n"
                 "    int16_t x, y;    /* top-left, relative to the feet-centre origin */\n"
                 "    int16_t px, py;  /* pivot inside the image */\n} mascot_part_t;\n\n")
    names = [n for n, _, _ in parts]
    h_out.append("enum {\n" + "".join(f"    MASCOT_{n.upper()},\n" for n in names) + "    MASCOT_PART_COUNT\n};\n\n")
    h_out.append("typedef struct {\n    float scale;\n    int16_t width, height;  /* of the whole figure */\n"
                 "    mascot_part_t parts[MASCOT_PART_COUNT];\n} mascot_set_t;\n\n")
    total = 0
    for set_name, scale in MASCOT_SETS.items():
        rows = []
        for name, frag, pivot in parts:
            im = render(mascot_art.svg(frag, scale))
            box = im.getchannel("A").getbbox()
            x0, y0, x1, y1 = max(box[0] - 1, 0), max(box[1] - 1, 0), min(box[2] + 1, im.width), min(box[3] + 1, im.height)
            crop = im.crop((x0, y0, x1, y1))
            sym = f"img_mascot_{set_name}_{name}"
            c_out.append(emit_rgb565a8(sym, crop))
            total += crop.width * crop.height * 3
            ox, oy = round(ORIGIN[0] * scale), round(ORIGIN[1] * scale)
            rows.append(f"        [MASCOT_{name.upper()}] = {{&{sym}, {x0 - ox}, {y0 - oy}, "
                        f"{round(pivot[0] * scale) - x0}, {round(pivot[1] * scale) - y0}}},\n")
        c_out.append(f"const mascot_set_t mascot_{set_name} = {{\n    .scale = {scale}f, "
                     f".width = {round(mascot_art.W * scale)}, .height = {round(mascot_art.H * scale)},\n"
                     f"    .parts = {{\n{''.join(rows)}    }},\n}};\n\n")
        h_out.append(f"extern const mascot_set_t mascot_{set_name};\n")
    h_out.append("\n")
    return total


def build_icons(c_out, h_out):
    for name, frag in ICONS.items():
        px = ICON_PX.get(name, ICON_PX["default"])
        im = render(f'<svg xmlns="http://www.w3.org/2000/svg" width="{px}" height="{px}" viewBox="0 0 48 48">{frag}</svg>')
        c_out.append(emit_a8(f"icon_{name}", im))
        h_out.append(f"extern const lv_image_dsc_t icon_{name};\n")
    h_out.append("\n")


def build_fonts(h_out):
    with tempfile.TemporaryDirectory() as tmp:
        instances = {}
        for name, weight, size, ranges in FONTS:
            if weight not in instances:
                ttf = os.path.join(tmp, f"nunito-{weight}.ttf")
                subprocess.run([sys.executable, "-m", "fontTools.varLib.instancer", FONT_VF, f"wght={weight}", "-o", ttf, "-q"],
                               check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                instances[weight] = ttf
            out = os.path.join(GEN, f"font_{name}.c")
            subprocess.run(["lv_font_conv", "--font", instances[weight], "-r", ranges, "--size", str(size),
                            "--bpp", "4", "--format", "lvgl", "--no-compress", "--lv-include", "lvgl.h",
                            "--lv-font-name", f"font_{name}", "-o", out], check=True)
            h_out.append(f"extern const lv_font_t font_{name};\n")
    h_out.append("\n")


def main():
    os.makedirs(GEN, exist_ok=True)
    c_out = ["// Generated by tools/assetc.py. Do not edit.\n#include \"assets.h\"\n\n"]
    h_out = ["// Generated by tools/assetc.py. Do not edit.\n#pragma once\n#include \"lvgl.h\"\n\n"
             "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"]
    total = build_mascot(c_out, h_out)
    build_icons(c_out, h_out)
    build_fonts(h_out)
    h_out.append("#ifdef __cplusplus\n}\n#endif\n")
    with open(os.path.join(GEN, "assets.c"), "w") as f:
        f.write("".join(c_out))
    with open(os.path.join(GEN, "assets.h"), "w") as f:
        f.write("".join(h_out))
    print(f"mascot sprites: {total / 1024:.0f} KB; wrote {GEN}")


if __name__ == "__main__":
    main()
