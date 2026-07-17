#pragma once

#include <QMainWindow>
#include <QSocketNotifier>
#include <QTimer>
#include <memory>
#include <vector>
#include <cstdint>
#include "ShmReader.h"

class DashboardWidget;
class AlarmTableWidget;
class SensorChartWidget;
class AiSnapshotWidget;

class MonitorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MonitorWindow(QWidget* parent = nullptr);
    ~MonitorWindow();
private slots:
    void onShmNotify();
    void onUdsData();
    void onTimerRefresh();
    void onAlarmQuery();
private:
    void handleAlarmResponse(const std::vector<uint8_t>& payload);
    void sendAlarmQuery();

    std::unique_ptr<ShmReader> reader_;
    DashboardWidget* dashboard_;
    SensorChartWidget* chart_;
    AlarmTableWidget* alarm_table_;
    AiSnapshotWidget* ai_snapshot_;
    int notify_fd_;
    QSocketNotifier* notifier_;
    QTimer* refresh_timer_;
    QTimer* alarm_query_timer_;
    int last_alarm_id_ = 0;  // 增量查询：上次已收到的最大告警 ID

    int uds_fd_ = -1;
    QSocketNotifier* uds_notifier_ = nullptr;
    std::vector<uint8_t> uds_buf_;
    static constexpr size_t kUdsBufSize = 512 * 1024;
};
