#include "status.hpp"
namespace pa
{
QJsonObject composeStatus(const ConfigSnapshot& snapshot,
    const QJsonObject& startup,
    QJsonObject status,
    const QString& why,
    bool installed,
    bool direct,
    bool autostart)
{
	const auto& config = snapshot.config;
	const auto& configError = snapshot.error;
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
	}
	status["configured_enabled"] = config.enabled;
	status["configuration_valid"] = configError.isEmpty();
	status["direct_autostart_enabled"] = direct;
	status["autostart_configured"] = autostart;
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
}
