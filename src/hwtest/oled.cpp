// hwtest_oled：1.3 吋 SH1106 OLED（第二組 I2C：SDA 13、SCL 14、位址 0x3C）
// 預期：0x3C 有回應，文字可讀、白框完整、全白均勻、全黑無殘影。
// ACK 不能證明畫面正確；偏移、條紋或不可讀文字仍須查驅動、接線與實板型號。
#include <Arduino.h>
#include <Drivers.h>
#include <Pins.h>
#include <Wire.h>

drivers::OledDisplay oled(pins::OLED_ADDR);
bool ready = false;

bool displayCommand(uint8_t command) {
    Wire1.beginTransmission(pins::OLED_ADDR);
    Wire1.write(0x00);  // command, not framebuffer data
    Wire1.write(command);
    uint8_t status = Wire1.endTransmission();
    Serial.printf("OLED command 0x%02X: I2C status=%u\n", command, status);
    return status == 0;
}

void scanOledBus() {
    Serial.print("OLED I2C:");
    int found = 0;
    for (uint8_t address = 1; address < 127; ++address) {
        Wire1.beginTransmission(address);
        if (Wire1.endTransmission() == 0) {
            Serial.printf(" 0x%02X", address);
            ++found;
        }
    }
    if (!found) Serial.print(" (沒有找到任何裝置)");
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== hwtest_oled ===");
    ready = oled.begin();
    if (!ready) {
        Serial.println("OLED 初始化失敗：檢查 GPIO13/14、供電與位址 0x3C");
        return;
    }
    // 先用較慢的時脈排查線路；不改主程式的 I2C 設定。
    oled.u8g2().setBusClock(50000);
    Wire1.setClock(50000);
    oled.u8g2().setContrast(96);
    scanOledBus();
    Serial.println("OLED responded; visual inspection is still required (50 kHz).");
    Serial.println("Cycle: text / frame + solid block / all pixels ON / black, 3 seconds each.");
}

void loop() {
    if (!ready) {
        delay(1000);
        return;
    }
    static int page = 0;
    // A5 強制所有像素點亮，繞過字型、繪圖與顯示記憶體內容；A4 恢復正常顯示。
    if (!displayCommand(page == 2 ? 0xA5 : 0xA4)) {
        ready = false;
        return;
    }
    core::DisplayText t;
    if (page == 0) {
        t.line[0] = "智慧 Ubike 籃子";
        t.line[1] = "請刷卡借車";
        t.line[2] = "籃內有他人物品";
        t.line[3] = "請勿取走";
        t.line[4] = "ABCabc 0123456789";
        oled.show(t);
        Serial.println("畫面 1：中英文字（5 行都要看得到）");
    } else if (page == 1) {
        auto& u = oled.u8g2();
        u.clearBuffer();
        u.drawBox(0, 0, 128, 3);
        u.drawBox(0, 61, 128, 3);
        u.drawBox(0, 0, 3, 64);
        u.drawBox(125, 0, 3, 64);
        u.setFont(u8g2_font_6x12_tf);
        u.drawStr(8, 18, "ABC 0123456789");
        u.drawHLine(8, 22, 112);
        u.drawLine(8, 30, 72, 53);
        u.drawBox(88, 30, 28, 24);
        u.sendBuffer();
        Serial.println("Pattern: solid border, readable ASCII, solid white block.");
    } else if (page == 2) {
        Serial.println("All pixels ON: entire panel must be uniformly lit, with no black stripes.");
    } else {
        oled.u8g2().clearBuffer();
        oled.u8g2().sendBuffer();
        Serial.println("Black: entire panel must be dark.");
    }
    page = (page + 1) % 4;
    delay(3000);
}
