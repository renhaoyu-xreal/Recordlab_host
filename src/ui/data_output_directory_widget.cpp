#include "recordlab_host/ui/data_output_directory_widget.h"

#include <QAbstractItemView>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

namespace recordlab::host::ui {

namespace {

QString uniqueExportPath(const QString& path) {
    if (!QFileInfo::exists(path)) {
        return path;
    }

    const QFileInfo info(path);
    const QDir parent = info.dir();
    const QString stem = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QString() : QStringLiteral(".") + info.suffix();
    QString candidate = parent.filePath(stem + QStringLiteral("_copy") + suffix);
    int counter = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = parent.filePath(QStringLiteral("%1_copy%2%3").arg(stem).arg(counter).arg(suffix));
        ++counter;
    }
    return candidate;
}

bool copyPathRecursively(const QString& source_path, const QString& target_path, QString* error_message) {
    const QFileInfo source_info(source_path);
    if (!source_info.exists()) {
        if (error_message) *error_message = QStringLiteral("源路径不存在: %1").arg(source_path);
        return false;
    }

    if (source_info.isDir()) {
        QDir target_dir(target_path);
        if (!target_dir.exists() && !QDir().mkpath(target_path)) {
            if (error_message) *error_message = QStringLiteral("无法创建目录: %1").arg(target_path);
            return false;
        }
        const QDir source_dir(source_path);
        const auto entries = source_dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
        for (const auto& entry : entries) {
            if (!copyPathRecursively(entry.absoluteFilePath(), target_dir.filePath(entry.fileName()), error_message)) {
                return false;
            }
        }
        return true;
    }

    QDir().mkpath(QFileInfo(target_path).dir().absolutePath());
    if (!QFile::copy(source_path, target_path)) {
        if (error_message) *error_message = QStringLiteral("复制失败: %1 -> %2").arg(source_path, target_path);
        return false;
    }
    return true;
}

}  // namespace

DataOutputDirectoryWidget::DataOutputDirectoryWidget(const QString& root_path, QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("data_output_directory_widget"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("data_output_tree"));
    tree_->setColumnCount(4);
    tree_->setHeaderLabels({
        QStringLiteral("名称"),
        QStringLiteral("大小"),
        QStringLiteral("类型"),
        QStringLiteral("修改时间"),
    });
    tree_->setAlternatingRowColors(true);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->header()->setStretchLastSection(false);
    tree_->setColumnWidth(0, 260);
    layout->addWidget(tree_, 1);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    refresh_button_ = new QPushButton(QStringLiteral("刷新目录"), this);
    refresh_button_->setObjectName(QStringLiteral("refresh_data_output_button"));
    open_button_ = new QPushButton(QStringLiteral("打开所在目录"), this);
    open_button_->setObjectName(QStringLiteral("open_data_output_button"));
    export_button_ = new QPushButton(QStringLiteral("导出选中项"), this);
    export_button_->setObjectName(QStringLiteral("export_data_output_button"));
    for (auto* button : {refresh_button_, open_button_, export_button_}) {
        button->setMinimumHeight(34);
        buttons->addWidget(button);
    }
    layout->addLayout(buttons);

    connect(refresh_button_, &QPushButton::clicked, this, &DataOutputDirectoryWidget::refresh);
    connect(open_button_, &QPushButton::clicked, this, &DataOutputDirectoryWidget::openSelectedDirectory);
    connect(export_button_, &QPushButton::clicked, this, &DataOutputDirectoryWidget::exportSelectedItems);

    setRootPath(root_path);
}

void DataOutputDirectoryWidget::setRootPath(const QString& root_path) {
    root_path_ = QDir::cleanPath(root_path);
    QDir().mkpath(root_path_);
    emit titleChanged(titleText());
    if (tree_) {
        tree_->clear();
        addDirectoryItems(nullptr, root_path_, 0);
        tree_->sortItems(3, Qt::DescendingOrder);
        tree_->expandToDepth(0);
    }
}

QString DataOutputDirectoryWidget::rootPath() const {
    return root_path_;
}

QString DataOutputDirectoryWidget::titleText() const {
    return QStringLiteral("data输出目录：%1").arg(root_path_);
}

void DataOutputDirectoryWidget::refresh() {
    setRootPath(root_path_);
    emit messageReady(QStringLiteral("已刷新 data 输出目录: %1").arg(root_path_));
}

QStringList DataOutputDirectoryWidget::selectedDataPaths() const {
    QStringList paths;
    if (!tree_) return paths;

    const QDir root_dir(root_path_);
    const auto selected_items = tree_->selectedItems();
    for (const auto* item : selected_items) {
        const QString path = item->data(0, Qt::UserRole).toString();
        const QString relative = root_dir.relativeFilePath(path);
        if (path.isEmpty() || relative == QStringLiteral(".") || relative.startsWith(QStringLiteral(".."))) {
            continue;
        }
        if (!paths.contains(path) && QFileInfo::exists(path)) paths << path;
    }
    return paths;
}

void DataOutputDirectoryWidget::addDirectoryItems(QTreeWidgetItem* parent, const QString& path, int depth) {
    if (!tree_ || depth > 3) return;
    const QDir dir(path);
    const auto entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::DirsFirst | QDir::Time);
    for (const auto& info : entries) {
        const QString size = info.isDir() ? QStringLiteral("--") : QString::number(info.size());
        const QString type = info.isDir() ? QStringLiteral("目录") : info.suffix().isEmpty() ? QStringLiteral("文件") : info.suffix();
        auto* item = parent
            ? new QTreeWidgetItem(parent)
            : new QTreeWidgetItem(tree_);
        item->setText(0, info.fileName());
        item->setText(1, size);
        item->setText(2, type);
        item->setText(3, info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        item->setData(0, Qt::UserRole, info.absoluteFilePath());
        if (info.isDir()) {
            addDirectoryItems(item, info.absoluteFilePath(), depth + 1);
        }
    }
}

void DataOutputDirectoryWidget::openSelectedDirectory() {
    const QStringList paths = selectedDataPaths();
    QString directory = root_path_;
    if (!paths.isEmpty()) {
        const QFileInfo info(paths.first());
        directory = info.isDir() ? info.absoluteFilePath() : info.dir().absolutePath();
    }
    QDir().mkpath(directory);
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
        emit messageReady(QStringLiteral("已打开目录: %1").arg(directory));
        return;
    }
    const QString message = QStringLiteral("无法打开目录: %1").arg(directory);
    emit messageReady(message);
    QMessageBox::warning(this, QStringLiteral("打开失败"), message);
}

void DataOutputDirectoryWidget::exportSelectedItems() {
    const QStringList paths = selectedDataPaths();
    if (paths.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("未选择 data 项"),
                                 QStringLiteral("请先在 data 输出目录中选择要导出的文件或文件夹"));
        return;
    }

    const QString target_dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择导出目标目录"));
    if (target_dir.isEmpty()) return;

    int copied = 0;
    for (const auto& source_path : paths) {
        const QFileInfo source_info(source_path);
        const QString target_path = uniqueExportPath(QDir(target_dir).filePath(source_info.fileName()));
        QString error_message;
        if (!copyPathRecursively(source_path, target_path, &error_message)) {
            emit messageReady(QStringLiteral("导出 data 文件失败: %1").arg(error_message));
            QMessageBox::warning(this, QStringLiteral("导出失败"), error_message);
            return;
        }
        ++copied;
    }
    emit messageReady(QStringLiteral("已导出 %1 个 data 项到: %2").arg(copied).arg(target_dir));
}

}  // namespace recordlab::host::ui
