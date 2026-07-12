#include "pipeline/Pipeline.h"
#include "UdsClient.h"

class HeartbeatReporter {
public:
    HeartbeatReporter(gateway_engine::Pipeline& pipeline, UdsClient& client);

    // 被主循环每 5s 调一次，不是独立线程
    void send_heartbeat();

private:
    gateway_engine::Pipeline& pipeline_;
    UdsClient& client_;
};