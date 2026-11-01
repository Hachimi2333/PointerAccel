#include "session_watch.hpp"
#include "startup.hpp"
#include "installation.hpp"
#include "context.hpp"
#include "watch_paths.hpp"
#include "platform/paths.hpp"
#include "platform/json_io.hpp"
#include "control/protocol.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QTimer>
#include <QDebug>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pa
{
using namespace maintenance;
namespace
{
class Lock
{
public:
	Lock()
	{
		fd = open(maintenanceLockPath().toLocal8Bit().constData(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
		struct stat s{};
		if (fd < 0)
			return;
		if (fstat(fd, &s) || s.st_uid != getuid() || !S_ISREG(s.st_mode) || flock(fd, LOCK_EX | LOCK_NB))
		{
			close(fd);
			fd = -1;
		}
	}

	~Lock()
	{
		if (fd >= 0)
			close(fd);
	}

	bool valid() const
	{
		return fd >= 0;
	}

private:
	int fd = -1;
};

}
int watchSession()
{
	QFileSystemWatcher files;
	QTimer poll, debounce;
	poll.setInterval(30000);
	debounce.setSingleShot(true);
	debounce.setInterval(300);
	QDBusServiceWatcher kwin("org.kde.KWin", QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange);
	QByteArray lastAttributes;
	QJsonObject cached;
	QString attempted;
	auto inspect = [&]
	{
		Lock lock;
		if (!lock.valid())
			return;
		const auto paths = installationWatchPaths();
		const auto attributes = owner().toUtf8() + fileAttributes(paths);
		for (const auto& path : paths)
			for (const auto& watch : {path, QFileInfo(path).path()})
				if (QFileInfo::exists(watch) && !files.files().contains(watch) && !files.directories().contains(watch))
					files.addPath(watch);

		if (attributes != lastAttributes)
		{
			cached = startupStatus();
			lastAttributes = attributes;
		}
		const auto checkedOwner = cached["kwin_owner"].toString();
		if (cached["startup_state"] == "ready" && !checkedOwner.isEmpty() && owner() == checkedOwner &&
		    attempted != checkedOwner)
		{
			attempted = checkedOwner;
			QDBusInterface plugins(checkedOwner, "/Plugins", "org.kde.KWin.Plugins");
			plugins.setTimeout(3000);
			const auto loaded = plugins.property("LoadedPlugins").toStringList();
			bool ok = loaded.contains("pointeraccel_kwin");
			if (!ok)
			{
				const QDBusReply<bool> reply = plugins.call("LoadPlugin", "pointeraccel_kwin");
				ok = reply.isValid() && reply.value();
			}
			if (!ok)
			{
				cached["startup_state"] = "load-failed";
				cached["error"] = "模块加载失败，请运行 pointeraccel doctor 查看诊断；不会自动重试当前 KWin。";
			}
			else
			{
				QDBusInterface module(checkedOwner, dbusPath, dbusInterface);
				module.setTimeout(3000);
				const QDBusReply<QString> info = module.call("Info"), state = module.call("Status");
				const auto build =
				    QJsonDocument::fromJson(info.isValid() ? info.value().toUtf8() : QByteArray()).object();
				const auto runtime =
				    QJsonDocument::fromJson(state.isValid() ? state.value().toUtf8() : QByteArray()).object();
				if (build["image_build_id"] != cached["installed_build"].toObject()["module_build_id"])
				{
					cached["startup_state"] = "pending-login";
					cached["error"] = "当前仍驻留旧模块，请重新登录。";
				}
				else if (!runtime["healthy"].toBool())
				{
					cached["startup_state"] = "load-failed";
					cached["error"] = runtime["error"].toString("模块未正常接入，请运行 pointeraccel doctor。");
				}
			}
		}
		const auto error = cached["error"].toString();
		if (!error.isEmpty() && cached["startup_state"] != "pending-kwin")
		{
			const QJsonObject notification{{"state", cached["startup_state"]},
			    {"error", error},
			    {"build", cached["installed_build"]},
			    {"kwin", cached["installed_kwin_version"]}};
			const auto key = QString::fromLatin1(QCryptographicHash::hash(
			    QJsonDocument(notification).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
			        .toHex());
			const auto previous = readJson(stateRoot() + "/notification.json");
			if (previous["key"].toString() != key)
			{
				auto message = QDBusMessage::createMethodCall("org.freedesktop.Notifications",
				    "/org/freedesktop/Notifications",
				    "org.freedesktop.Notifications",
				    "Notify");
				message.setAutoStartService(false);
				message.setArguments(
				    {"PointerAccel", uint(0), "input-mouse", "PointerAccel", error, QStringList(), QVariantMap(), -1});
				const QDBusReply<uint> reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 1000);
				if (reply.isValid())
					writeJson(stateRoot() + "/notification.json", {{"key", key}});
				qWarning().noquote() << error;
			}
		}
	};
	QObject::connect(&files,
	    &QFileSystemWatcher::fileChanged,
	    &debounce,
	    [&]
	    {
		    lastAttributes.clear();
		    debounce.start();
	    });
	QObject::connect(&files,
	    &QFileSystemWatcher::directoryChanged,
	    &debounce,
	    [&]
	    {
		    debounce.start();
	    });
	QObject::connect(&kwin,
	    &QDBusServiceWatcher::serviceOwnerChanged,
	    &debounce,
	    [&]
	    {
		    lastAttributes.clear();
		    debounce.start();
	    });
	QObject::connect(&debounce, &QTimer::timeout, &debounce, inspect);
	QObject::connect(&poll, &QTimer::timeout, &poll, inspect);
	poll.start();
	QTimer::singleShot(0, &debounce, inspect);
	return QCoreApplication::exec();
}
}
