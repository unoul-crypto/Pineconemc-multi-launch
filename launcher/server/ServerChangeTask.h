// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QLockFile>
#include <QPointer>
#include <QTemporaryDir>
#include <memory>
#include <optional>
#include "ServerCatalog.h"
#include "tasks/Task.h"
class QNetworkAccessManager;
class ServerInstance;
class ServerRequest;

class ServerChangeTask : public Task {
    Q_OBJECT
   public:
    ServerChangeTask(ServerInstance* instance,
                     QNetworkAccessManager* network,
                     QString jar,
                     std::optional<ServerCatalog::Download> download = {});
    ~ServerChangeTask() override;
    bool canAbort() const override { return true; }
    bool abort() override;
    QString backupPath() const { return m_backupPath; }

   protected:
    void executeTask() override;

   private:
    void install();
    void release();
    void fail(const QString& error);
    QPointer<ServerInstance> m_instance;
    QNetworkAccessManager* m_network;
    QString m_jar;
    std::optional<ServerCatalog::Download> m_download;
    QPointer<ServerRequest> m_request;
    QTemporaryDir m_staging;
    std::unique_ptr<QLockFile> m_lock;
    QString m_backupPath;
    bool m_ownsBusyState = false;
};
