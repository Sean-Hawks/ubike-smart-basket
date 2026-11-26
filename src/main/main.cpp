// 主程式：核心邏輯（lib/core）+ 真實驅動（lib/drivers）+ 網頁（Web.cpp）
#include <Arduino.h>
#include <Basket.h>
#include <Drivers.h>
#include <ESPmDNS.h>
#include <esp_system.h>
#include <Pins.h>
#include <WiFi.h>
#include <Wire.h>

#include "Web.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif
#ifndef AP_SSID
#define AP_SSID "SmartBasket"
#endif
#ifndef AP_PASS
#define AP_PASS ""
#endif

namespace {

constexpr uint32_t kTickMs = 50;
constexpr uint32_t kMonitorMs = 250;
#ifdef BASKET_WEB_DISPLAY
constexpr bool kWebDisplay = true;
#else
constexpr bool kWebDisplay = false;
#endif
#ifdef BASKET_TOF_TEST
constexpr bool kTofTest = true;
// 測試車籃的實測空籃最低值是 95 / 114 mm。各路低於該值就判定有物。
constexpr core::DistanceCutoffs kDistanceCutoffsMm{94, 113};
#else
constexpr bool kTofTest = false;
constexpr core::DistanceCutoffs kDistanceCutoffsMm{};
#endif
#ifdef BASKET_IR_MONITOR
constexpr bool kIrActive = true;
#else
constexpr bool kIrActive = !kTofTest;
#endif

drivers::SolenoidLock lock(pins::LOCK);
drivers::OledDisplay oled(pins::OLED_ADDR);
drivers::TofSensor tof0(pins::TOF_MUX_CH[0]), tof1(pins::TOF_MUX_CH[1]);
drivers::Hx711Scale scale(pins::HX711_DT, pins::HX711_SCK);
drivers::IrBeam ir(pins::IR_TX, pins::IR_RX, pins::LEDC_CH_IR, pins::IR_RX_PULLUP);
drivers::Pn532Reader nfc(pins::NFC_SCK, pins::NFC_MISO, pins::NFC_MOSI, pins::NFC_SS);
drivers::PwmBuzzer buzzer(pins::BUZZER, pins::LEDC_CH_BUZZER);
drivers::LidSwitch lid(pins::LID, pins::LID_CLOSED_LEVEL);
struct ManualLid : core::ILidSwitch {
    bool closed = false;
    bool isClosed() override { return closed; }
} manualLid;
drivers::Button button(pins::BUTTON);
drivers::PrefsStorage storage;

core::Basket basket(core::Hardware{kWebDisplay ? nullptr : &scale, &tof0, &tof1, kTofTest ? nullptr : &ir, &nfc,
                                  &lock, &buzzer,
                                  kWebDisplay ? static_cast<core::ILidSwitch*>(&manualLid) : &lid,
                                  &button, &oled, &storage, kDistanceCutoffsMm});

std::string hwStatus;
bool hardwareReady = true;

#ifdef BASKET_IR_MONITOR
volatile uint32_t irRxPulses = 0;
void IRAM_ATTR countIrPulse() {
    ++irRxPulses;
}
#endif

struct Monitor {
    core::Reading reading;
    bool irValid = false;
    bool irBlocked = false;
    bool irSampled = false;
    uint32_t lastIrSample = 0;
    bool sampled = false;
    bool buttonPressed = false;
    uint32_t buttonPresses = 0;

    void update(uint32_t now) {
        const bool pressed = button.isPressed();
        if (pressed && !buttonPressed) ++buttonPresses;
        buttonPressed = pressed;

        const bool lidClosed = basket.lidClosed();
        if (lidClosed) {
            reading = basket.current();
            if (!kTofTest) {
                irValid = basket.irValid();
                irBlocked = reading.irBlocked;
            }
        } else {
            // 開蓋的讀值只供查看。Basket 仍會清除借還車判定用的讀值。
            static uint32_t last = 0;
            if (sampled && now - last < kMonitorMs) return;
            last = now;
            reading = {};
            reading.distValid[0] = tof0.read(reading.distMm[0]);
            reading.distValid[1] = tof1.read(reading.distMm[1]);
        }
        // 測距測試的紅外線只供觀測。開蓋時正式版也由監測獨立取樣。
        if (kIrActive && (kTofTest || !lidClosed) && (!irSampled || now - lastIrSample >= kMonitorMs)) {
            irBlocked = ir.isBlocked();
            irValid = ir.healthy();
            irSampled = true;
            lastIrSample = now;
        }
        sampled = true;
    }

    std::string json() const {
        std::string out = std::string("{\"lockOn\":") + (lock.energized() ? "true" : "false") +
                          ",\"lockPulses\":" + std::to_string(lock.pulseCount()) +
                          ",\"buzzerHz\":" + std::to_string(buzzer.frequency()) +
                          ",\"buzzerLastHz\":" + std::to_string(buzzer.lastFrequency()) +
                          ",\"buzzerTones\":" + std::to_string(buzzer.toneCount()) +
                          ",\"buttonPressed\":" + (buttonPressed ? "true" : "false") +
                          ",\"buttonPresses\":" + std::to_string(buttonPresses) +
                          ",\"buttonPin\":" + std::to_string(pins::BUTTON) +
                          ",\"nfcStage\":\"" + nfc.initStage() +
                          "\",\"nfcAttempts\":" + std::to_string(nfc.initAttempts()) +
                          ",\"nfcFirmware\":" + std::to_string(nfc.firmware()) +
                          ",\"irHits\":" + (sampled && kIrActive ? std::to_string(ir.lastHits()) : "null") +
                          ",\"tofTest\":" + (kTofTest ? "true" : "false") +
                          ",\"liveSensors\":{\"irValid\":" + (irValid ? "true" : "false") +
                          ",\"irBlocked\":" + (irValid ? (irBlocked ? "true" : "false") : "null");
        for (int i = 0; i < 2; ++i) {
            out += ",\"dist" + std::to_string(i) + "\":" +
                   (reading.distValid[i] ? std::to_string(reading.distMm[i]) : "null");
        }
        out += "},\"irRxPin\":" + (kIrActive ? std::to_string(pins::IR_RX) : std::string("null"));
        out += ",\"irTxPin\":" + (kIrActive ? std::to_string(pins::IR_TX) : std::string("null"));
#ifdef BASKET_IR_MONITOR
        out += ",\"irRxPulses\":" + std::to_string(irRxPulses);
        out += std::string(",\"irRxHigh\":") + (digitalRead(pins::IR_RX) == HIGH ? "true" : "false");
#endif
        return out + "}";
    }
} monitor;

void addStatus(const char* name, bool ok, bool required = true) {
    hardwareReady = hardwareReady && (!required || ok);
    Serial.printf("  %-8s %s\n", name, ok ? "OK" : "失敗");
    if (!hwStatus.empty()) hwStatus += ",";
    hwStatus += std::string("\"") + name + "\":" + (ok ? "true" : "false");
}

void bootScreen(const char* l1, const std::string& l2 = "") {
    core::DisplayText t;
    t.line[0] = "智慧 Ubike 籃子";
    t.line[1] = l1;
    t.line[2] = l2;
    oled.show(t);
}

std::string startWifi() {
    WiFi.mode(WIFI_STA);
    if (strlen(WIFI_SSID) > 0) {
        bootScreen("連線 Wi-Fi 中", WIFI_SSID);
        WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
            Serial.printf("Wi-Fi 中斷 reason=%u\n",
                          static_cast<unsigned>(info.wifi_sta_disconnected.reason));
        }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) delay(250);
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("Wi-Fi 已連線：http://%s\n", WiFi.localIP().toString().c_str());
            return WiFi.localIP().toString().c_str();
        }

        // 只掃描設定的網路，區分看不到 2.4 GHz 訊號與連線認證失敗。
        WiFi.setAutoReconnect(false);
        WiFi.disconnect();
        const int matches = WiFi.scanNetworks(false, true, false, 300, 0, WIFI_SSID);
        Serial.printf("Wi-Fi 2.4 GHz 掃描：符合設定名稱的基地台=%d\n", matches);
        for (int i = 0; i < matches; ++i) {
            Serial.printf("Wi-Fi 基地台：頻道=%ld，訊號=%ld dBm\n",
                          static_cast<long>(WiFi.channel(i)), static_cast<long>(WiFi.RSSI(i)));
        }
        WiFi.scanDelete();
    }
    // 連不上就自己開熱點
    WiFi.mode(WIFI_AP);
    char generated[17];
    const char* passphrase = AP_PASS;
    if (strlen(passphrase) < 8 || strlen(passphrase) > 63) {
        uint32_t randomWords[2];
        esp_fill_random(randomWords, sizeof(randomWords));
        snprintf(generated, sizeof(generated), "%08lx%08lx",
                 static_cast<unsigned long>(randomWords[0]),
                 static_cast<unsigned long>(randomWords[1]));
        passphrase = generated;
    }
    if (!WiFi.softAP(AP_SSID, passphrase)) {
        Serial.println("備援熱點啟動失敗，仍可用 USB 收集資料");
        return "0.0.0.0";
    }
    Serial.printf("開啟熱點 %s（密碼 %s）：http://%s\n", AP_SSID, passphrase, WiFi.softAPIP().toString().c_str());
    return WiFi.softAPIP().toString().c_str();
}

}  // namespace

void setup() {
    lock.begin();  // 第一件事：確保電控鎖斷電
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== 智慧 Ubike 籃子 ===");

    Wire.begin(pins::I2C_SDA, pins::I2C_SCL, pins::I2C_HZ);
    Wire.setTimeOut(25);
    Serial.println("硬體初始化：");
    addStatus("oled", oled.begin(), !kWebDisplay);
    bootScreen("硬體初始化中");
    addStatus("tca9548a", drivers::muxSelect(pins::TOF_MUX_CH[0]));
    addStatus("tof0", tof0.begin());
    addStatus("tof1", tof1.begin());
    if (!kWebDisplay) {
        long off;
        float sc;
        bool cal = drivers::loadHx711Calibration(off, sc);
        scale.begin(off, sc);
        addStatus("hx711", scale.raw().wait_ready_timeout(500));
        addStatus("hx711cal", cal);
    }
    addStatus("nfc", nfc.begin());
    Serial.printf("  PN532    stage=%s attempts=%d firmware=0x%08lx\n",
                  nfc.initStage(), nfc.initAttempts(), static_cast<unsigned long>(nfc.firmware()));
    if (kIrActive) ir.begin();
#ifdef BASKET_IR_MONITOR
    attachInterrupt(digitalPinToInterrupt(pins::IR_RX), countIrPulse, FALLING);
#endif
    buzzer.begin();
    if (!kWebDisplay) lid.begin();
    button.begin();
    addStatus("storage", storage.begin(kTofTest ? "basket_test" : "basket"));
    basket.setHardwareReady(hardwareReady);

    std::string ip = startWifi();
    if (MDNS.begin("smartbasket")) MDNS.addService("http", "tcp", 80);
    webBegin(basket, "{" + hwStatus + "}", [] { return monitor.json(); },
             kWebDisplay ? +[](bool closed) { manualLid.closed = closed; } : nullptr);
    bootScreen("網頁 http://", ip);
    delay(1500);

    basket.begin(millis());
}

void loop() {
    lock.loop();
    webLoop();
    if (kWebDisplay) webSerialLoop();
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last >= kTickMs) {
        last = now;
        basket.tick(now);
        monitor.update(now);
    }
}
