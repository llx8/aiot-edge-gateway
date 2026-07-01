#pragma once

#include <QWidget>
#include "ShmReader.h"

class QTableWidget;
class QTimer;

class AlarmTableWidget : public QWidget {
    Q_OBJECT
public:
    // 构造函数，传入ShmReader引用
    explicit AlarmTableWidget(ShmReader& reader, QWidget* parent = nullptr);

    // 析构函数
    ~AlarmTableWidget();
private slots:
    // QTimer::timeout()槽函数，用于轮询共享内存数据
    void onUpdateTimer();
private:
    ShmReader& reader_; // 引用，不负责生命周期
    QTableWidget* table_; // 显示告警的表格
    QTimer* timer_; // 轮询定时器
};