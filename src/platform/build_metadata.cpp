#include "build_metadata.hpp"
#include "build_info.hpp"
#include "runtime_identity.hpp"
#include "input/report.hpp"
namespace pa
{
QJsonObject buildInfo()
{
	return {{"version", PA_VERSION},
	    {"kwin_version", PA_KWIN_VERSION},
	    {"image_build_id", imageBuildId(reinterpret_cast<const void*>(&buildInfo))},
	    {"kwin_library", PA_KWIN_LIBRARY},
	    {"kwin_sha256", PA_KWIN_SHA256},
	    {"kwin_executable", PA_KWIN_EXECUTABLE},
	    {"executable_sha256", PA_EXECUTABLE_SHA256},
	    {"kwin_build_id", PA_KWIN_BUILD_ID},
	    {"executable_build_id", PA_EXECUTABLE_BUILD_ID},
	    {"kwin_iid", "org.kde.kwin.PluginFactoryInterface" PA_KWIN_VERSION},
	    {"engine", "windows-pointer-linux 1.0.0 (BSD-3-Clause)"},
	    {"capture_abi", 1},
	    {"report_capacity", static_cast<int>(PA_REPORT_CAPACITY)}};
}

}
