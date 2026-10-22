// SPDX-License-Identifier: GPL-3.0-only
#include "ServerCreationTask.h"
#include <QDir>
#include "ServerInstance.h"
#include "ServerRequest.h"
#include "settings/INISettingsObject.h"

void ServerCreationTask::executeTask()
{
    setStatus(tr("Creating server instance…"));
    QDir().mkpath(m_stagingPath);
    if (m_options.minMemory < 128 || m_options.maxMemory < m_options.minMemory) {
        emitFailed(tr("Invalid memory limits."));
        return;
    }
    if (!m_options.download) {
        createInstance();
        return;
    }
    const auto& download = *m_options.download;
    if (!ServerCatalog::validDownload(download)) {
        emitFailed(tr("Invalid server download metadata."));
        return;
    }
    const auto serverDir = QDir(m_stagingPath).filePath("server");
    if (!QDir().mkpath(serverDir)) {
        emitFailed(tr("Could not create the server folder."));
        return;
    }
    m_options.jar = QDir(serverDir).filePath("server.jar");
    auto* request = new ServerRequest(m_network, download.url, m_options.jar, download.checksum, download.hashAlgorithm, download.size);
    request->setParent(this);
    m_request = request;
    setStatus(tr("Downloading %1 %2 server…").arg(ServerCatalog::name(download.core), download.minecraftVersion));
    connect(request, &Task::progress, this, &Task::setProgress);
    connect(request, &Task::finished, this, [this, request] {
        m_request = nullptr;
        if (!isRunning())
            return;
        if (request->wasSuccessful())
            createInstance();
        else if (request->getState() == Task::State::AbortedByUser)
            emitAborted();
        else
            emitFailed(request->failReason());
    });
    request->start();
}
bool ServerCreationTask::abort()
{
    if (isFinished())
        return true;
    if (m_request)
        return m_request->abort();
    emitAborted();
    return true;
}
void ServerCreationTask::createInstance()
{
    ServerInstance instance(m_globalSettings, std::make_unique<INISettingsObject>(QDir(m_stagingPath).filePath("instance.cfg")),
                            m_stagingPath);
    QString error;
    if (!instance.importJar(m_options.jar, &error) || !instance.setEulaAccepted(m_options.acceptEula, &error)) {
        emitFailed(error);
        return;
    }
    auto* settings = instance.settings();
    {
        SettingsObject::Lock lock(settings);
        instance.setName(name());
        instance.setIconKey(m_instIcon);
        settings->set("ServerJavaPath", m_options.java);
        settings->set("ServerMinMem", m_options.minMemory);
        settings->set("ServerMaxMem", m_options.maxMemory);
        settings->set("ServerArgs", m_options.arguments);
        if (m_options.download) {
            const auto& download = *m_options.download;
            settings->set("ServerCore", ServerCatalog::name(download.core));
            settings->set("ServerMinecraftVersion", download.minecraftVersion);
            settings->set("ServerBuild", download.build);
            settings->set("ServerLoaderVersion", download.loader);
            settings->set("ServerInstallerVersion", download.installer);
        }
        settings->set("OverrideConsole", true);
        settings->set("ShowConsole", true);
        settings->set("ShowConsoleOnError", true);
        settings->set("AutoCloseConsole", false);
        settings->set("OverrideGameTime", true);
        settings->set("ShowGameTime", false);
        settings->set("RecordGameTime", false);
    }
    emitSucceeded();
}
