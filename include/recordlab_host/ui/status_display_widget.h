#pragma once

#include <QWidget>

class QFrame;
class QLabel;
class QTimer;

namespace recordlab::host::ui {

class FlashValueDisplayWidget : public QWidget {
    Q_OBJECT

public:
    explicit FlashValueDisplayWidget(QString label,
                                     QString default_value,
                                     QString normal_style,
                                     QString flash_style,
                                     QWidget* parent = nullptr);

    QString valueText() const;
    QLabel* valueLabel() const;
    QFrame* valueFrame() const;

public slots:
    void reset();

protected:
    void updateValueText(const QString& value_text);

private slots:
    void toggleFlash();
    void clearIfStale();

private:
    void startFlash();
    void applyNormalStyle();

    QLabel* value_label_ = nullptr;
    QFrame* value_frame_ = nullptr;
    QTimer* flash_timer_ = nullptr;
    QTimer* stale_timer_ = nullptr;
    QString default_value_;
    QString normal_style_;
    QString flash_style_;
    int flash_count_ = 0;
    bool is_flashing_ = false;
};

class RecordTimerDisplayWidget : public FlashValueDisplayWidget {
    Q_OBJECT

public:
    explicit RecordTimerDisplayWidget(QString label = QStringLiteral("录制时长"),
                                      QWidget* parent = nullptr);

public slots:
    void updateTime(double seconds);
};

class TimeDelayDisplayWidget : public FlashValueDisplayWidget {
    Q_OBJECT

public:
    explicit TimeDelayDisplayWidget(QString label = QStringLiteral("时间延迟"),
                                    QWidget* parent = nullptr);

public slots:
    void updateDelay(double delay_ms);
};

}  // namespace recordlab::host::ui
