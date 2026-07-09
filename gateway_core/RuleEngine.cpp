#include "RuleEngine.h"

void RuleEngine::add_rule(Condition cond, Action act) {
    rules_.push_back({std::move(cond), std::move(act)});
}

void RuleEngine::evaluate(const InternalMessage& msg) {
    for (const auto& rule : rules_) {
        if (rule.cond(msg)) {
            rule.act(msg);
        }
    }
}