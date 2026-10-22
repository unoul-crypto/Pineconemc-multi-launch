// SPDX-License-Identifier: GPL-3.0-only
#include "ServerChangeTask.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QUuid>
#include "ServerInstance.h"
#include "ServerRequest.h"
#include "settings/INIFile.h"

namespace {
bool copyAtomic(const QString& source, const QString& destination, QString* error)
{
    QFile input(source);
    QSaveFile output(destination);
    if (!input.open(QIODevice::ReadOnly)) {
        *error = input.errorString();
        return false;
    }
    if (!output.open(QIODevice::WriteOnly)) {
        *error = output.errorString();
        return false;
    }
    while (!input.atEnd()) {
        const auto data = input.read(1024 * 1024);
        if ((data.isEmpty() && input.error() != QFile::NoError) || output.write(data) != data.size()) {
            *error = QObject::tr("Could not copy the server file.");
            return false;
        }
    }
    if (!output.commit()) {
        *error = output.errorString();
        return false;
    }
    return true;
}
}  // namespace
ServerChangeTask::ServerChangeTask(ServerInstance* instance,
                                   QNetworkAccessManager* network,
                                   QString jar,
                                   std::optional<ServerCatalog::Download> download)
    : m_instance(instance), m_network(network), m_jar(std::move(jar)), m_download(std::move(download))
{
    connect(instance, &QObject::destroyed, this, [this] {
        if (isRunning())
            abort();
    });
}
ServerChangeTask::~ServerChangeTask()
{
    if (m_request) {
        m_request->disconnect(this);
        m_request->abort();
    }
    release();
}
void ServerChangeTask::release()
{
    m_lock.reset();
    if (m_ownsBusyState && m_instance)
        m_instance->setChangingSoftware(false);
    m_ownsBusyState = false;
}
void ServerChangeTask::fail(const QString& error)
{
    release();
    emitFailed(error);
}
void ServerChangeTask::executeTask()
{
    if (!m_instance || m_instance->isRunning() || m_instance->isChangingSoftware()) {
        fail(tr("Stop the server and wait for other software changes to finish first."));
        return;
    }
    if (!m_staging.isValid() || !QDir().mkpath(m_instance->gameRoot())) {
        fail(tr("Could not create the temporary server folder."));
        return;
    }
    m_lock = std::make_unique<QLockFile>(QDir(m_instance->gameRoot()).filePath(".server-launch.lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock()) {
        fail(tr("This server folder is in use by another launcher or cannot be locked."));
        return;
    }
    m_ownsBusyState = true;
    m_instance->setChangingSoftware(true);
    if (!m_download) {
        install();
        return;
    }
    if (!ServerCatalog::validDownload(*m_download)) {
        fail(tr("Invalid server download metadata."));
        return;
    }
    m_jar = m_staging.filePath("selected-server.jar");
    auto* request = new ServerRequest(m_network, m_download->url, m_jar, m_download->checksum, m_download->hashAlgorithm, m_download->size);
    request->setParent(this);
    m_request = request;
    setStatus(tr("Downloading the selected server software…"));
    connect(request, &Task::progress, this, &Task::setProgress);
    connect(request, &Task::finished, this, [this, request] {
        m_request = nullptr;
        if (!isRunning())
            return;
        if (request->wasSuccessful())
            install();
        else if (request->getState() == Task::State::AbortedByUser) {
            release();
            emitAborted();
        } else
            fail(request->failReason());
    });
    request->start();
}
void ServerChangeTask::install()
{
    if (!m_instance || m_instance->isRunning() || !m_ownsBusyState) {
        fail(tr("The server is no longer available for changes."));
        return;
    }
    QFile input(m_jar);
    if (!input.open(QIODevice::ReadOnly) || !input.peek(4).startsWith("PK")) {
        fail(tr("Select a readable server JAR archive."));
        return;
    }
    input.close();
    const auto root = m_instance->instanceRoot();
    const auto configPath = QDir(root).filePath("instance.cfg");
    INIFile config;
    if (!config.loadFile(configPath)) {
        fail(tr("Could not read the current instance settings."));
        return;
    }
    const auto stamp =
        QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + "-" + QUuid::createUuid().toString(QUuid::Id128).left(8);
    m_backupPath = QDir(root).filePath("server-backups/" + stamp);
    if (!QDir().mkpath(m_backupPath) || !QFile::copy(configPath, QDir(m_backupPath).filePath("instance.cfg"))) {
        fail(tr("Could not back up the instance settings. The server was not changed."));
        return;
    }
    const auto oldJar = m_instance->jarPath();
    if (QFile::exists(oldJar) && !QFile::copy(oldJar, QDir(m_backupPath).filePath("previous-server.jar"))) {
        fail(tr("Could not back up the old server JAR. The server was not changed."));
        return;
    }
    // New JAR first, then atomically switch the configuration pointer. The old
    // JAR remains in place, so interruption cannot leave an active partial JAR.
    const auto fileName = "launcher-server-" + QUuid::createUuid().toString(QUuid::Id128) + ".jar";
    const auto destination = QDir(m_instance->gameRoot()).filePath(fileName);
    QString error;
    setStatus(tr("Backing up and installing server software…"));
    if (!copyAtomic(m_jar, destination, &error)) {
        fail(error);
        return;
    }
    config.set("ServerJarPath", fileName);
    config.set("ServerCore", m_download ? ServerCatalog::name(m_download->core) : QString("Custom JAR"));
    config.set("ServerMinecraftVersion", m_download ? m_download->minecraftVersion : QString());
    config.set("ServerBuild", m_download ? m_download->build : QString());
    config.set("ServerLoaderVersion", m_download ? m_download->loader : QString());
    config.set("ServerInstallerVersion", m_download ? m_download->installer : QString());
    if (!config.saveFile(configPath)) {
        fail(tr("Could not save the new software settings. The old server is still selected."));
        return;
    }
    if (!m_instance->settings()->reload()) {
        QString restoreError;
        const bool restored = copyAtomic(QDir(m_backupPath).filePath("instance.cfg"), configPath, &restoreError);
        m_instance->settings()->reload();
        fail(restored ? tr("Could not reload software settings. Previous settings were restored.")
                      : tr("Could not reload or restore settings. Restore instance.cfg from %1 before starting.").arg(m_backupPath));
        return;
    }
    release();
    emitSucceeded();
}
bool ServerChangeTask::abort()
{
    if (isFinished())
        return true;
    if (m_request)
        return m_request->abort();
    release();
    emitAborted();
    return true;
}
