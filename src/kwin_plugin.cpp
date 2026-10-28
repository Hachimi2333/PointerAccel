#include "common.hpp"
#include "capture.hpp"
#include "build_info.hpp"
#include <kwin/plugin.h>
#include <kwin/input.h>
#include <kwin/pointer_input.h>
#include <kwin/workspace.h>
#include <kwin/core/output.h>
#include <kwin/wayland_server.h>
#include <kwin/wayland/seat.h>
#include <kwin/input_event.h>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <libudev.h>
#include <dlfcn.h>
#include <array>
#include <cmath>
#include <map>
#include <cstring>
#ifdef PA_TESTING
#include "virtual_devices.hpp"
#endif

namespace {
QString jsonText(const QJsonObject &json) { return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)); }
struct Device {
    QPointer<KWin::InputDevice> pointer;
    QString sysName, id, name;
    bool eligible = false;
    QMetaObject::Connection replacement;
};
struct Sample {
    QString device;
    double rawX=0,rawY=0,scale=1;
    windows_pointer::Motion output;
    std::uint16_t dpi=96;
    std::uint64_t time=0;
};
QJsonObject sampleJson(const Sample &s) {
    return {{"device",s.device},{"dpi",s.dpi},{"scale",s.scale},{"raw",QJsonArray{s.rawX,s.rawY}},
            {"output",QJsonArray{s.output.x,s.output.y}},{"time_usec",static_cast<double>(s.time)}};
}
class ZeroRawFilter final : public KWin::InputEventFilter {
public:
    explicit ZeroRawFilter(const bool &pending):InputEventFilter(KWin::InputFilterOrder::Forward),pending(pending) {}
    bool pointerMotion(KWin::PointerMotionEvent *event) override {
        if (!pending || event->warp || !event->delta.isNull() || event->deltaUnaccelerated.isNull()) return false;
        // KWin 6.7 skips relativePointerMotion when the accelerated delta is zero.
        // Integer Windows motion can legitimately be zero with nonzero raw counts.
        // This filter sits immediately before Forward, after lock/drag/effect filters.
        auto *seat=KWin::waylandServer()->seat();
        seat->setTimestamp(event->timestamp);seat->notifyPointerMotion(event->position);
        seat->relativePointerMotion(event->delta,event->deltaUnaccelerated,event->timestamp);
        return true;
    }
private:
    const bool &pending;
};
}

class PointerAccelPlugin final : public KWin::Plugin {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.PointerAccel1")
public:
    PointerAccelPlugin() {
        clock.start();
        QDBusConnection::sessionBus().registerObject(pa::dbusPath,this,QDBusConnection::ExportScriptableSlots);
        QTimer::singleShot(0,this,[this] {
            if (!KWin::input()) { fault="KWin 输入管理器尚不可用。";return; }
            connect(KWin::input(),&KWin::InputRedirection::deviceAdded,this,[this](auto *d) {
                addDevice(d);
                if (auto it=devices.find(d);active && it!=devices.end() && !attach(it->second)) fail("新设备的原输入连接无法接管。");
            });
            connect(KWin::input(),&KWin::InputRedirection::deviceRemoved,this,[this](auto *d) {
                auto it=devices.find(d);
                if (it!=devices.end()) { restore(it->second); devices.erase(it); }
            });
            for (auto *device : KWin::input()->devices()) addDevice(device);
            apply();
        });
    }
    ~PointerAccelPlugin() override {
        deactivate();
        QDBusConnection::sessionBus().unregisterObject(pa::dbusPath);
    }
public Q_SLOTS:
    Q_SCRIPTABLE QString Status() const {
        const bool installed=QFile::exists(pa::manifestPath());
        return jsonText({{"installed",installed},{"loaded",true},{"configured_enabled",config.enabled},
                         {"active",active},{"temporary_override",temporary.has_value()},
                         {"healthy",fault.isEmpty()},{"compatible",compatibility},
                         {"state",!fault.isEmpty()?"error":!active?"disabled":processed==0?"idle":"active"},
                         {"error",fault},{"processed",static_cast<double>(processed)},
                         {"settings",pa::toJson(config)}});
    }
    Q_SCRIPTABLE QString Info() const { return jsonText(pa::buildInfo()); }
    Q_SCRIPTABLE QString Devices() const {
        QJsonArray list;
        for (const auto &[_,d] : devices) {
            list.append(QJsonObject{{"id",d.id},{"name",d.name},{"sys_name",d.sysName},
                        {"eligible",d.eligible},{"excluded",config.excluded.contains(d.id)},
                        {"accelerated",active && d.eligible && !config.excluded.contains(d.id)}});
        }
        return jsonText({{"devices",list}});
    }
    Q_SCRIPTABLE QString Diagnostics() const {
        QJsonObject result{{"processed",static_cast<double>(processed)},{"passthrough",static_cast<double>(passthrough)},
                           {"batches",static_cast<double>(batches)},{"last_age_ms",lastReport<0?-1:(clock.elapsed()-lastReport)},
                           {"last",lastReport<0?QJsonObject():sampleJson(last)},{"error",fault},{"trace_enabled",config.trace},
                           {"trace_samples",traceCount},{"trace_capacity",traceCapacity}};
        if (config.trace) {
            QJsonArray samples;
            for(int i=0;i<traceCount;++i) samples.append(sampleJson(trace[(traceNext+traceCapacity-traceCount+i)%traceCapacity]));
            result["trace"]=samples;
        }
        return jsonText(result);
    }
    Q_SCRIPTABLE QString ApplyConfig() { temporary.reset();apply();return Status(); }
    Q_SCRIPTABLE QString SetTemporaryEnabled(bool enabled) { temporary=enabled;apply();return Status(); }
    Q_SCRIPTABLE QString ResetState() { engine.reset();return jsonText({{"ok",true}}); }
    Q_SCRIPTABLE QString Shutdown() { temporary=false;deactivate();return Status(); }
#ifdef PA_TESTING
    Q_SCRIPTABLE QString TestHotplug() {
        if (!testBackend || !QCoreApplication::arguments().contains("--virtual")) return "{}";
        auto *old=testBackend->mice[1];const auto sys=old->sys;
        Q_EMIT testBackend->deviceRemoved(old);delete old;
        auto *replacement=new TestMouse(sys,false);testBackend->mice[1]=replacement;
        Q_EMIT testBackend->deviceAdded(replacement);devices.at(replacement).eligible=true;
        return Status();
    }
    Q_SCRIPTABLE QString TestEnvironment() const {
        return jsonText({{"DISPLAY",qEnvironmentVariable("DISPLAY")},{"WAYLAND_DISPLAY",qEnvironmentVariable("WAYLAND_DISPLAY")},
                         {"XAUTHORITY",qEnvironmentVariable("XAUTHORITY")}});
    }
    Q_SCRIPTABLE QString TestReports(const QString &payload) {
        if (!QCoreApplication::arguments().contains("--virtual") || qgetenv("POINTERACCEL_ISOLATED_TEST")!="1") return "{}";
        if (!testBackend) {
            auto backend=std::make_unique<TestBackend>();testBackend=backend.get();
            KWin::input()->addInputBackend(std::move(backend));
            for (auto *m:testBackend->mice) devices.at(m).eligible=!m->isTouchpad();
        }
        const auto input=QJsonDocument::fromJson(payload.toUtf8()).object();
        const auto list=input["reports"].toArray();
        if (list.isEmpty()) return "{}";
        if (input["center"].toBool(true)) KWin::input()->warpPointer(KWin::workspace()->geometry().center());
        engine.reset();windows_pointer::Engine reference(config.settings);
        MotionSpy spy;KWin::input()->installInputEventSpy(&spy);
        QPointF sum,raw;std::uint64_t timestamp=0;QJsonArray expected;
        auto *first=testBackend->mice.at(list[0].toObject()["device"].toInt());
        for (const auto &value:list) {
            const auto o=value.toObject();const int which=o["device"].toInt();
            PaReport r{o["dx"].toDouble(),o["dy"].toDouble(),o["x"].toDouble(),o["y"].toDouble(),
                       static_cast<std::uint64_t>(o["time"].toDouble()),{}};
            auto *m=testBackend->mice.at(which);
            std::strncpy(r.sysName,m->sys.toLocal8Bit().constData(),sizeof(r.sysName)-1);
            if (active) pa::capture::appendForTest(r);
            sum+=QPointF(r.dx,r.dy);raw+=QPointF(r.rawX,r.rawY);timestamp=r.timeUsec;
            QPointF expectedDelta(r.dx,r.dy);
            if (active && !m->pad && !config.excluded.contains(devices.at(m).id)) {
                const auto integer=pa::rawMotion(r.rawX,r.rawY);
                if (integer) {
                    const auto *output=KWin::workspace()->outputAt(KWin::input()->globalPointer());
                    auto out=reference.apply(*integer,windows_pointer::displayDpiFromScale(output?output->scale():1));
                    expectedDelta={double(out.x),double(out.y)};
                }
            }
            expected.append(QJsonObject{{"dx",expectedDelta.x()},{"dy",expectedDelta.y()},
                {"raw_x",r.rawX},{"raw_y",r.rawY},{"device",m->name()},{"time",double(r.timeUsec)}});
        }
        if (input["corrupt"].toBool()) raw.rx()+=1;
        Q_EMIT first->pointerMotion(sum,raw,std::chrono::microseconds(timestamp),first);
        Q_EMIT first->pointerFrame(first);
        KWin::input()->uninstallInputEventSpy(&spy);
        return jsonText({{"observed",spy.samples},{"expected",expected},{"status",QJsonDocument::fromJson(Status().toUtf8()).object()}});
    }
#endif
private:
    void addDevice(KWin::InputDevice *p) {
        // Libinput devices expose sysName; other backends keep their original connections.
        const auto sys=p->property("sysName").toString();
        if (!p->isPointer() || sys.isEmpty()) return;
        Device d; d.pointer=p; d.sysName=sys; d.name=p->name();
        QString identity=QString::number(p->vendor())+":"+QString::number(p->product())+":"+p->name();
        auto *context=udev_new();
        auto *native=context?udev_device_new_from_subsystem_sysname(context,"input",sys.toLocal8Bit().constData()):nullptr;
        if (native) {
            auto property=[native](const char *key) {return QString::fromUtf8(udev_device_get_property_value(native,key));};
            auto serial=property("ID_SERIAL");
            identity += ":"+(serial.isEmpty()?property("ID_PATH"):serial);
            d.eligible=property("ID_INPUT_MOUSE")=="1" && property("ID_INPUT_TOUCHPAD")!="1"
                && !p->isTouchpad() && !p->isTabletTool() && !p->isTouch()
                && !QString::fromUtf8(udev_device_get_syspath(native)).startsWith("/sys/devices/virtual/");
            udev_device_unref(native);
        }
        if (context) udev_unref(context);
        d.id="mouse-"+QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(),QCryptographicHash::Sha256).toHex().left(24));
        devices.emplace(p,std::move(d));
    }
    bool attach(Device &d) {
        if (!d.pointer || d.replacement) return true;
        const bool removed=QObject::disconnect(d.pointer,&KWin::InputDevice::pointerMotion,
                            KWin::input()->pointer(),&KWin::PointerInputRedirection::processMotion);
        if (!removed) return false;
        d.replacement=connect(d.pointer,&KWin::InputDevice::pointerMotion,this,
                     [this,sys=d.sysName](const QPointF &delta,const QPointF &raw,std::chrono::microseconds time,KWin::InputDevice *source) {
                         motion(delta,raw,time,source,sys);
                     });
        return bool(d.replacement);
    }
    void restore(Device &d) {
        if (!d.replacement) return;
        disconnect(d.replacement);d.replacement={};
        if (d.pointer && KWin::input()) connect(d.pointer,&KWin::InputDevice::pointerMotion,
            KWin::input()->pointer(),&KWin::PointerInputRedirection::processMotion,Qt::UniqueConnection);
    }
    bool deactivate() {
        pa::capture::arm(false);
        zeroRaw.reset();replayingZero=false;
        for (auto &[_,d] : devices) restore(d);
        active=false;
        QString why;
        if (!pa::capture::uninstall(&why)) {fault=why;return false;}
        return true;
    }
    void fail(const QString &message) {
        fault=message;deactivate();
        qWarning().noquote()<<"PointerAccel:"<<fault;
    }
    void apply() {
        const bool wasActive=active;
        if (!deactivate()) return;
        fault.clear();
        config=pa::readConfig(&fault);
        if (!config.trace) {traceCount=0;traceNext=0;}
        compatibility=pa::compatible(&fault);
        if (!compatibility || !fault.isEmpty()) return;
        engine.configure(config.settings);
        if (!temporary.value_or(config.enabled)) return;
        if (qEnvironmentVariableIntValue("KWIN_RESTART_COUNT")>0) {fault="KWin 本次为崩溃恢复启动，已跳过接入；请重新登录。";return;}
        if (!pa::capture::install(PA_KWIN_LIBRARY,&fault)) return;
        for (auto &[_,d] : devices) if (!attach(d)) {fail("无法接管 KWin 原生位移连接。");return;}
        if (!wasActive) engine.reset();
        zeroRaw=std::make_unique<ZeroRawFilter>(replayingZero);
        KWin::input()->installInputEventFilter(zeroRaw.get());
        pa::capture::arm(true);active=true;
    }
    void motion(const QPointF &delta,const QPointF &raw,std::chrono::microseconds time,KWin::InputDevice *source,const QString &sys) {
        bool overflow=false;
        const auto count=pa::capture::drain(reports.data(),reports.size(),&overflow);
        auto batch=std::span(reports.data(),count);
        bool valid=!overflow && pa::batchMatches(batch,delta.x(),delta.y(),raw.x(),raw.y(),time.count(),sys.toLocal8Bit().constData());
        std::array<Device *,PA_REPORT_CAPACITY> mapped;
        if (valid) for (std::size_t i=0;i<count;++i) {
            mapped[i]=nullptr;
            for (auto &[_,d] : devices) if (d.sysName==QLatin1StringView(reports[i].sysName)) {mapped[i]=&d;break;}
            if (!mapped[i] || !mapped[i]->pointer) {valid=false;break;}
        }
        if (!valid) {
            fail(overflow?"报告队列溢出，已恢复原生输入。":"报告与合并事件不一致或设备缺失，已恢复原生输入。");
            KWin::input()->pointer()->processMotion(delta,raw,time,source);
            return;
        }
        ++batches;
        // All validation precedes engine advancement: a rejected batch is replayed natively once.
        for (std::size_t i=0;i<count;++i) {
            const auto &r=reports[i];const auto *d=mapped[i];
            QPointF moved(r.dx,r.dy);
            const auto integer=pa::rawMotion(r.rawX,r.rawY);
            const bool accelerated=d->eligible && !config.excluded.contains(d->id) && integer.has_value();
            if (accelerated) {
                const auto *display=KWin::workspace()->outputAt(KWin::input()->globalPointer());
                const auto scale=display?display->scale():1.0;
                const auto dpi=windows_pointer::displayDpiFromScale(scale);
                const auto out=engine.apply(*integer,dpi);
                moved={static_cast<double>(out.x),static_cast<double>(out.y)};
                ++processed;lastReport=clock.elapsed();
                last={d->id,r.rawX,r.rawY,scale,out,dpi,r.timeUsec};
                if (config.trace) {
                    trace[traceNext]=last;traceNext=(traceNext+1)%traceCapacity;
                    traceCount=std::min(traceCount+1,traceCapacity);
                }
            } else ++passthrough;
            replayingZero=accelerated && moved.isNull() && (r.rawX!=0 || r.rawY!=0);
            KWin::input()->pointer()->processMotion(moved,{r.rawX,r.rawY},std::chrono::microseconds(r.timeUsec),d->pointer);
            replayingZero=false;
            // XWayland consumes pending relative motion at wl_pointer.frame. A single
            // frame for an entire replay batch would keep only its last relative delta.
            KWin::input()->pointer()->processFrame(d->pointer);
        }
    }
    std::array<PaReport,PA_REPORT_CAPACITY> reports{};
    std::map<KWin::InputDevice *,Device> devices;
    pa::Config config;
    windows_pointer::Engine engine;
    std::optional<bool> temporary;
    bool active=false,compatibility=false;
    QString fault;
    std::uint64_t processed=0,passthrough=0,batches=0;
    QElapsedTimer clock;
    qint64 lastReport=-1;
    Sample last;
    static constexpr int traceCapacity=4096;
    std::array<Sample,traceCapacity> trace;
    int traceNext=0,traceCount=0;
    bool replayingZero=false;
    std::unique_ptr<ZeroRawFilter> zeroRaw;
#ifdef PA_TESTING
    TestBackend *testBackend=nullptr;
#endif
};

class PointerAccelFactory final : public KWin::PluginFactory {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginFactory_iid FILE "kwin_plugin.json")
    Q_INTERFACES(KWin::PluginFactory)
public:
    std::unique_ptr<KWin::Plugin> create() const override { return std::make_unique<PointerAccelPlugin>(); }
};
#include "kwin_plugin.moc"
