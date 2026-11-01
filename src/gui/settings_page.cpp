#include "settings_page.hpp"
#include "controller.hpp"
#include "facts.hpp"
#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
namespace pa
{
SettingsPage::SettingsPage(GuiController& controller, QWidget* parent) : QWidget(parent), controller(controller)
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(16);
	auto* settings = new QGroupBox("指针设置", this);
	auto* form = new QVBoxLayout(settings);
	form->setSpacing(12);
	enabled = new QCheckBox("启用 PointerAccel", settings);
	enabled->setObjectName("enabledSwitch");
	form->addWidget(enabled);
	auto* label = new QLabel("指针速度", settings);
	form->addWidget(label);
	auto* speedRow = new QHBoxLayout;
	speedRow->addWidget(new QLabel("慢", settings));
	speed = new QSlider(Qt::Horizontal, settings);
	speed->setRange(1, 20);
	speed->setTickInterval(1);
	speed->setTickPosition(QSlider::TicksBelow);
	speed->setAccessibleName("指针速度");
	speed->setObjectName("speedSlider");
	speedRow->addWidget(speed, 1);
	speedRow->addWidget(new QLabel("快", settings));
	speedNumber = new QSpinBox(settings);
	speedNumber->setRange(1, 20);
	speedNumber->setSuffix(" / 20");
	speedNumber->setAccessibleName("指针速度档位");
	speedNumber->setObjectName("speedSpinBox");
	label->setBuddy(speedNumber);
	speedRow->addWidget(speedNumber);
	form->addLayout(speedRow);
	epp = new QCheckBox("提高指针精确度（EPP）", settings);
	epp->setObjectName("eppCheckBox");
	form->addWidget(epp);
	auto* hint = new QLabel("更改会保留为草稿。点击“应用”后统一保存，并清除 CLI 的临时启停覆盖。", settings);
	hint->setWordWrap(true);
	form->addWidget(hint);
	layout->addWidget(settings);
	auto* running = new QGroupBox("当前运行状态", this);
	auto* runningLayout = new QVBoxLayout(running);
	status = new Facts(running);
	for (const auto& row : QList<QPair<QString, QString>>{{"state", "运行状态"},
	         {"configured_enabled", "保存的启用设置"},
	         {"loaded", "模块已加载"},
	         {"active", "实际生效"},
	         {"temporary_override", "CLI 临时覆盖"},
	         {"healthy", "健康状态"},
	         {"startup_state", "下次启动"}})
		status->add(row.first, row.second);
	runningLayout->addWidget(status);
	layout->addWidget(running);
	auto* lifetime = new QLabel("关闭面板后，算法和会话检查服务继续运行。", this);
	lifetime->setWordWrap(true);
	layout->addWidget(lifetime);
	layout->addStretch();
	connect(enabled, &QCheckBox::toggled, &controller, &GuiController::setEnabled);
	connect(epp, &QCheckBox::toggled, &controller, &GuiController::setEpp);
	connect(speed, &QSlider::valueChanged, &controller, &GuiController::setPointerSpeed);
	connect(speedNumber, &QSpinBox::valueChanged, &controller, &GuiController::setPointerSpeed);
	connect(&controller,
	    &GuiController::changed,
	    this,
	    [this]
	    {
		    refresh();
	    });
	refresh();
}
void SettingsPage::refresh()
{
	const QSignalBlocker a(enabled), b(epp), c(speed), d(speedNumber);
	const auto& draft = controller.draft();
	enabled->setChecked(draft.enabled);
	epp->setChecked(draft.settings.enhancePointerPrecision);
	speed->setValue(draft.settings.pointerSpeed);
	speedNumber->setValue(draft.settings.pointerSpeed);
	for (QWidget* widget : QList<QWidget*>{enabled, epp, speed, speedNumber})
		widget->setEnabled(controller.valid() && !controller.applying());
	const auto& current = controller.status();
	for (const auto& key : {"configured_enabled", "loaded", "active", "temporary_override"})
		status->set(key, yesNo(current[key]));
	status->set("state", stateName(current["state"]));
	status->set("startup_state", stateName(current["startup_state"]));
	status->set("healthy",
	    current["healthy"].isUndefined() ? "正在检查…"
	    : current["healthy"].toBool()    ? "正常"
	                                     : "需要处理");
}
}
