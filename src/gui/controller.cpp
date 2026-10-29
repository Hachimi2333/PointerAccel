#include "controller.hpp"
#include "control.hpp"
#include "session.hpp"
#include <QClipboard>
#include <QDateTime>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSet>
#include <memory>
#include <sys/stat.h>

namespace pa {
// This object and its private bus connection live exclusively on the worker thread.
class GuiWorker : public QObject {
public:
    ~GuiWorker() override {
        client.reset();
        if (!connectionName.isEmpty()) QDBusConnection::disconnectFromBus(connectionName);
    }
    ControlClient &control() {
        if (!client) {
            connectionName="pointeraccel-gui-"+QString::number(QCoreApplication::applicationPid());
            client=std::make_unique<ControlClient>(QDBusConnection::connectToBus(QDBusConnection::SessionBus,connectionName));
        }
        return *client;
    }
    QStringList watchedPaths() const {
        QStringList paths{configPath(),manifestPath(),stateRoot()+"/session.json",kwinConfigPath(),sessionUnitPath(),sessionLinkPath(),
            installedPluginPath(),systemRecordRoot(),systemRecordRoot()+"/compatibility.json",systemRecordRoot()+"/owner.tsv",
            systemRecordRoot()+"/pending",stateRoot()+".transaction.json","/usr/bin/kwin_wayland","/usr/lib64/libkwin.so.6",
            "/usr/lib/sysimage/rpm/rpmdb.sqlite"};
        QFile file(manifestPath());
        if (file.open(QIODevice::ReadOnly)) for (const auto &line:file.readAll().split('\n')) {
            const auto fields=line.split('\t');
            if (fields.size()==3 && (fields[0]=="F" || fields[0]=="L")) paths<<QString::fromUtf8(fields[2]);
        }
        paths.removeDuplicates();
        return paths;
    }
    QVariantMap inspect(int page, bool force = false, bool checkAttributes = false, bool all = false) {
        if (force || checkAttributes || startup.isEmpty()) {
            paths=watchedPaths();
            QByteArray attributes;
            for (const auto &path:paths) {
                struct stat s{};
                const bool exists=::stat(path.toLocal8Bit().constData(),&s)==0;
                attributes+=path.toUtf8()+':'+QByteArray::number(exists)+':'+QByteArray::number(s.st_ino)+':'+QByteArray::number(s.st_size)
                    +':'+QByteArray::number(s.st_mtim.tv_sec)+':'+QByteArray::number(s.st_mtim.tv_nsec)
                    +':'+QByteArray::number(s.st_ctim.tv_sec)+':'+QByteArray::number(s.st_ctim.tv_nsec)+';';
            }
            if (force || startup.isEmpty() || attributes!=lastAttributes) startup=startupStatus();
            lastAttributes=attributes;
        }
        const auto snapshot=configuration();
        QVariantMap result{{"configuration",toJson(snapshot.config).toVariantMap()},{"revision",snapshot.revision},
            {"config_error",snapshot.error},{"status",control().status(startup).toVariantMap()},{"paths",paths}};
        if (page==1 || all) {
            QString error;
            result["devices"]=control().request("Devices",{},&error)["devices"].toArray().toVariantList();
            result["devices_error"]=error;
        }
        if (page==2 || all) {
            QString error;
            result["diagnostics"]=control().request("Diagnostics",{},&error).toVariantMap();
            result["diagnostics_error"]=error;
        }
        return result;
    }
private:
    QString connectionName;
    std::unique_ptr<ControlClient> client;
    QStringList paths;
    QByteArray lastAttributes;
    QJsonObject startup;
};

GuiController::GuiController(QObject *parent) : QObject(parent), m_worker(new GuiWorker) {
    m_draft=m_saved=toJson(Config{}).toVariantMap();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread,&QThread::finished,m_worker,&QObject::deleteLater);
    m_thread.start();
    m_poll.setInterval(2000);
    m_attributes.setInterval(30000);
    connect(&m_poll,&QTimer::timeout,this,[this]{refresh(false);});
    connect(&m_attributes,&QTimer::timeout,this,[this]{
        if (m_busy) return;
        const int currentPage=m_page;
        dispatch([currentPage](GuiWorker *w){return w->inspect(currentPage,false,true);});
    });
    auto *kwin=new QDBusServiceWatcher("org.kde.KWin",QDBusConnection::sessionBus(),QDBusServiceWatcher::WatchForOwnerChange,this);
    connect(kwin,&QDBusServiceWatcher::serviceOwnerChanged,this,[this]{refresh(true);});
    // Watching parents keeps QSaveFile's atomic replacement visible to other windows.
    connect(&m_files,&QFileSystemWatcher::fileChanged,this,[this]{refresh(true);});
    connect(&m_files,&QFileSystemWatcher::directoryChanged,this,[this]{
        if (!m_busy) {
            const int currentPage=m_page;
            dispatch([currentPage](GuiWorker *w){return w->inspect(currentPage,false,true);});
        } else {m_pending=true;}
    });
    m_poll.start();m_attributes.start();
    QTimer::singleShot(0,this,[this]{refresh(true);});
}
GuiController::~GuiController() { m_thread.quit();m_thread.wait(); }
QString GuiController::configFile() const { return configPath(); }
void GuiController::dispatch(const std::function<QVariantMap(GuiWorker *)> &operation,
                             const std::function<void(const QVariantMap &)> &complete) {
    if (m_busy) return;
    m_busy=true;emit changed();
    QPointer<GuiController> guard(this);
    auto *worker=m_worker;
    QMetaObject::invokeMethod(worker,[worker,guard,operation,complete]{
        const auto result=operation(worker);
        if (!guard) return;
        QMetaObject::invokeMethod(guard,[guard,result,complete]{
            if (!guard) return;
            guard->m_busy=false;
            guard->accept(result);
            if (complete) complete(result);
            emit guard->changed();
            if (guard->m_pending) {
                const bool full=guard->m_fullPending;
                guard->m_pending=guard->m_fullPending=false;
                QTimer::singleShot(0,guard,[guard,full]{if(guard)guard->refresh(full);});
            }
        },Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void GuiController::accept(const QVariantMap &snapshot) {
    m_latest=snapshot.value("configuration").toMap();
    m_latestRevision=snapshot.value("revision").toString();
    m_configError=snapshot.value("config_error").toString();
    if (!m_ready || !dirty()) {
        m_saved=m_draft=m_latest;m_revision=m_latestRevision;m_conflict=false;
    } else if (m_revision!=m_latestRevision || !m_configError.isEmpty()) {
        m_conflict=true;
        m_message="配置已被外部修改。草稿已保留，请撤销修改以重新加载。";
    }
    m_ready=true;
    m_status=snapshot.value("status").toMap();
    if (snapshot.contains("devices")) {
        m_devices=snapshot.value("devices").toList();
        const auto error=snapshot.value("devices_error").toString();
        m_status["devices_error"]=error;
    }
    if (snapshot.contains("diagnostics")) {
        m_diagnostics=snapshot.value("diagnostics").toMap();
        const auto error=snapshot.value("diagnostics_error").toString();
        if (!error.isEmpty()) m_diagnostics["error"]=error;
    }
    QStringList watch;
    for (const auto &path:snapshot.value("paths").toStringList()) {
        if (QFileInfo::exists(path)) watch<<path;
        auto parent=QFileInfo(path).path();
        while (!QFileInfo::exists(parent) && parent!="/") parent=QFileInfo(parent).path();
        watch<<parent;
    }
    watch.removeDuplicates();
    const auto old=m_files.files()+m_files.directories();
    QStringList remove,add;
    for (const auto &path:old) if (!watch.contains(path)) remove<<path;
    for (const auto &path:watch) if (!old.contains(path)) add<<path;
    if (!remove.isEmpty()) m_files.removePaths(remove);
    if (!add.isEmpty()) m_files.addPaths(add);
}
QVariantList GuiController::devices() const {
    auto list=m_devices;
    QSet<QString> connected;
    const auto excluded=m_draft.value("excluded_devices").toStringList();
    for (auto &item:list) {
        auto device=item.toMap();
        connected.insert(device["id"].toString());
        device["draft_excluded"]=excluded.contains(device["id"].toString());
        device["connected"]=true;item=device;
    }
    // Include saved exclusions even while a pending draft removes them.
    auto known=m_saved.value("excluded_devices").toStringList()+excluded;
    known.removeDuplicates();
    for (const auto &id:known) if (!connected.contains(id)) list<<QVariantMap{
        {"id",id},{"name","未连接的设备"},{"eligible",true},{"connected",false},
        {"accelerated",false},{"draft_excluded",excluded.contains(id)}};
    return list;
}
void GuiController::setPage(int page) {
    if (page<0 || page>2 || m_page==page) return;
    m_page=page;emit changed();refresh(false);
}
void GuiController::setValue(const QString &key, const QVariant &value) {
    if (!valid() || m_applying) return;
    if (key=="pointer_speed") {
        if (value.toInt()<1 || value.toInt()>20) return;
        m_draft[key]=value.toInt();
    } else if (key=="enabled" || key=="enhance_pointer_precision" || key=="trace") m_draft[key]=value.toBool();
    else return;
    m_message.clear();emit changed();
}
void GuiController::setExcluded(const QString &id, bool excluded) {
    if (!valid() || m_applying) return;
    bool eligible=false;
    for (const auto &entry:devices()) {
        const auto device=entry.toMap();
        if (device["id"]==id && device["eligible"].toBool()) eligible=true;
    }
    if (!eligible) return;
    auto list=m_draft["excluded_devices"].toStringList();
    list.removeAll(id);if(excluded)list.append(id);
    m_draft["excluded_devices"]=QJsonArray::fromStringList(list).toVariantList();m_message.clear();emit changed();
}
void GuiController::refresh(bool full) {
    if (m_busy) { m_pending=true;m_fullPending|=full;return; }
    const int currentPage=m_page;
    dispatch([currentPage,full](GuiWorker *w){return w->inspect(currentPage,full);});
}
void GuiController::revert() {
    if (m_busy) return;
    m_draft=m_saved=m_latest;m_revision=m_latestRevision;m_conflict=false;m_message.clear();
    emit changed();refresh(true);
}
void GuiController::apply() {
    if (!valid() || !dirty() || m_busy || m_conflict) return;
    QString error;
    const auto target=parseConfig(QJsonObject::fromVariantMap(m_draft),&error);
    if (!error.isEmpty()) {m_message=error;emit changed();return;}
    const auto revision=m_revision;
    const int currentPage=m_page;
    m_applying=true;
    dispatch([target,revision,currentPage](GuiWorker *w){
        const auto result=w->control().commit(target,revision);
        auto state=w->inspect(currentPage,true);
        state["saved"]=result.saved;state["applied"]=result.applied;
        state["conflict"]=result.conflict;state["error"]=result.error;
        return state;
    },[this](const QVariantMap &result){
        m_applying=false;
        const bool saved=result.value("saved").toBool(),applied=result.value("applied").toBool();
        if (saved) {
            m_draft=m_saved=m_latest;m_revision=m_latestRevision;m_conflict=false;
            m_message=applied?"配置已保存并生效。":"配置已保存，尚未应用。"+result.value("error").toString();
        } else {
            m_conflict|=result.value("conflict").toBool();
            m_message="保存失败："+result.value("error").toString();
        }
        emit applyFinished(saved,applied);
    });
}
void GuiController::resetState() {
    if (m_busy) return;
    const int currentPage=m_page;
    dispatch([currentPage](GuiWorker *w){
        QString error;const auto response=w->control().request("ResetState",{},&error);
        auto result=w->inspect(currentPage);result["action_error"]=error;
        result["reset_ok"]=response["ok"].toBool();return result;
    },[this](const QVariantMap &result){
        m_message=result.value("reset_ok").toBool()?"算法状态已重置。":"重置失败："+result.value("action_error").toString();
    });
}
void GuiController::exportDiagnostics(const QUrl &url) {
    if (m_busy) return;
    if (!url.isLocalFile()) {m_message="请选择本地文件。";emit changed();return;}
    const auto path=url.toLocalFile();
    if (QFileInfo(path).absoluteFilePath()==QFileInfo(configPath()).absoluteFilePath()) {
        m_message="诊断导出不能覆盖程序配置。";emit changed();return;
    }
    dispatch([path](GuiWorker *w){
        auto snapshot=w->inspect(2,true,false,true);
        const QJsonObject report{{"exported_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"configuration",QJsonObject::fromVariantMap(snapshot["configuration"].toMap())},
            {"configuration_error",snapshot["config_error"].toString()},
            {"status",QJsonObject::fromVariantMap(snapshot["status"].toMap())},
            {"devices",QJsonArray::fromVariantList(snapshot["devices"].toList())},
            {"devices_error",snapshot["devices_error"].toString()},
            {"diagnostics",QJsonObject::fromVariantMap(snapshot["diagnostics"].toMap())},
            {"diagnostics_error",snapshot["diagnostics_error"].toString()}};
        QString error;const bool ok=writeJson(path,report,&error);
        snapshot["export_message"]=ok?"诊断已导出到 "+path:"导出失败："+error;return snapshot;
    },[this](const QVariantMap &result){m_message=result.value("export_message").toString();});
}
void GuiController::setPollingActive(bool active) {
    if (m_polling==active) return;
    m_polling=active;
    if (active) {m_poll.start();m_attributes.start();refresh(true);}
    else {m_poll.stop();m_attributes.stop();}
}
void GuiController::copyText(const QString &text) { QGuiApplication::clipboard()->setText(text); }
}
