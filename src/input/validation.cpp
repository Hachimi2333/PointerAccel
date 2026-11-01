#include "validation.hpp"
#include <cmath>
#include <cstring>
#include <limits>
namespace pa
{
std::optional<pa::Motion> rawMotion(double x, double y)
{
	auto valid = [](double v)
	{
		return std::isfinite(v) && v >= std::numeric_limits<std::int32_t>::min() &&
		       v <= std::numeric_limits<std::int32_t>::max() && std::abs(v - std::round(v)) <= 1e-9;
	};
	if (!valid(x) || !valid(y))
		return std::nullopt;
	return pa::Motion{static_cast<std::int32_t>(std::round(x)), static_cast<std::int32_t>(std::round(y))};
}

bool batchMatches(std::span<const PaReport> reports,
    double dx,
    double dy,
    double rx,
    double ry,
    std::uint64_t time,
    const char* source)
{
	if (reports.empty() || reports.back().timeUsec != time || std::strcmp(reports.front().sysName, source) != 0)
		return false;
	double sx = 0, sy = 0, srx = 0, sry = 0;
	for (const auto& r : reports)
	{
		if (!std::isfinite(r.dx) || !std::isfinite(r.dy) || !std::isfinite(r.rawX) || !std::isfinite(r.rawY))
			return false;
		sx += r.dx;
		sy += r.dy;
		srx += r.rawX;
		sry += r.rawY;
	}
	auto same = [](double a, double b)
	{
		return std::isfinite(b) && std::abs(a - b) <= 1e-8;
	};
	return same(sx, dx) && same(sy, dy) && same(srx, rx) && same(sry, ry);
}
}
