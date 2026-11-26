#pragma once
// 智慧籃子的狀態機與罰款 / 回饋規則。純 C++，時間由外部傳入（毫秒）。
//
// 狀態：
//   Boot      開機，等待有效讀數；首次使用要在清空籃子後手動記錄基準
//   Idle      沒人借車，籃子上鎖
//   Riding    借車中，按鈕可開蓋；同一張卡再刷一次 = 還車
//   Countdown 還車時籃內有東西，蜂鳴器響、倒數讓使用者拿出來
//
// 籃子標記（可以同時存在）：
//   有垃圾   hasTrash()  確認是垃圾 / 逾時。下一位借車者清空還車可得回饋金
//   有遺失物 hasLost()   上一位說是自己的東西。下一位 OLED 顯示「請勿取走」，清空也不給回饋
//   待確認   pendings()  倒數結束還有東西，等上一位在網頁確認
#include <vector>

#include "Accounts.h"
#include "Config.h"
#include "Detector.h"
#include "EventLog.h"
#include "Hal.h"

namespace core {

enum class State { Boot, Idle, Riding, Countdown };
const char* stateName(State s);

enum class ConfirmKind { Trash, Mine };

struct Pending {
    std::string owner;       // 留下東西的人
    uint32_t createdMs = 0;
    std::string witness;     // 物品還在籃內時借車的人
    bool cleared = false;    // 在確認之前籃子就被清空了
    std::string cleaner;     // 清空的人
};

// weight、ir 和 display 可省略。其餘硬體介面必須提供。
struct Hardware {
    IWeightSensor* weight;
    IDistanceSensor* dist0;
    IDistanceSensor* dist1;
    IIrBeam* ir;
    ICardReader* card;
    ILock* lock;
    IBuzzer* buzzer;
    ILidSwitch* lid;
    IButton* button;
    IDisplay* display;
    IStorage* storage;
    DistanceCutoffs distanceCutoffsMm{};
};

class Basket {
public:
    explicit Basket(const Hardware& hw, const Config& cfg = Config());

    void begin(uint32_t now);  // 從 storage 載入資料
    void tick(uint32_t now);   // 主迴圈每次呼叫（建議 20~100 ms 一次）

    // 網頁確認。owner 為空字串代表最舊的一筆待確認。找不到回傳 false。
    bool confirm(const std::string& owner, ConfirmKind kind, uint32_t now);
    // 重新記錄空籃基準（蓋子要關著、讀數要穩定）。
    bool calibrate(uint32_t now);
    bool setConfig(const std::string& key, double value);
    void setHardwareReady(bool ready) { hardwareReady_ = ready; }
    bool hardwareReady() const { return hardwareReady_; }
    bool weightEnabled() const { return hw_.weight != nullptr; }
    bool irEnabled() const { return hw_.ir != nullptr; }
    bool storageHealthy() const { return storageHealthy_; }

    // ---- 查詢（給網頁 / 模擬器 / 測試） ----
    State state() const { return state_; }
    const std::string& rider() const { return rider_; }
    uint32_t countdownLeftMs(uint32_t now) const;
    bool hasTrash() const { return hasTrash_; }
    const std::string& trashOwner() const { return trashOwner_; }
    bool hasLost() const { return hasLost_; }
    bool isClean() const;
    const std::vector<Pending>& pendings() const { return pendings_; }
    uint32_t pendingLeftMs(const Pending& p, uint32_t now) const;
    const Accounts& accounts() const { return accounts_; }
    const EventLog& events() const { return log_; }
    const Config& config() const { return cfg_; }
    const DistanceCutoffs& distanceCutoffsMm() const { return hw_.distanceCutoffsMm; }
    bool hasBaseline() const { return hasBaseline_; }
    const Reading& baseline() const { return baseline_; }
    bool sensorsReady() const;
    bool irValid() const { return ir_.hasValue(); }
    bool lidClosed() const { return lidClosed_; }
    Reading current() const;
    DetectResult detectNow() const { return detectItems(baseline_, current(), cfg_, distanceCutoffsMm()); }
    const DisplayText& displayText() const { return shown_; }

private:
    enum class Check { Empty, Unchanged, Items };

    void sample(uint32_t now);
    void handleButton(uint32_t now);
    void handleCard(uint32_t now);
    void handleTimers(uint32_t now);
    void updateBuzzer(uint32_t now);
    void updateDisplay(uint32_t now);

    void rent(const std::string& uid, uint32_t now);
    void tryReturn(uint32_t now);
    Check check() const;
    void startCountdown(uint32_t now);
    void finishEmpty(uint32_t now);
    void finishUnchanged(uint32_t now);
    void createPending(uint32_t now);
    void endRide();
    void resolve(size_t idx, ConfirmKind kind, bool timeout, uint32_t now);
    void doCalibrate(uint32_t now, bool logIt);

    void flash(const std::string& text, uint32_t now, uint32_t ms = 3000);
    void beep(uint32_t freq, uint32_t ms, uint32_t now);
    void log(uint32_t now, EventType t, const std::string& uid, int32_t amount, const std::string& text);
    void persist();
    void load(uint32_t now);
    std::string displayUid(const std::string& uid) const;

    Hardware hw_;
    Config cfg_;
    Accounts accounts_;
    EventLog log_;

    State state_ = State::Boot;
    std::string rider_;
    uint32_t countdownStart_ = 0;

    bool hasBaseline_ = false;
    Reading baseline_;
    Reading snapshot_;     // 借車當下的讀數（籃子本來就有東西時用來比較有沒有新增）
    bool rentMarked_ = false;

    bool hasTrash_ = false;
    std::string trashOwner_, trashWitness_;
    bool hasLost_ = false;
    std::vector<Pending> pendings_;

    Smoother weight_, dist_[2], ir_;
    bool lidClosed_ = false;
    bool buttonWas_ = false;
    std::string lastCardUid_;
    uint32_t lastCardMs_ = 0;

    std::string flashText_;
    uint32_t flashUntil_ = 0;
    uint32_t beepFreq_ = 0, beepUntil_ = 0;
    uint32_t buzzerFreq_ = 0;
    bool shownOnce_ = false;
    DisplayText shown_;

    bool hardwareReady_ = true;
    bool storageHealthy_ = true;
    std::string savedState_;
};

}  // namespace core
