#include "EventLog.h"

namespace core {

const char* eventTypeName(EventType t) {
    switch (t) {
        case EventType::Boot: return "boot";
        case EventType::Calibrate: return "calibrate";
        case EventType::CardTap: return "card_tap";
        case EventType::Rent: return "rent";
        case EventType::Return: return "return";
        case EventType::ItemLeft: return "item_left";
        case EventType::Pending: return "pending";
        case EventType::ConfirmTrash: return "confirm_trash";
        case EventType::ConfirmMine: return "confirm_mine";
        case EventType::Penalty: return "penalty";
        case EventType::Reward: return "reward";
        case EventType::Warning: return "warning";
    }
    return "?";
}

void EventLog::add(uint32_t timeMs, EventType type, const std::string& uid, int32_t amount, const std::string& text) {
    Event e;
    e.seq = ++seq_;
    e.timeMs = timeMs;
    e.type = type;
    e.uid = uid;
    e.amount = amount;
    e.text = text;
    if (buf_.size() < kCapacity) {
        buf_.push_back(e);
    } else {
        buf_[head_] = e;
        head_ = (head_ + 1) % kCapacity;
    }
}

std::vector<Event> EventLog::list() const {
    std::vector<Event> out;
    out.reserve(buf_.size());
    for (size_t i = 0; i < buf_.size(); ++i) out.push_back(buf_[(head_ + i) % buf_.size()]);
    return out;
}

}  // namespace core
