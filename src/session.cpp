#include "session.hpp"
#include "common.hpp"
#include "compatibility.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QDebug>
#include <KConfig>
#include <KConfigGroup>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pa {
namespace {
QString configBase() { return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation); }
bool testing() {
#ifdef PA_TESTING
    return qEnvironmentVariable("POINTERACCEL_ISOLATED_TEST")=="1";
#else
    return false;
#endif
}
QString readText(const QString &path) { QFile f(path);return f.open(QIODevice::ReadOnly)?QString::fromUtf8(f.readAll()).trimmed():QString(); }
QString owner() {
    auto *interface=QDBusConnection::sessionBus().interface();if(!interface)return {};
    const auto reply=interface->serviceOwner("org.kde.KWin");
    return reply.isValid()?reply.value():QString();
}
QString busId() {
    QDBusInterface bus("org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus");
    const QDBusReply<QString> reply=bus.call("GetId");return reply.isValid()?reply.value():QString();
}
QString installedVersion() {
    QProcess rpm;rpm.start("rpm",{"-q","--qf","%{VERSION}","kwin"});
    if(!rpm.waitForFinished(3000) || rpm.exitCode()!=0)return {};
    return QString::fromUtf8(rpm.readAllStandardOutput()).trimmed();
}
bool sameSession(const QJsonObject &stamp, const QString &kwinOwner) {
    // A user manager can retain XDG_SESSION_ID across logins. Prefer the
    // checked compositor's bus identity whenever installation observed it.
    if(!stamp["owner"].toString().isEmpty() && !stamp["bus"].toString().isEmpty())
        return !kwinOwner.isEmpty() && stamp["owner"].toString()==kwinOwner && stamp["bus"].toString()==busId();
    const auto session=qEnvironmentVariable("XDG_SESSION_ID");
    return !session.isEmpty() && stamp["session"].toString()==session
            && stamp["boot"].toString()==readText("/proc/sys/kernel/random/boot_id");
}
class Lock {
public:
    Lock() {
        fd=open(maintenanceLockPath().toLocal8Bit().constData(),O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
        struct stat s{};
        if(fd<0)return;
        if(fstat(fd,&s) || s.st_uid!=getuid() || !S_ISREG(s.st_mode) || flock(fd,LOCK_EX|LOCK_NB)) {close(fd);fd=-1;}
    }
    ~Lock() { if(fd>=0)close(fd); }
    bool valid() const {return fd>=0;}
private:
    int fd=-1;
};
bool plainPath(const QString &path) {return path.startsWith('/') && !path.contains("/../") && !path.contains("/./") && !path.contains('\n') && !path.contains('\t');}
bool allowedFile(const QString &path) {
    return plainPath(path) && (path.startsWith(dataRoot()+"/") || path.startsWith(stateRoot()+"/backups/")
        || QStringList{stateRoot()+"/owner",stateRoot()+"/system.sha256",stateRoot()+"/session.json",sessionUnitPath()}.contains(path));
}
bool allowedDirectory(const QString &path) {
    if(!plainPath(path) || path=="/" || path==QDir::homePath())return false;
    for(const auto &root : {dataRoot(),configRoot(),stateRoot(),QFileInfo(cliPath()).path(),QFileInfo(sessionUnitPath()).path(),QFileInfo(sessionLinkPath()).path()})
        if(path==root || root.startsWith(path+"/") || path.startsWith(dataRoot()+"/") || path.startsWith(stateRoot()+"/"))return true;
    return false;
}
}
QString systemRecordRoot() {
    const auto root=qEnvironmentVariable("POINTERACCEL_TEST_SYSTEM_ROOT");
    return testing()&&!root.isEmpty()?root+"/records":"/usr/local/share/pointeraccel";
}
QString installedPluginPath() {
    const auto root=qEnvironmentVariable("POINTERACCEL_TEST_SYSTEM_ROOT");
    return testing()&&!root.isEmpty()?root+"/plugins/pointeraccel_kwin.so":pluginPath();
}
QString sessionUnitPath() {return configBase()+"/systemd/user/pointeraccel-session.service";}
QString sessionLinkPath() {return configBase()+"/systemd/user/plasma-workspace.target.wants/pointeraccel-session.service";}
QString maintenanceLockPath() {
    return "/tmp/pointeraccel-"+QString::number(getuid())+"-"+QString::fromLatin1(QCryptographicHash::hash(stateRoot().toUtf8(),QCryptographicHash::Sha256).toHex())+".lock";
}
bool verifyInstallation(QString *error) {
    const auto reject=[&](const QString &text){if(error)*error=text;return false;};
    QFile file(manifestPath());
    if(QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly) || readText(stateRoot()+"/owner")!=QString::number(getuid()))
        return reject("缺少有效的当前用户安装清单。");
    for(const auto &path : {dataRoot(),stateRoot(),dataRoot()+"/bin",dataRoot()+"/scripts",stateRoot()+"/backups",QFileInfo(sessionUnitPath()).path(),QFileInfo(sessionLinkPath()).path()})
        if(QFileInfo(path).isSymLink())return reject("安装目录变成符号链接: "+path);
    QSet<QString> entries;
    for(const auto &line : file.readAll().split('\n')) {
        if(line.isEmpty())continue;
        const auto parts=line.split('\t');
        if(parts.size()!=3)return reject("安装清单格式错误。");
        const auto kind=parts[0];const QString value=QString::fromUtf8(parts[1]),path=QString::fromUtf8(parts[2]);
        if(kind=="D") {if(!allowedDirectory(path))return reject("目录清单超出允许范围。");continue;}
        if(entries.contains(path))return reject("安装清单存在重复文件。");
        entries.insert(path);
        if(kind=="F") {
            if(!allowedFile(path) || QFileInfo(path).isSymLink() || !QFileInfo(path).isFile() || digest(path)!=value)
                return reject("安装文件缺失或被修改: "+path);
        } else if(kind=="L") {
            const auto expected=path==cliPath()?dataRoot()+"/bin/pointeraccel":path==sessionLinkPath()?sessionUnitPath():QString();
            if(expected.isEmpty() || value!=expected || !QFileInfo(path).isSymLink() || QFileInfo(path).symLinkTarget()!=expected)
                return reject("安装入口冲突: "+path);
        } else return reject("安装清单包含未知记录。");
    }
    for(const auto &required : {dataRoot()+"/bin/pointeraccel",stateRoot()+"/owner",stateRoot()+"/system.sha256",stateRoot()+"/backups/kwin.json",cliPath()})
        if(!entries.contains(required))return reject("安装清单缺少必要记录: "+required);
    return true;
}
QJsonObject sessionStamp() {
    return {{"session",qEnvironmentVariable("XDG_SESSION_ID")},{"boot",readText("/proc/sys/kernel/random/boot_id")},
            {"owner",owner()},{"bus",busId()}};
}
QJsonObject startupStatus() {
    QJsonObject status{{"startup_state","blocked"},{"startup_compatible",false},{"repair_required",false},{"relogin_required",false},
                       {"module_file_present",QFileInfo::exists(installedPluginPath())},{"module_file_valid",false},{"error",QString()}};
    const auto stop=[&](const QString &state,const QString &error,bool repair=false) {
        status["startup_state"]=state;status["error"]=error;status["repair_required"]=repair;return status;
    };
    if(!QFileInfo::exists(manifestPath()))return stop("not-installed","PointerAccel 未安装或安装清单缺失。");
    QString error;
    if(!verifyInstallation(&error))return stop("blocked",error);
    if(QFileInfo::exists(systemRecordRoot()+"/pending") || QFileInfo::exists(stateRoot()+".transaction.json"))
        return stop("blocked","安装事务未结束，请从源码运行 ./install.sh --recover。");
    const auto recordPath=systemRecordRoot()+"/compatibility.json";
    const QFileInfo recordFile(recordPath),module(installedPluginPath()),systemDirectory(systemRecordRoot());
    if(!testing()) {
        for(const auto &info : {recordFile,module,systemDirectory})
            if(!info.exists() || info.isSymLink() || info.ownerId()!=0
               || (info.permissions()&(QFileDevice::WriteGroup|QFileDevice::WriteOther)))
                return stop("blocked","系统模块或兼容清单的所有权、权限无效。");
    }
    const auto record=readJson(recordPath,&error);
    if(!error.isEmpty() || record["schema"].toInt()!=1 || record["owner"].toInt(-1)!=int(getuid())
       || record["kwin_iid"].toString()!="org.kde.kwin.PluginFactoryInterface"+record["kwin_version"].toString())
        return stop("blocked","缺少有效兼容清单；旧安装请重新运行 install.sh 迁移。");
    status["installed_build"]=record;
    const auto expected=readText(stateRoot()+"/system.sha256");
    const bool valid=expected.size()==64 && expected==record["module_sha256"].toString() && digest(installedPluginPath())==expected
        && readText(systemRecordRoot()+"/owner.tsv")==QString::number(getuid())+"\t"+expected+"\t"+digest(recordPath);
    status["module_file_valid"]=valid;
    if(!valid)return stop("blocked","系统模块校验失败，已阻止加载。");
    if(KConfig(kwinConfigPath(),KConfig::SimpleConfig).group("Plugins").readEntry("pointeraccel_kwinEnabled",false))
        return stop("blocked","KWin 直接自启仍开启，请重新运行 install.sh 迁移到先检查后加载。");
    if(!QFileInfo::exists(sessionUnitPath()) || !QFileInfo(sessionLinkPath()).isSymLink())return stop("blocked","会话检查服务未配置。");
    const auto version=installedVersion();status["installed_kwin_version"]=version;
    if(version.isEmpty())return stop("blocked","无法确认已安装的 KWin 版本。");
    if(!QRegularExpression("^6\\.7\\.\\d+$").match(version).hasMatch())
        return stop("unsupported","KWin "+version+" 尚未适配，需要代码审查；重新编译不能解决。",true);
    if(record["kwin_library"].toString()!=QFileInfo("/usr/lib64/libkwin.so.6").canonicalFilePath()
       || record["kwin_executable"].toString()!="/usr/bin/kwin_wayland"
       || version!=record["kwin_version"].toString()
       || digest(record["kwin_library"].toString())!=record["kwin_sha256"].toString()
       || digest("/usr/bin/kwin_wayland")!=record["executable_sha256"].toString())
        return stop("repair-required","KWin 已更新，请运行 pointeraccel repair；当前已加载模块可继续使用。",true);
    status["startup_compatible"]=true;
    const auto currentOwner=owner();status["kwin_owner"]=currentOwner;
    error.clear();const auto stamp=readJson(stateRoot()+"/session.json",&error);
    if(!error.isEmpty())return stop("blocked","缺少有效的安装会话记录。");
    if(sameSession(stamp,currentOwner)) {
        status["relogin_required"]=true;return stop("pending-login","安装已完成，请重新登录；不会向当前 KWin 加载新模块。");
    }
    if(currentOwner.isEmpty())return stop("pending-kwin","等待 Plasma Wayland 会话。");
    QDBusInterface kwin(currentOwner,"/KWin","org.kde.KWin");kwin.setTimeout(3000);
    const QDBusReply<QString> info=kwin.call("supportInformation");
    const auto match=QRegularExpression("KWin version: ([^\\n]+)").match(info.isValid()?info.value():QString());
    if(!match.hasMatch() || match.captured(1).trimmed()!=version) {
        status["relogin_required"]=true;return stop("pending-login","运行中的 KWin 版本与磁盘不同，请重新登录。");
    }
    return stop("ready",{});
}

int repair(const QString &source) {
    QString error;if(!verifyInstallation(&error)){qCritical().noquote()<<error;return 1;}
    QTemporaryDir temporary;
    if(!temporary.isValid())return 1;
    QString sourceRoot=source;
    if(sourceRoot.isEmpty()) {
        QProcess unpack;unpack.start("python3",{dataRoot()+"/scripts/maintenance.py","extract",dataRoot()+"/source.tar.gz",temporary.path()});
        if(!unpack.waitForFinished(30000) || unpack.exitCode()!=0) {qCritical().noquote()<<unpack.readAllStandardError();return 1;}
        sourceRoot=temporary.path();
    }
    sourceRoot=QFileInfo(sourceRoot).canonicalFilePath();
    if(sourceRoot.isEmpty() || !QFileInfo::exists(sourceRoot+"/install.sh")){qCritical()<<"源码目录缺少 install.sh。";return 1;}
    const auto cache=QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)+"/pointeraccel";
    if(!QDir().mkpath(cache))return 1;
    QTemporaryDir build(cache+"/repair-XXXXXX");if(!build.isValid())return 1;
    QStringList args{sourceRoot+"/install.sh","--build-dir",build.path()};
#ifdef PA_TESTING
    if(testing()) {
        const auto root=qEnvironmentVariable("POINTERACCEL_TEST_SYSTEM_ROOT");
        if(!root.isEmpty())args<<"--test-system"<<root;
        const auto reuse=qEnvironmentVariable("POINTERACCEL_TEST_BUILD_DIR");
        if(!reuse.isEmpty())args[2]=reuse;
    }
#endif
    QProcess install;install.setProcessChannelMode(QProcess::ForwardedChannels);install.setInputChannelMode(QProcess::ForwardedInputChannel);
    install.start("bash",args);
    if(!install.waitForStarted())return 1;
    install.waitForFinished(-1);return install.exitStatus()==QProcess::NormalExit?install.exitCode():1;
}

int watchSession() {
    QFileSystemWatcher files;
    QTimer poll,debounce;poll.setInterval(30000);debounce.setSingleShot(true);debounce.setInterval(300);
    QDBusServiceWatcher kwin("org.kde.KWin",QDBusConnection::sessionBus(),QDBusServiceWatcher::WatchForOwnerChange);
    QByteArray lastAttributes;QJsonObject cached;QString attempted;
    auto inspect=[&] {
        Lock lock;if(!lock.valid())return;
        QStringList paths{manifestPath(),stateRoot()+"/session.json",kwinConfigPath(),sessionUnitPath(),sessionLinkPath(),
                          installedPluginPath(),systemRecordRoot(),systemRecordRoot()+"/compatibility.json",systemRecordRoot()+"/owner.tsv",systemRecordRoot()+"/pending",
                          stateRoot()+".transaction.json",
                          "/usr/bin/kwin_wayland","/usr/lib64/libkwin.so.6","/usr/lib/sysimage/rpm/rpmdb.sqlite",configPath()};
        // Watch every manifest-owned file, including source snapshots and helpers.
        QFile manifest(manifestPath());if(manifest.open(QIODevice::ReadOnly))for(const auto &line:manifest.readAll().split('\n')) {
            const auto parts=line.split('\t');if(parts.size()==3 && (parts[0]=="F" || parts[0]=="L"))paths<<QString::fromUtf8(parts[2]);
        }
        QByteArray attributes=owner().toUtf8();
        for(const auto &path:paths) {
            struct stat s{};const bool exists=::stat(path.toLocal8Bit().constData(),&s)==0;
            attributes+=path.toUtf8()+QByteArray::number(exists)+QByteArray::number(s.st_ino)+QByteArray::number(s.st_size)
                +QByteArray::number(s.st_mtim.tv_sec)+QByteArray::number(s.st_mtim.tv_nsec)
                +QByteArray::number(s.st_ctim.tv_sec)+QByteArray::number(s.st_ctim.tv_nsec);
            for(const auto &watch:{path,QFileInfo(path).path()})if(QFileInfo::exists(watch) && !files.files().contains(watch) && !files.directories().contains(watch))files.addPath(watch);
        }
        if(attributes!=lastAttributes) {cached=startupStatus();lastAttributes=attributes;}
        const auto checkedOwner=cached["kwin_owner"].toString();
        if(cached["startup_state"]=="ready" && !checkedOwner.isEmpty() && owner()==checkedOwner && attempted!=checkedOwner) {
            attempted=checkedOwner;
            QDBusInterface plugins(checkedOwner,"/Plugins","org.kde.KWin.Plugins");plugins.setTimeout(3000);
            const auto loaded=plugins.property("LoadedPlugins").toStringList();
            bool ok=loaded.contains("pointeraccel_kwin");
            if(!ok) {const QDBusReply<bool> reply=plugins.call("LoadPlugin","pointeraccel_kwin");ok=reply.isValid()&&reply.value();}
            if(!ok) {cached["startup_state"]="load-failed";cached["error"]="模块加载失败，请运行 pointeraccel doctor 查看诊断；不会自动重试当前 KWin。";}
            else {
                QDBusInterface module(checkedOwner,dbusPath,dbusInterface);module.setTimeout(3000);
                const QDBusReply<QString> info=module.call("Info"),state=module.call("Status");
                const auto build=QJsonDocument::fromJson(info.isValid()?info.value().toUtf8():QByteArray()).object();
                const auto runtime=QJsonDocument::fromJson(state.isValid()?state.value().toUtf8():QByteArray()).object();
                if(build["image_build_id"]!=cached["installed_build"].toObject()["module_build_id"]) {
                    cached["startup_state"]="pending-login";cached["error"]="当前仍驻留旧模块，请重新登录。";
                } else if(!runtime["healthy"].toBool()) {
                    cached["startup_state"]="load-failed";cached["error"]=runtime["error"].toString("模块未正常接入，请运行 pointeraccel doctor。");
                }
            }
        }
        const auto error=cached["error"].toString();
        if(!error.isEmpty() && cached["startup_state"]!="pending-kwin") {
            const QJsonObject notification{{"state",cached["startup_state"]},{"error",error},{"build",cached["installed_build"]},{"kwin",cached["installed_kwin_version"]}};
            const auto key=QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(notification).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
            const auto previous=readJson(stateRoot()+"/notification.json");
            if(previous["key"].toString()!=key) {
                auto message=QDBusMessage::createMethodCall("org.freedesktop.Notifications","/org/freedesktop/Notifications","org.freedesktop.Notifications","Notify");
                message.setAutoStartService(false);
                message.setArguments({"PointerAccel",uint(0),"input-mouse","PointerAccel",error,QStringList(),QVariantMap(),-1});
                const QDBusReply<uint> reply=QDBusConnection::sessionBus().call(message,QDBus::Block,1000);
                if(reply.isValid())writeJson(stateRoot()+"/notification.json",{{"key",key}});
                qWarning().noquote()<<error;
            }
        }
    };
    QObject::connect(&files,&QFileSystemWatcher::fileChanged,&debounce,[&]{lastAttributes.clear();debounce.start();});
    QObject::connect(&files,&QFileSystemWatcher::directoryChanged,&debounce,[&]{debounce.start();});
    QObject::connect(&kwin,&QDBusServiceWatcher::serviceOwnerChanged,&debounce,[&]{lastAttributes.clear();debounce.start();});
    QObject::connect(&debounce,&QTimer::timeout,&debounce,inspect);
    QObject::connect(&poll,&QTimer::timeout,&poll,inspect);
    poll.start();QTimer::singleShot(0,&debounce,inspect);
    return QCoreApplication::exec();
}
}
