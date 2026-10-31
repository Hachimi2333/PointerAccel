#pragma once
#include "capture_api.hpp"
#include <QString>

namespace pa::capture
{
bool install(const QString& library, QString* error);
bool uninstall(QString* error);
void arm(bool enabled);
std::size_t drain(PaReport* destination, std::size_t capacity, bool* overflow);
#ifdef PA_TESTING
void appendForTest(const PaReport& report);
#endif
}
