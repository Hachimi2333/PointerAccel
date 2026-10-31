#include "control.hpp"
#include <QCoreApplication>
#include <QDBusContext>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

class Peer : public QObject, protected QDBusContext
{
	Q_OBJECT
	Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.PointerAccel1")
	bool failure = false, connected = true, temporary = false;

	static QString json(const QJsonObject& object)
	{
		return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
	}

public slots:

	QString Status()
	{
		const auto c = pa::readConfig();
		return json({{"loaded", true},
		    {"active", c.enabled && !failure},
		    {"healthy", !failure},
		    {"configured_enabled", c.enabled},
		    {"temporary_override", temporary},
		    {"state",
		        failure     ? "error"
		        : c.enabled ? "idle"
		                    : "disabled"},
		    {"error", failure ? "模拟应用失败" : ""},
		    {"settings", pa::toJson(c)},
		    {"running_compatible", true},
		    {"running_build", QJsonObject{{"image_build_id", "fake"}}}});
	}

	QString ApplyConfig()
	{
		temporary = false;
		return Status();
	}

	QString Devices()
	{
		QJsonArray list;
		if (connected)
			list.append(QJsonObject{{"id", "mouse-0123456789abcdef01234567"},
			    {"name", "测试鼠标"},
			    {"eligible", true},
			    {"accelerated", true},
			    {"excluded", false}});
		list.append(QJsonObject{{"id", "mouse-fedcba9876543210fedcba98"},
		    {"name", "测试触摸板"},
		    {"eligible", false},
		    {"accelerated", false},
		    {"excluded", false}});
		return json({{"devices", list}});
	}

	QString Diagnostics()
	{
		const auto trace = pa::readConfig().trace;
		const QJsonObject sample{{"device", "mouse-0123456789abcdef01234567"},
		    {"raw", QJsonArray{2, 1}},
		    {"output", QJsonArray{3, 1}},
		    {"scale", 1.25},
		    {"dpi", 120}};
		QJsonObject result{{"processed", 42},
		    {"passthrough", 3},
		    {"batches", 12},
		    {"last_age_ms", 100},
		    {"last", sample},
		    {"trace_enabled", trace},
		    {"trace_samples", trace ? 1 : 0},
		    {"trace_capacity", 4096}};
		if (trace)
			result["trace"] = QJsonArray{sample};
		return json(result);
	}

	QString ResetState()
	{
		return json({{"ok", true}});
	}

	QString SetFailure(bool value)
	{
		failure = value;
		return json({{"ok", true}});
	}

	QString SetConnected(bool value)
	{
		connected = value;
		return json({{"ok", true}});
	}

	QString NoReply()
	{
		setDelayedReply(true);
		return {};
	}

	QString InvalidReply()
	{
		return "invalid JSON";
	}
};

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	Peer peer;
	auto bus = QDBusConnection::sessionBus();
	if (!bus.registerService("org.kde.KWin") ||
	    !bus.registerObject(pa::dbusPath, &peer, QDBusConnection::ExportAllSlots))
		return 1;
	QTextStream(stdout) << "READY\n" << Qt::flush;
	return app.exec();
}

#include "control_peer.moc"
