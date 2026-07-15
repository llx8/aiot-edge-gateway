#pragma once

#include <QMainWindow>
#include <QSocketNotifier>
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
    void onUdsData();  // 接收 JPEG 快照等数据
private:
    std::unique_ptr<ShmReader> reader_;
    DashboardWidget* dashboard_;
    SensorChartWidget* chart_;
    AlarmTableWidget* alarm_table_;
    AiSnapshotWidget* ai_snapshot_;
    int notify_fd_;
    QSocketNotifier* notifier_;

    // UDS 持久连接：接收 gateway_core 推送的 JPEG 快照
    int uds_fd_ = -1;
    QSocketNotifier* uds_notifier_ = nullptr;
    std::vector<uint8_t> uds_buf_;  // 接收缓冲区
    static constexpr size_t kUdsBufSize = 512 * 1024;  // 512KB
};