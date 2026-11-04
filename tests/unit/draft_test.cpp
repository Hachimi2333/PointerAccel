#include "gui/draft.hpp"
#include <gtest/gtest.h>
TEST(ConfigDraft, KeepsUnsavedEditsWhileRefreshesArrive)
{
	pa::ConfigDraft draft;
	EXPECT_FALSE(draft.valid());
	pa::ConfigSnapshot initial;
	initial.revision = "a";
	EXPECT_FALSE(draft.accept(initial));
	draft.edit(
	    [](pa::Config& c)
	    {
		    c.settings.pointerSpeed = 18;
	    });
	EXPECT_TRUE(draft.dirty());
	EXPECT_FALSE(draft.accept(initial));
	EXPECT_EQ(draft.value().settings.pointerSpeed, 18);
	auto external = initial;
	external.revision = "b";
	external.config.settings.pointerSpeed = 7;
	EXPECT_TRUE(draft.accept(external));
	EXPECT_TRUE(draft.conflict());
	EXPECT_EQ(draft.value().settings.pointerSpeed, 18);
	draft.revert();
	EXPECT_FALSE(draft.conflict());
	EXPECT_FALSE(draft.dirty());
	EXPECT_EQ(draft.value().settings.pointerSpeed, 7);
	EXPECT_EQ(draft.revision(), "b");
}
TEST(ConfigDraft, MakesCorruptConfigurationReadOnlyAndKeepsDraft)
{
	pa::ConfigDraft draft;
	pa::ConfigSnapshot initial;
	initial.revision = "a";
	draft.accept(initial);
	draft.edit(
	    [](pa::Config& c)
	    {
		    c.trace = true;
	    });
	auto broken = initial;
	broken.error = "broken";
	broken.revision = "b";
	EXPECT_TRUE(draft.accept(broken));
	EXPECT_FALSE(draft.valid());
	draft.edit(
	    [](pa::Config& c)
	    {
		    c.trace = false;
	    });
	EXPECT_TRUE(draft.value().trace);
	EXPECT_TRUE(draft.dirty());
}
TEST(ConfigDraft, CleanRefreshAndSuccessfulCommitAdoptLatestRevision)
{
	pa::ConfigDraft draft;
	pa::ConfigSnapshot snapshot;
	snapshot.revision = "first";
	draft.accept(snapshot);
	snapshot.config.enabled = false;
	snapshot.revision = "external";
	EXPECT_FALSE(draft.accept(snapshot));
	EXPECT_FALSE(draft.value().enabled);
	draft.edit(
	    [](pa::Config& c)
	    {
		    c.trace = true;
	    });
	snapshot.config = draft.value();
	snapshot.revision = "saved";
	draft.accept(snapshot);
	draft.revert();
	EXPECT_EQ(draft.revision(), "saved");
	EXPECT_FALSE(draft.dirty());
	EXPECT_TRUE(draft.value().trace);
}
