#pragma once
// ESP32 真實驅動：每個類別實作 lib/core/Hal.h 的一個介面。
// 主程式（src/main）和零件測試（src/hwtest）都用這裡的類別。
#include <Adafruit_PN532.h>
#include <Arduino.h>
#include <HX711.h>
#include <Hal.h>
#include <Preferences.h>
#include <Ticker.h>
#include <U8g2lib.h>
#include <VL53L0X.h>

// 缺少這個設定時，U8g2 的第二組 I2C 傳送函式仍會編譯，但不會送出資料。
#ifndef U8X8_HAVE_2ND_HW_I2C
#error "OLED requires U8g2 Wire1 support: enable U8X8_HAVE_2ND_HW_I2C in platformio.ini"
#endif

namespace drivers {

// ---- I2C 多工器 TCA9548A ----
bool muxSelect(uint8_t channel);  // 成功回傳 true
void i2cScan(Print& out);         // 把找到的 I2C 位址印出來（除錯用）

// ---- VL53L0X 測距（接在多工器某個通道上）----
class TofSensor : public core::IDistanceSensor {
public:
    explicit TofSensor(uint8_t muxChannel) : ch_(muxChannel) {}
    bool begin();
    bool read(int& mm) override;
    bool ok() const { return ok_; }

private:
    uint8_t ch_;
    bool ok_ = false;
    VL53L0X dev_;
};

// ---- HX711 秤重 ----
// offset / scale 由 hwtest_hx711 校正後存在 Preferences（namespace "hx711"）。
class Hx711Scale : public core::IWeightSensor {
public:
    Hx711Scale(int dt, int sck) : dt_(dt), sck_(sck) {}
    void begin(long offset, float scale);
    bool read(float& grams) override;  // 最多沿用 500 ms，逾時回傳 false
    HX711& raw() { return hx_; }

private:
    int dt_, sck_;
    HX711 hx_;
    bool has_ = false;
    float last_ = 0;
    uint32_t lastReadMs_ = 0;
};
// 從 Preferences 讀校正值；沒校正過回傳 false（offset=0, scale=1）
bool loadHx711Calibration(long& offset, float& scale);
bool saveHx711Calibration(long offset, float scale);

// ---- 紅外線對射（38kHz，分段送）----
class IrBeam : public core::IIrBeam {
public:
    IrBeam(int txPin, int rxPin, int ledcChannel, bool pullup = false)
        : tx_(txPin), rx_(rxPin), ch_(ledcChannel), pullup_(pullup) {}
    void begin();
    bool isBlocked() override;
    bool healthy() const override { return healthy_; }
    int lastHits() const { return hits_; }  // 最近一次有幾段收到（除錯用）
    static constexpr int kBursts = 4;
    static constexpr uint32_t kOnUs = 600, kOffUs = 600;

private:
    int tx_, rx_, ch_;
    bool pullup_;
    int hits_ = 0;
    bool healthy_ = true;
};

// ---- PN532 NFC（SPI）----
class Pn532Reader : public core::ICardReader {
public:
    Pn532Reader(int sck, int miso, int mosi, int ss) : sck_(sck), miso_(miso), mosi_(mosi), ss_(ss), nfc_(ss, &SPI) {}
    bool begin();  // 找不到 PN532 回傳 false
    bool poll(std::string& uid) override;
    uint32_t firmware() const { return fw_; }
    const char* initStage() const { return initStage_; }
    int initAttempts() const { return initAttempts_; }

private:
    int sck_, miso_, mosi_, ss_;
    Adafruit_PN532 nfc_;
    uint32_t fw_ = 0;
    uint32_t lastPoll_ = 0;
    const char* initStage_ = "not_started";
    int initAttempts_ = 0;
};

// ---- 電控鎖（MOSFET）----
class SolenoidLock : public core::ILock {
public:
    explicit SolenoidLock(int pin) : pin_(pin) {}
    void begin();
    void unlockPulse(uint32_t ms) override;  // 上限 1000 ms，兩次之間至少隔 kCooldownMs
    void loop();                             // 保險：超時強制斷電，主迴圈要一直呼叫
    bool energized() const { return on_; }
    uint32_t pulseCount() const { return pulseCount_; }
    static constexpr uint32_t kCooldownMs = 1500;

private:
    static void offCallback(SolenoidLock* self);
    int pin_;
    Ticker off_;
    volatile bool on_ = false;
    uint32_t onSince_ = 0, limit_ = 0;
    uint32_t pulseCount_ = 0;
};

// ---- 無源蜂鳴器（PWM）----
class PwmBuzzer : public core::IBuzzer {
public:
    PwmBuzzer(int pin, int ledcChannel) : pin_(pin), ch_(ledcChannel) {}
    void begin();
    void tone(uint32_t freqHz) override;
    uint32_t frequency() const { return frequency_; }
    uint32_t lastFrequency() const { return lastFrequency_; }
    uint32_t toneCount() const { return toneCount_; }

private:
    int pin_, ch_;
    uint32_t frequency_ = 0;
    uint32_t lastFrequency_ = 0, toneCount_ = 0;
};

// ---- 數位輸入（含去彈跳）----
class DebouncedInput {
public:
    DebouncedInput(int pin, int activeLevel, uint32_t debounceMs = 30)
        : pin_(pin), active_(activeLevel), ms_(debounceMs) {}
    void begin();
    bool active();

private:
    int pin_, active_;
    uint32_t ms_;
    bool raw_ = false, stable_ = false;
    uint32_t changed_ = 0;
};

class LidSwitch : public core::ILidSwitch {
public:
    LidSwitch(int pin, int closedLevel) : in_(pin, closedLevel, 50) {}
    void begin() { in_.begin(); }
    bool isClosed() override { return in_.active(); }

private:
    DebouncedInput in_;
};

class Button : public core::IButton {
public:
    explicit Button(int pin) : in_(pin, LOW, 30), enabled_(pin >= 0) {}
    void begin() { if (enabled_) in_.begin(); }
    bool isPressed() override { return enabled_ && in_.active(); }

private:
    DebouncedInput in_;
    bool enabled_;
};

// ---- OLED SH1106 128x64（第二組 I2C：GPIO13/14）----
// 5 行：ASCII 用 6x12 字型，中文用 OledGlyphs.h 的 12x12 點陣（繁體）。
class OledDisplay : public core::IDisplay {
public:
    explicit OledDisplay(uint8_t addr) : addr_(addr), u8g2_(U8G2_R0, U8X8_PIN_NONE) {}
    bool begin();
    void show(const core::DisplayText& text) override;
    void drawLine(int row, const std::string& utf8);  // 不清畫面，給 hwtest 用
    U8G2& u8g2() { return u8g2_; }

private:
    uint8_t addr_;
    bool ready_ = false;
    U8G2_SH1106_128X64_NONAME_F_2ND_HW_I2C u8g2_;
};

// ---- Preferences 儲存 ----
class PrefsStorage : public core::IStorage {
public:
    bool begin(const char* ns = "basket") { return prefs_.begin(ns, false); }
    bool load(const char* key, std::string& out) override;
    bool save(const char* key, const std::string& value) override;
    void clear() { prefs_.clear(); }

private:
    Preferences prefs_;
};

}  // namespace drivers
