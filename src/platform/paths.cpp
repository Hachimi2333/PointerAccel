#include "paths.hpp"
#include <QDir>
#include <QStandardPaths>
namespace pa
{
QString dataRoot()
{
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/pointeraccel";
}

QString configRoot()
{
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/pointeraccel";
}

QString stateRoot()
{
	auto base = qEnvironmentVariable("XDG_STATE_HOME");
	if (base.isEmpty())
		base = QDir::homePath() + "/.local/state";
	return base + "/pointeraccel";
}

QString configPath()
{
	return configRoot() + "/config.json";
}

QString cliPath()
{
	return QDir::homePath() + "/.local/bin/pointeraccel";
}

QString manifestPath()
{
	return stateRoot() + "/manifest.tsv";
}

QString pluginPath()
{
	return "/usr/lib64/qt6/plugins/kwin/plugins/pointeraccel_kwin.so";
}

QString kwinConfigPath()
{
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/kwinrc";
}

}
