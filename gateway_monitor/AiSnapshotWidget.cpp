#include "AiSnapshotWidget.h"
#include <QVBoxLayout>
#include <QPainter>
#include <QImage>

AiSnapshotWidget::AiSnapshotWidget(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    image_label_ = new QLabel(this);
    image_label_->setAlignment(Qt::AlignCenter);
    image_label_->setMinimumSize(400, 300);
    image_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    image_label_->setStyleSheet("background-color: #1e1e1e; border: 1px solid #444;");
    layout->addWidget(image_label_);
}

void AiSnapshotWidget::update_snapshot(const std::vector<uint8_t>& jpeg_data,
                                        const std::vector<QRect>& boxes,
                                        const std::vector<QString>& labels)
{
    if (jpeg_data.empty()) return;

    QImage img;
    if (!img.loadFromData(jpeg_data.data(), jpeg_data.size(), "JPEG")) return;

    QPixmap pixmap = QPixmap::fromImage(img);
    current_pixmap_ = draw_boxes(pixmap, boxes, labels);
    image_label_->setPixmap(current_pixmap_.scaled(
        image_label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QPixmap AiSnapshotWidget::draw_boxes(const QPixmap& src,
                                      const std::vector<QRect>& boxes,
                                      const std::vector<QString>& labels)
{
    QPixmap result = src.copy();
    QPainter painter(&result);
    painter.setPen(QPen(QColor(0, 255, 0), 2));

    QFont font = painter.font();
    font.setPointSize(10);
    painter.setFont(font);

    for (size_t i = 0; i < boxes.size() && i < labels.size(); i++) {
        painter.drawRect(boxes[i]);
        painter.drawText(boxes[i].topLeft() + QPoint(2, -2), labels[i]);
    }

    painter.end();
    return result;
}

void AiSnapshotWidget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
}
