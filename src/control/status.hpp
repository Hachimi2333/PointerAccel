#pragma once
#include "config/config.hpp"
namespace pa
{
QJsonObject composeStatus(const ConfigSnapshot& snapshot,
    const QJsonObject& startup,
    QJsonObject status,
    const QString& transportError,
    bool installed,
    bool direct,
    bool autostart);
}
