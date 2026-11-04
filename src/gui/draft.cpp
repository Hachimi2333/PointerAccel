#include "draft.hpp"
namespace pa
{
bool ConfigDraft::accept(const ConfigSnapshot& snapshot)
{
	latest = snapshot.config;
	latestRevision = snapshot.revision;
	configError = snapshot.error;
	bool externalConflict = false;
	if (!ready || !dirty())
		revert();
	else if (savedRevision != latestRevision || !configError.isEmpty())
	{
		conflicted = true;
		externalConflict = true;
	}
	ready = true;
	return externalConflict;
}
void ConfigDraft::edit(const std::function<void(Config&)>& change)
{
	if (valid())
		change(draft);
}
void ConfigDraft::revert()
{
	draft = saved = latest;
	savedRevision = latestRevision;
	conflicted = false;
}
}
