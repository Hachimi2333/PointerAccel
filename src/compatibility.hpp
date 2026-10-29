#pragma once
#include <QJsonObject>
#include <QString>

namespace pa {
// Only examines this process. KWin's file capabilities intentionally prevent
// an unprivileged helper from examining /proc/<kwin-pid>/maps or exe.
bool runtimeCompatible(const QJsonObject &expected, QString *error = nullptr);
QJsonObject runtimeIdentity();
QString elfBuildId(const QString &path);
QString imageBuildId(const void *address);
}
