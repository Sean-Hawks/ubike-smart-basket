#pragma once
// 事件紀錄：只保留最近 kCapacity 筆（環狀），給網頁和模擬器看。
#include <cstdint>
#include <string>
#include <vector>

namespace core {

enum class EventType {
    Boot,
    Calibrate,
    CardTap,
    Rent,
    Return,
    ItemLeft,      // 還車時有東西，開始倒數
    Pending,       // 倒數結束仍有東西，等上一位確認
    ConfirmTrash,  // 確認是垃圾 / 逾時
    ConfirmMine,   // 確認是自己的東西（遺失物）
    Penalty,
    Reward,
    Warning,
};

const char* eventTypeName(EventType t);

struct Event {
    uint32_t seq = 0;
    uint32_t timeMs = 0;
    EventType type = EventType::Boot;
    std::string uid;
    int32_t amount = 0;
    std::string text;
};

class EventLog {
public:
    static constexpr size_t kCapacity = 40;
    void add(uint32_t timeMs, EventType type, const std::string& uid, int32_t amount, const std::string& text);
    // 由舊到新
    std::vector<Event> list() const;
    uint32_t lastSeq() const { return seq_; }

private:
    std::vector<Event> buf_;
    size_t head_ = 0;  // 下一個寫入位置（滿了之後）
    uint32_t seq_ = 0;
};

}  // namespace core
