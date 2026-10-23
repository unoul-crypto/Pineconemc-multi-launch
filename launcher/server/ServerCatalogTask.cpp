// SPDX-License-Identifier: GPL-3.0-only
#include "ServerCatalogTask.h"
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QUrlQuery>
#include "ServerRequest.h"

namespace {
constexpr auto arclightCache = "_pineconeArclightCatalog";
constexpr auto arclightExpiry = "_pineconeArclightCatalogExpiry";
}  // namespace

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
    if (m_core == ServerCatalog::Core::Arclight) {
        if (!m_network) {
            emitFailed(tr("Network access is unavailable."));
            return;
        }
        // Share the complete release list across selection stages, scoped to this network manager.
        // A new Versions request (including Reload) always refreshes it.
        if (m_operation != Operation::Versions && m_network->property(arclightExpiry).toLongLong() > QDateTime::currentMSecsSinceEpoch()) {
            const auto cached = QJsonDocument::fromJson(m_network->property(arclightCache).toByteArray());
            if (cached.isArray()) {
                parseArclight(cached);
                return;
            }
        }
        arclightPage(1, {});
        return;
    }
    if (m_operation == Operation::Versions) {
        json(ServerCatalog::versionsUrl(m_core), [this](const QJsonDocument& document) {
            QString error;
            if (!ServerCatalog::parseVersions(m_core, document, &m_versions, &error)) {
                emitFailed(error);
                return;
            }
            if (m_core == ServerCatalog::Core::Mohist || m_core == ServerCatalog::Core::Youer) {
                auto pending = m_versions;
                m_versions.clear();
                filterHybridVersions(std::move(pending));
            } else
                emitSucceeded();
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
        else if (m_core == ServerCatalog::Core::Mohist || m_core == ServerCatalog::Core::Youer)
            hybridBuilds(m_version, parse);
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
void ServerCatalogTask::hybridBuilds(const ServerCatalog::Version& version,
                                     std::function<void(const QJsonDocument&)> callback,
                                     bool refresh)
{
    const auto url = ServerCatalog::buildsUrl(m_core, version);
    const auto key = url.toString();
    constexpr auto cacheKey = "_pineconeHybridBuildLists";
    const auto entry = m_network ? m_network->property(cacheKey).toMap().value(key).toMap() : QVariantMap();
    if (!refresh && entry.value("expires").toLongLong() > QDateTime::currentMSecsSinceEpoch()) {
        callback(QJsonDocument::fromJson(entry.value("json").toByteArray()));
        return;
    }
    json(url, [this, key, callback = std::move(callback)](const QJsonDocument& document) {
        if (!document.isArray()) {
            emitFailed(tr("The hybrid server service returned an invalid build list."));
            return;
        }
        auto cache = m_network->property("_pineconeHybridBuildLists").toMap();
        cache.insert(key, QVariantMap{ { "json", document.toJson(QJsonDocument::Compact) },
                                       { "expires", QDateTime::currentMSecsSinceEpoch() + 5 * 60 * 1000 } });
        m_network->setProperty("_pineconeHybridBuildLists", cache);
        callback(document);
    });
}
void ServerCatalogTask::filterHybridVersions(QList<ServerCatalog::Version> pending)
{
    if (pending.isEmpty()) {
        if (m_versions.isEmpty())
            emitFailed(tr("No downloadable builds are available for this server software."));
        else
            emitSucceeded();
        return;
    }
    const auto version = pending.takeFirst();
    hybridBuilds(
        version,
        [this, version, pending = std::move(pending)](const QJsonDocument& document) mutable {
            QList<ServerCatalog::Build> builds;
            QString error;
            if (ServerCatalog::parseBuilds(m_core, version, document, &builds, &error))
                m_versions.append(version);
            filterHybridVersions(std::move(pending));
        },
        true);
}
void ServerCatalogTask::arclightPage(int page, QJsonArray releases)
{
    if (page > 20) {
        emitFailed(tr("The Arclight release list is too large. Please retry later."));
        return;
    }
    auto url = ServerCatalog::versionsUrl(m_core);
    QUrlQuery query(url);
    query.removeAllQueryItems("page");
    query.addQueryItem("page", QString::number(page));
    url.setQuery(query);
    json(url, [this, page, releases = std::move(releases)](const QJsonDocument& document) mutable {
        if (!document.isArray()) {
            emitFailed(tr("GitHub returned an invalid Arclight release list."));
            return;
        }
        for (const auto& release : document.array())
            releases.append(release);
        if (document.array().size() == 100) {
            arclightPage(page + 1, std::move(releases));
            return;
        }
        const QJsonDocument catalog(releases);
        m_network->setProperty(arclightCache, catalog.toJson(QJsonDocument::Compact));
        m_network->setProperty(arclightExpiry, QDateTime::currentMSecsSinceEpoch() + 5 * 60 * 1000);
        parseArclight(catalog);
    });
}
void ServerCatalogTask::parseArclight(const QJsonDocument& document)
{
    QString error;
    const bool success = m_operation == Operation::Versions ? ServerCatalog::parseVersions(m_core, document, &m_versions, &error)
                         : m_operation == Operation::Builds
                             ? ServerCatalog::parseBuilds(m_core, m_version, document, &m_builds, &error)
                             : ServerCatalog::parseDownload(m_core, m_version, document, &m_download, &error, m_build);
    if (success)
        emitSucceeded();
    else
        emitFailed(error);
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
