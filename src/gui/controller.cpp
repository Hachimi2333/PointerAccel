#include "controller.hpp"
#include "worker.hpp"
#include "platform/paths.hpp"
#include <QDBusServiceWatcher>
#include <QFileInfo>
#include <QPointer>
#include <QSet>
namespace pa
{
GuiController::GuiController(QObject* parent) : QObject(parent), m_worker(new GuiWorker)
{
	m_worker->moveToThread(&m_thread);
	connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
	m_thread.start();
	m_poll.setInterval(2000);
	m_attributes.setInterval(30000);
	connect(&m_poll,
	    &QTimer::timeout,
	    this,
	    [this]
	    {
		    refresh(false);
	    });
	connect(&m_attributes,
	    &QTimer::timeout,
	    this,
	    [this]
	    {
		    if (m_busy)
			    return;
		    const Page currentPage = m_page;
		    dispatch(
		        [currentPage](GuiWorker* w)
		        {
			        return GuiResult{w->inspect(currentPage, false, true), {}, {}};
		        });
	    });
	auto* kwin = new QDBusServiceWatcher(
	    "org.kde.KWin", QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForOwnerChange, this);
	connect(kwin,
	    &QDBusServiceWatcher::serviceOwnerChanged,
	    this,
	    [this]
	    {
		    refresh(true);
	    });
	// Watching parents keeps QSaveFile's atomic replacement visible to other windows.
	connect(&m_files,
	    &QFileSystemWatcher::fileChanged,
	    this,
	    [this]
	    {
		    refresh(true);
	    });
	connect(&m_files,
	    &QFileSystemWatcher::directoryChanged,
	    this,
	    [this]
	    {
		    if (!m_busy)
		    {
			    const Page currentPage = m_page;
			    dispatch(
			        [currentPage](GuiWorker* w)
			        {
				        return GuiResult{w->inspect(currentPage, false, true), {}, {}};
			        });
		    }
		    else
		    {
			    m_pending = true;
		    }
	    });
	m_poll.start();
	m_attributes.start();
	QTimer::singleShot(0,
	    this,
	    [this]
	    {
		    refresh(true);
	    });
}

GuiController::~GuiController()
{
	m_thread.quit();
	m_thread.wait();
}

QString GuiController::configFile() const
{
	return configPath();
}

void GuiController::dispatch(
    const std::function<GuiResult(GuiWorker*)>& operation, const std::function<void(const GuiResult&)>& complete)
{
	if (m_busy)
		return;
	m_busy = true;
	emit changed();
	QPointer<GuiController> guard(this);
	auto* worker = m_worker;
	QMetaObject::invokeMethod(
	    worker,
	    [worker, guard, operation, complete]
	    {
		    const auto result = operation(worker);
		    if (!guard)
			    return;
		    QMetaObject::invokeMethod(
		        guard,
		        [guard, result, complete]
		        {
			        if (!guard)
				        return;
			        guard->m_busy = false;
			        guard->accept(result.snapshot);
			        if (complete)
				        complete(result);
			        emit guard->changed();
			        if (guard->m_pending)
			        {
				        const bool full = guard->m_fullPending;
				        guard->m_pending = guard->m_fullPending = false;
				        QTimer::singleShot(0,
				            guard,
				            [guard, full]
				            {
					            if (guard)
						            guard->refresh(full);
				            });
			        }
		        },
		        Qt::QueuedConnection);
	    },
	    Qt::QueuedConnection);
}

void GuiController::accept(const GuiSnapshot& snapshot)
{
	m_latest = snapshot.configuration.config;
	m_latestRevision = snapshot.configuration.revision;
	m_configError = snapshot.configuration.error;
	if (!m_ready || !dirty())
	{
		m_saved = m_draft = m_latest;
		m_revision = m_latestRevision;
		m_conflict = false;
	}
	else if (m_revision != m_latestRevision || !m_configError.isEmpty())
	{
		m_conflict = true;
		m_message = "配置已被外部修改。草稿已保留，请撤销修改以重新加载。";
	}
	m_ready = true;
	m_status = snapshot.status;
	if (snapshot.devices)
	{
		m_devices = *snapshot.devices;
		m_devicesError = snapshot.devicesError;
	}
	if (snapshot.diagnostics)
	{
		m_diagnostics = *snapshot.diagnostics;
		if (!snapshot.diagnosticsError.isEmpty())
			m_diagnostics["error"] = snapshot.diagnosticsError;
	}
	QStringList watch;
	for (const auto& path : snapshot.paths)
	{
		if (QFileInfo::exists(path))
			watch << path;
		auto parent = QFileInfo(path).path();
		while (!QFileInfo::exists(parent) && parent != "/")
			parent = QFileInfo(parent).path();
		watch << parent;
	}
	watch.removeDuplicates();
	const auto old = m_files.files() + m_files.directories();
	QStringList remove, add;
	for (const auto& path : old)
		if (!watch.contains(path))
			remove << path;
	for (const auto& path : watch)
		if (!old.contains(path))
			add << path;
	if (!remove.isEmpty())
		m_files.removePaths(remove);
	if (!add.isEmpty())
		m_files.addPaths(add);
}

QList<DeviceInfo> GuiController::devices() const
{
	auto list = m_devices;
	QSet<QString> connected;
	for (auto& device : list)
	{
		connected.insert(device.id);
		device.excluded = m_draft.excluded.contains(device.id);
	}
	auto known = m_saved.excluded + m_draft.excluded;
	known.removeDuplicates();
	for (const auto& id : known)
		if (!connected.contains(id))
			list.append({id, "未连接的设备", true, false, false, m_draft.excluded.contains(id)});
	return list;
}
void GuiController::setPage(Page page)
{
	if (m_page == page)
		return;
	m_page = page;
	emit changed();
	refresh(false);
}
void GuiController::edit(const std::function<void(Config&)>& change)
{
	if (!valid() || m_applying)
		return;
	change(m_draft);
	m_message.clear();
	emit changed();
}
void GuiController::setEnabled(bool enabled)
{
	edit(
	    [enabled](Config& c)
	    {
		    c.enabled = enabled;
	    });
}
void GuiController::setPointerSpeed(int speed)
{
	if (speed < 1 || speed > 20)
		return;
	edit(
	    [speed](Config& c)
	    {
		    c.settings.pointerSpeed = static_cast<std::uint8_t>(speed);
	    });
}
void GuiController::setEpp(bool enhanced)
{
	edit(
	    [enhanced](Config& c)
	    {
		    c.settings.enhancePointerPrecision = enhanced;
	    });
}
void GuiController::setTrace(bool trace)
{
	edit(
	    [trace](Config& c)
	    {
		    c.trace = trace;
	    });
}
void GuiController::setExcluded(const QString& id, bool excluded)
{
	for (const auto& device : devices())
		if (device.id == id && device.eligible)
		{
			edit(
			    [&](Config& c)
			    {
				    c.excluded.removeAll(id);
				    if (excluded)
					    c.excluded.append(id);
			    });
			return;
		}
}
void GuiController::refresh(bool full)
{
	if (m_busy)
	{
		m_pending = true;
		m_fullPending |= full;
		return;
	}
	const Page currentPage = m_page;
	dispatch(
	    [currentPage, full](GuiWorker* w)
	    {
		    return GuiResult{w->inspect(currentPage, full), {}, {}};
	    });
}

void GuiController::revert()
{
	if (m_busy)
		return;
	m_draft = m_saved = m_latest;
	m_revision = m_latestRevision;
	m_conflict = false;
	m_message.clear();
	emit changed();
	refresh(true);
}

void GuiController::apply()
{
	if (!valid() || !dirty() || m_busy || m_conflict)
		return;
	const auto target = m_draft;
	const auto revision = m_revision;
	const auto page = m_page;
	m_applying = true;
	dispatch(
	    [target, revision, page](GuiWorker* w)
	    {
		    return w->apply(target, revision, page);
	    },
	    [this](const GuiResult& result)
	    {
		    m_applying = false;
		    const auto& commit = *result.commit;
		    if (commit.saved)
		    {
			    m_draft = m_saved = m_latest;
			    m_revision = m_latestRevision;
			    m_conflict = false;
			    m_message = commit.applied ? "配置已保存并生效。" : "配置已保存，尚未应用。" + commit.error;
		    }
		    else
		    {
			    m_conflict |= commit.conflict;
			    m_message = "保存失败：" + commit.error;
		    }
		    emit applyFinished(commit.saved, commit.applied);
	    });
}
void GuiController::resetState()
{
	if (m_busy)
		return;
	const auto page = m_page;
	dispatch(
	    [page](GuiWorker* w)
	    {
		    return w->reset(page);
	    },
	    [this](const GuiResult& result)
	    {
		    m_message = result.message;
	    });
}
void GuiController::exportDiagnostics(const QUrl& url)
{
	if (m_busy)
		return;
	if (!url.isLocalFile())
	{
		m_message = "请选择本地文件。";
		emit changed();
		return;
	}
	const auto path = url.toLocalFile();
	if (QFileInfo(path).absoluteFilePath() == QFileInfo(configPath()).absoluteFilePath())
	{
		m_message = "诊断导出不能覆盖程序配置。";
		emit changed();
		return;
	}
	dispatch(
	    [path](GuiWorker* w)
	    {
		    return w->exportDiagnostics(path);
	    },
	    [this](const GuiResult& result)
	    {
		    m_message = result.message;
	    });
}
void GuiController::setPollingActive(bool active)
{
	if (m_polling == active)
		return;
	m_polling = active;
	if (active)
	{
		m_poll.start();
		m_attributes.start();
		refresh(true);
	}
	else
	{
		m_poll.stop();
		m_attributes.stop();
	}
}

}
