#pragma once
#include <cstddef>
#include <cstdint>

inline constexpr std::size_t PA_REPORT_CAPACITY = 16384;
struct PaReport
{
	double dx, dy, rawX, rawY;
	std::uint64_t timeUsec;
	char sysName[64];
};
