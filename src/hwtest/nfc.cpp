// hwtest_nfc：PN532 V3（SPI：SCK 18、MISO 19、MOSI 23、SS 5）
// 預期：印出韌體版本；把卡片靠近時印出 UID（這個 UID 就是帳號）。
// 找不到 PN532：確認板子上的撥碼開關切到 SPI（通常是 SEL0=OFF、SEL1=ON），以及 3.3V/GND。
#include <Arduino.h>
#include <Drivers.h>
#include <Pins.h>

drivers::Pn532Reader nfc(pins::NFC_SCK, pins::NFC_MISO, pins::NFC_MOSI, pins::NFC_SS);
bool ok = false;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== hwtest_nfc ===");
    ok = nfc.begin();
    if (!ok) {
        Serial.println("找不到 PN532：檢查 SPI 接線與撥碼開關");
        return;
    }
    uint32_t v = nfc.firmware();
    Serial.printf("PN5%02X 韌體 %u.%u\n", (unsigned)((v >> 24) & 0xFF), (unsigned)((v >> 16) & 0xFF),
                  (unsigned)((v >> 8) & 0xFF));
    Serial.println("請把卡片靠近讀卡機...");
}

void loop() {
    if (!ok) {
        delay(1000);
        return;
    }
    std::string uid;
    if (nfc.poll(uid)) Serial.printf("[%lu ms] 卡片 UID = %s\n", millis(), uid.c_str());
    delay(10);
}
