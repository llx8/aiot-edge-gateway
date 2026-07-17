#include "DashboardWidget.h"
#include <QLabel>
#include <QGridLayout>
#include <QFrame>
#include "Logger.h"

DashboardWidget::DashboardWidget(ShmReader& reader, QWidget* parent)
    : QWidget(parent)
    , reader_(reader)
{
    cpu_val_ = new QLabel("N/A", this);
    mem_val_ = new QLabel("N/A", this);
    online_nodes_val_ = new QLabel("N/A", this);
    total_packets_val_ = new QLabel("N/A", this);
    total_alarm_val_ = new QLabel("N/A", this);
    alarm_active_val_ = new QLabel("N/A", this);
    npu_temp_val_ = new QLabel("N/A", this);
    inference_fps_val_ = new QLabel("N/A", this);

    for (auto* v : {cpu_val_, mem_val_, online_nodes_val_, total_packets_val_,
                     total_alarm_val_, alarm_active_val_, npu_temp_val_, inference_fps_val_}) {
        v->setStyleSheet("font-size:18px; font-weight:bold; color:#f0f6fc;");
        v->setAlignment(Qt::AlignCenter);
    }

    auto makeCard = [&](const QString& title, QLabel* val) -> QFrame* {
        auto* card = new QFrame(this);
        card->setStyleSheet(
            "QFrame{background:#161b22; border:1px solid #30363d; border-radius:6px;}");
        auto* lay = new QVBoxLayout(card);
        lay->setContentsMargins(8, 6, 8, 6);
        lay->setSpacing(2);
        auto* lbl = new QLabel(title, card);
        lbl->setStyleSheet("font-size:10px; color:#8b949e; border:none;");
        lbl->setAlignment(Qt::AlignCenter);
        lay->addWidget(lbl);
        lay->addWidget(val);
        return card;
    };

    auto* grid = new QGridLayout(this);
    grid->setSpacing(8);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->addWidget(makeCard("CPU", cpu_val_), 0, 0);
    grid->addWidget(makeCard("Memory", mem_val_), 0, 1);
    grid->addWidget(makeCard("NPU Temp", npu_temp_val_), 0, 2);
    grid->addWidget(makeCard("Inference FPS", inference_fps_val_), 1, 0);
    grid->addWidget(makeCard("Alarms", total_alarm_val_), 1, 1);
    grid->addWidget(makeCard("Online", online_nodes_val_), 1, 2);
}

DashboardWidget::~DashboardWidget() {}

void DashboardWidget::refresh() {
    ShmBlock block;
    if (reader_.read(block)) {
        cpu_val_->setText(QString::number(block.cpu_usage, 'f', 1) + "%");
        mem_val_->setText(QString::number(block.mem_usage, 'f', 1) + "%");
        online_nodes_val_->setText(QString::number(block.online_nodes));
        total_alarm_val_->setText(QString::number(block.total_alarms));
        alarm_active_val_->setText(QString::number(block.alarm_active));
        npu_temp_val_->setText(block.npu_temp_c > 0
            ? QString::number(block.npu_temp_c, 'f', 1) + "\u00b0C"
            : "N/A");
        inference_fps_val_->setText(block.inference_fps > 0
            ? QString::number(block.inference_fps, 'f', 1)
            : "N/A");
        this->repaint();  // force immediate repaint on VNC/X11
    } else {
        GetLogger("DashboardWidget")->warn("ShmReader::read() failed - no shared memory?");
    }
}
