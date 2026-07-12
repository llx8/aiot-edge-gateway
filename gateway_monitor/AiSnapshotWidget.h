#pragma once

#include <QWidget>
#include <QPixmap>
#include <QLabel>
#include <vector>
#include <QRect>
#include <QString>

class AiSnapshotWidget : public QWidget {
    Q_OBJECT
public:
    explicit AiSnapshotWidget(QWidget* parent = nullptr);

    void update_snapshot(const std::vector<uint8_t>& jpeg_data,
                         const std::vector<QRect>& boxes,
                         const std::vector<QString>& labels);

private:
    QLabel* image_label_;
    QPixmap current_pixmap_;

    void paintEvent(QPaintEvent* event) override;
    QPixmap draw_boxes(const QPixmap& src, const std::vector<QRect>& boxes,
                       const std::vector<QString>& labels);
};
