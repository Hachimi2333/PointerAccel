#include "config/repository.hpp"
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

class ConfigRepositoryTest : public testing::Test
{
protected:
	QTemporaryDir temporary;
	QString path = temporary.path() + "/nested/config.json";
	pa::ConfigRepository repository{path};
};
TEST_F(ConfigRepositoryTest, DistinguishesMissingAndMalformedFilesWithoutOverwritingThem)
{
	EXPECT_EQ(repository.snapshot().revision, "missing");
	EXPECT_TRUE(repository.snapshot().error.isEmpty());
	QString error;
	ASSERT_TRUE(repository.write({}, &error));
	QFile file(path);
	ASSERT_TRUE(file.open(QIODevice::WriteOnly));
	file.write("broken");
	file.close();
	const auto broken = repository.snapshot();
	EXPECT_FALSE(broken.error.isEmpty());
	EXPECT_FALSE(broken.config.enabled);
	pa::ConfigRepository::Transaction transaction(repository);
	EXPECT_FALSE(transaction
	        .save(
	            [](pa::Config&)
	            {
	            })
	        .saved);
	ASSERT_TRUE(file.open(QIODevice::ReadOnly));
	EXPECT_EQ(file.readAll(), "broken");
}
TEST_F(ConfigRepositoryTest, DetectsStaleRevisionBeforeCallingEditor)
{
	const QString missing = repository.snapshot().revision;
	QString error;
	pa::Config config;
	config.trace = true;
	ASSERT_TRUE(repository.write(config, &error));
	const auto before = repository.snapshot();
	bool called = false;
	pa::ConfigRepository::Transaction transaction(repository);
	const auto result = transaction.save(
	    [&](pa::Config&)
	    {
		    called = true;
	    },
	    &missing);
	EXPECT_TRUE(result.conflict);
	EXPECT_FALSE(result.saved);
	EXPECT_FALSE(called);
	EXPECT_EQ(repository.snapshot().revision, before.revision);
}
TEST_F(ConfigRepositoryTest, HoldsWriterLockAfterSaveUntilTransactionEnds)
{
	{
		pa::ConfigRepository::Transaction first(repository);
		ASSERT_TRUE(first
		        .save(
		            [](pa::Config& c)
		            {
			            c.settings.pointerSpeed = 14;
		            })
		        .saved);
		pa::ConfigRepository::Transaction second(repository);
		const auto blocked = second.save(
		    [](pa::Config&)
		    {
		    });
		EXPECT_FALSE(blocked.saved);
		EXPECT_FALSE(blocked.error.isEmpty());
	}
	pa::ConfigRepository::Transaction next(repository);
	ASSERT_TRUE(
	    next.save(
	            [](pa::Config& c)
	            {
		            c.trace = true;
	            })
	        .saved);
	EXPECT_EQ(repository.snapshot().config.settings.pointerSpeed, 14);
	EXPECT_TRUE(repository.snapshot().config.trace);
}
TEST_F(ConfigRepositoryTest, RejectsInvalidEditsAndSymbolicLinks)
{
	pa::ConfigRepository::Transaction transaction(repository);
	EXPECT_FALSE(transaction
	        .save(
	            [](pa::Config& c)
	            {
		            c.settings.pointerSpeed = 0;
	            })
	        .saved);
	EXPECT_EQ(repository.snapshot().revision, "missing");
	const auto target = temporary.path() + "/target";
	QFile file(target);
	ASSERT_TRUE(file.open(QIODevice::WriteOnly));
	file.write("{}");
	file.close();
	ASSERT_TRUE(QFile::link(target, path));
	EXPECT_FALSE(repository.snapshot().error.isEmpty());
	QString error;
	EXPECT_FALSE(repository.write({}, &error));
}
TEST(Configuration, ValidatesTypesRangesAndExclusionEntries)
{
	const auto valid = pa::toJson({});
	for (const auto& value : {QJsonValue(0), QJsonValue(21), QJsonValue(1.5), QJsonValue("10")})
	{
		auto invalid = valid;
		invalid["pointer_speed"] = value;
		QString error;
		pa::parseConfig(invalid, &error);
		EXPECT_FALSE(error.isEmpty());
	}
	for (auto it = valid.begin(); it != valid.end(); ++it)
	{
		auto incomplete = valid;
		incomplete.remove(it.key());
		QString error;
		pa::parseConfig(incomplete, &error);
		EXPECT_FALSE(error.isEmpty()) << it.key().toStdString();
	}
	QString error;
	EXPECT_EQ(pa::parseConfig(valid, &error), pa::Config{});
	EXPECT_TRUE(error.isEmpty());
}
