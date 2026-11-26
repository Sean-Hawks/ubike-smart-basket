// 借還車流程、罰款與回饋規則。全部用假硬體，時間由測試推進。
#include <Basket.h>
#include <Fakes.h>
#include <Json.h>
#include <unity.h>

#include <cstring>
#include <memory>

using namespace core;

static const char* A = "AAAA01";
static const char* B = "BBBB02";

struct World {
    fakes::Rig rig;
    std::unique_ptr<Basket> b;
    uint32_t t = 0;

    explicit World(const Config& cfg = Config(), bool tofOnly = false, const DistanceCutoffs& cutoffs = {}) {
        rig.weight.grams = 0;
        rig.dist0.mm = 300;
        rig.dist1.mm = 300;
        auto hardware = rig.hw();
        hardware.distanceCutoffsMm = cutoffs;
        if (tofOnly) {
            hardware.weight = nullptr;
            hardware.ir = nullptr;
        }
        b.reset(new Basket(hardware, cfg));
        b->begin(t);
        run(1000);
        b->calibrate(t);  // 測試治具確定空籃，模擬首次人工校正。
        run(50);
    }
    void run(uint32_t ms) {
        for (uint32_t i = 0; i < ms; i += 50) b->tick(t += 50);
    }
    void tap(const char* uid) {
        rig.card.tap(uid);
        run(500);
    }
    void press() {
        rig.button.pressed = true;
        run(100);
        rig.button.pressed = false;
        run(100);
    }
    void reboot() {
        b.reset(new Basket(rig.hw()));
        t = 0;
        b->begin(t);
        run(1000);
    }
    // 開蓋放東西 / 拿東西，再關蓋
    void putItem(float grams) {
        rig.lid.closed = false;
        run(500);
        rig.weight.grams = grams;
        rig.dist0.mm = grams > 0 ? 200 : 300;
        rig.lid.closed = true;
        run(1000);
    }
    void removeAll() { putItem(0); }
    int32_t bal(const char* uid) { return b->accounts().balance(uid); }
    bool displayHas(const std::string& s) {
        for (auto& l : rig.display.text.line)
            if (l.find(s) != std::string::npos) return true;
        return false;
    }
    // A 借車、留垃圾、倒數結束 → 待確認
    void leaveItem(const char* uid) {
        tap(uid);
        run(3000);
        putItem(150);
        tap(uid);
        TEST_ASSERT_EQUAL(State::Countdown, b->state());
        run(16000);
        TEST_ASSERT_EQUAL(State::Idle, b->state());
    }
};

void setUp() {}
void tearDown() {}

void test_boot_calibrates_and_goes_idle() {
    World w;
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    TEST_ASSERT_TRUE(w.b->hasBaseline());
    TEST_ASSERT_TRUE(w.displayHas("請刷卡借車"));
}

void test_normal_rent_and_return_empty() {
    World w;
    w.tap(A);
    TEST_ASSERT_EQUAL(State::Riding, w.b->state());
    TEST_ASSERT_EQUAL_STRING(A, w.b->rider().c_str());
    TEST_ASSERT_TRUE(w.displayHas("借車中"));

    w.press();  // 借車中按鈕可以開鎖
    TEST_ASSERT_EQUAL(1, w.rig.lock.pulses);
    TEST_ASSERT_TRUE(w.rig.lock.lastMs <= kMaxLockPulseMs);

    w.run(3000);
    w.tap(A);
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    TEST_ASSERT_EQUAL(100, w.bal(A));
    TEST_ASSERT_TRUE(w.b->isClean());
    TEST_ASSERT_EQUAL(0, (int)w.b->pendings().size());
}

void test_fixed_distance_cutoffs_apply_to_idle_and_return() {
    World w(Config{}, true, {94, 113});
    w.rig.dist0.mm = 95;
    w.rig.dist1.mm = 114;
    w.run(500);
    TEST_ASSERT_TRUE(w.b->calibrate(w.t));
    TEST_ASSERT_FALSE(w.b->detectNow().present);

    w.tap(A);
    w.rig.dist0.mm = 94;
    w.run(500);
    TEST_ASSERT_TRUE(w.b->detectNow().byDist[0]);
    TEST_ASSERT_FALSE(w.b->detectNow().byDist[1]);
    w.rig.dist0.mm = 95;
    w.rig.dist1.mm = 113;
    w.run(2500);
    TEST_ASSERT_FALSE(w.b->detectNow().byDist[0]);
    TEST_ASSERT_TRUE(w.b->detectNow().byDist[1]);
    w.tap(A);
    TEST_ASSERT_EQUAL(State::Countdown, w.b->state());

    w.rig.dist1.mm = 114;
    w.run(1000);
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());

    w.rig.dist1.mm = 113;
    w.run(500);
    w.tap(B);
    w.run(2500);
    w.tap(B);
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    TEST_ASSERT_TRUE(w.b->pendings().empty());
}

void test_button_ignored_when_not_riding() {
    World w;
    w.press();
    TEST_ASSERT_EQUAL(0, w.rig.lock.pulses);
}

void test_other_card_while_riding_is_rejected() {
    World w;
    w.tap(A);
    w.tap(B);
    TEST_ASSERT_EQUAL_STRING(A, w.b->rider().c_str());
    TEST_ASSERT_TRUE(w.displayHas("使用中"));
}

void test_card_held_on_reader_does_not_return_immediately() {
    World w;
    w.tap(A);
    w.tap(A);  // 2 秒內同一張卡：當作同一次感應
    TEST_ASSERT_EQUAL(State::Riding, w.b->state());
}

void test_return_with_lid_open_is_refused() {
    World w;
    w.tap(A);
    w.run(3000);
    w.rig.lid.closed = false;
    w.tap(A);
    TEST_ASSERT_EQUAL(State::Riding, w.b->state());
    TEST_ASSERT_TRUE(w.displayHas("請先關上蓋子"));
}

void test_item_removed_within_countdown_no_penalty() {
    World w;
    w.tap(A);
    w.run(3000);
    w.putItem(150);
    w.tap(A);
    TEST_ASSERT_EQUAL(State::Countdown, w.b->state());
    TEST_ASSERT_TRUE(w.rig.buzzer.starts > 0);  // 蜂鳴器響
    TEST_ASSERT_TRUE(w.displayHas("倒數"));

    w.press();  // 倒數中可以開蓋拿東西
    TEST_ASSERT_EQUAL(1, w.rig.lock.pulses);
    w.run(5000);
    w.removeAll();
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    TEST_ASSERT_EQUAL(100, w.bal(A));
    TEST_ASSERT_EQUAL(0, (int)w.b->pendings().size());
    TEST_ASSERT_TRUE(w.b->isClean());
    TEST_ASSERT_EQUAL(0u, w.rig.buzzer.freq);
}

void test_countdown_end_creates_pending_without_charging() {
    World w;
    w.leaveItem(A);
    TEST_ASSERT_EQUAL(1, (int)w.b->pendings().size());
    TEST_ASSERT_EQUAL_STRING(A, w.b->pendings()[0].owner.c_str());
    TEST_ASSERT_EQUAL(100, w.bal(A));  // 待確認時先不扣錢
    TEST_ASSERT_FALSE(w.b->hasTrash());
}

void test_confirm_trash_charges_penalty() {
    World w;
    w.leaveItem(A);
    TEST_ASSERT_TRUE(w.b->confirm(A, ConfirmKind::Trash, w.t));
    TEST_ASSERT_EQUAL(85, w.bal(A));
    TEST_ASSERT_TRUE(w.b->hasTrash());
    TEST_ASSERT_EQUAL(0, (int)w.b->pendings().size());
}

void test_confirm_timeout_charges_penalty() {
    World w;
    w.leaveItem(A);
    w.run(118000);  // 待確認是在倒數結束時建立的，距今約 1 秒
    TEST_ASSERT_EQUAL(100, w.bal(A));
    w.run(3000);  // 超過 2 分鐘
    TEST_ASSERT_EQUAL(85, w.bal(A));
    TEST_ASSERT_TRUE(w.b->hasTrash());
}

void test_confirm_timeout_is_adjustable() {
    World w;
    TEST_ASSERT_TRUE(w.b->setConfig("timeout_s", 30));
    w.leaveItem(A);
    w.run(31000);
    TEST_ASSERT_EQUAL(85, w.bal(A));
}

void test_confirm_mine_no_penalty_marks_lost() {
    World w;
    w.leaveItem(A);
    TEST_ASSERT_TRUE(w.b->confirm(A, ConfirmKind::Mine, w.t));
    TEST_ASSERT_EQUAL(100, w.bal(A));
    TEST_ASSERT_TRUE(w.b->hasLost());
    TEST_ASSERT_FALSE(w.b->hasTrash());

    // 下一位借車：OLED 顯示請勿取走；清空也不給回饋
    w.tap(B);
    TEST_ASSERT_TRUE(w.displayHas("請勿取走"));
    w.run(3000);
    w.removeAll();
    w.tap(B);
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    TEST_ASSERT_EQUAL(100, w.bal(B));
    TEST_ASSERT_FALSE(w.b->hasLost());
}

void test_next_rider_cleans_trash_gets_reward() {
    World w;
    w.leaveItem(A);
    w.b->confirm(A, ConfirmKind::Trash, w.t);
    TEST_ASSERT_EQUAL(85, w.bal(A));

    w.tap(B);
    TEST_ASSERT_TRUE(w.displayHas("可得回饋"));
    w.run(3000);
    w.removeAll();
    w.tap(B);
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    TEST_ASSERT_EQUAL(110, w.bal(B));
    TEST_ASSERT_FALSE(w.b->hasTrash());
    TEST_ASSERT_TRUE(w.b->isClean());

    // 回饋只給一次
    w.run(3000);
    w.tap(B);
    w.run(3000);
    w.tap(B);
    TEST_ASSERT_EQUAL(110, w.bal(B));
}

void test_reward_is_less_than_penalty() {
    Config c;
    TEST_ASSERT_TRUE(c.reward < c.penalty);
    TEST_ASSERT_FALSE(c.set("reward", 15));  // 等於罰款：不允許
    TEST_ASSERT_FALSE(c.set("reward", 20));
    TEST_ASSERT_FALSE(c.set("penalty", 10));  // 罰款降到 <= 回饋：不允許
    TEST_ASSERT_EQUAL(10, c.reward);
    TEST_ASSERT_EQUAL(15, c.penalty);
    TEST_ASSERT_TRUE(c.set("reward", 14));
}

void test_lock_pulse_is_capped() {
    Config c;
    TEST_ASSERT_FALSE(c.set("lock_ms", 1500));
    TEST_ASSERT_TRUE(c.set("lock_ms", 1000));
}

void test_rider_not_cleaning_trash_is_not_charged() {
    World w;
    w.leaveItem(A);
    w.b->confirm(A, ConfirmKind::Trash, w.t);
    w.tap(B);
    w.run(3000);
    w.tap(B);  // 沒清垃圾就還車：不是 B 的東西，不倒數、不扣錢
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    TEST_ASSERT_EQUAL(100, w.bal(B));
    TEST_ASSERT_TRUE(w.b->hasTrash());
}

void test_rider_adding_more_to_trash_basket_counts_down() {
    World w;
    w.leaveItem(A);
    w.b->confirm(A, ConfirmKind::Trash, w.t);
    w.tap(B);
    w.run(3000);
    w.putItem(400);  // B 又多放了東西
    w.tap(B);
    TEST_ASSERT_EQUAL(State::Countdown, w.b->state());
}

void test_cleaning_own_trash_gives_no_reward() {
    World w;
    w.leaveItem(A);
    w.b->confirm(A, ConfirmKind::Trash, w.t);
    w.tap(A);
    w.run(3000);
    w.removeAll();
    w.tap(A);
    TEST_ASSERT_EQUAL(85, w.bal(A));
    TEST_ASSERT_TRUE(w.b->isClean());
}

void test_cleared_before_confirm_then_trash_rewards_cleaner() {
    World w;
    w.leaveItem(A);
    // A 還沒確認，B 就把東西清掉了
    w.tap(B);
    TEST_ASSERT_TRUE(w.displayHas("請勿取走"));
    w.run(3000);
    w.removeAll();
    w.tap(B);
    TEST_ASSERT_EQUAL(100, w.bal(B));
    w.b->confirm(A, ConfirmKind::Trash, w.t);
    TEST_ASSERT_EQUAL(85, w.bal(A));
    TEST_ASSERT_EQUAL(110, w.bal(B));
    TEST_ASSERT_FALSE(w.b->hasTrash());  // 已經清空了
}

void test_trash_confirmed_during_next_ride_still_rewards() {
    World w;
    w.leaveItem(A);
    w.tap(B);  // 借車時還是待確認
    w.b->confirm(A, ConfirmKind::Trash, w.t);
    w.run(3000);
    w.removeAll();
    w.tap(B);
    TEST_ASSERT_EQUAL(85, w.bal(A));
    TEST_ASSERT_EQUAL(110, w.bal(B));
}

void test_single_noisy_reading_does_not_trigger() {
    World w;
    w.tap(A);
    w.run(3000);
    w.rig.weight.grams = 40;  // 一次雜訊
    w.b->tick(w.t += 50);
    w.rig.weight.grams = 0;
    w.rig.card.tap(A);
    w.b->tick(w.t += 50);
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
}

void test_ir_alone_triggers_countdown() {
    World w;
    w.tap(A);
    w.run(3000);
    w.rig.ir.blocked = true;  // 很輕、很矮的東西：只有紅外線擋到
    w.run(1000);
    w.tap(A);
    TEST_ASSERT_EQUAL(State::Countdown, w.b->state());
}

void test_state_survives_reboot() {
    World w;
    w.leaveItem(A);
    w.b->confirm(A, ConfirmKind::Trash, w.t);
    // 用同一份 storage 重開機
    Basket b2(w.rig.hw());
    b2.begin(0);
    for (uint32_t t = 50; t <= 1000; t += 50) b2.tick(t);
    TEST_ASSERT_EQUAL(85, b2.accounts().balance(A));
    TEST_ASSERT_TRUE(b2.hasTrash());
    TEST_ASSERT_EQUAL_STRING(A, b2.trashOwner().c_str());
}

void test_state_json() {
    World w;
    w.leaveItem(A);
    std::string j = stateJson(*w.b, w.t);
    TEST_ASSERT_NOT_NULL(strstr(j.c_str(), "\"state\":\"idle\""));
    TEST_ASSERT_NOT_NULL(strstr(j.c_str(), "\"owner\":\"AAAA01\""));
    TEST_ASSERT_NOT_NULL(strstr(j.c_str(), "\"balance\":100"));
    TEST_ASSERT_NOT_NULL(strstr(j.c_str(), "\"type\":\"pending\""));
}


void test_first_boot_requires_explicit_empty_calibration() {
    fakes::Rig rig;
    Basket b(rig.hw());
    b.begin(0);
    for (uint32_t t=50;t<=1000;t+=50) b.tick(t);
    TEST_ASSERT_EQUAL(State::Boot, b.state());
    TEST_ASSERT_FALSE(b.hasBaseline());
    TEST_ASSERT_TRUE(b.calibrate(1000));
    b.tick(1050);
    TEST_ASSERT_EQUAL(State::Idle, b.state());
}

void test_reboot_restores_rider_and_original_baseline() {
    World w;
    w.tap(A);
    w.putItem(150);
    w.reboot();
    TEST_ASSERT_EQUAL(State::Riding, w.b->state());
    TEST_ASSERT_EQUAL_STRING(A, w.b->rider().c_str());
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0, w.b->baseline().weightG);
    TEST_ASSERT_TRUE(w.b->detectNow().present);
    w.tap(A); // 重啟後仍貼著的卡不能立刻還車。
    TEST_ASSERT_EQUAL(State::Riding, w.b->state());
    w.run(3000);
    w.tap(A);
    TEST_ASSERT_EQUAL(State::Countdown, w.b->state());
}

void test_reboot_restores_countdown_and_charges_only_once() {
    World w;
    w.tap(A); w.run(3000); w.putItem(150); w.tap(A);
    w.reboot();
    TEST_ASSERT_EQUAL(State::Countdown, w.b->state());
    TEST_ASSERT_EQUAL(100, w.bal(A));
    w.run(16000);
    TEST_ASSERT_EQUAL(1, w.b->pendings().size());
    TEST_ASSERT_TRUE(w.b->confirm(A, ConfirmKind::Trash, w.t));
    w.reboot();
    TEST_ASSERT_EQUAL(85, w.bal(A));
    TEST_ASSERT_EQUAL(0, w.b->pendings().size());
    w.run(130000);
    TEST_ASSERT_EQUAL(85, w.bal(A));
}

void test_reboot_preserves_marked_ride_snapshot() {
    World w;
    w.leaveItem(A); w.b->confirm(A, ConfirmKind::Trash, w.t);
    w.tap(B); w.putItem(400); w.reboot(); w.run(3000); w.tap(B);
    TEST_ASSERT_EQUAL(State::Countdown, w.b->state());
}

void test_idle_reboot_does_not_rezero_existing_item() {
    World w;
    w.putItem(150); w.reboot();
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0, w.b->baseline().weightG);
    TEST_ASSERT_TRUE(w.b->detectNow().present);
    w.tap(B);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0, w.b->baseline().weightG);
}

void test_open_lid_or_unsettled_readings_cannot_rent() {
    World w;
    w.leaveItem(A); w.b->confirm(A, ConfirmKind::Trash, w.t);
    w.rig.lid.closed=false; w.run(500); w.tap(B);
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    w.rig.lid.closed=true;
    w.rig.card.tap("CCCC03"); w.run(50);
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    w.run(3000); w.tap(B); w.putItem(400); w.run(3000); w.tap(B);
    TEST_ASSERT_EQUAL(State::Countdown, w.b->state());
}

void test_failed_sensors_are_invalid_and_block_return() {
    World w;
    w.tap(A); w.run(3000);
    w.rig.weight.ok=false; w.rig.dist0.ok=false; w.rig.dist1.ok=false;
    w.run(1000);
    TEST_ASSERT_FALSE(w.b->sensorsReady());
    TEST_ASSERT_FALSE(w.b->current().weightValid);
    TEST_ASSERT_FALSE(w.b->current().distValid[0]);
    TEST_ASSERT_FALSE(w.b->current().distValid[1]);
    w.tap(A);
    TEST_ASSERT_EQUAL(State::Riding, w.b->state());
    TEST_ASSERT_EQUAL(0, w.b->pendings().size());
    w.rig.weight.ok=w.rig.dist0.ok=w.rig.dist1.ok=true;
    w.run(200);
    TEST_ASSERT_FALSE(w.b->sensorsReady());
    w.run(3000); w.tap(A);
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
}

void test_single_sensor_or_ir_fault_blocks_rent() {
    World w;
    w.rig.dist0.ok=false; w.run(500); w.tap(A);
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    w.rig.dist0.ok=true; w.rig.ir.ok=false; w.run(3000); w.tap(A);
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    TEST_ASSERT_FALSE(w.b->irValid());
    TEST_ASSERT_NOT_NULL(strstr(stateJson(*w.b,w.t).c_str(), "\"irValid\":false"));
    w.rig.ir.ok=true; w.run(3000); w.tap(A);
    TEST_ASSERT_EQUAL(State::Riding, w.b->state());
}

void test_optical_only_mode_preserves_baseline_and_ride() {
    fakes::Rig rig;
    auto hw = rig.hw();
    hw.weight = nullptr;
    hw.display = nullptr;
    rig.dist0.mm = rig.dist1.mm = 300;
    uint32_t now = 0;
    auto tick = [&now](Basket& basket, unsigned samples = 20) {
        for (unsigned i = 0; i < samples; ++i) basket.tick(now += 50);
    };
    Basket basket(hw);
    basket.begin(now);
    tick(basket);
    TEST_ASSERT_TRUE(basket.sensorsReady());
    TEST_ASSERT_FALSE(basket.current().weightValid);
    TEST_ASSERT_TRUE(basket.calibrate(now));
    tick(basket);
    TEST_ASSERT_EQUAL(State::Idle, basket.state());
    TEST_ASSERT_FALSE(basket.displayText().line[0].empty());
    rig.card.tap(A);
    tick(basket);
    TEST_ASSERT_EQUAL(State::Riding, basket.state());

    Basket restored(hw);
    restored.begin(now);
    TEST_ASSERT_TRUE(restored.hasBaseline());
    TEST_ASSERT_EQUAL(State::Riding, restored.state());
    rig.dist0.ok = false;
    tick(restored, 80);
    TEST_ASSERT_FALSE(restored.sensorsReady());
    rig.card.tap(A);
    tick(restored);
    TEST_ASSERT_EQUAL(State::Riding, restored.state());
    rig.dist0.ok = true;
    rig.dist1.mm = 200;
    tick(restored, 80);
    rig.card.tap(A);
    tick(restored);
    TEST_ASSERT_EQUAL(State::Countdown, restored.state());
}

void test_sensor_fault_during_countdown_never_finishes_empty() {
    World w;
    w.tap(A); w.run(3000); w.putItem(150); w.tap(A);
    w.rig.weight.ok=false; w.run(20000);
    TEST_ASSERT_EQUAL(State::Countdown, w.b->state());
    TEST_ASSERT_EQUAL(0, w.b->pendings().size());
    TEST_ASSERT_EQUAL(100, w.bal(A));
    w.rig.weight.ok=true; w.run(1000);
    TEST_ASSERT_EQUAL(1, w.b->pendings().size());
}

void test_tof_only_rent_return_and_item_confirmation() {
    World w(Config{}, true);
    TEST_ASSERT_TRUE(w.b->hasBaseline());
    TEST_ASSERT_TRUE(w.b->sensorsReady());
    TEST_ASSERT_FALSE(w.b->irEnabled());
    TEST_ASSERT_FALSE(w.b->weightEnabled());
    TEST_ASSERT_NOT_NULL(strstr(stateJson(*w.b, w.t).c_str(), "\"irEnabled\":false"));

    w.rig.dist1.ok = false;
    w.run(500);
    w.tap(A);
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    w.rig.dist1.ok = true;
    w.run(3000);
    w.tap(A);
    TEST_ASSERT_EQUAL(State::Riding, w.b->state());
    w.run(3000);
    w.tap(A);
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    TEST_ASSERT_EQUAL(100, w.bal(A));

    w.run(3000);
    w.leaveItem(A);
    TEST_ASSERT_EQUAL(1, w.b->pendings().size());
    TEST_ASSERT_TRUE(w.b->confirm(A, ConfirmKind::Mine, w.t));
    TEST_ASSERT_TRUE(w.b->hasLost());
    TEST_ASSERT_EQUAL(100, w.bal(A));
}

void test_calibration_and_config_cannot_change_during_ride() {
    World w;
    w.tap(A); w.run(3000); w.putItem(150);
    TEST_ASSERT_FALSE(w.b->calibrate(w.t));
    TEST_ASSERT_FALSE(w.b->setConfig("reward", 3));
    w.tap(A);
    TEST_ASSERT_FALSE(w.b->calibrate(w.t));
    w.run(16000);
    TEST_ASSERT_FALSE(w.b->calibrate(w.t));
    TEST_ASSERT_FALSE(w.b->setConfig("penalty", 20));
}

void test_lower_penalty_and_reward_survive_reboot() {
    World w;
    TEST_ASSERT_TRUE(w.b->setConfig("reward", 3));
    TEST_ASSERT_TRUE(w.b->setConfig("penalty", 5));
    w.reboot();
    TEST_ASSERT_EQUAL(5, w.b->config().penalty);
    TEST_ASSERT_EQUAL(3, w.b->config().reward);
    w.leaveItem(A); w.b->confirm(A, ConfirmKind::Trash, w.t);
    TEST_ASSERT_EQUAL(95, w.bal(A));
}

void test_hardware_startup_failure_blocks_actions() {
    World w;
    w.b->setHardwareReady(false);
    w.tap(A); w.press();
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    TEST_ASSERT_EQUAL(0, w.rig.lock.pulses);
    TEST_ASSERT_FALSE(w.b->calibrate(w.t));
}

void test_failed_storage_write_stops_operations_and_keeps_last_snapshot() {
    World w;
    std::string before=w.rig.storage.data.at("state");
    w.rig.storage.writable=false;
    TEST_ASSERT_FALSE(w.b->setConfig("reward", 3));
    TEST_ASSERT_FALSE(w.b->storageHealthy());
    TEST_ASSERT_EQUAL_STRING(before.c_str(),w.rig.storage.data.at("state").c_str());
    w.tap(A); w.press();
    TEST_ASSERT_EQUAL(State::Idle, w.b->state());
    TEST_ASSERT_EQUAL(0,w.rig.lock.pulses);
    w.rig.storage.writable=true; w.reboot();
    TEST_ASSERT_TRUE(w.b->storageHealthy());
    TEST_ASSERT_EQUAL(10,w.b->config().reward);
}

void test_corrupt_snapshot_does_not_silently_reset_accounts() {
    World w;
    w.rig.storage.data["state"]="truncated";
    w.reboot(); w.tap(A);
    TEST_ASSERT_FALSE(w.b->storageHealthy());
    TEST_ASSERT_EQUAL(State::Boot,w.b->state());
}

void test_legacy_storage_migrates_without_losing_money_or_marks() {
    World w;
    w.rig.storage.data.clear();
    w.rig.storage.data["cfg"]="penalty=5;reward=3;initial=100;countdown_s=15;timeout_s=120;weight_g=10;dist_mm=20;lock_ms=600";
    w.rig.storage.data["accts"]="AAAA01=95";
    w.rig.storage.data["flags"]="1,AAAA01,,0";
    w.rig.storage.data["base"]="0,1,300,1,300,1";
    w.putItem(150); w.reboot();
    TEST_ASSERT_TRUE(w.b->hasTrash());
    TEST_ASSERT_EQUAL(95,w.bal(A));
    w.tap(B);
    TEST_ASSERT_TRUE(w.rig.storage.data.count("state")>0);
    w.reboot();
    TEST_ASSERT_EQUAL(State::Riding,w.b->state());
    TEST_ASSERT_EQUAL_STRING(B,w.b->rider().c_str());
    TEST_ASSERT_EQUAL(95,w.bal(A));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_boot_calibrates_and_goes_idle);
    RUN_TEST(test_normal_rent_and_return_empty);
    RUN_TEST(test_fixed_distance_cutoffs_apply_to_idle_and_return);
    RUN_TEST(test_button_ignored_when_not_riding);
    RUN_TEST(test_other_card_while_riding_is_rejected);
    RUN_TEST(test_card_held_on_reader_does_not_return_immediately);
    RUN_TEST(test_return_with_lid_open_is_refused);
    RUN_TEST(test_item_removed_within_countdown_no_penalty);
    RUN_TEST(test_countdown_end_creates_pending_without_charging);
    RUN_TEST(test_confirm_trash_charges_penalty);
    RUN_TEST(test_confirm_timeout_charges_penalty);
    RUN_TEST(test_confirm_timeout_is_adjustable);
    RUN_TEST(test_confirm_mine_no_penalty_marks_lost);
    RUN_TEST(test_next_rider_cleans_trash_gets_reward);
    RUN_TEST(test_reward_is_less_than_penalty);
    RUN_TEST(test_lock_pulse_is_capped);
    RUN_TEST(test_rider_not_cleaning_trash_is_not_charged);
    RUN_TEST(test_rider_adding_more_to_trash_basket_counts_down);
    RUN_TEST(test_cleaning_own_trash_gives_no_reward);
    RUN_TEST(test_cleared_before_confirm_then_trash_rewards_cleaner);
    RUN_TEST(test_trash_confirmed_during_next_ride_still_rewards);
    RUN_TEST(test_single_noisy_reading_does_not_trigger);
    RUN_TEST(test_ir_alone_triggers_countdown);
    RUN_TEST(test_state_survives_reboot);
    RUN_TEST(test_state_json);
    RUN_TEST(test_first_boot_requires_explicit_empty_calibration);
    RUN_TEST(test_reboot_restores_rider_and_original_baseline);
    RUN_TEST(test_reboot_restores_countdown_and_charges_only_once);
    RUN_TEST(test_reboot_preserves_marked_ride_snapshot);
    RUN_TEST(test_idle_reboot_does_not_rezero_existing_item);
    RUN_TEST(test_open_lid_or_unsettled_readings_cannot_rent);
    RUN_TEST(test_failed_sensors_are_invalid_and_block_return);
    RUN_TEST(test_single_sensor_or_ir_fault_blocks_rent);
    RUN_TEST(test_optical_only_mode_preserves_baseline_and_ride);
    RUN_TEST(test_sensor_fault_during_countdown_never_finishes_empty);
    RUN_TEST(test_tof_only_rent_return_and_item_confirmation);
    RUN_TEST(test_calibration_and_config_cannot_change_during_ride);
    RUN_TEST(test_lower_penalty_and_reward_survive_reboot);
    RUN_TEST(test_hardware_startup_failure_blocks_actions);
    RUN_TEST(test_failed_storage_write_stops_operations_and_keeps_last_snapshot);
    RUN_TEST(test_corrupt_snapshot_does_not_silently_reset_accounts);
    RUN_TEST(test_legacy_storage_migrates_without_losing_money_or_marks);
    return UNITY_END();
}
