#pragma once

#include "InternalMessage.h"
#include <functional>
#include <vector>

class RuleEngine {
public:
    // 条件
    using Condition = std::function<bool(const InternalMessage&)>;
    // 动作
    using Action = std::function<void(const InternalMessage&)>;
    // 添加一条规则
    void add_rule(Condition cond, Action act);
    // 拿一条消息过所有规则
    void evaluate(const InternalMessage& msg);
private:
    struct Rule {
        Condition cond;
        Action act;
    };
    std::vector<Rule> rules_;
};
