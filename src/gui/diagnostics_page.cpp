#include "diagnostics_page.hpp"
#include "controller.hpp"
#include "facts.hpp"
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
namespace pa
{
DiagnosticsPage::DiagnosticsPage(GuiController& controller, QWidget* parent) : QWidget(parent), controller(controller)
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(16);
	auto* health = new QGroupBox("健康与兼容性", this);
	auto* healthLayout = new QVBoxLayout(health);
	compatibility = new Facts(health);
	for (const auto& row : QList<QPair<QString, QString>>{{"running_compatible", "运行模块兼容"},
	         {"startup_compatible", "下次启动兼容"},
	         {"repair_required", "需要修复"},
	         {"relogin_required", "需要重新登录"},
	         {"installed_kwin_version", "已安装 KWin"}})
		compatibility->add(row.first, row.second);
	healthLayout->addWidget(compatibility);
	layout->addWidget(health);
	auto* group = new QGroupBox("输入诊断", this);
	auto* inputLayout = new QVBoxLayout(group);
	input = new Facts(group);
	for (const auto& row : QList<QPair<QString, QString>>{{"processed", "已处理报告"},
	         {"passthrough", "透传报告"},
	         {"batches", "输入批次"},
	         {"last_age_ms", "最近样本距今"},
	         {"device", "样本设备"},
	         {"raw", "原始位移"},
	         {"output", "处理后位移"},
	         {"display", "屏幕缩放 / DPI"},
	         {"trace_enabled", "实际 trace 状态"},
	         {"trace_samples", "trace 样本 / 容量"}})
		input->add(row.first, row.second);
	inputLayout->addWidget(input);
	error = new QLabel(group);
	error->setTextFormat(Qt::PlainText);
	error->setWordWrap(true);
	inputLayout->addWidget(error);
	trace = new QCheckBox("记录逐报告 trace（待应用设置）", group);
	trace->setObjectName("traceCheckBox");
	inputLayout->addWidget(trace);
	auto* hint = new QLabel("trace 仅保存在模块内存，最多 4096 份；关闭记录后清空。关闭 GUI 不会自动关闭记录。", group);
	hint->setWordWrap(true);
	inputLayout->addWidget(hint);
	auto* actions = new QHBoxLayout;
	exportButton = new QPushButton("导出诊断 JSON…", group);
	exportButton->setObjectName("exportButton");
	resetButton = new QPushButton("重置算法状态", group);
	resetButton->setObjectName("resetButton");
	actions->addWidget(exportButton);
	actions->addWidget(resetButton);
	actions->addStretch();
	inputLayout->addLayout(actions);
	layout->addWidget(group);
	auto* maintenance = new QGroupBox("终端维护", this);
	auto* maintenanceLayout = new QVBoxLayout(maintenance);
	auto* commands = new QLabel("pointeraccel doctor\npointeraccel repair", maintenance);
	commands->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	maintenanceLayout->addWidget(commands);
	auto* copy = new QHBoxLayout;
	for (const auto& pair : QList<QPair<QString, QString>>{
	         {"复制诊断命令", "pointeraccel doctor"}, {"复制修复命令", "pointeraccel repair"}})
	{
		auto* button = new QPushButton(pair.first, maintenance);
		copy->addWidget(button);
		connect(button,
		    &QPushButton::clicked,
		    this,
		    [command = pair.second]
		    {
			    QApplication::clipboard()->setText(command);
		    });
	}
	copy->addStretch();
	maintenanceLayout->addLayout(copy);
	auto* repairHint = new QLabel("修复在终端执行；完成后手动重新登录 Plasma。", maintenance);
	repairHint->setWordWrap(true);
	maintenanceLayout->addWidget(repairHint);
	layout->addWidget(maintenance);
	layout->addStretch();
	connect(trace, &QCheckBox::toggled, &controller, &GuiController::setTrace);
	connect(resetButton, &QPushButton::clicked, &controller, &GuiController::resetState);
	connect(exportButton,
	    &QPushButton::clicked,
	    this,
	    [this]
	    {
		    const auto path = QFileDialog::getSaveFileName(
		        this, "导出 PointerAccel 诊断", "pointeraccel-diagnostics.json", "JSON 文件 (*.json)");
		    if (!path.isEmpty())
			    this->controller.exportDiagnostics(QUrl::fromLocalFile(path));
	    });
	connect(&controller,
	    &GuiController::changed,
	    this,
	    [this]
	    {
		    refresh();
	    });
	refresh();
}
void DiagnosticsPage::refresh()
{
	const auto& status = controller.status();
	for (const auto& key : {"running_compatible", "startup_compatible", "repair_required", "relogin_required"})
		compatibility->set(key, yesNo(status[key]));
	compatibility->set("installed_kwin_version", displayText(status["installed_kwin_version"]));
	const auto& diagnostics = controller.diagnostics();
	for (const auto& key : {"processed", "passthrough", "batches"})
		input->set(key, displayText(diagnostics[key]));
	input->set("last_age_ms",
	    diagnostics["last_age_ms"].toInt(-1) >= 0 ? displayText(diagnostics["last_age_ms"]) + " ms" : "尚无输入样本");
	const auto sample = diagnostics["last"].toObject();
	input->set("device", displayText(sample["device"]));
	for (const auto& key : {"raw", "output"})
	{
		const auto values = sample[key].toArray();
		input->set(key, values.size() == 2 ? displayText(values[0]) + ", " + displayText(values[1]) : "—");
	}
	input->set(
	    "display", sample.contains("scale") ? displayText(sample["scale"]) + " / " + displayText(sample["dpi"]) : "—");
	input->set("trace_enabled",
	    diagnostics["trace_enabled"].isUndefined() ? "—"
	    : diagnostics["trace_enabled"].toBool()    ? "记录中"
	                                               : "已关闭");
	input->set("trace_samples",
	    displayText(diagnostics["trace_samples"]) + " / " + displayText(diagnostics["trace_capacity"]));
	error->setText(diagnostics["error"].toString());
	error->setVisible(!error->text().isEmpty());
	const QSignalBlocker block(trace);
	trace->setChecked(controller.draft().trace);
	trace->setEnabled(controller.valid() && !controller.applying());
	exportButton->setEnabled(!controller.busy());
	resetButton->setEnabled(!controller.busy() && status["loaded"].toBool());
}
}
