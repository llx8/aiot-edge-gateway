#pragma once

#include <QWidget>
#include <deque>

class SensorChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit SensorChartWidget(QWidget* parent = nullptr);

    void update_value(float temp, float hum);

    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr int kMaxPoints = 60;
    std::deque<float> temps_;
    std::deque<float> hums_;
    float temp_min_ = 20, temp_max_ = 100;
    float hum_min_ = 0, hum_max_ = 100;

    void recalc_range();
};
