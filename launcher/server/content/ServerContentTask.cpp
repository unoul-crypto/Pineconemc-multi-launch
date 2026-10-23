// SPDX-License-Identifier: GPL-3.0-only
#include "ServerContentTask.h"
#include <QJsonArray>
#include <QTimer>
#include <algorithm>
#include "server/ServerRequest.h"

using namespace ServerContent;
ServerContentTask::ServerContentTask(QNetworkAccessManager* network, Target target, Kind kind, Query query)
    : m_network(network), m_target(std::move(target)), m_kind(kind), m_query(std::move(query))
{}
void ServerContentTask::json(const QUrl& url, std::function<void(const QJsonDocument&)> callback)
{
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
            emitFailed(tr("Modrinth returned invalid JSON: %1").arg(error.errorString()));
            return;
        }
        callback(document);
    });
    request->start();
}
void ServerContentTask::executeTask()
{
    if (!m_target.supports(m_kind)) {
        emitFailed(tr("Select a supported server software, Minecraft version and mod loader / plugin API first."));
        return;
    }
    if (m_query.operation == Operation::Search) {
        setStatus(tr("Searching Modrinth…"));
        json(searchUrl(m_target, m_kind, m_query.text, m_query.offset), [this](const auto& document) {
            QString error;
            if (parseSearch(m_target, m_kind, document, &m_projects, &m_total, &error))
                emitSucceeded();
            else
                emitFailed(error);
        });
        return;
    }
    if (!validId(m_query.projectId) || (m_query.operation == Operation::Resolve && !validId(m_query.versionId))) {
        emitFailed(tr("Invalid Modrinth project or version ID."));
        return;
    }
    if (m_query.operation == Operation::Versions) {
        setStatus(tr("Loading compatible versions…"));
        json(projectUrl(m_query.projectId), [this](const auto& project) {
            if (project.object().value("id") != m_query.projectId || !serverProject(project.object())) {
                emitFailed(tr("This project does not declare dedicated server support."));
                return;
            }
            json(versionsUrl(m_target, m_kind, m_query.projectId), [this](const auto& document) {
                if (!document.isArray()) {
                    emitFailed(tr("Modrinth returned an invalid version list."));
                    return;
                }
                for (const auto& version : parseVersions(m_target, m_kind, document, m_query.prereleases))
                    if (version.projectId == m_query.projectId)
                        m_versions.append(version);
                if (m_versions.isEmpty())
                    emitFailed(tr("No compatible server JAR is available. Try another project or enable pre-release versions."));
                else
                    emitSucceeded();
            });
        });
        return;
    }
    setStatus(tr("Resolving required dependencies…"));
    m_pending.append({ m_query.projectId, m_query.versionId, "required" });
    nextDependency();
}
void ServerContentTask::nextDependency()
{
    if (!isRunning())
        return;
    if (++m_dependencyCount > 4096) {
        emitFailed(tr("The dependency graph is too large."));
        return;
    }
    if (m_pending.isEmpty()) {
        finishPlan();
        return;
    }
    const auto dependency = m_pending.takeFirst();
    if ((!dependency.projectId.isEmpty() && !validId(dependency.projectId)) ||
        (!dependency.versionId.isEmpty() && !validId(dependency.versionId)) ||
        (dependency.projectId.isEmpty() && dependency.versionId.isEmpty())) {
        emitFailed(
            tr("A required dependency has no downloadable Modrinth project/version. Check the project's installation instructions."));
        return;
    }
    for (const auto& version : m_plan) {
        if (version.projectId == dependency.projectId || version.id == dependency.versionId) {
            if (!dependency.versionId.isEmpty() && version.id != dependency.versionId) {
                emitFailed(tr("Required dependencies request conflicting versions of %1.").arg(version.name));
                return;
            }
            QTimer::singleShot(0, this, &ServerContentTask::nextDependency);
            return;
        }
    }
    if (m_plan.size() >= 128) {
        emitFailed(tr("The dependency graph exceeds 128 projects."));
        return;
    }
    const bool pinned = !dependency.versionId.isEmpty();
    json(pinned ? versionUrl(dependency.versionId) : versionsUrl(m_target, m_kind, dependency.projectId),
         [this, dependency, pinned](const auto& document) {
             AddonVersion selected;
             QString error;
             if (pinned) {
                 if (!parseVersion(m_target, m_kind, document.object(), &selected, &error) || selected.id != dependency.versionId) {
                     emitFailed(tr("Required version %1 is incompatible or invalid: %2").arg(dependency.versionId, error));
                     return;
                 }
             } else {
                 const auto candidates = parseVersions(m_target, m_kind, document, m_query.prereleases);
                 for (const auto& candidate : candidates) {
                     if (candidate.projectId != dependency.projectId)
                         continue;
                     if (selected.id.isEmpty())
                         selected = candidate;
                     for (const auto& installed : m_query.installed)
                         if (installed.kind == m_kind && installed.projectId == candidate.projectId && installed.versionId == candidate.id)
                             selected = candidate;
                     if (std::any_of(m_query.installed.begin(), m_query.installed.end(), [&candidate](const auto& item) {
                             return item.projectId == candidate.projectId && item.versionId == candidate.id;
                         }))
                         break;
                 }
                 if (selected.id.isEmpty()) {
                     emitFailed(tr("No compatible server version was found for required dependency %1.").arg(dependency.projectId));
                     return;
                 }
             }
             if (!dependency.projectId.isEmpty() && selected.projectId != dependency.projectId) {
                 emitFailed(tr("A dependency version belongs to a different project."));
                 return;
             }
             addVersion(std::move(selected));
         });
}
void ServerContentTask::addVersion(AddonVersion version)
{
    for (const auto& existing : m_plan) {
        if (existing.projectId == version.projectId) {
            if (existing.id == version.id)
                nextDependency();
            else
                emitFailed(tr("Required dependencies request conflicting versions of project %1.").arg(version.projectId));
            return;
        }
    }
    const auto url = projectUrl(version.projectId);
    json(url, [this, version = std::move(version)](const auto& project) mutable {
        if (project.object().value("id") != version.projectId || !serverProject(project.object())) {
            emitFailed(tr("Required project %1 does not declare dedicated server support.").arg(version.projectId));
            return;
        }
        version.name = project.object().value("title").toString(version.name);
        if (version.dependencies.size() + m_pending.size() > 1024) {
            emitFailed(tr("The dependency graph is too large."));
            return;
        }
        for (const auto& dependency : version.dependencies) {
            if (dependency.type == "required")
                m_pending.append(dependency);
            else if (dependency.type != "optional" && dependency.type != "embedded" && dependency.type != "incompatible") {
                emitFailed(tr("The project contains an unsupported dependency declaration."));
                return;
            }
        }
        m_plan.append(version);
        nextDependency();
    });
}
void ServerContentTask::finishPlan()
{
    QList<Installed> effective = m_query.installed;
    for (const auto& version : m_plan) {
        for (const auto& installed : m_query.installed)
            if (installed.projectId == version.projectId && installed.kind != version.kind) {
                emitFailed(
                    tr("%1 is already installed as another content type. Use one variant of a project per server.").arg(version.name));
                return;
            }
        effective.removeIf([&version](const auto& installed) { return installed.projectId == version.projectId; });
        effective.append({ version.projectId, version.id, version.name, version.filename, version.sha512, version.kind, version.number,
                           version.dependencies });
    }
    QString error;
    if (!validateConstraints(effective, m_query.installed, &error)) {
        emitFailed(error);
        return;
    }
    emitSucceeded();
}
bool ServerContentTask::abort()
{
    if (isFinished())
        return true;
    if (m_request)
        return m_request->abort();
    emitAborted();
    return true;
}
