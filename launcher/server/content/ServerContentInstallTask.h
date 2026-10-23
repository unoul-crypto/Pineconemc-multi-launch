// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QFutureWatcher>
#include <QLockFile>
#include <QPointer>
#include <QTemporaryDir>
#include "ServerContentStore.h"
#include "tasks/Task.h"
class QNetworkAccessManager;
class ServerInstance;
class ServerRequest;

class ServerContentInstallTask : public Task {
    Q_OBJECT
   public:
    // An empty plan recovers an interrupted installation without installing new content.
    ServerContentInstallTask(ServerInstance* instance,
                             QNetworkAccessManager* network,
                             ServerContent::Target target,
                             QList<ServerContent::AddonVersion> plan);
    ~ServerContentInstallTask() override;
    bool canAbort() const override { return !m_committing; }
    bool abort() override;
    QString backupPath() const { return m_result.backupPath; }

   protected:
    void executeTask() override;

   private:
    void downloadNext();
    void commit();
    void release();
    void fail(const QString& error);
    QPointer<ServerInstance> m_instance;
    QNetworkAccessManager* m_network;
    ServerContent::Target m_target;
    QList<ServerContent::AddonVersion> m_plan;
    QStringList m_staged;
    QTemporaryDir m_staging;
    std::unique_ptr<QLockFile> m_lock;
    QPointer<ServerRequest> m_request;
    QFutureWatcher<ServerContentStore::Result> m_watcher;
    ServerContentStore::Result m_result;
    bool m_ownsBusyState = false;
    bool m_committing = false;
};
