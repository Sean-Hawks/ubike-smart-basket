#include "Json.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace core {

std::string jsonEscape(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    o += buf;
                } else {
                    o += c;
                }
        }
    }
    return o + "\"";
}

static const char* b(bool v) { return v ? "true" : "false"; }

static std::string readingJson(const Reading& r) {
    std::ostringstream o;
    o << "{\"weight\":";
    if (r.weightValid) o << std::lround(r.weightG * 10) / 10.0;
    else o << "null";
    for (int i = 0; i < 2; ++i) {
        o << ",\"dist" << i << "\":";
        if (r.distValid[i]) o << r.distMm[i];
        else o << "null";
    }
    o << ",\"ir\":" << b(r.irBlocked) << "}";
    return o.str();
}

std::string accountsJson(const Basket& bk) {
    std::ostringstream o;
    o << "[";
    bool first = true;
    for (auto& kv : bk.accounts().all()) {
        if (!first) o << ",";
        first = false;
        o << "{\"uid\":" << jsonEscape(kv.first) << ",\"balance\":" << kv.second << "}";
    }
    o << "]";
    return o.str();
}

std::string eventsJson(const Basket& bk) {
    std::ostringstream o;
    o << "[";
    bool first = true;
    for (auto& e : bk.events().list()) {
        if (!first) o << ",";
        first = false;
        o << "{\"seq\":" << e.seq << ",\"t\":" << e.timeMs << ",\"type\":\"" << eventTypeName(e.type)
          << "\",\"uid\":" << jsonEscape(e.uid) << ",\"amount\":" << e.amount << ",\"text\":" << jsonEscape(e.text)
          << "}";
    }
    o << "]";
    return o.str();
}

std::string stateJson(const Basket& bk, uint32_t now) {
    const Config& c = bk.config();
    std::ostringstream o;
    o << "{\"state\":\"" << stateName(bk.state()) << "\"";
    o << ",\"rider\":" << jsonEscape(bk.rider());
    o << ",\"hardwareReady\":" << b(bk.hardwareReady()) << ",\"storageHealthy\":" << b(bk.storageHealthy());
    o << ",\"hasBaseline\":" << b(bk.hasBaseline());
    o << ",\"countdownLeft\":" << (bk.countdownLeftMs(now) + 999) / 1000;
    o << ",\"hasTrash\":" << b(bk.hasTrash()) << ",\"trashOwner\":" << jsonEscape(bk.trashOwner());
    o << ",\"hasLost\":" << b(bk.hasLost());

    o << ",\"pending\":[";
    bool first = true;
    for (auto& p : bk.pendings()) {
        if (!first) o << ",";
        first = false;
        o << "{\"owner\":" << jsonEscape(p.owner) << ",\"leftS\":" << (bk.pendingLeftMs(p, now) + 999) / 1000
          << ",\"cleared\":" << b(p.cleared) << "}";
    }
    o << "]";

    o << ",\"display\":[";
    for (int i = 0; i < DisplayText::kLines; ++i) o << (i ? "," : "") << jsonEscape(bk.displayText().line[i]);
    o << "]";

    DetectResult d = bk.detectNow();
    const auto& cutoffs = bk.distanceCutoffsMm();
    o << ",\"sensors\":{\"ready\":" << b(bk.sensorsReady()) << ",\"lidClosed\":" << b(bk.lidClosed())
      << ",\"weightEnabled\":" << b(bk.weightEnabled())
      << ",\"irEnabled\":" << b(bk.irEnabled())
      << ",\"irValid\":" << b(bk.irValid())
      << ",\"distanceCutoffsMm\":[" << cutoffs[0] << ',' << cutoffs[1] << ']'
      << ",\"now\":" << readingJson(bk.current()) << ",\"baseline\":" << readingJson(bk.baseline())
      << ",\"present\":" << b(d.present) << ",\"byWeight\":" << b(d.byWeight) << ",\"byDist0\":" << b(d.byDist[0])
      << ",\"byDist1\":" << b(d.byDist[1]) << ",\"byIr\":" << b(d.byIr) << "}";

    o << ",\"config\":{\"penalty\":" << c.penalty << ",\"reward\":" << c.reward << ",\"initial\":" << c.initialBalance
      << ",\"countdown_s\":" << c.countdownMs / 1000.0 << ",\"timeout_s\":" << c.confirmTimeoutMs / 1000.0
      << ",\"weight_g\":" << c.weightThresholdG << ",\"dist_mm\":" << c.distThresholdMm
      << ",\"lock_ms\":" << c.lockPulseMs << "}";

    o << ",\"accounts\":" << accountsJson(bk);
    o << ",\"events\":" << eventsJson(bk);
    o << ",\"uptime\":" << now / 1000 << "}";
    return o.str();
}

}  // namespace core
