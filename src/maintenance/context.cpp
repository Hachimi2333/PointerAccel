#include "context.hpp"
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

namespace pa::maintenance
{
QString configBase()
{
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

bool testing()
{
#ifdef PA_TESTING
	return qEnvironmentVariable("POINTERACCEL_ISOLATED_TEST") == "1";
#else
	return false;
#endif
}

QString readText(const QString& path)
{
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

QString owner()
{
	auto* interface = QDBusConnection::sessionBus().interface();
	if (!interface)
		return {};
	const auto reply = interface->serviceOwner("org.kde.KWin");
	return reply.isValid() ? reply.value() : QString();
}

QString busId()
{
	QDBusInterface bus("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus");
	const QDBusReply<QString> reply = bus.call("GetId");
	return reply.isValid() ? reply.value() : QString();
}

QString installedVersion()
{
	QProcess rpm;
	rpm.start("rpm", {"-q", "--qf", "%{VERSION}", "kwin"});
	if (!rpm.waitForFinished(3000) || rpm.exitCode() != 0)
		return {};
	return QString::fromUtf8(rpm.readAllStandardOutput()).trimmed();
}

bool sameSession(const QJsonObject& stamp, const QString& kwinOwner)
{
	// A user manager can retain XDG_SESSION_ID across logins. Prefer the
	// checked compositor's bus identity whenever installation observed it.
	if (!stamp["owner"].toString().isEmpty() && !stamp["bus"].toString().isEmpty())
		return !kwinOwner.isEmpty() && stamp["owner"].toString() == kwinOwner && stamp["bus"].toString() == busId();
	const auto session = qEnvironmentVariable("XDG_SESSION_ID");
	return !session.isEmpty() && stamp["session"].toString() == session &&
	       stamp["boot"].toString() == readText("/proc/sys/kernel/random/boot_id");
}

}
