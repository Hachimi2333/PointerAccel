#include "common.hpp"
#include "build_info.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>
#include <cstring>
#include <limits>

namespace pa {
QString dataRoot() { return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/pointeraccel"; }
QString configRoot() { return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/pointeraccel"; }
QString stateRoot() {
    auto base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty()) base = QDir::homePath() + "/.local/state";
    return base + "/pointeraccel";
}
QString configPath() { return configRoot() + "/config.json"; }
QString overridePath() { return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/systemd/user/plasma-kwin_wayland.service.d/80-pointeraccel.conf"; }
QString cliPath() { return QDir::homePath() + "/.local/bin/pointeraccel"; }
QString manifestPath() { return stateRoot()+"/manifest.tsv"; }
QString pluginPath() { return "/usr/lib64/qt6/plugins/kwin/plugins/pointeraccel_kwin.so"; }
QString kwinConfigPath() { return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+"/kwinrc"; }
QString digest(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}
bool compatible(QString *reason) {
    bool ok = digest(PA_KWIN_LIBRARY) == PA_KWIN_SHA256 && digest(PA_KWIN_EXECUTABLE) == PA_EXECUTABLE_SHA256;
    if (!ok && reason) *reason = "KWin 文件与构建时不同，请重新运行 install.sh 编译适配。";
    return ok;
}
QJsonObject buildInfo() {
    return {{"version", PA_VERSION}, {"kwin_version", PA_KWIN_VERSION},
            {"kwin_library", PA_KWIN_LIBRARY}, {"kwin_sha256", PA_KWIN_SHA256},
            {"engine", "windows-pointer-linux 1.0.0 (BSD-3-Clause)"},
            {"capture_abi", 1}, {"report_capacity", static_cast<int>(PA_REPORT_CAPACITY)}};
}
QJsonObject toJson(const Config &c) {
    return {{"enabled", c.enabled}, {"pointer_speed", c.settings.pointerSpeed},
            {"enhance_pointer_precision", c.settings.enhancePointerPrecision},
            {"excluded_devices", QJsonArray::fromStringList(c.excluded)}, {"trace", c.trace}};
}
QJsonObject readJson(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = "无效 JSON: " + parse.errorString();
        return {};
    }
    return doc.object();
}
Config readConfig(QString *error) {
    Config c;
    if (QFileInfo(configPath()).isSymLink()) {c.enabled=false;if(error)*error="配置不能是符号链接。";return c;}
    if (!QFile::exists(configPath())) return c;
    QString why;
    const auto json = readJson(configPath(), &why);
    if (why.isEmpty()) {
        const auto speed = json.value("pointer_speed");
        if (!json.value("enabled").isBool() || !speed.isDouble() || speed.toDouble() != speed.toInt()
            || speed.toInt() < 1 || speed.toInt() > 20 || !json.value("enhance_pointer_precision").isBool()
            || !json.value("excluded_devices").isArray() || !json.value("trace").isBool()) why = "配置字段类型或取值不正确。";
        for (const auto &id : json.value("excluded_devices").toArray()) {
            if (!id.isString() || id.toString().isEmpty()) why = "设备排除列表包含无效标识。";
            else c.excluded.append(id.toString());
        }
    }
    if (!why.isEmpty()) {
        c.enabled = false;
        if (error) *error = why;
        return c;
    }
    c.enabled = json["enabled"].toBool();
    c.settings.pointerSpeed = static_cast<std::uint8_t>(json["pointer_speed"].toInt());
    c.settings.enhancePointerPrecision = json["enhance_pointer_precision"].toBool();
    c.trace = json["trace"].toBool();
    return c;
}
bool writeJson(const QString &path, const QJsonObject &json, QString *error) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = "无法建立目录: " + path;
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const auto bytes = QJsonDocument(json).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit()) { if (error) *error = file.errorString(); return false; }
    return true;
}
bool writeConfig(const Config &config, QString *error) {
    if (QFileInfo(configPath()).isSymLink()) {if(error)*error="配置不能是符号链接。";return false;}
    return writeJson(configPath(), toJson(config), error);
}
std::optional<windows_pointer::Motion> rawMotion(double x, double y) {
    auto valid = [](double v) {
        return std::isfinite(v) && v >= std::numeric_limits<std::int32_t>::min()
            && v <= std::numeric_limits<std::int32_t>::max() && std::abs(v - std::round(v)) <= 1e-9;
    };
    if (!valid(x) || !valid(y)) return std::nullopt;
    return windows_pointer::Motion{static_cast<std::int32_t>(std::round(x)), static_cast<std::int32_t>(std::round(y))};
}
bool batchMatches(std::span<const PaReport> reports, double dx, double dy, double rx, double ry,
                  std::uint64_t time, const char *source) {
    if (reports.empty() || reports.back().timeUsec != time || std::strcmp(reports.front().sysName, source) != 0) return false;
    double sx=0, sy=0, srx=0, sry=0;
    for (const auto &r : reports) {
        if (!std::isfinite(r.dx) || !std::isfinite(r.dy) || !std::isfinite(r.rawX) || !std::isfinite(r.rawY)) return false;
        sx += r.dx; sy += r.dy; srx += r.rawX; sry += r.rawY;
    }
    auto same = [](double a, double b) { return std::isfinite(b) && std::abs(a-b) <= 1e-8; };
    return same(sx,dx) && same(sy,dy) && same(srx,rx) && same(sry,ry);
}
}
