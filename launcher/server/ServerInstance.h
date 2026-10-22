// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "BaseInstance.h"

class ServerInstance : public BaseInstance {
    Q_OBJECT
   public:
    ServerInstance(SettingsObject* global, std::unique_ptr<SettingsObject> settings, const QString& root);
    bool isServer() const override { return true; }
    void saveNow() override {}
    void loadSpecificSettings() override { setSpecificSettingsLoaded(true); }
    QString gameRoot() const override;
    QString modsRoot() const override;
    QSet<QString> traits() const override { return {}; }
    QList<Task::Ptr> createUpdateTask() override { return {}; }
    LaunchTask* createLaunchTask(AuthSessionPtr, MinecraftTarget::Ptr) override;
    QProcessEnvironment createEnvironment() override { return QProcessEnvironment::systemEnvironment(); }
    QProcessEnvironment createLaunchEnvironment() override { return createEnvironment(); }
    QStringList getLogFileSearchPaths() override;
    QString getStatusbarDescription() override;
    QString instanceConfigFolder() const override { return gameRoot(); }
    QMap<QString, QString> getVariables() override { return {}; }
    QString typeName() const override { return tr("Minecraft server"); }
    bool canEdit() const override { return true; }
    bool canExport() const override { return false; }
    bool canLaunch() const override { return !isRunning() && !m_changingSoftware; }
    bool isChangingSoftware() const { return m_changingSoftware; }
    void populateLaunchMenu(QMenu*) override {}
    QStringList verboseDescription(AuthSessionPtr, MinecraftTarget::Ptr) override;
    QString jarPath() const;
    QString javaPath() const;
    QStringList launchArguments() const;
    QString validateLaunch() const;
    bool importJar(const QString& source, QString* error);
    bool eulaAccepted() const;
    bool setEulaAccepted(bool accepted, QString* error);
   signals:
    void softwareChangeChanged(bool changing);

   private:
    friend class ServerChangeTask;
    void setChangingSoftware(bool changing);
    bool m_changingSoftware = false;
};
