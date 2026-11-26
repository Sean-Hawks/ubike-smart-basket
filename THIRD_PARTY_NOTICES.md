# 第三方授權

`lib/drivers/OledGlyphs.h` 中的中文字形取自 WenQuanYi Bitmap Song 9pt，版本 0.9.9.8。Copyright © 2004–2010, WenQuanYi Project Board of Trustees and Qianqian Fang。字形由 `tools/gen_oled_glyphs.py` 自上游 BDF 取出，調整至 12 × 12 格後轉成 XBM 點陣格式。字形與其衍生點陣依 [GNU GPL v2](LICENSES/GPL-2.0-only.txt) 及[字型嵌入例外](LICENSES/Font-exception-2.0.txt) 提供。[上游 BDF](https://github.com/olikraus/u8g2/blob/master/tools/font/bdf/wenquanyi_9pt.bdf)

PlatformIO 依 `platformio.ini` 安裝 Arduino-ESP32、U8g2、Adafruit PN532、VL53L0X 與 HX711 等相依套件。這些套件的授權由各自的上游專案提供。
