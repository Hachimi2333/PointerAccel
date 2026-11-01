#pragma once
#include "state.hpp"
#include <QObject>
#include <memory>
namespace pa
{
// All methods, the private bus connection and the client belong to the worker thread.
class GuiWorker : public QObject
{
public:
	~GuiWorker() override;
	GuiSnapshot inspect(Page page, bool force = false, bool checkAttributes = false, bool all = false);
	GuiResult apply(const Config& config, const QString& revision, Page page);
	GuiResult reset(Page page);
	GuiResult exportDiagnostics(const QString& path);

private:
	ControlClient& control();
	QString connectionName;
	std::unique_ptr<ControlClient> client;
	QStringList paths;
	QByteArray lastAttributes;
	QJsonObject startup;
};
}
