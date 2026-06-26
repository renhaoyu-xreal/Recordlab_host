#include "recordlab_host/ui/virtual_nodes_page.h"

#include <QCheckBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace recordlab::host::ui {
namespace {

QSettings makeVirtualNodeSettings() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope,
                     QStringLiteral("RecordLab"), QStringLiteral("RecordLabHost"));
}

constexpr const char* kDurationKey = "virtual_nodes/ur_node/trajectory_duration_s";
constexpr const char* kFileSizeKey = "virtual_nodes/ur_node/trajectory_file_size_mib";
constexpr const char* kReturnRateKey = "virtual_nodes/ur_node/trajectory_return_rate_mib_per_s";

}  // namespace

VirtualNodesPage::VirtualNodesPage(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("virtual_nodes_page"));

    auto* root_layout = new QHBoxLayout(this);
    root_layout->setContentsMargins(8, 8, 8, 8);
    root_layout->setSpacing(12);

    auto* list_group = new QFrame(this);
    list_group->setObjectName(QStringLiteral("virtual_nodes_list_group"));
    list_group->setMinimumWidth(300);
    list_group->setStyleSheet(QStringLiteral(
        "QFrame#virtual_nodes_list_group { background: #fffdf6; border: 1px solid #c9c0ad; border-radius: 8px; }"));
    auto* list_layout = new QVBoxLayout(list_group);
    list_layout->setContentsMargins(10, 10, 10, 10);
    list_layout->setSpacing(10);

    auto* list_title = new QLabel(QStringLiteral("虚拟节点列表"), list_group);
    QFont title_font = list_title->font();
    title_font.setPointSize(13);
    title_font.setBold(true);
    list_title->setFont(title_font);
    list_layout->addWidget(list_title);

    node_list_ = new QListWidget(list_group);
    node_list_->setObjectName(QStringLiteral("virtual_nodes_list"));
    node_list_->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { border: none; padding: 2px; }"));
    list_layout->addWidget(node_list_, 1);

    auto* item = new QListWidgetItem(node_list_);
    item->setSizeHint(QSize(0, 72));
    auto* row_widget = new QFrame(node_list_);
    row_widget->setObjectName(QStringLiteral("virtual_node_row_UR_node"));
    row_widget->setStyleSheet(QStringLiteral(
        "QFrame#virtual_node_row_UR_node { background: #f4f1ea; border: 1px solid #d0c6b3; border-radius: 8px; }"));
    auto* row_layout = new QHBoxLayout(row_widget);
    row_layout->setContentsMargins(12, 10, 12, 10);
    row_layout->setSpacing(10);

    auto* name_layout = new QVBoxLayout();
    auto* name_label = new QLabel(QStringLiteral("UR_node"), row_widget);
    name_label->setObjectName(QStringLiteral("virtual_node_name_UR_node"));
    QFont name_font = name_label->font();
    name_font.setBold(true);
    name_label->setFont(name_font);
    ur_status_label_ = new QLabel(row_widget);
    ur_status_label_->setObjectName(QStringLiteral("virtual_node_status_UR_node"));
    name_layout->addWidget(name_label);
    name_layout->addWidget(ur_status_label_);
    row_layout->addLayout(name_layout, 1);

    ur_toggle_ = new QCheckBox(row_widget);
    ur_toggle_->setObjectName(QStringLiteral("virtual_node_toggle_UR_node"));
    ur_toggle_->setCursor(Qt::PointingHandCursor);
    ur_toggle_->setText(QStringLiteral("启用"));
    ur_toggle_->setStyleSheet(QStringLiteral(R"(
        QCheckBox {
            spacing: 8px;
            color: #3f372c;
            font-weight: 600;
        }
        QCheckBox::indicator {
            width: 44px;
            height: 24px;
            border-radius: 12px;
            background: #d0c8bb;
        }
        QCheckBox::indicator:checked {
            background: #73b66b;
        }
    )"));
    row_layout->addWidget(ur_toggle_, 0, Qt::AlignRight | Qt::AlignVCenter);
    node_list_->addItem(item);
    node_list_->setItemWidget(item, row_widget);
    node_list_->setCurrentItem(item);

    auto* config_group = new QFrame(this);
    config_group->setObjectName(QStringLiteral("virtual_node_config_group"));
    config_group->setStyleSheet(QStringLiteral(
        "QFrame#virtual_node_config_group { background: #fffdf6; border: 1px solid #c9c0ad; border-radius: 8px; }"));
    auto* config_layout = new QVBoxLayout(config_group);
    config_layout->setContentsMargins(14, 14, 14, 14);
    config_layout->setSpacing(12);

    auto* config_title = new QLabel(QStringLiteral("UR_node 配置"), config_group);
    config_title->setObjectName(QStringLiteral("virtual_node_config_title"));
    config_title->setFont(title_font);
    config_layout->addWidget(config_title);

    auto* config_form = new QFormLayout();
    config_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    config_form->setFormAlignment(Qt::AlignTop);
    config_form->setHorizontalSpacing(16);
    config_form->setVerticalSpacing(12);

    trajectory_duration_spinbox_ = new QSpinBox(config_group);
    trajectory_duration_spinbox_->setObjectName(QStringLiteral("virtual_ur_duration_spinbox"));
    trajectory_duration_spinbox_->setRange(1, 3600);
    trajectory_duration_spinbox_->setSuffix(QStringLiteral(" s"));
    config_form->addRow(QStringLiteral("轨迹执行时长"), trajectory_duration_spinbox_);

    trajectory_file_size_spinbox_ = new QSpinBox(config_group);
    trajectory_file_size_spinbox_->setObjectName(QStringLiteral("virtual_ur_file_size_spinbox"));
    trajectory_file_size_spinbox_->setRange(1, 4096);
    trajectory_file_size_spinbox_->setSuffix(QStringLiteral(" MiB"));
    config_form->addRow(QStringLiteral("轨迹文件大小"), trajectory_file_size_spinbox_);

    trajectory_return_rate_spinbox_ = new QSpinBox(config_group);
    trajectory_return_rate_spinbox_->setObjectName(QStringLiteral("virtual_ur_return_rate_spinbox"));
    trajectory_return_rate_spinbox_->setRange(1, 1024);
    trajectory_return_rate_spinbox_->setSuffix(QStringLiteral(" MiB/s"));
    config_form->addRow(QStringLiteral("轨迹回传速率"), trajectory_return_rate_spinbox_);

    config_layout->addLayout(config_form);
    config_layout->addStretch(1);

    root_layout->addWidget(list_group, 0);
    root_layout->addWidget(config_group, 1);

    loadSettings();
    updateStatusText();

    connect(ur_toggle_, &QCheckBox::toggled, this, [this](bool enabled) {
        emit virtualUrToggleRequested(enabled);
    });
    connect(trajectory_duration_spinbox_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        saveDurationSetting(value);
        emit virtualUrSettingsChanged(
            value,
            trajectory_file_size_spinbox_ ? trajectory_file_size_spinbox_->value() : 32,
            trajectory_return_rate_spinbox_ ? trajectory_return_rate_spinbox_->value() : 8);
    });
    connect(trajectory_file_size_spinbox_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        saveFileSizeSetting(value);
        emit virtualUrSettingsChanged(
            trajectory_duration_spinbox_ ? trajectory_duration_spinbox_->value() : 20,
            value,
            trajectory_return_rate_spinbox_ ? trajectory_return_rate_spinbox_->value() : 8);
    });
    connect(trajectory_return_rate_spinbox_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        saveReturnRateSetting(value);
        emit virtualUrSettingsChanged(
            trajectory_duration_spinbox_ ? trajectory_duration_spinbox_->value() : 20,
            trajectory_file_size_spinbox_ ? trajectory_file_size_spinbox_->value() : 32,
            value);
    });
}

bool VirtualNodesPage::isVirtualUrEnabled() const {
    return ur_toggle_ && ur_toggle_->isChecked();
}

int VirtualNodesPage::trajectoryDurationSeconds() const {
    return trajectory_duration_spinbox_ ? trajectory_duration_spinbox_->value() : 20;
}

int VirtualNodesPage::trajectoryFileSizeMiB() const {
    return trajectory_file_size_spinbox_ ? trajectory_file_size_spinbox_->value() : 32;
}

int VirtualNodesPage::trajectoryReturnRateMiBPerS() const {
    return trajectory_return_rate_spinbox_ ? trajectory_return_rate_spinbox_->value() : 8;
}

void VirtualNodesPage::setVirtualUrEnabled(bool enabled) {
    if (!ur_toggle_) {
        return;
    }
    const QSignalBlocker blocker(ur_toggle_);
    ur_toggle_->setChecked(enabled);
    updateStatusText();
}

void VirtualNodesPage::loadSettings() {
    QSettings settings = makeVirtualNodeSettings();
    if (trajectory_duration_spinbox_) {
        trajectory_duration_spinbox_->setValue(settings.value(QString::fromLatin1(kDurationKey), 20).toInt());
    }
    if (trajectory_file_size_spinbox_) {
        trajectory_file_size_spinbox_->setValue(settings.value(QString::fromLatin1(kFileSizeKey), 32).toInt());
    }
    if (trajectory_return_rate_spinbox_) {
        trajectory_return_rate_spinbox_->setValue(settings.value(QString::fromLatin1(kReturnRateKey), 8).toInt());
    }
    setVirtualUrEnabled(false);
}

void VirtualNodesPage::saveDurationSetting(int value) {
    QSettings settings = makeVirtualNodeSettings();
    settings.setValue(QString::fromLatin1(kDurationKey), value);
}

void VirtualNodesPage::saveFileSizeSetting(int value) {
    QSettings settings = makeVirtualNodeSettings();
    settings.setValue(QString::fromLatin1(kFileSizeKey), value);
}

void VirtualNodesPage::saveReturnRateSetting(int value) {
    QSettings settings = makeVirtualNodeSettings();
    settings.setValue(QString::fromLatin1(kReturnRateKey), value);
}

void VirtualNodesPage::updateStatusText() {
    if (!ur_status_label_) {
        return;
    }
    const bool enabled = ur_toggle_ && ur_toggle_->isChecked();
    ur_status_label_->setText(enabled ? QStringLiteral("已启用") : QStringLiteral("已关闭"));
    ur_status_label_->setStyleSheet(enabled
        ? QStringLiteral("QLabel { color: #2d7d32; font-weight: 600; }")
        : QStringLiteral("QLabel { color: #8a4a3a; font-weight: 600; }"));
}

}  // namespace recordlab::host::ui
