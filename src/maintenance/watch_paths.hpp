#pragma once
#include <QByteArray>
#include <QStringList>
namespace pa
{
QStringList installationWatchPaths();
QByteArray fileAttributes(const QStringList& paths);
}
