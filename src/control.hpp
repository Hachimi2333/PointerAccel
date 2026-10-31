#pragma once
#include "common.hpp"
#include <QDBusConnection>
#include <functional>

namespace pa
{
struct ConfigSnapshot
{
	Config config;
	// Exact file digest (or "missing"), checked again while holding the write lock.
	QString revision;
	QString error;
};

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

ConfigSnapshot configuration();

class ControlClient
{
public:
	// Synchronous API: the GUI calls it only on its dedicated worker thread.
	explicit ControlClient(QDBusConnection connection = QDBusConnection::sessionBus(), int timeoutMs = 5000);
	QJsonObject request(const QString& method, const QVariantList& args, QString* error) const;
	QJsonObject status(const QJsonObject& cachedStartup = {}) const;
	CommitResult commit(const Config& config, const QString& expectedRevision) const;
	CommitResult update(const std::function<void(Config&)>& edit) const;

private:
	QDBusConnection bus;
	int timeout;
};
}
