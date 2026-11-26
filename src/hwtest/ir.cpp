// hwtest_ir：紅外線對射（發射 GPIO 27 用 PWM 產生 38kHz，接收 GPIO 34）
// 商品標示 5V。無轉換板時先做全 3.3V 相容性初測：TX/RX VCC 都接 3V3、DAT 串 1kΩ 接 GPIO27、OUT 接 GPIO34。
// 全 3.3V 的功能及距離待實測；若採 5V，OUT 須經轉換，完整接法見 WIRING.md。
// 發射端一段一段送（600 µs 送、600 µs 停），不能連續送，否則接收頭的 AGC 會把訊號當雜訊濾掉。
// 預期：沒擋住時印「通」、收到 4/4 段；用手擋住時印「擋住」、收到 0/4 段。
// 一直顯示擋住：檢查對準、接收頭電源、IR LED 限流電阻；一直顯示通：可能是反射光，加遮光管。
#include <Arduino.h>
#include <Drivers.h>
#include <Pins.h>

drivers::IrBeam ir(pins::IR_TX, pins::IR_RX, pins::LEDC_CH_IR);

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== hwtest_ir ===");
    Serial.println("全3.3V初測：兩片VCC→3V3、GPIO27→1kΩ→DAT、OUT→GPIO34。商品標5V，相容性待測；5V接法見WIRING.md");
    ir.begin();
    pinMode(pins::IR_RX, INPUT);
    Serial.printf("不發射時接收頭輸出 = %d（應該是 1）\n", digitalRead(pins::IR_RX));
}

void loop() {
    bool blocked = ir.isBlocked();
    if (!ir.healthy()) Serial.println("IR 接收訊號異常：未發射時仍為 LOW，檢查短路／干擾");
    static bool last = !blocked;
    Serial.printf("%s（收到 %d/%d 段）%s\n", blocked ? "擋住" : "通  ", ir.lastHits(), drivers::IrBeam::kBursts,
                  blocked != last ? "  ← 變化" : "");
    last = blocked;
    delay(200);
}
