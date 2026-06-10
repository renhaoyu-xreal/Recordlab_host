#include "recordlab_host/ui/log_text_edit.h"

#include <QScrollBar>
#include <QTextCursor>

namespace recordlab::host::ui {

namespace {

QString htmlEscape(QString text) {
    text.replace('&', QStringLiteral("&amp;"));
    text.replace('<', QStringLiteral("&lt;"));
    text.replace('>', QStringLiteral("&gt;"));
    text.replace('"', QStringLiteral("&quot;"));
    text.replace('\n', QStringLiteral("<br/>"));
    return text;
}

QString stripLogTag(QString text) {
    text = text.trimmed();
    if (text.startsWith('[') && text.endsWith(']') && text.size() >= 2) {
        text = text.mid(1, text.size() - 2).trimmed();
    }
    return text;
}

}  // namespace

LogTextEdit::LogTextEdit(QWidget* parent) : QTextEdit(parent) {
    setReadOnly(true);
    setUndoRedoEnabled(false);
    document()->setMaximumBlockCount(2000);
}

void LogTextEdit::appendLogEntry(const QString& message,
                                 const QString& level,
                                 const QString& log_type,
                                 const QDateTime& timestamp) {
    const QString normalized_level = normalizedLevel(level);
    const QString normalized_type = normalizedType(log_type);
    const QString severity_color = levelColor(normalized_level);
    const QString effective_type_color = normalized_level == QStringLiteral("info")
        ? typeColor(normalized_type)
        : severity_color;
    const QString html = QStringLiteral(
        "<div style='margin:2px 0;'>"
        "<span style='color:#6b6b6b;'>[%1]</span> "
        "<span style='color:%2;font-weight:700;'>[%3]</span> "
        "<span style='color:%4;font-weight:700;'>[%5]</span> "
        "<span style='color:%2;'>%6</span>"
        "</div>")
        .arg(htmlEscape(timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))),
             severity_color,
             htmlEscape(levelText(normalized_level)),
             effective_type_color,
             htmlEscape(typeText(normalized_type)),
             htmlEscape(message));
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(html);
    cursor.insertBlock();
    setTextCursor(cursor);
    if (verticalScrollBar()) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }
}

QString LogTextEdit::inferLevel(const QString& message) {
    const QString lowered = message.trimmed().toLower();
    if (message.contains(QStringLiteral("失败"))
        || message.contains(QStringLiteral("错误"))
        || message.contains(QStringLiteral("异常"))
        || lowered.contains(QStringLiteral("failed"))
        || lowered.contains(QStringLiteral("error"))
        || lowered.contains(QStringLiteral("exception"))) {
        return QStringLiteral("error");
    }
    if (message.contains(QStringLiteral("成功"))
        || message.contains(QStringLiteral("已"))
        || message.contains(QStringLiteral("完成"))
        || lowered.contains(QStringLiteral("success"))
        || lowered.contains(QStringLiteral("completed"))) {
        return QStringLiteral("success");
    }
    if (message.contains(QStringLiteral("停止"))
        || message.contains(QStringLiteral("警告"))
        || lowered.contains(QStringLiteral("warning"))) {
        return QStringLiteral("warning");
    }
    return QStringLiteral("info");
}

QString LogTextEdit::normalizedLevel(QString level) {
    level = stripLogTag(level).toLower();
    if (level == QStringLiteral("success")
        || level == QStringLiteral("ok")
        || level == QStringLiteral("passed")
        || level == QStringLiteral("completed")) {
        return QStringLiteral("success");
    }
    if (level == QStringLiteral("warning")
        || level == QStringLiteral("warn")) {
        return QStringLiteral("warning");
    }
    if (level == QStringLiteral("error")
        || level == QStringLiteral("failed")
        || level == QStringLiteral("failure")) {
        return QStringLiteral("error");
    }
    if (level == QStringLiteral("success")
        || level == QStringLiteral("warning")
        || level == QStringLiteral("error")) {
        return level;
    }
    return QStringLiteral("info");
}

QString LogTextEdit::normalizedType(QString log_type) {
    log_type = stripLogTag(log_type).toLower();
    if (log_type.isEmpty()) {
        return QStringLiteral("system");
    }
    return log_type;
}

QString LogTextEdit::levelText(const QString& level) {
    if (level == QStringLiteral("success")) return QStringLiteral("SUCCESS");
    if (level == QStringLiteral("warning")) return QStringLiteral("WARNING");
    if (level == QStringLiteral("error")) return QStringLiteral("ERROR");
    return QStringLiteral("INFO");
}

QString LogTextEdit::typeText(const QString& log_type) {
    if (log_type == QStringLiteral("command")) return QStringLiteral("COMMAND");
    if (log_type == QStringLiteral("agent")) return QStringLiteral("AGENT");
    if (log_type == QStringLiteral("script")) return QStringLiteral("SCRIPT");
    if (log_type == QStringLiteral("watchdog")) return QStringLiteral("WATCHDOG");
    if (log_type == QStringLiteral("data")) return QStringLiteral("DATA");
    if (log_type == QStringLiteral("ui")) return QStringLiteral("UI");
    return QStringLiteral("SYSTEM");
}

QString LogTextEdit::levelColor(const QString& level) {
    if (level == QStringLiteral("success")) return QStringLiteral("#1B5E20");
    if (level == QStringLiteral("warning")) return QStringLiteral("#E65100");
    if (level == QStringLiteral("error")) return QStringLiteral("#B71C1C");
    return QStringLiteral("#0D47A1");
}

QString LogTextEdit::typeColor(const QString& log_type) {
    if (log_type == QStringLiteral("command")) return QStringLiteral("#1565C0");
    if (log_type == QStringLiteral("agent")) return QStringLiteral("#455A64");
    if (log_type == QStringLiteral("script")) return QStringLiteral("#6A1B9A");
    if (log_type == QStringLiteral("watchdog")) return QStringLiteral("#00838F");
    if (log_type == QStringLiteral("data")) return QStringLiteral("#2E7D32");
    if (log_type == QStringLiteral("ui")) return QStringLiteral("#AD1457");
    return QStringLiteral("#5D4037");
}

}  // namespace recordlab::host::ui
