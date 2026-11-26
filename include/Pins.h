#pragma once
// 腳位與硬體位址。換板子或改接線只要改這裡（和 platformio.ini 的 board）。
#include <stdint.h>

namespace pins {

// I2C 主線：TCA9548A 多工器與兩顆 VL53L0X
constexpr int I2C_SDA = 21;
constexpr int I2C_SCL = 22;
constexpr uint32_t I2C_HZ = 400000;

// OLED 使用 ESP32 的第二組硬體 I2C，避免和 TCA9548A 共用 SDA/SCL。
// 這兩腳目前沒有被其他功能使用；GPIO13=SDA、GPIO14=SCL。
constexpr int OLED_SDA = 13;
constexpr int OLED_SCL = 14;

constexpr uint8_t TCA9548A_ADDR = 0x70;
constexpr uint8_t TOF_MUX_CH[2] = {0, 1};  // 兩顆 VL53L0X（都是 0x29）接在多工器第 0、1 通道
constexpr uint8_t OLED_ADDR = 0x3C;        // 1.3 吋 SH1106，接在第二組 I2C

// PN532 V3（SPI 模式，板子上的撥碼開關要切到 SPI）
constexpr int NFC_SCK = 18;
constexpr int NFC_MISO = 19;
constexpr int NFC_MOSI = 23;
constexpr int NFC_SS = 5;

// 照片這批 HX711 接 5V：DT 經 BSS138 降壓，SCK 經 AHCT125 升壓。接線見 WIRING.md。
constexpr int HX711_DT = 16;
constexpr int HX711_SCK = 17;

constexpr int BUZZER = 25;  // 無源蜂鳴器經 NPN 驅動，PWM
constexpr int LOCK = 26;    // MOSFET 閘極，HIGH = 通電開鎖（每次 <= 1 秒）
constexpr int IR_TX = 27;   // 全 3.3V 相容性初測：串 1kΩ 到 DAT；5V 的轉換／緩衝接法見 WIRING.md
constexpr int LID = 33;     // 蓋子開關，INPUT_PULLUP
#ifdef BASKET_IR_RX_32
constexpr int IR_RX = 32;   // 診斷接法：借用按鈕腳測接收板 OUT
constexpr bool IR_RX_PULLUP = true;
constexpr int BUTTON = -1;  // 診斷期間不讀實體開蓋按鈕
#else
constexpr int IR_RX = 34;   // 全 3.3V 初測 OUT 直連；5V OUT 須經轉換。收到載波 LOW，此腳無內部上拉
constexpr bool IR_RX_PULLUP = false;
constexpr int BUTTON = 32;  // 開蓋按鈕，INPUT_PULLUP，按下 = LOW
#endif

// 蓋子關上時讀到的電位。開關接法相反（常閉 / 磁簧方向不同）就改成 1。
constexpr int LID_CLOSED_LEVEL = 0;

// LEDC 通道：蜂鳴器和紅外線要用不同計時器（ch0/1 共用 timer0，ch2/3 共用 timer1）
constexpr int LEDC_CH_BUZZER = 0;
constexpr int LEDC_CH_IR = 2;

}  // namespace pins
