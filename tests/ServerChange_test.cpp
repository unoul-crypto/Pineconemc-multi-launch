// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>
#include "ServerNetworkFixture.h"
#include "server/ServerCatalogTask.h"
#include "server/ServerChangeTask.h"
#include "server/ServerInstance.h"
#include "server/ServerLaunchTask.h"
using namespace ServerTest;
using namespace ServerCatalog;
namespace {
bool write(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
Download fixtureDownload(const QByteArray& jar)
{
    Download download;
    download.core = Core::Paper;
    download.minecraftVersion = "1.20.1";
    download.build = "195";
    download.url = QUrl("https://example.test/paper.jar");
    download.checksum = hash(jar, QCryptographicHash::Sha256);
    download.size = jar.size();
    return download;
}
}  // namespace
class ServerChangeTest : public QObject {
    Q_OBJECT
   private slots:
    void replacementBacksUpAndPreservesServerData()
    {
        QTemporaryDir dir;
        auto settings = global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        const QByteArray oldJar("PK\003\004old server");
        const QByteArray newJar("PK\003\004new server");
        QVERIFY(write(dir.filePath("old.jar"), oldJar));
        QString error;
        QVERIFY(instance.importJar(dir.filePath("old.jar"), &error));
        instance.settings()->set("ServerJavaPath", "custom-java");
        instance.settings()->set("ServerMaxMem", 4096);
        instance.settings()->set("ServerArgs", "nogui --custom");
        const auto originalConfig = read(dir.filePath("instance.cfg"));
        const auto originalJarPath = instance.jarPath();
        const QStringList paths{ "server.properties", "eula.txt", "world/region/r.0.0.mca", "plugins/test/data.dat", "mods/test.jar" };
        for (const auto& path : paths)
            QVERIFY(write(QDir(instance.gameRoot()).filePath(path), "preserve:" + path.toUtf8()));
        Network network;
        const auto download = fixtureDownload(newJar);
        network.routes.insert(download.url.toString(), { newJar });
        ServerChangeTask change(&instance, &network, {}, download);
        change.start();
        QVERIFY(instance.isChangingSoftware());
        QVERIFY(!instance.canLaunch());
        QVERIFY(!instance.createLaunchTask({}, {}));
        QTRY_VERIFY(change.isFinished());
        QVERIFY2(change.wasSuccessful(), qPrintable(change.failReason()));
        QVERIFY(instance.canLaunch());
        QCOMPARE(read(QDir(change.backupPath()).filePath("instance.cfg")), originalConfig);
        QCOMPARE(read(QDir(change.backupPath()).filePath("previous-server.jar")), oldJar);
        QCOMPARE(read(originalJarPath), oldJar);
        QCOMPARE(read(instance.jarPath()), newJar);
        QVERIFY(instance.jarPath() != originalJarPath);
        QCOMPARE(instance.settings()->get("ServerCore").toString(), QString("Paper"));
        QCOMPARE(instance.settings()->get("ServerBuild").toString(), QString("195"));
        QCOMPARE(instance.settings()->get("ServerMaxMem").toInt(), 4096);
        QCOMPARE(instance.settings()->get("ServerJavaPath").toString(), QString("custom-java"));
        QCOMPARE(instance.settings()->get("ServerArgs").toString(), QString("nogui --custom"));
        for (const auto& path : paths)
            QCOMPARE(read(QDir(instance.gameRoot()).filePath(path)), "preserve:" + path.toUtf8());
        // Saving unrelated settings must keep the active managed JAR and labels.
        QVERIFY(instance.importJar(instance.jarPath(), &error));
        QCOMPARE(instance.settings()->get("ServerCore").toString(), QString("Paper"));
        ServerInstance reopened(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        QCOMPARE(reopened.jarPath(), instance.jarPath());
        QCOMPARE(reopened.settings()->get("ServerBuild").toString(), QString("195"));
        QVERIFY(write(dir.filePath("custom.jar"), oldJar));
        ServerChangeTask custom(&instance, nullptr, dir.filePath("custom.jar"));
        custom.start();
        QVERIFY2(custom.wasSuccessful(), qPrintable(custom.failReason()));
        QCOMPARE(instance.settings()->get("ServerCore").toString(), QString("Custom JAR"));
        QVERIFY(instance.settings()->get("ServerMinecraftVersion").toString().isEmpty());
        QCOMPARE(read(instance.jarPath()), oldJar);
    }
    void cancellationAndDownloadFailureLeaveOriginalSelected()
    {
        QTemporaryDir dir;
        auto settings = global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        const QByteArray jar("PK\003\004server");
        QVERIFY(write(dir.filePath("source.jar"), jar));
        QString error;
        QVERIFY(instance.importJar(dir.filePath("source.jar"), &error));
        const auto original = read(dir.filePath("instance.cfg"));
        const auto originalPath = instance.jarPath();
        Network network;
        const auto download = fixtureDownload(jar);
        network.routes.insert(download.url.toString(), { jar, 200, 100 });
        ServerChangeTask change(&instance, &network, {}, download);
        change.start();
        QVERIFY(instance.isChangingSoftware());
        QLockFile competitor(QDir(instance.gameRoot()).filePath(".server-launch.lock"));
        QVERIFY(!competitor.tryLock());
        ServerChangeTask duplicate(&instance, &network, {}, download);
        duplicate.start();
        QVERIFY(!duplicate.wasSuccessful());
        QVERIFY(instance.isChangingSoftware());
        QVERIFY(change.abort());
        QCOMPARE(change.getState(), Task::State::AbortedByUser);
        QVERIFY(!instance.isChangingSoftware());
        QVERIFY(competitor.tryLock());
        competitor.unlock();
        QTest::qWait(120);
        QCOMPARE(instance.jarPath(), originalPath);
        QCOMPARE(read(dir.filePath("instance.cfg")), original);
        QCOMPARE(read(originalPath), jar);
        network.routes[download.url.toString()] = { "PK\003\004wrong checksum" };
        ServerChangeTask badChecksum(&instance, &network, {}, download);
        badChecksum.start();
        QTRY_VERIFY(badChecksum.isFinished());
        QVERIFY(!badChecksum.wasSuccessful());
        QVERIFY(instance.canLaunch());
        QCOMPARE(read(dir.filePath("instance.cfg")), original);
        QCOMPARE(read(instance.jarPath()), jar);
        QVERIFY(!QDir(dir.filePath("server-backups")).exists());
    }
    void runningAndBackupFailuresDoNotChangeJar()
    {
        QTemporaryDir dir;
        auto settings = global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        const QByteArray jar("PK\003\004server");
        QVERIFY(write(dir.filePath("source.jar"), jar));
        QString error;
        QVERIFY(instance.importJar(dir.filePath("source.jar"), &error));
        auto* running = instance.createLaunchTask({}, {});
        ServerChangeTask blocked(&instance, nullptr, dir.filePath("source.jar"));
        blocked.start();
        QVERIFY(!blocked.wasSuccessful());
        QVERIFY(instance.isRunning());
        QVERIFY(running->abort());
        const auto original = read(dir.filePath("instance.cfg"));
        QVERIFY(write(dir.filePath("server-backups"), "not a directory"));
        ServerChangeTask backupFailure(&instance, nullptr, dir.filePath("source.jar"));
        backupFailure.start();
        QVERIFY(!backupFailure.wasSuccessful());
        QVERIFY(instance.canLaunch());
        QCOMPARE(read(dir.filePath("instance.cfg")), original);
        QCOMPARE(read(instance.jarPath()), jar);
    }
    void livePinnedBuildChange()
    {
        if (!qEnvironmentVariableIsSet("PINECONE_SERVER_CHANGE_LIVE"))
            QSKIP("Explicit opt-in for live pinned software changes.");
        QTemporaryDir dir;
        auto settings = global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        QVERIFY(write(dir.filePath("source.jar"), "PK\003\004initial fixture"));
        QString error;
        QVERIFY(instance.importJar(dir.filePath("source.jar"), &error));
        QNetworkAccessManager network;
        for (const auto core : { Core::Paper, Core::Purpur, Core::Fabric }) {
            const auto pinned = core == Core::Paper ? QString("195") : core == Core::Purpur ? QString("2060") : QString("0.16.14");
            ServerCatalogTask builds(&network, core, ServerCatalogTask::Operation::Builds, { "1.20.1", {} });
            builds.start();
            QTRY_VERIFY_WITH_TIMEOUT(builds.isFinished(), 45000);
            QVERIFY2(builds.wasSuccessful(), qPrintable(builds.failReason()));
            bool found = false;
            for (const auto& build : builds.builds())
                if (build.id == pinned)
                    found = true;
            QVERIFY(found);
            ServerCatalogTask resolve(&network, core, ServerCatalogTask::Operation::Resolve, { "1.20.1", {} }, pinned);
            resolve.start();
            QTRY_VERIFY_WITH_TIMEOUT(resolve.isFinished(), 45000);
            QVERIFY2(resolve.wasSuccessful(), qPrintable(resolve.failReason()));
            ServerChangeTask change(&instance, &network, {}, resolve.download());
            change.start();
            QTRY_VERIFY_WITH_TIMEOUT(change.isFinished(), 180000);
            QVERIFY2(change.wasSuccessful(), qPrintable(change.failReason()));
            QCOMPARE(instance.settings()->get("ServerCore").toString(), name(core));
            QCOMPARE(instance.settings()->get(core == Core::Fabric ? "ServerLoaderVersion" : "ServerBuild").toString(), pinned);
            QVERIFY(QFile::exists(QDir(change.backupPath()).filePath("previous-server.jar")));
            QVERIFY(!QFile::exists(QDir(instance.gameRoot()).filePath("eula.txt")));
        }
    }
};
QTEST_GUILESS_MAIN(ServerChangeTest)
#include "ServerChange_test.moc"
