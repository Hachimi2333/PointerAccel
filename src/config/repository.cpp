#include "repository.hpp"
#include "platform/paths.hpp"
#include "platform/json_io.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
namespace pa
{
ConfigRepository::ConfigRepository(QString path) : path(std::move(path))
{
}
ConfigSnapshot ConfigRepository::snapshot() const
{
	ConfigSnapshot result;
	QFile file(path);
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

bool ConfigRepository::write(const Config& config, QString* error) const
{
	if (QFileInfo(path).isSymLink())
	{
		if (error)
			*error = "配置不能是符号链接。";
		return false;
	}
	return writeJson(path, toJson(config), error);
}

ConfigRepository::Transaction::Transaction(const ConfigRepository& repository)
    : repository(repository), lock(QFileInfo(repository.path).path() + "/config.lock")
{
	if (!QDir().mkpath(QFileInfo(repository.path).path()))
		error = "无法建立配置目录。";
	else if (!lock.tryLock(0))
		error = "另一个进程正在保存配置，请稍后重试。";
}
SaveResult ConfigRepository::Transaction::save(
    const std::function<void(Config&)>& edit, const QString* expectedRevision)
{
	SaveResult result;
	if (!error.isEmpty())
	{
		result.error = error;
		return result;
	}
	result.snapshot = repository.snapshot();
	if (!result.snapshot.error.isEmpty())
	{
		result.error = result.snapshot.error;
		return result;
	}
	if (expectedRevision && result.snapshot.revision != *expectedRevision)
	{
		result.conflict = true;
		result.error = "配置已被外部修改。请重新加载后再编辑。";
		return result;
	}
	auto config = result.snapshot.config;
	edit(config);
	parseConfig(toJson(config), &result.error);
	if (!result.error.isEmpty() || !repository.write(config, &result.error))
		return result;
	result.saved = true;
	result.snapshot = repository.snapshot();
	return result;
}
ConfigSnapshot configuration()
{
	return ConfigRepository(configPath()).snapshot();
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
	return ConfigRepository(configPath()).write(config, error);
}
}
