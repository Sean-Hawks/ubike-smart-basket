#include "Accounts.h"

#include <cstdlib>
#include <sstream>

namespace core {

void Accounts::ensure(const std::string& uid) {
    if (!exists(uid)) bal_[uid] = initial_;
}

int32_t Accounts::balance(const std::string& uid) const {
    auto it = bal_.find(uid);
    return it == bal_.end() ? initial_ : it->second;
}

void Accounts::add(const std::string& uid, int32_t delta) {
    ensure(uid);
    bal_[uid] += delta;
}

std::string Accounts::serialize() const {
    std::string out;
    for (auto& kv : bal_) {
        if (!out.empty()) out += ';';
        out += kv.first + "=" + std::to_string(kv.second);
    }
    return out;
}

void Accounts::parse(const std::string& s) {
    bal_.clear();
    std::istringstream in(s);
    std::string item;
    while (std::getline(in, item, ';')) {
        auto eq = item.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        bal_[item.substr(0, eq)] = (int32_t)std::atol(item.c_str() + eq + 1);
    }
}

}  // namespace core
