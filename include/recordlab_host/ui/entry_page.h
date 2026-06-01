#pragma once

#include <QWidget>

#include <string>
#include <vector>

class QGridLayout;
class QLabel;

namespace recordlab::host::ui {

class EntryPage : public QWidget {
    Q_OBJECT

public:
    explicit EntryPage(QWidget* parent = nullptr);

    void setAgents(const std::vector<std::string>& primary_agents);
    QLabel* summaryLabel() const;

signals:
    void agentSelected(const QString& agent_name);

private:
    void rebuildButtons();

    std::vector<std::string> primary_agents_;
    QGridLayout* button_grid_ = nullptr;
    QLabel* summary_label_ = nullptr;
};

}  // namespace recordlab::host::ui
