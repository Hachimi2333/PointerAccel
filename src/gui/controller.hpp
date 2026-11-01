#pragma once
#include "state.hpp"
#include <QObject>
#include <QFileSystemWatcher>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <functional>
namespace pa
{
class GuiWorker;
class GuiController : public QObject
{
	Q_OBJECT
public:
	explicit GuiController(QObject* parent = nullptr);
	~GuiController() override;
	const Config& draft() const
	{
		return m_draft;
	}
	const QJsonObject& status() const
	{
		return m_status;
	}
	const QJsonObject& diagnostics() const
	{
		return m_diagnostics;
	}
	QList<DeviceInfo> devices() const;
	QString devicesError() const
	{
		return m_devicesError;
	}
	bool dirty() const
	{
		return m_draft != m_saved;
	}
	bool busy() const
	{
		return m_busy;
	}
	bool applying() const
	{
		return m_applying;
	}
	bool valid() const
	{
		return m_ready && m_configError.isEmpty();
	}
	bool conflict() const
	{
		return m_conflict;
	}
	QString message() const
	{
		return m_message;
	}
	QString configError() const
	{
		return m_configError;
	}
	QString configFile() const;
	Page page() const
	{
		return m_page;
	}
	void setPage(Page page);
	void setEnabled(bool enabled);
	void setPointerSpeed(int speed);
	void setEpp(bool enhanced);
	void setTrace(bool trace);
	void setExcluded(const QString& id, bool excluded);
	void refresh(bool full = true);
	void revert();
	void apply();
	void resetState();
	void exportDiagnostics(const QUrl& url);
	void setPollingActive(bool active);
signals:
	void changed();
	void applyFinished(bool saved, bool applied);

private:
	void edit(const std::function<void(Config&)>& change);
	void dispatch(const std::function<GuiResult(GuiWorker*)>& operation,
	    const std::function<void(const GuiResult&)>& complete = {});
	void accept(const GuiSnapshot& snapshot);
	QThread m_thread;
	GuiWorker* m_worker;
	QTimer m_poll, m_attributes;
	QFileSystemWatcher m_files;
	Config m_draft, m_saved, m_latest;
	QJsonObject m_status, m_diagnostics;
	QList<DeviceInfo> m_devices;
	QString m_devicesError;
	QString m_revision, m_latestRevision, m_configError, m_message;
	Page m_page = Page::Settings;
	bool m_busy = false, m_applying = false, m_ready = false, m_conflict = false;
	bool m_pending = false, m_fullPending = false, m_polling = true;
};
}
