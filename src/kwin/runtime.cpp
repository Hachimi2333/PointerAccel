#include "runtime.hpp"
#include "platform/paths.hpp"
#include "platform/build_metadata.hpp"
#include "platform/runtime_identity.hpp"
#include "input/validation.hpp"
#include "input/capture.hpp"
#include "build_info.hpp"
#include <kwin/input.h>
#include <kwin/pointer_input.h>
#include <kwin/workspace.h>
#include <kwin/core/output.h>
#include <kwin/wayland_server.h>
#include <kwin/wayland/seat.h>
#include <kwin/input_event.h>
#include <QFile>
#include <QJsonArray>
#include <QTimer>
#include <QDebug>

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
		// KWin 6.7 skips relativePointerMotion when the accelerated delta is zero.
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
PointerRuntime::PointerRuntime()
{
	QTimer::singleShot(0,
	    this,
	    [this]
	    {
		    if (!KWin::input())
		    {
			    fault = "KWin 输入管理器尚不可用。";
			    return;
		    }
		    connect(KWin::input(),
		        &KWin::InputRedirection::deviceAdded,
		        this,
		        [this](auto* d)
		        {
			        registry.add(d);
			        if (auto it = registry.entries.find(d);
			            active && it != registry.entries.end() && !attach(it->second))
				        fail("新设备的原输入连接无法接管。");
		        });
		    connect(KWin::input(),
		        &KWin::InputRedirection::deviceRemoved,
		        this,
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
		    apply();
	    });
}

PointerRuntime::~PointerRuntime()
{
	deactivate();
}
QJsonObject PointerRuntime::status() const
{
	const bool installed = QFile::exists(pa::manifestPath());
	return QJsonObject({{"installed", installed},
	    {"loaded", true},
	    {"configured_enabled", config.enabled},
	    {"active", active},
	    {"temporary_override", temporary.has_value()},
	    {"healthy", fault.isEmpty()},
	    {"compatible", compatibility},
	    {"running_compatible", compatibility},
	    {"running_build", pa::buildInfo()},
	    {"state",
	        !fault.isEmpty()             ? "error"
	        : !active                    ? "disabled"
	        : diagnostics.processed == 0 ? "idle"
	                                     : "active"},
	    {"error", fault},
	    {"processed", static_cast<double>(diagnostics.processed)},
	    {"settings", pa::toJson(config)}});
}

QJsonObject PointerRuntime::deviceList() const
{
	QJsonArray list;
	for (const auto& [_, d] : registry.entries)
	{
		list.append(QJsonObject{{"id", d.id},
		    {"name", d.name},
		    {"sys_name", d.sysName},
		    {"eligible", d.eligible},
		    {"excluded", config.excluded.contains(d.id)},
		    {"accelerated", active && d.eligible && !config.excluded.contains(d.id)}});
	}
	return QJsonObject({{"devices", list}});
}

QJsonObject PointerRuntime::applyConfig()
{
	temporary.reset();
	apply();
	return status();
}

QJsonObject PointerRuntime::setTemporaryEnabled(bool enabled)
{
	temporary = enabled;
	apply();
	return status();
}

QJsonObject PointerRuntime::resetState()
{
	engine.reset();
	return QJsonObject({{"ok", true}});
}

QJsonObject PointerRuntime::shutdown()
{
	temporary = false;
	if (deactivate())
		fault.clear();
	return status();
}
bool PointerRuntime::attach(Device& d)
{
	return registry.attach(d,
	    this,
	    [this](const QPointF& delta,
	        const QPointF& raw,
	        std::chrono::microseconds time,
	        KWin::InputDevice* source,
	        const QString& sys)
	    {
		    motion(delta, raw, time, source, sys);
	    });
}
bool PointerRuntime::deactivate()
{
	pa::capture::arm(false);
	zeroRaw.reset();
	replayingZero = false;
	for (auto& [_, d] : registry.entries)
		registry.restore(d);
	active = false;
	QString why;
	if (!pa::capture::uninstall(&why))
	{
		fault = why;
		return false;
	}
	return true;
}

void PointerRuntime::fail(const QString& message)
{
	fault = message;
	deactivate();
	qWarning().noquote() << "PointerAccel:" << fault;
}

void PointerRuntime::apply()
{
	const bool wasActive = active;
	if (!deactivate())
		return;
	fault.clear();
	config = pa::readConfig(&fault);
	if (!config.trace)
	{
		diagnostics.clearTrace();
	}
	compatibility = pa::runtimeCompatible(pa::buildInfo(), &fault);
	if (!compatibility || !fault.isEmpty())
		return;
	engine.configure(config.settings);
	if (!temporary.value_or(config.enabled))
		return;
	if (qEnvironmentVariableIntValue("KWIN_RESTART_COUNT") > 0)
	{
		fault = "KWin 本次为崩溃恢复启动，已跳过接入；请重新登录。";
		return;
	}
	if (!pa::capture::install(PA_KWIN_LIBRARY, &fault))
		return;
	for (auto& [_, d] : registry.entries)
		if (!attach(d))
		{
			fail("无法接管 KWin 原生位移连接。");
			return;
		}
	if (!wasActive)
		engine.reset();
	zeroRaw = std::make_unique<ZeroRawFilter>(replayingZero);
	KWin::input()->installInputEventFilter(zeroRaw.get());
	pa::capture::arm(true);
	active = true;
}

void PointerRuntime::motion(const QPointF& delta,
    const QPointF& raw,
    std::chrono::microseconds time,
    KWin::InputDevice* source,
    const QString& sys)
{
	bool overflow = false;
	const auto count = pa::capture::drain(reports.data(), reports.size(), &overflow);
	auto batch = std::span(reports.data(), count);
	bool valid =
	    !overflow &&
	    pa::batchMatches(batch, delta.x(), delta.y(), raw.x(), raw.y(), time.count(), sys.toLocal8Bit().constData());
	std::array<Device*, PA_REPORT_CAPACITY> mapped;
	if (valid)
		for (std::size_t i = 0; i < count; ++i)
		{
			mapped[i] = nullptr;
			for (auto& [_, d] : registry.entries)
				if (d.sysName == QLatin1StringView(reports[i].sysName))
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
	if (!valid)
	{
		fail(overflow ? "报告队列溢出，已恢复原生输入。" : "报告与合并事件不一致或设备缺失，已恢复原生输入。");
		KWin::input()->pointer()->processMotion(delta, raw, time, source);
		return;
	}
	++diagnostics.batches;
	// All validation precedes engine advancement: a rejected batch is replayed natively once.
	for (std::size_t i = 0; i < count; ++i)
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
		replayingZero = accelerated && moved.isNull() && (r.rawX != 0 || r.rawY != 0);
		KWin::input()->pointer()->processMotion(
		    moved, {r.rawX, r.rawY}, std::chrono::microseconds(r.timeUsec), d->pointer);
		replayingZero = false;
		// XWayland consumes pending relative motion at wl_pointer.frame. A single
		// frame for an entire replay batch would keep only its last relative delta.
		KWin::input()->pointer()->processFrame(d->pointer);
	}
}

}
