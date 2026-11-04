#pragma once
#include "devices.hpp"
#include <memory>
namespace pa
{
class ZeroRawFilter;
// Owns native input connections and the capture/filter lifecycle, never engine state.
class InputSession
{
public:
	InputSession();
	~InputSession();
	void initialize(
	    QObject* receiver, DeviceRegistry::MotionHandler motion, std::function<void(const QString&)> failure);
	DeviceRegistry& devices()
	{
		return registry;
	}
	const DeviceRegistry& devices() const
	{
		return registry;
	}
	bool active() const
	{
		return enabled;
	}
	bool attach(Device& device);
	bool installCapture(const QString& library, QString* error);
	void activate();
	bool deactivate(QString* error);
	void replayingZero(bool pending)
	{
		zeroPending = pending;
	}

private:
	DeviceRegistry registry;
	QObject* receiver = nullptr;
	DeviceRegistry::MotionHandler motion;
	bool enabled = false, zeroPending = false;
	std::unique_ptr<ZeroRawFilter> zeroRaw;
};
}
