// Drives the real QML controller in an isolated nested compositor, then closes.
#include "controller.hpp"
#include <QApplication>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

int main(int argc,char **argv) {
    QApplication app(argc,argv);
    app.setQuitOnLastWindowClosed(false);
    QQuickStyle::setFallbackStyle("Fusion");
    pa::GuiController controller;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"controller",QVariant::fromValue(&controller)}});
    engine.load(QUrl("qrc:/pointeraccel/gui/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    bool submitted=false;
    const bool preview=app.arguments().size()==3 && app.arguments()[1]=="--preview";
    int previewPage=0;
    bool previewScheduled=false;
    QObject::connect(&controller,&pa::GuiController::changed,&app,[&]{
        if (preview) {
            if (!controller.valid() || controller.busy() || previewScheduled) return;
            previewScheduled=true;controller.setPollingActive(false);
            QTimer::singleShot(250,&app,[&]{
                if (controller.busy()) {previewScheduled=false;return;}
                const auto folder=app.arguments()[2];
                if (!QDir().mkpath(folder) || !window->grabWindow().save(folder+"/page-"+QString::number(previewPage)+".png")) {app.exit(5);return;}
                if (++previewPage==3) {window->close();app.exit(0);return;}
                previewScheduled=false;controller.setPage(previewPage);
            });
            return;
        }
        if (!submitted && controller.valid() && !controller.busy()) {
            submitted=true;controller.setPollingActive(false);
            controller.setValue("pointer_speed",15);controller.setValue("enhance_pointer_precision",false);
            controller.apply();
        }
    });
    QObject::connect(&controller,&pa::GuiController::applyFinished,&app,[&](bool saved,bool applied){
        if (!saved || !applied) {app.exit(2);return;}
        QTimer::singleShot(0,&app,[&]{window->close();app.exit(window->isVisible()?3:0);});
    });
    QTimer::singleShot(15000,&app,[&]{app.exit(4);});
    return app.exec();
}
