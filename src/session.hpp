#pragma once
#include <QJsonObject>
#include <QString>

namespace pa
{
QString systemRecordRoot();
QString installedPluginPath();
QString sessionUnitPath();
QString sessionLinkPath();
QString maintenanceLockPath();
bool verifyInstallation(QString* error);
QJsonObject startupStatus();
QJsonObject sessionStamp();
int watchSession();
int repair(const QString& source);
}
