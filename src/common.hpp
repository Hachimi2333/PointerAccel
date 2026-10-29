#pragma once
#include "capture_api.hpp"
#include <windows_pointer/engine.hpp>
#include <QJsonObject>
#include <QStringList>
#include <optional>
#include <span>

namespace pa {
inline constexpr auto dbusPath = "/org/kde/KWin/PointerAccel";
inline constexpr auto dbusInterface = "org.kde.KWin.PointerAccel1";
struct Config {
    bool enabled = true;
    windows_pointer::Settings settings{};
    QStringList excluded;
    bool trace = false;
};
QString dataRoot();
QString configRoot();
QString stateRoot();
QString configPath();
QString overridePath();
QString cliPath();
QString manifestPath();
QString pluginPath();
QString kwinConfigPath();
QString digest(const QString &path);
bool compatible(QString *reason = nullptr);
QJsonObject buildInfo();
QJsonObject toJson(const Config &config);
Config parseConfig(const QJsonObject &json, QString *error = nullptr);
Config readConfig(QString *error = nullptr);
bool writeConfig(const Config &config, QString *error);
bool writeJson(const QString &path, const QJsonObject &json, QString *error = nullptr);
QJsonObject readJson(const QString &path, QString *error = nullptr);
std::optional<windows_pointer::Motion> rawMotion(double x, double y);
bool batchMatches(std::span<const PaReport> reports, double dx, double dy,
                  double rawX, double rawY, std::uint64_t time, const char *source);
}
