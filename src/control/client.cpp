#include "client.hpp"
#include "config/repository.hpp"
#include "status.hpp"
#include "maintenance/startup.hpp"
#include "maintenance/installation.hpp"
#include "platform/paths.hpp"
#include <QFile>
#include <QFileInfo>
#include <KConfig>
#include <KConfigGroup>

namespace pa
{
ControlClient::ControlClient(QDBusConnection connection, int timeoutMs) : transport(std::move(connection), timeoutMs)
{
}

QJsonObject ControlClient::request(const QString& method, const QVariantList& args, QString* error) const
{
	return transport.request(method, args, error);
}

QJsonObject ControlClient::status(const QJsonObject& cachedStartup) const
{
	return status(configuration(), cachedStartup);
}
QJsonObject ControlClient::status(const ConfigSnapshot& snapshot, const QJsonObject& cachedStartup) const
{
	const auto startup = cachedStartup.isEmpty() ? startupStatus() : cachedStartup;
	QString error;
	auto runtime = request("Status", {}, &error);
	if (error.isEmpty() && !runtime.contains("running_build"))
	{
		QString infoError;
		runtime["running_build"] = request("Info", {}, &infoError);
	}
	const bool direct = KConfig(pa::kwinConfigPath(), KConfig::SimpleConfig)
	                        .group("Plugins")
	                        .readEntry("pointeraccel_kwinEnabled", false);
	const bool autostart = !direct && QFile::exists(sessionUnitPath()) && QFileInfo(sessionLinkPath()).isSymLink();
	return composeStatus(snapshot, startup, runtime, error, QFile::exists(manifestPath()), direct, autostart);
}

namespace
{
CommitResult save(
    ControlClient const& client, const std::function<void(Config&)>& edit, const QString* expectedRevision)
{
	CommitResult result;
	const ConfigRepository repository(configPath());
	ConfigRepository::Transaction transaction(repository);
	const auto saved = transaction.save(edit, expectedRevision);
	result.saved = saved.saved;
	result.conflict = saved.conflict;
	result.snapshot = saved.snapshot;
	result.error = saved.error;
	if (!result.saved)
		return result;
	result.response = client.request("ApplyConfig", {}, &result.error);
	result.applied = result.error.isEmpty() && result.response.value("healthy").isBool() &&
	                 result.response.value("healthy").toBool();
	if (!result.applied && result.error.isEmpty())
		result.error = result.response.value("error").toString("模块未确认配置已生效。");
	return result;
}
}

CommitResult ControlClient::commit(const Config& config, const QString& expectedRevision) const
{
	return save(
	    *this,
	    [&](Config& current)
	    {
		    current = config;
	    },
	    &expectedRevision);
}

CommitResult ControlClient::update(const std::function<void(Config&)>& edit) const
{
	return save(*this, edit, nullptr);
}
}
