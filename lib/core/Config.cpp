#include "Config.h"

#include <cstdlib>
#include <cmath>
#include <limits>
#include <iomanip>
#include <sstream>

namespace core {

namespace {
// 先檢查數值範圍再轉型；跨欄位限制由整份 Config 一起驗證。
bool assign(Config& c, const std::string& key, double v) {
    if (!std::isfinite(v) || v < 0) return false;
    if (key == "weight_g") {
        if (v > std::numeric_limits<float>::max()) return false;
        c.weightThresholdG = (float)v;
    } else if (key == "countdown_s" || key == "timeout_s") {
        // reached() 用有號差值比較，期限不可超過 INT32_MAX 毫秒。
        if (v * 1000 > INT32_MAX) return false;
        if (key == "countdown_s") c.countdownMs = (uint32_t)std::round(v * 1000);
        else c.confirmTimeoutMs = (uint32_t)std::round(v * 1000);
    } else {
        if (v > INT32_MAX || std::floor(v) != v) return false;
        if (key == "penalty") c.penalty = (int32_t)v;
        else if (key == "reward") c.reward = (int32_t)v;
        else if (key == "initial") c.initialBalance = (int32_t)v;
        else if (key == "dist_mm") c.distThresholdMm = (int32_t)v;
        else if (key == "lock_ms") c.lockPulseMs = (uint32_t)v;
        else return false;
    }
    return true;
}
}  // namespace

bool Config::valid() const {
    return penalty > 0 && reward >= 0 && reward < penalty && initialBalance >= 0 &&
           countdownMs > 0 && countdownMs <= INT32_MAX && confirmTimeoutMs > 0 && confirmTimeoutMs <= INT32_MAX &&
           std::isfinite(weightThresholdG) && weightThresholdG > 0 && distThresholdMm > 0 &&
           lockPulseMs > 0 && lockPulseMs <= kMaxLockPulseMs;
}

const char* Config::keys() {
    return "penalty reward initial countdown_s timeout_s weight_g dist_mm lock_ms";
}

bool Config::set(const std::string& key, double v) {
    Config c = *this;
    if (!assign(c, key, v) || !c.valid()) return false;
    *this = c;
    return true;
}

std::string Config::serialize() const {
    std::ostringstream o;
    o << std::setprecision(17);
    o << "penalty=" << penalty << ";reward=" << reward << ";initial=" << initialBalance
      << ";countdown_s=" << countdownMs / 1000.0 << ";timeout_s=" << confirmTimeoutMs / 1000.0
      << ";weight_g=" << weightThresholdG << ";dist_mm=" << distThresholdMm << ";lock_ms=" << lockPulseMs;
    return o.str();
}

bool Config::parse(const std::string& s) {
    Config c = *this;
    std::istringstream in(s);
    std::string item;
    while (std::getline(in, item, ';')) {
        auto eq = item.find('=');
        if (eq == std::string::npos) return false;
        const char* start = item.c_str() + eq + 1;
        char* end;
        double value = std::strtod(start, &end);
        if (end == start || *end != '\0' || !assign(c, item.substr(0, eq), value)) return false;
    }
    if (!c.valid()) return false;
    *this = c;
    return true;
}

}  // namespace core
