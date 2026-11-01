#pragma once
#include "runtime.hpp"
#include "control/protocol.hpp"
#include "platform/build_metadata.hpp"
#include <QDBusConnection>
#include <QJsonDocument>
namespace pa
{
class PluginControl final : public QObject
{
	Q_OBJECT
	Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.PointerAccel1")
public:
	explicit PluginControl(pa::PointerRuntime& runtime) : runtime(runtime)
	{
		QDBusConnection::sessionBus().registerObject(pa::dbusPath, this, QDBusConnection::ExportScriptableSlots);
	}
	~PluginControl() override
	{
		QDBusConnection::sessionBus().unregisterObject(pa::dbusPath);
	}
public Q_SLOTS:
	Q_SCRIPTABLE QString Status() const
	{
		return encode(runtime.status());
	}
	Q_SCRIPTABLE QString Info() const
	{
		return encode(pa::buildInfo());
	}
	Q_SCRIPTABLE QString Devices() const
	{
		return encode(runtime.deviceList());
	}
	Q_SCRIPTABLE QString Diagnostics() const
	{
		return encode(runtime.diagnosticSnapshot());
	}
	Q_SCRIPTABLE QString ApplyConfig()
	{
		return encode(runtime.applyConfig());
	}
	Q_SCRIPTABLE QString SetTemporaryEnabled(bool enabled)
	{
		return encode(runtime.setTemporaryEnabled(enabled));
	}
	Q_SCRIPTABLE QString ResetState()
	{
		return encode(runtime.resetState());
	}
	Q_SCRIPTABLE QString Shutdown()
	{
		return encode(runtime.shutdown());
	}
#ifdef PA_TESTING
	Q_SCRIPTABLE QString TestHotplug()
	{
		return runtime.TestHotplug();
	}
	Q_SCRIPTABLE QString TestEnvironment() const
	{
		return runtime.TestEnvironment();
	}
	Q_SCRIPTABLE QString TestReports(const QString& payload)
	{
		return runtime.TestReports(payload);
	}
#endif
private:
	static QString encode(const QJsonObject& json)
	{
		return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
	}
	pa::PointerRuntime& runtime;
};
}
