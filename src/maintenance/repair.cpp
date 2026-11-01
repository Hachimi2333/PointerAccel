#include "repair.hpp"
#include "installation.hpp"
#include "context.hpp"
#include "platform/paths.hpp"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDebug>

namespace pa
{
using namespace maintenance;
int repair(const QString& source)
{
	QString error;
	if (!verifyInstallation(&error))
	{
		qCritical().noquote() << error;
		return 1;
	}
	QTemporaryDir temporary;
	if (!temporary.isValid())
		return 1;
	QString sourceRoot = source;
	if (sourceRoot.isEmpty())
	{
		QProcess unpack;
		unpack.start("python3",
		    {dataRoot() + "/scripts/maintenance.py", "extract", dataRoot() + "/source.tar.gz", temporary.path()});
		if (!unpack.waitForFinished(30000) || unpack.exitCode() != 0)
		{
			qCritical().noquote() << unpack.readAllStandardError();
			return 1;
		}
		sourceRoot = temporary.path();
	}
	sourceRoot = QFileInfo(sourceRoot).canonicalFilePath();
	if (sourceRoot.isEmpty() || !QFileInfo::exists(sourceRoot + "/install.sh"))
	{
		qCritical() << "源码目录缺少 install.sh。";
		return 1;
	}
	const auto cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/pointeraccel";
	if (!QDir().mkpath(cache))
		return 1;
	QTemporaryDir build(cache + "/repair-XXXXXX");
	if (!build.isValid())
		return 1;
	QStringList args{sourceRoot + "/install.sh", "--build-dir", build.path()};
#ifdef PA_TESTING
	if (testing())
	{
		const auto root = qEnvironmentVariable("POINTERACCEL_TEST_SYSTEM_ROOT");
		if (!root.isEmpty())
			args << "--test-system" << root;
		const auto reuse = qEnvironmentVariable("POINTERACCEL_TEST_BUILD_DIR");
		if (!reuse.isEmpty())
			args[2] = reuse;
	}
#endif
	QProcess install;
	install.setProcessChannelMode(QProcess::ForwardedChannels);
	install.setInputChannelMode(QProcess::ForwardedInputChannel);
	install.start("bash", args);
	if (!install.waitForStarted())
		return 1;
	install.waitForFinished(-1);
	return install.exitStatus() == QProcess::NormalExit ? install.exitCode() : 1;
}

}
