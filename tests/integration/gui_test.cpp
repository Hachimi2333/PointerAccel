#include "support/control_fixture.hpp"
#include "control/client.hpp"
#include "controller.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLockFile>
#include <QProcess>
#include "main_window.hpp"
#include "device_model.hpp"
#include "platform/paths.hpp"
#include "platform/json_io.hpp"
#include <QDialog>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QSlider>
#include <QSpinBox>
#include <QScrollArea>
#include <QTableView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class GuiTest : public QObject, protected ControlFixture
{
	Q_OBJECT

	void ready(pa::GuiController& controller)
	{
		QTRY_VERIFY_WITH_TIMEOUT(controller.valid() && !controller.busy(), 10000);
		controller.setPollingActive(false);
	}

	static void click(QWidget* root, const char* name)
	{
		auto* widget = root->findChild<QWidget*>(name);
		QVERIFY2(widget, name);
		QVERIFY2(widget->isEnabled(), name);
		for (auto* parent = widget->parentWidget(); parent; parent = parent->parentWidget())
			if (auto* scroll = qobject_cast<QScrollArea*>(parent))
				scroll->ensureWidgetVisible(widget);
		QTest::mouseClick(widget,
		    Qt::LeftButton,
		    Qt::NoModifier,
		    qobject_cast<QCheckBox*>(widget) ? QPoint(8, widget->height() / 2) : widget->rect().center());
	}
	static void navigate(QWidget* root, int page)
	{
		auto* list = root->findChild<QListWidget*>("pageNavigation");
		QVERIFY(list);
		QTest::mouseClick(
		    list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(page)).center());
	}

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

	void draftExternalChangesAndRevert()
	{
		pa::GuiController controller;
		ready(controller);
		const auto revision = pa::configuration().revision;
		controller.setPointerSpeed(16);
		controller.setTrace(true);
		QVERIFY(controller.dirty());
		controller.setPage(pa::Page::Devices);
		QTRY_VERIFY(!controller.busy());
		controller.setPage(pa::Page::Diagnostics);
		QTRY_VERIFY(!controller.busy());
		QCOMPARE(controller.draft().settings.pointerSpeed, 16);
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
		QCOMPARE(controller.draft().settings.pointerSpeed, 16);
		controller.revert();
		QTRY_VERIFY(!controller.busy());
		QVERIFY(!controller.dirty());
		QVERIFY(!controller.conflict());
		QCOMPARE(controller.draft().settings.pointerSpeed, 7);
		QVERIFY(pa::ControlClient()
		        .update(
		            [](pa::Config& c)
		            {
			            c.settings.pointerSpeed = 9;
		            })
		        .saved);
		controller.refresh();
		QTRY_VERIFY(!controller.busy());
		QCOMPARE(controller.draft().settings.pointerSpeed, 9);
		controller.refresh(false);
		controller.setPointerSpeed(18);
		QTRY_VERIFY(!controller.busy());
		QCOMPARE(controller.draft().settings.pointerSpeed, 18);
		QVERIFY(controller.dirty());
	}

	void independentWorkersSurviveOtherControllerDestruction()
	{
		auto first = std::make_unique<pa::GuiController>();
		ready(*first);
		pa::GuiController second;
		ready(second);
		first.reset();
		second.setPointerSpeed(16);
		second.apply();
		QTRY_VERIFY(!second.busy());
		QVERIFY(!second.dirty());
		QCOMPARE(pa::readConfig().settings.pointerSpeed, 16);
	}

	void devicesTraceAndExport()
	{
		pa::GuiController controller;
		ready(controller);
		controller.setPage(pa::Page::Devices);
		QTRY_VERIFY(!controller.busy());
		QCOMPARE(controller.devices().size(), 2);
		const QString mouse = "mouse-0123456789abcdef01234567";
		controller.setExcluded("mouse-fedcba9876543210fedcba98", true);
		QVERIFY(!controller.dirty());
		controller.setExcluded(mouse, true);
		controller.setTrace(true);
		controller.apply();
		QTRY_VERIFY(!controller.busy());
		QVERIFY(!controller.dirty());
		QString error;
		pa::ControlClient().request("SetConnected", {false}, &error);
		controller.refresh(false);
		QTRY_VERIFY(!controller.busy());
		QCOMPARE(controller.devices().size(), 2);
		auto disconnected = controller.devices().last();
		QCOMPARE(disconnected.connected, false);
		controller.setExcluded(mouse, false);
		QVERIFY(controller.dirty());
		controller.setPage(pa::Page::Diagnostics);
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
		controller.setTrace(false);
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
		controller.setEnabled(true);
		QVERIFY(!controller.dirty());
		controller.apply();
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll(), QByteArray("{}"));
	}

	void widgetFlow_data()
	{
		QTest::addColumn<bool>("dark");
		QTest::addColumn<QSize>("size");
		QTest::newRow("light") << false << QSize(900, 680);
		QTest::newRow("dark-compact") << true << QSize(600, 500);
	}
	void widgetFlow()
	{
		QFETCH(bool, dark);
		QFETCH(QSize, size);
		pa::GuiController controller;
		ready(controller);
		pa::MainWindow window(controller);
		if (dark)
		{
			auto palette = window.palette();
			palette.setColor(QPalette::Window, QColor("#292929"));
			palette.setColor(QPalette::Base, QColor("#1c1c1c"));
			palette.setColor(QPalette::AlternateBase, QColor("#333333"));
			palette.setColor(QPalette::Button, QColor("#292929"));
			for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
				palette.setColor(role, QColor("#eeeeee"));
			window.setPalette(palette);
		}
		window.resize(size);
		window.show();
		QTRY_VERIFY(!controller.busy());
		controller.setPollingActive(false);
		QVERIFY(!controller.dirty()); // Rendering and refresh must not become edits.
		auto* apply = window.findChild<QPushButton*>("applyButton");
		QVERIFY(apply);
		QVERIFY(apply->mapTo(&window, QPoint(0, apply->height())).y() <= window.height());
		click(&window, "enabledSwitch");
		QVERIFY(controller.dirty());
		QVERIFY(pa::readConfig().enabled);
		auto* slider = window.findChild<QSlider*>("speedSlider");
		QVERIFY(slider);
		slider->setFocus();
		QTest::keyClick(slider, Qt::Key_Right);
		QCOMPARE(controller.draft().settings.pointerSpeed, 11);
		auto* spin = window.findChild<QSpinBox*>("speedSpinBox");
		QCOMPARE(spin->value(), 11);
		spin->setValue(12);
		QCOMPARE(slider->value(), 12);
		const auto qa =
		    qEnvironmentVariable("POINTERACCEL_TEST_ARTIFACTS", QCoreApplication::applicationDirPath() + "/gui-qa");
		QVERIFY(QDir().mkpath(qa));
		const auto suffix = QString(QTest::currentDataTag()) + "-" + QString::number(window.devicePixelRatioF());
		for (int page : {1, 2, 0})
		{
			navigate(&window, page);
			QTRY_VERIFY(!controller.busy());
			QCOMPARE(controller.page(), static_cast<pa::Page>(page));
			QVERIFY(controller.dirty());
			QTest::qWait(50);
			QVERIFY(window.grab().save(qa + "/page-" + QString::number(page) + "-" + suffix + ".png"));
		}
		window.close();
		QVERIFY(window.isVisible());
		auto* dialog = window.findChild<QDialog*>("closeDialog");
		QVERIFY(dialog && dialog->isVisible());
		click(&window, "cancelCloseButton");
		QVERIFY(controller.dirty());
		click(&window, "applyButton");
		QTRY_VERIFY(!controller.busy());
		QVERIFY(!controller.dirty());
		QVERIFY(!pa::readConfig().enabled);
		window.close();
		QVERIFY(!window.isVisible());
	}
	void deviceModelEditsOnlyEligibleDevices()
	{
		pa::GuiController controller;
		ready(controller);
		pa::MainWindow window(controller);
		window.show();
		navigate(&window, 1);
		QTRY_VERIFY(!controller.busy());
		auto* view = window.findChild<QTableView*>("devicesTable");
		QVERIFY(view);
		auto* model = view->model();
		QCOMPARE(model->rowCount(), 2);
		auto index = model->index(0, pa::DeviceModel::Excluded);
		QVERIFY(model->flags(index) & Qt::ItemIsUserCheckable);
		QTest::mouseClick(view->viewport(),
		    Qt::LeftButton,
		    Qt::NoModifier,
		    QPoint(view->visualRect(index).left() + 8, view->visualRect(index).center().y()));
		QVERIFY(controller.dirty());
		QCOMPARE(controller.draft().excluded.size(), 1);
		const auto unsupported = model->index(1, pa::DeviceModel::Excluded);
		QVERIFY(!(model->flags(unsupported) & Qt::ItemIsUserCheckable));
		QVERIFY(!model->setData(unsupported, Qt::Checked, Qt::CheckStateRole));
		controller.revert();
		QTRY_VERIFY(!controller.busy());
		window.close();
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
		pa::MainWindow window(controller);
		window.show();
		QTRY_VERIFY(!controller.busy());
		controller.setPointerSpeed(12);
		if (action == "offline")
		{
			peer.terminate();
			QVERIFY(peer.waitForFinished());
			controller.refresh();
			QTRY_VERIFY(!controller.busy());
		}
		window.close();
		QVERIFY(window.isVisible());
		if (action == "discard")
		{
			click(&window, "discardCloseButton");
			QVERIFY(!window.isVisible());
			QCOMPARE(pa::readConfig().settings.pointerSpeed, 10);
		}
		else
		{
			click(&window, "applyCloseButton");
			QTRY_VERIFY(!controller.busy());
			QCOMPARE(pa::readConfig().settings.pointerSpeed, 12);
			QVERIFY(!controller.dirty());
			if (action == "offline")
			{
				QVERIFY(window.isVisible());
				QVERIFY(controller.message().contains("尚未应用"));
				window.close();
				QVERIFY(!window.isVisible());
			}
			else
				QTRY_VERIFY(!window.isVisible());
		}
	}
};
int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	GuiTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "gui_test.moc"
