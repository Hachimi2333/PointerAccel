#include "input_batch.hpp"
#include "input/capture.hpp"
#include "input/validation.hpp"
namespace pa
{
bool InputBatch::collect(DeviceRegistry& registry,
    const QPointF& delta,
    const QPointF& raw,
    std::chrono::microseconds time,
    const QString& sys,
    std::array<Device*, PA_REPORT_CAPACITY>& mapped)
{
	overflowed = false;
	count = pa::capture::drain(buffer.data(), buffer.size(), &overflowed);
	auto batch = std::span(buffer.data(), count);
	bool valid =
	    !overflowed &&
	    pa::batchMatches(batch, delta.x(), delta.y(), raw.x(), raw.y(), time.count(), sys.toLocal8Bit().constData());
	if (valid)
		for (std::size_t i = 0; i < count; ++i)
		{
			mapped[i] = nullptr;
			for (auto& [_, d] : registry.entries)
				if (d.sysName == QLatin1StringView(buffer[i].sysName))
				{
					mapped[i] = &d;
					break;
				}
			if (!mapped[i] || !mapped[i]->pointer)
			{
				valid = false;
				break;
			}
		}
	return valid;
}
}
