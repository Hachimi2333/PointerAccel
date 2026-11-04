#include "input_session.hpp"
#include "input/capture.hpp"
#include <kwin/input.h>
#include <kwin/pointer_input.h>
#include <kwin/wayland_server.h>
#include <kwin/wayland/seat.h>
#include <kwin/input_event.h>
namespace pa
{
class ZeroRawFilter final : public KWin::InputEventFilter
{
public:
	explicit ZeroRawFilter(const bool& pending) : InputEventFilter(KWin::InputFilterOrder::Forward), pending(pending)
	{
	}

	bool pointerMotion(KWin::PointerMotionEvent* event) override
	{
		if (!pending || event->warp || !event->delta.isNull() || event->deltaUnaccelerated.isNull())
			return false;
		// KWin 6.7/6.8 skips relativePointerMotion when the accelerated delta is zero.
		// Integer Windows motion can legitimately be zero with nonzero raw counts.
		// This filter sits immediately before Forward, after lock/drag/effect filters.
		auto* seat = KWin::waylandServer()->seat();
		seat->setTimestamp(event->timestamp);
		seat->notifyPointerMotion(event->position);
		seat->relativePointerMotion(event->delta, event->deltaUnaccelerated, event->timestamp);
		return true;
	}

private:
	const bool& pending;
};

InputSession::InputSession() = default;
InputSession::~InputSession() = default;
void InputSession::initialize(
    QObject* receiver, DeviceRegistry::MotionHandler motion, std::function<void(const QString&)> failure)
{
	this->receiver = receiver;
	this->motion = std::move(motion);
	QObject::connect(KWin::input(),
	    &KWin::InputRedirection::deviceAdded,
	    receiver,
	    [this, failure](auto* d)
	    {
		    registry.add(d);
		    if (auto it = registry.entries.find(d); enabled && it != registry.entries.end() && !attach(it->second))
			    failure("新设备的原输入连接无法接管。");
	    });
	QObject::connect(KWin::input(),
	    &KWin::InputRedirection::deviceRemoved,
	    receiver,
	    [this](auto* d)
	    {
		    auto it = registry.entries.find(d);
		    if (it != registry.entries.end())
		    {
			    registry.restore(it->second);
			    registry.entries.erase(it);
		    }
	    });
	for (auto* device : KWin::input()->devices())
		registry.add(device);
}
bool InputSession::attach(Device& device)
{
	return registry.attach(device, receiver, motion);
}
bool InputSession::installCapture(const QString& library, QString* error)
{
	return pa::capture::install(library, error);
}
void InputSession::activate()
{
	zeroRaw = std::make_unique<ZeroRawFilter>(zeroPending);
	KWin::input()->installInputEventFilter(zeroRaw.get());
	pa::capture::arm(true);
	enabled = true;
}
bool InputSession::deactivate(QString* error)
{
	pa::capture::arm(false);
	zeroRaw.reset();
	zeroPending = false;
	for (auto& [_, d] : registry.entries)
		registry.restore(d);
	enabled = false;
	QString why;
	if (!pa::capture::uninstall(&why))
	{
		*error = why;
		return false;
	}
	return true;
}
}
