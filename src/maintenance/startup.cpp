#include "startup.hpp"
#include "startup_facts.hpp"
#include "context.hpp"
namespace pa
{
QJsonObject sessionStamp()
{
	using namespace maintenance;
	return {{"session", qEnvironmentVariable("XDG_SESSION_ID")},
	    {"boot", readText("/proc/sys/kernel/random/boot_id")},
	    {"owner", owner()},
	    {"bus", busId()}};
}
QJsonObject startupStatus()
{
	return evaluateStartup(collectStartupFacts()).toJson();
}
}
