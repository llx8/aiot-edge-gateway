#pragma once

#include <QWidget>
#include "ShmReader.h"

class QLabel;
class QTimer;

class DashboardWidget : public QWidget {
    Q_OBJECT
public:
    // 构造函数，传入ShmReader引用
    explicit DashboardWidget(ShmReader& reader, QWidget* parent = nullptr);

    // 析构函数
    ~DashboardWidget();

private slots:
    // QTimer::timeout()槽函数，用于轮询共享内存数据
    void onUpdateTimer();
private:
    ShmReader& reader_; // 引用，不负责生命周期
    // 6个Label显示数值 + 6个Label显示标题
    QLabel* cpu_val_;
    QLabel* mem_val_;
    QLabel* online_nodes_val_;
    QLabel* total_packets_val_;
    QLabel* total_alarm_val_;
    QLabel* alarm_active_val_;

    QTimer* timer_; // 轮询定时器
};