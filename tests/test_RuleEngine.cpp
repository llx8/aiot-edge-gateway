#include <gtest/gtest.h>
#include "RuleEngine.h"
#include "InternalMessage.h"

TEST(RuleEngineTest, EmptyRules_NoCrash) {
    RuleEngine engine;
    InternalMessage msg;
    msg.source_type = 1;
    EXPECT_NO_THROW(engine.evaluate(msg));
}

TEST(RuleEngineTest, SingleRule_Match) {
    RuleEngine engine;
    int call_count = 0;

    engine.add_rule(
        // 条件：匹配 source_type == 1
        [](const InternalMessage& m) { return m.source_type == 1; },
        // 动作：计数
        [&](const InternalMessage&) { call_count++; }
    );

    InternalMessage msg;
    msg.source_type = 1;
    engine.evaluate(msg);
    EXPECT_EQ(call_count, 1);
}

TEST(RuleEngineTest, SingleRule_NoMatch) {
    RuleEngine engine;
    int call_count = 0;

    engine.add_rule(
        [](const InternalMessage& m) { return m.node_id == 100; },
        [&](const InternalMessage&) { call_count++; }
    );

    InternalMessage msg;
    msg.node_id = 1;   // 不匹配 100
    engine.evaluate(msg);
    EXPECT_EQ(call_count, 0);
}

TEST(RuleEngineTest, MultipleRules) {
    RuleEngine engine;
    int count_a = 0, count_b = 0, count_c = 0;

    // 规则 A：总是匹配
    engine.add_rule(
        [](const InternalMessage&) { return true; },
        [&](const InternalMessage&) { count_a++; }
    );
    // 规则 B：匹配 node_id == 2
    engine.add_rule(
        [](const InternalMessage& m) { return m.node_id == 2; },
        [&](const InternalMessage&) { count_b++; }
    );
    // 规则 C：匹配 source_type == 99（不匹配）
    engine.add_rule(
        [](const InternalMessage& m) { return m.source_type == 99; },
        [&](const InternalMessage&) { count_c++; }
    );

    InternalMessage msg;
    msg.node_id = 2;
    msg.source_type = 1;
    engine.evaluate(msg);

    EXPECT_EQ(count_a, 1);  // A 匹配
    EXPECT_EQ(count_b, 1);  // B 匹配
    EXPECT_EQ(count_c, 0);  // C 不匹配
}
