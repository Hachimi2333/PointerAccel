#include "maintenance/installation.hpp"
#include "watch_paths.hpp"
#include "startup.hpp"
#include "platform/paths.hpp"
#include <QFile>
#include <sys/stat.h>
namespace pa
{
QStringList installationWatchPaths()
{
	QStringList paths{configPath(),
	    manifestPath(),
	    stateRoot() + "/session.json",
	    kwinConfigPath(),
	    sessionUnitPath(),
	    sessionLinkPath(),
	    installedPluginPath(),
	    systemRecordRoot(),
	    systemRecordRoot() + "/compatibility.json",
	    systemRecordRoot() + "/owner.tsv",
	    systemRecordRoot() + "/pending",
	    stateRoot() + ".transaction.json",
	    "/usr/bin/kwin_wayland",
	    "/usr/lib64/libkwin.so.6",
	    "/usr/lib/sysimage/rpm/rpmdb.sqlite"};
	QFile manifest(manifestPath());
	if (manifest.open(QIODevice::ReadOnly))
		for (const auto& line : manifest.readAll().split('\n'))
		{
			const auto fields = line.split('\t');
			if (fields.size() == 3 && (fields[0] == "F" || fields[0] == "L"))
				paths << QString::fromUtf8(fields[2]);
		}
	paths.removeDuplicates();
	return paths;
}
QByteArray fileAttributes(const QStringList& paths)
{
	QByteArray attributes;
	for (const auto& path : paths)
	{
		struct stat s{};
		const bool exists = ::stat(path.toLocal8Bit().constData(), &s) == 0;
		attributes += path.toUtf8() + ':' + QByteArray::number(exists) + ':' + QByteArray::number(s.st_ino) + ':' +
		              QByteArray::number(s.st_size) + ':' + QByteArray::number(s.st_mtim.tv_sec) + ':' +
		              QByteArray::number(s.st_mtim.tv_nsec) + ':' + QByteArray::number(s.st_ctim.tv_sec) + ':' +
		              QByteArray::number(s.st_ctim.tv_nsec) + ';';
	}
	return attributes;
}
}
