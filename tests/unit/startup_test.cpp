#include "maintenance/startup_facts.hpp"
#include "control/status.hpp"
#include <gtest/gtest.h>
namespace
{
pa::StartupFacts ready()
{
	pa::StartupFacts f;
	f.installed = f.modulePresent = f.serviceConfigured = true;
	f.uid = 1000;
	f.expectedModuleDigest = QString(64, 'a');
	f.moduleDigest = f.expectedModuleDigest;
	f.recordDigest = QString(64, 'b');
	f.ownerRecord = "1000\t" + f.moduleDigest + "\t" + f.recordDigest;
	f.version = f.runningVersion = "6.8.2";
	f.libraryPath = "/libkwin.so.6";
	f.libraryDigest = "library-hash";
	f.executableDigest = "exe-hash";
	f.kwinOwner = ":1.42";
	f.record = {{"schema", 1},
	    {"owner", f.uid},
	    {"kwin_version", f.version},
	    {"kwin_iid", "org.kde.kwin.PluginFactoryInterface6.8.2"},
	    {"module_sha256", f.moduleDigest},
	    {"kwin_library", f.libraryPath},
	    {"kwin_executable", "/usr/bin/kwin_wayland"},
	    {"kwin_sha256", f.libraryDigest},
	    {"executable_sha256", f.executableDigest},
	    {"module_build_id", "installed"}};
	return f;
}
}
TEST(StartupPolicy, KeepsGatesOrderedAndSerializesExistingStates)
{
	EXPECT_EQ(pa::evaluateStartup({}).state, pa::StartupState::NotInstalled);
	auto f = ready();
	EXPECT_EQ(pa::evaluateStartup(f).state, pa::StartupState::Ready);
	f.installationError = "manifest failure";
	f.transactionPending = true;
	EXPECT_EQ(pa::evaluateStartup(f).error, "manifest failure");
	f.installationError.clear();
	EXPECT_EQ(pa::evaluateStartup(f).state, pa::StartupState::Blocked);
	f = ready();
	f.version = "6.9.0";
	EXPECT_EQ(pa::evaluateStartup(f).state, pa::StartupState::Unsupported);
	f = ready();
	f.libraryDigest = "changed";
	EXPECT_EQ(pa::evaluateStartup(f).state, pa::StartupState::RepairRequired);
	EXPECT_TRUE(pa::evaluateStartup(f).repairRequired);
	f = ready();
	f.sameSession = true;
	EXPECT_EQ(pa::evaluateStartup(f).toJson()["startup_state"], "pending-login");
	f = ready();
	f.kwinOwner.clear();
	EXPECT_EQ(pa::evaluateStartup(f).state, pa::StartupState::PendingKWin);
	f = ready();
	f.runningVersion = "6.7.5";
	EXPECT_TRUE(pa::evaluateStartup(f).reloginRequired);
}
TEST(StartupPolicy, DoesNotTreatMissingBinaryFingerprintsAsCompatibility)
{
	auto f = ready();
	f.record.remove("kwin_sha256");
	EXPECT_EQ(pa::evaluateStartup(f).state, pa::StartupState::RepairRequired);
}
TEST(StatusComposition, UsesSuppliedConfigAndKeepsActiveRuntimeDuringDiskUpgrade)
{
	const auto facts = ready();
	auto startup = pa::evaluateStartup(facts).toJson();
	pa::ConfigSnapshot config;
	config.config.enabled = false;
	QJsonObject runtime{{"loaded", true},
	    {"active", true},
	    {"healthy", true},
	    {"running_compatible", true},
	    {"running_build", QJsonObject{{"image_build_id", "installed"}}}};
	auto status = pa::composeStatus(config, startup, runtime, {}, true, false, true);
	EXPECT_FALSE(status["configured_enabled"].toBool());
	EXPECT_TRUE(status["healthy"].toBool());
	startup["startup_state"] = "repair-required";
	startup["repair_required"] = true;
	status = pa::composeStatus(config, startup, runtime, {}, true, false, true);
	EXPECT_TRUE(status["active"].toBool());
	EXPECT_TRUE(status["running_compatible"].toBool());
	EXPECT_FALSE(status["healthy"].toBool());
	EXPECT_TRUE(status["repair_required"].toBool());
}
