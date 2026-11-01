#pragma once
#include "core/pointer_engine.hpp"
#include <QJsonObject>
#include <QStringList>
namespace pa
{
struct Config
{
	bool enabled = true;
	PointerSettings settings{};
	QStringList excluded;
	bool trace = false;
	bool operator==(const Config&) const = default;
};
struct ConfigSnapshot
{
	Config config;
	// Exact file digest (or "missing"), checked again under the writer lock.
	QString revision;
	QString error;
};
QJsonObject toJson(const Config& config);
Config parseConfig(const QJsonObject& json, QString* error = nullptr);
ConfigSnapshot configuration();
Config readConfig(QString* error = nullptr);
bool writeConfig(const Config& config, QString* error);
}
