#include "runtime.hpp"
#include "input/validation.hpp"
#include <kwin/input.h>
#include <kwin/pointer_input.h>
#include <kwin/workspace.h>
#include <kwin/core/output.h>
namespace pa
{
void PointerRuntime::replay(std::span<const PaReport> reports, std::span<Device*> mapped)
{
	++diagnostics.batches;
	// All validation precedes engine advancement: a rejected batch is replayed natively once.
	for (std::size_t i = 0; i < reports.size(); ++i)
	{
		const auto& r = reports[i];
		const auto* d = mapped[i];
		QPointF moved(r.dx, r.dy);
		const auto integer = pa::rawMotion(r.rawX, r.rawY);
		const bool accelerated = d->eligible && !config.excluded.contains(d->id) && integer.has_value();
		if (accelerated)
		{
			const auto* display = KWin::workspace()->outputAt(KWin::input()->globalPointer());
			const auto scale = display ? display->scale() : 1.0;
			const auto dpi = pa::displayDpiFromScale(scale);
			const auto out = engine.apply(*integer, dpi);
			moved = {static_cast<double>(out.x), static_cast<double>(out.y)};
			diagnostics.record({d->id, r.rawX, r.rawY, scale, out, dpi, r.timeUsec}, config.trace);
		}
		else
			++diagnostics.passthrough;
		session.replayingZero(accelerated && moved.isNull() && (r.rawX != 0 || r.rawY != 0));
		KWin::input()->pointer()->processMotion(
		    moved, {r.rawX, r.rawY}, std::chrono::microseconds(r.timeUsec), d->pointer);
		session.replayingZero(false);
		// XWayland consumes pending relative motion at wl_pointer.frame. A single
		// frame for an entire replay batch would keep only its last relative delta.
		KWin::input()->pointer()->processFrame(d->pointer);
	}
}
}
