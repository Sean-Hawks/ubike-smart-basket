// hwtest_hx711：5kg 秤重感測器 + HX711（5V；DT16/SCK17 經電位轉換，見 WIRING.md）
//
// 校正步驟（序列埠監看視窗輸入，115200，結尾要送換行）：
//   1. 籃子清空，輸入 t      → 歸零（記錄 offset）
//   2. 放上已知重量的物品，輸入 c 和公克數，例如 c 200   → 算出校正係數 scale
//   3. 輸入 s               → 存進 Preferences（主程式開機時會讀）
//   其他：r = 顯示原始值，p = 顯示目前的校正值
//
// 校正係數 = (放物品時的原始平均 - offset) / 公克數
#include <Arduino.h>
#include <Drivers.h>
#include <Pins.h>
#include <cmath>

drivers::Hx711Scale scale(pins::HX711_DT, pins::HX711_SCK);
long offset = 0;
float factor = 1.0f;
bool tared = false, calibrated = false;

bool averageRaw(int n, long& result) {
    int64_t sum = 0;
    for (int i = 0; i < n; ++i) {
        if (!scale.raw().wait_ready_timeout(1000)) {
            Serial.println("讀取逾時，校正未變更；檢查 HX711 接線");
            return false;
        }
        sum += scale.raw().read();
    }
    result = (long)(sum / n);
    return true;
}

void printCal() { Serial.printf("目前 offset = %ld，scale = %.4f\n", offset, factor); }

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== hwtest_hx711 ===");
    bool had = drivers::loadHx711Calibration(offset, factor);
    tared = calibrated = had;
    scale.begin(offset, factor);
    Serial.println(had ? "已讀取之前存的校正值" : "還沒校正過（offset=0, scale=1）");
    printCal();
    if (!scale.raw().wait_ready_timeout(1000)) {
        Serial.println("HX711 沒有回應：檢查 5V 供電、DT/SCK 轉換與共地（見 WIRING.md）");
    }
    Serial.println("指令：t=歸零  c <公克>=校正  s=儲存  r=原始值  p=顯示校正值");
}

void loop() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        if (line == "t") {
            Serial.println("歸零中，請不要碰籃子...");
            long raw;
            if (!averageRaw(20, raw)) return;
            offset = raw;
            tared = true;
            scale.raw().set_offset(offset);
            Serial.printf("offset = %ld\n", offset);
        } else if (line.startsWith("c")) {
            float grams = line.substring(1).toFloat();
            if (!tared) {
                Serial.println("先清空籃子並輸入 t 歸零");
            } else if (!std::isfinite(grams) || grams <= 0) {
                Serial.println("用法：c 200（放上的物品是 200 公克）");
            } else {
                Serial.printf("量測 %.1f g 的物品中...\n", grams);
                long raw;
                if (!averageRaw(20, raw)) return;
                float next = (raw - offset) / grams;
                if (!std::isfinite(next) || next == 0) {
                    Serial.println("沒有量到重量變化，拒絕校正；檢查感測器安裝與砝碼");
                    return;
                }
                factor = next;
                calibrated = true;
                scale.raw().set_scale(factor);
                Serial.printf("原始平均 %ld，scale = (%ld - %ld) / %.1f = %.4f\n", raw, raw, offset, grams, factor);
                Serial.println("確認讀數正確後輸入 s 儲存");
            }
        } else if (line == "s") {
            if (!tared || !calibrated) { Serial.println("請先完成 t 與 c 校正"); return; }
            Serial.println(drivers::saveHx711Calibration(offset, factor) ? "已存入 Preferences（namespace hx711）" : "儲存失敗，請勿使用主程式");
            printCal();
        } else if (line == "r") {
            long raw;
            if (averageRaw(5, raw)) Serial.printf("原始值 %ld\n", raw);
        } else if (line == "p") {
            printCal();
        }
    }

    static uint32_t last = 0;
    if (millis() - last >= 500) {
        last = millis();
        float g;
        if (scale.read(g)) Serial.printf("重量 %.1f g\n", g);
        else Serial.println("等待 HX711 資料...");
    }
}
