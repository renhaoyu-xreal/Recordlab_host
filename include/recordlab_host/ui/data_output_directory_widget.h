#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace recordlab::host::ui {

class DataOutputDirectoryWidget : public QWidget {
    Q_OBJECT

public:
    explicit DataOutputDirectoryWidget(const QString& root_path, QWidget* parent = nullptr);

    void setRootPath(const QString& root_path);
    QString rootPath() const;
    QString titleText() const;

signals:
    void messageReady(const QString& message);
    void titleChanged(const QString& title);

public slots:
    void refresh();
    void openSelectedDirectory();
    void exportSelectedItems();

private:
    void addDirectoryItems(QTreeWidgetItem* parent, const QString& path, int depth);
    QStringList selectedDataPaths() const;

    QString root_path_;
    QTreeWidget* tree_ = nullptr;
    QPushButton* refresh_button_ = nullptr;
    QPushButton* open_button_ = nullptr;
    QPushButton* export_button_ = nullptr;
};

}  // namespace recordlab::host::ui
