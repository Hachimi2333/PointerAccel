#include "controller.hpp"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char **argv) {
    QApplication app(argc,argv);
    app.setApplicationName("pointeraccel-gui");
    app.setApplicationDisplayName("PointerAccel");
    QQuickStyle::setFallbackStyle("Fusion");
    pa::GuiController controller;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"controller",QVariant::fromValue(&controller)}});
    QObject::connect(&engine,&QQmlApplicationEngine::objectCreationFailed,&app,[]{QCoreApplication::exit(1);},Qt::QueuedConnection);
    engine.load(QUrl("qrc:/pointeraccel/gui/Main.qml"));
    return app.exec();
}
