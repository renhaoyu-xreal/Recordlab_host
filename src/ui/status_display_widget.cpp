#include "recordlab_host/ui/status_display_widget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QFont>

namespace recordlab::host::ui {

namespace {

constexpr int kFlashIntervalMs = 200;
constexpr int kFlashToggleCount = 4;
constexpr int kStaleTimeoutMs = 5000;

}  // namespace

FlashValueDisplayWidget::FlashValueDisplayWidget(QString label,
                                                 QString default_value,
                                                 QString normal_style,
                                                 QString flash_style,
                                                 QWidget* parent)
    : QWidget(parent),
      default_value_(std::move(default_value)),
      normal_style_(std::move(normal_style)),
      flash_style_(std::move(flash_style)) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 5, 8, 5);
    layout->setSpacing(10);

    auto* label_widget = new QLabel(std::move(label), this);
    QFont label_font(QStringLiteral("Arial"), 10, QFont::Bold);
    label_widget->setFont(label_font);
    label_widget->setStyleSheet(QStringLiteral("color: #333;"));
    layout->addWidget(label_widget);

    value_frame_ = new QFrame(this);
    applyNormalStyle();
    auto* value_layout = new QHBoxLayout(value_frame_);
    value_layout->setContentsMargins(5, 2, 5, 2);

    value_label_ = new QLabel(default_value_, value_frame_);
    value_label_->setAlignment(Qt::AlignCenter);
    value_label_->setStyleSheet(QStringLiteral("color: #000;"));
    QFont value_font(QStringLiteral("Courier"), 14, QFont::Bold);
    value_label_->setFont(value_font);
    value_layout->addWidget(value_label_);

    layout->addWidget(value_frame_);
    layout->addStretch();

    flash_timer_ = new QTimer(this);
    connect(flash_timer_, &QTimer::timeout, this, &FlashValueDisplayWidget::toggleFlash);

    stale_timer_ = new QTimer(this);
    stale_timer_->setSingleShot(true);
    connect(stale_timer_, &QTimer::timeout, this, &FlashValueDisplayWidget::clearIfStale);
}

QString FlashValueDisplayWidget::valueText() const {
    return value_label_ ? value_label_->text() : QString{};
}

void FlashValueDisplayWidget::reset() {
    if (flash_timer_) {
        flash_timer_->stop();
    }
    if (stale_timer_) {
        stale_timer_->stop();
    }
    is_flashing_ = false;
    flash_count_ = 0;
    if (value_label_) {
        value_label_->setText(default_value_);
    }
    applyNormalStyle();
}

void FlashValueDisplayWidget::updateValueText(const QString& value_text) {
    if (!value_label_) {
        return;
    }
    value_label_->setText(value_text);
    if (stale_timer_) {
        stale_timer_->start(kStaleTimeoutMs);
    }
    startFlash();
}

QLabel* FlashValueDisplayWidget::valueLabel() const {
    return value_label_;
}

QFrame* FlashValueDisplayWidget::valueFrame() const {
    return value_frame_;
}

void FlashValueDisplayWidget::toggleFlash() {
    if (!value_frame_) {
        return;
    }
    if (flash_count_ % 2 == 0) {
        value_frame_->setStyleSheet(flash_style_);
    } else {
        applyNormalStyle();
    }
    ++flash_count_;
    if (flash_count_ >= kFlashToggleCount) {
        flash_timer_->stop();
        is_flashing_ = false;
        flash_count_ = 0;
        applyNormalStyle();
    }
}

void FlashValueDisplayWidget::clearIfStale() {
    reset();
}

void FlashValueDisplayWidget::startFlash() {
    if (flash_timer_) {
        flash_timer_->stop();
    }
    is_flashing_ = true;
    flash_count_ = 0;
    if (value_frame_) {
        value_frame_->setStyleSheet(flash_style_);
    }
    flash_timer_->start(kFlashIntervalMs);
}

void FlashValueDisplayWidget::applyNormalStyle() {
    if (value_frame_) {
        value_frame_->setStyleSheet(normal_style_);
    }
}

RecordTimerDisplayWidget::RecordTimerDisplayWidget(QString label, QWidget* parent)
    : FlashValueDisplayWidget(
          std::move(label),
          QStringLiteral("00:00.000"),
          QStringLiteral(
              "QFrame {"
              " background-color: #90EE90;"
              " border: 2px solid #006400;"
              " border-radius: 5px;"
              " padding: 5px 15px;"
              "}"),
          QStringLiteral(
              "QFrame {"
              " background-color: #FFFF00;"
              " border: 2px solid #FFD700;"
              " border-radius: 5px;"
              " padding: 5px 15px;"
              "}"),
          parent) {}

void RecordTimerDisplayWidget::updateTime(double seconds) {
    const int minutes = static_cast<int>(seconds / 60.0);
    const double secs = seconds - static_cast<double>(minutes) * 60.0;
    updateValueText(QStringLiteral("%1:%2")
                        .arg(minutes, 2, 10, QChar('0'))
                        .arg(secs, 6, 'f', 3, QChar('0')));
}

TimeDelayDisplayWidget::TimeDelayDisplayWidget(QString label, QWidget* parent)
    : FlashValueDisplayWidget(
          std::move(label),
          QStringLiteral("0.000 ms"),
          QStringLiteral(
              "QFrame {"
              " background-color: #87CEEB;"
              " border: 2px solid #4682B4;"
              " border-radius: 5px;"
              " padding: 5px 15px;"
              "}"),
          QStringLiteral(
              "QFrame {"
              " background-color: #FFD700;"
              " border: 2px solid #FFA500;"
              " border-radius: 5px;"
              " padding: 5px 15px;"
              "}"),
          parent) {}

void TimeDelayDisplayWidget::updateDelay(double delay_ms) {
    updateValueText(QStringLiteral("%1 ms").arg(delay_ms, 0, 'f', 3));
}

}  // namespace recordlab::host::ui
