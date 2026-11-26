# 騎淨共享｜共享單車智慧車籃

騎士刷同一張 NFC 卡還車時，ESP32 會檢查關蓋後的車籃讀值。系統發現新遺留物會先提醒騎士取回；倒數結束仍有物品時，再由原借用者在網頁回覆。扣款與清理回饋均使用專題模擬帳戶。

**展示架構**　ESP32 執行借還狀態機及基本操作網頁。Mac 透過 USB 或區域網路取得遙測，由 Python 伺服器將快照、卡片活動及模擬餘額歸檔至 SQLite。瀏覽器可看歷史、回覆待確認物品，也能在開發者頁訂閱即時訊號。團隊透過 Tailscale Serve 把 Mac 上的網頁提供給私有網路內的裝置。

2026-11-23 的雙 ToF 整機測試資料收錄 4 次借車及 4 次還車。IR 通道仍在診斷，重量通道停用，電控鎖尚未接線。機構、感測覆蓋與戶外使用仍待驗收。[接線與首次測試](WIRING.md)・[實機驗收紀錄](HARDWARE_ACCEPTANCE.md)

## 運作方式

- 借車時記下當時的籃內狀態。如果已有被標記的物品，還車時會另外和借車快照比較，避免把未變動的舊物記到下一位騎士名下。完整版規劃以按鈕和電控鎖開蓋，桌面展示可在網頁回報蓋子狀態。
- **還車**（同一張卡再刷一次）時如果籃內還有東西，先進入 15 秒取回倒數。桌面展示以網頁顯示，完整版另有蜂鳴器及 OLED 提示。倒數結束還有東西，就記成「待確認」，先不扣錢。
- 網頁通知上一位使用者確認：
  - 選「是垃圾」或超過時限沒回應（預設 2 分鐘，可調）：**扣 15 元**，籃子標記為「有垃圾」。
  - 選「是我的東西」：不扣錢，籃子標記為「有遺失物」，下一位借車時 OLED 顯示「請勿取走」，清空也不給回饋。
- 籃子標記「有垃圾」時，下一位借車的人還車時籃子是空的，就發 **10 元回饋金**。回饋金一定小於罰款（程式強制），防止兩人串通賺錢；清自己留的垃圾也不給回饋。
- 帳戶餘額和事件紀錄可在網頁上看，OLED 也會顯示目前狀態。

### 「籃內有東西」的判定

完整版首次使用須先完成 HX711 校正，清空籃子、關蓋，等讀數有效後在網頁記錄「空籃基準」：重量、兩顆測距的距離。之後開機及借車沿用已存基準，不自動把目前物品歸零。符合**任一條件**就算有東西：

| 條件 | 預設門檻 | 設定名稱 |
|---|---|---|
| 重量比基準多出 | ≥ 10 g | `weight_g` |
| 任一顆測距比基準短 | ≥ 20 mm | `dist_mm` |
| 紅外線對射被擋住 | — | — |

讀數取最近 5 次有效輪詢平均（`kSmoothSamples`），蓋子打開時不判定（測距會照到別處），關上後重新累積。重量、兩顆測距及 IR 都要有效，才能借還車；失效的讀數會清除，HX711 舊值最多沿用 500 ms。一般版的距離縮短門檻與重量差可由設定調整，會存進 ESP32 內部儲存；借車／倒數／待確認期間禁止改設定，借車中或有物品標記時不能重新校正。

雙 ToF 測試版使用兩路固定門檻：第 1 路 ≤94 mm 或第 2 路 ≤113 mm 即判有物。這組門檻是針對桌面車籃設定；首次使用仍須保存有效的空籃基準。`esp32dev_web_tof_ir34` 可同步觀察 GPIO34 的紅外線診斷訊號，借還判定只採用雙路距離。

## 硬體接線

主控板：傳統 ESP32（目前用 `esp32dev`，需確認 GPIO16/17 未被 PSRAM 佔用；C3/S3/S2 不能直接照此腳位表，換板子須改 `platformio.ini` 的 `board` 和 `include/Pins.h`）。

以下是訊號對照摘要，**不是全部都能直接連 GPIO**。照片這組 IR 標示 5 V；使用者目前沒有轉換板，可先將 IR 兩板都用 3.3 V 供電、DAT 串 1 kΩ，做待驗證的相容性初測。若採 5 V IR，接收 OUT 須經轉換；發射可用 BSS138 + 1 kΩ 初測，或 AHCT125 + 680 Ω 較強輸出方案。各方案的實際籃寬都須驗收。本表 HX711 採 5 V 方案，DT 經 BSS138、SCK 經 AHCT125。電源、每條轉換線與功率驅動請照 [WIRING.md](WIRING.md)。

TCA9548A 是 I²C 分接／切換器：兩顆 VL53L0X 都是 `0x29`，要靠 TCA 分成第 0、1 路。現在 OLED 已改用 ESP32 第二組 I²C（SDA=GPIO13、SCL=GPIO14），TCA／兩顆測距維持主線 GPIO21/22，因此 OLED 不再和 TCA 共用 SDA/SCL。沒有麵包板時可直接焊短線；詳見 [WIRING.md](WIRING.md) 的直接焊接小節。

| 功能 | 零件 | 介面 / 腳位 |
|---|---|---|
| I2C 主線 | TCA9548A、兩顆 VL53L0X | SDA 21、SCL 22 |
| I2C 多工器 | TCA9548A（0x70） | I2C 主線 |
| 測距 ×2 | VL53L0X（都是 0x29） | 接 TCA9548A 第 0、1 通道；裝在蓋子內側往下照 |
| 顯示 | 1.3 吋 OLED 128x64（0x3C） | 第二組 I2C：SDA 13、SCL 14；供應商標示 SH1106，實機畫面仍有條紋／不可讀，尚未驗收通過 |
| NFC | PN532 V3 | VCC→ESP32 5V、GND 共地；SPI：SCK 18、MISO 19、MOSI 23、SS 5，訊號 3.3V |
| 秤重 | 5kg 秤重感測器 + HX711 | HX711 用 5 V；DT → BSS138 → GPIO16、GPIO17 → AHCT125 → SCK |
| 蜂鳴器 | 無源蜂鳴器 | GPIO25 經 NPN 驅動，用 PWM 發聲；該款中間腳不用 |
| 電控鎖 | 12V 350mA，用 MOSFET 驅動 | GPIO 26；通電開鎖，每次只通電 1 秒以內，不能長時間通電 |
| 紅外線發射 | 商品標 5 V 的發射模組 | 全 3.3 V 相容性初測：GPIO27 → 1 kΩ → DAT。5 V 轉換／緩衝方案見 WIRING.md。PWM 38 kHz，分段送 600 µs、停 600 µs |
| 紅外線接收 | 商品標 5 V 的接收模組 | 全 3.3 V 初測 OUT → GPIO34；5 V 供電時 OUT 須經轉換。左右對射 |
| 蓋子開關 | 微動或磁簧開關 | GPIO 33，內部上拉 |
| 開蓋按鈕 | 按鈕 | GPIO 32，內部上拉；只在借車期間有效 |

接線注意：

- 電控鎖：12V 電源和 ESP32 要**共地**；線圈兩端並聯飛輪二極體；MOSFET 閘極加 10kΩ 下拉，開機時才不會誤開鎖。程式限制每次通電 ≤ 1 秒、兩次之間至少隔 1.5 秒。
- GPIO34 只能輸入且**沒有內部上拉**；本方案由 BSS138 的 LV 側上拉到 3.3 V，不能直接接 5 V OUT。4 路邏輯轉換板不能推鎖、不能當電源降壓器。
- PN532 V3 板上的撥碼開關要切到 **SPI**。
- 蓋子開關接法：開關一端接 GPIO 33、一端接 GND，蓋子關上時導通（讀到 LOW）。接法相反就改 `include/Pins.h` 的 `LID_CLOSED_LEVEL`。

## 開發環境

需要 [PlatformIO](https://platformio.org/)（VS Code 外掛或 `pip install platformio`）。

`platformio.ini` 鎖定 `espressif32@6.12.0`，內建 **Arduino core 2.0.17**，所以 LEDC（PWM）用 2.x 的通道 API（`ledcSetup` / `ledcAttachPin`）。`lib/drivers/Pwm.h` 也寫好了 3.x 的分支，之後升級平台不用改其他程式。

### Wi-Fi 設定

```bash
cp include/secrets.example.h include/secrets.h
```

填入 Wi-Fi 帳密（`secrets.h` 不會進 git）。沒有 `secrets.h` 或連不上時，ESP32 會自己開熱點 `SmartBasket`（從 USB 序列埠查看本次密碼），手機連上後開 <http://192.168.4.1>。連上 Wi-Fi 時可以開 <http://smartbasket.local> 或序列埠印出的 IP。

## 編譯與燒錄

| env | 用途 |
|---|---|
| `native` | 電腦端：單元測試 + 終端機模擬器 |
| `esp32dev` | 主程式（核心邏輯 + 真實驅動 + 網頁） |
| `hwtest_oled` | OLED：中英文字、外框（檢查是不是真的 SH1106） |
| `hwtest_nfc` | PN532：印韌體版本、卡片 UID |
| `hwtest_tof` | 兩顆 VL53L0X：I2C 掃描、多工器通道、距離 |
| `hwtest_hx711` | 秤重：**校正流程**，結果存進 Preferences |
| `hwtest_buzzer` | 蜂鳴器：音階、提示音、倒數警報 |
| `hwtest_ir` | 紅外線對射：通 / 擋住、每次收到幾段 |
| `hwtest_lock` | 電控鎖：按按鈕或序列埠輸入 `u` 開鎖 500 ms |
| `hwtest_inputs` | 蓋子開關、開蓋按鈕 |

```bash
pio run                                   # 編譯全部 env
pio run -e esp32dev -t upload -t monitor  # 燒錄主程式並開序列埠
pio run -e hwtest_oled -t upload -t monitor
```

### 硬體到了的建議順序

macOS 可直接雙擊專案內的 **`test_hardware.command`**。其他環境執行 `python3 tools/bringup.py`：中文選單選元件 → 看接線／操作／正常結果 → 選 ESP32 的 USB 埠 → 自動編譯、燒錄並開監看。從 OLED 開始；Ctrl+C 結束目前監看並回選單，輸入 `0` 離開。關閉監看後 ESP32 韌體仍在運行，改線前需斷電。

只想先看說明、還沒接硬體時，用 `python3 tools/bringup.py guide oled`（可換成 `hx711`、`ir` 等）。8 支獨立測試放在 `src/hwtest/`，不必先接好整台，也不會自動認定硬體通過；亮屏、聲音、重量誤差、鎖舌動作仍需實際觀察。

先依 [接線手冊](WIRING.md) 備齊配件、斷電接線、量電源；電控鎖最後接。也可用 `python3 tools/bringup.py list` 看項目、`python3 tools/bringup.py ports` 找埠，然後 `python3 tools/bringup.py flash oled --port <實際序列埠>` 自動編譯、燒錄並開啟 115200/LF 監看。

1. `hwtest_inputs`：先確認板子、蓋子開關、按鈕能動
2. `hwtest_oled`：四邊外框要完整、中文要清楚
3. `hwtest_tof`：記下空籃時兩顆的距離
4. `hwtest_hx711`：**一定要校正**（主程式開機會讀校正值）
   1. 籃子清空，序列埠輸入 `t` 歸零
   2. 放上已知重量的物品（例如 200 g 砝碼或秤過的水瓶），輸入 `c 200`
   3. 讀數正確後輸入 `s` 儲存
5. `hwtest_ir`：對準到「通」時 4/4 段都收到
6. `hwtest_nfc`：記下每張卡的 UID（就是帳號）
7. `hwtest_buzzer`；最後才做 `hwtest_lock`，先確認無負載的控制脈衝，再斷電接 12 V 鎖
8. 燒 `esp32dev`，確認開機硬體狀態全部 ✓（包含 `hx711cal`）；首次清空關蓋，在網頁記錄空籃基準，才會進入等待借車。開機檢查有失敗時禁止借還車，修好後須重啟。
9. 用 [實機驗證表](HARDWARE_ACCEPTANCE.md) 測實際物品、感測器失效、借還途中斷電與鎖脈衝；硬體狀態 ✓ 只代表開機初始化，不能代替實測。

## 測試與模擬器

### 智慧籃子 Web 與 SQLite

執行 `python3 tools/preview.py`，再開 <http://127.0.0.1:8765>。**騎乘總覽**可依卡片查詢借還狀態、待確認物品、餘額與帳務。「**開發者模式**」可切換車籃，查看感測器、裝置輸出、OLED、最後收集時間與資料收集狀態。

預設使用 native 模擬器，首次啟動會載入兩張示範卡。到開發者模式按「載入待確認情境」，回騎乘總覽即可試垃圾／遺失物確認。重啟會保留模擬餘額與籃內狀態，並重新開始未完成的倒數。載入另一個模擬情境會重設目前模擬餘額，舊紀錄仍保留。

連實機時，先燒錄含 `/api/telemetry` 的 `esp32dev` 韌體，再用下列方式啟動。可以重複帶入 `--device` 收集多個車籃。每台裝置的餘額與歷史各自儲存；每塊板請使用固定名稱。

```bash
python3 tools/preview.py --device '車籃01=http://smartbasket.local'
```

秤重和籃蓋開關尚未安裝時，可燒錄 `esp32dev_web`。車籃訊息同時送到 OLED 和網頁，物品偵測使用兩顆測距和紅外線。先把籃蓋實際關好，在「開發者模式」點「蓋子已關上」，等紅外線顯示通過、兩顆距離有讀值後，在空籃時點「記錄空籃基準」。開蓋也要在網頁回報。ESP32 每次重啟會從蓋子開啟開始。這個版本的物品覆蓋取決於測距視野和紅外線的安裝位置。

```bash
pio run -e esp32dev_web -t upload --upload-port /dev/cu.usbserial-1130
python3 tools/preview.py --device '實體車籃=serial:///dev/cu.usbserial-1130'
```

將指令中的序列埠換成實際裝置。透過 USB 收集時，Mac 可以留在原本的網路。要透過 Tailscale Serve 在私有網路內免輸入網頁帳密，讓 Serve 轉送至 `127.0.0.1:8765`，啟動時加上 `--tailscale-host <完整的 .ts.net 主機名>` 並清除 `BASKET_WEB_PASSWORD` 環境變數。保持 Mac、網頁服務及 USB 連線運作。燒錄前先停止 USB 收集器，完成後再啟動。

紅外線暫時無法使用時，可在斷電拆除紅外線接線後，燒錄 `esp32dev_web_tof_test` 驗證借還流程。這個測試版只採用兩顆測距，基準與帳戶另存於 ESP32 的 `basket_test`，網頁收集器應使用獨立的裝置名稱。物品必須進入測距視野才能用這個版本驗證。

手機若與電腦在同一個受信任的 Wi-Fi，可讓服務監聽區域網路，再用手機瀏覽 `http://<電腦的區網 IP>:8765`。先設定 `BASKET_WEB_PASSWORD` 環境變數，啟動指令加上 `--host 0.0.0.0`，瀏覽器以 `basket` 和所設定的密碼登入。這是供專題展示的共用登入，卡片選單用於篩選紀錄。服務需持續執行才能從實機收集資料。

預設資料庫在 `data/basket.sqlite3`，可用 `--db` 指定其他路徑。SQLite 保存去彈跳後的刷卡、借還、扣款及回饋事件，也保存裝置最近快照與帳戶餘額。收集器使用裝置開機識別碼與事件序號去重，時間欄位是伺服器收錄時間。實機韌體只在記憶體保留最近 40 筆事件。若裝置在連不上收集器期間重啟，或事件超出韌體緩衝區，部分舊紀錄可能收不到。可辨識的序號缺口會顯示在開發者頁。

### 單元測試

```bash
pio test -e native
```

涵蓋物品判定、借還、扣款／回饋／遺失物，以及首次校正、只用光學感測、借用／倒數途中重開機、感測器失效、開蓋禁止借車、設定完整還原、儲存失敗停止操作及舊資料移轉。2026-11-26 在 native 環境重跑 57 項，全部通過。這組測試使用假硬體。

### 終端機模擬器

不用硬體也能操作整個流程：

```bash
pio run -e native
.pio/build/native/program                       # 互動模式，輸入 help 看指令
.pio/build/native/program < scripts/demo.txt    # 留垃圾 → 扣款 → 下一位清理 → 拿到回饋
.pio/build/native/program < scripts/demo_lost.txt  # 遺失物、逾時扣款
```

| 指令 | 說明 |
|---|---|
| `tap A` | 感應卡片 A（借車；同一張卡再刷 = 還車） |
| `weight 120` | 秤重讀數 120 g |
| `dist 0 150` | 第 0 顆測距讀數 150 mm |
| `ir block` / `ir clear` | 紅外線擋住 / 通 |
| `lid open` / `lid close` | 打開 / 關上蓋子 |
| `button` | 按一下開蓋按鈕 |
| `put 150` / `take` | 快捷：開蓋放入 150 g 物品 / 全部拿走，再關蓋 |
| `confirm trash A` / `confirm mine A` | 網頁確認（不給卡號 = 最舊的一筆） |
| `tick 16` | 時間前進 16 秒 |
| `set timeout_s 30` | 調整設定（`penalty reward initial countdown_s timeout_s weight_g dist_mm lock_ms`） |
| `calibrate` / `status` / `json` | 重新記錄基準 / 顯示狀態 / 印出網頁 API 的 JSON |

每個指令執行後模擬器會自動前進 0.5 秒（讓平滑處理有讀數）。同一張卡 2 秒內重複感應只算一次，所以借車後要 `tick` 一下再還車。

## ESP32 網頁 API

| 方法 | 路徑 | 說明 |
|---|---|---|
| GET | `/` | 網頁介面（每秒更新） |
| GET | `/api/state` | 全部狀態：狀態、標記、待確認、OLED 內容、感測器、設定、帳戶、事件 |
| GET | `/api/accounts` | 帳戶餘額 |
| GET | `/api/events` | 最近 40 筆事件 |
| GET | `/api/hw` | 開機時各硬體初始化結果 |
| GET | `/api/telemetry` | 給 SQLite 收集器的單次快照，含裝置與開機識別碼、硬體狀態、即時訊號與最近事件 |
| POST | `/api/confirm?owner=<卡號>&kind=trash\|mine` | 確認是垃圾 / 自己的東西 |
| POST | `/api/config?key=<名稱>&value=<值>` | 調整設定 |
| POST | `/api/calibrate` | 重新記錄空籃基準 |

`/api/state` 另含 `hardwareReady`（開機硬體檢查）、`storageHealthy`（儲存是否正常）、`hasBaseline`、`sensors.ready`。讀數未有效或沒有基準時，網頁顯示「暫停判定」。首次校正須人工確認真的空籃；網頁仍為沒有登入的受控展示介面。

帳戶、標記、設定、基準與借用 snapshot 一起存入 Preferences namespace `basket` 的 `state`；讀取相容舊版 `accts/flags/base/cfg`，下一次修改時移轉。借車／倒數途中重啟會恢復借用者及原基準，倒數重新給完整時間。寫入失敗會停止後續操作，修復後重啟讀最後成功保存的狀態；事件紀錄不跨重啟保存。新韌體移轉後不要直接降回只讀舊 key 的版本並假定帳戶仍同步。

## 專案結構

```
lib/core/      核心邏輯（純 C++，不依賴 Arduino）：狀態機、判定、帳戶、事件、JSON
lib/fakes/     假硬體（測試、模擬器用）
lib/drivers/   ESP32 真實驅動、LEDC 相容層、OLED 繁中點陣表
include/       Pins.h（腳位）、secrets.example.h
src/main/      ESP32 主程式、原版區網頁與遙測端點
src/sim/       終端機模擬器
src/hwtest/    每個零件一支測試程式
test/          Unity 單元測試
scripts/       模擬器示範腳本
tools/         OLED 點陣產生器、單體燒錄工具、本機 Web 與 SQLite 收集器
web/           騎乘總覽與開發者模式
```

OLED 的中文：U8g2 內建中文字型只有簡體字集，所以 `tools/gen_oled_glyphs.py` 從文泉驛 12px 點陣字型取出用到的繁體字，產生 `lib/drivers/OledGlyphs.h`。**改了 OLED 上的中文字串要重跑一次**（CI 會用 `--check` 檢查缺字）。

## 已知限制

- ESP32 內建網頁沒有個人登入。Mac 網站可用私有 tailnet 或展示共用登入限制進入，卡片篩選器不驗證操作者身分。
- 籃子本來就有別人東西時，借車者「拿走原本的、放入差不多重的自己的東西」分辨不出來。
- 待確認的時限在重開機後會重新計算。
- 整機及尚未完成的硬體項目仍待實機驗收；物品的最小尺寸／重量與可偵測位置，須依安裝結果驗收。已完成的桌面初測紀錄見 HARDWARE_ACCEPTANCE.md。
- 2026-11-18 已修正 OLED 第二組 I2C 在 U8g2 未啟用而不傳資料的設定；實機已亮起，但多組驅動測試後，使用者肉眼仍看到黑線與缺筆畫，顯示問題尚未解決。PN532 VCC 從 3V3 改接這片 V3 額定範圍內的 5V 後，已讀到韌體 1.6 及穩定的單卡 UID；後續已在獨立測試區分兩張實體卡，長時間穩定性仍待測。秤重、蜂鳴器、電控鎖未接，按鈕與蓋子開關尚未驗證。
- 照片尚未能確認完整主控／供電／功率驅動配件；IR 距離與訊號電路、HX711 橋式激勵及精度須實測，詳見 WIRING.md。
- 尚無雲端通知、真實 YouBike／支付串接、帳戶清理介面或戶外耐候驗證。NVS 容量不足會停止操作並提示儲存失敗。

## 開源授權

本專題原創程式碼採 [MIT License](LICENSE)。OLED 中文點陣字形及外部函式庫沿用各自的授權，詳見[第三方授權說明](THIRD_PARTY_NOTICES.md)。以 `python3 tools/preview.py` 啟動本機模擬器，可在不連接 ESP32 的情況下體驗網站。實機的 Wi-Fi 設定與含卡片紀錄的 SQLite 資料庫由操作者在本機建立。
