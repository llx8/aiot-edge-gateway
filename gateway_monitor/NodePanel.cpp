#include "NodePanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFrame>
#include <QTableWidgetItem>
#include <QFont>

NodePanel::NodePanel(ShmReader& reader, QWidget* parent)
    : QWidget(parent)
    , reader_(reader)
{
    setStyleSheet("QWidget{background:#0d1117;}");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(8);

    auto* title = new QLabel("Node Management", this);
    title->setStyleSheet("font-size:12px; font-weight:bold; color:#58a6ff;");
    layout->addWidget(title);

    auto* status_bar = new QFrame(this);
    status_bar->setStyleSheet("QFrame{background:#161b22; border:1px solid #30363d; border-radius:6px;}");
    auto* status_lay = new QHBoxLayout(status_bar);
    status_lay->setContentsMargins(12, 8, 12, 8);

    ai_status_ = new QLabel("AI Engine: --", status_bar);
    sensor_status_ = new QLabel("Sensors: --", status_bar);
    mqtt_status_ = new QLabel("MQTT: --", status_bar);
    for (auto* lbl : {ai_status_, sensor_status_, mqtt_status_}) {
        lbl->setStyleSheet("font-size:11px; color:#8b949e; border:none;");
        status_lay->addWidget(lbl);
    }
    layout->addWidget(status_bar);

    table_ = new QTableWidget(0, 7, this);
    table_->setHorizontalHeaderLabels({"ID", "Type", "Status", "Detail", "Heartbeat", "Temp/CPU", "Hum/Mem"});
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setStyleSheet(
        "QTableWidget{background:#161b22; border:1px solid #30363d; border-radius:6px; gridline-color:#30363d;}"
        "QTableWidget::item{color:#c9d1d9; padding:4px 8px;}"
        "QHeaderView::section{background:#21262d; color:#8b949e; border:none; padding:4px 8px; font-size:10px;}");
    layout->addWidget(table_);
}

void NodePanel::refresh() {
    ShmBlock block;
    if (!reader_.read(block)) return;

    bool ai_online = block.ai_engine_online != 0;
    bool mqtt_ok = block.mqtt_connected != 0;
    bool sensors_ok = block.online_nodes > 0;

    ai_status_->setText(QString("AI Engine: %1  FPS:%2  NPU:%3°C  %4")
        .arg(ai_online ? "ONLINE" : "OFFLINE")
        .arg(block.inference_fps, 0, 'f', 1)
        .arg(block.npu_temp_c, 0, 'f', 1)
        .arg(block.last_model_name[0] ? QString("model=%1").arg(block.last_model_name) : ""));
    ai_status_->setStyleSheet(QString("font-size:11px; color:%1; border:none;")
        .arg(ai_online ? "#3fb950" : "#f85149"));

    sensor_status_->setText(QString("Sensors: %1 nodes  T:%2°C  H:%3%%")
        .arg(block.online_nodes)
        .arg(block.sensor_temp, 0, 'f', 1)
        .arg(block.sensor_hum, 0, 'f', 1));
    sensor_status_->setStyleSheet(QString("font-size:11px; color:%1; border:none;")
        .arg(sensors_ok ? "#3fb950" : "#8b949e"));

    mqtt_status_->setText(QString("MQTT: %1  Pkts:%2  CPU:%3%  Mem:%4%")
        .arg(mqtt_ok ? "CONNECTED" : "DISCONNECTED")
        .arg(block.total_packets)
        .arg(block.cpu_usage, 0, 'f', 1)
        .arg(block.mem_usage, 0, 'f', 1));
    mqtt_status_->setStyleSheet(QString("font-size:11px; color:%1; border:none;")
        .arg(mqtt_ok ? "#3fb950" : "#f85149"));

    int row = 0;
    table_->setRowCount(5);

    auto set_cell = [&](int r, int c, const QString& v, const QString& color = "#c9d1d9") {
        auto* item = table_->item(r, c);
        if (!item) { item = new QTableWidgetItem(); table_->setItem(r, c, item); }
        item->setText(v);
        item->setForeground(QColor(color));
    };

    set_cell(row, 0, "0"); set_cell(row, 1, "AI Engine");
    set_cell(row, 2, ai_online ? "ONLINE" : "OFFLINE", ai_online ? "#3fb950" : "#f85149");
    set_cell(row, 3, block.last_model_name[0] ? block.last_model_name : "N/A");
    set_cell(row, 4, QString("%1 FPS").arg(block.inference_fps, 0, 'f', 1));
    set_cell(row, 5, QString("%1°C").arg(block.npu_temp_c, 0, 'f', 1));
    set_cell(row, 6, QString("v%1").arg(block.model_version));
    row++;

    set_cell(row, 0, "1"); set_cell(row, 1, "Modbus Sensor");
    set_cell(row, 2, sensors_ok ? "ONLINE" : "OFFLINE", sensors_ok ? "#3fb950" : "#8b949e");
    set_cell(row, 3, "RTU /dev/ttyUSB0");
    set_cell(row, 4, QString("%1 nodes").arg(block.online_nodes));
    set_cell(row, 5, QString("%1°C").arg(block.sensor_temp, 0, 'f', 1));
    set_cell(row, 6, QString("%1%").arg(block.sensor_hum, 0, 'f', 1));
    row++;

    set_cell(row, 0, "100"); set_cell(row, 1, "MQTT Broker");
    set_cell(row, 2, mqtt_ok ? "CONNECTED" : "DOWN", mqtt_ok ? "#3fb950" : "#f85149");
    set_cell(row, 3, "SparkplugB");
    set_cell(row, 4, QString("%1 pkts").arg(block.total_packets));
    set_cell(row, 5, QString("CPU %1%").arg(block.cpu_usage, 0, 'f', 1));
    set_cell(row, 6, QString("Mem %1%").arg(block.mem_usage, 0, 'f', 1));
    row++;

    set_cell(row, 0, "255"); set_cell(row, 1, "Watchdog");
    set_cell(row, 2, "ACTIVE", "#3fb950");
    set_cell(row, 3, "fork/waitpid");
    set_cell(row, 4, QString("uptime %1s").arg(block.uptime_sec));
    set_cell(row, 5, QString("%1 alarms").arg(block.total_alarms));
    set_cell(row, 6, block.alarm_active ? "ALARMS ACTIVE" : "OK");
    row++;

    set_cell(row, 0, "--"); set_cell(row, 1, "HTTP Dashboard");
    set_cell(row, 2, "ACTIVE", "#3fb950");
    set_cell(row, 3, ":8081");
    set_cell(row, 4, QString::number(block.last_detection_ts));
    set_cell(row, 5, "JPEG:");
    set_cell(row, 6, QString("%1B").arg(block.snapshot_jpeg_len));
}
