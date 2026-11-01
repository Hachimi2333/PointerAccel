#include "json_io.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
namespace pa
{
QString digest(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file))
		return {};
	return QString::fromLatin1(hash.result().toHex());
}

QJsonObject readJson(const QString& path, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		if (error)
			*error = file.errorString();
		return {};
	}
	QJsonParseError parse;
	auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
	if (parse.error != QJsonParseError::NoError || !doc.isObject())
	{
		if (error)
			*error = "无效 JSON: " + parse.errorString();
		return {};
	}
	return doc.object();
}

bool writeJson(const QString& path, const QJsonObject& json, QString* error)
{
	if (!QDir().mkpath(QFileInfo(path).absolutePath()))
	{
		if (error)
			*error = "无法建立目录: " + path;
		return false;
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
	{
		if (error)
			*error = file.errorString();
		return false;
	}
	file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	const auto bytes = QJsonDocument(json).toJson();
	if (file.write(bytes) != bytes.size() || !file.commit())
	{
		if (error)
			*error = file.errorString();
		return false;
	}
	return true;
}

}
