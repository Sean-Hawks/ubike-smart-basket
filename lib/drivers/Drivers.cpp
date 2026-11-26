#include "Drivers.h"

#include <Pins.h>
#include <Wire.h>
#include <cmath>

#include "OledGlyphs.h"
#include "Pwm.h"

namespace drivers {

// ---------------------------------------------------------------------------
// I2C
// ---------------------------------------------------------------------------

bool muxSelect(uint8_t channel) {
    if (channel > 7) return false;
    Wire.beginTransmission(pins::TCA9548A_ADDR);
    Wire.write(1 << channel);
    return Wire.endTransmission() == 0;
}

void i2cScan(Print& out) {
    out.print("I2C:");
    int n = 0;
    for (uint8_t a = 1; a < 127; ++a) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            out.printf(" 0x%02X", a);
            ++n;
        }
    }
    out.println(n ? "" : " (沒有找到任何裝置)");
}

// ---------------------------------------------------------------------------
// VL53L0X
// ---------------------------------------------------------------------------

bool TofSensor::begin() {
    ok_ = false;
    if (!muxSelect(ch_)) return false;
    dev_.setTimeout(100);
    if (!dev_.init()) return false;
    dev_.startContinuous();  // 連續量測，read() 時通常已經有資料，不太會卡
    ok_ = true;
    return true;
}

bool TofSensor::read(int& mm) {
    if (!ok_ || !muxSelect(ch_)) return false;
    uint16_t v = dev_.readRangeContinuousMillimeters();
    if (dev_.timeoutOccurred() || dev_.last_status != 0 || v == 0 || v >= 8190) return false;
    mm = v;
    return true;
}

// ---------------------------------------------------------------------------
// HX711
// ---------------------------------------------------------------------------

void Hx711Scale::begin(long offset, float scale) {
    has_ = false;
    hx_.begin(dt_, sck_);
    hx_.set_offset(offset);
    hx_.set_scale(scale);
}

bool Hx711Scale::read(float& grams) {
    if (hx_.is_ready()) {  // HX711 每秒只出 10 筆，沒準備好就用上一筆，不卡主迴圈
        last_ = hx_.get_units(1);
        lastReadMs_ = millis();
        has_ = std::isfinite(last_);
    }
    grams = last_;
    return has_ && millis() - lastReadMs_ <= 500;
}

bool loadHx711Calibration(long& offset, float& scale) {
    Preferences p;
    bool opened = p.begin("hx711", true);
    bool ok = opened && p.isKey("scale") && p.isKey("offset");
    offset = p.getLong("offset", 0);
    scale = p.getFloat("scale", 1.0f);
    p.end();
    if (!std::isfinite(scale) || scale == 0) { scale = 1.0f; ok = false; }
    return ok;
}

bool saveHx711Calibration(long offset, float scale) {
    if (!std::isfinite(scale) || scale == 0) return false;
    Preferences p;
    if (!p.begin("hx711", false)) return false;
    bool ok = p.putLong("offset", offset) == sizeof(long);
    ok = p.putFloat("scale", scale) == sizeof(float) && ok;
    p.end();
    return ok;
}

// ---------------------------------------------------------------------------
// 紅外線對射
// ---------------------------------------------------------------------------

void IrBeam::begin() {
    pinMode(rx_, pullup_ ? INPUT_PULLUP : INPUT);
    pwm::attach(tx_, ch_, 38000, 8);
    pwm::write(tx_, ch_, 0);
}

bool IrBeam::isBlocked() {
    // 接收頭的 AGC 會把「一直送」的載波當成雜訊濾掉，所以要一段一段送：送 600 µs、停 600 µs
    int hits = 0;
    // 上次 burst 已結束；未發射卻一直 LOW 是接線短路／外部干擾，不能當成「通」。
    healthy_ = digitalRead(rx_) == HIGH;
    for (int i = 0; i < kBursts; ++i) {
        pwm::write(tx_, ch_, 128);  // 50% duty
        bool seen = false;
        uint32_t t0 = micros();
        while (micros() - t0 < kOnUs) {
            if (digitalRead(rx_) == LOW) seen = true;  // 收到 38kHz 時輸出 LOW
        }
        pwm::write(tx_, ch_, 0);
        t0 = micros();
        while (micros() - t0 < kOffUs) {
            if (micros() - t0 < 150 && digitalRead(rx_) == LOW) seen = true;  // 接收頭輸出會稍微延遲
        }
        if (seen) ++hits;
    }
    hits_ = hits;
    healthy_ = healthy_ && digitalRead(rx_) == HIGH;
    return !healthy_ || hits * 2 < kBursts;
}

// ---------------------------------------------------------------------------
// PN532
// ---------------------------------------------------------------------------

bool Pn532Reader::begin() {
    SPI.begin(sck_, miso_, mosi_, ss_);
    constexpr int maxAttempts = 3;
    for (initAttempts_ = 1; initAttempts_ <= maxAttempts; ++initAttempts_) {
        initStage_ = "spi";
        if (nfc_.begin()) {
            initStage_ = "firmware";
            fw_ = nfc_.getFirmwareVersion();
            if (fw_) {
                initStage_ = "sam_config";
                if (nfc_.SAMConfig()) {
                    initStage_ = "rf_config";
                    if (nfc_.setPassiveActivationRetries(0x00)) {
                        initStage_ = "ready";
                        return true;
                    }
                }
            }
        }
        if (initAttempts_ < maxAttempts) delay(120);
    }
    initAttempts_ = maxAttempts;
    fw_ = 0;
    return false;
}

bool Pn532Reader::poll(std::string& uid) {
    if (!fw_) return false;
    uint32_t now = millis();
    if (now - lastPoll_ < 250) return false;
    lastPoll_ = now;
    uint8_t buf[10];
    uint8_t len = 0;
    // 晶片只嘗試一次，主機等待給足；避免上一個未結束的無限讀卡命令污染下一次回應。
    if (!nfc_.readPassiveTargetID(PN532_MIFARE_ISO14443A, buf, &len, 150)) return false;
    if (len != 4 && len != 7) return false;
    char hex[3];
    uid.clear();
    for (uint8_t i = 0; i < len && i < sizeof buf; ++i) {
        snprintf(hex, sizeof hex, "%02X", buf[i]);
        uid += hex;
    }
    return !uid.empty();
}

// ---------------------------------------------------------------------------
// 電控鎖
// ---------------------------------------------------------------------------

void SolenoidLock::offCallback(SolenoidLock* self) {
    digitalWrite(self->pin_, LOW);
    self->on_ = false;
}

void SolenoidLock::begin() {
    digitalWrite(pin_, LOW);
    pinMode(pin_, OUTPUT);
    digitalWrite(pin_, LOW);
}

void SolenoidLock::unlockPulse(uint32_t ms) {
    uint32_t now = millis();
    if (on_ || (onSince_ && now - onSince_ < kCooldownMs)) return;  // 冷卻中，避免線圈過熱
    if (ms > 1000) ms = 1000;
    if (ms == 0) return;
    on_ = true;
    onSince_ = now;
    limit_ = ms;
    digitalWrite(pin_, HIGH);
    ++pulseCount_;
    off_.once_ms(ms, offCallback, this);
}

void SolenoidLock::loop() {
    if (on_ && millis() - onSince_ >= limit_) {
        digitalWrite(pin_, LOW);  // Ticker 應該已經關了，這裡是保險
        on_ = false;
    }
}

// ---------------------------------------------------------------------------
// 蜂鳴器
// ---------------------------------------------------------------------------

void PwmBuzzer::begin() {
    pwm::attach(pin_, ch_, 2000, 10);
    pwm::tone(pin_, ch_, 0);
}

void PwmBuzzer::tone(uint32_t freqHz) {
    if (freqHz && freqHz != frequency_) {
        lastFrequency_ = freqHz;
        ++toneCount_;
    }
    frequency_ = freqHz;
    pwm::tone(pin_, ch_, freqHz);
}

// ---------------------------------------------------------------------------
// 數位輸入
// ---------------------------------------------------------------------------

void DebouncedInput::begin() {
    pinMode(pin_, INPUT_PULLUP);
    raw_ = stable_ = digitalRead(pin_) == active_;
    changed_ = millis();
}

bool DebouncedInput::active() {
    bool r = digitalRead(pin_) == active_;
    uint32_t now = millis();
    if (r != raw_) {
        raw_ = r;
        changed_ = now;
    }
    if (now - changed_ >= ms_) stable_ = raw_;
    return stable_;
}

// ---------------------------------------------------------------------------
// OLED
// ---------------------------------------------------------------------------

static const OledGlyph* findGlyph(uint32_t cp) {
    int lo = 0, hi = kOledGlyphCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (kOledGlyphs[mid].code == cp) return &kOledGlyphs[mid];
        if (kOledGlyphs[mid].code < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

bool OledDisplay::begin() {
    ready_ = false;
    // U8g2 的 2ND_HW_I2C 會使用 Wire1；先設定自訂腳位，再讓 U8g2 初始化。
    if (!Wire1.setPins(pins::OLED_SDA, pins::OLED_SCL) ||
        !Wire1.begin(pins::OLED_SDA, pins::OLED_SCL, pins::I2C_HZ)) return false;
    Wire1.setTimeOut(25);
    Wire1.beginTransmission(addr_);
    if (Wire1.endTransmission() != 0) return false;
    u8g2_.setI2CAddress(addr_ * 2);
    if (!u8g2_.begin()) return false;
    Wire1.setClock(pins::I2C_HZ);  // u8g2.begin() 可能把時脈改回 100k
    u8g2_.setFont(u8g2_font_6x12_tf);
    u8g2_.setFontPosBaseline();
    u8g2_.clearBuffer();
    u8g2_.sendBuffer();
    ready_ = true;
    return true;
}

void OledDisplay::drawLine(int row, const std::string& s) {
    const int top = row * 13;  // 5 行 x 13 px
    int x = 0;
    for (size_t i = 0; i < s.size() && x < 128;) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp;
        int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else { cp = c & 0x07; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += n;

        if (cp < 0x80) {
            u8g2_.drawGlyph(x, top + 10, (uint16_t)cp);
            x += 6;
        } else if (const OledGlyph* g = findGlyph(cp)) {
            u8g2_.drawXBM(x, top, 12, 12, g->bits);
            x += 12;
        } else {
            u8g2_.drawFrame(x + 1, top + 1, 10, 10);  // 缺字：畫方框（重跑 tools/gen_oled_glyphs.py）
            x += 12;
        }
    }
}

void OledDisplay::show(const core::DisplayText& t) {
    if (!ready_) return;
    u8g2_.clearBuffer();
    for (int i = 0; i < core::DisplayText::kLines; ++i) drawLine(i, t.line[i]);
    u8g2_.sendBuffer();
}

// ---------------------------------------------------------------------------
// Preferences
// ---------------------------------------------------------------------------

bool PrefsStorage::load(const char* key, std::string& out) {
    if (!prefs_.isKey(key)) return false;
    out = prefs_.getString(key).c_str();
    return true;
}

bool PrefsStorage::save(const char* key, const std::string& value) {
    return prefs_.putString(key, value.c_str()) == value.size();
}

}  // namespace drivers
