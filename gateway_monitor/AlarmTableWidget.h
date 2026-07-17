#pragma once

#include <QWidget>
#include <QString>
#include <vector>

class QTableWidget;

struct AlarmEntry {
    int id;
    QString time;
    int severity;
    QString detail;
};

class AlarmTableWidget : public QWidget {
    Q_OBJECT
public:
    explicit AlarmTableWidget(QWidget* parent = nullptr);
    ~AlarmTableWidget();

    void setAlarms(const std::vector<AlarmEntry>& alarms);
    void appendAlarms(const std::vector<AlarmEntry>& new_alarms);

private:
    QTableWidget* table_;
    const char* severityLabel(int sev) const;
    const char* severityColor(int sev) const;
};
