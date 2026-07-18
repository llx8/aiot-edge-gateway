#include "SensorChartWidget.h"
#include <QPainter>
#include <algorithm>

SensorChartWidget::SensorChartWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(300, 200);
}

void SensorChartWidget::update_value(float temp, float hum) {
    temps_.push_back(temp);
    hums_.push_back(hum);
    if (static_cast<int>(temps_.size()) > kMaxPoints) {
        temps_.pop_front();
        hums_.pop_front();
    }
    recalc_range();
    update();
}

void SensorChartWidget::clear() {
    temps_.clear();
    hums_.clear();
    temp_min_ = 20; temp_max_ = 100;
    hum_min_ = 0; hum_max_ = 100;
    update();
}

void SensorChartWidget::recalc_range() {
    if (temps_.empty()) return;
    auto [tlo, thi] = std::minmax_element(temps_.begin(), temps_.end());
    float trange = *thi - *tlo;
    if (trange < 1.0f) trange = 1.0f;
    temp_min_ = *tlo - trange * 0.1f;
    temp_max_ = *thi + trange * 0.1f;
    if (temp_max_ < 90.0f) temp_max_ = 90.0f;  // 预留尖峰空间

    auto [hlo, hhi] = std::minmax_element(hums_.begin(), hums_.end());
    float hrange = *hhi - *hlo;
    if (hrange < 5.0f) hrange = 5.0f;
    hum_min_ = *hlo - hrange * 0.1f;
    hum_max_ = *hhi + hrange * 0.1f;
    if (hum_min_ < 0) hum_min_ = 0;
    if (hum_max_ > 100) hum_max_ = 100;
}

void SensorChartWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int w = width(), h = height();
    if (w < 30 || h < 30) return;

    int ml = 44, mr = 16, mt = 18, mb = 28;
    int pw = w - ml - mr, ph = h - mt - mb;

    // 背景
    p.fillRect(rect(), QColor(22, 27, 34));
    p.fillRect(ml, mt, pw, ph, QColor(13, 17, 23));

    // ── 左 Y 轴（温度 °C, 红色）──
    float t_range = temp_max_ - temp_min_;
    p.setFont(QFont("monospace", 9));
    for (int i = 0; i <= 4; ++i) {
        int y = mt + ph * i / 4;
        p.setPen(QColor(50, 50, 55));
        p.drawLine(ml, y, ml + pw, y);
        p.setPen(QColor(255, 100, 80));
        float val = temp_max_ - t_range * i / 4;
        p.drawText(0, y - 6, ml - 4, 12, Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(val, 'f', 0));
    }

    // ── 右 Y 轴（湿度 %, 蓝色）──
    float h_range = hum_max_ - hum_min_;
    for (int i = 0; i <= 4; ++i) {
        int y = mt + ph * i / 4;
        p.setPen(QColor(80, 160, 255));
        float val = hum_max_ - h_range * i / 4;
        p.drawText(ml + pw + 4, y - 6, mr, 12, Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(val, 'f', 0) + "%");
    }

    // ── 图例 ──
    p.setPen(QColor(255, 100, 80));
    p.drawText(ml + 4, mt + 14, "Temp °C");
    p.setPen(QColor(80, 160, 255));
    p.drawText(ml + 70, mt + 14, "Hum %");

    if (temps_.empty()) return;

    // ── 温度折线（红）──
    p.setPen(QPen(QColor(255, 80, 50), 2));
    float xs = static_cast<float>(pw) / (kMaxPoints - 1);
    int cnt = static_cast<int>(temps_.size());
    int xoff = kMaxPoints - cnt;
    for (int i = 0; i < cnt - 1; ++i) {
        float x1 = ml + (xoff + i) * xs;
        float y1 = mt + ph * (1.0f - (temps_[i] - temp_min_) / t_range);
        float x2 = ml + (xoff + i + 1) * xs;
        float y2 = mt + ph * (1.0f - (temps_[i + 1] - temp_min_) / t_range);
        p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    }

    // ── 湿度折线（蓝）──
    p.setPen(QPen(QColor(60, 140, 255), 2));
    for (int i = 0; i < cnt - 1; ++i) {
        float x1 = ml + (xoff + i) * xs;
        float y1 = mt + ph * (1.0f - (hums_[i] - hum_min_) / h_range);
        float x2 = ml + (xoff + i + 1) * xs;
        float y2 = mt + ph * (1.0f - (hums_[i + 1] - hum_min_) / h_range);
        p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    }
}
