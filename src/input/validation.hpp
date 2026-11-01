#pragma once
#include "report.hpp"
#include "core/pointer_engine.hpp"
#include <optional>
#include <span>
namespace pa
{
std::optional<Motion> rawMotion(double x, double y);
bool batchMatches(std::span<const PaReport> reports,
    double dx,
    double dy,
    double rawX,
    double rawY,
    std::uint64_t time,
    const char* source);
}
