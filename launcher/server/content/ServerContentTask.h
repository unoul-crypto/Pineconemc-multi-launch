// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QPointer>
#include <functional>
#include "ServerContent.h"
#include "tasks/Task.h"
class QNetworkAccessManager;
class ServerRequest;

class ServerContentTask : public Task {
    Q_OBJECT
   public:
    enum class Operation { Search, Versions, Resolve };
    struct Query {
        Operation operation = Operation::Search;
        QString text, projectId, versionId;
        int offset = 0;
        bool prereleases = false;
        QList<ServerContent::Installed> installed;
    };
    ServerContentTask(QNetworkAccessManager* network, ServerContent::Target target, ServerContent::Kind kind, Query query);
    bool canAbort() const override { return true; }
    bool abort() override;
    const QList<ServerContent::Project>& projects() const { return m_projects; }
    const QList<ServerContent::AddonVersion>& versions() const { return m_versions; }
    const QList<ServerContent::AddonVersion>& plan() const { return m_plan; }
    int total() const { return m_total; }

   protected:
    void executeTask() override;

   private:
    void json(const QUrl& url, std::function<void(const QJsonDocument&)> callback);
    void nextDependency();
    void addVersion(ServerContent::AddonVersion version);
    void finishPlan();
    QNetworkAccessManager* m_network;
    ServerContent::Target m_target;
    ServerContent::Kind m_kind;
    Query m_query;
    QPointer<ServerRequest> m_request;
    QList<ServerContent::Project> m_projects;
    QList<ServerContent::AddonVersion> m_versions, m_plan;
    QList<ServerContent::Dependency> m_pending;
    int m_total = 0;
    int m_dependencyCount = 0;
};
