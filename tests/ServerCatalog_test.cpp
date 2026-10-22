// SPDX-License-Identifier: GPL-3.0-only
#include "server/ServerCatalog.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstring>
#include "server/ServerCatalogTask.h"
#include "server/ServerCreationTask.h"
#include "server/ServerInstance.h"
#include "server/ServerRequest.h"
#include "settings/INISettingsObject.h"

using namespace ServerCatalog;
namespace {
struct Response {
    QByteArray body;
    int status = 200;
    int delay = 0;
};
class Reply : public QNetworkReply {
   public:
    Reply(const QNetworkRequest& request, Response response, QObject* parent) : QNetworkReply(parent), m_body(response.body)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(response.delay, this, [this, response] {
            if (isFinished())
                return;
            if (response.status != 200)
                setError(QNetworkReply::ContentNotFoundError, "fixture HTTP error");
            setFinished(true);
            emit readyRead();
            emit downloadProgress(m_body.size(), m_body.size());
            emit finished();
        });
    }
    void abort() override
    {
        if (isFinished())
            return;
        setFinished(true);
        setError(QNetworkReply::OperationCanceledError, "cancelled");
        QTimer::singleShot(0, this, [this] { emit finished(); });
    }
    qint64 bytesAvailable() const override { return m_body.size() - m_offset + QNetworkReply::bytesAvailable(); }

   protected:
    qint64 readData(char* data, qint64 max) override
    {
        const auto size = qMin(max, static_cast<qint64>(m_body.size() - m_offset));
        if (!size)
            return -1;
        std::memcpy(data, m_body.constData() + m_offset, static_cast<size_t>(size));
        m_offset += size;
        return size;
    }

   private:
    QByteArray m_body;
    qint64 m_offset = 0;
};
class Network : public QNetworkAccessManager {
   public:
    QHash<QString, Response> routes;
    QList<QNetworkRequest> requests;

   protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
    {
        requests.append(request);
        return new Reply(request, routes.value(request.url().toString(), { "not found", 404, 0 }), this);
    }
};
QJsonDocument json(const QByteArray& bytes)
{
    return QJsonDocument::fromJson(bytes);
}
QString hash(const QByteArray& data, QCryptographicHash::Algorithm algorithm)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, algorithm).toHex());
}
std::unique_ptr<INISettingsObject> global(const QString& path)
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
}  // namespace
class ServerCatalogTest : public QObject {
    Q_OBJECT
   private slots:
    void catalogsFilterAndSortReleases()
    {
        QList<Version> versions;
        QString error;
        QVERIFY(parseVersions(
            Core::Paper, json("{\"versions\":{\"1.20\":[\"1.20.1\",\"1.20.6\",\"1.20.6-pre1\"],\"26.3\":[\"26.3\"],\"1.9\":[\"1.9\"]}}"),
            &versions, &error));
        QCOMPARE(versions.first().id, QString("26.3"));
        QCOMPARE(versions[1].id, QString("1.20.6"));
        QCOMPARE(versions.size(), 4);
        QVERIFY(parseVersions(Core::Purpur, json("{\"versions\":[\"1.9\",\"1.20.1\",\"1.20.1\"]}"), &versions, &error));
        QCOMPARE(versions.size(), 2);
        QCOMPARE(versions.first().id, QString("1.20.1"));
        QVERIFY(parseVersions(Core::Fabric, json("[{\"version\":\"1.20.1\",\"stable\":true},{\"version\":\"1.21-pre1\",\"stable\":false}]"),
                              &versions, &error));
        QCOMPARE(versions.size(), 1);
        QVERIFY(
            parseVersions(Core::Vanilla,
                          json("{\"versions\":[{\"id\":\"1.20.1\",\"type\":\"release\",\"url\":\"https://piston-meta.mojang.com/"
                               "v.json\"},{\"id\":\"1.21\",\"type\":\"release\",\"url\":\"http://invalid/"
                               "v.json\"},{\"id\":\"23w01a\",\"type\":\"snapshot\",\"url\":\"https://piston-meta.mojang.com/s.json\"}]}"),
                          &versions, &error));
        QCOMPARE(versions.size(), 1);
        QVERIFY(!parseVersions(Core::Paper, json("{\"error\":\"service unavailable\"}"), &versions, &error));
    }
    void pinnedBuildsAndRequiredChecksums()
    {
        Download download;
        QString error;
        const auto sha = QString(64, 'a');
        auto paper = QJsonDocument(QJsonArray{
            QJsonObject{ { "id", 999 }, { "channel", "EXPERIMENTAL" } },
            QJsonObject{
                { "id", 7 },
                { "channel", "STABLE" },
                { "downloads", QJsonObject{ { "server:default", QJsonObject{ { "url", "https://fill-data.papermc.io/server.jar" },
                                                                             { "size", 123 },
                                                                             { "checksums", QJsonObject{ { "sha256", sha } } } } } } } },
            QJsonObject{ { "id", 6 }, { "channel", "STABLE" } } });
        QVERIFY(parseDownload(Core::Paper, { "1.20.1", {} }, paper, &download, &error));
        QCOMPARE(download.build, QString("7"));
        QCOMPARE(download.checksum, sha);
        QCOMPARE(download.size, 123);
        QVERIFY(!parseDownload(Core::Paper, { "1.20.1", {} }, json("[{\"id\":1,\"channel\":\"EXPERIMENTAL\"}]"), &download, &error));
        QVERIFY(!parseDownload(Core::Vanilla, { "1.20.1", {} }, json("{\"id\":\"1.20.1\",\"downloads\":{}}"), &download, &error));
        const auto purpur = QJsonDocument(
            QJsonObject{ { "version", "1.20.1" }, { "result", "SUCCESS" }, { "build", "2062" }, { "md5", QString(32, 'a') } });
        QVERIFY(parseDownload(Core::Purpur, { "1.20.1", {} }, purpur, &download, &error));
        QVERIFY(download.url.toString().endsWith("/1.20.1/2062/download"));
        QVERIFY(!parseDownload(Core::Purpur, { "1.19.4", {} }, purpur, &download, &error));
    }
    void fabricUsesCompatibleStableLoaderAndPinnedInstaller()
    {
        Network network;
        const auto loaderUrl = metadataUrl(Core::Fabric, { "1.20.1", {} }).toString();
        network.routes.insert(
            loaderUrl,
            { "[{\"loader\":{\"version\":\"0.99.0\",\"stable\":false}},{\"loader\":{\"version\":\"0.16.14\",\"stable\":true}}]" });
        network.routes.insert("https://meta.fabricmc.net/v2/versions/installer",
                              { "[{\"version\":\"2.0\",\"stable\":false},{\"version\":\"1.0.3\",\"stable\":true}]" });
        ServerCatalogTask task(&network, Core::Fabric, ServerCatalogTask::Operation::Resolve, { "1.20.1", {} });
        task.start();
        QTRY_VERIFY(task.isFinished());
        QVERIFY2(task.wasSuccessful(), qPrintable(task.failReason()));
        QCOMPARE(task.download().loader, QString("0.16.14"));
        QCOMPARE(task.download().installer, QString("1.0.3"));
        QCOMPARE(task.download().url.toString(), QString("https://meta.fabricmc.net/v2/versions/loader/1.20.1/0.16.14/1.0.3/server/jar"));
        for (const auto& request : network.requests)
            QCOMPARE(request.rawHeader("User-Agent"), userAgent());
    }
    void atomicDownloadRejectsBadHashSizeHtmlAndHttpFailure()
    {
        QTemporaryDir dir;
        Network network;
        const QByteArray jar("PK\003\004test archive");
        const QUrl url("https://example.test/server.jar");
        const auto destination = dir.filePath("server.jar");
        network.routes.insert(url.toString(), { jar });
        ServerRequest valid(&network, url, destination, hash(jar, QCryptographicHash::Sha256), QCryptographicHash::Sha256, jar.size());
        valid.start();
        QTRY_VERIFY(valid.isFinished());
        QVERIFY2(valid.wasSuccessful(), qPrintable(valid.failReason()));
        auto original = [&] {
            QFile file(destination);
            if (!file.open(QIODevice::ReadOnly))
                return QByteArray();
            return file.readAll();
        };
        QCOMPARE(original(), jar);
        ServerRequest wrongHash(&network, url, destination, QString(64, '0'));
        wrongHash.start();
        QTRY_VERIFY(wrongHash.isFinished());
        QVERIFY(!wrongHash.wasSuccessful());
        QCOMPARE(original(), jar);
        ServerRequest wrongSize(&network, url, destination, {}, QCryptographicHash::Sha256, jar.size() + 1);
        wrongSize.start();
        QTRY_VERIFY(wrongSize.isFinished());
        QVERIFY(!wrongSize.wasSuccessful());
        QCOMPARE(original(), jar);
        network.routes[url.toString()] = { "<html>error</html>" };
        ServerRequest html(&network, url, destination);
        html.start();
        QTRY_VERIFY(html.isFinished());
        QVERIFY(!html.wasSuccessful());
        QCOMPARE(original(), jar);
        network.routes[url.toString()] = { jar, 404 };
        ServerRequest httpError(&network, url, destination);
        httpError.start();
        QTRY_VERIFY(httpError.isFinished());
        QVERIFY(!httpError.wasSuccessful());
        QCOMPARE(original(), jar);
    }
    void creationPersistsSoftwareAndNeverAcceptsEulaAutomatically()
    {
        QTemporaryDir dir;
        Network network;
        const QByteArray jar("PK\003\004download fixture");
        Download download;
        download.core = Core::Paper;
        download.minecraftVersion = "1.20.1";
        download.build = "196";
        download.url = QUrl("https://example.test/server.jar");
        download.checksum = hash(jar, QCryptographicHash::Sha256);
        download.size = jar.size();
        network.routes.insert(download.url.toString(), { jar });
        auto settings = global(dir.filePath("global.cfg"));
        ServerOptions options;
        options.java = "java";
        options.download = download;
        ServerCreationTask task(options, &network);
        task.setParentSettings(settings.get());
        task.setName("Paper test");
        task.setStagingPath(dir.filePath("stage"));
        task.setIcon("default");
        task.start();
        QTRY_VERIFY(task.isFinished());
        QVERIFY2(task.wasSuccessful(), qPrintable(task.failReason()));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("stage/instance.cfg")),
                                dir.filePath("stage"));
        QCOMPARE(instance.settings()->get("ServerCore").toString(), QString("Paper"));
        QCOMPARE(instance.settings()->get("ServerMinecraftVersion").toString(), QString("1.20.1"));
        QCOMPARE(instance.settings()->get("ServerBuild").toString(), QString("196"));
        QVERIFY(!instance.eulaAccepted());
        QVERIFY(QFile::exists(instance.jarPath()));
        QVERIFY(!QFile::exists(dir.filePath("stage/mmc-pack.json")));
        // Replacing a managed JAR must clear its old software/version labels.
        QFile local(dir.filePath("replacement.jar"));
        QVERIFY(local.open(QIODevice::WriteOnly));
        local.write(jar);
        local.close();
        QString error;
        QVERIFY(instance.importJar(local.fileName(), &error));
        QCOMPARE(instance.settings()->get("ServerCore").toString(), QString("Custom JAR"));
        QVERIFY(instance.settings()->get("ServerMinecraftVersion").toString().isEmpty());
    }
    void cancelledRequestsHaveNoLateSuccessOrCommittedJar()
    {
        QTemporaryDir dir;
        Network network;
        const QUrl url("https://example.test/server.jar");
        network.routes.insert(url.toString(), { "PK\003\004delayed", 200, 50 });
        ServerRequest request(&network, url, dir.filePath("server.jar"));
        QSignalSpy completed(&request, &Task::finished);
        request.start();
        QVERIFY(request.abort());
        QCOMPARE(request.getState(), Task::State::AbortedByUser);
        QTest::qWait(80);
        QCOMPARE(completed.count(), 1);
        QVERIFY(!QFile::exists(dir.filePath("server.jar")));
        network.routes.insert(versionsUrl(Core::Paper).toString(), { "{\"versions\":{\"1.20\":[\"1.20.1\"]}}", 200, 50 });
        ServerCatalogTask task(&network, Core::Paper, ServerCatalogTask::Operation::Versions);
        QSignalSpy catalogFinished(&task, &Task::finished);
        task.start();
        QVERIFY(task.abort());
        QTest::qWait(80);
        QCOMPARE(catalogFinished.count(), 1);
        QCOMPARE(task.getState(), Task::State::AbortedByUser);
    }
    void liveOfficialServices()
    {
        if (!qEnvironmentVariableIsSet("PINECONE_SERVER_LIVE_TESTS"))
            QSKIP("Explicit opt-in for live official services.");
        QNetworkAccessManager network;
        QTemporaryDir dir;
        auto settings = global(dir.filePath("global.cfg"));
        for (const auto core : { Core::Vanilla, Core::Paper, Core::Purpur, Core::Fabric }) {
            ServerCatalogTask versions(&network, core, ServerCatalogTask::Operation::Versions);
            versions.start();
            QTRY_VERIFY_WITH_TIMEOUT(versions.isFinished(), 45000);
            QVERIFY2(versions.wasSuccessful(), qPrintable(versions.failReason()));
            Version selected;
            for (const auto& version : versions.versions())
                if (version.id == "1.20.1")
                    selected = version;
            QCOMPARE(selected.id, QString("1.20.1"));
            ServerCatalogTask resolve(&network, core, ServerCatalogTask::Operation::Resolve, selected);
            resolve.start();
            QTRY_VERIFY_WITH_TIMEOUT(resolve.isFinished(), 60000);
            QVERIFY2(resolve.wasSuccessful(), qPrintable(resolve.failReason()));
            QVERIFY(validDownload(resolve.download()));
            qInfo() << name(core) << resolve.download().minecraftVersion << resolve.download().build << resolve.download().loader;
            if (!qEnvironmentVariableIsSet("PINECONE_SERVER_LIVE_DOWNLOADS"))
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
            QTRY_VERIFY_WITH_TIMEOUT(creation.isFinished(), 180000);
            QVERIFY2(creation.wasSuccessful(), qPrintable(creation.failReason()));
            QVERIFY(QFile::exists(QDir(root).filePath("server/server.jar")));
            QVERIFY(!QFile::exists(QDir(root).filePath("server/eula.txt")));
        }
    }
};
QTEST_GUILESS_MAIN(ServerCatalogTest)
#include "ServerCatalog_test.moc"
