#include "AlarmTableWidget.h"
#include <QTableWidget>
#include <QTimer>
#include "Logger.h"
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QDateTime>
#include <cstring>

// 构造函数，传入ShmReader引用
AlarmTableWidget::AlarmTableWidget(ShmReader& reader, QWidget* parent)
    : QWidget(parent)
    , reader_(reader)
{
    // 创建表格
    table_ = new QTableWidget(this);
    // 设置表格列数为3
    table_->setColumnCount(3);
    // 设置表头
    QStringList headers;
    headers << "Alarm Info" << "Active Count" << "Time";
    table_->setHorizontalHeaderLabels(headers);
    // 布局
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(table_);
    setLayout(layout);

    // 创建定时器，每秒轮询一次共享内存数据
    timer_ = new QTimer(this);
    timer_->setInterval(1000); // 1秒
    // 连接定时器的timeout信号到槽函数
    connect(timer_, &QTimer::timeout, this, &AlarmTableWidget::onUpdateTimer);
    timer_->start();
}

// 析构函数 不需要手动释放reader_，因为它是引用，不负责生命周期
AlarmTableWidget::~AlarmTableWidget() {
    // 打印日志
    GetLogger("gateway_monitor")->info("AlarmTableWidget destroyed");
    // qt对象树会自动释放子控件和定时器
}

// QTimer::timeout()槽函数，用于轮询共享内存数据
void AlarmTableWidget::onUpdateTimer() {
    // 读取共享内存数据
    ShmBlock block;
    if (!reader_.read(block)) {
        GetLogger("gateway_monitor")->error("Failed to read shared memory");
        return;
    }
    // 清空旧数据
    table_->setRowCount(0);
    // 插入一行
    table_->insertRow(0);

    if(block.alarm_active == 0 && strlen(block.last_alarm) == 0) {
        // 没有告警
        table_->setItem(0, 0, new QTableWidgetItem("No active alarms"));
        table_->setItem(0, 1, new QTableWidgetItem("0"));
        table_->setItem(0, 2, new QTableWidgetItem("0"));
        return;
    }
    else{
            // 设置单元格内容
        table_->setItem(0, 0, new QTableWidgetItem(block.last_alarm));
        table_->setItem(0, 1, new QTableWidgetItem(QString::number(block.alarm_active)));
        // 时间列
        table_->setItem(0, 2, new QTableWidgetItem(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")));
    }
    
}