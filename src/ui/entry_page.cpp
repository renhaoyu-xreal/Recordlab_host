#include "recordlab_host/ui/entry_page.h"

#include <QFont>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace recordlab::host::ui {

EntryPage::EntryPage(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("entry_page"));
    setStyleSheet(QStringLiteral(R"(
        EntryPage { background: #f4f1ea; }
        QLabel[role="heroTitle"] { color: #1b2514; }
        QLabel[role="heroSubtitle"] { color: #4f5d47; font-size: 18px; }
    )"));

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(24, 24, 24, 24);
    root_layout->setSpacing(14);
    root_layout->setAlignment(Qt::AlignCenter);
    root_layout->addStretch(1);

    auto* title_label = new QLabel(QStringLiteral("RecordLabC 控制中心"), this);
    title_label->setObjectName(QStringLiteral("entry_title"));
    QFont title_font = title_label->font();
    title_font.setPointSize(28);
    title_font.setBold(true);
    title_label->setFont(title_font);
    title_label->setProperty("role", QStringLiteral("heroTitle"));
    title_label->setAlignment(Qt::AlignCenter);
    root_layout->addWidget(title_label);

    auto* subtitle_label = new QLabel(QStringLiteral("选择主 Agent"), this);
    subtitle_label->setObjectName(QStringLiteral("entry_subtitle"));
    subtitle_label->setProperty("role", QStringLiteral("heroSubtitle"));
    subtitle_label->setAlignment(Qt::AlignCenter);
    root_layout->addWidget(subtitle_label);

    summary_label_ = new QLabel(this);
    summary_label_->setObjectName(QStringLiteral("entry_summary"));
    summary_label_->hide();

    button_grid_ = new QGridLayout();
    button_grid_->setHorizontalSpacing(40);
    button_grid_->setVerticalSpacing(40);
    root_layout->addLayout(button_grid_);
    root_layout->addStretch(1);
}

void EntryPage::setAgents(const std::vector<std::string>& primary_agents) {
    primary_agents_ = primary_agents;
    rebuildButtons();
}

QLabel* EntryPage::summaryLabel() const {
    return summary_label_;
}

void EntryPage::rebuildButtons() {
    while (button_grid_->count() > 0) {
        auto* item = button_grid_->takeAt(0);
        if (auto* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    if (primary_agents_.empty()) {
        auto* empty_label = new QLabel(QStringLiteral("当前未配置主 Agent。"), this);
        empty_label->setObjectName(QStringLiteral("entry_empty_label"));
        empty_label->setAlignment(Qt::AlignCenter);
        button_grid_->addWidget(empty_label, 0, 0);
        return;
    }

    int index = 0;
    for (const auto& agent : primary_agents_) {
        const QString agent_name = QString::fromStdString(agent);
        const int row = index / 3;
        const int column = index % 3;

        auto* button = new QPushButton(
            QStringLiteral("%1\n(%2)").arg(agent_name.toUpper(), agent_name),
            this);
        button->setObjectName(QStringLiteral("agent_button_%1").arg(agent_name));
        button->setMinimumSize(250, 120);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        QFont button_font = button->font();
        button_font.setPointSize(16);
        button->setFont(button_font);
        button->setStyleSheet(QStringLiteral(R"(
            QPushButton {
                background: #90EE90;
                border: 3px solid #006400;
                border-radius: 15px;
                color: #1b2514;
                text-align: center;
                padding: 14px;
            }
            QPushButton:hover { background: #7CFC00; }
            QPushButton:pressed { background: #82d46f; }
        )"));
        connect(button, &QPushButton::clicked, this, [this, agent_name]() {
            emit agentSelected(agent_name);
        });
        button_grid_->addWidget(button, row, column);
        ++index;
    }
}

}  // namespace recordlab::host::ui
