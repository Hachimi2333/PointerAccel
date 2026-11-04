#include "startup_facts.hpp"
#include <QRegularExpression>
namespace pa
{
bool supportedKWinVersion(const QString& version)
{
	return QRegularExpression("^6\\.(7|8)\\.\\d+$").match(version).hasMatch();
}
QJsonObject StartupResult::toJson() const
{
	const char* name = "blocked";
	switch (state)
	{
	case StartupState::Blocked:
		break;
	case StartupState::NotInstalled:
		name = "not-installed";
		break;
	case StartupState::Unsupported:
		name = "unsupported";
		break;
	case StartupState::RepairRequired:
		name = "repair-required";
		break;
	case StartupState::PendingLogin:
		name = "pending-login";
		break;
	case StartupState::PendingKWin:
		name = "pending-kwin";
		break;
	case StartupState::Ready:
		name = "ready";
		break;
	}
	QJsonObject result{{"startup_state", name},
	    {"startup_compatible", compatible},
	    {"repair_required", repairRequired},
	    {"relogin_required", reloginRequired},
	    {"module_file_present", modulePresent},
	    {"module_file_valid", moduleValid},
	    {"error", error}};
	if (!installedBuild.isEmpty())
		result["installed_build"] = installedBuild;
	if (installedVersion)
		result["installed_kwin_version"] = *installedVersion;
	if (kwinOwner)
		result["kwin_owner"] = *kwinOwner;
	return result;
}
StartupResult evaluateStartup(const StartupFacts& f)
{
	StartupResult result;
	result.modulePresent = f.modulePresent;
	const auto stop = [&](StartupState state, const QString& error, bool repair = false)
	{
		result.state = state;
		result.error = error;
		result.repairRequired = repair;
		return result;
	};
	if (!f.installed)
		return stop(StartupState::NotInstalled, "PointerAccel 未安装或安装清单缺失。");
	if (!f.installationError.isEmpty())
		return stop(StartupState::Blocked, f.installationError);
	if (f.transactionPending)
		return stop(StartupState::Blocked, "安装事务未结束，请从源码运行 ./install.sh --recover。");
	if (!f.systemTrusted)
		return stop(StartupState::Blocked, "系统模块或兼容清单的所有权、权限无效。");
	const auto& record = f.record;
	if (!f.recordError.isEmpty() || record["schema"].toInt() != 1 || record["owner"].toInt(-1) != f.uid ||
	    record["kwin_iid"].toString() != "org.kde.kwin.PluginFactoryInterface" + record["kwin_version"].toString())
		return stop(StartupState::Blocked, "缺少有效兼容清单；旧安装请重新运行 install.sh 迁移。");
	result.installedBuild = record;
	result.moduleValid =
	    f.expectedModuleDigest.size() == 64 && f.expectedModuleDigest == record["module_sha256"].toString() &&
	    f.moduleDigest == f.expectedModuleDigest &&
	    f.ownerRecord == QString::number(f.uid) + "\t" + f.expectedModuleDigest + "\t" + f.recordDigest;
	if (!result.moduleValid)
		return stop(StartupState::Blocked, "系统模块校验失败，已阻止加载。");
	if (f.directAutostart)
		return stop(StartupState::Blocked, "KWin 直接自启仍开启，请重新运行 install.sh 迁移到先检查后加载。");
	if (!f.serviceConfigured)
		return stop(StartupState::Blocked, "会话检查服务未配置。");
	result.installedVersion = f.version;
	if (f.version.isEmpty())
		return stop(StartupState::Blocked, "无法确认已安装的 KWin 版本。");
	if (!supportedKWinVersion(f.version))
		return stop(StartupState::Unsupported,
		    "KWin " + f.version + " 尚未适配；支持系列为 6.7、6.8，需要代码审查；重新编译不能解决。",
		    true);
	if (record["kwin_library"].toString() != f.libraryPath ||
	    record["kwin_executable"].toString() != "/usr/bin/kwin_wayland" ||
	    f.version != record["kwin_version"].toString() || f.libraryDigest != record["kwin_sha256"].toString() ||
	    f.executableDigest != record["executable_sha256"].toString())
		return stop(
		    StartupState::RepairRequired, "KWin 已更新，请运行 pointeraccel repair；当前已加载模块可继续使用。", true);
	result.compatible = true;
	result.kwinOwner = f.kwinOwner;
	if (!f.stampError.isEmpty())
		return stop(StartupState::Blocked, "缺少有效的安装会话记录。");
	if (f.sameSession)
	{
		result.reloginRequired = true;
		return stop(StartupState::PendingLogin, "安装已完成，请重新登录；不会向当前 KWin 加载新模块。");
	}
	if (f.kwinOwner.isEmpty())
		return stop(StartupState::PendingKWin, "等待 Plasma Wayland 会话。");
	if (f.runningVersion != f.version)
	{
		result.reloginRequired = true;
		return stop(StartupState::PendingLogin, "运行中的 KWin 版本与磁盘不同，请重新登录。");
	}
	return stop(StartupState::Ready, {});
}
}
