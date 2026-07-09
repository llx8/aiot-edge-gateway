#include "SensorChartWidget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QFont>
#include <algorithm>
#include <cmath>

SensorChartWidget::SensorChartWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(300, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void SensorChartWidget::update_value(float value) {
    values_.push_back(value);
    if (static_cast<int>(values_.size()) > kMaxPoints) {
        values_.pop_front();
    }
    recalc_range();
    update();  // 触发 paintEvent
}

void SensorChartWidget::clear() {
    values_.clear();
    min_val_ = 0;
    max_val_ = 100;
    update();
}

void SensorChartWidget::recalc_range() {
    if (values_.empty()) return;
    auto [lo, hi] = std::minmax_element(values_.begin(), values_.end());
    float range = *hi - *lo;
    if (range < 1.0f) range = 1.0f;  // 避免除零
    min_val_ = *lo - range * 0.1f;   // 下方留 10% 边距
    max_val_ = *hi + range * 0.1f;   // 上方留 10% 边距
}

void SensorChartWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();
    if (w < 50 || h < 50) return;

    int margin_left = 50;
    int margin_right = 20;
    int margin_top = 20;
    int margin_bottom = 30;
    int plot_w = w - margin_left - margin_right;
    int plot_h = h - margin_top - margin_bottom;

    // ── 背景 ──
    p.fillRect(rect(), QColor(40, 40, 40));
    p.fillRect(margin_left, margin_top, plot_w, plot_h, QColor(30, 30, 30));

    // ── 网格线 + Y 轴标签 ──
    p.setPen(QPen(QColor(60, 60, 60), 1));
    QFont label_font = p.font();
    label_font.setPixelSize(10);
    p.setFont(label_font);
    float range = max_val_ - min_val_;
    for (int i = 0; i <= 4; ++i) {
        int y = margin_top + plot_h * i / 4;
        p.drawLine(margin_left, y, margin_left + plot_w, y);
        float val = max_val_ - range * i / 4;
        p.setPen(QColor(180, 180, 180));
        p.drawText(0, y - 6, margin_left - 4, 12, Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(val, 'f', 1));
        p.setPen(QPen(QColor(60, 60, 60), 1));
    }

    // ── X 轴标签 ──
    p.setPen(QColor(180, 180, 180));
    p.drawText(margin_left, h - margin_bottom, plot_w, 15,
               Qt::AlignLeft | Qt::AlignTop, QString("%1 points").arg(values_.size()));

    if (values_.empty()) return;

    // ── 折线 ──
    p.setPen(QPen(QColor(0, 200, 100), 2));
    float x_step = static_cast<float>(plot_w) / (kMaxPoints - 1);
    int count = static_cast<int>(values_.size());
    for (int i = 0; i < count - 1; ++i) {
        int x_offset = kMaxPoints - count;  // 左对齐，新数据在右边
        float x1 = margin_left + (x_offset + i) * x_step;
        float y1 = margin_top + plot_h * (1.0f - (values_[i] - min_val_) / range);
        float x2 = margin_left + (x_offset + i + 1) * x_step;
        float y2 = margin_top + plot_h * (1.0f - (values_[i + 1] - min_val_) / range);
        p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    }

    // ── 最后一个数据点：小圆点 ──
    int last_x_offset = kMaxPoints - count + (count - 1);
    float last_x = margin_left + last_x_offset * x_step;
    float last_y = margin_top + plot_h * (1.0f - (values_.back() - min_val_) / range);
    p.setBrush(QColor(0, 200, 100));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(last_x, last_y), 4, 4);

    // ── 当前值显示 ──
    p.setPen(QColor(200, 200, 200));
    QFont val_font = p.font();
    val_font.setPixelSize(12);
    val_font.setBold(true);
    p.setFont(val_font);
    p.drawText(margin_left + plot_w - 80, margin_top,
               76, 16, Qt::AlignRight | Qt::AlignVCenter,
               QString::number(values_.back(), 'f', 1));
}
