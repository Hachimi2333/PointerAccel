#pragma once
#include "config/config.hpp"
#include "protocol.hpp"
#include "transport.hpp"
#include <QDBusConnection>
#include <functional>

namespace pa
{
struct CommitResult
{
	// A successful save remains valid even if the compositor cannot apply it.
	bool saved = false;
	bool applied = false;
	bool conflict = false;
	QString error;
	ConfigSnapshot snapshot;
	QJsonObject response;
};

class ControlClient
{
public:
	// Synchronous API: the GUI calls it only on its dedicated worker thread.
	explicit ControlClient(QDBusConnection connection = QDBusConnection::sessionBus(), int timeoutMs = 5000);
	QJsonObject request(const QString& method, const QVariantList& args, QString* error) const;
	QJsonObject status(const QJsonObject& cachedStartup = {}) const;
	QJsonObject status(const ConfigSnapshot& snapshot, const QJsonObject& cachedStartup) const;
	CommitResult commit(const Config& config, const QString& expectedRevision) const;
	CommitResult update(const std::function<void(Config&)>& edit) const;

private:
	DbusTransport transport;
};
}
