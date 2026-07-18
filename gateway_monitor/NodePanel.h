#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include "ShmReader.h"

class NodePanel : public QWidget {
    Q_OBJECT
public:
    explicit NodePanel(ShmReader& reader, QWidget* parent = nullptr);

    void refresh();

private:
    void setup_ui();
    QLabel* make_status_dot(bool online);

    ShmReader& reader_;
    QTableWidget* table_;
    QLabel* ai_status_;
    QLabel* sensor_status_;
    QLabel* mqtt_status_;
};
