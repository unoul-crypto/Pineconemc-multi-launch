// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLockFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>
#include "ServerNetworkFixture.h"
#include "server/ServerChangeTask.h"
#include "server/ServerInstance.h"
#include "server/ServerLaunchTask.h"
#include "server/content/ServerContentInstallTask.h"
#include "server/content/ServerContentTask.h"

using namespace ServerContent;
using ServerTest::Network;
namespace {
const Target fabric{ "1.20.1", "fabric", {} };
const QByteArray jar("PK\003\004server content fixture");
bool write(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray bytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QString digest(const QByteArray& data)
{
    return ServerTest::hash(data, QCryptographicHash::Sha512);
}
QJsonObject metadata(const QString& project, const QString& id, const QString& loader = "fabric", const QJsonArray& dependencies = {})
{
    const auto filename = project + "-" + id + ".jar";
    return { { "id", id },
             { "project_id", project },
             { "name", project },
             { "version_number", id },
             { "version_type", "release" },
             { "status", "listed" },
             { "date_published", "2026-10-01T12:00:00Z" },
             { "game_versions", QJsonArray{ "1.20.1" } },
             { "loaders", QJsonArray{ loader } },
             { "environment", "client_and_server" },
             { "dependencies", dependencies },
             { "files", QJsonArray{ QJsonObject{ { "primary", true },
                                                 { "filename", filename },
                                                 { "size", jar.size() },
                                                 { "hashes", QJsonObject{ { "sha512", digest(jar) } } },
                                                 { "url", "https://cdn.modrinth.com/data/" + project + "/" + filename } } } } };
}
QJsonObject dependency(const QString& project, const QString& version = {}, const QString& type = "required")
{
    return { { "project_id", project }, { "version_id", version }, { "dependency_type", type } };
}
void route(Network& network, const QJsonObject& version)
{
    const auto project = version.value("project_id").toString();
    network.routes.insert(versionUrl(version.value("id").toString()).toString(), { QJsonDocument(version).toJson() });
    const QJsonObject info{ { "id", project }, { "title", project }, { "server_side", "optional" } };
    network.routes.insert(projectUrl(project).toString(), { QJsonDocument(info).toJson() });
    network.routes.insert(versionsUrl(fabric, Kind::Mod, project).toString(), { QJsonDocument(QJsonArray{ version }).toJson() });
}
AddonVersion addon(const QString& project, const QString& id, const QByteArray& data = jar)
{
    AddonVersion result;
    QString error;
    if (!parseVersion(fabric, Kind::Mod, metadata(project, id), &result, &error))
        qFatal("Invalid content test fixture");
    result.sha512 = digest(data);
    result.size = data.size();
    return result;
}
void setup(ServerInstance& instance)
{
    instance.settings()->set("ServerCore", "Fabric");
    instance.settings()->set("ServerMinecraftVersion", "1.20.1");
    QDir().mkpath(instance.gameRoot());
}
}  // namespace
class ServerContentTest : public QObject {
    Q_OBJECT
   private slots:
    void batchSelectionsShareDependenciesAndRespectExplicitPins()
    {
        Network network;
        route(network, metadata("a", "a1", "fabric", { dependency("dep", "d1") }));
        route(network, metadata("b", "b1", "fabric", { dependency("dep", "d1") }));
        route(network, metadata("dep", "d1"));
        route(network, metadata("dep", "d2"));
        ServerContentTask::Query query;
        query.operation = ServerContentTask::Operation::Resolve;
        query.selections = { { "a", "a1" }, { "b", "b1" } };
        ServerContentTask both(&network, fabric, Kind::Mod, query);
        both.start();
        QTRY_VERIFY(both.isFinished());
        QVERIFY2(both.wasSuccessful(), qPrintable(both.failReason()));
        QCOMPARE(both.plan().size(), 3);
        QCOMPARE(std::count_if(both.plan().begin(), both.plan().end(), [](const auto& version) { return version.projectId == "dep"; }), 1);
        query.selections = { { "a", "a1" }, { "dep", "d2" } };
        ServerContentTask conflict(&network, fabric, Kind::Mod, query);
        conflict.start();
        QTRY_VERIFY(conflict.isFinished());
        QVERIFY(!conflict.wasSuccessful());
    }
    void sortAndFilterFacetsKeepServerCompatibility()
    {
        SearchOptions options;
        options.sort = "updated";
        options.categories = { "utility", "optimization" };
        options.openSource = true;
        options.excludedProjects = { "oldProject" };
        options.limit = 100;
        const QUrlQuery query(searchUrl(fabric, Kind::Mod, "hello", 100, options));
        QCOMPARE(query.queryItemValue("index"), QString("updated"));
        QCOMPARE(query.queryItemValue("limit"), QString("100"));
        const auto facets = QJsonDocument::fromJson(query.queryItemValue("facets", QUrl::FullyDecoded).toUtf8()).array();
        QVERIFY(facets.contains(QJsonArray{ "versions:1.20.1" }));
        QVERIFY(facets.contains(QJsonArray{ "categories:fabric" }));
        QVERIFY(facets.contains(QJsonArray{ "categories:utility", "categories:optimization" }));
        QVERIFY(facets.contains(QJsonArray{ "open_source:true" }));
        QVERIFY(facets.contains(QJsonArray{ "project_id!=oldProject" }));
    }
    void installedConstraintsProtectPinsAndReverseIncompatibilities()
    {
        Installed root{ "root", "r1", "Root", "root.jar", QString(128, 'a'), Kind::Mod };
        root.dependencies.append({ "dep", "d1", "required" });
        Installed dep{ "dep", "d1", "Dependency", "dep.jar", QString(128, 'b'), Kind::Mod };
        auto update = dep;
        update.versionId = "d2";
        QString error;
        QVERIFY(validateConstraints({ root, dep }, { root, dep }, &error));
        QVERIFY(!validateConstraints({ root, update }, { root, dep }, &error));
        root.dependencies = { { {}, "d1", "required" } };
        QVERIFY(!validateConstraints({ root, update }, { root, dep }, &error));
        root.dependencies = { { "dep", {}, "incompatible" } };
        QVERIFY(!validateConstraints({ root, dep }, { root }, &error));
    }
    void coreProfilesAndSearchFilters()
    {
        QTemporaryDir dir;
        auto settings = ServerTest::global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        instance.settings()->set("ServerMinecraftVersion", "1.20.1");
        instance.settings()->set("ServerCore", "Mohist");
        auto profile = target(instance);
        QCOMPARE(profile.modLoader, QString("forge"));
        QVERIFY(profile.supports(Kind::Mod));
        QVERIFY(profile.pluginLoaders.contains("spigot"));
        QVERIFY(!profile.pluginLoaders.contains("paper"));
        instance.settings()->set("ServerCore", "Youer");
        profile = target(instance);
        QCOMPARE(profile.modLoader, QString("neoforge"));
        QVERIFY(profile.pluginLoaders.contains("purpur"));
        instance.settings()->set("ServerCore", "Arclight");
        instance.settings()->set("ServerLoaderVersion", "Fabric");
        profile = target(instance);
        QCOMPARE(profile.modLoader, QString("fabric"));
        QVERIFY(!profile.pluginLoaders.contains("paper"));
        instance.settings()->set("ServerCore", "Vanilla");
        QVERIFY(!target(instance).supports(Kind::Mod));
        QVERIFY(!target(instance).supports(Kind::Plugin));
        instance.settings()->set("ServerCore", "Custom JAR");
        instance.settings()->set("ServerContentMinecraftVersion", "1.16.5");
        instance.settings()->set("ServerContentModLoader", "forge");
        instance.settings()->set("ServerContentPluginApi", "spigot");
        profile = target(instance);
        QCOMPARE(profile.minecraft, QString("1.16.5"));
        QVERIFY(profile.supports(Kind::Mod));
        QVERIFY(profile.supports(Kind::Plugin));
        const QUrlQuery query(searchUrl(profile, Kind::Plugin, "a&b", 20));
        QCOMPARE(query.queryItemValue("query"), QString("a&b"));
        const auto facets = QJsonDocument::fromJson(query.queryItemValue("facets", QUrl::FullyDecoded).toUtf8()).array();
        QVERIFY(facets.contains(QJsonArray{ "versions:1.16.5" }));
        QVERIFY(facets.contains(QJsonArray{ "categories:bukkit", "categories:spigot" }));
    }
    void parserRejectsClientOnlyWrongLoaderAndUnsafeFiles()
    {
        QString error;
        AddonVersion version;
        const auto good = metadata("root", "v1");
        QVERIFY(parseVersion(fabric, Kind::Mod, good, &version, &error));
        QCOMPARE(version.target, fabric);
        auto bad = good;
        bad.insert("environment", "client_only");
        QVERIFY(!parseVersion(fabric, Kind::Mod, bad, &version, &error));
        bad = good;
        bad.insert("loaders", QJsonArray{ "forge" });
        QVERIFY(!parseVersion(fabric, Kind::Mod, bad, &version, &error));
        bad = good;
        bad.insert("game_versions", QJsonArray{ "1.21.1" });
        QVERIFY(!parseVersion(fabric, Kind::Mod, bad, &version, &error));
        for (const auto& name : { "../a.jar", "a\\b.jar", "CON.jar", "a:stream.jar", "a.jar ", ".jar" })
            QVERIFY(!safeFilename(name));
        bad = good;
        auto file = good.value("files").toArray().first().toObject();
        file.insert("hashes", QJsonObject());
        bad.insert("files", QJsonArray{ file });
        QVERIFY(!parseVersion(fabric, Kind::Mod, bad, &version, &error));
        bad = good;
        auto other = good.value("files").toArray().first().toObject();
        other.insert("filename", "other.jar");
        bad.insert("files", QJsonArray{ good.value("files").toArray().first(), other });
        QVERIFY(!parseVersion(fabric, Kind::Mod, bad, &version, &error));
        QVERIFY(!serverProject(QJsonObject{ { "server_side", "unsupported" } }));
        QVERIFY(serverProject(QJsonObject{ { "environment", QJsonArray{ "client_and_server" } } }));
    }
    void requiredDependenciesPinnedCyclesAndConflicts()
    {
        Network network;
        route(network, metadata("root", "r1", "fabric", { dependency("dep", "d1"), dependency("ignored", {}, "optional") }));
        route(network, metadata("dep", "d1", "fabric", { dependency("root", "r1") }));
        ServerContentTask::Query query;
        query.operation = ServerContentTask::Operation::Resolve;
        query.projectId = "root";
        query.versionId = "r1";
        ServerContentTask task(&network, fabric, Kind::Mod, query);
        task.start();
        QTRY_VERIFY(task.isFinished());
        QVERIFY2(task.wasSuccessful(), qPrintable(task.failReason()));
        QCOMPARE(task.plan().size(), 2);
        QCOMPARE(task.plan().last().id, QString("d1"));
        QCOMPARE(network.requests.size(), 4);
        route(network, metadata("root", "r1", "fabric", { dependency("dep", "d1"), dependency("dep", "d2") }));
        ServerContentTask conflict(&network, fabric, Kind::Mod, query);
        conflict.start();
        QTRY_VERIFY(conflict.isFinished());
        QVERIFY(!conflict.wasSuccessful());
        route(network, metadata("root", "r1", "fabric", { dependency("dep", "d1") }));
        route(network, metadata("dep", "d1", "forge"));
        ServerContentTask incompatible(&network, fabric, Kind::Mod, query);
        incompatible.start();
        QTRY_VERIFY(incompatible.isFinished());
        QVERIFY(!incompatible.wasSuccessful());
        route(network, metadata("root", "r1", "fabric", { dependency("old", {}, "incompatible") }));
        query.installed = { { "old", "old1", "Old project", "old.jar", QString(128, 'a'), Kind::Mod } };
        ServerContentTask installedConflict(&network, fabric, Kind::Mod, query);
        installedConflict.start();
        QTRY_VERIFY(installedConflict.isFinished());
        QVERIFY(!installedConflict.wasSuccessful());
        route(network, metadata("root", "r1", "fabric", { dependency({}, {}) }));
        ServerContentTask external(&network, fabric, Kind::Mod, query);
        external.start();
        QTRY_VERIFY(external.isFinished());
        QVERIFY(!external.wasSuccessful());
    }
    void projectDependencyKeepsInstalledCompatibleVersion()
    {
        Network network;
        route(network, metadata("root", "r1", "fabric", { dependency("dep") }));
        const auto old = metadata("dep", "d1");
        route(network, old);
        network.routes[versionsUrl(fabric, Kind::Mod, "dep").toString()] = {
            QJsonDocument(QJsonArray{ metadata("dep", "d2"), old }).toJson()
        };
        ServerContentTask::Query query;
        query.operation = ServerContentTask::Operation::Resolve;
        query.projectId = "root";
        query.versionId = "r1";
        query.installed = { { "dep", "d1", "Dependency", "dep-d1.jar", digest(jar), Kind::Mod } };
        ServerContentTask task(&network, fabric, Kind::Mod, query);
        task.start();
        QTRY_VERIFY(task.isFinished());
        QVERIFY2(task.wasSuccessful(), qPrintable(task.failReason()));
        QCOMPARE(task.plan().last().id, QString("d1"));
    }
    void atomicInstallUpdatesBackupsAndPreservesUserData()
    {
        QTemporaryDir dir;
        auto settings = ServerTest::global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        setup(instance);
        const auto config = bytes(dir.filePath("instance.cfg"));
        const auto world = dir.filePath("server/world/level.dat");
        QVERIFY(write(world, "world data"));
        QVERIFY(write(dir.filePath("server/mods/manual.jar"), "manual content"));
        Network network;
        auto root = addon("root", "r1");
        root.dependencies.append({ "dep", "d1", "required" });
        const auto dep = addon("dep", "d1");
        for (const auto& version : { root, dep })
            network.routes.insert(version.url.toString(), { jar });
        ServerContentInstallTask task(&instance, &network, fabric, { root, dep });
        task.start();
        QVERIFY(instance.isChangingSoftware());
        QVERIFY(!instance.canLaunch());
        QLockFile lock(dir.filePath("server/.server-launch.lock"));
        lock.setStaleLockTime(0);
        QVERIFY(!lock.tryLock());
        ServerChangeTask coreChange(&instance, &network, "unused.jar", {});
        coreChange.start();
        QVERIFY(!coreChange.wasSuccessful());
        QTRY_VERIFY_WITH_TIMEOUT(task.isFinished(), 10000);
        QVERIFY2(task.wasSuccessful(), qPrintable(task.failReason()));
        QVERIFY(instance.canLaunch());
        QCOMPARE(bytes(dir.filePath("server/mods/root-r1.jar")), jar);
        QCOMPARE(bytes(dir.filePath("server/mods/dep-d1.jar")), jar);
        const auto originalIndex = bytes(ServerContentStore::manifestPath(dir.path()));
        QList<Installed> indexed;
        QString indexError;
        QVERIFY(ServerContentStore::read(dir.path(), &indexed, &indexError));
        QCOMPARE(indexed.first().dependencies.size(), 1);
        const auto dependencyUpdate = addon("dep", "d2");
        network.routes.insert(dependencyUpdate.url.toString(), { jar });
        ServerContentInstallTask breaksPin(&instance, &network, fabric, { dependencyUpdate });
        breaksPin.start();
        QTRY_VERIFY_WITH_TIMEOUT(breaksPin.isFinished(), 10000);
        QVERIFY(!breaksPin.wasSuccessful());
        QCOMPARE(bytes(ServerContentStore::manifestPath(dir.path())), originalIndex);
        QVERIFY(!QFile::exists(dir.filePath("server/mods/dep-d2.jar")));
        const QByteArray updated("PK\003\004updated version");
        const auto update = addon("root", "r2", updated);
        network.routes.insert(update.url.toString(), { updated });
        ServerContentInstallTask replacement(&instance, &network, fabric, { update });
        replacement.start();
        QTRY_VERIFY_WITH_TIMEOUT(replacement.isFinished(), 10000);
        QVERIFY2(replacement.wasSuccessful(), qPrintable(replacement.failReason()));
        QVERIFY(!QFile::exists(dir.filePath("server/mods/root-r1.jar")));
        QCOMPARE(bytes(dir.filePath("server/mods/root-r2.jar")), updated);
        QCOMPARE(bytes(QDir(replacement.backupPath()).filePath("server-content.json")), originalIndex);
        bool oldJarFound = false;
        for (const auto& file : QDir(replacement.backupPath()).entryList({ "*.jar" }, QDir::Files))
            oldJarFound |= bytes(QDir(replacement.backupPath()).filePath(file)) == jar;
        QVERIFY(oldJarFound);
        QCOMPARE(bytes(world), QByteArray("world data"));
        QCOMPARE(bytes(dir.filePath("server/mods/manual.jar")), QByteArray("manual content"));
        QCOMPARE(bytes(dir.filePath("instance.cfg")), config);
        QVERIFY(!ServerContentStore::interrupted(dir.path()));
    }
    void cancellationBadDependencyHashAndFilenameCollisionLeaveOldFiles()
    {
        QTemporaryDir dir;
        auto settings = ServerTest::global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        setup(instance);
        Network network;
        const auto root = addon("root", "r1");
        const auto dep = addon("dep", "d1");
        network.routes.insert(root.url.toString(), { jar });
        network.routes.insert(dep.url.toString(), { "PK\003\004bad dependency" });
        ServerContentInstallTask badHash(&instance, &network, fabric, { root, dep });
        badHash.start();
        QTRY_VERIFY(badHash.isFinished());
        QVERIFY(!badHash.wasSuccessful());
        QVERIFY(!QFile::exists(dir.filePath("server/mods/root-r1.jar")));
        QVERIFY(!QFile::exists(ServerContentStore::manifestPath(dir.path())));
        network.routes[root.url.toString()] = { jar, 200, 100 };
        ServerContentInstallTask cancelled(&instance, &network, fabric, { root });
        QSignalSpy done(&cancelled, &Task::finished);
        cancelled.start();
        QVERIFY(cancelled.abort());
        QTest::qWait(130);
        QCOMPARE(done.count(), 1);
        QVERIFY(instance.canLaunch());
        QVERIFY(!QFile::exists(dir.filePath("server/mods/root-r1.jar")));
        network.routes[root.url.toString()] = { jar };
        const auto collision = dir.filePath("server/mods/root-r1.jar");
        QVERIFY(write(collision, "user-installed jar"));
        ServerContentInstallTask blocked(&instance, &network, fabric, { root });
        blocked.start();
        QTRY_VERIFY_WITH_TIMEOUT(blocked.isFinished(), 10000);
        QVERIFY(!blocked.wasSuccessful());
        QCOMPARE(bytes(collision), QByteArray("user-installed jar"));
        const auto stale = fabric;
        instance.settings()->set("ServerMinecraftVersion", "1.21.1");
        ServerContentInstallTask changed(&instance, &network, stale, { root });
        changed.start();
        QVERIFY(!changed.wasSuccessful());
    }
    void interruptedInstallRecoversBeforeLaunchAndDoesNotClobberEdits()
    {
        QTemporaryDir dir;
        auto settings = ServerTest::global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        setup(instance);
        const auto root = addon("root", "r1");
        const auto staged = dir.filePath("staged.jar");
        QVERIFY(write(staged, jar));
        const auto result = ServerContentStore::install(dir.path(), { root }, { staged });
        QVERIFY2(result.success, qPrintable(result.error));
        const auto oldIndex = bytes(ServerContentStore::manifestPath(dir.path()));
        const auto backupRelative = QString("server-content-backups/interrupted");
        const auto backup = dir.filePath(backupRelative);
        QVERIFY(write(QDir(backup).filePath("0.jar"), jar));
        const QByteArray changed("PK\003\004interrupted update");
        const auto destination = dir.filePath("server/mods/root-r1.jar");
        QVERIFY(write(destination, changed));
        const QJsonArray operations{ QJsonObject{
            { "kind", "mods" }, { "filename", "root-r1.jar" }, { "before", digest(jar) }, { "after", digest(changed) } } };
        const QJsonObject journal{ { "format", 1 },
                                   { "backup", backupRelative },
                                   { "operations", operations },
                                   { "hadManifest", true },
                                   { "oldManifest", QString::fromLatin1(oldIndex.toBase64()) },
                                   { "newManifest", "" } };
        const auto journalPath = dir.filePath("server-content-transaction.json");
        QVERIFY(write(journalPath, QJsonDocument(journal).toJson()));
        QVERIFY(!instance.canLaunch());
        QVERIFY(instance.validateLaunch().contains("Recover"));
        QVERIFY(write(destination, "user changed it after crash"));
        const auto refused = ServerContentStore::recover(dir.path());
        QVERIFY(!refused.success);
        QCOMPARE(bytes(destination), QByteArray("user changed it after crash"));
        QVERIFY(ServerContentStore::interrupted(dir.path()));
        QVERIFY(write(destination, changed));
        Network network;
        ServerContentInstallTask recovery(&instance, &network, fabric, {});
        recovery.start();
        QTRY_VERIFY_WITH_TIMEOUT(recovery.isFinished(), 10000);
        QVERIFY2(recovery.wasSuccessful(), qPrintable(recovery.failReason()));
        QCOMPARE(bytes(destination), jar);
        QCOMPARE(bytes(ServerContentStore::manifestPath(dir.path())), oldIndex);
        QVERIFY(instance.canLaunch());
        QVERIFY(ServerContentStore::recover(dir.path()).success);
    }
    void liveModrinthSearchResolveAndInstall()
    {
        if (!qEnvironmentVariableIsSet("PINECONE_CONTENT_LIVE_TESTS"))
            QSKIP("Explicit opt-in for live Modrinth requests.");
        QNetworkAccessManager network;
        for (const auto& core : { QString("Fabric"), QString("Paper"), QString("Mohist"), QString("Youer") }) {
            const auto kind = core == "Paper" ? Kind::Plugin : Kind::Mod;
            QTemporaryDir dir;
            auto settings = ServerTest::global(dir.filePath("global.cfg"));
            ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
            setup(instance);
            instance.settings()->set("ServerCore", core);
            if (core == "Mohist")
                instance.settings()->set("ServerMinecraftVersion", "1.16.5");
            if (core == "Youer")
                instance.settings()->set("ServerMinecraftVersion", "1.21.1");
            const auto profile = target(instance);
            const auto slug = core == "Paper"    ? QString("luckperms")
                              : core == "Mohist" ? QString("jei")
                              : core == "Youer"  ? QString("ferrite-core")
                                                 : QString("ledger");
            ServerContentTask::Query query;
            query.text = slug;
            ServerContentTask search(&network, profile, kind, query);
            search.start();
            QTRY_VERIFY_WITH_TIMEOUT(search.isFinished(), 60000);
            QVERIFY2(search.wasSuccessful(), qPrintable(search.failReason()));
            QString id;
            for (const auto& project : search.projects())
                if (project.slug == slug)
                    id = project.id;
            QVERIFY2(!id.isEmpty(), qPrintable(slug));
            query.operation = ServerContentTask::Operation::Versions;
            query.projectId = id;
            ServerContentTask versions(&network, profile, kind, query);
            versions.start();
            QTRY_VERIFY_WITH_TIMEOUT(versions.isFinished(), 60000);
            QVERIFY2(versions.wasSuccessful(), qPrintable(versions.failReason()));
            query.operation = ServerContentTask::Operation::Resolve;
            query.versionId = versions.versions().first().id;
            ServerContentTask resolve(&network, profile, kind, query);
            resolve.start();
            QTRY_VERIFY_WITH_TIMEOUT(resolve.isFinished(), 120000);
            QVERIFY2(resolve.wasSuccessful(), qPrintable(resolve.failReason()));
            if (core == "Fabric")
                QVERIFY(resolve.plan().size() >= 3);
            qInfo() << slug << query.versionId << resolve.plan().size() << "files";
            if (!qEnvironmentVariableIsSet("PINECONE_CONTENT_LIVE_DOWNLOADS"))
                continue;
            ServerContentInstallTask install(&instance, &network, profile, resolve.plan());
            install.start();
            QTRY_VERIFY_WITH_TIMEOUT(install.isFinished(), 180000);
            QVERIFY2(install.wasSuccessful(), qPrintable(install.failReason()));
            QList<Installed> installed;
            QString error;
            QVERIFY(ServerContentStore::read(dir.path(), &installed, &error));
            QCOMPARE(installed.size(), resolve.plan().size());
            for (const auto& version : resolve.plan())
                QCOMPARE(ServerTest::hash(bytes(QDir(instance.gameRoot()).filePath(folder(kind) + "/" + version.filename)),
                                          QCryptographicHash::Sha512),
                         version.sha512);
        }
    }
};
QTEST_GUILESS_MAIN(ServerContentTest)
#include "ServerContent_test.moc"
