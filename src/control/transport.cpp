#include "transport.hpp"
#include "protocol.hpp"
#include <QDBusMessage>
#include <QJsonDocument>
namespace pa
{
DbusTransport::DbusTransport(QDBusConnection connection, int timeoutMs) : bus(std::move(connection)), timeout(timeoutMs)
{
}
QJsonObject DbusTransport::request(const QString& method, const QVariantList& args, QString* error) const
{
	if (error)
		error->clear();
	auto message = QDBusMessage::createMethodCall("org.kde.KWin", dbusPath, dbusInterface, method);
	message.setAutoStartService(false);
	message.setArguments(args);
	const auto reply = bus.call(message, QDBus::Block, timeout);
	if (reply.type() == QDBusMessage::ErrorMessage)
	{
		if (error)
			*error = reply.errorName() == "org.freedesktop.DBus.Error.UnknownObject" ||
			                 reply.errorName() == "org.freedesktop.DBus.Error.ServiceUnknown"
			             ? QStringLiteral("KWin 模块未加载。首次安装后请重新登录 Plasma。")
			             : reply.errorMessage();
		return {};
	}
	const auto doc = QJsonDocument::fromJson(reply.arguments().value(0).toString().toUtf8());
	if (!doc.isObject())
	{
		if (error)
			*error = "模块返回的数据无效。";
		return {};
	}
	return doc.object();
}

}
