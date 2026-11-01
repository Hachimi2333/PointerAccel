#pragma once
#include "control/client.hpp"
#include <QJsonArray>
#include <optional>
namespace pa
{
enum class Page
{
	Settings,
	Devices,
	Diagnostics
};
struct DeviceInfo
{
	QString id, name;
	bool eligible = false, connected = true, accelerated = false, excluded = false;
	bool operator==(const DeviceInfo&) const = default;
};
struct GuiSnapshot
{
	ConfigSnapshot configuration;
	QJsonObject status;
	std::optional<QList<DeviceInfo>> devices;
	std::optional<QJsonObject> diagnostics;
	QJsonArray devicePayload;
	QString devicesError, diagnosticsError;
	QStringList paths;
};
struct GuiResult
{
	GuiSnapshot snapshot;
	std::optional<CommitResult> commit;
	QString message;
};
}
