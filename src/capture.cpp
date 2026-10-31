#include "capture.hpp"
#include "import_hooks.hpp"
#include <libinput.h>
#include <array>
#include <atomic>
#include <cstring>

namespace pa::capture
{
namespace
{
struct State
{
	std::array<PaReport, PA_REPORT_CAPACITY> reports{};
	std::size_t count = 0;
	bool overflow = false;
	libinput_event_pointer* last = nullptr;
	std::atomic<void*> owner{nullptr};
};

State state;
thread_local char threadToken;
ImportHooks hooks;

void record(libinput_event_pointer* event)
{
	if (state.owner.load(std::memory_order_acquire) != &threadToken || state.last == event)
		return;
	state.last = event;
	if (state.count == state.reports.size())
	{
		state.overflow = true;
		return;
	}
	auto* base = libinput_event_pointer_get_base_event(event);
	if (libinput_event_get_type(base) != LIBINPUT_EVENT_POINTER_MOTION)
	{
		state.overflow = true;
		return;
	}
	auto& r = state.reports[state.count++];
	r = {libinput_event_pointer_get_dx(event),
	    libinput_event_pointer_get_dy(event),
	    libinput_event_pointer_get_dx_unaccelerated(event),
	    libinput_event_pointer_get_dy_unaccelerated(event),
	    libinput_event_pointer_get_time_usec(event),
	    {}};
	const char* name = libinput_device_get_sysname(libinput_event_get_device(base));
	if (!name || std::strlen(name) >= sizeof(r.sysName))
		state.overflow = true;
	else
		std::strcpy(r.sysName, name);
}

double dx(libinput_event_pointer* event)
{
	record(event);
	return libinput_event_pointer_get_dx(event);
}

double dy(libinput_event_pointer* event)
{
	record(event);
	return libinput_event_pointer_get_dy(event);
}

void destroy(libinput_event* event)
{
	if (state.owner.load(std::memory_order_acquire) == &threadToken && state.last &&
	    libinput_event_pointer_get_base_event(state.last) == event)
		state.last = nullptr;
	libinput_event_destroy(event);
}
}

bool install(const QString& library, QString* error)
{
	return hooks.install(library,
	    {{"libinput_event_pointer_get_dx",
	         reinterpret_cast<void*>(dx),
	         reinterpret_cast<void*>(libinput_event_pointer_get_dx)},
	        {"libinput_event_pointer_get_dy",
	            reinterpret_cast<void*>(dy),
	            reinterpret_cast<void*>(libinput_event_pointer_get_dy)},
	        {"libinput_event_destroy",
	            reinterpret_cast<void*>(destroy),
	            reinterpret_cast<void*>(libinput_event_destroy)}},
	    error);
}

bool uninstall(QString* error)
{
	arm(false);
	return hooks.restore(error);
}

void arm(bool enabled)
{
	state.owner.store(nullptr, std::memory_order_release);
	state.count = 0;
	state.overflow = false;
	state.last = nullptr;
	if (enabled)
		state.owner.store(&threadToken, std::memory_order_release);
}

std::size_t drain(PaReport* out, std::size_t capacity, bool* overflow)
{
	const auto n = std::min(state.count, capacity);
	std::memcpy(out, state.reports.data(), n * sizeof(PaReport));
	*overflow = state.overflow || state.count > capacity;
	state.count = 0;
	state.overflow = false;
	return n;
}
#ifdef PA_TESTING
void appendForTest(const PaReport& report)
{
	if (state.count == state.reports.size())
		state.overflow = true;
	else
		state.reports[state.count++] = report;
}
#endif
}
