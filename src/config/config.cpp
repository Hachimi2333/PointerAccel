#include "config.hpp"
#include "platform/paths.hpp"
#include "platform/json_io.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
namespace pa
{
QJsonObject toJson(const Config& c)
{
	return {{"enabled", c.enabled},
	    {"pointer_speed", c.settings.pointerSpeed},
	    {"enhance_pointer_precision", c.settings.enhancePointerPrecision},
	    {"excluded_devices", QJsonArray::fromStringList(c.excluded)},
	    {"trace", c.trace}};
}

Config parseConfig(const QJsonObject& json, QString* error)
{
	Config c;
	QString why;
	const auto speed = json.value("pointer_speed");
	if (!json.value("enabled").isBool() || !speed.isDouble() || speed.toDouble() != speed.toInt() ||
	    speed.toInt() < 1 || speed.toInt() > 20 || !json.value("enhance_pointer_precision").isBool() ||
	    !json.value("excluded_devices").isArray() || !json.value("trace").isBool())
		why = "配置字段类型或取值不正确。";
	for (const auto& id : json.value("excluded_devices").toArray())
	{
		if (!id.isString() || id.toString().isEmpty())
			why = "设备排除列表包含无效标识。";
		else
			c.excluded.append(id.toString());
	}

	if (!why.isEmpty())
	{
		c.enabled = false;
		if (error)
			*error = why;
		return c;
	}
	c.enabled = json["enabled"].toBool();
	c.settings.pointerSpeed = static_cast<std::uint8_t>(json["pointer_speed"].toInt());
	c.settings.enhancePointerPrecision = json["enhance_pointer_precision"].toBool();
	c.trace = json["trace"].toBool();
	return c;
}

ConfigSnapshot configuration()
{
	ConfigSnapshot result;
	QFile file(configPath());
	if (QFileInfo(file).isSymLink())
	{
		result.error = "配置不能是符号链接。";
		result.config.enabled = false;
		return result;
	}
	if (!file.exists())
	{
		result.revision = "missing";
		return result;
	}
	if (!file.open(QIODevice::ReadOnly))
	{
		result.error = file.errorString();
		result.config.enabled = false;
		return result;
	}
	const auto bytes = file.readAll();
	result.revision = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(bytes, &parse);
	if (parse.error != QJsonParseError::NoError || !document.isObject())
	{
		result.error = "无效 JSON: " + parse.errorString();
		result.config.enabled = false;
		return result;
	}
	result.config = parseConfig(document.object(), &result.error);
	return result;
}

Config readConfig(QString* error)
{
	const auto snapshot = configuration();
	if (error)
		*error = snapshot.error;
	return snapshot.config;
}
bool writeConfig(const Config& config, QString* error)
{
	if (QFileInfo(configPath()).isSymLink())
	{
		if (error)
			*error = "配置不能是符号链接。";
		return false;
	}
	return writeJson(configPath(), toJson(config), error);
}

}
