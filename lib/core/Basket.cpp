#include "Basket.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace core {

namespace {
constexpr uint32_t kBeepOkHz = 1800, kBeepErrHz = 400, kAlarmHz = 2400;

// 處理 millis() 溢位的時間比較
bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string item;
    std::istringstream in(s);
    while (std::getline(in, item, sep)) out.push_back(item);
    return out;
}
}  // namespace

const char* stateName(State s) {
    switch (s) {
        case State::Boot: return "boot";
        case State::Idle: return "idle";
        case State::Riding: return "riding";
        case State::Countdown: return "countdown";
    }
    return "?";
}

Basket::Basket(const Hardware& hw, const Config& cfg) : hw_(hw), cfg_(cfg), accounts_(cfg.initialBalance) {}

// ---------------------------------------------------------------------------
// 主流程
// ---------------------------------------------------------------------------

void Basket::begin(uint32_t now) {
    load(now);
    log(now, EventType::Boot, "", 0, "開機");
    updateDisplay(now);
}

void Basket::tick(uint32_t now) {
    sample(now);
    handleButton(now);
    handleCard(now);
    handleTimers(now);
    updateBuzzer(now);
    updateDisplay(now);
}

void Basket::sample(uint32_t) {
    bool closed = hw_.lid->isClosed();
    if (!closed) {
        // 蓋子打開時測距會照到別的地方，讀數不可信：清掉平滑資料，關上後重新累積
        weight_.clear();
        dist_[0].clear();
        dist_[1].clear();
        ir_.clear();
    } else {
        float g;
        if (hw_.weight && hw_.weight->read(g) && std::isfinite(g)) weight_.add(g);
        else weight_.clear();
        int mm;
        if (hw_.dist0->read(mm) && mm > 0 && mm < 8190) dist_[0].add((float)mm);
        else dist_[0].clear();
        if (hw_.dist1->read(mm) && mm > 0 && mm < 8190) dist_[1].add((float)mm);
        else dist_[1].clear();
        if (hw_.ir) {
            bool blocked = hw_.ir->isBlocked();
            if (hw_.ir->healthy()) ir_.add(blocked ? 1.0f : 0.0f);
            else ir_.clear();
        }
    }
    lidClosed_ = closed;
}

bool Basket::sensorsReady() const {
    return hardwareReady_ && storageHealthy_ && lidClosed_ && (!hw_.weight || weight_.full()) &&
           dist_[0].full() && dist_[1].full() && (!hw_.ir || ir_.full());
}

Reading Basket::current() const {
    Reading r;
    r.weightValid = weight_.hasValue();
    r.weightG = weight_.value();
    for (int i = 0; i < 2; ++i) {
        r.distValid[i] = dist_[i].hasValue();
        r.distMm[i] = (int)std::lround(dist_[i].value());
    }
    r.irBlocked = ir_.hasValue() && ir_.value() >= 0.5f;
    return r;
}

void Basket::handleButton(uint32_t now) {
    bool pressed = hw_.button->isPressed();
    bool edge = pressed && !buttonWas_;
    buttonWas_ = pressed;
    if (!edge) return;
    if (!hardwareReady_ || !storageHealthy_) return;
    if (state_ == State::Riding || state_ == State::Countdown) {
        uint32_t ms = cfg_.lockPulseMs > kMaxLockPulseMs ? kMaxLockPulseMs : cfg_.lockPulseMs;
        hw_.lock->unlockPulse(ms);
        beep(kBeepOkHz, 60, now);
    } else {
        flash("請先刷卡借車", now);
        beep(kBeepErrHz, 200, now);
    }
}

void Basket::handleCard(uint32_t now) {
    std::string uid;
    if (!hw_.card->poll(uid) || uid.empty()) return;
    // 卡片放著不拿開時讀卡機會一直讀到，同一張卡在 debounce 時間內只算一次
    if (uid == lastCardUid_ && !reached(now, lastCardMs_ + cfg_.cardDebounceMs)) {
        lastCardMs_ = now;
        return;
    }
    lastCardUid_ = uid;
    lastCardMs_ = now;
    log(now, EventType::CardTap, uid, 0, "刷卡");
    if (!hardwareReady_ || !storageHealthy_) return;

    switch (state_) {
        case State::Boot:
            flash("準備中，請稍候", now);
            beep(kBeepErrHz, 200, now);
            break;
        case State::Idle:
            rent(uid, now);
            break;
        case State::Riding:
            if (uid == rider_) {
                tryReturn(now);
            } else {
                flash("使用中", now);
                beep(kBeepErrHz, 200, now);
            }
            break;
        case State::Countdown:
            if (uid != rider_) {
                flash("使用中", now);
                beep(kBeepErrHz, 200, now);
            }
            break;
    }
}

void Basket::handleTimers(uint32_t now) {
    if (!hardwareReady_ || !storageHealthy_) return;
    // 開機：等讀數穩定
    if (state_ == State::Boot && sensorsReady() && hasBaseline_) {
        state_ = State::Idle;
    }

    // 倒數中：拿走了就提早結束；時間到還有東西就變成待確認
    if (state_ == State::Countdown && sensorsReady()) {
        Check c = check();
        if (c == Check::Empty) {
            finishEmpty(now);
        } else if (c == Check::Unchanged) {
            finishUnchanged(now);
        } else if (reached(now, countdownStart_ + cfg_.countdownMs)) {
            createPending(now);
        }
    }

    // 待確認逾時 → 視為垃圾
    for (size_t i = 0; i < pendings_.size() && storageHealthy_;) {
        if (reached(now, pendings_[i].createdMs + cfg_.confirmTimeoutMs)) {
            resolve(i, ConfirmKind::Trash, true, now);
        } else {
            ++i;
        }
    }
}

// ---------------------------------------------------------------------------
// 借還車
// ---------------------------------------------------------------------------

bool Basket::isClean() const {
    if (hasTrash_ || hasLost_) return false;
    for (auto& p : pendings_)
        if (!p.cleared) return false;
    return true;
}

void Basket::rent(const std::string& uid, uint32_t now) {
    if (!sensorsReady() || !hasBaseline_) {
        flash(lidClosed_ ? "感測器準備中" : "請先關上蓋子", now);
        beep(kBeepErrHz, 200, now);
        return;
    }
    accounts_.ensure(uid);
    // 不把未歸責物品或漂移自動吸收成空籃；基準只由明確校正更新。
    rentMarked_ = !isClean() || detectNow().present;
    snapshot_ = current();
    if (hasTrash_) trashWitness_ = uid;
    for (auto& p : pendings_)
        if (!p.cleared) p.witness = uid;
    rider_ = uid;
    state_ = State::Riding;
    log(now, EventType::Rent, uid, 0, "借車");
    beep(kBeepOkHz, 100, now);
    persist();
}

Basket::Check Basket::check() const {
    Reading cur = current();
    if (!detectItems(baseline_, cur, cfg_, distanceCutoffsMm()).present) return Check::Empty;
    // 借車時籃子本來就有別人的東西：跟借車當下比，沒有多出東西就不算這位的
    if (rentMarked_ && !detectItems(snapshot_, cur, cfg_).present) return Check::Unchanged;
    return Check::Items;
}

void Basket::tryReturn(uint32_t now) {
    if (!sensorsReady()) {
        flash(lidClosed_ ? "感測器準備中" : "請先關上蓋子", now);
        beep(kBeepErrHz, 200, now);
        return;
    }
    switch (check()) {
        case Check::Empty: finishEmpty(now); break;
        case Check::Unchanged: finishUnchanged(now); break;
        case Check::Items: startCountdown(now); break;
    }
}

void Basket::startCountdown(uint32_t now) {
    state_ = State::Countdown;
    countdownStart_ = now;
    log(now, EventType::ItemLeft, rider_, 0,
        "籃內有東西，倒數 " + std::to_string(cfg_.countdownMs / 1000) + " 秒");
    persist();
}

uint32_t Basket::countdownLeftMs(uint32_t now) const {
    if (state_ != State::Countdown) return 0;
    uint32_t end = countdownStart_ + cfg_.countdownMs;
    return reached(now, end) ? 0 : end - now;
}

void Basket::finishEmpty(uint32_t now) {
    const std::string uid = rider_;
    const bool cleanedTrash = hasTrash_ && trashWitness_ == uid;
    const bool rewarded = cleanedTrash && trashOwner_ != uid;
    if (rewarded) accounts_.add(uid, cfg_.reward);
    for (auto& p : pendings_) {
        if (!p.cleared) {
            p.cleared = true;
            p.cleaner = rider_;
        }
    }
    hasTrash_ = false;
    trashOwner_.clear();
    trashWitness_.clear();
    hasLost_ = false;
    endRide();
    persist();
    if (!storageHealthy_) return;
    if (rewarded) log(now, EventType::Reward, uid, cfg_.reward, "清理垃圾回饋金");
    else if (cleanedTrash) log(now, EventType::Warning, uid, 0, "清理自己留的垃圾，不給回饋");
    log(now, EventType::Return, uid, 0, "還車完成，籃子是空的");
    beep(kBeepOkHz, 100, now);
}

void Basket::finishUnchanged(uint32_t now) {
    log(now, EventType::Return, rider_, 0, "還車完成（籃內原有物品未變動）");
    endRide();
    beep(kBeepOkHz, 100, now);
    persist();
}

void Basket::createPending(uint32_t now) {
    Pending p;
    p.owner = rider_;
    p.createdMs = now;
    pendings_.push_back(p);
    log(now, EventType::Pending, rider_, 0, "倒數結束仍有物品，等待確認");
    endRide();
    persist();
}

void Basket::endRide() {
    rider_.clear();
    state_ = State::Idle;
    rentMarked_ = false;
}

// ---------------------------------------------------------------------------
// 網頁確認
// ---------------------------------------------------------------------------

bool Basket::confirm(const std::string& owner, ConfirmKind kind, uint32_t now) {
    if (!hardwareReady_ || !storageHealthy_) return false;
    for (size_t i = 0; i < pendings_.size(); ++i) {
        if (owner.empty() || pendings_[i].owner == owner) {
            resolve(i, kind, false, now);
            return storageHealthy_;
        }
    }
    return false;
}

uint32_t Basket::pendingLeftMs(const Pending& p, uint32_t now) const {
    uint32_t end = p.createdMs + cfg_.confirmTimeoutMs;
    return reached(now, end) ? 0 : end - now;
}

void Basket::resolve(size_t idx, ConfirmKind kind, bool timeout, uint32_t now) {
    Pending p = pendings_[idx];
    pendings_.erase(pendings_.begin() + (long)idx);

    if (kind == ConfirmKind::Trash) {
        const bool charged = !(p.cleared && p.cleaner == p.owner);
        const bool rewarded = p.cleared && !p.cleaner.empty() && p.cleaner != p.owner;
        if (charged) accounts_.add(p.owner, -cfg_.penalty);
        if (rewarded) accounts_.add(p.cleaner, cfg_.reward);
        if (!p.cleared) {
            hasTrash_ = true;
            trashOwner_ = p.owner;
            trashWitness_ = p.witness;
        }
        persist();
        if (!storageHealthy_) return;
        log(now, EventType::ConfirmTrash, p.owner, 0, timeout ? "逾時未確認，視為垃圾" : "確認是垃圾");
        if (charged) log(now, EventType::Penalty, p.owner, -cfg_.penalty, "留垃圾罰款");
        if (rewarded) log(now, EventType::Reward, p.cleaner, cfg_.reward, "清理垃圾回饋金");
    } else {
        if (!p.cleared) hasLost_ = true;
        persist();
        if (storageHealthy_) log(now, EventType::ConfirmMine, p.owner, 0, "確認是自己的東西（遺失物）");
    }
}

// ---------------------------------------------------------------------------
// 基準、設定
// ---------------------------------------------------------------------------

bool Basket::calibrate(uint32_t now) {
    if (!sensorsReady() || (state_ != State::Boot && state_ != State::Idle) ||
        !isClean() || !pendings_.empty() || current().irBlocked) return false;
    doCalibrate(now, true);
    return storageHealthy_;
}

void Basket::doCalibrate(uint32_t now, bool logIt) {
    baseline_ = current();
    baseline_.irBlocked = false;  // 空籃時對射一定是通的
    hasBaseline_ = true;
    if (logIt) {
        std::ostringstream o;
        o << "記錄空籃基準：";
        if (baseline_.weightValid) o << "重量 " << (int)std::lround(baseline_.weightG) << " g，";
        o << "距離 " << baseline_.distMm[0] << "/"
          << baseline_.distMm[1] << " mm";
        log(now, EventType::Calibrate, "", 0, o.str());
    }
    persist();
}

bool Basket::setConfig(const std::string& key, double value) {
    if (!storageHealthy_ || state_ == State::Riding || state_ == State::Countdown || !pendings_.empty()) return false;
    if (!cfg_.set(key, value)) return false;
    accounts_.setInitialBalance(cfg_.initialBalance);
    persist();
    return storageHealthy_;
}

// ---------------------------------------------------------------------------
// 蜂鳴器、OLED
// ---------------------------------------------------------------------------

void Basket::flash(const std::string& text, uint32_t now, uint32_t ms) {
    flashText_ = text;
    flashUntil_ = now + ms;
}

void Basket::beep(uint32_t freq, uint32_t ms, uint32_t now) {
    beepFreq_ = freq;
    beepUntil_ = now + ms;
}

void Basket::updateBuzzer(uint32_t now) {
    uint32_t want = 0;
    if (state_ == State::Countdown) {
        // 每秒響 200 ms；時間到但蓋子沒關時繼續響
        want = ((now - countdownStart_) % 1000) < 200 ? kAlarmHz : 0;
    }
    if (beepUntil_ && !reached(now, beepUntil_)) want = beepFreq_;
    else beepUntil_ = 0;
    if (want != buzzerFreq_) {
        buzzerFreq_ = want;
        hw_.buzzer->tone(want);
    }
}

std::string Basket::displayUid(const std::string& uid) const {
    return uid.size() > 6 ? uid.substr(uid.size() - 6) : uid;
}

void Basket::updateDisplay(uint32_t now) {
    DisplayText t;
    bool othersItem = hasLost_;
    int open = 0;
    for (auto& p : pendings_)
        if (!p.cleared) ++open, othersItem = true;

    switch (state_) {
        case State::Boot:
            t.line[0] = "智慧 Ubike 籃子";
            t.line[1] = sensorsReady() && !hasBaseline_ ? "請清空籃子" : "感測器準備中";
            t.line[2] = sensorsReady() && !hasBaseline_ ? "網頁記錄空籃基準" : "請關好蓋子";
            break;
        case State::Idle:
            t.line[0] = "請刷卡借車";
            if (hasLost_) t.line[1] = "籃內有遺失物";
            else if (open) t.line[1] = "籃內物品待確認";
            else if (hasTrash_) t.line[1] = "籃內有垃圾";
            else t.line[1] = detectNow().present ? "籃內有物品" : "籃子是空的";
            if (!pendings_.empty()) t.line[2] = "待確認 " + std::to_string(pendings_.size()) + " 筆";
            break;
        case State::Riding:
            t.line[0] = "借車中 " + displayUid(rider_);
            t.line[1] = "餘額 " + std::to_string(accounts_.balance(rider_)) + " 元";
            if (othersItem) {
                t.line[2] = "籃內有他人物品";
                t.line[3] = "請勿取走";
            } else if (hasTrash_) {
                t.line[2] = "有垃圾，清空還車";
                t.line[3] = "可得回饋 " + std::to_string(cfg_.reward) + " 元";
            } else {
                t.line[2] = "按按鈕開蓋";
                t.line[3] = "再刷卡即還車";
            }
            break;
        case State::Countdown: {
            uint32_t left = countdownLeftMs(now);
            if (left > 0) {
                t.line[0] = "請取出籃內物品";
                t.line[1] = "倒數 " + std::to_string((left + 999) / 1000) + " 秒";
                t.line[2] = "取出後請關蓋";
            } else {
                t.line[0] = "時間到";
                t.line[1] = "請關上蓋子";
            }
            break;
        }
    }
    if (flashUntil_ && !reached(now, flashUntil_)) t.line[4] = flashText_;
    else flashUntil_ = 0;

    if (!storageHealthy_) t.line[4] = "儲存失敗請維修";
    else if (!hardwareReady_) t.line[4] = "硬體異常請檢查";
    else if (lidClosed_ && !sensorsReady()) t.line[4] = "感測器準備中";

    if (!shownOnce_ || t != shown_) {
        shown_ = t;
        shownOnce_ = true;
        if (hw_.display) hw_.display->show(t);
    }
}

// ---------------------------------------------------------------------------
// 紀錄與儲存
// ---------------------------------------------------------------------------

void Basket::log(uint32_t now, EventType t, const std::string& uid, int32_t amount, const std::string& text) {
    log_.add(now, t, uid, amount, text);
}

namespace {
std::string encodeReading(const Reading& r) {
    std::ostringstream out;
    out << std::setprecision(9) << r.weightG << ',' << r.weightValid << ','
        << r.distMm[0] << ',' << r.distValid[0] << ',' << r.distMm[1] << ',' << r.distValid[1] << ',' << r.irBlocked;
    return out.str();
}

bool decodeReading(std::string text, Reading& r) {
    for (char& c : text) if (c == ',') c = ' ';
    std::istringstream in(text);
    if (!(in >> r.weightG >> r.weightValid >> r.distMm[0] >> r.distValid[0] >> r.distMm[1] >> r.distValid[1])) return false;
    // 舊版 base 沒有最後的 ir 欄位。
    in >> std::ws;
    if (!in.eof() && !(in >> r.irBlocked)) return false;
    return std::isfinite(r.weightG) && (!r.distValid[0] || (r.distMm[0] > 0 && r.distMm[0] < 8190)) &&
           (!r.distValid[1] || (r.distMm[1] > 0 && r.distMm[1] < 8190));
}
}  // namespace

void Basket::persist() {
    if (!storageHealthy_) return;
    std::ostringstream flags;
    flags << hasTrash_ << ',' << trashOwner_ << ',' << trashWitness_ << ',' << hasLost_;
    for (auto& p : pendings_)
        flags << '\n' << p.owner << ',' << p.witness << ',' << p.cleared << ',' << p.cleaner;

    std::ostringstream ride;
    if (!rider_.empty()) {
        ride << stateName(state_) << ' ' << std::quoted(rider_) << ' ' << rentMarked_ << ' '
             << std::quoted(encodeReading(snapshot_));
    }
    // 一次寫入同一個 NVS value，避免帳戶、標記、借用狀態分開寫造成半套交易。
    std::ostringstream out;
    out << "ub1 " << std::quoted(cfg_.serialize()) << ' ' << std::quoted(accounts_.serialize()) << ' '
        << std::quoted(flags.str()) << ' ' << std::quoted(hasBaseline_ ? encodeReading(baseline_) : "") << ' '
        << std::quoted(ride.str());
    std::string value = out.str();
    if (value == savedState_) return;
    if (hw_.storage->save("state", value)) savedState_ = value;
    else storageHealthy_ = false;  // 不繼續借還車／扣款；保留最後成功寫入的完整狀態供重開機恢復。
}

void Basket::load(uint32_t now) {
    std::string cfg, accts, flags, base, ride, value;
    if (hw_.storage->load("state", value)) {
        std::istringstream in(value);
        std::string version;
        if (!(in >> version >> std::quoted(cfg) >> std::quoted(accts) >> std::quoted(flags)
                 >> std::quoted(base) >> std::quoted(ride)) || version != "ub1") {
            storageHealthy_ = false;
            return;
        }
        in >> std::ws;
        if (!in.eof()) { storageHealthy_ = false; return; }
        savedState_ = value;
    } else {
        // 相容舊韌體；下一次變更會轉為單筆 state，舊 key 不再寫入。
        hw_.storage->load("cfg", cfg);
        hw_.storage->load("accts", accts);
        hw_.storage->load("flags", flags);
        hw_.storage->load("base", base);
    }
    if (!cfg.empty() && !cfg_.parse(cfg)) { storageHealthy_ = false; return; }
    accounts_.setInitialBalance(cfg_.initialBalance);
    accounts_.parse(accts);
    if (!flags.empty()) {
        auto lines = split(flags, '\n');
        for (size_t i = 0; i < lines.size(); ++i) {
            auto f = split(lines[i], ',');
            while (f.size() < 4) f.push_back("");
            if (i == 0) {
                hasTrash_ = f[0] == "1";
                trashOwner_ = f[1];
                trashWitness_ = f[2];
                hasLost_ = f[3] == "1";
            } else if (!f[0].empty()) {
                Pending p;
                p.owner = f[0];
                p.witness = f[1];
                p.cleared = f[2] == "1";
                p.cleaner = f[3];
                p.createdMs = now;  // 無 RTC，重開機後重新給完整確認期限。
                pendings_.push_back(p);
            }
        }
    }
    if (!base.empty()) {
        if (!decodeReading(base, baseline_)) { storageHealthy_ = false; return; }
        hasBaseline_ = baseline_.distValid[0] && baseline_.distValid[1];
    }
    if (!ride.empty()) {
        std::istringstream in(ride);
        std::string state, snapshot;
        if (!(in >> state >> std::quoted(rider_) >> rentMarked_ >> std::quoted(snapshot)) ||
            rider_.empty() || !hasBaseline_ || !decodeReading(snapshot, snapshot_) ||
            !snapshot_.distValid[0] || !snapshot_.distValid[1] ||
            (state != "riding" && state != "countdown")) {
            storageHealthy_ = false;
            return;
        }
        state_ = state == "riding" ? State::Riding : State::Countdown;
        countdownStart_ = now;  // 倒數給完整寬限時間，不把重啟造成的停機算在使用者身上。
        lastCardUid_ = rider_;
        lastCardMs_ = now;  // 重啟時卡仍貼著，不能立刻觸發還車。
    }
}

}  // namespace core
