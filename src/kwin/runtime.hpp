#pragma once
#include "config/config.hpp"
#include "input/report.hpp"
#include "devices.hpp"
#include "diagnostics.hpp"
#include <QObject>
#include <array>
#include <memory>
#ifdef PA_TESTING
class TestBackend;
#endif
namespace pa
{
class ZeroRawFilter;
class PointerRuntime : public QObject
{
public:
	PointerRuntime();
	~PointerRuntime() override;
	QJsonObject status() const;
	QJsonObject deviceList() const;
	QJsonObject diagnosticSnapshot() const
	{
		return diagnostics.snapshot(config.trace, fault);
	}
	QJsonObject applyConfig();
	QJsonObject setTemporaryEnabled(bool enabled);
	QJsonObject resetState();
	QJsonObject shutdown();
#ifdef PA_TESTING
	QString TestHotplug();
	QString TestEnvironment() const;
	QString TestReports(const QString& payload);
#endif
private:
	bool attach(Device& device);
	bool deactivate();
	void fail(const QString& message);
	void apply();
	void motion(const QPointF& delta,
	    const QPointF& raw,
	    std::chrono::microseconds time,
	    KWin::InputDevice* source,
	    const QString& sys);
	std::array<PaReport, PA_REPORT_CAPACITY> reports{};
	DeviceRegistry registry;
	Config config;
	PointerEngine engine;
	std::optional<bool> temporary;
	bool active = false, compatibility = false;
	QString fault;
	InputDiagnostics diagnostics;
	bool replayingZero = false;
	std::unique_ptr<ZeroRawFilter> zeroRaw;
#ifdef PA_TESTING
	TestBackend* testBackend = nullptr;
#endif
};
}
