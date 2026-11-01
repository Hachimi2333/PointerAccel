#include "maintenance/installation.hpp"
#include "worker.hpp"
#include "maintenance/startup.hpp"
#include "maintenance/watch_paths.hpp"
#include "platform/json_io.hpp"
#include <QCoreApplication>
#include <QDateTime>
namespace pa
{
GuiWorker::~GuiWorker()
{
	client.reset();
	if (!connectionName.isEmpty())
		QDBusConnection::disconnectFromBus(connectionName);
}
ControlClient& GuiWorker::control()
{
	if (!client)
	{
		connectionName = "pointeraccel-gui-" + QString::number(QCoreApplication::applicationPid());
		client =
		    std::make_unique<ControlClient>(QDBusConnection::connectToBus(QDBusConnection::SessionBus, connectionName));
	}
	return *client;
}
GuiSnapshot GuiWorker::inspect(Page page, bool force, bool checkAttributes, bool all)
{
	if (force || checkAttributes || startup.isEmpty())
	{
		paths = installationWatchPaths();
		const auto attributes = fileAttributes(paths);
		if (force || startup.isEmpty() || attributes != lastAttributes)
			startup = startupStatus();
		lastAttributes = attributes;
	}
	GuiSnapshot result;
	result.configuration = configuration();
	result.status = control().status(startup);
	result.paths = paths;
	if (page == Page::Devices || all)
	{
		QString error;
		const auto response = control().request("Devices", {}, &error);
		result.devicePayload = response["devices"].toArray();
		result.devices.emplace();
		for (const auto& item : result.devicePayload)
		{
			const auto device = item.toObject();
			result.devices->append({device["id"].toString(),
			    device["name"].toString(),
			    device["eligible"].toBool(),
			    true,
			    device["accelerated"].toBool(),
			    device["excluded"].toBool()});
		}
		result.devicesError = error;
	}
	if (page == Page::Diagnostics || all)
	{
		QString error;
		result.diagnostics = control().request("Diagnostics", {}, &error);
		result.diagnosticsError = error;
	}
	return result;
}
GuiResult GuiWorker::apply(const Config& config, const QString& revision, Page page)
{
	const auto result = control().commit(config, revision);
	return {inspect(page, true), result, {}};
}
GuiResult GuiWorker::reset(Page page)
{
	QString error;
	const auto response = control().request("ResetState", {}, &error);
	return {inspect(page), {}, response["ok"].toBool() ? "算法状态已重置。" : "重置失败：" + error};
}
GuiResult GuiWorker::exportDiagnostics(const QString& path)
{
	// Keep the full protocol objects in exported reports, including unknown future fields.
	auto snapshot = inspect(Page::Diagnostics, true, false, true);
	const QJsonObject report{{"exported_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
	    {"configuration", toJson(snapshot.configuration.config)},
	    {"configuration_error", snapshot.configuration.error},
	    {"status", snapshot.status},
	    {"devices", snapshot.devicePayload},
	    {"devices_error", snapshot.devicesError},
	    {"diagnostics", *snapshot.diagnostics},
	    {"diagnostics_error", snapshot.diagnosticsError}};
	QString error;
	const bool ok = writeJson(path, report, &error);
	return {snapshot, {}, ok ? "诊断已导出到 " + path : "导出失败：" + error};
}
}
