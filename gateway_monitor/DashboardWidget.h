#pragma once

#include <QWidget>
#include "ShmReader.h"

class QLabel;

class DashboardWidget : public QWidget {
    Q_OBJECT
public:
    // 构造函数，传入ShmReader引用
    explicit DashboardWidget(ShmReader& reader, QWidget* parent = nullptr);

    // 析构函数
    ~DashboardWidget();

    // 刷新显示数据
    void refresh();
private:
    ShmReader& reader_; // 引用，不负责生命周期
    // 8个Label显示数值 + 8个Label显示标题
    QLabel* cpu_val_;
    QLabel* mem_val_;
    QLabel* online_nodes_val_;
    QLabel* total_packets_val_;
    QLabel* total_alarm_val_;
    QLabel* alarm_active_val_;
    QLabel* npu_temp_val_;     // NPU 温度（设计:392）
    QLabel* inference_fps_val_; // 推理帧率（设计:392）
};