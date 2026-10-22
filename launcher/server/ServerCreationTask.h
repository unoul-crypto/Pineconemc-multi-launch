// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QPointer>
#include "InstanceTask.h"
#include "ServerOptions.h"
class QNetworkAccessManager;
class ServerRequest;

class ServerCreationTask : public InstanceTask {
    Q_OBJECT
   public:
    explicit ServerCreationTask(ServerOptions options, QNetworkAccessManager* network = nullptr)
        : m_options(std::move(options)), m_network(network)
    {}
    void executeTask() override;
    bool canAbort() const override { return true; }
    bool abort() override;

   private:
    ServerOptions m_options;
    QNetworkAccessManager* m_network;
    QPointer<ServerRequest> m_request;
    void createInstance();
};
