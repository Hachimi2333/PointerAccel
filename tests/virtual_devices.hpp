#pragma once
#include <kwin/core/inputbackend.h>
#include <kwin/core/inputdevice.h>
#include <kwin/input_event_spy.h>
#include <kwin/input_event.h>
#include <QJsonArray>
#include <QJsonObject>
// This fixture is compiled into the isolated test module only.
class TestMouse : public KWin::InputDevice {
public:
    TestMouse(QString sys,bool pad):sys(std::move(sys)),pad(pad) {setProperty("sysName",this->sys);}
    QString name() const override {return sys;}
    bool isEnabled() const override {return true;}
    void setEnabled(bool) override {}
    bool isKeyboard() const override {return false;}
    bool isPointer() const override {return true;}
    bool isTouchpad() const override {return pad;}
    bool isTouch() const override {return false;}
    bool isTabletTool() const override {return false;}
    bool isTabletPad() const override {return false;}
    bool isTabletModeSwitch() const override {return false;}
    bool isLidSwitch() const override {return false;}
    QString sys;bool pad;
};
class TestBackend : public KWin::InputBackend {
public:
    TestBackend() {for (int i=0;i<3;++i) mice.push_back(new TestMouse(QString("pa-test-%1").arg(i),i==2));}
    ~TestBackend() override {qDeleteAll(mice);}
    void initialize() override {for (auto *m:mice) Q_EMIT deviceAdded(m);}
    QList<TestMouse *> mice;
};
class MotionSpy : public KWin::InputEventSpy {
public:
    void pointerMotion(KWin::PointerMotionEvent *e) override {
        samples.append(QJsonObject{{"dx",e->delta.x()},{"dy",e->delta.y()},
            {"raw_x",e->deltaUnaccelerated.x()},{"raw_y",e->deltaUnaccelerated.y()},
            {"device",e->device?e->device->name():QString()}, {"time",static_cast<double>(e->timestamp.count())}});
    }
    QJsonArray samples;
};
