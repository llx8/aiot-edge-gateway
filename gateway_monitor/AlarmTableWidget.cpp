#include "AlarmTableWidget.h"
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QDateTime>
#include <cstring>

AlarmTableWidget::AlarmTableWidget(QWidget* parent)
    : QWidget(parent)
{
    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({"Time", "Sev", "Type", "Detail"});

    table_->setStyleSheet(
        "QTableWidget{background:#161b22; border:none; color:#c9d1d9; gridline-color:#30363d;}"
        "QTableWidget::item{color:#c9d1d9; padding:4px;}"
        "QHeaderView::section{background:#21262d; color:#8b949e; border:1px solid #30363d; padding:4px;}"
    );
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(table_);
}

AlarmTableWidget::~AlarmTableWidget() {}

const char* AlarmTableWidget::severityLabel(int sev) const {
    switch (sev) {
        case 1: return "LOW";
        case 2: return "MED";
        case 3: return "HIGH";
        case 4: return "CRIT";
        default: return "?";
    }
}

const char* AlarmTableWidget::severityColor(int sev) const {
    switch (sev) {
        case 1: return "#58a6ff";
        case 2: return "#d29922";
        case 3: return "#f0883e";
        case 4: return "#f85149";
        default: return "#8b949e";
    }
}

void AlarmTableWidget::setAlarms(const std::vector<AlarmEntry>& alarms) {
    table_->setRowCount(0);
    if (alarms.empty()) {
        table_->insertRow(0);
        auto* item = new QTableWidgetItem("No alarms recorded");
        item->setForeground(QColor("#8b949e"));
        table_->setItem(0, 0, item);
        table_->setSpan(0, 0, 1, 4);
        return;
    }

    table_->setRowCount(static_cast<int>(alarms.size()));
    for (size_t i = 0; i < alarms.size(); ++i) {
        int row = static_cast<int>(i);
        const auto& a = alarms[i];

        // Time
        auto* time_item = new QTableWidgetItem(a.time);
        time_item->setForeground(QColor("#8b949e"));
        table_->setItem(row, 0, time_item);

        // Severity
        auto* sev_item = new QTableWidgetItem(severityLabel(a.severity));
        sev_item->setForeground(QColor(severityColor(a.severity)));
        table_->setItem(row, 1, sev_item);

        // Type (extract from detail if JSON)
        QString type_str = "Alert";
        auto idx = a.detail.indexOf("\"type\"");
        if (idx >= 0) {
            auto start = a.detail.indexOf('"', idx + 6) + 1;
            auto end = a.detail.indexOf('"', start);
            if (start > 0 && end > start)
                type_str = a.detail.mid(start, end - start);
        }
        auto* type_item = new QTableWidgetItem(type_str);
        table_->setItem(row, 2, type_item);

        // Detail (truncated)
        QString detail_str = a.detail.left(120);
        if (a.detail.length() > 120) detail_str += "...";
        auto* detail_item = new QTableWidgetItem(detail_str);
        table_->setItem(row, 3, detail_item);
    }
}

void AlarmTableWidget::appendAlarms(const std::vector<AlarmEntry>& new_alarms) {
    if (new_alarms.empty()) return;

    // 移除空状态行
    if (table_->rowCount() == 1 && table_->columnSpan(0, 0) == 4) {
        table_->setRowCount(0);
    }

    int old_count = table_->rowCount();
    table_->setRowCount(old_count + static_cast<int>(new_alarms.size()));

    // 新告警插入到顶部（最新在前）
    for (int i = old_count - 1; i >= 0; --i) {
        for (int col = 0; col < 4; ++col) {
            QTableWidgetItem* item = table_->takeItem(i, col);
            if (item) table_->setItem(i + static_cast<int>(new_alarms.size()), col, item);
        }
    }

    for (size_t i = 0; i < new_alarms.size(); ++i) {
        int row = static_cast<int>(new_alarms.size() - 1 - i);  // 最新在前
        const auto& a = new_alarms[i];

        auto* time_item = new QTableWidgetItem(a.time);
        time_item->setForeground(QColor("#8b949e"));
        table_->setItem(row, 0, time_item);

        auto* sev_item = new QTableWidgetItem(severityLabel(a.severity));
        sev_item->setForeground(QColor(severityColor(a.severity)));
        table_->setItem(row, 1, sev_item);

        QString type_str = "Alert";
        auto idx = a.detail.indexOf("\"type\"");
        if (idx >= 0) {
            auto start = a.detail.indexOf('"', idx + 6) + 1;
            auto end = a.detail.indexOf('"', start);
            if (start > 0 && end > start)
                type_str = a.detail.mid(start, end - start);
        }
        auto* type_item = new QTableWidgetItem(type_str);
        table_->setItem(row, 2, type_item);

        QString detail_str = a.detail.left(120);
        if (a.detail.length() > 120) detail_str += "...";
        auto* detail_item = new QTableWidgetItem(detail_str);
        table_->setItem(row, 3, detail_item);
    }

    // 保留最多 500 行
    while (table_->rowCount() > 500) {
        table_->removeRow(table_->rowCount() - 1);
    }

    table_->scrollToTop();
}
