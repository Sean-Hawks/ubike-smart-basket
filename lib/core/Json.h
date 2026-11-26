#pragma once
// 把籃子狀態轉成 JSON 字串（給 ESP32 網頁 API 用，也能在電腦上測）。
#include <string>

#include "Basket.h"

namespace core {

std::string jsonEscape(const std::string& s);
std::string stateJson(const Basket& b, uint32_t now);     // 全部：狀態、標記、待確認、帳戶、事件、設定、感測器
std::string accountsJson(const Basket& b);                // [{"uid":..,"balance":..}]
std::string eventsJson(const Basket& b);                  // [{"seq":..,"t":..,"type":..,...}]

}  // namespace core
