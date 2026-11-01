#pragma once
#include <QWidget>
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QDialog;
namespace pa
{
class GuiController;
class MainWindow : public QWidget
{
public:
	explicit MainWindow(GuiController& controller, QWidget* parent = nullptr);

protected:
	void closeEvent(QCloseEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	void refresh();
	void updatePolling();
	void showCloseDialog();
	GuiController& controller;
	QListWidget* navigation;
	QStackedWidget* pages;
	QLabel *summary, *message, *draftState;
	QPushButton *refreshButton, *revertButton, *applyButton, *copyPath;
	QProgressBar* progress;
	QDialog* closeDialog = nullptr;
	bool closeAfterApply = false, discard = false;
};
}
