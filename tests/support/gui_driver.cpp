// Drives the real QWidget controller in an isolated nested compositor, then closes.
#include "controller.hpp"
#include "main_window.hpp"
#include <QApplication>
#include <QDir>
#include <QTimer>

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	pa::GuiController controller;
	pa::MainWindow mainWindow(controller);
	auto* window = &mainWindow;
	window->show();
	bool submitted = false;
	const bool preview = app.arguments().size() == 3 && app.arguments()[1] == "--preview";
	int previewPage = 0;
	bool previewScheduled = false;
	QObject::connect(&controller,
	    &pa::GuiController::changed,
	    &app,
	    [&]
	    {
		    if (preview)
		    {
			    if (!controller.valid() || controller.busy() || previewScheduled)
				    return;
			    previewScheduled = true;
			    controller.setPollingActive(false);
			    QTimer::singleShot(250,
			        &app,
			        [&]
			        {
				        if (controller.busy())
				        {
					        previewScheduled = false;
					        return;
				        }
				        const auto folder = app.arguments()[2];
				        if (!QDir().mkpath(folder) ||
				            !window->grab().save(folder + "/page-" + QString::number(previewPage) + ".png"))
				        {
					        app.exit(5);
					        return;
				        }
				        if (++previewPage == 3)
				        {
					        window->close();
					        app.exit(0);
					        return;
				        }
				        previewScheduled = false;
				        controller.setPage(static_cast<pa::Page>(previewPage));
			        });
			    return;
		    }
		    if (!submitted && controller.valid() && !controller.busy())
		    {
			    submitted = true;
			    controller.setPollingActive(false);
			    controller.setPointerSpeed(15);
			    controller.setEpp(false);
			    controller.apply();
		    }
	    });
	QObject::connect(&controller,
	    &pa::GuiController::applyFinished,
	    &app,
	    [&](bool saved, bool applied)
	    {
		    if (!saved || !applied)
		    {
			    app.exit(2);
			    return;
		    }
		    QTimer::singleShot(0,
		        &app,
		        [&]
		        {
			        window->close();
			        app.exit(window->isVisible() ? 3 : 0);
		        });
	    });
	QTimer::singleShot(15000,
	    &app,
	    [&]
	    {
		    app.exit(4);
	    });
	return app.exec();
}
