#include "plugin_control.hpp"
#include "platform/runtime_identity.hpp"
#include <kwin/plugin.h>
#include <QDebug>
namespace
{
class PointerAccelPlugin final : public KWin::Plugin
{
public:
	PointerAccelPlugin() : control(runtime)
	{
	}

private:
	pa::PointerRuntime runtime;
	pa::PluginControl control;
};
}
class PointerAccelFactory final : public KWin::PluginFactory
{
	Q_OBJECT
	Q_PLUGIN_METADATA(IID PluginFactory_iid FILE "kwin_plugin.json")
	Q_INTERFACES(KWin::PluginFactory)
public:
	std::unique_ptr<KWin::Plugin> create() const override
	{
		QString error;
		auto expected = pa::buildInfo();
#ifdef PA_TEST_RUNTIME_MISMATCH
		expected["kwin_build_id"] = "0000000000000000000000000000000000000000";
#endif
		if (!pa::runtimeCompatible(expected, &error))
		{
			qWarning().noquote() << "PointerAccel:" << error;
			return {};
		}
		return std::make_unique<PointerAccelPlugin>();
	}
};

#include "plugin.moc"
