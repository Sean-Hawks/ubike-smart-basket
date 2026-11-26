// hwtest_buzzer：無源蜂鳴器（GPIO 25，LEDC PWM）
// 照片款 S 是正端、− 是負端，中間不用；GPIO25 經 NPN 驅動，見 WIRING.md。
// 預期：依序聽到 Do Re Mi Fa Sol、還車提示音、倒數警報（每秒嗶一聲），重複。
#include <Arduino.h>
#include <Drivers.h>
#include <Pins.h>

drivers::PwmBuzzer buzzer(pins::BUZZER, pins::LEDC_CH_BUZZER);

void beep(uint32_t f, uint32_t ms) {
    buzzer.tone(f);
    delay(ms);
    buzzer.tone(0);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== hwtest_buzzer ===");
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    Serial.println("Arduino core 3.x（LEDC 用腳位 API）");
#else
    Serial.println("Arduino core 2.x（LEDC 用通道 API）");
#endif
    buzzer.begin();
}

void loop() {
    const uint32_t notes[] = {523, 587, 659, 698, 784};
    Serial.println("音階 Do Re Mi Fa Sol");
    for (uint32_t f : notes) {
        beep(f, 250);
        delay(50);
    }
    delay(500);
    Serial.println("借還車提示音（1800 Hz 100 ms）");
    beep(1800, 100);
    delay(800);
    Serial.println("錯誤提示音（400 Hz 200 ms）");
    beep(400, 200);
    delay(800);
    Serial.println("倒數警報（2400 Hz，每秒 200 ms，5 秒）");
    for (int i = 0; i < 5; ++i) {
        beep(2400, 200);
        delay(800);
    }
    delay(2000);
}
