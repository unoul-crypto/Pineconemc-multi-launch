// SPDX-License-Identifier: GPL-3.0-only
#include "ServerContentInstallTask.h"
#include <QDir>
#include <QtConcurrent>
#include "server/ServerInstance.h"
#include "server/ServerRequest.h"

ServerContentInstallTask::ServerContentInstallTask(ServerInstance* instance,
                                                   QNetworkAccessManager* network,
                                                   ServerContent::Target target,
                                                   QList<ServerContent::AddonVersion> plan)
    : m_instance(instance), m_network(network), m_target(std::move(target)), m_plan(std::move(plan))
{
    connect(instance, &QObject::destroyed, this, [this] {
        if (isRunning())
            abort();
    });
    connect(&m_watcher, &QFutureWatcher<ServerContentStore::Result>::finished, this, [this] {
        m_result = m_watcher.result();
        m_committing = false;
        release();
        if (m_result.success)
            emitSucceeded();
        else
            emitFailed(m_result.error);
    });
}
ServerContentInstallTask::~ServerContentInstallTask()
{
    if (m_request) {
        m_request->disconnect(this);
        m_request->abort();
    }
    if (m_watcher.isRunning())
        m_watcher.waitForFinished();
    release();
}
void ServerContentInstallTask::release()
{
    m_lock.reset();
    if (m_ownsBusyState && m_instance)
        m_instance->setChangingSoftware(false);
    m_ownsBusyState = false;
}
void ServerContentInstallTask::fail(const QString& error)
{
    release();
    emitFailed(error);
}
void ServerContentInstallTask::executeTask()
{
    if (!m_instance || m_instance->isRunning() || m_instance->isChangingSoftware()) {
        fail(tr("Stop the server and wait for other installations to finish first."));
        return;
    }
    if (!m_staging.isValid() || !QDir().mkpath(m_instance->gameRoot())) {
        fail(tr("Could not create the content staging folder."));
        return;
    }
    if (!m_plan.isEmpty() && ServerContent::target(*m_instance) != m_target) {
        fail(tr("The server software or compatibility profile changed. Search again before installing."));
        return;
    }
    m_lock = std::make_unique<QLockFile>(QDir(m_instance->gameRoot()).filePath(".server-launch.lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock()) {
        fail(tr("This server folder is in use by another launcher."));
        return;
    }
    if (!m_instance->settings()->reload() || (!m_plan.isEmpty() && ServerContent::target(*m_instance) != m_target)) {
        fail(tr("The server settings changed in another launcher. Search again before installing."));
        return;
    }
    m_ownsBusyState = true;
    m_instance->setChangingSoftware(true);
    if (ServerContentStore::interrupted(m_instance->instanceRoot()) && !m_plan.isEmpty()) {
        fail(tr("Recover the interrupted content installation before installing another project."));
        return;
    }
    downloadNext();
}
void ServerContentInstallTask::downloadNext()
{
    if (m_staged.size() == m_plan.size()) {
        commit();
        return;
    }
    const auto& version = m_plan[m_staged.size()];
    if (version.target != m_target || !m_target.supports(version.kind) || !ServerContent::safeFilename(version.filename) ||
        !ServerContent::validHash(version.sha512) || version.url.scheme() != "https" || version.url.host() != "cdn.modrinth.com" ||
        !version.url.userInfo().isEmpty() || version.size <= 0 || version.size > 512LL * 1024 * 1024) {
        fail(tr("Invalid content download metadata."));
        return;
    }
    const auto destination = m_staging.filePath(QString::number(m_staged.size()) + ".jar");
    setStatus(tr("Downloading %1 (%2 of %3)…").arg(version.name).arg(m_staged.size() + 1).arg(m_plan.size()));
    auto* request = new ServerRequest(m_network, version.url, destination, version.sha512, QCryptographicHash::Sha512, version.size);
    request->setParent(this);
    m_request = request;
    connect(request, &Task::progress, this, &Task::setProgress);
    connect(request, &Task::finished, this, [this, request, destination] {
        m_request = nullptr;
        request->deleteLater();
        if (!isRunning())
            return;
        if (!request->wasSuccessful()) {
            release();
            if (request->getState() == Task::State::AbortedByUser)
                emitAborted();
            else
                emitFailed(request->failReason());
            return;
        }
        m_staged.append(destination);
        downloadNext();
    });
    request->start();
}
void ServerContentInstallTask::commit()
{
    if (!m_instance || m_instance->isRunning() || (!m_plan.isEmpty() && ServerContent::target(*m_instance) != m_target)) {
        fail(tr("The server changed while content was downloading."));
        return;
    }
    m_committing = true;
    setStatus(tr("Backing up and installing server content…"));
    setProgress(0, 0);
    const auto root = m_instance->instanceRoot();
    // Hashing, backups and transaction recovery run outside the UI thread.
    m_watcher.setFuture(
        QtConcurrent::run([root, plan = m_plan, staged = m_staged] { return ServerContentStore::install(root, plan, staged); }));
}
bool ServerContentInstallTask::abort()
{
    if (isFinished())
        return true;
    if (m_committing)
        return false;
    if (m_request)
        return m_request->abort();
    release();
    emitAborted();
    return true;
}
