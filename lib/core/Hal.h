#pragma once
// 硬體抽象層：核心邏輯只認得這些介面。
// ESP32 端的真實驅動在 lib/drivers/，電腦端（測試、模擬器）的假實作在 lib/fakes/。
#include <cstdint>
#include <string>

namespace core {

// 重量（公克，已扣皮重並乘上校正係數）。讀不到時回傳 false。
struct IWeightSensor {
    virtual ~IWeightSensor() = default;
    virtual bool read(float& grams) = 0;
};

// 一顆測距感測器（mm）。超出範圍或讀取失敗回傳 false。
struct IDistanceSensor {
    virtual ~IDistanceSensor() = default;
    virtual bool read(int& mm) = 0;
};

// 紅外線對射：光被擋住回傳 true。
struct IIrBeam {
    virtual ~IIrBeam() = default;
    virtual bool isBlocked() = 0;
    virtual bool healthy() const { return true; }
};

// NFC 讀卡機：有卡片時回傳 true 並填入 UID（十六進位大寫字串）。
struct ICardReader {
    virtual ~ICardReader() = default;
    virtual bool poll(std::string& uid) = 0;
};

// 電控鎖：通電 ms 毫秒後自動斷電（驅動端也必須限制在 1 秒內）。
struct ILock {
    virtual ~ILock() = default;
    virtual void unlockPulse(uint32_t ms) = 0;
};

// 蜂鳴器：freqHz = 0 代表停止。
struct IBuzzer {
    virtual ~IBuzzer() = default;
    virtual void tone(uint32_t freqHz) = 0;
};

// 蓋子開關：蓋子關上回傳 true。
struct ILidSwitch {
    virtual ~ILidSwitch() = default;
    virtual bool isClosed() = 0;
};

// 開蓋按鈕：目前是否按著（邊緣偵測由核心做）。
struct IButton {
    virtual ~IButton() = default;
    virtual bool isPressed() = 0;
};

// OLED 顯示內容：固定 5 行 UTF-8 文字。
struct DisplayText {
    static constexpr int kLines = 5;
    std::string line[kLines];
    bool operator==(const DisplayText& o) const {
        for (int i = 0; i < kLines; ++i)
            if (line[i] != o.line[i]) return false;
        return true;
    }
    bool operator!=(const DisplayText& o) const { return !(*this == o); }
};

struct IDisplay {
    virtual ~IDisplay() = default;
    virtual void show(const DisplayText& text) = 0;
};

// 簡單的鍵值儲存（ESP32 用 Preferences）。key 最長 15 字元。
struct IStorage {
    virtual ~IStorage() = default;
    virtual bool load(const char* key, std::string& out) = 0;
    virtual bool save(const char* key, const std::string& value) = 0;
};

}  // namespace core
