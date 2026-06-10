#pragma once

#include <QDateTime>
#include <QTextEdit>

namespace recordlab::host::ui {

class LogTextEdit : public QTextEdit {
    Q_OBJECT

public:
    explicit LogTextEdit(QWidget* parent = nullptr);

    void appendLogEntry(const QString& message,
                        const QString& level = QStringLiteral("info"),
                        const QString& log_type = QStringLiteral("system"),
                        const QDateTime& timestamp = QDateTime::currentDateTime());

    static QString inferLevel(const QString& message);

private:
    static QString normalizedLevel(QString level);
    static QString normalizedType(QString log_type);
    static QString levelText(const QString& level);
    static QString typeText(const QString& log_type);
    static QString levelColor(const QString& level);
    static QString typeColor(const QString& log_type);
};

}  // namespace recordlab::host::ui
