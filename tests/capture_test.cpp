#include "capture.hpp"
#include <libinput.h>
#include <dlfcn.h>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <QFile>

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
#define CHECK(expr)                                              \
	do                                                           \
	{                                                            \
		if (!(expr))                                             \
		{                                                        \
			std::cerr << "line " << __LINE__ << ": " #expr "\n"; \
			return 1;                                            \
		}                                                        \
	} while (false)

int main()
{
	Dl_info info{};
	CHECK(dladdr(reinterpret_cast<void*>(consume_x), &info));
	auto protections = [&]
	{
		QFile maps("/proc/self/maps");
		if (!maps.open(QIODevice::ReadOnly))
			return QByteArray();
		QByteArray result;
		const auto name = QByteArray(info.dli_fname).split('/').last();
		for (const auto& line : maps.readAll().split('\n'))
			if (line.contains(name))
				result += line + '\n';
		return result;
	};
	const auto before = protections();
	QString error;
	CHECK(pa::capture::install(QString::fromLocal8Bit(info.dli_fname), &error));
	CHECK(protections() == before);
	const PaCaptureApi apiValue{1, pa::capture::arm, pa::capture::drain};
	const auto* api = &apiValue;
	std::vector<PaReport> reports(PA_REPORT_CAPACITY);
	FakeDevice mouse{"event17"}, touchpad{"event7"};
	FakeEvent first{&mouse, 1.25, -2.5, 1, -2, 10}, second{&touchpad, -0.5, 1, 0.25, 0.75, 11};
	auto* a = reinterpret_cast<libinput_event_pointer*>(&first);
	auto* b = reinterpret_cast<libinput_event_pointer*>(&second);
	api->arm(true);
	CHECK(consume_y(a) == -2.5);
	CHECK(consume_x(a) == 1.25);
	CHECK(libinput_event_pointer_get_dx_unaccelerated(a) == 1);
	CHECK(consume_x(b) == -0.5);
	CHECK(consume_y(b) == 1);
	consume_destroy(reinterpret_cast<libinput_event*>(b));
	bool overflow = false;
	CHECK(api->drain(reports.data(), reports.size(), &overflow) == 2);
	CHECK(!overflow);
	CHECK(reports[0].rawX == 1 && reports[0].rawY == -2 && reports[1].rawX == 0.25);
	CHECK(std::strcmp(reports[0].sysName, "event17") == 0 && std::strcmp(reports[1].sysName, "event7") == 0);
	// An address reused after destruction must produce a fresh report.
	consume_destroy(reinterpret_cast<libinput_event*>(a));
	first.rx = 3;
	consume_x(a);
	CHECK(api->drain(reports.data(), reports.size(), &overflow) == 1 && reports[0].rawX == 3);
	consume_destroy(reinterpret_cast<libinput_event*>(a));
	std::thread unrelated(
	    [a]
	    {
		    consume_x(a);
	    });
	unrelated.join();
	CHECK(api->drain(reports.data(), reports.size(), &overflow) == 0);
	// Overflow must be signalled and drained, never silently drop and accelerate the remainder.
	for (std::size_t i = 0; i < PA_REPORT_CAPACITY + 1; ++i)
	{
		consume_x(a);
		consume_destroy(reinterpret_cast<libinput_event*>(a));
	}
	CHECK(api->drain(reports.data(), reports.size(), &overflow) == PA_REPORT_CAPACITY && overflow);
	api->arm(false);
	consume_x(a);
	CHECK(api->drain(reports.data(), reports.size(), &overflow) == 0 && !overflow);
	const auto start = std::chrono::steady_clock::now();
	api->arm(true);
	for (int batch = 0; batch < 1000; ++batch)
	{
		for (int i = 0; i < 1000; ++i)
		{
			consume_x(a);
			consume_y(a);
			consume_destroy(reinterpret_cast<libinput_event*>(a));
		}
		CHECK(api->drain(reports.data(), reports.size(), &overflow) == 1000 && !overflow);
	}
	auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
	std::cout << "collector: 1000000 reports, " << ns / 1000000.0 << " ns/report (synthetic, not end-to-end)\n";
	CHECK(pa::capture::uninstall(&error));
	CHECK(protections() == before);
	CHECK(consume_x(a) == first.dx);
}
