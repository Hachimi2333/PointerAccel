#include "control.hpp"
#include "session.hpp"
#include <QCryptographicHash>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <KConfig>
#include <KConfigGroup>

namespace pa
{
ConfigSnapshot configuration()
{
	ConfigSnapshot result;
	QFile file(configPath());
	if (QFileInfo(file).isSymLink())
	{
		result.error = "配置不能是符号链接。";
		result.config.enabled = false;
		return result;
	}
	if (!file.exists())
	{
		result.revision = "missing";
		return result;
	}
	if (!file.open(QIODevice::ReadOnly))
	{
		result.error = file.errorString();
		result.config.enabled = false;
		return result;
	}
	const auto bytes = file.readAll();
	result.revision = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(bytes, &parse);
	if (parse.error != QJsonParseError::NoError || !document.isObject())
	{
		result.error = "无效 JSON: " + parse.errorString();
		result.config.enabled = false;
		return result;
	}
	result.config = parseConfig(document.object(), &result.error);
	return result;
}

ControlClient::ControlClient(QDBusConnection connection, int timeoutMs) : bus(std::move(connection)), timeout(timeoutMs)
{
}

QJsonObject ControlClient::request(const QString& method, const QVariantList& args, QString* error) const
{
	if (error)
		error->clear();
	auto message = QDBusMessage::createMethodCall("org.kde.KWin", dbusPath, dbusInterface, method);
	message.setAutoStartService(false);
	message.setArguments(args);
	const auto reply = bus.call(message, QDBus::Block, timeout);
	if (reply.type() == QDBusMessage::ErrorMessage)
	{
		if (error)
			*error = reply.errorName() == "org.freedesktop.DBus.Error.UnknownObject" ||
			                 reply.errorName() == "org.freedesktop.DBus.Error.ServiceUnknown"
			             ? QStringLiteral("KWin 模块未加载。首次安装后请重新登录 Plasma。")
			             : reply.errorMessage();
		return {};
	}
	const auto doc = QJsonDocument::fromJson(reply.arguments().value(0).toString().toUtf8());
	if (!doc.isObject())
	{
		if (error)
			*error = "模块返回的数据无效。";
		return {};
	}
	return doc.object();
}

QJsonObject ControlClient::status(const QJsonObject& cachedStartup) const
{
	const auto snapshot = configuration();
	const auto config = snapshot.config;
	const auto configError = snapshot.error;
	const auto startup = cachedStartup.isEmpty() ? startupStatus() : cachedStartup;
	QString why;
	auto status = request("Status", {}, &why);
	const bool installed = QFile::exists(pa::manifestPath());
	if (!why.isEmpty())
	{
		status = {{"installed", installed},
		    {"loaded", false},
		    {"active", false},
		    {"configured_enabled", config.enabled},
		    {"compatible", false},
		    {"running_compatible", false},
		    {"healthy", false},
		    {"state", startup["startup_state"]},
		    {"error", startup["error"]},
		    {"settings", pa::toJson(config)}};
		if (startup["startup_state"] == "ready")
		{
			status["state"] = "load-failed";
			status["error"] = why;
		}
	}
	else
	{
		if (!status.contains("running_compatible"))
			status["running_compatible"] = status["compatible"];
		if (!status.contains("running_build"))
		{
			QString infoError;
			status["running_build"] = request("Info", {}, &infoError);
		}
	}
	status["configured_enabled"] = config.enabled;
	status["configuration_valid"] = configError.isEmpty();
	const bool direct = KConfig(pa::kwinConfigPath(), KConfig::SimpleConfig)
	                        .group("Plugins")
	                        .readEntry("pointeraccel_kwinEnabled", false);
	status["direct_autostart_enabled"] = direct;
	status["autostart_configured"] =
	    !direct && QFile::exists(pa::sessionUnitPath()) && QFileInfo(pa::sessionLinkPath()).isSymLink();
	for (const auto* key : {"startup_state",
	         "startup_compatible",
	         "repair_required",
	         "relogin_required",
	         "module_file_present",
	         "module_file_valid",
	         "installed_build",
	         "installed_kwin_version"})
		status[key] = startup[key];
	const auto runningBuild = status["running_build"].toObject(),
	           installedBuild = startup["installed_build"].toObject();
	const bool moduleMatches = status["loaded"].toBool() && !runningBuild["image_build_id"].toString().isEmpty() &&
	                           runningBuild["image_build_id"] == installedBuild["module_build_id"];
	status["running_module_matches_install"] = moduleMatches;
	if (status["loaded"].toBool() && !moduleMatches)
	{
		status["relogin_required"] = true;
		if (status["startup_state"] == "ready")
		{
			status["startup_state"] = "pending-login";
			status["error"] = "当前仍驻留旧模块，请重新登录以加载已安装的新模块。";
		}
	}
	if (!startup["error"].toString().isEmpty())
		status["error"] = startup["error"];
	if (!configError.isEmpty())
		status["error"] = configError;
	const bool healthy = status["healthy"].toBool() && configError.isEmpty() && installed &&
	                     status["module_file_valid"].toBool() && status["autostart_configured"].toBool() &&
	                     status["startup_state"] == "ready" && status["running_compatible"].toBool() && moduleMatches;
	status["healthy"] = healthy;
	return status;
}

namespace
{
CommitResult save(
    ControlClient const& client, const std::function<void(Config&)>& edit, const QString* expectedRevision)
{
	CommitResult result;
	if (!QDir().mkpath(configRoot()))
	{
		result.error = "无法建立配置目录。";
		return result;
	}
	QLockFile lock(configRoot() + "/config.lock");
	if (!lock.tryLock(0))
	{
		result.error = "另一个进程正在保存配置，请稍后重试。";
		return result;
	}
	result.snapshot = configuration();
	if (!result.snapshot.error.isEmpty())
	{
		result.error = result.snapshot.error;
		return result;
	}
	if (expectedRevision && result.snapshot.revision != *expectedRevision)
	{
		result.conflict = true;
		result.error = "配置已被外部修改。请重新加载后再编辑。";
		return result;
	}
	auto config = result.snapshot.config;
	edit(config);
	QString validation;
	parseConfig(toJson(config), &validation);
	if (!validation.isEmpty())
	{
		result.error = validation;
		return result;
	}
	if (!writeConfig(config, &result.error))
		return result;
	result.saved = true;
	result.snapshot = configuration();
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
