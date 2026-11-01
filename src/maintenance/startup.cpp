#include "startup.hpp"
#include "installation.hpp"
#include "context.hpp"
#include "platform/paths.hpp"
#include "platform/json_io.hpp"
#include <QDBusInterface>
#include <QDBusReply>
#include <QFileInfo>
#include <QRegularExpression>
#include <KConfig>
#include <KConfigGroup>
#include <unistd.h>

namespace pa
{
using namespace maintenance;
QJsonObject sessionStamp()
{
	return {{"session", qEnvironmentVariable("XDG_SESSION_ID")},
	    {"boot", readText("/proc/sys/kernel/random/boot_id")},
	    {"owner", owner()},
	    {"bus", busId()}};
}

QJsonObject startupStatus()
{
	QJsonObject status{{"startup_state", "blocked"},
	    {"startup_compatible", false},
	    {"repair_required", false},
	    {"relogin_required", false},
	    {"module_file_present", QFileInfo::exists(installedPluginPath())},
	    {"module_file_valid", false},
	    {"error", QString()}};
	const auto stop = [&](const QString& state, const QString& error, bool repair = false)
	{
		status["startup_state"] = state;
		status["error"] = error;
		status["repair_required"] = repair;
		return status;
	};
	if (!QFileInfo::exists(manifestPath()))
		return stop("not-installed", "PointerAccel 未安装或安装清单缺失。");
	QString error;
	if (!verifyInstallation(&error))
		return stop("blocked", error);
	if (QFileInfo::exists(systemRecordRoot() + "/pending") || QFileInfo::exists(stateRoot() + ".transaction.json"))
		return stop("blocked", "安装事务未结束，请从源码运行 ./install.sh --recover。");
	const auto recordPath = systemRecordRoot() + "/compatibility.json";
	const QFileInfo recordFile(recordPath), module(installedPluginPath()), systemDirectory(systemRecordRoot());
	if (!testing())
	{
		for (const auto& info : {recordFile, module, systemDirectory})
			if (!info.exists() || info.isSymLink() || info.ownerId() != 0 ||
			    (info.permissions() & (QFileDevice::WriteGroup | QFileDevice::WriteOther)))
				return stop("blocked", "系统模块或兼容清单的所有权、权限无效。");
	}
	const auto record = readJson(recordPath, &error);
	if (!error.isEmpty() || record["schema"].toInt() != 1 || record["owner"].toInt(-1) != int(getuid()) ||
	    record["kwin_iid"].toString() != "org.kde.kwin.PluginFactoryInterface" + record["kwin_version"].toString())
		return stop("blocked", "缺少有效兼容清单；旧安装请重新运行 install.sh 迁移。");
	status["installed_build"] = record;
	const auto expected = readText(stateRoot() + "/system.sha256");
	const bool valid = expected.size() == 64 && expected == record["module_sha256"].toString() &&
	                   digest(installedPluginPath()) == expected &&
	                   readText(systemRecordRoot() + "/owner.tsv") ==
	                       QString::number(getuid()) + "\t" + expected + "\t" + digest(recordPath);
	status["module_file_valid"] = valid;
	if (!valid)
		return stop("blocked", "系统模块校验失败，已阻止加载。");
	if (KConfig(kwinConfigPath(), KConfig::SimpleConfig).group("Plugins").readEntry("pointeraccel_kwinEnabled", false))
		return stop("blocked", "KWin 直接自启仍开启，请重新运行 install.sh 迁移到先检查后加载。");
	if (!QFileInfo::exists(sessionUnitPath()) || !QFileInfo(sessionLinkPath()).isSymLink())
		return stop("blocked", "会话检查服务未配置。");
	const auto version = installedVersion();
	status["installed_kwin_version"] = version;
	if (version.isEmpty())
		return stop("blocked", "无法确认已安装的 KWin 版本。");
	if (!QRegularExpression("^6\\.(7|8)\\.\\d+$").match(version).hasMatch())
		return stop("unsupported",
		    "KWin " + version + " 尚未适配；支持系列为 6.7、6.8，需要代码审查；重新编译不能解决。",
		    true);
	if (record["kwin_library"].toString() != QFileInfo("/usr/lib64/libkwin.so.6").canonicalFilePath() ||
	    record["kwin_executable"].toString() != "/usr/bin/kwin_wayland" ||
	    version != record["kwin_version"].toString() ||
	    digest(record["kwin_library"].toString()) != record["kwin_sha256"].toString() ||
	    digest("/usr/bin/kwin_wayland") != record["executable_sha256"].toString())
		return stop("repair-required", "KWin 已更新，请运行 pointeraccel repair；当前已加载模块可继续使用。", true);
	status["startup_compatible"] = true;
	const auto currentOwner = owner();
	status["kwin_owner"] = currentOwner;
	error.clear();
	const auto stamp = readJson(stateRoot() + "/session.json", &error);
	if (!error.isEmpty())
		return stop("blocked", "缺少有效的安装会话记录。");
	if (sameSession(stamp, currentOwner))
	{
		status["relogin_required"] = true;
		return stop("pending-login", "安装已完成，请重新登录；不会向当前 KWin 加载新模块。");
	}
	if (currentOwner.isEmpty())
		return stop("pending-kwin", "等待 Plasma Wayland 会话。");
	QDBusInterface kwin(currentOwner, "/KWin", "org.kde.KWin");
	kwin.setTimeout(3000);
	const QDBusReply<QString> info = kwin.call("supportInformation");
	const auto match = QRegularExpression("KWin version: ([^\\n]+)").match(info.isValid() ? info.value() : QString());
	if (!match.hasMatch() || match.captured(1).trimmed() != version)
	{
		status["relogin_required"] = true;
		return stop("pending-login", "运行中的 KWin 版本与磁盘不同，请重新登录。");
	}
	return stop("ready", {});
}

}
