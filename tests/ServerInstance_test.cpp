// SPDX-License-Identifier: GPL-3.0-only
#include "server/ServerInstance.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include "server/PropertiesFile.h"
#include "server/ServerCreationTask.h"
#include "server/ServerLaunchTask.h"
#include "settings/INISettingsObject.h"

class ServerInstanceTest : public QObject {
    Q_OBJECT
   private:
    static std::unique_ptr<INISettingsObject> global(const QString& path)
    {
        auto settings = std::make_unique<INISettingsObject>(path);
        for (const auto& key :
             { "ShowGameTime", "RecordGameTime", "ShowConsole", "AutoCloseConsole", "ShowConsoleOnError", "LogPrePostOutput" })
            settings->registerSetting(key, false);
        for (const auto& key : { "PreLaunchCommand", "WrapperCommand", "PostExitCommand", "JavaPath" })
            settings->registerSetting(key, QString());
        settings->registerSetting("ConsoleMaxLines", 1000);
        settings->registerSetting("ConsoleOverflowStop", false);
        return settings;
    }
    static QString jar(const QString& root)
    {
        const auto path = QDir(root).filePath("source server.jar");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        file.write("PK\003\004fixture");
        return path;
    }
    static void prepare(ServerInstance& server, const QString& jarPath)
    {
        QString error;
        QVERIFY2(server.importJar(jarPath, &error), qPrintable(error));
        QVERIFY2(server.setEulaAccepted(true, &error), qPrintable(error));
        server.settings()->set("ServerJavaPath", QCoreApplication::applicationFilePath());
        server.settings()->set("ServerArgs", "--server-fixture");
    }
   private slots:
    void creationCopiesJarAndDoesNotAcceptEulaAutomatically()
    {
        QTemporaryDir dir;
        auto settings = global(dir.filePath("global.cfg"));
        const auto source = jar(dir.path());
        ServerOptions options{ source, "java", 512, 1536, "nogui", false };
        ServerCreationTask creation(options);
        creation.setParentSettings(settings.get());
        creation.setStagingPath(dir.filePath("new server"));
        creation.setName("My server");
        creation.setIcon("default");
        creation.start();
        QVERIFY2(creation.wasSuccessful(), qPrintable(creation.failReason()));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("new server/instance.cfg")),
                                dir.filePath("new server"));
        QVERIFY(instance.isServer());
        QCOMPARE(instance.instanceType(), QString("Server"));
        QCOMPARE(instance.name(), QString("My server"));
        QCOMPARE(instance.settings()->get("ServerMaxMem").toInt(), 1536);
        QVERIFY(QFile::exists(instance.jarPath()));
        QVERIFY(!instance.eulaAccepted());
        QVERIFY(instance.launchArguments().contains("-Xms512m"));
        QVERIFY(instance.launchArguments().contains("-Xmx1536m"));
        QVERIFY(!QFile::exists(dir.filePath("new server/mmc-pack.json")));
    }
    void processCommandsStopLogsAndSingleInstance()
    {
        QTemporaryDir dir;
        auto settings = global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        prepare(instance, jar(dir.path()));
        auto* task = instance.createLaunchTask({}, {});
        QVERIFY(task);
        QVERIFY(!instance.canLaunch());
        QVERIFY(!instance.createLaunchTask({}, {}));
        QSignalSpy completed(task, &Task::finished);
        task->start();
        QTRY_VERIFY_WITH_TIMEOUT(task->acceptsCommands(), 10000);
        QTRY_VERIFY(task->getLogModel()->toPlainText().contains("fixture ready"));
        QVERIFY(task->sendCommand("say Привет"));
        QTRY_VERIFY(task->getLogModel()->toPlainText().contains(QString::fromUtf8("echo:say Привет")));
        QVERIFY(!task->sendCommand("list\nstop"));
        QVERIFY(!task->sendCommand("list\rstop"));
        QVERIFY(task->sendCommand("/stop"));
        QVERIFY(!task->acceptsCommands());
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 10000);
        QVERIFY(task->wasSuccessful());
        QVERIFY(!instance.isRunning());
        QVERIFY(instance.canLaunch());
        QVERIFY(task->getLogModel()->toPlainText().contains("saved cleanly"));
        QVERIFY(task->getLogModel()->toPlainText().contains("final fragment"));
        const auto logs = QDir(QDir(instance.gameRoot()).filePath("logs")).entryList({ "launcher-console-*.log" }, QDir::Files);
        QCOMPARE(logs.size(), 1);
        QFile log(QDir(instance.gameRoot()).filePath("logs/" + logs.first()));
        QVERIFY(log.open(QIODevice::ReadOnly));
        QVERIFY(log.readAll().contains("saved cleanly"));
    }
    void lockPreventsOtherLauncherUsingSameFolder()
    {
        QTemporaryDir dir;
        auto settings = global(dir.filePath("global.cfg"));
        ServerInstance first(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        prepare(first, jar(dir.path()));
        auto* task = first.createLaunchTask({}, {});
        task->start();
        QTRY_VERIFY_WITH_TIMEOUT(task->acceptsCommands(), 10000);
        ServerInstance second(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        auto* blocked = second.createLaunchTask({}, {});
        blocked->start();
        QVERIFY(blocked->isFinished());
        QVERIFY(!blocked->wasSuccessful());
        QVERIFY(!second.isRunning());
        QVERIFY(task->sendCommand("stop"));
        QTRY_VERIFY_WITH_TIMEOUT(task->isFinished(), 10000);
        auto* next = second.createLaunchTask({}, {});
        next->start();
        QTRY_VERIFY_WITH_TIMEOUT(next->acceptsCommands(), 10000);
        QVERIFY(next->sendCommand("stop"));
        QTRY_VERIFY_WITH_TIMEOUT(next->isFinished(), 10000);
        QVERIFY(next->wasSuccessful());
    }
    void failedStartAndForceKillLeaveInstanceUsable()
    {
        QTemporaryDir dir;
        auto settings = global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        prepare(instance, jar(dir.path()));
        instance.settings()->set("ServerJavaPath", dir.filePath("missing-java"));
        auto* failed = instance.createLaunchTask({}, {});
        failed->start();
        QVERIFY(failed->isFinished());
        QVERIFY(!failed->wasSuccessful());
        QVERIFY(instance.canLaunch());
        instance.settings()->set("ServerJavaPath", QCoreApplication::applicationFilePath());
        auto* running = instance.createLaunchTask({}, {});
        running->start();
        QTRY_VERIFY_WITH_TIMEOUT(running->acceptsCommands(), 10000);
        QVERIFY(running->abort());
        QTRY_VERIFY_WITH_TIMEOUT(running->isFinished(), 10000);
        QVERIFY(instance.canLaunch());
        QVERIFY(!instance.hasCrashed());
    }
    void rejectsBadMemoryAndMissingEula()
    {
        QTemporaryDir dir;
        auto settings = global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        QString error;
        QVERIFY(instance.importJar(jar(dir.path()), &error));
        instance.settings()->set("ServerJavaPath", QCoreApplication::applicationFilePath());
        QVERIFY(instance.validateLaunch().contains("EULA"));
        instance.settings()->set("ServerMaxMem", 256);
        instance.settings()->set("ServerMinMem", 512);
        QVERIFY(!instance.validateLaunch().isEmpty());
    }
};

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == "--server-fixture") {
            std::cout << "fixture ready" << std::endl;
            std::string line;
            while (std::getline(std::cin, line)) {
                if (line == "stop") {
                    std::cout << "saved cleanly\nfinal fragment" << std::flush;
                    return 0;
                }
                std::cout << "echo:" << line << std::endl;
            }
            return 2;
        }
    }
    QCoreApplication app(argc, argv);
    ServerInstanceTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "ServerInstance_test.moc"
