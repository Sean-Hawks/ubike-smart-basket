// hwtest_inputs：蓋子開關（GPIO 33）與開蓋按鈕（GPIO 32），都用內部上拉
// 預期：開關蓋子、按按鈕時印出狀態變化。如果蓋子開關顯示相反，改 include/Pins.h 的 LID_CLOSED_LEVEL。
#include <Arduino.h>
#include <Drivers.h>
#include <Pins.h>

drivers::LidSwitch lid(pins::LID, pins::LID_CLOSED_LEVEL);
drivers::Button button(pins::BUTTON);

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== hwtest_inputs ===");
    lid.begin();
    button.begin();
}

void loop() {
    static int lastLid = -1, lastBtn = -1;
    int l = lid.isClosed(), b = button.isPressed();
    if (l != lastLid) Serial.printf("[%lu ms] 蓋子：%s（GPIO33 = %d）\n", millis(), l ? "關上" : "打開", digitalRead(pins::LID));
    if (b != lastBtn) Serial.printf("[%lu ms] 按鈕：%s\n", millis(), b ? "按下" : "放開");
    lastLid = l;
    lastBtn = b;
    delay(5);
}
