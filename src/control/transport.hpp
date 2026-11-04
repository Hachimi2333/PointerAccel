#pragma once
#include <QDBusConnection>
#include <QJsonObject>
namespace pa
{
class DbusTransport
{
public:
	explicit DbusTransport(QDBusConnection connection, int timeoutMs);
	QJsonObject request(const QString& method, const QVariantList& args, QString* error) const;

private:
	QDBusConnection bus;
	int timeout;
};
}
