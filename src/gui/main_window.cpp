#include "main_window.hpp"
#include "controller.hpp"
#include "settings_page.hpp"
#include "devices_page.hpp"
#include "diagnostics_page.hpp"
#include "facts.hpp"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
namespace pa
{
MainWindow::MainWindow(GuiController& controller, QWidget* parent) : QWidget(parent), controller(controller)
{
	setObjectName("mainWindow");
	resize(900, 680);
	setMinimumSize(600, 500);
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(16, 16, 16, 12);
	layout->setSpacing(12);
	auto* header = new QHBoxLayout;
	auto* name = new QLabel("PointerAccel", this);
	auto font = name->font();
	font.setPointSizeF(font.pointSizeF() + 4);
	font.setBold(true);
	name->setFont(font);
	header->addWidget(name);
	header->addStretch();
	progress = new QProgressBar(this);
	progress->setRange(0, 0);
	progress->setTextVisible(false);
	progress->setFixedSize(64, 6);
	progress->setAccessibleName("正在刷新");
	header->addWidget(progress);
	refreshButton = new QPushButton("刷新", this);
	refreshButton->setObjectName("refreshButton");
	header->addWidget(refreshButton);
	layout->addLayout(header);
	summary = new QLabel(this);
	summary->setObjectName("runtimeSummary");
	summary->setTextFormat(Qt::PlainText);
	summary->setWordWrap(true);
	layout->addWidget(summary);
	message = new QLabel(this);
	message->setObjectName("messageLabel");
	message->setWordWrap(true);
	message->setTextFormat(Qt::PlainText);
	message->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	layout->addWidget(message);
	copyPath = new QPushButton("复制配置路径", this);
	layout->addWidget(copyPath, 0, Qt::AlignLeft);
	auto* content = new QHBoxLayout;
	content->setSpacing(16);
	navigation = new QListWidget(this);
	navigation->setObjectName("pageNavigation");
	navigation->setAccessibleName("页面导航");
	navigation->addItems({"设置", "设备", "诊断"});
	navigation->setFixedWidth(100);
	navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	content->addWidget(navigation);
	pages = new QStackedWidget(this);
	for (QWidget* page :
	    QList<QWidget*>{new SettingsPage(controller), new DevicesPage(controller), new DiagnosticsPage(controller)})
	{
		auto* scroll = new QScrollArea(pages);
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		page->setContentsMargins(0, 0, 8, 0);
		scroll->setWidget(page);
		pages->addWidget(scroll);
	}
	content->addWidget(pages, 1);
	layout->addLayout(content, 1);
	auto* line = new QFrame(this);
	line->setFrameShape(QFrame::HLine);
	layout->addWidget(line);
	auto* footer = new QHBoxLayout;
	draftState = new QLabel(this);
	draftState->setObjectName("draftStateLabel");
	draftState->setWordWrap(true);
	footer->addWidget(draftState, 1);
	revertButton = new QPushButton("撤销修改", this);
	revertButton->setObjectName("revertButton");
	applyButton = new QPushButton("应用", this);
	applyButton->setObjectName("applyButton");
	applyButton->setDefault(true);
	footer->addWidget(revertButton);
	footer->addWidget(applyButton);
	layout->addLayout(footer);
	connect(navigation,
	    &QListWidget::currentRowChanged,
	    this,
	    [&controller](int row)
	    {
		    if (row >= 0)
			    controller.setPage(static_cast<Page>(row));
	    });
	connect(refreshButton,
	    &QPushButton::clicked,
	    this,
	    [&controller]
	    {
		    controller.refresh();
	    });
	connect(revertButton, &QPushButton::clicked, &controller, &GuiController::revert);
	connect(applyButton, &QPushButton::clicked, &controller, &GuiController::apply);
	connect(copyPath,
	    &QPushButton::clicked,
	    this,
	    [&controller]
	    {
		    QApplication::clipboard()->setText(controller.configFile());
	    });
	connect(&controller,
	    &GuiController::changed,
	    this,
	    [this]
	    {
		    refresh();
	    });
	connect(&controller,
	    &GuiController::applyFinished,
	    this,
	    [this](bool saved, bool applied)
	    {
		    if (!closeAfterApply)
			    return;
		    closeAfterApply = false;
		    if (saved && applied)
			    QTimer::singleShot(0,
			        this,
			        [this]
			        {
				        close();
			        });
	    });
	refresh();
}
void MainWindow::refresh()
{
	setWindowTitle("PointerAccel" + QString(controller.dirty() ? " *" : ""));
	const QSignalBlocker block(navigation);
	navigation->setCurrentRow(static_cast<int>(controller.page()));
	pages->setCurrentIndex(static_cast<int>(controller.page()));
	const auto& status = controller.status();
	summary->setText(stateName(status["state"]) + "  ·  实际生效：" + yesNo(status["active"]) + "  ·  健康：" +
	                 (status["healthy"].isUndefined() ? "正在检查…"
	                     : status["healthy"].toBool() ? "正常"
	                                                  : "需要处理"));
	QString text;
	if (!controller.configError().isEmpty())
		text = "配置无效，已禁止保存：" + controller.configError() + "\n" + controller.configFile();
	else if (controller.conflict())
		text = "配置已被外部修改，草稿已保留。请撤销修改以重新加载。";
	else
		text = controller.message();
	const auto runtimeError = status["error"].toString();
	if (!runtimeError.isEmpty() && !text.contains(runtimeError))
		text += (text.isEmpty() ? "" : "\n") + runtimeError;
	message->setText(text);
	message->setVisible(!text.isEmpty());
	copyPath->setVisible(!controller.configError().isEmpty());
	progress->setVisible(controller.busy());
	refreshButton->setEnabled(!controller.busy());
	revertButton->setEnabled(!controller.busy() && (controller.dirty() || controller.conflict()));
	applyButton->setEnabled(controller.valid() && !controller.busy() && controller.dirty() && !controller.conflict());
	draftState->setText(controller.conflict() ? "外部配置已变化"
	                    : controller.dirty()  ? "有未应用的修改"
	                                          : "草稿与保存配置一致");
	if (closeDialog)
		closeDialog->findChild<QPushButton*>("applyCloseButton")->setEnabled(applyButton->isEnabled());
}
void MainWindow::showCloseDialog()
{
	if (closeDialog)
	{
		closeDialog->raise();
		closeDialog->activateWindow();
		return;
	}
	closeDialog = new QDialog(this);
	closeDialog->setObjectName("closeDialog");
	closeDialog->setWindowTitle("有未保存的修改");
	closeDialog->setModal(true);
	auto* layout = new QVBoxLayout(closeDialog);
	layout->addWidget(new QLabel("将修改应用后关闭，或放弃本次草稿。", closeDialog));
	auto* buttons = new QDialogButtonBox(closeDialog);
	auto* apply = buttons->addButton("应用并关闭", QDialogButtonBox::AcceptRole);
	apply->setObjectName("applyCloseButton");
	auto* abandon = buttons->addButton("放弃修改", QDialogButtonBox::DestructiveRole);
	abandon->setObjectName("discardCloseButton");
	auto* cancel = buttons->addButton("取消", QDialogButtonBox::RejectRole);
	cancel->setObjectName("cancelCloseButton");
	layout->addWidget(buttons);
	connect(apply,
	    &QPushButton::clicked,
	    this,
	    [this]
	    {
		    closeAfterApply = true;
		    closeDialog->accept();
		    controller.apply();
	    });
	connect(abandon,
	    &QPushButton::clicked,
	    this,
	    [this]
	    {
		    discard = true;
		    closeDialog->reject();
		    close();
	    });
	connect(cancel, &QPushButton::clicked, closeDialog, &QDialog::reject);
	connect(closeDialog,
	    &QDialog::finished,
	    this,
	    [this]
	    {
		    auto* dialog = closeDialog;
		    closeDialog = nullptr;
		    dialog->deleteLater();
	    });
	refresh();
	closeDialog->open();
}
void MainWindow::closeEvent(QCloseEvent* event)
{
	if (!discard && controller.dirty())
	{
		event->ignore();
		if (!controller.applying())
			showCloseDialog();
		return;
	}
	QWidget::closeEvent(event);
}
void MainWindow::updatePolling()
{
	controller.setPollingActive(isVisible() && !isMinimized());
}
void MainWindow::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	updatePolling();
}
void MainWindow::hideEvent(QHideEvent* event)
{
	QWidget::hideEvent(event);
	updatePolling();
}
void MainWindow::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::WindowStateChange)
		updatePolling();
}
}
