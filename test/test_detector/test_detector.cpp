// 判定邏輯：重量、測距、紅外線三種條件各自觸發，以及門檻邊界值；平滑處理。
#include <Detector.h>
#include <unity.h>

using namespace core;

static Config cfg;
static Reading base;

static Reading emptyReading() {
    Reading r;
    r.weightValid = true;
    r.weightG = 50;  // 籃子本身的重量（基準不一定是 0）
    r.distValid[0] = r.distValid[1] = true;
    r.distMm[0] = 300;
    r.distMm[1] = 310;
    r.irBlocked = false;
    return r;
}

void setUp() {
    cfg = Config();
    base = emptyReading();
}
void tearDown() {}

void test_empty_is_not_present() {
    DetectResult r = detectItems(base, emptyReading(), cfg);
    TEST_ASSERT_FALSE(r.present);
}

void test_weight_boundary() {
    Reading n = emptyReading();
    n.weightG = 50 + 9.9f;
    TEST_ASSERT_FALSE(detectItems(base, n, cfg).present);
    n.weightG = 50 + 10.0f;  // 「多出 10 g 以上」：剛好 10 g 要算
    DetectResult r = detectItems(base, n, cfg);
    TEST_ASSERT_TRUE(r.present);
    TEST_ASSERT_TRUE(r.byWeight);
    TEST_ASSERT_FALSE(r.byDist[0] || r.byDist[1] || r.byIr);
}

void test_weight_decrease_is_not_item() {
    Reading n = emptyReading();
    n.weightG = 0;
    TEST_ASSERT_FALSE(detectItems(base, n, cfg).present);
}

void test_distance0_boundary() {
    Reading n = emptyReading();
    n.distMm[0] = 300 - 19;
    TEST_ASSERT_FALSE(detectItems(base, n, cfg).present);
    n.distMm[0] = 300 - 20;  // 短 20 mm 以上
    DetectResult r = detectItems(base, n, cfg);
    TEST_ASSERT_TRUE(r.present);
    TEST_ASSERT_TRUE(r.byDist[0]);
    TEST_ASSERT_FALSE(r.byWeight || r.byDist[1] || r.byIr);
}

void test_distance1_only() {
    Reading n = emptyReading();
    n.distMm[1] = 200;
    DetectResult r = detectItems(base, n, cfg);
    TEST_ASSERT_TRUE(r.present);
    TEST_ASSERT_TRUE(r.byDist[1]);
    TEST_ASSERT_FALSE(r.byDist[0]);
}

void test_distance_longer_is_not_item() {
    Reading n = emptyReading();
    n.distMm[0] = 400;
    TEST_ASSERT_FALSE(detectItems(base, n, cfg).present);
}

void test_ir_blocked() {
    Reading n = emptyReading();
    n.irBlocked = true;
    DetectResult r = detectItems(base, n, cfg);
    TEST_ASSERT_TRUE(r.present);
    TEST_ASSERT_TRUE(r.byIr);
    TEST_ASSERT_FALSE(r.byWeight || r.byDist[0] || r.byDist[1]);
}

void test_ir_already_blocked_in_baseline_is_ignored() {
    // 用「借車當下」的讀數當基準比較時，本來就擋住的紅外線不算新增
    base.irBlocked = true;
    Reading n = emptyReading();
    n.irBlocked = true;
    TEST_ASSERT_FALSE(detectItems(base, n, cfg).present);
}

void test_invalid_sensor_is_ignored() {
    Reading n = emptyReading();
    n.distValid[0] = false;
    n.distMm[0] = 0;  // 讀不到時的垃圾值不能造成誤判
    n.weightValid = false;
    n.weightG = 9999;
    TEST_ASSERT_FALSE(detectItems(base, n, cfg).present);
}

void test_thresholds_are_configurable() {
    TEST_ASSERT_TRUE(cfg.set("weight_g", 50));
    TEST_ASSERT_TRUE(cfg.set("dist_mm", 5));
    Reading n = emptyReading();
    n.weightG = 50 + 30;
    TEST_ASSERT_FALSE(detectItems(base, n, cfg).present);
    n = emptyReading();
    n.distMm[0] = 300 - 5;
    TEST_ASSERT_TRUE(detectItems(base, n, cfg).present);
}

void test_smoother_average() {
    Smoother s(5);
    TEST_ASSERT_FALSE(s.hasValue());
    for (int i = 0; i < 4; ++i) s.add(0);
    s.add(40);  // 單次雜訊
    TEST_ASSERT_TRUE(s.full());
    TEST_ASSERT_FLOAT_WITHIN(0.001, 8.0, s.value());  // 平均後低於 10 g 門檻
    for (int i = 0; i < 5; ++i) s.add(20);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 20.0, s.value());  // 舊資料被擠掉
    s.clear();
    TEST_ASSERT_FALSE(s.hasValue());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_is_not_present);
    RUN_TEST(test_weight_boundary);
    RUN_TEST(test_weight_decrease_is_not_item);
    RUN_TEST(test_distance0_boundary);
    RUN_TEST(test_distance1_only);
    RUN_TEST(test_distance_longer_is_not_item);
    RUN_TEST(test_ir_blocked);
    RUN_TEST(test_ir_already_blocked_in_baseline_is_ignored);
    RUN_TEST(test_invalid_sensor_is_ignored);
    RUN_TEST(test_thresholds_are_configurable);
    RUN_TEST(test_smoother_average);
    return UNITY_END();
}
