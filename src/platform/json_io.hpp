#pragma once
#include <QJsonObject>
namespace pa
{
QString digest(const QString& path);
QJsonObject readJson(const QString& path, QString* error = nullptr);
bool writeJson(const QString& path, const QJsonObject& json, QString* error = nullptr);
}
