// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include "ServerNetworkFixture.h"
#include "server/ServerCatalogTask.h"
#include "server/ServerChangeTask.h"
#include "server/ServerCreationTask.h"
#include "server/ServerInstance.h"
#include "settings/INISettingsObject.h"

using namespace ServerCatalog;
using namespace ServerTest;
namespace {
QJsonObject asset(const QString& minecraft, const QString& build, const QString& loader, const QString& digest = {})
{
    const auto filename = "arclight-" + loader + "-" + minecraft + "-" + build + ".jar";
    return { { "name", filename },
             { "browser_download_url", "https://github.com/IzzelAliz/Arclight/releases/download/test/" + filename },
             { "size", 32 },
             { "digest", digest } };
}
QJsonObject release(const QJsonArray& assets, bool prerelease = false, bool draft = false)
{
    return { { "prerelease", prerelease }, { "draft", draft }, { "assets", assets } };
}
QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}  // namespace
class ServerHybridCatalogTest : public QObject {
    Q_OBJECT
   private slots:
    void hybridVersionCatalogExcludesEmptyBranchesAndReusesBuilds()
    {
        for (const auto core : { Core::Mohist, Core::Youer }) {
            Network network;
            network.routes.insert(versionsUrl(core).toString(), { "[{\"name\":\"1.21.4\"},{\"name\":\"1.20.1\"}]" });
            network.routes.insert(buildsUrl(core, { "1.21.4", {} }).toString(), { "[]" });
            network.routes.insert(buildsUrl(core, { "1.20.1", {} }).toString(), { "[{\"id\":921}]" });
            ServerCatalogTask versions(&network, core, ServerCatalogTask::Operation::Versions);
            versions.start();
            QTRY_VERIFY(versions.isFinished());
            QVERIFY2(versions.wasSuccessful(), qPrintable(versions.failReason()));
            QCOMPARE(versions.versions().size(), 1);
            QCOMPARE(versions.versions().first().id, QString("1.20.1"));
            ServerCatalogTask builds(&network, core, ServerCatalogTask::Operation::Builds, versions.versions().first());
            builds.start();
            QTRY_VERIFY(builds.isFinished());
            QVERIFY(builds.wasSuccessful());
            QCOMPARE(network.requests.size(), 3);
        }
    }
    void mohistAndYouerVersionsAndExactBuilds()
    {
        for (const auto core : { Core::Mohist, Core::Youer }) {
            QString error;
            QList<ServerCatalog::Version> versions;
            QVERIFY(parseVersions(core, json("[{\"name\":\"1.16.5\"},{\"name\":\"26.2\"},{\"name\":\"26.2\"},{\"name\":\"snapshot\"}]"),
                                  &versions, &error));
            QCOMPARE(versions.size(), 2);
            QCOMPARE(versions.first().id, QString("26.2"));
            const ServerCatalog::Version version{ "1.21.1", {} };
            const QJsonDocument metadata(QJsonArray{ QJsonObject{ { "id", 920 }, { "file_sha256", QString(64, 'a') } },
                                                     QJsonObject{ { "id", 12 }, { "file_sha256", QString(64, 'b') } },
                                                     QJsonObject{ { "id", -1 } } });
            QList<Build> builds;
            QVERIFY(parseBuilds(core, version, metadata, &builds, &error));
            QCOMPARE(builds.size(), 2);
            QCOMPARE(builds.first().id, QString("920"));
            Download download;
            QVERIFY(parseDownload(core, version, metadata, &download, &error, "12"));
            QCOMPARE(download.build, QString("12"));
            QCOMPARE(download.checksum, QString(64, 'b'));
            QVERIFY(download.url.path().endsWith("/1.21.1/builds/12/download"));
            QCOMPARE(download.url.host(), QString("api.mohistmc.com"));
            QVERIFY(!parseDownload(core, version, metadata, &download, &error, "13"));
            QVERIFY(!parseDownload(core, version, json("{\"id\":12,\"file_sha256\":\"bad\"}"), &download, &error, "12"));
            QVERIFY(!parseDownload(core, version, json("{\"id\":12}"), &download, &error, "12"));
            QCOMPARE(metadataUrl(core, version, "12").path(), buildsUrl(core, version).path() + "/12");
        }
    }
    void arclightSeparatesLoadersAndFiltersAssets()
    {
        auto unsafe = asset("1.99.1", "9.0.0", "forge");
        unsafe.insert("browser_download_url", "https://example.test/untrusted.jar");
        const QJsonDocument catalog(QJsonArray{
            release({ asset("1.21.1", "1.0.1-8ec9529", "neoforge", "sha256:" + QString(64, 'a')), asset("1.21.1", "1.0.1-8ec9529", "forge"),
                      asset("1.21.1", "1.0.1-8ec9529", "fabric"), unsafe }),
            release({ asset("1.20.1", "1.0.6", "forge"), asset("1.21.1", "1.0.0", "neoforge") }),
            release({ asset("26.1.2", "1.0.0", "neoforge") }, true), release({ asset("1.99.2", "1.0.0", "forge") }, false, true),
            release({ asset("1.21", "1.0.0-SNAPSHOT", "fabric"), asset("1.21.1", "1.0.1-sources", "forge") }) });
        QString error;
        QList<ServerCatalog::Version> versions;
        QVERIFY(parseVersions(Core::Arclight, catalog, &versions, &error));
        QCOMPARE(versions.size(), 2);
        QCOMPARE(versions.first().id, QString("1.21.1"));
        QList<Build> builds;
        const ServerCatalog::Version version{ "1.21.1", {} };
        QVERIFY(parseBuilds(Core::Arclight, version, catalog, &builds, &error));
        QCOMPARE(builds.size(), 4);
        Download download;
        QVERIFY(parseDownload(Core::Arclight, version, catalog, &download, &error, "1.0.1-8ec9529 (NeoForge)"));
        QCOMPARE(download.loader, QString("NeoForge"));
        QCOMPARE(download.checksum, QString(64, 'a'));
        QVERIFY(parseDownload(Core::Arclight, version, catalog, &download, &error, "1.0.1-8ec9529 (Fabric)"));
        QCOMPARE(download.loader, QString("Fabric"));
        QVERIFY(download.url.fileName().startsWith("arclight-fabric-1.21.1-"));
        QVERIFY(parseDownload(Core::Arclight, { "1.20.1", {} }, catalog, &download, &error, "1.0.6 (Forge)"));
        QVERIFY(download.checksum.isEmpty());
        QCOMPARE(download.size, 32);
        QVERIFY(!parseDownload(Core::Arclight, version, catalog, &download, &error, "1.0.6 (Forge)"));
        QVERIFY(!parseDownload(Core::Arclight, version, catalog, &download, &error, "1.0.2 (NeoForge)"));
        for (const auto& digest : { QString("sha256:"), QString("sha256:abc"), QString("sha512:") + QString(128, 'a') }) {
            const QJsonDocument invalid(QJsonArray{ release({ asset("1.20.1", "1.0.6", "forge", digest) }) });
            QVERIFY(!parseDownload(Core::Arclight, { "1.20.1", {} }, invalid, &download, &error));
        }
    }
    void arclightPaginationCacheRefreshAndCancellation()
    {
        Network network;
        auto first = versionsUrl(Core::Arclight);
        auto second = first.toString();
        second.replace("&page=1", "&page=2");
        QJsonArray page;
        for (int i = 0; i < 100; ++i)
            page.append(release({}, false, true));
        network.routes.insert(first.toString(), { QJsonDocument(page).toJson() });
        const QJsonDocument older(QJsonArray{ release({ asset("1.16.5", "1.0.24", "forge") }) });
        network.routes.insert(second, { older.toJson() });
        ServerCatalogTask versions(&network, Core::Arclight, ServerCatalogTask::Operation::Versions);
        versions.start();
        QTRY_VERIFY(versions.isFinished());
        QVERIFY2(versions.wasSuccessful(), qPrintable(versions.failReason()));
        QCOMPARE(versions.versions().first().id, QString("1.16.5"));
        QCOMPARE(network.requests.size(), 2);
        ServerCatalogTask builds(&network, Core::Arclight, ServerCatalogTask::Operation::Builds, versions.versions().first());
        builds.start();
        QTRY_VERIFY(builds.isFinished());
        QVERIFY(builds.wasSuccessful());
        ServerCatalogTask resolve(&network, Core::Arclight, ServerCatalogTask::Operation::Resolve, versions.versions().first(),
                                  "1.0.24 (Forge)");
        resolve.start();
        QTRY_VERIFY(resolve.isFinished());
        QVERIFY(resolve.wasSuccessful());
        QCOMPARE(network.requests.size(), 2);
        network.routes[first.toString()] = { "[]" };
        ServerCatalogTask refresh(&network, Core::Arclight, ServerCatalogTask::Operation::Versions);
        refresh.start();
        QTRY_VERIFY(refresh.isFinished());
        QVERIFY(!refresh.wasSuccessful());
        QCOMPARE(network.requests.size(), 3);
        network.routes[first.toString()] = { QJsonDocument(page).toJson() };
        network.routes[second] = { older.toJson(), 200, 100 };
        ServerCatalogTask cancelled(&network, Core::Arclight, ServerCatalogTask::Operation::Versions);
        QSignalSpy finished(&cancelled, &Task::finished);
        cancelled.start();
        QTRY_COMPARE(network.requests.size(), 5);
        QVERIFY(cancelled.abort());
        QTest::qWait(130);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(cancelled.getState(), Task::State::AbortedByUser);
    }
    void hybridCreationAndReplacementPreserveServerData()
    {
        QTemporaryDir dir;
        Network network;
        auto settings = global(dir.filePath("global.cfg"));
        const QByteArray jar("PK\003\004hybrid test archive");
        Download download;
        download.core = Core::Mohist;
        download.minecraftVersion = "1.20.1";
        download.build = "921";
        download.url = QUrl("https://api.mohistmc.com/project/mohist/1.20.1/builds/921/download");
        download.checksum = hash(jar, QCryptographicHash::Sha256);
        download.size = jar.size();
        network.routes.insert(download.url.toString(), { jar });
        ServerOptions options;
        options.java = "java";
        options.download = download;
        const auto root = dir.filePath("server-instance");
        ServerCreationTask creation(options, &network);
        creation.setParentSettings(settings.get());
        creation.setName("Hybrid");
        creation.setIcon("default");
        creation.setStagingPath(root);
        creation.start();
        QTRY_VERIFY(creation.isFinished());
        QVERIFY2(creation.wasSuccessful(), qPrintable(creation.failReason()));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(QDir(root).filePath("instance.cfg")), root);
        QCOMPARE(instance.settings()->get("ServerCore").toString(), QString("Mohist"));
        const auto world = QDir(instance.gameRoot()).filePath("world/level.dat");
        QDir().mkpath(QFileInfo(world).absolutePath());
        QFile file(world);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("preserved world");
        file.close();
        for (const auto core : { Core::Youer, Core::Arclight }) {
            download.core = core;
            download.minecraftVersion = "1.21.1";
            download.build = core == Core::Youer ? "920" : "1.0.1 (NeoForge)";
            download.loader = core == Core::Arclight ? "NeoForge" : "";
            download.url = core == Core::Youer
                               ? QUrl("https://api.mohistmc.com/project/youer/1.21.1/builds/920/download")
                               : QUrl("https://github.com/IzzelAliz/Arclight/releases/download/test/arclight-neoforge-1.21.1-1.0.1.jar");
            network.routes.insert(download.url.toString(), { jar });
            ServerChangeTask change(&instance, &network, {}, download);
            change.start();
            QTRY_VERIFY(change.isFinished());
            QVERIFY2(change.wasSuccessful(), qPrintable(change.failReason()));
            QCOMPARE(instance.settings()->get("ServerCore").toString(), name(core));
            QCOMPARE(instance.settings()->get("ServerBuild").toString(), download.build);
            QCOMPARE(instance.settings()->get("ServerLoaderVersion").toString(), download.loader);
            QCOMPARE(read(world), QByteArray("preserved world"));
            QCOMPARE(read(instance.jarPath()), jar);
            QVERIFY(!instance.eulaAccepted());
            QVERIFY(QFile::exists(QDir(change.backupPath()).filePath("instance.cfg")));
        }
    }
    void liveOfficialHybridServices()
    {
        if (!qEnvironmentVariableIsSet("PINECONE_HYBRID_LIVE_TESTS"))
            QSKIP("Explicit opt-in for official hybrid services.");
        QNetworkAccessManager network;
        QTemporaryDir dir;
        auto settings = global(dir.filePath("global.cfg"));
        for (const auto core : { Core::Mohist, Core::Youer, Core::Arclight }) {
            ServerCatalogTask versions(&network, core, ServerCatalogTask::Operation::Versions);
            versions.start();
            QTRY_VERIFY_WITH_TIMEOUT(versions.isFinished(), 90000);
            QVERIFY2(versions.wasSuccessful(), qPrintable(versions.failReason()));
            const QString mc = core == Core::Youer ? "1.21.1" : "1.20.1";
            ServerCatalog::Version selected;
            for (const auto& version : versions.versions())
                if (version.id == mc)
                    selected = version;
            QCOMPARE(selected.id, mc);
            ServerCatalogTask builds(&network, core, ServerCatalogTask::Operation::Builds, selected);
            builds.start();
            QTRY_VERIFY_WITH_TIMEOUT(builds.isFinished(), 60000);
            QVERIFY2(builds.wasSuccessful(), qPrintable(builds.failReason()));
            ServerCatalogTask resolve(&network, core, ServerCatalogTask::Operation::Resolve, selected, builds.builds().first().id);
            resolve.start();
            QTRY_VERIFY_WITH_TIMEOUT(resolve.isFinished(), 60000);
            QVERIFY2(resolve.wasSuccessful(), qPrintable(resolve.failReason()));
            qInfo() << name(core) << resolve.download().minecraftVersion << resolve.download().build << resolve.download().url;
            if (!qEnvironmentVariableIsSet("PINECONE_HYBRID_LIVE_DOWNLOADS"))
                continue;
            ServerOptions options;
            options.java = "java";
            options.download = resolve.download();
            ServerCreationTask creation(options, &network);
            creation.setParentSettings(settings.get());
            creation.setName(name(core));
            creation.setIcon("default");
            const auto root = dir.filePath(name(core));
            creation.setStagingPath(root);
            creation.start();
            QTRY_VERIFY_WITH_TIMEOUT(creation.isFinished(), 300000);
            QVERIFY2(creation.wasSuccessful(), qPrintable(creation.failReason()));
            ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(QDir(root).filePath("instance.cfg")), root);
            QCOMPARE(instance.settings()->get("ServerBuild").toString(), resolve.download().build);
            QVERIFY(QFileInfo(instance.jarPath()).size() > 1000);
            QVERIFY(!instance.eulaAccepted());
        }
    }
};
QTEST_GUILESS_MAIN(ServerHybridCatalogTest)
#include "ServerHybridCatalog_test.moc"
