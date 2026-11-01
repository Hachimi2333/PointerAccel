#pragma once
#include <QWidget>
class QLabel;
class QTableView;
namespace pa
{
class GuiController;
class DeviceModel;
class DevicesPage : public QWidget
{
public:
	explicit DevicesPage(GuiController& controller, QWidget* parent = nullptr);

private:
	void refresh();
	GuiController& controller;
	DeviceModel* model;
	QTableView* table;
	QLabel* message;
};
}
