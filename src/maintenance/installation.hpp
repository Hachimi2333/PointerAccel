#pragma once
#include <QString>
namespace pa
{
QString systemRecordRoot();
QString installedPluginPath();
QString sessionUnitPath();
QString sessionLinkPath();
QString maintenanceLockPath();
bool verifyInstallation(QString* error);
}
