#pragma once
// LEDC（PWM）相容層：Arduino core 2.x 用「通道」，3.x 改成用「腳位」。
// 目前 platformio.ini 鎖定 espressif32@6.12.0 = Arduino core 2.0.17，走 2.x 分支。
#include <Arduino.h>
#include <esp_arduino_version.h>

namespace pwm {

inline void attach(int pin, int channel, uint32_t freq, uint8_t resolutionBits) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttachChannel(pin, freq, resolutionBits, channel);
#else
    ledcSetup(channel, freq, resolutionBits);
    ledcAttachPin(pin, channel);
#endif
}

inline void write(int pin, int channel, uint32_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    (void)channel;
    ledcWrite(pin, duty);
#else
    (void)pin;
    ledcWrite(channel, duty);
#endif
}

// freq = 0 會停止輸出
inline void tone(int pin, int channel, uint32_t freq) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    (void)channel;
    ledcWriteTone(pin, freq);
#else
    (void)pin;
    ledcWriteTone(channel, freq);
#endif
}

}  // namespace pwm
