#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QDateTimeEdit>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <deque>

struct HistoryEntry {
    int64_t ts;
    QString type;
    QString detail;
    float temp = 0;
    float hum = 0;
};

class HistoryChart : public QFrame {
    Q_OBJECT
public:
    explicit HistoryChart(QWidget* parent = nullptr);
    void set_data(const std::deque<float>& temps, const std::deque<float>& hums);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const std::deque<float>* temps_ = nullptr;
    const std::deque<float>* hums_ = nullptr;
};

class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit HistoryPanel(QWidget* parent = nullptr);

    void append_entry(const HistoryEntry& e);
    void clear();

signals:
    void query_requested(int64_t start_ts, int64_t end_ts);

private slots:
    void on_query();

private:
    void setup_ui();

    QDateTimeEdit* start_edit_;
    QDateTimeEdit* end_edit_;
    QPushButton* query_btn_;
    QTableWidget* table_;
    HistoryChart* chart_;

    std::deque<HistoryEntry> entries_;
    std::deque<float> temp_hist_;
    std::deque<float> hum_hist_;
    static constexpr int kMaxHistory = 300;
};
