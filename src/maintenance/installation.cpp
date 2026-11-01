#include "installation.hpp"
#include "context.hpp"
#include "platform/paths.hpp"
#include "platform/json_io.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <unistd.h>

namespace pa
{
using namespace maintenance;
namespace
{
bool plainPath(const QString& path)
{
	return path.startsWith('/') && !path.contains("/../") && !path.contains("/./") && !path.contains('\n') &&
	       !path.contains('\t');
}

bool allowedFile(const QString& path)
{
	return plainPath(path) && (path.startsWith(dataRoot() + "/") || path.startsWith(stateRoot() + "/backups/") ||
	                              QStringList{stateRoot() + "/owner",
	                                  stateRoot() + "/system.sha256",
	                                  stateRoot() + "/session.json",
	                                  sessionUnitPath()}
	                                  .contains(path));
}

bool allowedDirectory(const QString& path)
{
	if (!plainPath(path) || path == "/" || path == QDir::homePath())
		return false;
	for (const auto& root : {dataRoot(),
	         configRoot(),
	         stateRoot(),
	         QFileInfo(cliPath()).path(),
	         QFileInfo(sessionUnitPath()).path(),
	         QFileInfo(sessionLinkPath()).path()})
		if (path == root || root.startsWith(path + "/") || path.startsWith(dataRoot() + "/") ||
		    path.startsWith(stateRoot() + "/"))
			return true;
	return false;
}
}
QString systemRecordRoot()
{
	const auto root = qEnvironmentVariable("POINTERACCEL_TEST_SYSTEM_ROOT");
	return testing() && !root.isEmpty() ? root + "/records" : "/usr/local/share/pointeraccel";
}

QString installedPluginPath()
{
	const auto root = qEnvironmentVariable("POINTERACCEL_TEST_SYSTEM_ROOT");
	return testing() && !root.isEmpty() ? root + "/plugins/pointeraccel_kwin.so" : pluginPath();
}

QString sessionUnitPath()
{
	return configBase() + "/systemd/user/pointeraccel-session.service";
}

QString sessionLinkPath()
{
	return configBase() + "/systemd/user/plasma-workspace.target.wants/pointeraccel-session.service";
}

QString maintenanceLockPath()
{
	return "/tmp/pointeraccel-" + QString::number(getuid()) + "-" +
	       QString::fromLatin1(QCryptographicHash::hash(stateRoot().toUtf8(), QCryptographicHash::Sha256).toHex()) +
	       ".lock";
}

bool verifyInstallation(QString* error)
{
	const auto reject = [&](const QString& text)
	{
		if (error)
			*error = text;
		return false;
	};
	QFile file(manifestPath());
	if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly) ||
	    readText(stateRoot() + "/owner") != QString::number(getuid()))
		return reject("缺少有效的当前用户安装清单。");
	for (const auto& path : {dataRoot(),
	         stateRoot(),
	         dataRoot() + "/bin",
	         dataRoot() + "/scripts",
	         stateRoot() + "/backups",
	         QFileInfo(sessionUnitPath()).path(),
	         QFileInfo(sessionLinkPath()).path()})
		if (QFileInfo(path).isSymLink())
			return reject("安装目录变成符号链接: " + path);
	QSet<QString> entries;
	for (const auto& line : file.readAll().split('\n'))
	{
		if (line.isEmpty())
			continue;
		const auto parts = line.split('\t');
		if (parts.size() != 3)
			return reject("安装清单格式错误。");
		const auto kind = parts[0];
		const QString value = QString::fromUtf8(parts[1]), path = QString::fromUtf8(parts[2]);
		if (kind == "D")
		{
			if (!allowedDirectory(path))
				return reject("目录清单超出允许范围。");
			continue;
		}
		if (entries.contains(path))
			return reject("安装清单存在重复文件。");
		entries.insert(path);
		if (kind == "F")
		{
			if (!allowedFile(path) || QFileInfo(path).isSymLink() || !QFileInfo(path).isFile() || digest(path) != value)
				return reject("安装文件缺失或被修改: " + path);
		}
		else if (kind == "L")
		{
			const auto expected = path == cliPath()           ? dataRoot() + "/bin/pointeraccel"
			                      : path == sessionLinkPath() ? sessionUnitPath()
			                                                  : QString();
			if (expected.isEmpty() || value != expected || !QFileInfo(path).isSymLink() ||
			    QFileInfo(path).symLinkTarget() != expected)
				return reject("安装入口冲突: " + path);
		}
		else
			return reject("安装清单包含未知记录。");
	}
	for (const auto& required : {dataRoot() + "/bin/pointeraccel",
	         stateRoot() + "/owner",
	         stateRoot() + "/system.sha256",
	         stateRoot() + "/backups/kwin.json",
	         cliPath()})
		if (!entries.contains(required))
			return reject("安装清单缺少必要记录: " + required);
	return true;
}

}
