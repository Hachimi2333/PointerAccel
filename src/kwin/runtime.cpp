#include "runtime.hpp"
#include "platform/paths.hpp"
#include "platform/build_metadata.hpp"
#include "platform/runtime_identity.hpp"
#include "build_info.hpp"
#include <kwin/input.h>
#include <kwin/pointer_input.h>
#include <QFile>
#include <QJsonArray>
#include <QTimer>
#include <QDebug>

namespace pa
{
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
		    session.initialize(
		        this,
		        [this](const QPointF& delta,
		            const QPointF& raw,
		            std::chrono::microseconds time,
		            KWin::InputDevice* source,
		            const QString& sys)
		        {
			        motion(delta, raw, time, source, sys);
		        },
		        [this](const QString& message)
		        {
			        fail(message);
		        });
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
	    {"active", session.active()},
	    {"temporary_override", temporary.has_value()},
	    {"healthy", fault.isEmpty()},
	    {"compatible", compatibility},
	    {"running_compatible", compatibility},
	    {"running_build", pa::buildInfo()},
	    {"state",
	        !fault.isEmpty()             ? "error"
	        : !session.active()          ? "disabled"
	        : diagnostics.processed == 0 ? "idle"
	                                     : "active"},
	    {"error", fault},
	    {"processed", static_cast<double>(diagnostics.processed)},
	    {"settings", pa::toJson(config)}});
}

QJsonObject PointerRuntime::deviceList() const
{
	QJsonArray list;
	for (const auto& [_, d] : session.devices().entries)
	{
		list.append(QJsonObject{{"id", d.id},
		    {"name", d.name},
		    {"sys_name", d.sysName},
		    {"eligible", d.eligible},
		    {"excluded", config.excluded.contains(d.id)},
		    {"accelerated", session.active() && d.eligible && !config.excluded.contains(d.id)}});
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
bool PointerRuntime::deactivate()
{
	return session.deactivate(&fault);
}

void PointerRuntime::fail(const QString& message)
{
	fault = message;
	deactivate();
	qWarning().noquote() << "PointerAccel:" << fault;
}

void PointerRuntime::apply()
{
	const bool wasActive = session.active();
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
	if (!session.installCapture(PA_KWIN_LIBRARY, &fault))
		return;
	for (auto& [_, d] : session.devices().entries)
		if (!session.attach(d))
		{
			fail("无法接管 KWin 原生位移连接。");
			return;
		}
	if (!wasActive)
		engine.reset();
	session.activate();
}

void PointerRuntime::motion(const QPointF& delta,
    const QPointF& raw,
    std::chrono::microseconds time,
    KWin::InputDevice* source,
    const QString& sys)
{
	std::array<Device*, PA_REPORT_CAPACITY> mapped;
	const bool valid = batch.collect(session.devices(), delta, raw, time, sys, mapped);
	if (!valid)
	{
		fail(batch.overflow() ? "报告队列溢出，已恢复原生输入。" : "报告与合并事件不一致或设备缺失，已恢复原生输入。");
		KWin::input()->pointer()->processMotion(delta, raw, time, source);
		return;
	}
	replay(batch.reports(), std::span(mapped).first(batch.reports().size()));
}

}
