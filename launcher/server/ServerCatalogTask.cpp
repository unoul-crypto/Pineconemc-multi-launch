// SPDX-License-Identifier: GPL-3.0-only
#include "ServerCatalogTask.h"
#include "ServerRequest.h"

ServerCatalogTask::ServerCatalogTask(QNetworkAccessManager* network,
                                     ServerCatalog::Core core,
                                     Operation operation,
                                     ServerCatalog::Version version,
                                     QString build)
    : m_network(network), m_core(core), m_operation(operation), m_version(std::move(version)), m_build(std::move(build))
{}
void ServerCatalogTask::json(const QUrl& url, std::function<void(const QJsonDocument&)> callback)
{
    if (url.scheme() != "https" || url.host().isEmpty()) {
        emitFailed(tr("Invalid server metadata URL."));
        return;
    }
    auto* request = new ServerRequest(m_network, url);
    request->setParent(this);
    m_request = request;
    connect(request, &Task::finished, this, [this, request, callback = std::move(callback)] {
        m_request = nullptr;
        request->deleteLater();
        if (!isRunning())
            return;
        if (!request->wasSuccessful()) {
            if (request->getState() == Task::State::AbortedByUser)
                emitAborted();
            else
                emitFailed(request->failReason());
            return;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(request->response(), &error);
        if (error.error != QJsonParseError::NoError || document.isNull()) {
            emitFailed(tr("The server service returned invalid metadata: %1").arg(error.errorString()));
            return;
        }
        callback(document);
    });
    request->start();
}
void ServerCatalogTask::executeTask()
{
    setStatus(tr("Loading %1 server metadata…").arg(ServerCatalog::name(m_core)));
    if (m_core == ServerCatalog::Core::Custom) {
        emitFailed(tr("Custom servers use a local JAR."));
        return;
    }
    if (m_operation == Operation::Versions) {
        json(ServerCatalog::versionsUrl(m_core), [this](const QJsonDocument& document) {
            QString error;
            if (ServerCatalog::parseVersions(m_core, document, &m_versions, &error))
                emitSucceeded();
            else
                emitFailed(error);
        });
        return;
    }
    if (m_operation == Operation::Builds) {
        auto parse = [this](const QJsonDocument& document) {
            QString error;
            if (ServerCatalog::parseBuilds(m_core, m_version, document, &m_builds, &error))
                emitSucceeded();
            else
                emitFailed(error);
        };
        if (m_core == ServerCatalog::Core::Vanilla)
            parse({});
        else
            json(ServerCatalog::buildsUrl(m_core, m_version), parse);
        return;
    }
    json(ServerCatalog::metadataUrl(m_core, m_version, m_build), [this](const QJsonDocument& document) {
        if (m_core != ServerCatalog::Core::Fabric) {
            QString error;
            if (ServerCatalog::parseDownload(m_core, m_version, document, &m_download, &error, m_build))
                emitSucceeded();
            else
                emitFailed(error);
            return;
        }
        m_download.core = m_core;
        m_download.minecraftVersion = m_version.id;
        if (m_build.isEmpty())
            m_download.loader = ServerCatalog::stableFabricVersion(document, true);
        else {
            QList<ServerCatalog::Build> builds;
            QString error;
            if (ServerCatalog::parseBuilds(m_core, m_version, document, &builds, &error))
                for (const auto& build : builds)
                    if (build.id == m_build)
                        m_download.loader = m_build;
        }
        if (m_download.loader.isEmpty()) {
            emitFailed(tr("No stable Fabric loader is available for this version."));
            return;
        }
        json(QUrl("https://meta.fabricmc.net/v2/versions/installer"), [this](const QJsonDocument& installers) {
            m_download.installer = ServerCatalog::stableFabricVersion(installers, false);
            auto encode = [](const QString& value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); };
            m_download.url = QUrl("https://meta.fabricmc.net/v2/versions/loader/" + encode(m_version.id) + "/" + encode(m_download.loader) +
                                  "/" + encode(m_download.installer) + "/server/jar");
            if (ServerCatalog::validDownload(m_download))
                emitSucceeded();
            else
                emitFailed(tr("No supported Fabric server launcher is available."));
        });
    });
}
bool ServerCatalogTask::abort()
{
    if (isFinished())
        return true;
    if (m_request)
        return m_request->abort();
    emitAborted();
    return true;
}
