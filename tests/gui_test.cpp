#include "control.hpp"
#include "controller.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLockFile>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class GuiTest : public QObject
{
	Q_OBJECT
	std::unique_ptr<QTemporaryDir> home;
	QProcess peer;

	void ready(pa::GuiController& controller)
	{
		QTRY_VERIFY_WITH_TIMEOUT(controller.valid() && !controller.busy(), 10000);
		controller.setPollingActive(false);
	}

	static void click(QObject* root, const char* name)
	{
		auto* item = root->findChild<QQuickItem*>(name);
		QVERIFY2(item, name);
		QVERIFY2(item->isEnabled(), name);
		QTest::mouseClick(item->window(),
		    Qt::LeftButton,
		    Qt::NoModifier,
		    item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
	}

private slots:

	void init()
	{
		home = std::make_unique<QTemporaryDir>();
		QVERIFY(home->isValid());
		qputenv("HOME", home->path().toUtf8());
		for (const auto& pair : QList<QPair<QByteArray, QByteArray>>{{"XDG_CONFIG_HOME", "/config"},
		         {"XDG_DATA_HOME", "/data"},
		         {"XDG_STATE_HOME", "/state"},
		         {"XDG_CACHE_HOME", "/cache"}})
			qputenv(pair.first, (home->path().toUtf8() + pair.second));
		QString error;
		QVERIFY(pa::writeConfig({}, &error));
		peer.start(QCoreApplication::applicationDirPath() + "/pa_control_peer", QStringList{});
		QVERIFY(peer.waitForStarted());
		QVERIFY(peer.waitForReadyRead());
		QCOMPARE(peer.readLine(), QByteArray("READY\n"));
	}

	void cleanup()
	{
		peer.terminate();
		if (!peer.waitForFinished(3000))
		{
			peer.kill();
			peer.waitForFinished();
		}
		home.reset();
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

	void draftExternalChangesAndRevert()
	{
		pa::GuiController controller;
		ready(controller);
		const auto revision = pa::configuration().revision;
		controller.setValue("pointer_speed", 16);
		controller.setValue("trace", true);
		QVERIFY(controller.dirty());
		controller.setPage(1);
		QTRY_VERIFY(!controller.busy());
		controller.setPage(2);
		QTRY_VERIFY(!controller.busy());
		QCOMPARE(controller.draft()["pointer_speed"].toInt(), 16);
		QCOMPARE(pa::configuration().revision, revision);
		QVERIFY(pa::ControlClient()
		        .update(
		            [](pa::Config& c)
		            {
			            c.settings.pointerSpeed = 7;
		            })
		        .saved);
		controller.refresh();
		QTRY_VERIFY(!controller.busy());
		QVERIFY(controller.conflict());
		QCOMPARE(controller.draft()["pointer_speed"].toInt(), 16);
		controller.revert();
		QTRY_VERIFY(!controller.busy());
		QVERIFY(!controller.dirty());
		QVERIFY(!controller.conflict());
		QCOMPARE(controller.draft()["pointer_speed"].toInt(), 7);
		QVERIFY(pa::ControlClient()
		        .update(
		            [](pa::Config& c)
		            {
			            c.settings.pointerSpeed = 9;
		            })
		        .saved);
		controller.refresh();
		QTRY_VERIFY(!controller.busy());
		QCOMPARE(controller.draft()["pointer_speed"].toInt(), 9);
		controller.refresh(false);
		controller.setValue("pointer_speed", 18);
		QTRY_VERIFY(!controller.busy());
		QCOMPARE(controller.draft()["pointer_speed"].toInt(), 18);
		QVERIFY(controller.dirty());
	}

	void devicesTraceAndExport()
	{
		pa::GuiController controller;
		ready(controller);
		controller.setPage(1);
		QTRY_VERIFY(!controller.busy());
		QCOMPARE(controller.devices().size(), 2);
		const QString mouse = "mouse-0123456789abcdef01234567";
		controller.setExcluded("mouse-fedcba9876543210fedcba98", true);
		QVERIFY(!controller.dirty());
		controller.setExcluded(mouse, true);
		controller.setValue("trace", true);
		controller.apply();
		QTRY_VERIFY(!controller.busy());
		QVERIFY(!controller.dirty());
		QString error;
		pa::ControlClient().request("SetConnected", {false}, &error);
		controller.refresh(false);
		QTRY_VERIFY(!controller.busy());
		QCOMPARE(controller.devices().size(), 2);
		auto disconnected = controller.devices().last().toMap();
		QCOMPARE(disconnected["connected"].toBool(), false);
		controller.setExcluded(mouse, false);
		QVERIFY(controller.dirty());
		controller.setPage(2);
		QTRY_VERIFY(!controller.busy());
		QCOMPARE(controller.diagnostics()["trace_samples"].toInt(), 1);
		const auto path = home->path() + "/report.json";
		controller.exportDiagnostics(QUrl::fromLocalFile(path));
		QTRY_VERIFY(!controller.busy());
		const auto report = pa::readJson(path, &error);
		QVERIFY(error.isEmpty());
		QCOMPARE(report["configuration"].toObject()["excluded_devices"].toArray().size(), 1);
		QCOMPARE(report["diagnostics"].toObject()["trace"].toArray().size(), 1);
		QCOMPARE(report["devices"].toArray().size(), 1);
		// Export uses saved configuration, not the pending removal.
		QVERIFY(controller.dirty());
		controller.revert();
		QTRY_VERIFY(!controller.busy());
		controller.setValue("trace", false);
		controller.apply();
		QTRY_VERIFY(!controller.busy());
		QCOMPARE(controller.diagnostics()["trace_samples"].toInt(), 0);
		controller.resetState();
		QTRY_VERIFY(!controller.busy());
		QVERIFY(controller.message().contains("已重置"));
	}

	void corruptConfigurationIsReadOnly()
	{
		QFile file(pa::configPath());
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("{}");
		file.close();
		pa::GuiController controller;
		QTRY_VERIFY_WITH_TIMEOUT(!controller.busy() && !controller.configError().isEmpty(), 10000);
		QVERIFY(!controller.valid());
		controller.setValue("enabled", true);
		QVERIFY(!controller.dirty());
		controller.apply();
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll(), QByteArray("{}"));
	}

	void qmlFlow_data()
	{
		QTest::addColumn<bool>("dark");
		QTest::addColumn<QSize>("size");
		QTest::newRow("light") << false << QSize(840, 700);
		QTest::newRow("dark-compact") << true << QSize(600, 500);
	}

	void qmlFlow()
	{
		QFETCH(bool, dark);
		QFETCH(QSize, size);
		pa::GuiController controller;
		ready(controller);
		QQmlApplicationEngine engine;
		QStringList warnings;
		connect(&engine,
		    &QQmlEngine::warnings,
		    this,
		    [&](const QList<QQmlError>& errors)
		    {
			    for (const auto& error : errors)
				    warnings << error.toString();
		    });
		engine.setInitialProperties({{"controller", QVariant::fromValue(&controller)}});
		engine.load(QUrl("qrc:/pointeraccel/gui/Main.qml"));
		QCOMPARE(engine.rootObjects().size(), 1);
		auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
		QVERIFY(window);
		window->resize(size);
		QTest::qWait(150);
		if (dark)
		{
			// The offscreen platform has no desktop theme. Inject the dark palette
			// on the root control, as a platform theme would, and check inheritance.
			auto* palette = window->property("palette").value<QObject*>();
			QVERIFY(palette);
			for (const auto& role : QList<QByteArray>{"window", "button", "alternateBase"})
				palette->setProperty(role, QColor("#292929"));
			palette->setProperty("base", QColor("#1c1c1c"));
			for (const auto& role : QList<QByteArray>{"windowText", "buttonText", "text"})
				palette->setProperty(role, QColor("#eeeeee"));
			palette->setProperty("placeholderText", QColor("#aaaaaa"));
			QVERIFY(palette->property("window").value<QColor>().lightness() < 128);
			QTest::qWait(50);
		}
		const auto* apply = window->findChild<QQuickItem*>("applyButton");
		QVERIFY(apply);
		QVERIFY(apply->mapToScene(QPointF(0, apply->height())).y() <= window->height());
		click(window, "enabledSwitch");
		QVERIFY(controller.dirty());
		QVERIFY(pa::readConfig().enabled);
		auto* slider = window->findChild<QQuickItem*>("speedSlider");
		QVERIFY(slider);
		slider->forceActiveFocus();
		QTest::keyClick(window, Qt::Key_Right);
		QCOMPARE(controller.draft()["pointer_speed"].toInt(), 11);
		click(window, "devicesTab");
		QTRY_VERIFY(!controller.busy());
		QCOMPARE(controller.page(), 1);
		QVERIFY(controller.dirty());
		click(window, "diagnosticsTab");
		QTRY_VERIFY(!controller.busy());
		QCOMPARE(controller.page(), 2);
		const auto qa = QCoreApplication::applicationDirPath() + "/gui-qa";
		QVERIFY(QDir().mkpath(qa));
		const auto suffix = QString(QTest::currentDataTag()) + "-" + QString::number(window->devicePixelRatio());
		QTest::qWait(100);
		QVERIFY(window->grabWindow().save(qa + "/diagnostics-" + suffix + ".png"));
		click(window, "settingsTab");
		QTRY_VERIFY(!controller.busy());
		QTest::qWait(100);
		QVERIFY(window->grabWindow().save(qa + "/settings-" + suffix + ".png"));
		// The close event opens a dialog and cancel preserves the draft.
		window->close();
		QTest::qWait(30);
		QVERIFY(window->isVisible());
		auto* dialog = window->findChild<QObject*>("closeDialog");
		QVERIFY(dialog);
		QVERIFY(dialog->property("visible").toBool());
		click(window, "cancelCloseButton");
		QVERIFY(controller.dirty());
		click(window, "applyButton");
		QTRY_VERIFY(!controller.busy());
		QVERIFY(!controller.dirty());
		QVERIFY(!pa::readConfig().enabled);
		window->close();
		QVERIFY(!window->isVisible());
		QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
	}

	void closeActions_data()
	{
		QTest::addColumn<QString>("action");
		QTest::newRow("apply-close") << QString("apply");
		QTest::newRow("discard-close") << QString("discard");
		QTest::newRow("offline-save-keeps-open") << QString("offline");
	}

	void closeActions()
	{
		QFETCH(QString, action);
		pa::GuiController controller;
		ready(controller);
		QQmlApplicationEngine engine;
		engine.setInitialProperties({{"controller", QVariant::fromValue(&controller)}});
		engine.load(QUrl("qrc:/pointeraccel/gui/Main.qml"));
		QCOMPARE(engine.rootObjects().size(), 1);
		auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
		QVERIFY(window);
		QTest::qWait(100);
		controller.setValue("pointer_speed", 12);
		if (action == "offline")
		{
			peer.terminate();
			QVERIFY(peer.waitForFinished());
			controller.refresh();
			QTRY_VERIFY(!controller.busy());
		}
		window->close();
		QTest::qWait(30);
		if (action == "discard")
		{
			QSignalSpy quit(&engine, &QQmlEngine::quit);
			click(window, "discardCloseButton");
			QCOMPARE(quit.count(), 1);
			QCOMPARE(pa::readConfig().settings.pointerSpeed, 10);
		}
		else
		{
			click(window, "applyCloseButton");
			QTRY_VERIFY(!controller.busy());
			QCOMPARE(pa::readConfig().settings.pointerSpeed, 12);
			QVERIFY(!controller.dirty());
			if (action == "offline")
			{
				QVERIFY(window->isVisible());
				QVERIFY(controller.message().contains("尚未应用"));
				window->close();
				QVERIFY(!window->isVisible());
			}
			else
				QTRY_VERIFY(!window->isVisible());
		}
	}
};

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	QQuickStyle::setFallbackStyle("Fusion");
	GuiTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "gui_test.moc"
