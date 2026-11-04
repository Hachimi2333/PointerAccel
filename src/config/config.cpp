#include "config.hpp"
#include <QJsonArray>
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

}
