#pragma once
#include <kwin/core/inputdevice.h>
#include <QPointer>
#include <functional>
#include <chrono>
#include <map>
namespace pa
{
struct Device
{
	QPointer<KWin::InputDevice> pointer;
	QString sysName, id, name;
	bool eligible = false;
	QMetaObject::Connection replacement;
};

class DeviceRegistry
{
public:
	using MotionHandler = std::function<void(
	    const QPointF&, const QPointF&, std::chrono::microseconds, KWin::InputDevice*, const QString&)>;
	void add(KWin::InputDevice* device);
	bool attach(Device& device, QObject* receiver, const MotionHandler& motion);
	void restore(Device& device);
	std::map<KWin::InputDevice*, Device> entries;
};
}
