#pragma once
#include "config/config.hpp"
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <memory>
#include <optional>

// Shared setup only. Tests assert protocol behavior or widget behavior independently.
class ControlFixture
{
protected:
	std::unique_ptr<QTemporaryDir> home;
	QProcess peer;
	QList<QPair<QByteArray, std::optional<QByteArray>>> environment;

	QString startPeer()
	{
		home = std::make_unique<QTemporaryDir>();
		if (!home->isValid())
			return "Cannot create isolated HOME";
		for (const auto& pair : QList<QPair<QByteArray, QByteArray>>{{"HOME", ""},
		         {"XDG_CONFIG_HOME", "/config"},
		         {"XDG_DATA_HOME", "/data"},
		         {"XDG_STATE_HOME", "/state"},
		         {"XDG_CACHE_HOME", "/cache"}})
		{
			environment.append({pair.first,
			    qEnvironmentVariableIsSet(pair.first.constData())
			        ? std::optional<QByteArray>(qgetenv(pair.first.constData()))
			        : std::nullopt});
			qputenv(pair.first, home->path().toUtf8() + pair.second);
		}
		QString error;
		if (!pa::writeConfig({}, &error))
			return error;
		peer.start(QCoreApplication::applicationDirPath() + "/pa_control_peer", QStringList{});
		if (!peer.waitForStarted() || !peer.waitForReadyRead() || peer.readLine() != "READY\n")
			return "Control peer did not become ready: " + peer.errorString();
		return {};
	}
	void releasePeer()
	{
		if (peer.state() != QProcess::NotRunning)
		{
			peer.terminate();
			if (!peer.waitForFinished(3000))
			{
				peer.kill();
				peer.waitForFinished(3000);
			}
		}
		home.reset();
		for (const auto& pair : environment)
			if (pair.second)
				qputenv(pair.first, *pair.second);
			else
				qunsetenv(pair.first);
		environment.clear();
	}
	~ControlFixture()
	{
		releasePeer();
	}
};
