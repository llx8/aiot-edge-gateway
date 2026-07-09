#pragma once

#include <QMainWindow>
#include "ShmReader.h"
#include <QSocketNotifier>

class DashboardWidget;
class AlarmTableWidget;
class SensorChartWidget;

class MonitorWindow : public QMainWindow {
    Q_OBJECT
public:
    // 构造函数，创建ShmReader对象，并传入DashboardWidget和AlarmTableWidget
    explicit MonitorWindow(QWidget* parent = nullptr);
    // 析构函数，销毁ShmReader对象
    ~MonitorWindow();
private slots:
    void onShmNotify();
private:
    ShmReader* reader_; // MonitorWindow负责创建和销毁ShmReader对象
    DashboardWidget* dashboard_;// 显示CPU、内存、在线节点数、总包数、总告警数、活动告警数
    SensorChartWidget* chart_;      // 实时折线图
    AlarmTableWidget* alarm_table_; // 显示告警信息的表格
    int notify_fd_;
    QSocketNotifier* notifier_;
};