#pragma once
#include <QWidget>
class QCheckBox;
class QLabel;
class QPushButton;
namespace pa
{
class GuiController;
class Facts;
class DiagnosticsPage : public QWidget
{
public:
	explicit DiagnosticsPage(GuiController& controller, QWidget* parent = nullptr);

private:
	void refresh();
	GuiController& controller;
	Facts *compatibility, *input;
	QCheckBox* trace;
	QLabel* error;
	QPushButton *exportButton, *resetButton;
};
}
