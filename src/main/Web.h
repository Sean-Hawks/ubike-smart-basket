#pragma once
#include <Basket.h>

#include <string>

// ESP32 網頁伺服器（port 80）：
//   GET  /                網頁介面
//   GET  /api/state       全部狀態（含帳戶、事件、設定、感測器）
//   GET  /api/accounts    帳戶餘額
//   GET  /api/events      事件紀錄
//   GET  /api/hw          硬體初始化結果
//   POST /api/confirm     owner=<卡號>&kind=trash|mine
//   POST /api/config      key=<名稱>&value=<值>
//   POST /api/calibrate   重新記錄空籃基準
using DeviceSignals = std::string (*)();
using SetLid = void (*)(bool closed);
void webBegin(core::Basket& basket, const std::string& hwStatusJson, DeviceSignals signals, SetLid setLid = nullptr);
void webSerialLoop();
void webLoop();
