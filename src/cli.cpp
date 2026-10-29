#include "common.hpp"
#include "session.hpp"
#include <QCoreApplication>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTextStream>
#include <QProcess>
#include <QDir>
#include <KConfig>
#include <KConfigGroup>
#include <unistd.h>

namespace {
QTextStream output(stdout),errors(stderr);
void print(const QJsonObject &o) {output<<QJsonDocument(o).toJson(QJsonDocument::Indented);output.flush();}
int fail(const QString &why,int code=1) {errors<<why<<'\n';return code;}
QJsonObject request(const QString &method,const QVariantList &args,QString *error) {
    QDBusInterface interface("org.kde.KWin",pa::dbusPath,pa::dbusInterface,QDBusConnection::sessionBus());
    interface.setTimeout(5000);
    if (!interface.isValid()) {*error="KWin 模块未加载。首次安装后请重新登录 Plasma。";return {};}
    const QDBusReply<QString> reply=interface.callWithArgumentList(QDBus::Block,method,args);
    if (!reply.isValid()) {*error=reply.error().message();return {};}
    const auto doc=QJsonDocument::fromJson(reply.value().toUtf8());
    if (!doc.isObject()) {*error="模块返回的数据无效。";return {};}
    return doc.object();
}
bool boolean(const QString &value,bool *out) {
    if (value=="on" || value=="true") {*out=true;return true;}
    if (value=="off" || value=="false") {*out=false;return true;}
    return false;
}
int internalPlugin(const QStringList &args) {
    if (args.size()<2) return fail("缺少内部维护参数。",2);
    const auto path=pa::kwinConfigPath();
    KConfig config(path,KConfig::SimpleConfig);KConfigGroup group(&config,"Plugins");
    constexpr auto key="pointeraccel_kwinEnabled";
    if (args[1]=="backup" && args.size()==3) {
        QFile original(path);const bool existed=original.exists();
        if (existed && !original.open(QIODevice::ReadOnly)) return fail(original.errorString());
        QJsonObject saved{{"file_existed",existed},{"contents",QString::fromLatin1(original.readAll().toBase64())},
                          {"permissions",int(original.permissions())},{"key_existed",group.hasKey(key)},
                          {"value",group.readEntry(key,QString())}};
        QString why;return pa::writeJson(args[2],saved,&why)?0:fail(why);
    }
    if ((args[1]=="enable" || args[1]=="guard") && args.size()==2) {
        group.writeEntry(key,args[1]=="enable");if (!config.sync()) return fail("无法保存 KWin 自启设置。");
        output<<pa::digest(path)<<'\n';return 0;
    }
    if (args[1]=="restore" && args.size()==4) {
        QString why;auto saved=pa::readJson(args[2],&why);if (!why.isEmpty()) return fail(why);
        if (pa::digest(path)==args[3]) {
            if (!saved["file_existed"].toBool()) return QFile::remove(path)||!QFile::exists(path)?0:fail("无法删除本程序创建的 kwinrc。");
            QSaveFile file(path);if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
            const auto bytes=QByteArray::fromBase64(saved["contents"].toString().toLatin1());
            file.setPermissions(QFileDevice::Permissions(saved["permissions"].toInt()));
            if (file.write(bytes)!=bytes.size() || !file.commit()) return fail(file.errorString());
        } else {
            // Preserve unrelated changes made since installation.
            if (saved["key_existed"].toBool()) group.writeEntry(key,saved["value"].toString());
            else group.deleteEntry(key);
            if (!config.sync()) return fail("无法恢复 KWin 自启设置。");
        }
        return 0;
    }
    return fail("无效内部维护命令。",2);
}
void usage() {
    output<<"PointerAccel — Windows 鼠标算法 / Plasma Wayland\n"
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
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);app.setApplicationName("pointeraccel");
    auto args=app.arguments();args.removeFirst();
    bool json=args.removeAll("--json");
    if (args.isEmpty() || args==QStringList{"--help"} || args==QStringList{"help"}) {usage();return 0;}
    if (args[0]=="internal-plugin") return internalPlugin(args);
    if (args==QStringList{"internal-watch"}) return pa::watchSession();
    if (args==QStringList{"internal-startup-status"}) {print(pa::startupStatus());return 0;}
    if (args.size()==2 && args[0]=="internal-session-stamp") {
        QString why;return pa::writeJson(args[1],pa::sessionStamp(),&why)?0:fail(why);
    }
    if (args[0]=="repair") {
        if(args.size()==1)return pa::repair({});
        if(args.size()==3 && args[1]=="--source")return pa::repair(args[2]);
        return fail("用法: pointeraccel repair [--source /path/to/PointerAccel]",2);
    }
    if (args.size()==2 && args[0]=="raw-probe" && (args[1]=="wayland" || args[1]=="x11")) {
        QProcess probe;probe.setProcessChannelMode(QProcess::ForwardedChannels);probe.setInputChannelMode(QProcess::ForwardedInputChannel);
        probe.start(QCoreApplication::applicationDirPath()+"/pointeraccel-raw-"+args[1],QStringList());
        if(!probe.waitForStarted())return fail(probe.errorString());
        probe.waitForFinished(-1);return probe.exitStatus()==QProcess::NormalExit?probe.exitCode():1;
    }
    if (args==QStringList{"internal-init-config"}) {QString why;return QFile::exists(pa::configPath())||pa::writeConfig({},&why)?0:fail(why);}
    if (args==QStringList{"internal-shutdown"}) {
        QString why;const auto status=request("Shutdown",{},&why);
        return why.isEmpty() && !status["active"].toBool() && status["healthy"].toBool()?0:fail(why.isEmpty()?status["error"].toString():why);
    }
    QString configError;auto config=pa::readConfig(&configError);
    const auto command=args[0];
    if (command=="info" && args.size()==1) {print(pa::buildInfo());return 0;}
    if ((command=="status" || command=="doctor") && args.size()==1) {
        QString why;auto status=request("Status",{},&why);
        const bool installed=QFile::exists(pa::manifestPath());
        const auto startup=pa::startupStatus();
        if (!why.isEmpty()) {
            status={{"installed",installed},{"loaded",false},{"active",false},{"configured_enabled",config.enabled},
                    {"compatible",false},{"running_compatible",false},{"healthy",false},{"state",startup["startup_state"]},
                    {"error",startup["error"]},{"settings",pa::toJson(config)}};
            if(startup["startup_state"]=="ready") {status["state"]="load-failed";status["error"]=why;}
        } else {
            if(!status.contains("running_compatible"))status["running_compatible"]=status["compatible"];
            QString infoError;status["running_build"]=request("Info",{},&infoError);
        }
        status["configuration_valid"]=configError.isEmpty();
        const bool direct=KConfig(pa::kwinConfigPath(),KConfig::SimpleConfig).group("Plugins").readEntry("pointeraccel_kwinEnabled",false);
        status["direct_autostart_enabled"]=direct;
        status["autostart_configured"]=!direct && QFile::exists(pa::sessionUnitPath()) && QFileInfo(pa::sessionLinkPath()).isSymLink();
        for(const auto *key:{"startup_state","startup_compatible","repair_required","relogin_required","module_file_present","module_file_valid","installed_build","installed_kwin_version"})
            status[key]=startup[key];
        const auto runningBuild=status["running_build"].toObject(),installedBuild=startup["installed_build"].toObject();
        const bool moduleMatches=status["loaded"].toBool() && !runningBuild["image_build_id"].toString().isEmpty()
            && runningBuild["image_build_id"]==installedBuild["module_build_id"];
        status["running_module_matches_install"]=moduleMatches;
        if(status["loaded"].toBool() && !moduleMatches) {
            status["relogin_required"]=true;
            if(status["startup_state"]=="ready") {
                status["startup_state"]="pending-login";
                status["error"]="当前仍驻留旧模块，请重新登录以加载已安装的新模块。";
            }
        }
        if(!startup["error"].toString().isEmpty())status["error"]=startup["error"];
        if(!configError.isEmpty())status["error"]=configError;
        const bool healthy=status["healthy"].toBool() && configError.isEmpty() && installed
            && status["module_file_valid"].toBool() && status["autostart_configured"].toBool()
            && status["startup_state"]=="ready" && status["running_compatible"].toBool() && moduleMatches;
        status["healthy"]=healthy;
        if (json || command=="doctor") print(status);
        else {
            output<<"状态: "<<status["state"].toString()<<"\n配置启用: "<<(status["configured_enabled"].toBool()?"是":"否")
                  <<"\n模块加载: "<<(status["loaded"].toBool()?"是":"否")<<"\n实际生效: "<<(status["active"].toBool()?"是":"否")
                  <<"\n健康: "<<(healthy?"正常":"待处理")<<"\n下次启动: "<<status["startup_state"].toString()<<'\n';
            if (!status["error"].toString().isEmpty()) output<<status["error"].toString()<<'\n';
        }
        return command=="doctor"&&!healthy?1:0;
    }
    if (command=="config" && args==QStringList{"config","get"}) {
        if (!configError.isEmpty()) return fail(configError);
        print(pa::toJson(config));return 0;
    }
    QString method;QVariantList parameters;bool save=false;
    if ((command=="enable" || command=="disable") && (args.size()==1 || (args.size()==2 && args[1]=="--temporary"))) {
        if (!configError.isEmpty()) return fail(configError);
        const bool enabled=command=="enable";
        if (args.size()==2) {method="SetTemporaryEnabled";parameters<<enabled;}
        else {config.enabled=enabled;save=true;}
    } else if (command=="config" && args.size()==4 && args[1]=="set") {
        if (!configError.isEmpty()) return fail(configError);
        if (args[2]=="pointer-speed") {
            const auto speed=windows_pointer::parsePointerSpeed(args[3].toStdString());
            if (!speed) return fail("速度必须为 1/20 至 20/20。",2);
            config.settings.pointerSpeed=*speed;
        } else if (args[2]=="enhance-pointer-precision") {
            if (!boolean(args[3],&config.settings.enhancePointerPrecision)) return fail("请输入 on 或 off。",2);
        } else if (args[2]=="trace") {
            if (!boolean(args[3],&config.trace)) return fail("请输入 on 或 off。",2);
        } else return fail("未知配置项。",2);
        save=true;
    } else if (command=="devices" && args.size()==3 && (args[1]=="include" || args[1]=="exclude")) {
        if (!configError.isEmpty()) return fail(configError);
        if (!args[2].startsWith("mouse-") || args[2].size()!=30) return fail("请使用 devices 返回的稳定设备 ID。",2);
        config.excluded.removeAll(args[2]);if(args[1]=="exclude")config.excluded.append(args[2]);save=true;
    } else if (command=="devices" && args.size()==1) method="Devices";
    else if (command=="diagnostics" && args.size()==1) method="Diagnostics";
    else if (command=="reset-state" && args.size()==1) method="ResetState";
    else {usage();return fail("无效命令或参数。",2);}
    if (save) {
        QString why;if (!pa::writeConfig(config,&why))return fail(why);method="ApplyConfig";
    }
    QString why;auto reply=request(method,parameters,&why);
    if (!why.isEmpty()) {
        if (save) {
            if(json) print({{"saved",true},{"pending_login",true},{"settings",pa::toJson(config)}});
            else output<<"配置已保存，下次加载模块时生效。\n";
            return 0;
        }
        return fail(why);
    }
    print(reply);
    return reply.contains("healthy")&&!reply["healthy"].toBool()?1:0;
}
