#include "MonitorWindow.h"
#include "DashboardWidget.h"
#include "AlarmTableWidget.h"
#include "Logger.h"
#include <QSplitter>
#include <QStatusBar>

// 构造函数，创建ShmReader对象，并传入DashboardWidget和AlarmTableWidget
MonitorWindow::MonitorWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // 创建ShmReader对象， 使用与ShmPublisher相同的key
    reader_ = new ShmReader(0x47574D4D);

    // 创建DashboardWidget和AlarmTableWidget
    dashboard_ = new DashboardWidget(*reader_, this);
    alarm_table_ = new AlarmTableWidget(*reader_, this);

    // 使用QSplitter将两个Widget分割
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(dashboard_);
    splitter->addWidget(alarm_table_);
    
    // 设置拉伸比例
    splitter->setStretchFactor(0, 1); // DashboardWidget占1份
    splitter->setStretchFactor(1, 2); // AlarmTableWidget占2份

    // 设为中心窗口
    setCentralWidget(splitter);

    // 窗口属性
    setWindowTitle("AIoT 边缘网关监控");
    resize(800, 600);

    statusBar()->showMessage("监控运行中...");
}

// 析构函数，销毁ShmReader对象
MonitorWindow::~MonitorWindow() {
    delete reader_; // 释放ShmReader对象
    // qt对象树会自动释放dashboard_和alarm_table_
    GetLogger("gateway_monitor")->info("MonitorWindow destroyed");
}