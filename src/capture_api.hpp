#pragma once
#include <cstddef>
#include <cstdint>

// Private, versioned C ABI. No Qt or C++ objects cross the preload boundary.
inline constexpr std::size_t PA_REPORT_CAPACITY = 16384;

struct PaReport
{
	double dx, dy, rawX, rawY;
	std::uint64_t timeUsec;
	char sysName[64];
};

struct PaCaptureApi
{
	std::uint32_t version;
	void (*arm)(bool enabled);
	std::size_t (*drain)(PaReport* destination, std::size_t capacity, bool* overflow);
};

using PaGetCaptureApi = const PaCaptureApi* (*)();
