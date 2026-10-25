// SPDX-License-Identifier: GPL-3.0-only
#include "ServerInstance.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include "Commandline.h"
#include "PropertiesFile.h"
#include "ServerLaunchTask.h"
#include "content/ServerContentStore.h"
#include "settings/Setting.h"

ServerInstance::ServerInstance(SettingsObject* global, std::unique_ptr<SettingsObject> settings, const QString& root)
    : BaseInstance(global, std::move(settings), root)
{
    m_settings->set("InstanceType", "Server");
    m_settings->registerSetting("ServerJarPath", "server.jar");
    const auto globalJava = global->getSetting("JavaPath");
    m_settings->registerSetting("ServerJavaPath",
                                globalJava && !globalJava->get().toString().isEmpty() ? globalJava->get() : QVariant("java"));
    m_settings->registerSetting("ServerMinMem", 1024);
    m_settings->registerSetting("ServerMaxMem", 2048);
    m_settings->registerSetting("ServerArgs", "nogui");
    m_settings->registerSetting("ServerCore", "Custom JAR");
    for (const auto& key : { "ServerMinecraftVersion", "ServerBuild", "ServerLoaderVersion", "ServerInstallerVersion" })
        m_settings->registerSetting(key, QString());
    for (const auto& key : { "ServerContentMinecraftVersion", "ServerContentModLoader", "ServerContentPluginApi" })
        m_settings->registerSetting(key, QString());
    m_settings->registerSetting("JvmArgs", "");
}
QString ServerInstance::gameRoot() const
{
    return QDir(instanceRoot()).filePath("server");
}
QString ServerInstance::modsRoot() const
{
    return QDir(gameRoot()).filePath("mods");
}
QString ServerInstance::jarPath() const
{
    return QDir(gameRoot()).absoluteFilePath(m_settings->get("ServerJarPath").toString());
}
QStringList ServerInstance::getLogFileSearchPaths()
{
    return { QDir(gameRoot()).filePath("logs"), QDir(gameRoot()).filePath("crash-reports") };
}
QString ServerInstance::javaPath() const
{
    auto path = m_settings->get("ServerJavaPath").toString().trimmed();
#ifdef Q_OS_WIN
    if (QFileInfo(path).fileName().compare("javaw.exe", Qt::CaseInsensitive) == 0) {
        auto consoleJava = QFileInfo(path).dir().filePath("java.exe");
        if (QFileInfo::exists(consoleJava))
            path = consoleJava;
    }
#endif
    return path;
}
QStringList ServerInstance::launchArguments() const
{
    QStringList args{ QString("-Xms%1m").arg(m_settings->get("ServerMinMem").toInt()),
                      QString("-Xmx%1m").arg(m_settings->get("ServerMaxMem").toInt()), "-Dfile.encoding=UTF-8", "-jar", jarPath() };
    args.append(Commandline::splitArgs(m_settings->get("ServerArgs").toString()));
    return args;
}
QString ServerInstance::validateLaunch() const
{
    if (ServerContentStore::interrupted(instanceRoot()))
        return tr("Recover the interrupted installation on the Mods or Plugins page before starting this server.");
    if (m_changingSoftware)
        return tr("Wait for the server software installation to finish.");
    const auto min = m_settings->get("ServerMinMem").toInt();
    const auto max = m_settings->get("ServerMaxMem").toInt();
    if (min < 128 || max < min)
        return tr("Memory must be at least 128 MiB and maximum memory must not be below minimum memory.");
    const auto java = javaPath();
    if (java.isEmpty() || (!QFileInfo(java).isFile() && QStandardPaths::findExecutable(java).isEmpty()))
        return tr("Java was not found. Select a Java executable in Server settings.");
    if (!QFileInfo(jarPath()).isFile() || !QFileInfo(jarPath()).isReadable())
        return tr("The server JAR was not found or cannot be read. Select a runnable server JAR in Server settings.");
    if (!eulaAccepted())
        return tr("Review the Minecraft EULA and accept it in Server settings before starting the server.");
    return {};
}
bool ServerInstance::canLaunch() const
{
    return !isRunning() && !m_changingSoftware && !ServerContentStore::interrupted(instanceRoot());
}
bool ServerInstance::importJar(const QString& source, QString* error)
{
    if (isRunning() || m_changingSoftware) {
        if (error)
            *error = tr("Stop the server before replacing its JAR.");
        return false;
    }
    QFile input(source);
    if (QFileInfo(source).suffix().compare("jar", Qt::CaseInsensitive) != 0 || !input.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Select a readable .jar file.");
        return false;
    }
    if (!input.peek(4).startsWith("PK")) {
        if (error)
            *error = tr("The selected file is not a JAR archive.");
        return false;
    }
    if (QFileInfo(source).canonicalFilePath() == QFileInfo(jarPath()).canonicalFilePath())
        return true;
    QDir().mkpath(gameRoot());
    const auto destination = QDir(gameRoot()).filePath("server.jar");
    if (QFileInfo(source).canonicalFilePath() != QFileInfo(destination).canonicalFilePath()) {
        QSaveFile output(destination);
        if (!output.open(QIODevice::WriteOnly)) {
            if (error)
                *error = output.errorString();
            return false;
        }
        while (!input.atEnd()) {
            const auto data = input.read(1024 * 1024);
            if (data.isEmpty() && input.error() != QFile::NoError) {
                if (error)
                    *error = input.errorString();
                return false;
            }
            if (output.write(data) != data.size()) {
                if (error)
                    *error = output.errorString();
                return false;
            }
        }
        if (!output.commit()) {
            if (error)
                *error = output.errorString();
            return false;
        }
        m_settings->set("ServerCore", "Custom JAR");
        for (const auto& key : { "ServerMinecraftVersion", "ServerBuild", "ServerLoaderVersion", "ServerInstallerVersion" })
            m_settings->set(key, QString());
        for (const auto& key : { "ServerContentMinecraftVersion", "ServerContentModLoader", "ServerContentPluginApi" })
            m_settings->set(key, QString());
    }
    m_settings->set("ServerJarPath", "server.jar");
    return true;
}
bool ServerInstance::eulaAccepted() const
{
    PropertiesFile properties;
    return PropertiesFile::read(QDir(gameRoot()).filePath("eula.txt"), &properties) &&
           properties.value("eula").compare("true", Qt::CaseInsensitive) == 0;
}
bool ServerInstance::setEulaAccepted(bool accepted, QString* error)
{
    if (accepted == eulaAccepted())
        return true;
    if (isRunning()) {
        if (error)
            *error = tr("Stop the server before changing its settings.");
        return false;
    }
    return PropertiesFile::update(QDir(gameRoot()).filePath("eula.txt"), { { "eula", accepted ? "true" : "false" } }, error);
}
LaunchTask* ServerInstance::createLaunchTask(AuthSessionPtr, MinecraftTarget::Ptr)
{
    if (!canLaunch())
        return nullptr;
    return adoptLaunchTask(std::make_unique<ServerLaunchTask>(this));
}
void ServerInstance::setChangingSoftware(bool changing)
{
    if (m_changingSoftware == changing)
        return;
    m_changingSoftware = changing;
    emit softwareChangeChanged(changing);
    emit propertiesChanged(this);
}
QString ServerInstance::getStatusbarDescription()
{
    return tr("Server · %1 %2")
        .arg(m_settings->get("ServerCore").toString(), m_settings->get("ServerMinecraftVersion").toString())
        .trimmed();
}
QStringList ServerInstance::verboseDescription(AuthSessionPtr, MinecraftTarget::Ptr)
{
    return { typeName(), jarPath(), javaPath() };
}
