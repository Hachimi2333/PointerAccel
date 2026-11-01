#pragma once
#include <QWidget>
class QCheckBox;
class QSlider;
class QSpinBox;
namespace pa
{
class GuiController;
class Facts;
class SettingsPage : public QWidget
{
public:
	explicit SettingsPage(GuiController& controller, QWidget* parent = nullptr);

private:
	void refresh();
	GuiController& controller;
	QCheckBox *enabled, *epp;
	QSlider* speed;
	QSpinBox* speedNumber;
	Facts* status;
};
}
