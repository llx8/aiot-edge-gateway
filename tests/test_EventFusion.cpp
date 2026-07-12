#include <gtest/gtest.h>
#include "EventFusion.h"
#include <cstring>
#include <vector>

// 辅助：构造 AI 检测 payload
// 格式: [num:int32] + [Detection × N], 每个 Detection = x(4)+y(4)+w(4)+h(4)+conf(4)+class_id(4) = 24B
static std::vector<uint8_t> make_ai_payload(int num_detections, const int* class_ids) {
    std::vector<uint8_t> payload;
    payload.resize(4 + num_detections * 24);

    std::memcpy(payload.data(), &num_detections, 4);

    for (int i = 0; i < num_detections; i++) {
        size_t off = 4 + i * 24;
        float val;
        val = 0.5f; std::memcpy(payload.data() + off,      &val, 4);  // x
        val = 0.5f; std::memcpy(payload.data() + off + 4,  &val, 4);  // y
        val = 0.2f; std::memcpy(payload.data() + off + 8,  &val, 4);  // w
        val = 0.3f; std::memcpy(payload.data() + off + 12, &val, 4);  // h
        val = 0.92f; std::memcpy(payload.data() + off + 16, &val, 4); // confidence
        std::memcpy(payload.data() + off + 20, &class_ids[i], 4);     // class_id
    }

    return payload;
}

TEST(EventFusionTest, SensorMessage_UpdatesCache) {
    EventFusion fusion;

    // 发传感器消息
    InternalMessage sensor;
    sensor.source_type = 0;
    sensor.node_id = 101;
    float temp = 42.5f;
    sensor.payload.resize(4);
    std::memcpy(sensor.payload.data(), &temp, 4);

    auto result = fusion.evaluate(sensor);
    EXPECT_FALSE(result.has_value());  // 传感器消息不输出

    // 发 AI 消息查缓存，验证缓存已更新
    int class_ids[] = {1};
    InternalMessage ai;
    ai.source_type = 3;
    ai.node_id = 101;       // 和传感器同一个 node_id
    ai.payload = make_ai_payload(1, class_ids);

    fusion.add_rule({1, [](float v) { return v > 40.0f; }, 3});
    result = fusion.evaluate(ai);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->tlv_type, 0x06);
}

TEST(EventFusionTest, NoRules_ReturnsNullopt) {
    EventFusion fusion;

    int class_ids[] = {0};
    InternalMessage ai;
    ai.source_type = 3;
    ai.node_id = 101;
    ai.payload = make_ai_payload(1, class_ids);

    auto result = fusion.evaluate(ai);
    EXPECT_FALSE(result.has_value());
}

TEST(EventFusionTest, SingleRuleMatch) {
    EventFusion fusion;
    fusion.add_rule({0, [](float v) { return v > 30.0f; }, 3}); // class 0 + temperture>30 → HIGH

    // 先缓存传感器
    InternalMessage sensor;
    sensor.source_type = 0;
    sensor.node_id = 101;
    float temp = 35.0f;
    sensor.payload.resize(4);
    std::memcpy(sensor.payload.data(), &temp, 4);
    fusion.evaluate(sensor);

    // AI 检测到 class 0
    int class_ids[] = {0};
    InternalMessage ai;
    ai.source_type = 3;
    ai.node_id = 101;
    ai.payload = make_ai_payload(1, class_ids);

    auto result = fusion.evaluate(ai);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->tlv_type, 0x06);
    EXPECT_EQ(result->source_type, 3);
    EXPECT_EQ(result->node_id, 101);

    // 验证 severity（payload 末尾 4 字节）
    int severity;
    std::memcpy(&severity, result->payload.data() + result->payload.size() - 4, 4);
    EXPECT_EQ(severity, 3);
}

TEST(EventFusionTest, RuleNoMatch_SensorConditionFails) {
    EventFusion fusion;
    fusion.add_rule({0, [](float v) { return v > 50.0f; }, 3}); // 需要 > 50

    // 缓存传感器值 35（不满足条件）
    InternalMessage sensor;
    sensor.source_type = 0;
    sensor.node_id = 101;
    float temp = 35.0f;
    sensor.payload.resize(4);
    std::memcpy(sensor.payload.data(), &temp, 4);
    fusion.evaluate(sensor);

    int class_ids[] = {0};
    InternalMessage ai;
    ai.source_type = 3;
    ai.node_id = 101;
    ai.payload = make_ai_payload(1, class_ids);

    auto result = fusion.evaluate(ai);
    EXPECT_FALSE(result.has_value());
}

TEST(EventFusionTest, MultipleRules_TakesMaxSeverity) {
    EventFusion fusion;
    fusion.add_rule({0, [](float v) { return v > 30.0f; }, 3});  // HIGH
    fusion.add_rule({1, [](float v) { return v > 30.0f; }, 4});  // CRITICAL（更高）

    // 缓存传感器
    InternalMessage sensor;
    sensor.source_type = 0;
    sensor.node_id = 101;
    float temp = 35.0f;
    sensor.payload.resize(4);
    std::memcpy(sensor.payload.data(), &temp, 4);
    fusion.evaluate(sensor);

    // AI 同时检测到 class 0 和 class 1（两个人，一个没戴安全帽，一个烟火）
    int class_ids[] = {0, 1};
    InternalMessage ai;
    ai.source_type = 3;
    ai.node_id = 101;
    ai.payload = make_ai_payload(2, class_ids);

    auto result = fusion.evaluate(ai);
    ASSERT_TRUE(result.has_value());

    int severity;
    std::memcpy(&severity, result->payload.data() + result->payload.size() - 4, 4);
    EXPECT_EQ(severity, 4);  // 取最高 severity
}

TEST(EventFusionTest, MultipleDetections_OneMatch) {
    EventFusion fusion;
    fusion.add_rule({2, [](float v) { return v > 30.0f; }, 3}); // class 2 才触发

    // 缓存传感器
    InternalMessage sensor;
    sensor.source_type = 0;
    sensor.node_id = 101;
    float temp = 35.0f;
    sensor.payload.resize(4);
    std::memcpy(sensor.payload.data(), &temp, 4);
    fusion.evaluate(sensor);

    // AI 检测到 class 0 和 class 1，没有 class 2
    int class_ids[] = {0, 1};
    InternalMessage ai;
    ai.source_type = 3;
    ai.node_id = 101;
    ai.payload = make_ai_payload(2, class_ids);

    auto result = fusion.evaluate(ai);
    EXPECT_FALSE(result.has_value());
}
