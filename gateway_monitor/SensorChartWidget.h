#pragma once

#include <QWidget>
#include <deque>

// 基于 QPainter 的轻量实时折线图，替代 QCustomPlot
// 显示最近 N 个传感器数据点的变化趋势
class SensorChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit SensorChartWidget(QWidget* parent = nullptr);

    // 推入新的数据点
    void update_value(float value);

    // 清空历史数据
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr int kMaxPoints = 60;   // 最多保留 60 个点
    std::deque<float> values_;              // 环形缓冲区
    float min_val_ = 0;                     // 当前窗口最小值
    float max_val_ = 100;                   // 当前窗口最大值

    void recalc_range();                    // 重新计算 Y 轴范围
};
