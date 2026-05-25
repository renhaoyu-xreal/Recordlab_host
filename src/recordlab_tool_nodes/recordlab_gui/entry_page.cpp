#include "recordlab_tool_nodes/recordlab_gui/entry_page.h"

#include <QFont>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace recordlab::nodes::gui {
namespace {

QString buttonText(const GuiAgentConfig &agent) {
  const QString node = QString::fromStdString(agent.node);
  return QStringLiteral("%1\n(%2)").arg(node.toUpper(), node);
}

}  // namespace

EntryPage::EntryPage(const GuiConfig &config, std::function<void(std::string)> on_agent_selected,
                     QWidget *parent)
    : QWidget(parent), on_agent_selected_(std::move(on_agent_selected)) {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(24, 24, 24, 24);
  root->setSpacing(14);
  root->setAlignment(Qt::AlignCenter);
  root->addStretch(1);

  auto *title = new QLabel(QStringLiteral("RecordLabC 控制中心"), this);
  QFont title_font = title->font();
  title_font.setPointSize(28);
  title_font.setBold(true);
  title->setFont(title_font);
  title->setProperty("role", QStringLiteral("heroTitle"));
  title->setAlignment(Qt::AlignCenter);
  root->addWidget(title);

  auto *subtitle = new QLabel(QStringLiteral("选择主 Agent"), this);
  subtitle->setProperty("role", QStringLiteral("heroSubtitle"));
  subtitle->setAlignment(Qt::AlignCenter);
  root->addWidget(subtitle);

  button_grid_ = new QGridLayout();
  button_grid_->setHorizontalSpacing(40);
  button_grid_->setVerticalSpacing(40);
  root->addLayout(button_grid_);
  root->addStretch(1);

  rebuildButtons(config);
}

void EntryPage::rebuildButtons(const GuiConfig &config) {
  int index = 0;
  for (const auto &agent : config.primary_agents) {
    auto *button = new QPushButton(buttonText(agent), this);
    button->setMinimumSize(250, 120);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QFont font = button->font();
    font.setPointSize(16);
    button->setFont(font);
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
    const std::string node = agent.node;
    QObject::connect(button, &QPushButton::clicked, this, [this, node]() {
      if (on_agent_selected_) on_agent_selected_(node);
    });
    button_grid_->addWidget(button, index / 3, index % 3);
    ++index;
  }
}

}  // namespace recordlab::nodes::gui
