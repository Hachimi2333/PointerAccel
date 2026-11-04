#pragma once
#include "config/config.hpp"
#include "input/report.hpp"
#include "input_session.hpp"
#include "input_batch.hpp"
#include "diagnostics.hpp"
#include <QObject>
#include <array>
#include <memory>
#ifdef PA_TESTING
class TestBackend;
#endif
namespace pa
{
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
	bool deactivate();
	void fail(const QString& message);
	void apply();
	void motion(const QPointF& delta,
	    const QPointF& raw,
	    std::chrono::microseconds time,
	    KWin::InputDevice* source,
	    const QString& sys);
	void replay(std::span<const PaReport> reports, std::span<Device*> mapped);
	InputBatch batch;
	InputSession session;
	Config config;
	PointerEngine engine;
	std::optional<bool> temporary;
	bool compatibility = false;
	QString fault;
	InputDiagnostics diagnostics;
#ifdef PA_TESTING
	TestBackend* testBackend = nullptr;
#endif
};
}
