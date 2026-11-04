#pragma once
#include "config/config.hpp"
#include <functional>
namespace pa
{
// No QObject, I/O or transport. A refresh cannot replace an unsaved local edit.
class ConfigDraft
{
public:
	const Config& value() const
	{
		return draft;
	}
	const Config& savedValue() const
	{
		return saved;
	}
	const QString& revision() const
	{
		return savedRevision;
	}
	const QString& error() const
	{
		return configError;
	}
	bool dirty() const
	{
		return draft != saved;
	}
	bool valid() const
	{
		return ready && configError.isEmpty();
	}
	bool conflict() const
	{
		return conflicted;
	}
	bool accept(const ConfigSnapshot& snapshot);
	void edit(const std::function<void(Config&)>& change);
	void revert();
	void markConflict(bool conflict)
	{
		conflicted |= conflict;
	}

private:
	Config draft, saved, latest;
	QString savedRevision, latestRevision, configError;
	bool ready = false, conflicted = false;
};
}
