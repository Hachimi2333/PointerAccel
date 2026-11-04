#include "input/capture.hpp"
#include "input/import_hooks.hpp"
#include "support/fake_input.hpp"
#include <QFile>
#include <dlfcn.h>
#include <gtest/gtest.h>
#include <cstring>
#include <thread>
#include <vector>

class CaptureTest : public testing::Test
{
protected:
	QString library;
	QByteArray before;
	std::vector<PaReport> reports{PA_REPORT_CAPACITY};
	FakeDevice mouse{"event17"}, touchpad{"event7"};
	FakeEvent first{&mouse, 1.25, -2.5, 1, -2, 10}, second{&touchpad, -0.5, 1, 0.25, 0.75, 11};
	libinput_event_pointer* a = reinterpret_cast<libinput_event_pointer*>(&first);
	libinput_event_pointer* b = reinterpret_cast<libinput_event_pointer*>(&second);
	bool overflow = false;

	QByteArray protections()
	{
		QFile maps("/proc/self/maps");
		if (!maps.open(QIODevice::ReadOnly))
			return {};
		QByteArray result;
		const auto name = library.toLocal8Bit().split('/').last();
		for (const auto& line : maps.readAll().split('\n'))
			if (line.contains(name))
				result += line + '\n';
		return result;
	}
	void SetUp() override
	{
		Dl_info info{};
		ASSERT_TRUE(dladdr(reinterpret_cast<void*>(consume_x), &info));
		library = QString::fromLocal8Bit(info.dli_fname);
		before = protections();
		ASSERT_FALSE(before.isEmpty());
		QString error;
		ASSERT_TRUE(pa::capture::install(library, &error)) << error.toStdString();
		pa::capture::arm(true);
	}
	void TearDown() override
	{
		QString error;
		EXPECT_TRUE(pa::capture::uninstall(&error)) << error.toStdString();
		EXPECT_EQ(protections(), before);
	}
	std::size_t drain()
	{
		return pa::capture::drain(reports.data(), reports.size(), &overflow);
	}
};

TEST_F(CaptureTest, PreservesGetterValuesAndDeduplicatesBothReadOrders)
{
	EXPECT_EQ(consume_y(a), -2.5);
	EXPECT_EQ(consume_x(a), 1.25);
	EXPECT_EQ(libinput_event_pointer_get_dx_unaccelerated(a), 1);
	EXPECT_EQ(consume_x(b), -0.5);
	EXPECT_EQ(consume_y(b), 1);
	ASSERT_EQ(drain(), 2);
	EXPECT_FALSE(overflow);
	EXPECT_EQ(reports[0].rawX, 1);
	EXPECT_EQ(reports[0].rawY, -2);
	EXPECT_EQ(reports[1].rawX, 0.25);
	EXPECT_STREQ(reports[0].sysName, "event17");
	EXPECT_STREQ(reports[1].sysName, "event7");
}

TEST_F(CaptureTest, ReusedEventAddressProducesFreshReportAfterDestruction)
{
	consume_x(a);
	ASSERT_EQ(drain(), 1);
	consume_destroy(reinterpret_cast<libinput_event*>(a));
	first.rx = 3;
	consume_x(a);
	ASSERT_EQ(drain(), 1);
	EXPECT_EQ(reports[0].rawX, 3);
}

TEST_F(CaptureTest, IgnoresOtherThreadsAndDisarmedInput)
{
	std::thread unrelated(
	    [this]
	    {
		    consume_x(a);
	    });
	unrelated.join();
	EXPECT_EQ(drain(), 0);
	pa::capture::arm(false);
	consume_x(a);
	EXPECT_EQ(drain(), 0);
	EXPECT_FALSE(overflow);
}

TEST_F(CaptureTest, SignalsOverflowAndResetsItAfterDrain)
{
	for (std::size_t i = 0; i <= PA_REPORT_CAPACITY; ++i)
	{
		consume_x(a);
		consume_destroy(reinterpret_cast<libinput_event*>(a));
	}
	EXPECT_EQ(drain(), PA_REPORT_CAPACITY);
	EXPECT_TRUE(overflow);
	EXPECT_EQ(drain(), 0);
	EXPECT_FALSE(overflow);
}

TEST_F(CaptureTest, RestoresFullRelroPermissionsAndOriginalImports)
{
	EXPECT_EQ(protections(), before);
	QString error;
	ASSERT_TRUE(pa::capture::uninstall(&error)) << error.toStdString();
	EXPECT_EQ(protections(), before);
	EXPECT_EQ(consume_x(a), first.dx);
	EXPECT_EQ(drain(), 0);
}

TEST_F(CaptureTest, RejectsMissingImportsWithoutChangingPages)
{
	pa::ImportHooks transaction;
	QString error;
	EXPECT_FALSE(transaction.install(library, {{"missing_pa_symbol", nullptr, nullptr}}, &error));
	EXPECT_FALSE(error.isEmpty());
	EXPECT_FALSE(transaction.installed());
	EXPECT_EQ(protections(), before);
}
