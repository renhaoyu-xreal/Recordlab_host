#pragma once

#include <QMainWindow>

#include <string>

class QStackedWidget;

namespace recordlab::host::ui {

class EntryPage;
class ImuRuntimeBridge;
class WorkspacePage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(std::string agents_config_path, QWidget* parent = nullptr);

    EntryPage* entryPage() const;
    WorkspacePage* workspacePage() const;

private:
    void loadAgents();

    std::string agents_config_path_;
    QStackedWidget* stack_ = nullptr;
    EntryPage* entry_page_ = nullptr;
    WorkspacePage* workspace_page_ = nullptr;
    ImuRuntimeBridge* runtime_ = nullptr;
};

}  // namespace recordlab::host::ui
