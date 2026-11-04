#pragma once
#include <QJsonObject>
#include <QString>
#include <optional>
namespace pa
{
enum class StartupState
{
	Blocked,
	NotInstalled,
	Unsupported,
	RepairRequired,
	PendingLogin,
	PendingKWin,
	Ready
};
struct StartupFacts
{
	bool installed = false, modulePresent = false, systemTrusted = true, transactionPending = false;
	QString installationError, recordError;
	QJsonObject record;
	int uid = -1;
	QString expectedModuleDigest, moduleDigest, recordDigest, ownerRecord;
	bool directAutostart = false, serviceConfigured = false;
	QString version, libraryPath, libraryDigest, executableDigest;
	QString kwinOwner, stampError, runningVersion;
	bool sameSession = false;
};
struct StartupResult
{
	StartupState state = StartupState::Blocked;
	QString error;
	bool compatible = false, repairRequired = false, reloginRequired = false;
	bool modulePresent = false, moduleValid = false;
	QJsonObject installedBuild;
	std::optional<QString> installedVersion, kwinOwner;
	QJsonObject toJson() const;
};
bool supportedKWinVersion(const QString& version);
StartupFacts collectStartupFacts();
StartupResult evaluateStartup(const StartupFacts& facts);
}
