#include "devices_page.hpp"
#include "controller.hpp"
#include "device_model.hpp"
#include <QHeaderView>
#include <QLabel>
#include <QTableView>
#include <QVBoxLayout>
namespace pa
{
DevicesPage::DevicesPage(GuiController& controller, QWidget* parent) : QWidget(parent), controller(controller)
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	auto* hint = new QLabel("排除的设备使用系统原生处理。更改需点击底部“应用”。", this);
	hint->setWordWrap(true);
	layout->addWidget(hint);
	message = new QLabel(this);
	message->setTextFormat(Qt::PlainText);
	message->setWordWrap(true);
	layout->addWidget(message);
	model = new DeviceModel(this);
	table = new QTableView(this);
	table->setObjectName("devicesTable");
	table->setAccessibleName("鼠标设备及排除设置");
	table->setModel(model);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::SingleSelection);
	table->setAlternatingRowColors(true);
	table->verticalHeader()->hide();
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(DeviceModel::Name, QHeaderView::Stretch);
	table->horizontalHeader()->setMinimumSectionSize(60);
	table->setMinimumHeight(180);
	layout->addWidget(table, 1);
	connect(model, &DeviceModel::exclusionChanged, &controller, &GuiController::setExcluded);
	connect(&controller,
	    &GuiController::changed,
	    this,
	    [this]
	    {
		    refresh();
	    });
	refresh();
}
void DevicesPage::refresh()
{
	const auto selectedId =
	    model->data(model->index(table->currentIndex().row(), DeviceModel::Id), Qt::DisplayRole).toString();
	model->update(controller.devices(), controller.valid() && !controller.applying());
	for (int row = 0; row < model->rowCount(); ++row)
		if (model->data(model->index(row, DeviceModel::Id), Qt::DisplayRole).toString() == selectedId)
			table->selectRow(row);
	message->setText(!controller.devicesError().isEmpty() ? controller.devicesError()
	                 : model->rowCount() == 0             ? "暂无可显示的设备。"
	                                                      : "");
	message->setVisible(!message->text().isEmpty());
}
}
