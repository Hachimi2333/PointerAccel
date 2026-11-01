#include "controller.hpp"
#include "main_window.hpp"
#include <QApplication>
#include <QIcon>

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	app.setApplicationName("pointeraccel-gui");
	app.setApplicationDisplayName("PointerAccel");
	QIcon icon;
	for (int size : {16, 24, 32, 48, 64, 128, 256, 512})
		icon.addFile(QStringLiteral(":/icons/pointeraccel-%1.png").arg(size), QSize(size, size));
	app.setWindowIcon(icon);
	pa::GuiController controller;
	pa::MainWindow window(controller);
	window.show();
	return app.exec();
}
