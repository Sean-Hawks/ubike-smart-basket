// hwtest_lock：12V 電控鎖（MOSFET 閘極接 GPIO 26，HIGH = 通電開鎖）
// 安全：每次只通電 500 ms（驅動上限 1 秒），兩次之間至少隔 1.5 秒。
// 操作：按開蓋按鈕（GPIO 32）或在序列埠輸入 u，應聽到「喀」一聲、鎖舌縮回。
// 12V 電源的 GND 要和 ESP32 共地；線圈兩端要並聯飛輪二極體。
#include <Arduino.h>
#include <Drivers.h>
#include <Pins.h>

drivers::SolenoidLock lock(pins::LOCK);
drivers::Button button(pins::BUTTON);

void unlock(const char* why) {
    Serial.printf("[%lu ms] 開鎖（%s），通電 500 ms\n", millis(), why);
    lock.unlockPulse(500);
}

void setup() {
    lock.begin();
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== hwtest_lock ===");
    button.begin();
    Serial.println("按按鈕或輸入 u 開鎖");
}

void loop() {
    lock.loop();
    static bool was = false;
    bool p = button.isPressed();
    if (p && !was) unlock("按鈕");
    was = p;
    if (Serial.available()) {
        String s = Serial.readStringUntil('\n');
        s.trim();
        if (s == "u") unlock("序列埠");
    }
    static bool lastLevel = false;
    bool level = digitalRead(pins::LOCK);
    if (level != lastLevel) Serial.printf("[%lu ms] GPIO26 = %s\n", millis(), level ? "HIGH（通電）" : "LOW（斷電）");
    lastLevel = level;
    delay(5);
}
