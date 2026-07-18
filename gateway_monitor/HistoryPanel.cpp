#include "HistoryPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <algorithm>

HistoryChart::HistoryChart(QWidget* parent)
    : QFrame(parent)
{
    setFixedHeight(180);
    setStyleSheet("QFrame{background:#161b22; border:1px solid #30363d; border-radius:6px;}");
}

void HistoryChart::set_data(const std::deque<float>& temps, const std::deque<float>& hums) {
    temps_ = &temps;
    hums_ = &hums;
    update();
}

void HistoryChart::paintEvent(QPaintEvent*) {
    QFrame::paintEvent(nullptr);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (!temps_ || temps_->empty()) return;

    int crw = width(), crh = height();
    int w = crw - 40;
    int h = crh - 40;
    int ox = 35, oy = 10;

    float tmin = *std::min_element(temps_->begin(), temps_->end());
    float tmax = *std::max_element(temps_->begin(), temps_->end());
    float hmin = *std::min_element(hums_->begin(), hums_->end());
    float hmax = *std::max_element(hums_->begin(), hums_->end());
    float trange = (tmax - tmin) < 1.0f ? 10.0f : (tmax - tmin);
    float hrange = (hmax - hmin) < 1.0f ? 10.0f : (hmax - hmin);

    p.setPen(QPen(QColor("#30363d"), 1));
    p.drawLine(ox, oy, ox, oy + h);
    p.drawLine(ox, oy + h, ox + w, oy + h);

    if (temps_->size() > 1) {
        QPainterPath tpath, hpath;
        float dx = (float)w / std::max(1, (int)temps_->size() - 1);
        for (size_t i = 0; i < temps_->size(); ++i) {
            float x = ox + i * dx;
            float ty = oy + h - ((*temps_)[i] - (tmin - trange * 0.1f)) / (trange * 1.2f) * h;
            float hy = oy + h - ((*hums_)[i] - (hmin - hrange * 0.1f)) / (hrange * 1.2f) * h;
            if (i == 0) { tpath.moveTo(x, ty); hpath.moveTo(x, hy); }
            else { tpath.lineTo(x, ty); hpath.lineTo(x, hy); }
        }
        p.setPen(QPen(QColor("#f0883e"), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(tpath);
        p.setPen(QPen(QColor("#58a6ff"), 2));
        p.drawPath(hpath);

        p.setPen(QColor("#8b949e"));
        p.setFont(QFont("monospace", 8));
        p.drawText(ox - 30, oy + 10, "T:" + QString::number(tmax, 'f', 1) + "°C");
        p.drawText(ox - 30, oy + h - 2, "T:" + QString::number(tmin, 'f', 1) + "°C");
    }
}

HistoryPanel::HistoryPanel(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet("QWidget{background:#0d1117;}");
    setup_ui();
}

void HistoryPanel::setup_ui() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);

    auto* title = new QLabel("History Replay", this);
    title->setStyleSheet("font-size:12px; font-weight:bold; color:#58a6ff;");
    layout->addWidget(title);

    auto* control_bar = new QFrame(this);
    control_bar->setStyleSheet("QFrame{background:#161b22; border:1px solid #30363d; border-radius:6px;}");
    auto* ctrl_lay = new QHBoxLayout(control_bar);
    ctrl_lay->setContentsMargins(8, 6, 8, 6);

    auto* start_lbl = new QLabel("From:", control_bar);
    start_lbl->setStyleSheet("font-size:11px; color:#8b949e; border:none;");
    start_edit_ = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(-300), control_bar);
    start_edit_->setDisplayFormat("HH:mm:ss");
    start_edit_->setStyleSheet("QDateTimeEdit{background:#0d1117; color:#c9d1d9; border:1px solid #30363d; padding:2px 6px; font-size:11px;}");

    auto* end_lbl = new QLabel("To:", control_bar);
    end_lbl->setStyleSheet("font-size:11px; color:#8b949e; border:none;");
    end_edit_ = new QDateTimeEdit(QDateTime::currentDateTime(), control_bar);
    end_edit_->setDisplayFormat("HH:mm:ss");
    end_edit_->setStyleSheet("QDateTimeEdit{background:#0d1117; color:#c9d1d9; border:1px solid #30363d; padding:2px 6px; font-size:11px;}");

    query_btn_ = new QPushButton("Query", control_bar);
    query_btn_->setStyleSheet(
        "QPushButton{background:#238636; color:#fff; border:none; border-radius:4px; padding:4px 12px; font-size:11px;}"
        "QPushButton:hover{background:#2ea043;}");
    connect(query_btn_, &QPushButton::clicked, this, &HistoryPanel::on_query);

    ctrl_lay->addWidget(start_lbl);
    ctrl_lay->addWidget(start_edit_);
    ctrl_lay->addWidget(end_lbl);
    ctrl_lay->addWidget(end_edit_);
    ctrl_lay->addWidget(query_btn_);
    ctrl_lay->addStretch();
    layout->addWidget(control_bar);

    chart_ = new HistoryChart(this);
    chart_->setVisible(false);
    layout->addWidget(chart_);

    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels({"Time", "Type", "Temperature", "Humidity"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setStyleSheet(
        "QTableWidget{background:#161b22; border:1px solid #30363d; border-radius:6px; gridline-color:#30363d;}"
        "QTableWidget::item{color:#c9d1d9; padding:3px 6px; font-size:11px;}"
        "QHeaderView::section{background:#21262d; color:#8b949e; border:none; padding:3px 6px; font-size:10px;}");
    layout->addWidget(table_);
}

void HistoryPanel::append_entry(const HistoryEntry& e) {
    entries_.push_back(e);
    temp_hist_.push_back(e.temp);
    hum_hist_.push_back(e.hum);
    if ((int)entries_.size() > kMaxHistory) { entries_.pop_front(); }
    if ((int)temp_hist_.size() > kMaxHistory) { temp_hist_.pop_front(); hum_hist_.pop_front(); }

    int row = table_->rowCount();
    table_->insertRow(0);
    table_->setItem(0, 0, new QTableWidgetItem(QDateTime::fromSecsSinceEpoch(e.ts).toString("HH:mm:ss")));
    table_->setItem(0, 1, new QTableWidgetItem(e.type));
    table_->setItem(0, 2, new QTableWidgetItem(QString::number(e.temp, 'f', 1) + "°C"));
    table_->setItem(0, 3, new QTableWidgetItem(QString::number(e.hum, 'f', 1) + "%"));

    chart_->set_data(temp_hist_, hum_hist_);
    if (!chart_->isVisible()) chart_->setVisible(true);
}

void HistoryPanel::clear() {
    entries_.clear();
    temp_hist_.clear();
    hum_hist_.clear();
    table_->setRowCount(0);
    chart_->setVisible(false);
}

void HistoryPanel::on_query() {
    emit query_requested(
        start_edit_->dateTime().toSecsSinceEpoch(),
        end_edit_->dateTime().toSecsSinceEpoch());
}
