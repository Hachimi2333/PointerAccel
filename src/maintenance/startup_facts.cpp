#include "startup_facts.hpp"
#include "installation.hpp"
#include "context.hpp"
#include "platform/paths.hpp"
#include "platform/json_io.hpp"
#include <QDBusInterface>
#include <QDBusReply>
#include <QFileInfo>
#include <QRegularExpression>
#include <KConfig>
#include <KConfigGroup>
#include <unistd.h>
namespace pa
{
StartupFacts collectStartupFacts()
{
	using namespace maintenance;
	StartupFacts facts;
	facts.uid = int(getuid());
	facts.installed = QFileInfo::exists(manifestPath());
	facts.modulePresent = QFileInfo::exists(installedPluginPath());
	if (!facts.installed)
		return facts;
	verifyInstallation(&facts.installationError);
	facts.transactionPending =
	    QFileInfo::exists(systemRecordRoot() + "/pending") || QFileInfo::exists(stateRoot() + ".transaction.json");
	const auto recordPath = systemRecordRoot() + "/compatibility.json";
	if (!testing())
		for (const auto& info :
		    {QFileInfo(recordPath), QFileInfo(installedPluginPath()), QFileInfo(systemRecordRoot())})
			if (!info.exists() || info.isSymLink() || info.ownerId() != 0 ||
			    (info.permissions() & (QFileDevice::WriteGroup | QFileDevice::WriteOther)))
				facts.systemTrusted = false;
	if (!facts.installationError.isEmpty() || facts.transactionPending || !facts.systemTrusted)
		return facts;
	facts.record = readJson(recordPath, &facts.recordError);
	if (evaluateStartup(facts).installedBuild.isEmpty())
		return facts;
	facts.expectedModuleDigest = readText(stateRoot() + "/system.sha256");
	facts.moduleDigest = digest(installedPluginPath());
	facts.recordDigest = digest(recordPath);
	facts.ownerRecord = readText(systemRecordRoot() + "/owner.tsv");
	facts.directAutostart =
	    KConfig(kwinConfigPath(), KConfig::SimpleConfig).group("Plugins").readEntry("pointeraccel_kwinEnabled", false);
	facts.serviceConfigured = QFileInfo::exists(sessionUnitPath()) && QFileInfo(sessionLinkPath()).isSymLink();
	// Do not query packages, hash KWin, or contact the compositor until local gates pass.
	// The pure evaluator owns the ordering and meaning of all startup decisions.
	if (evaluateStartup(facts).installedVersion == std::nullopt)
		return facts;
	facts.version = installedVersion();
	facts.libraryPath = QFileInfo("/usr/lib64/libkwin.so.6").canonicalFilePath();
	if (!supportedKWinVersion(facts.version))
		return facts;
	facts.libraryDigest = digest(facts.libraryPath);
	facts.executableDigest = digest("/usr/bin/kwin_wayland");
	if (!evaluateStartup(facts).compatible)
		return facts;
	facts.kwinOwner = owner();
	const auto stamp = readJson(stateRoot() + "/session.json", &facts.stampError);
	facts.sameSession = sameSession(stamp, facts.kwinOwner);
	if (!facts.stampError.isEmpty() || facts.sameSession || facts.kwinOwner.isEmpty())
		return facts;
	QDBusInterface kwin(facts.kwinOwner, "/KWin", "org.kde.KWin");
	kwin.setTimeout(3000);
	const QDBusReply<QString> info = kwin.call("supportInformation");
	const auto match = QRegularExpression("KWin version: ([^\\n]+)").match(info.isValid() ? info.value() : QString());
	if (match.hasMatch())
		facts.runningVersion = match.captured(1).trimmed();
	return facts;
}
}
