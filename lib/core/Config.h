#pragma once
// 可調整的設定。預設值在這裡改；執行中可用網頁 / 模擬器的 set 指令改（會存進 IStorage）。
#include <cstdint>
#include <string>

namespace core {

constexpr int32_t kDefaultPenalty = 15;   // 留垃圾罰款（元）
constexpr int32_t kDefaultReward = 10;    // 清理回饋金（元）
static_assert(kDefaultReward < kDefaultPenalty, "回饋金一定要小於罰款，避免兩人串通賺錢");

constexpr uint32_t kMaxLockPulseMs = 1000;  // 電控鎖單次通電上限，硬體限制，不能調高
constexpr int kSmoothSamples = 5;           // 平滑處理：取最近幾筆平均

struct Config {
    int32_t penalty = kDefaultPenalty;
    int32_t reward = kDefaultReward;
    int32_t initialBalance = 100;        // 新卡片的初始餘額
    uint32_t countdownMs = 15000;        // 還車時籃內有東西的倒數時間
    uint32_t confirmTimeoutMs = 120000;  // 網頁確認的時限，超過視為垃圾
    float weightThresholdG = 10.0f;      // 重量比基準多出幾克以上算有東西
    int32_t distThresholdMm = 20;        // 測距比基準短幾 mm 以上算有東西
    uint32_t lockPulseMs = 600;          // 開鎖通電時間（上限 kMaxLockPulseMs）
    uint32_t cardDebounceMs = 2000;      // 同一張卡連續感應的忽略時間

    bool valid() const;

    // 依名稱調整一個設定，名稱見 keys()。值不合法（例如回饋 >= 罰款）時回傳 false 且不變更。
    bool set(const std::string& key, double value);
    static const char* keys();

    std::string serialize() const;
    bool parse(const std::string& s);
};

}  // namespace core
