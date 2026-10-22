// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QPointer>
#include <functional>
#include "ServerCatalog.h"
#include "tasks/Task.h"
class QNetworkAccessManager;
class ServerRequest;

class ServerCatalogTask : public Task {
    Q_OBJECT
   public:
    enum class Operation { Versions, Builds, Resolve };
    ServerCatalogTask(QNetworkAccessManager* network,
                      ServerCatalog::Core core,
                      Operation operation,
                      ServerCatalog::Version version = {},
                      QString build = {});
    bool canAbort() const override { return true; }
    bool abort() override;
    const QList<ServerCatalog::Version>& versions() const { return m_versions; }
    const QList<ServerCatalog::Build>& builds() const { return m_builds; }
    const ServerCatalog::Download& download() const { return m_download; }

   protected:
    void executeTask() override;

   private:
    void json(const QUrl& url, std::function<void(const QJsonDocument&)> callback);
    QNetworkAccessManager* m_network;
    ServerCatalog::Core m_core;
    Operation m_operation;
    ServerCatalog::Version m_version;
    QString m_build;
    QList<ServerCatalog::Build> m_builds;
    QList<ServerCatalog::Version> m_versions;
    ServerCatalog::Download m_download;
    QPointer<ServerRequest> m_request;
};
