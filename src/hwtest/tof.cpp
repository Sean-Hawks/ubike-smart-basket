// hwtest_tof：兩顆 VL53L0X，接在 TCA9548A（0x70）第 0、1 通道
// 預期：主 I2C 掃描看得到 TCA 的 0x70，選通道後有測距器的 0x29；每 200 ms 印出兩顆距離（mm）。
// OLED 的 0x3C 位於 GPIO13/14 的第二組 I2C。
// 用手或物品擋在下面，距離應該變短。記下空籃時的距離，用來調整 dist_mm 門檻。
#include <Arduino.h>
#include <Drivers.h>
#include <Pins.h>
#include <Wire.h>

drivers::TofSensor tof[2] = {drivers::TofSensor(pins::TOF_MUX_CH[0]), drivers::TofSensor(pins::TOF_MUX_CH[1])};

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== hwtest_tof ===");
    Wire.begin(pins::I2C_SDA, pins::I2C_SCL, pins::I2C_HZ);
    Wire.setTimeOut(25);
    drivers::i2cScan(Serial);
    for (int i = 0; i < 2; ++i) {
        if (!drivers::muxSelect(pins::TOF_MUX_CH[i])) {
            Serial.printf("無法選擇多工器通道 %d：檢查 TCA9548A 供電、0x70 位址與接線\n", pins::TOF_MUX_CH[i]);
            continue;
        }
        Serial.printf("多工器通道 %d ", pins::TOF_MUX_CH[i]);
        drivers::i2cScan(Serial);  // 應該看到 0x29
        Serial.printf("測距 %d：%s\n", i, tof[i].begin() ? "OK" : "初始化失敗");
    }
}

void loop() {
    for (int i = 0; i < 2; ++i) {
        int mm;
        if (tof[i].read(mm)) Serial.printf("測距%d = %4d mm   ", i, mm);
        else Serial.printf("測距%d = ----（超出範圍或失敗）   ", i);
    }
    Serial.println();
    delay(200);
}
