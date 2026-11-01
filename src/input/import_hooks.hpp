#pragma once
#include <QString>
#include <vector>

namespace pa
{
class ImportHooks
{
public:
	struct Hook
	{
		const char* name;
		void* replacement;
		void* original;
	};

	bool install(const QString& library, const std::vector<Hook>& hooks, QString* error);
	bool restore(QString* error);

	bool installed() const
	{
		return !entries.empty();
	}

private:
	struct Slot
	{
		void** address;
		void* original;
		void* replacement;
		int protection;
	};

	bool change(bool installing, QString* error);
	std::vector<Slot> entries;
	bool patched = false;
};
}
