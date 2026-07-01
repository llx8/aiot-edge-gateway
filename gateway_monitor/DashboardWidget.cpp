#include "DashboardWidget.h"
#include <QLabel>
#include <QTimer>
#include <QGridLayout>
#include "Logger.h"

// 构造函数，传入ShmReader引用
DashboardWidget::DashboardWidget(ShmReader& reader, QWidget* parent)
    : QWidget(parent)
    , reader_(reader)
{
    // 创建6个Label显示数值
    cpu_val_ = new QLabel("N/A", this);
    mem_val_ = new QLabel("N/A", this);
    online_nodes_val_ = new QLabel("N/A", this);
    total_packets_val_ = new QLabel("N/A", this);
    total_alarm_val_ = new QLabel("N/A", this);
    alarm_active_val_ = new QLabel("N/A", this);

    // 创建6个Label显示标题
    QLabel* cpu_label = new QLabel("CPU Usage:", this);
    QLabel* mem_label = new QLabel("Memory Usage:", this);
    QLabel* online_nodes_label = new QLabel("Online Nodes:", this);
    QLabel* total_packets_label = new QLabel("Total Packets:", this);
    QLabel* total_alarm_label = new QLabel("Total Alarms:", this);
    QLabel* alarm_active_label = new QLabel("Active Alarms:", this);

    // 使用QGridLayout布局
    QGridLayout* layout = new QGridLayout(this);
    layout->addWidget(cpu_label, 0, 0);
    layout->addWidget(cpu_val_, 0, 1);
    layout->addWidget(mem_label, 1, 0);
    layout->addWidget(mem_val_, 1, 1);
    layout->addWidget(online_nodes_label, 2, 0);
    layout->addWidget(online_nodes_val_, 2, 1);
    layout->addWidget(total_packets_label, 3, 0);
    layout->addWidget(total_packets_val_, 3, 1);
    layout->addWidget(total_alarm_label, 4, 0);
    layout->addWidget(total_alarm_val_, 4, 1);
    layout->addWidget(alarm_active_label, 5, 0);
    layout->addWidget(alarm_active_val_, 5, 1);

    setLayout(layout);

    // 创建定时器，每秒轮询一次共享内存数据
    timer_ = new QTimer(this);
    timer_->setInterval(1000); // 1秒
    // 连接定时器的timeout信号到槽函数
    connect(timer_, &QTimer::timeout, this, &DashboardWidget::onUpdateTimer);
    timer_->start();
}

// 析构函数 不需要手动释放reader_，因为它是引用，不负责生命周期
DashboardWidget::~DashboardWidget() {
    // 打印日志
    GetLogger("gateway_monitor")->info("DashboardWidget destroyed");
    // qt对象树会自动释放子控件和定时器
}

// QTimer::timeout()槽函数，用于轮询共享内存数据
void DashboardWidget::onUpdateTimer() {
    ShmBlock block;
    if (reader_.read(block)) {
        // 更新UI
        cpu_val_->setText(QString::number(block.cpu_usage, 'f', 2) + "%");
        mem_val_->setText(QString::number(block.mem_usage, 'f', 2) + "%");
        online_nodes_val_->setText(QString::number(block.online_nodes));
        total_packets_val_->setText(QString::number(block.total_packets));
        total_alarm_val_->setText(QString::number(block.total_alarms));
        alarm_active_val_->setText(QString::number(block.alarm_active));
    } else {
        // 如果读取失败，显示N/A
        // 打印日志
        GetLogger("gateway_monitor")->error("Failed to read shared memory");
        // 保持不更新
    }
}