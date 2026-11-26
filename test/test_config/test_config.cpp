#include <Config.h>
#include <unity.h>
#include <limits>
using namespace core;
void setUp() {}
void tearDown() {}
void test_parse_validates_combined_fields_regardless_of_order() {
    Config c;
    TEST_ASSERT_TRUE(c.parse("penalty=5;reward=3"));
    TEST_ASSERT_EQUAL(5,c.penalty);
    Config d;
    TEST_ASSERT_TRUE(d.parse("reward=3;penalty=5"));
    TEST_ASSERT_EQUAL(5,d.penalty);
}
void test_invalid_parse_keeps_previous_configuration() {
    Config c;
    TEST_ASSERT_FALSE(c.parse("reward=99;penalty=5"));
    TEST_ASSERT_EQUAL(15,c.penalty);
    TEST_ASSERT_EQUAL(10,c.reward);
    TEST_ASSERT_FALSE(c.parse("reward=garbage"));
    TEST_ASSERT_FALSE(c.parse("penalty=5x;reward=3"));
}
void test_out_of_range_and_nonfinite_values_are_rejected() {
    Config c;
    TEST_ASSERT_FALSE(c.set("weight_g",std::numeric_limits<double>::infinity()));
    TEST_ASSERT_FALSE(c.set("penalty",std::numeric_limits<double>::quiet_NaN()));
    TEST_ASSERT_FALSE(c.set("penalty",1e30));
    TEST_ASSERT_FALSE(c.set("reward",3.5));
    TEST_ASSERT_FALSE(c.set("timeout_s",2147484));
    TEST_ASSERT_FALSE(c.set("countdown_s",-1));
    TEST_ASSERT_FALSE(c.set("lock_ms",1001));
}
void test_precision_survives_save_and_load() {
    Config c;
    TEST_ASSERT_TRUE(c.set("timeout_s",2147483.647));
    TEST_ASSERT_TRUE(c.set("weight_g",10.123456));
    Config loaded;
    TEST_ASSERT_TRUE(loaded.parse(c.serialize()));
    TEST_ASSERT_EQUAL_UINT32(c.confirmTimeoutMs,loaded.confirmTimeoutMs);
    TEST_ASSERT_EQUAL_FLOAT(c.weightThresholdG,loaded.weightThresholdG);
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_validates_combined_fields_regardless_of_order);
    RUN_TEST(test_invalid_parse_keeps_previous_configuration);
    RUN_TEST(test_out_of_range_and_nonfinite_values_are_rejected);
    RUN_TEST(test_precision_survives_save_and_load);
    return UNITY_END();
}
