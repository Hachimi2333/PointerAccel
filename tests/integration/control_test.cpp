#include "support/control_fixture.hpp"
#include "control/client.hpp"
#include "platform/paths.hpp"
#include "platform/json_io.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
class ControlTest : public QObject, protected ControlFixture
{
	Q_OBJECT
private slots:
	void init()
	{
		const auto error = startPeer();
		QVERIFY2(error.isEmpty(), qPrintable(error));
	}
	void cleanup()
	{
		releasePeer();
	}

	void wholeCommitAndConflict()
	{
		pa::ControlClient control;
		auto snapshot = pa::configuration();
		auto target = snapshot.config;
		target.enabled = false;
		target.trace = true;
		target.settings.pointerSpeed = 17;
		target.settings.enhancePointerPrecision = false;
		target.excluded << "mouse-0123456789abcdef01234567";
		const auto result = control.commit(target, snapshot.revision);
		QVERIFY(result.saved);
		QVERIFY(result.applied);
		QCOMPARE(pa::toJson(pa::configuration().config), pa::toJson(target));
		auto stale = control.commit({}, snapshot.revision);
		QVERIFY(stale.conflict);
		QVERIFY(!stale.saved);
		QCOMPARE(pa::configuration().config.settings.pointerSpeed, 17);
		// CLI-style edits preserve unrelated fields under the same lock.
		QVERIFY(control
		        .update(
		            [](pa::Config& c)
		            {
			            c.settings.pointerSpeed = 8;
		            })
		        .saved);
		auto current = pa::configuration().config;
		QCOMPARE(current.trace, true);
		QCOMPARE(current.excluded, target.excluded);
	}

	void failuresAndTimeout()
	{
		pa::ControlClient control;
		const auto revision = pa::configuration().revision;
		QLockFile held(pa::configRoot() + "/config.lock");
		QVERIFY(held.tryLock());
		QVERIFY(!control.commit({}, revision).saved);
		held.unlock();
		QString error;
		control.request("SetFailure", {true}, &error);
		QVERIFY(error.isEmpty());
		auto target = pa::configuration().config;
		target.settings.pointerSpeed = 12;
		const auto failed = control.commit(target, revision);
		QVERIFY(failed.saved);
		QVERIFY(!failed.applied);
		QVERIFY(!failed.error.isEmpty());
		pa::ControlClient shortTimeout(QDBusConnection::sessionBus(), 50);
		shortTimeout.request("NoReply", {}, &error);
		QVERIFY(!error.isEmpty());
		shortTimeout.request("InvalidReply", {}, &error);
		QVERIFY(!error.isEmpty());
		peer.terminate();
		QVERIFY(peer.waitForFinished());
		target.settings.pointerSpeed = 13;
		auto offline = control.commit(target, pa::configuration().revision);
		QVERIFY(offline.saved);
		QVERIFY(!offline.applied);
		QCOMPARE(pa::configuration().config.settings.pointerSpeed, 13);
		// A damaged file cannot be silently replaced with defaults.
		QFile file(pa::configPath());
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("broken");
		file.close();
		QVERIFY(!control.commit({}, pa::configuration().revision).saved);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll(), QByteArray("broken"));
		file.close();
		QVERIFY(QFile::remove(pa::configPath()));
		QVERIFY(QDir().mkdir(pa::configPath()));
		QVERIFY(!control.commit({}, pa::configuration().revision).saved);
	}
};
QTEST_GUILESS_MAIN(ControlTest)
#include "control_test.moc"
