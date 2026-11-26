#!/usr/bin/env python3
"""產生 OLED 用的繁體中文 12x12 點陣表 lib/drivers/OledGlyphs.h。

U8g2 內建的中文字型都是簡體字集（沒有「請、車、還、餘」等字），所以改成：
掃描會顯示在 OLED 上的原始碼，把用到的中文字從文泉驛 12px 點陣字型（BDF）取出來。

用法：
  python3 tools/gen_oled_glyphs.py            # 產生（第一次會下載 BDF 到 .pio/）
  python3 tools/gen_oled_glyphs.py --check    # 只檢查有沒有缺字（CI 用，不需要 BDF）

改了 OLED 上的中文字串之後要重跑一次。
"""
import os
import re
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = ["lib/core/Basket.cpp", "src/hwtest/oled.cpp", "src/main/main.cpp"]
OUT = os.path.join(ROOT, "lib/drivers/OledGlyphs.h")
BDF_URL = "https://raw.githubusercontent.com/olikraus/u8g2/master/tools/font/bdf/wenquanyi_9pt.bdf"
BDF_CACHE = os.path.join(ROOT, ".pio", "wenquanyi_9pt.bdf")
CELL = 12  # 12x12
ASCENT = 10  # 格子最上面一列對應 BDF 的 y=10


def wanted_chars():
    chars = set()
    for rel in SOURCES:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as f:
            for line in f:
                code = line.split("//")[0]  # 註解裡的字不算
                for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', code):
                    chars.update(c for c in lit if ord(c) >= 0x2E80)
    return sorted(chars)


def existing_chars():
    if not os.path.exists(OUT):
        return set()
    with open(OUT, encoding="utf-8") as f:
        return {chr(int(m, 16)) for m in re.findall(r"\{0x([0-9A-F]{4,5}),", f.read())}


def load_bdf(path, need):
    glyphs = {}
    with open(path, encoding="latin-1") as f:
        enc = None
        bbx = None
        rows = None
        for line in f:
            parts = line.split()
            if not parts:
                continue
            key = parts[0]
            if key == "ENCODING":
                enc = int(parts[1])
            elif key == "BBX":
                bbx = tuple(int(x) for x in parts[1:5])
            elif key == "BITMAP":
                rows = [] if enc is not None and chr(enc) in need else None
            elif key == "ENDCHAR":
                if rows is not None:
                    glyphs[chr(enc)] = (bbx, rows)
                enc, bbx, rows = None, None, None
            elif rows is not None:
                hexrow = parts[0][:4]  # 統一成 16 bit、靠左對齊
                rows.append(int(hexrow, 16) << (16 - 4 * len(hexrow)))
    return glyphs


def render(bbx, rows):
    """回傳 12 列、每列 12 bit 的整數（bit 11 = 最左邊）。"""
    w, h, xoff, yoff = bbx
    cell = [0] * CELL
    for r, bits in enumerate(rows):
        y = yoff + h - 1 - r
        cr = ASCENT - y
        if not 0 <= cr < CELL:
            continue
        for x in range(w):
            if bits & (0x8000 >> x):
                cx = x + xoff
                if 0 <= cx < CELL:
                    cell[cr] |= 1 << (CELL - 1 - cx)
    return cell


def to_xbm(cell):
    out = []
    for row in cell:
        b = 0
        for x in range(CELL):  # XBM：每個 byte 的 bit0 是最左邊
            if row & (1 << (CELL - 1 - x)):
                b |= 1 << x
        out += [b & 0xFF, (b >> 8) & 0xFF]
    return out


def main():
    need = wanted_chars()
    if "--check" in sys.argv:
        missing = [c for c in need if c not in existing_chars()]
        if missing:
            print("OledGlyphs.h 缺字：" + "".join(missing) + "，請執行 python3 tools/gen_oled_glyphs.py")
            sys.exit(1)
        print(f"OLED 點陣表完整（{len(need)} 字）")
        return

    if not os.path.exists(BDF_CACHE):
        os.makedirs(os.path.dirname(BDF_CACHE), exist_ok=True)
        print("下載", BDF_URL)
        urllib.request.urlretrieve(BDF_URL, BDF_CACHE)
    glyphs = load_bdf(BDF_CACHE, set(need))
    missing = [c for c in need if c not in glyphs]
    if missing:
        print("字型裡找不到：" + "".join(missing))
        sys.exit(1)

    lines = [
        "#pragma once",
        "// 自動產生，請勿手改：python3 tools/gen_oled_glyphs.py",
        "// 字型：文泉驛點陣宋體 12px（GPL v2 with font embedding exception）",
        "// 每字 12x12，XBM 格式（每列 2 byte，bit0 為最左邊），依 Unicode 排序",
        "#include <stdint.h>",
        "",
        "struct OledGlyph {",
        "    uint32_t code;",
        "    uint8_t bits[24];",
        "};",
        "",
        f"static const int kOledGlyphCount = {len(need)};",
        "static const OledGlyph kOledGlyphs[] = {",
    ]
    for c in need:
        data = ", ".join(f"0x{b:02X}" for b in to_xbm(render(*glyphs[c])))
        lines.append(f"    {{0x{ord(c):04X}, {{{data}}}}},  // {c}")
    lines.append("};")
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"已產生 {os.path.relpath(OUT, ROOT)}（{len(need)} 字）：{''.join(need)}")


if __name__ == "__main__":
    main()
