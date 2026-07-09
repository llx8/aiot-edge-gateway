#pragma once

#include <QWidget>
#include "ShmReader.h"

class QTableWidget;

class AlarmTableWidget : public QWidget {
    Q_OBJECT
public:
    // 构造函数，传入ShmReader引用
    explicit AlarmTableWidget(ShmReader& reader, QWidget* parent = nullptr);

    // 析构函数
    ~AlarmTableWidget();

    // 刷新显示数据
    void refresh();
private:
    ShmReader& reader_; // 引用，不负责生命周期
    QTableWidget* table_; // 显示告警的表格
};