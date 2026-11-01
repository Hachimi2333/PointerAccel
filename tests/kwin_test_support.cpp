#include "kwin/runtime.hpp"
#include "input/capture.hpp"
#include "input/validation.hpp"
#include "virtual_devices.hpp"
#include "engine/reference_model.hpp"
#include <kwin/input.h>
#include <kwin/workspace.h>
#include <kwin/core/output.h>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCoreApplication>
#include <cstring>
namespace pa
{
namespace
{
QString jsonText(const QJsonObject& json)
{
	return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}
}
QString PointerRuntime::TestHotplug()
{
	if (!testBackend || !QCoreApplication::arguments().contains("--virtual"))
		return "{}";
	auto* old = testBackend->mice[1];
	const auto sys = old->sys;
	Q_EMIT testBackend->deviceRemoved(old);
	delete old;
	auto* replacement = new TestMouse(sys, false);
	testBackend->mice[1] = replacement;
	Q_EMIT testBackend->deviceAdded(replacement);
	registry.entries.at(replacement).eligible = true;
	return jsonText(status());
}

QString PointerRuntime::TestEnvironment() const
{
	return jsonText({{"DISPLAY", qEnvironmentVariable("DISPLAY")},
	    {"WAYLAND_DISPLAY", qEnvironmentVariable("WAYLAND_DISPLAY")},
	    {"XAUTHORITY", qEnvironmentVariable("XAUTHORITY")}});
}

QString PointerRuntime::TestReports(const QString& payload)
{
	if (!QCoreApplication::arguments().contains("--virtual") || qgetenv("POINTERACCEL_ISOLATED_TEST") != "1")
		return "{}";
	if (!testBackend)
	{
		auto backend = std::make_unique<TestBackend>();
		testBackend = backend.get();
		KWin::input()->addInputBackend(std::move(backend));
		for (auto* m : testBackend->mice)
			registry.entries.at(m).eligible = !m->isTouchpad();
	}
	const auto input = QJsonDocument::fromJson(payload.toUtf8()).object();
	const auto list = input["reports"].toArray();
	if (list.isEmpty())
		return "{}";
	if (input["center"].toBool(true))
		KWin::input()->warpPointer(KWin::workspace()->geometry().center());
	if (input["reset"].toBool(true))
		engine.reset();
	pa::test::ReferenceModel reference(config.settings);
	MotionSpy spy;
	KWin::input()->installInputEventSpy(&spy);
	QPointF sum, raw;
	std::uint64_t timestamp = 0;
	QJsonArray expected;
	auto* first = testBackend->mice.at(list[0].toObject()["device"].toInt());
	for (const auto& value : list)
	{
		const auto o = value.toObject();
		const int which = o["device"].toInt();
		PaReport r{o["dx"].toDouble(),
		    o["dy"].toDouble(),
		    o["x"].toDouble(),
		    o["y"].toDouble(),
		    static_cast<std::uint64_t>(o["time"].toDouble()),
		    {}};
		auto* m = testBackend->mice.at(which);
		std::strncpy(r.sysName, m->sys.toLocal8Bit().constData(), sizeof(r.sysName) - 1);
		if (active)
			pa::capture::appendForTest(r);
		sum += QPointF(r.dx, r.dy);
		raw += QPointF(r.rawX, r.rawY);
		timestamp = r.timeUsec;
		QPointF expectedDelta(r.dx, r.dy);
		if (active && !m->pad && !config.excluded.contains(registry.entries.at(m).id))
		{
			const auto integer = pa::rawMotion(r.rawX, r.rawY);
			if (integer)
			{
				const auto* output = KWin::workspace()->outputAt(KWin::input()->globalPointer());
				auto out = reference.apply(*integer, pa::displayDpiFromScale(output ? output->scale() : 1));
				expectedDelta = {double(out.x), double(out.y)};
			}
		}
		expected.append(QJsonObject{{"dx", expectedDelta.x()},
		    {"dy", expectedDelta.y()},
		    {"raw_x", r.rawX},
		    {"raw_y", r.rawY},
		    {"device", m->name()},
		    {"time", double(r.timeUsec)}});
	}
	if (input["corrupt"].toBool())
		raw.rx() += 1;
	Q_EMIT first->pointerMotion(sum, raw, std::chrono::microseconds(timestamp), first);
	Q_EMIT first->pointerFrame(first);
	KWin::input()->uninstallInputEventSpy(&spy);
	QJsonObject result{{"observed", spy.samples}, {"status", status()}};
	// The per-call reference starts with fresh state. Continuous input instead
	// uses the frozen pre-refactor compositor trace in continuous_input.json.
	if (input["reset"].toBool(true))
		result["expected"] = expected;
	return jsonText(result);
}
}
