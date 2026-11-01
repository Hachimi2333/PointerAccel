#pragma once
#include <QJsonObject>
namespace pa::maintenance
{
QString configBase();
bool testing();
QString readText(const QString& path);
QString owner();
QString busId();
QString installedVersion();
bool sameSession(const QJsonObject& stamp, const QString& kwinOwner);
}
