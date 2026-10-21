// SPDX-License-Identifier: GPL-3.0-only
#include "ServerCreationTask.h"
#include <QDir>
#include "ServerInstance.h"
#include "settings/INISettingsObject.h"

void ServerCreationTask::executeTask()
{
    setStatus(tr("Creating server instance…"));
    QDir().mkpath(m_stagingPath);
    ServerInstance instance(m_globalSettings, std::make_unique<INISettingsObject>(QDir(m_stagingPath).filePath("instance.cfg")),
                            m_stagingPath);
    QString error;
    if (m_options.minMemory < 128 || m_options.maxMemory < m_options.minMemory) {
        emitFailed(tr("Invalid memory limits."));
        return;
    }
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
