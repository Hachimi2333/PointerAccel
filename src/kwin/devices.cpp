#include "devices.hpp"
#include <kwin/input.h>
#include <kwin/pointer_input.h>
#include <QCryptographicHash>
#include <libudev.h>
namespace pa
{
void DeviceRegistry::add(KWin::InputDevice* p)
{
	// Libinput devices expose sysName; other backends keep their original connections.
	const auto sys = p->property("sysName").toString();
	if (!p->isPointer() || sys.isEmpty())
		return;
	Device d;
	d.pointer = p;
	d.sysName = sys;
	d.name = p->name();
	QString identity = QString::number(p->vendor()) + ":" + QString::number(p->product()) + ":" + p->name();
	auto* context = udev_new();
	auto* native =
	    context ? udev_device_new_from_subsystem_sysname(context, "input", sys.toLocal8Bit().constData()) : nullptr;
	if (native)
	{
		auto property = [native](const char* key)
		{
			return QString::fromUtf8(udev_device_get_property_value(native, key));
		};
		auto serial = property("ID_SERIAL");
		identity += ":" + (serial.isEmpty() ? property("ID_PATH") : serial);
		d.eligible = property("ID_INPUT_MOUSE") == "1" && property("ID_INPUT_TOUCHPAD") != "1" && !p->isTouchpad() &&
		             !p->isTabletTool() && !p->isTouch() &&
		             !QString::fromUtf8(udev_device_get_syspath(native)).startsWith("/sys/devices/virtual/");
		udev_device_unref(native);
	}
	if (context)
		udev_unref(context);
	d.id = "mouse-" + QString::fromLatin1(
	                      QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
	entries.emplace(p, std::move(d));
}

bool DeviceRegistry::attach(Device& d, QObject* receiver, const MotionHandler& motion)
{
	if (!d.pointer || d.replacement)
		return true;
	const bool removed = QObject::disconnect(d.pointer,
	    &KWin::InputDevice::pointerMotion,
	    KWin::input()->pointer(),
	    &KWin::PointerInputRedirection::processMotion);
	if (!removed)
		return false;
	d.replacement = QObject::connect(d.pointer,
	    &KWin::InputDevice::pointerMotion,
	    receiver,
	    [motion, sys = d.sysName](
	        const QPointF& delta, const QPointF& raw, std::chrono::microseconds time, KWin::InputDevice* source)
	    {
		    motion(delta, raw, time, source, sys);
	    });
	return bool(d.replacement);
}

void DeviceRegistry::restore(Device& d)
{
	if (!d.replacement)
		return;
	QObject::disconnect(d.replacement);
	d.replacement = {};
	if (d.pointer && KWin::input())
		QObject::connect(d.pointer,
		    &KWin::InputDevice::pointerMotion,
		    KWin::input()->pointer(),
		    &KWin::PointerInputRedirection::processMotion,
		    Qt::UniqueConnection);
}

}
