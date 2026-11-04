#pragma once
#include "devices.hpp"
#include "input/report.hpp"
#include <array>
#include <span>
namespace pa
{
class InputBatch
{
public:
	bool collect(DeviceRegistry& registry,
	    const QPointF& delta,
	    const QPointF& raw,
	    std::chrono::microseconds time,
	    const QString& sys,
	    std::array<Device*, PA_REPORT_CAPACITY>& mapped);
	bool overflow() const
	{
		return overflowed;
	}
	std::span<const PaReport> reports() const
	{
		return {buffer.data(), count};
	}

private:
	std::array<PaReport, PA_REPORT_CAPACITY> buffer{};
	std::size_t count = 0;
	bool overflowed = false;
};
}
