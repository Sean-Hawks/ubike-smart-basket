#pragma once
// 複製成 include/secrets.h 再填入自己的 Wi-Fi（secrets.h 不會被 git 追蹤）：
//   cp include/secrets.example.h include/secrets.h
//
// 沒有 secrets.h、或連不上 Wi-Fi 時，ESP32 會自己開熱點（AP_SSID / AP_PASS），
// 手機連上後開 http://192.168.4.1

#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASS "your-wifi-password"

// 備用熱點。留白會在每次啟動熱點時產生密碼，可從 USB 序列埠查看。
// 若需固定密碼，請在本機 secrets.h 設定 8–63 個 ASCII 字元。
#define AP_SSID "SmartBasket"
#define AP_PASS ""
