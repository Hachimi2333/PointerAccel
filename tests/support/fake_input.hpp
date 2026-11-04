#pragma once
#include <libinput.h>
#include <cstdint>

struct FakeDevice
{
	const char* name;
};
struct FakeEvent
{
	FakeDevice* device;
	double dx, dy, rx, ry;
	std::uint64_t time;
};
extern "C" double consume_x(libinput_event_pointer*);
extern "C" double consume_y(libinput_event_pointer*);
extern "C" void consume_destroy(libinput_event*);
