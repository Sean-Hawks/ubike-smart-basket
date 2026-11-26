# CLAUDE.md — 智慧 Ubike 籃子

學校專題（一個月內完成），ESP32 + PlatformIO。用繁體中文溝通。不要過度設計。
完整說明見 README.md。依零件照片整理的逐腳接線、缺件、電源／驅動電路與測試指令見 WIRING.md。到貨檢查、通過標準與實測紀錄見 HARDWARE_ACCEPTANCE.md。

## 架構

- `lib/core/`：**純 C++，不能 include Arduino、不能呼叫 millis()**。時間一律由外部傳入 `uint32_t now`（ms），比較時間用 `reached(now, t)` 處理溢位。
  - `Config.h`：所有可調參數（罰款 15、回饋 10、倒數 15 s、確認時限 120 s、門檻 10 g / 20 mm、開鎖 600 ms）。`Config::set()` 會拒絕不合法的值：**回饋必須 < 罰款**、開鎖 ≤ 1000 ms。
  - `Hal.h`：硬體介面（IWeightSensor、IDistanceSensor、IIrBeam、ICardReader、ILock、IBuzzer、ILidSwitch、IButton、IDisplay、IStorage）。
  - `Detector.*`：`Smoother`（移動平均）+ `detectItems(baseline, now, cfg)`，任一條件成立 = 有東西；`>=` 門檻就算（邊界值有測試）。
  - `Basket.*`：狀態機 Boot → Idle → Riding → Countdown。`tick(now)` 每 50 ms 呼叫一次：取樣 → 按鈕 → 讀卡 → 計時 → 蜂鳴器 → OLED。
  - `Accounts.*`、`EventLog.*`、`Json.*`（網頁 API 的 JSON 在這裡產生，電腦上可測）。
- `lib/fakes/Fakes.h`：假硬體，`fakes::Rig` 一次建好一整組。測試和模擬器共用。
- `lib/drivers/`：ESP32 真實驅動（只在 espressif32 平台編譯）。`Pwm.h` 是 LEDC 2.x/3.x 相容層。
- `src/main/`（env `esp32dev`）、`src/sim/`（env `native` 的 program）、`src/hwtest/*.cpp`（env `hwtest_*`，每個零件一支）。用 `build_src_filter` 分開。
- `include/Pins.h`：所有腳位與 I2C 位址。ESP32 env 有 `-Iinclude` 讓 lib 也能 include。

## 規則重點（改邏輯前先看測試 test/test_basket）

- 只有借車中（和還車倒數中）按鈕能開鎖。同一張卡 2 秒內重複感應只算一次。
- 借車、還車都要關蓋，重量、兩顆測距及 IR 累積 5 次有效輪詢；失效即清掉該平滑窗口。HX711 驅動舊值最多沿用 500 ms。開機任一硬體／HX711 校正失敗，或 NVS 寫入失敗，停止借還車。
- 還車判定：跟空籃基準比 → 空的就完成；借車時籃子本來就有標記（垃圾 / 遺失物 / 待確認）的話，再跟「借車當下的讀數 snapshot」比，沒多出東西就不算這位的；否則倒數。
- 倒數中變空 → 提早完成、不扣錢；倒數結束還有東西 → `Pending`，不扣錢。
- `Pending` 逾時或確認垃圾 → 扣罰款；若確認前已被別人清空 → 直接給清理者回饋。確認遺失物 → 不扣錢、標記遺失物（下一位 OLED「請勿取走」，清空不給回饋）。
- 有垃圾標記時，借車期間看過垃圾的人（`trashWitness_`）清空還車才給回饋；清自己的垃圾不給。
- 空籃基準：首次清空關蓋後手動 calibrate；之後開機／借車不自動覆寫。有借用、標記、待確認或 IR 遮斷時拒絕校正。改設定須無借用／待確認。
- Preferences namespace `basket` 使用單筆 `state` 儲存帳戶、標記、基準、設定、借用狀態及 snapshot；讀取相容舊版 `accts/flags/base/cfg`。`IStorage::save` 回傳成功與否，失敗則停止後續操作。
- 重啟恢復借用者／倒數與原基準；倒數及確認期限重新給完整寬限時間。HX711 校正值在 namespace `hx711`（`offset`、`scale`），由 `hwtest_hx711` 寫入。

## 腳位

TCA9548A 主 I2C SDA 21 / SCL 22（VL53L0X 在通道 0、1）；OLED SH1106 第二組 I2C SDA 13 / SCL 14；PN532 SPI SCK 18 / MISO 19 / MOSI 23 / SS 5；HX711 DT 16 / SCK 17；蜂鳴器 25（LEDC ch0）；電控鎖 26；IR 發射 27（LEDC ch2，38kHz 分段送 600/600 µs）；IR 接收 34；蓋子 33；按鈕 32。

2026-11-16 照片對應的 IR 商品標示 5V。2026-11-18 使用者確認現階段沒有麵包板、BSS138 轉換板或 AHCT125，有 1kΩ 電阻。使用者確認秤重、蜂鳴器、12V電控鎖尚未接；PN532已接；按鈕與蓋子開關未確認。IR已採全3.3V初測接法：兩板VCC均3V3、共地、GPIO27→1kΩ→DAT、OUT直連GPIO34。使用者回報約10cm：對準47/47筆通4/4、遮斷40/40筆擋住0/4、恢復49筆均通（48筆4/4、1筆3/4），閒置LOW異常0次。實際籃寬、固定安裝穩定性與物品覆蓋仍待驗證；商品原規格仍為5V。若改5V，OUT須經轉換。備齊BSS138時可用HV1/LV1接RX34、LV3/HV3加1kΩ接TX27；AHCT125+680Ω為較強發射方案。HX711文件仍採5V方案，DT16經BSS138通道2、SCK17經AHCT125，須備齊相應零件。TCA主線21/22與兩顆VL53L0X於接線修正後均通過實測，各57/57筆有效；OLED第二組I2C13/14有0x3C ACK，但不能算顯示正常：已修正U8g2 Wire1未啟用而不傳資料的設定（platformio.ini必須保留-DU8X8_HAVE_2ND_HW_I2C），面板從全黑變成有畫面，但使用者回報白框條紋、文字完全不可讀；A/B測試SH1106較好但仍未通過。hwtest_oled的50kHz A5整片點亮測試中，使用者確認也有黑條或缺線，不能歸因於字型；仍需查初始化、供電與實板型號／面板，未判定零件損壞。PN532已由近照確認ELECHOUSE V3及雙黃滑塊，模式1OFF/2ON，SPI18/19/23/5。VCC原3V3讀不到晶片；改接ESP32的5V電源腳後，讀到韌體1.6、首張卡20筆UID一致，學生證拿開／靠近3次UID一致。兩組VCC/GND相通，只接一組，不能同時接3V3與5V。後續多協定診斷已確認同學卡也是ISO14443A，連續3輪UID一致且不同於使用者學生證；目前兩卡ID區分測試已達成，不推定所有卡種皆支援。蜂鳴器用NPN，鎖用獨立功率MOSFET。單體工具 `python3 tools/bringup.py flash <項目> --port <埠>`，不自動選埠或erase。

## 慣例

- 平台鎖 `espressif32@6.12.0`（Arduino core 2.0.17）。升級前先確認 LEDC API。
- OLED 中文：U8g2 內建字型沒有繁體，用 `tools/gen_oled_glyphs.py` 產生 `lib/drivers/OledGlyphs.h`（掃 `lib/core/Basket.cpp`、`src/main/main.cpp`、`src/hwtest/oled.cpp` 字串裡的中文）。**改 OLED 中文字串後要重跑**，CI 會 `--check`。OLED 5 行、每行約 10 個中文字（128 px / 12 px）。
- 不要把 `include/secrets.h` 加進 git。
- 元件測試入口：macOS `test_hardware.command` 或 `python3 tools/bringup.py` 中文選單；`guide <元件>` 離線顯示操作。不得將編譯／燒錄成功當實機通過，不自動選 USB 埠或送開鎖命令。工具回歸檢查：`python3 -m unittest discover -s test_tools -v`。
- 每次改完跑：`pio test -e native`、`pio run -e native && .pio/build/native/program < scripts/demo.txt`、`pio run`。
- CI（.github/workflows/ci.yml）：單元測試、兩個模擬器示範（檢查最終餘額）、點陣缺字檢查、編譯全部 env。

## 還沒在實機驗證的

2026-11-18 已完成兩顆 VL53L0X 經 TCA 的有效讀值初測，以及全3.3V IR約10cm對準／遮斷／恢復初測。OLED亮起但條紋、文字不可讀，依使用者要求停止實機調整、只做型號調查，仍未通過。PN532改5V後已完成基本讀取與學生證三次重新感應；同學卡與長時間穩定性仍待驗證。HX711校正、按鈕／蓋子開關、蜂鳴器、實際電控鎖與Ticker斷電、實際籃寬／安裝後感測覆蓋及整機流程仍待驗證；以 HARDWARE_ACCEPTANCE.md 最新紀錄為準。

最新診斷狀態（2026-11-18）：使用者要求停止OLED實機調整，只做型號調查；已關閉顯示，正式OLED驅動未換ALT0/COM02。OLED黑線非128x64規格的正常限制，但單片、批次或實際控制器相容性未定，HARDWARE_ACCEPTANCE.md已附官方及原始個案來源。NFC VCC改ESP32 5V後fw1.6；SPI18/19/23/SS5、模式1OFF/2ON，兩組VCC/GND同源只接一組。使用者學生證3次重新感應UID一致；同學卡在多協定診斷ISO14443A連續3輪讀到另一個固定UID。使用者最新明確只要確認不同卡有不同ID以對應身份，這兩卡測試已達成，不再擴張持續貼卡優化。額外比較：LIBRARY150/FULLFRAME150/FULLFRAME1000各1/8，FIELD_RESET_FULLFRAME150為8/8，屬卡片重新啟用流程差異；正式Pn532Reader未改。板上現在/tmp/ubike-oled-compare-20261118/probe.cpp為此比較診斷，完成會COMPARISON_DONE並停止，送r重跑；OLED保持關閉。各log在/tmp/ubike-nfc-protocol-identification-20261118.serial.log、/tmp/ubike-nfc-response-comparison-20261118.serial.log。最早20筆首卡身份未確認，勿當成使用者學生證或另張確定實卡來源；最新兩卡標籤依使用者說明。 最新覆蓋：使用者已重新要求測螢幕，先前OLED暫停對本輪測試已撤回。現在板上已燒錄正式專案hwtest_oled（13/14、3V3、50kHz，每3秒文字/框白塊/全白/全黑）；15秒串口0x3C及A4/A5狀態0，仍待本輪目視回覆，不能宣告畫面正常。NFC比較診斷已被替換，勿再聲稱目前NFC正在運行。

2026-11-16 的軟體整備新增感測器失效保護、首次人工基準、借用狀態復原、整筆 NVS 儲存、設定還原修正、PN532 有限次重試、OLED ACK 檢查與 HX711 校正逾時。54 個 native 測試通過仍不代表實機驗收通過；依 HARDWARE_ACCEPTANCE.md 填實測結果。
