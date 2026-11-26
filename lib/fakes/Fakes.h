#pragma once
// 假的硬體實作：給單元測試和電腦模擬器用。直接改公開欄位就能模擬感測器讀數。
#include <Hal.h>

#include <deque>
#include <map>
#include <string>

namespace fakes {

struct Weight : core::IWeightSensor {
    float grams = 0;
    bool ok = true;
    bool read(float& g) override {
        g = grams;
        return ok;
    }
};

struct Distance : core::IDistanceSensor {
    int mm = 300;
    bool ok = true;
    bool read(int& v) override {
        v = mm;
        return ok;
    }
};

struct Ir : core::IIrBeam {
    bool blocked = false;
    bool ok = true;
    bool isBlocked() override { return blocked; }
    bool healthy() const override { return ok; }
};

struct Card : core::ICardReader {
    std::deque<std::string> queue;  // 每次 poll 取一張
    void tap(const std::string& uid) { queue.push_back(uid); }
    bool poll(std::string& uid) override {
        if (queue.empty()) return false;
        uid = queue.front();
        queue.pop_front();
        return true;
    }
};

struct Lock : core::ILock {
    int pulses = 0;
    uint32_t lastMs = 0;
    void unlockPulse(uint32_t ms) override {
        ++pulses;
        lastMs = ms;
    }
};

struct Buzzer : core::IBuzzer {
    uint32_t freq = 0;
    int starts = 0;  // 從安靜變成發聲的次數
    void tone(uint32_t f) override {
        if (f && !freq) ++starts;
        freq = f;
    }
};

struct Lid : core::ILidSwitch {
    bool closed = true;
    bool isClosed() override { return closed; }
};

struct Button : core::IButton {
    bool pressed = false;
    bool isPressed() override { return pressed; }
};

struct Display : core::IDisplay {
    core::DisplayText text;
    int updates = 0;
    void show(const core::DisplayText& t) override {
        text = t;
        ++updates;
    }
};

struct Storage : core::IStorage {
    std::map<std::string, std::string> data;
    bool writable = true;
    bool load(const char* k, std::string& out) override {
        auto it = data.find(k);
        if (it == data.end()) return false;
        out = it->second;
        return true;
    }
    bool save(const char* k, const std::string& v) override {
        if (!writable) return false;
        data[k] = v;
        return true;
    }
};

// 一整組假硬體 + 轉成 core::Hardware
struct Rig {
    Weight weight;
    Distance dist0, dist1;
    Ir ir;
    Card card;
    Lock lock;
    Buzzer buzzer;
    Lid lid;
    Button button;
    Display display;
    Storage storage;

    core::Hardware hw() {
        return core::Hardware{&weight, &dist0, &dist1, &ir,     &card,   &lock,
                              &buzzer, &lid,   &button, &display, &storage};
    }
};

}  // namespace fakes
