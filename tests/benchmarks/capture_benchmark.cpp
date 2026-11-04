// Synthetic collection cost only; not compositor or hardware end-to-end latency.
#include "input/capture.hpp"
#include "support/fake_input.hpp"
#include <dlfcn.h>
#include <chrono>
#include <iostream>
#include <vector>
int main()
{
	Dl_info info{};
	if (!dladdr(reinterpret_cast<void*>(consume_x), &info))
		return 1;
	QString error;
	if (!pa::capture::install(QString::fromLocal8Bit(info.dli_fname), &error))
		return 1;
	FakeDevice mouse{"event17"};
	FakeEvent event{&mouse, 1.25, -2.5, 1, -2, 10};
	auto* pointer = reinterpret_cast<libinput_event_pointer*>(&event);
	std::vector<PaReport> reports(PA_REPORT_CAPACITY);
	pa::capture::arm(true);
	bool ok = true;
	const auto start = std::chrono::steady_clock::now();
	for (int batch = 0; batch < 1000; ++batch)
	{
		for (int i = 0; i < 1000; ++i)
		{
			consume_x(pointer);
			consume_y(pointer);
			consume_destroy(reinterpret_cast<libinput_event*>(pointer));
		}
		bool overflow = false;
		ok &= pa::capture::drain(reports.data(), reports.size(), &overflow) == 1000 && !overflow;
	}
	const auto ns =
	    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
	ok &= pa::capture::uninstall(&error);
	std::cout << "Synthetic collector: " << ns / 1000000.0 << " ns/report\n";
	return ok ? 0 : 1;
}
