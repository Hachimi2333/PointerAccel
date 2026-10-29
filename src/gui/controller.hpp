#pragma once
#include <QObject>
#include <QFileSystemWatcher>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <functional>

namespace pa {
class GuiWorker;
class GuiController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap draft READ draft NOTIFY changed)
    Q_PROPERTY(QVariantMap status READ status NOTIFY changed)
    Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool applying READ applying NOTIFY changed)
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(bool conflict READ conflict NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString configError READ configError NOTIFY changed)
    Q_PROPERTY(QString configFile READ configFile CONSTANT)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY changed)
public:
    explicit GuiController(QObject *parent = nullptr);
    ~GuiController() override;
    QVariantMap draft() const { return m_draft; }
    QVariantMap status() const { return m_status; }
    QVariantMap diagnostics() const { return m_diagnostics; }
    QVariantList devices() const;
    bool dirty() const { return m_draft != m_saved; }
    bool busy() const { return m_busy; }
    bool applying() const { return m_applying; }
    bool valid() const { return m_ready && m_configError.isEmpty(); }
    bool conflict() const { return m_conflict; }
    QString message() const { return m_message; }
    QString configError() const { return m_configError; }
    QString configFile() const;
    int page() const { return m_page; }
    void setPage(int page);
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void setExcluded(const QString &id, bool excluded);
    Q_INVOKABLE void refresh(bool full = true);
    Q_INVOKABLE void revert();
    Q_INVOKABLE void apply();
    Q_INVOKABLE void resetState();
    Q_INVOKABLE void exportDiagnostics(const QUrl &url);
    Q_INVOKABLE void setPollingActive(bool active);
    Q_INVOKABLE void copyText(const QString &text);
signals:
    void changed();
    void applyFinished(bool saved, bool applied);
private:
    void dispatch(const std::function<QVariantMap(GuiWorker *)> &operation,
                  const std::function<void(const QVariantMap &)> &complete = {});
    void accept(const QVariantMap &snapshot);
    QThread m_thread;
    GuiWorker *m_worker;
    QTimer m_poll, m_attributes;
    QFileSystemWatcher m_files;
    QVariantMap m_draft, m_saved, m_latest, m_status, m_diagnostics;
    QVariantList m_devices;
    QString m_revision, m_latestRevision, m_configError, m_message;
    int m_page = 0;
    bool m_busy = false, m_applying = false, m_ready = false, m_conflict = false;
    bool m_pending = false, m_fullPending = false, m_polling = true;
};
}
