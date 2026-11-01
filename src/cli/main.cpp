#include "config/config.hpp"
#include "platform/paths.hpp"
#include "platform/json_io.hpp"
#include "platform/build_metadata.hpp"
#include "plugin_config.hpp"
#include "maintenance/startup.hpp"
#include "maintenance/session_watch.hpp"
#include "maintenance/repair.hpp"
#include "control/client.hpp"
#include <cerrno>
#include <cstring>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>
#include <QProcess>
#include <unistd.h>

namespace
{
QTextStream output(stdout), errors(stderr);

void print(const QJsonObject& o)
{
	output << QJsonDocument(o).toJson(QJsonDocument::Indented);
	output.flush();
}

int fail(const QString& why, int code = 1)
{
	errors << why << '\n';
	return code;
}

QJsonObject request(const QString& method, const QVariantList& args, QString* error)
{
	return pa::ControlClient().request(method, args, error);
}

bool boolean(const QString& value, bool* out)
{
	if (value == "on" || value == "true")
	{
		*out = true;
		return true;
	}
	if (value == "off" || value == "false")
	{
		*out = false;
		return true;
	}
	return false;
}

void usage()
{
	output << "PointerAccel — Windows 鼠标算法 / Plasma Wayland\n"
	          "  pointeraccel gui\n"
	          "  pointeraccel enable|disable [--temporary]\n"
	          "  pointeraccel status|info|doctor|devices|diagnostics [--json]\n"
	          "  pointeraccel devices include|exclude ID\n"
	          "  pointeraccel config get [--json]\n"
	          "  pointeraccel config set pointer-speed 1/20..20/20\n"
	          "  pointeraccel config set enhance-pointer-precision on|off\n"
	          "  pointeraccel config set trace on|off\n"
	          "  pointeraccel reset-state\n"
	          "  pointeraccel repair [--source /path/to/PointerAccel]\n"
	          "  pointeraccel raw-probe wayland|x11  （关闭窗口或 Ctrl+C 结束）\n"
	          "启停和配置默认持久保存。--temporary 只影响当前会话。\n"
	          "doctor: 0=正常（包括停用/空闲），1=异常；参数错误返回 2。\n";
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	app.setApplicationName("pointeraccel");
	auto args = app.arguments();
	args.removeFirst();
	bool json = args.removeAll("--json");
	if (args.isEmpty() || args == QStringList{"--help"} || args == QStringList{"help"})
	{
		usage();
		return 0;
	}
	if (args == QStringList{"gui"})
	{
		const auto program = (QCoreApplication::applicationDirPath() + "/pointeraccel-gui").toLocal8Bit();
		execl(program.constData(), program.constData(), static_cast<char*>(nullptr));
		return fail("无法启动 GUI: " + QString::fromLocal8Bit(std::strerror(errno)));
	}
	if (args[0] == "internal-plugin")
		return pa::internalPlugin(args);
	if (args == QStringList{"internal-watch"})
		return pa::watchSession();
	if (args == QStringList{"internal-startup-status"})
	{
		print(pa::startupStatus());
		return 0;
	}
	if (args.size() == 2 && args[0] == "internal-session-stamp")
	{
		QString why;
		return pa::writeJson(args[1], pa::sessionStamp(), &why) ? 0 : fail(why);
	}
	if (args[0] == "repair")
	{
		if (args.size() == 1)
			return pa::repair({});
		if (args.size() == 3 && args[1] == "--source")
			return pa::repair(args[2]);
		return fail("用法: pointeraccel repair [--source /path/to/PointerAccel]", 2);
	}
	if (args.size() == 2 && args[0] == "raw-probe" && (args[1] == "wayland" || args[1] == "x11"))
	{
		QProcess probe;
		probe.setProcessChannelMode(QProcess::ForwardedChannels);
		probe.setInputChannelMode(QProcess::ForwardedInputChannel);
		probe.start(QCoreApplication::applicationDirPath() + "/pointeraccel-raw-" + args[1], QStringList());
		if (!probe.waitForStarted())
			return fail(probe.errorString());
		probe.waitForFinished(-1);
		return probe.exitStatus() == QProcess::NormalExit ? probe.exitCode() : 1;
	}
	if (args == QStringList{"internal-init-config"})
	{
		QString why;
		return QFile::exists(pa::configPath()) || pa::writeConfig({}, &why) ? 0 : fail(why);
	}
	if (args == QStringList{"internal-shutdown"})
	{
		QString why;
		const auto status = request("Shutdown", {}, &why);
		return why.isEmpty() && !status["active"].toBool() && status["healthy"].toBool()
		           ? 0
		           : fail(why.isEmpty() ? status["error"].toString() : why);
	}
	QString configError;
	auto config = pa::readConfig(&configError);
	const auto command = args[0];
	if (command == "info" && args.size() == 1)
	{
		print(pa::buildInfo());
		return 0;
	}
	if ((command == "status" || command == "doctor") && args.size() == 1)
	{
		const auto status = pa::ControlClient().status();
		const bool healthy = status["healthy"].toBool();
		if (json || command == "doctor")
			print(status);
		else
		{
			output << "状态: " << status["state"].toString()
			       << "\n配置启用: " << (status["configured_enabled"].toBool() ? "是" : "否")
			       << "\n模块加载: " << (status["loaded"].toBool() ? "是" : "否")
			       << "\n实际生效: " << (status["active"].toBool() ? "是" : "否")
			       << "\n健康: " << (healthy ? "正常" : "待处理")
			       << "\n下次启动: " << status["startup_state"].toString() << '\n';
			if (!status["error"].toString().isEmpty())
				output << status["error"].toString() << '\n';
		}
		return command == "doctor" && !healthy ? 1 : 0;
	}
	if (command == "config" && args == QStringList{"config", "get"})
	{
		if (!configError.isEmpty())
			return fail(configError);
		print(pa::toJson(config));
		return 0;
	}
	QString method;
	QVariantList parameters;
	bool save = false;
	std::function<void(pa::Config&)> edit;
	if ((command == "enable" || command == "disable") &&
	    (args.size() == 1 || (args.size() == 2 && args[1] == "--temporary")))
	{
		if (!configError.isEmpty())
			return fail(configError);
		const bool enabled = command == "enable";
		if (args.size() == 2)
		{
			method = "SetTemporaryEnabled";
			parameters << enabled;
		}
		else
		{
			edit = [enabled](pa::Config& c)
			{
				c.enabled = enabled;
			};
			save = true;
		}
	}
	else if (command == "config" && args.size() == 4 && args[1] == "set")
	{
		if (!configError.isEmpty())
			return fail(configError);
		if (args[2] == "pointer-speed")
		{
			const auto speed = pa::parsePointerSpeed(args[3].toStdString());
			if (!speed)
				return fail("速度必须为 1/20 至 20/20。", 2);
			edit = [value = *speed](pa::Config& c)
			{
				c.settings.pointerSpeed = value;
			};
		}
		else if (args[2] == "enhance-pointer-precision")
		{
			if (!boolean(args[3], &config.settings.enhancePointerPrecision))
				return fail("请输入 on 或 off。", 2);
		}
		else if (args[2] == "trace")
		{
			if (!boolean(args[3], &config.trace))
				return fail("请输入 on 或 off。", 2);
		}
		else
			return fail("未知配置项。", 2);
		if (args[2] == "enhance-pointer-precision")
			edit = [value = config.settings.enhancePointerPrecision](pa::Config& c)
			{
				c.settings.enhancePointerPrecision = value;
			};
		if (args[2] == "trace")
			edit = [value = config.trace](pa::Config& c)
			{
				c.trace = value;
			};
		save = true;
	}
	else if (command == "devices" && args.size() == 3 && (args[1] == "include" || args[1] == "exclude"))
	{
		if (!configError.isEmpty())
			return fail(configError);
		if (!args[2].startsWith("mouse-") || args[2].size() != 30)
			return fail("请使用 devices 返回的稳定设备 ID。", 2);
		edit = [id = args[2], exclude = args[1] == "exclude"](pa::Config& c)
		{
			c.excluded.removeAll(id);
			if (exclude)
				c.excluded.append(id);
		};
		save = true;
	}
	else if (command == "devices" && args.size() == 1)
		method = "Devices";
	else if (command == "diagnostics" && args.size() == 1)
		method = "Diagnostics";
	else if (command == "reset-state" && args.size() == 1)
		method = "ResetState";
	else
	{
		usage();
		return fail("无效命令或参数。", 2);
	}
	if (save)
	{
		const auto result = pa::ControlClient().update(edit);
		if (!result.saved)
			return fail(result.error);
		config = result.snapshot.config;
		if (!result.response.isEmpty())
		{
			print(result.response);
			return result.response.contains("healthy") && !result.response["healthy"].toBool() ? 1 : 0;
		}
		if (json)
			print({{"saved", true}, {"pending_login", true}, {"settings", pa::toJson(config)}});
		else
			output << "配置已保存，下次加载模块时生效。\n";
		return 0;
	}
	QString why;
	auto reply = request(method, parameters, &why);
	if (!why.isEmpty())
	{
		return fail(why);
	}
	print(reply);
	return reply.contains("healthy") && !reply["healthy"].toBool() ? 1 : 0;
}
