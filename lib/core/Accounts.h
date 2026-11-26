#pragma once
// 帳戶餘額（以卡片 UID 為帳號）。
#include <cstdint>
#include <map>
#include <string>

namespace core {

class Accounts {
public:
    explicit Accounts(int32_t initialBalance = 100) : initial_(initialBalance) {}
    void setInitialBalance(int32_t v) { initial_ = v; }

    void ensure(const std::string& uid);  // 沒看過的卡片就開戶（初始餘額）
    int32_t balance(const std::string& uid) const;
    bool exists(const std::string& uid) const { return bal_.count(uid) != 0; }
    void add(const std::string& uid, int32_t delta);
    const std::map<std::string, int32_t>& all() const { return bal_; }

    std::string serialize() const;  // "UID=餘額;UID=餘額"
    void parse(const std::string& s);

private:
    int32_t initial_;
    std::map<std::string, int32_t> bal_;
};

}  // namespace core
