#include "plugin_config.hpp"
#include "platform/paths.hpp"
#include "platform/json_io.hpp"
#include <QFile>
#include <QSaveFile>
#include <QTextStream>
#include <KConfig>
#include <KConfigGroup>

namespace pa
{
namespace
{
int failure(const QString& why, int code = 1)
{
	QTextStream(stderr) << why << Qt::endl;
	return code;
}
}
int internalPlugin(const QStringList& args)
{
	if (args.size() < 2)
		return failure("缺少内部维护参数。", 2);
	const auto path = pa::kwinConfigPath();
	KConfig config(path, KConfig::SimpleConfig);
	KConfigGroup group(&config, "Plugins");
	constexpr auto key = "pointeraccel_kwinEnabled";
	if (args[1] == "backup" && args.size() == 3)
	{
		QFile original(path);
		const bool existed = original.exists();
		if (existed && !original.open(QIODevice::ReadOnly))
			return failure(original.errorString());
		QJsonObject saved{{"file_existed", existed},
		    {"contents", QString::fromLatin1(original.readAll().toBase64())},
		    {"permissions", int(original.permissions())},
		    {"key_existed", group.hasKey(key)},
		    {"value", group.readEntry(key, QString())}};
		QString why;
		return pa::writeJson(args[2], saved, &why) ? 0 : failure(why);
	}
	if ((args[1] == "enable" || args[1] == "guard") && args.size() == 2)
	{
		group.writeEntry(key, args[1] == "enable");
		if (!config.sync())
			return failure("无法保存 KWin 自启设置。");
		QTextStream(stdout) << pa::digest(path) << '\n';
		return 0;
	}
	if (args[1] == "restore" && args.size() == 4)
	{
		QString why;
		auto saved = pa::readJson(args[2], &why);
		if (!why.isEmpty())
			return failure(why);
		if (pa::digest(path) == args[3])
		{
			if (!saved["file_existed"].toBool())
				return QFile::remove(path) || !QFile::exists(path) ? 0 : failure("无法删除本程序创建的 kwinrc。");
			QSaveFile file(path);
			if (!file.open(QIODevice::WriteOnly))
				return failure(file.errorString());
			const auto bytes = QByteArray::fromBase64(saved["contents"].toString().toLatin1());
			file.setPermissions(QFileDevice::Permissions(saved["permissions"].toInt()));
			if (file.write(bytes) != bytes.size() || !file.commit())
				return failure(file.errorString());
		}
		else
		{
			// Preserve unrelated changes made since installation.
			if (saved["key_existed"].toBool())
				group.writeEntry(key, saved["value"].toString());
			else
				group.deleteEntry(key);
			if (!config.sync())
				return failure("无法恢复 KWin 自启设置。");
		}
		return 0;
	}
	return failure("无效内部维护命令。", 2);
}

}
