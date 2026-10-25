// SPDX-License-Identifier: GPL-3.0-only
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QImageReader>
#include <QJsonArray>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmapCache>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QUrlQuery>
#include <algorithm>
#include "Markdown.h"
#include "ServerNetworkFixture.h"
#include "server/ServerInstance.h"
#include "server/ServerLaunchTask.h"
#include "server/content/ServerContentTask.h"
#include "ui/dialogs/ReviewMessageBox.h"
#include "ui/pages/instance/ServerContentPage.h"
#include "ui/pages/instance/ServerSettingsPage.h"
#include "ui/widgets/ModFilterWidget.h"
#include "ui/widgets/ProjectDescriptionPage.h"
#include "ui/widgets/ProjectIconCache.h"
#include "ui/widgets/ServerContentWidget.h"

using namespace ServerContent;
namespace {
const Target fabric{ "1.20.1", "fabric", {} };
const QByteArray jar("PK\003\004browser fixture");
const QUrl logo("https://cdn.modrinth.com/browser-test/icon.png");
QJsonObject hit(int i)
{
    return { { "project_id", "p" + QString::number(i) },
             { "title", "Project " + QString::number(i) },
             { "description", "Short summary" },
             { "versions", QJsonArray{ "1.20.1" } },
             { "categories", QJsonArray{ "fabric", "utility" } },
             { "server_side", "optional" },
             { "icon_url", logo.toString() } };
}
QJsonObject info(int i)
{
    return {
        { "id", "p" + QString::number(i) },
        { "title", "Project " + QString::number(i) },
        { "server_side", "optional" },
        { "description", "Short summary" },
        { "body",
          "# Full description\n\nA full paragraph missing from search results.\n\n**Bold text** and [a link](https://modrinth.com)." }
    };
}
QJsonObject version(int i, bool beta = false)
{
    const auto id = "v" + QString::number(i) + (beta ? "beta" : "");
    return { { "id", id },
             { "project_id", "p" + QString::number(i) },
             { "version_number", id },
             { "name", "Version " + id },
             { "version_type", beta ? "beta" : "release" },
             { "game_versions", QJsonArray{ "1.20.1" } },
             { "loaders", QJsonArray{ "fabric" } },
             { "dependencies", QJsonArray() },
             { "files", QJsonArray{ QJsonObject{
                            { "filename", id + ".jar" },
                            { "primary", true },
                            { "size", jar.size() },
                            { "url", "https://cdn.modrinth.com/browser-test/" + id + ".jar" },
                            { "hashes", QJsonObject{ { "sha512", ServerTest::hash(jar, QCryptographicHash::Sha512) } } } } } } };
}
void setup(ServerInstance& instance)
{
    instance.settings()->set("ServerCore", "Fabric");
    instance.settings()->set("ServerMinecraftVersion", "1.20.1");
    QDir().mkpath(instance.gameRoot());
}
void routePage(ServerTest::Network& network, int offset, int count, int total, const SearchOptions& options = {})
{
    QJsonArray hits;
    for (int i = offset; i < offset + count; ++i)
        hits.append(hit(i));
    network.routes.insert(searchUrl(fabric, Kind::Mod, "", offset, options).toString(),
                          { QJsonDocument(QJsonObject{ { "hits", hits }, { "total_hits", total }, { "offset", offset } }).toJson() });
}
void routeProjects(ServerTest::Network& network)
{
    network.routes.insert("https://api.modrinth.com/v2/tag/category",
                          { "[{\"name\":\"utility\",\"project_type\":\"mod\"},{\"name\":\"optimization\",\"project_type\":\"mod\"}]" });
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    network.routes.insert(logo.toString(), { png });
    for (int i = 0; i < 2; ++i) {
        network.routes.insert(projectUrl("p" + QString::number(i)).toString(), { QJsonDocument(info(i)).toJson() });
        network.routes.insert(versionsUrl(fabric, Kind::Mod, "p" + QString::number(i)).toString(),
                              { QJsonDocument(QJsonArray{ version(i, true), version(i) }).toJson() });
        network.routes.insert(versionUrl("v" + QString::number(i)).toString(), { QJsonDocument(version(i)).toJson() });
        network.routes.insert("https://cdn.modrinth.com/browser-test/v" + QString::number(i) + ".jar", { jar });
    }
}
int jarRequests(const ServerTest::Network& network)
{
    return std::count_if(network.requests.begin(), network.requests.end(),
                         [](const auto& request) { return request.url().path().endsWith(".jar"); });
}
}  // namespace
class ServerContentBrowserTest : public QObject {
    Q_OBJECT
   private slots:
    void sharedIconCacheReusesDiskAndRejectsInvalidResponses()
    {
        QTemporaryDir dir;
        ServerTest::Network network;
        routeProjects(network);
        const QUrl url("https://cdn.modrinth.com/browser-test/persistent.png");
        network.routes.insert(url.toString(), network.routes.value(logo.toString()));
        const auto file = dir.filePath("cache/icon");
        ProjectIconCache first(&network);
        QVERIFY(first.icon(url, file).isNull());
        first.icon(url, file);
        QCOMPARE(network.requests.size(), 1);
        QTRY_VERIFY(!first.icon(url, file).isNull());
        QVERIFY(QFileInfo::exists(file));
        QPixmapCache::remove(url.toString());
        ProjectIconCache reopened(&network);
        QVERIFY(!reopened.icon(url, file).isNull());
        QCOMPARE(network.requests.size(), 1);
        const QUrl bad("https://cdn.modrinth.com/browser-test/bad.png");
        network.routes.insert(bad.toString(), { "<html>not an image</html>" });
        reopened.icon(bad);
        QTest::qWait(30);
        QVERIFY(reopened.icon(bad).isNull());
        QCOMPARE(network.requests.size(), 2);
    }
    void paginationFiltersIconsAndFullDescription()
    {
        QTemporaryDir dir;
        auto settings = ServerTest::global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        setup(instance);
        ServerTest::Network network;
        routeProjects(network);
        routePage(network, 0, 50, 123);
        routePage(network, 50, 50, 123);
        routePage(network, 100, 23, 123);
        ServerContentWidget widget(&instance, &network, nullptr, Kind::Mod);
        auto* list = widget.findChild<QListWidget*>("serverContentResults");
        auto* more = widget.findChild<QPushButton*>("serverContentMore");
        widget.findChild<QPushButton*>("serverContentSearch")->click();
        QTRY_COMPARE(list->count(), 50);
        QPixmap pixmap;
        QTRY_VERIFY(QPixmapCache::find(logo.toString(), &pixmap));
        QCOMPARE(pixmap.toImage().pixelColor(0, 0), QColor(Qt::green));
        list->setCurrentRow(0);
        auto* versions = widget.findChild<QComboBox*>("serverContentVersion");
        QTRY_COMPARE(versions->count(), 1);
        auto* description = widget.findChild<ProjectDescriptionPage*>("serverContentDescription");
        QVERIFY(description->toPlainText().contains("A full paragraph missing from search results."));
        QVERIFY(description->toHtml().contains("font-weight:700") || description->toHtml().contains("font-weight:600"));
        widget.findChild<QPushButton*>("serverContentSelect")->click();
        auto* selection = widget.findChild<QListWidget*>("serverContentSelection");
        QCOMPARE(selection->count(), 1);
        more->click();
        QTRY_COMPARE(list->count(), 100);
        more->click();
        QTRY_COMPARE(list->count(), 123);
        QVERIFY(!more->isEnabled());
        QCOMPARE(selection->count(), 1);
        QCOMPARE(jarRequests(network), 0);
        if (!qEnvironmentVariable("PINECONE_BROWSER_PREVIEW").isEmpty()) {
            widget.resize(1200, 850);
            widget.show();
            widget.findChild<QPushButton*>("serverContentFilters")->click();
            QTest::qWait(50);
            QVERIFY(widget.grab().save(qEnvironmentVariable("PINECONE_BROWSER_PREVIEW")));
            widget.hide();
        }
        SearchOptions options;
        options.sort = "newest";
        routePage(network, 0, 50, 123, options);
        auto* sort = widget.findChild<QComboBox*>("serverContentSort");
        sort->setCurrentIndex(sort->findData("newest"));
        QTRY_COMPARE(list->count(), 50);
        QCOMPARE(selection->count(), 1);
        options.openSource = true;
        routePage(network, 0, 50, 123, options);
        widget.findChild<QCheckBox*>("openSource")->setChecked(true);
        QTRY_COMPARE(list->count(), 50);
        options.categories = { "utility" };
        routePage(network, 0, 50, 123, options);
        QCheckBox* category = nullptr;
        for (auto* box : widget.findChild<ModFilterWidget*>()->findChildren<QCheckBox*>())
            if (box->text() == "utility")
                category = box;
        QVERIFY(category);
        category->setChecked(true);
        QTRY_COMPARE(list->count(), 50);
        const auto facets = QUrlQuery(network.requests.last().url()).queryItemValue("facets", QUrl::FullyDecoded);
        QVERIFY(facets.contains("open_source:true"));
        QVERIFY(facets.contains("categories:utility"));
        widget.findChild<QCheckBox*>("betaCb")->setChecked(true);
        QTRY_COMPARE(list->count(), 50);
        list->setCurrentRow(1);
        QTRY_COMPARE(versions->count(), 2);
        QVERIFY(!widget.findChild<QCheckBox*>("serverSide")->isEnabled());
        QVERIFY(!widget.findChild<QComboBox*>("version")->isEnabled());
    }
    void multipleSelectionsWaitForReviewAndCancellationKeepsQueue()
    {
        QTemporaryDir dir;
        auto settings = ServerTest::global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        setup(instance);
        ServerTest::Network network;
        routeProjects(network);
        routePage(network, 0, 2, 2);
        ServerContentWidget widget(&instance, &network, nullptr, Kind::Mod);
        widget.findChild<QPushButton*>("serverContentSearch")->click();
        auto* list = widget.findChild<QListWidget*>("serverContentResults");
        auto* select = widget.findChild<QPushButton*>("serverContentSelect");
        QTRY_COMPARE(list->count(), 2);
        for (int row = 0; row < 2; ++row) {
            list->setCurrentRow(row);
            QTRY_VERIFY(select->isEnabled());
            select->click();
        }
        auto* selected = widget.findChild<QListWidget*>("serverContentSelection");
        QCOMPARE(selected->count(), 2);
        QCOMPARE(jarRequests(network), 0);
        auto* download = widget.findChild<QPushButton*>("serverContentInstall");
        QTimer review;
        bool accept = false;
        int reviews = 0;
        connect(&review, &QTimer::timeout, this, [&] {
            for (auto* window : QApplication::topLevelWidgets())
                if (auto* dialog = qobject_cast<ReviewMessageBox*>(window)) {
                    ++reviews;
                    if (accept)
                        dialog->accept();
                    else
                        dialog->reject();
                    return;
                }
        });
        review.start(10);
        download->click();
        QTRY_COMPARE(reviews, 1);
        QTRY_VERIFY(download->isEnabled());
        QCOMPARE(selected->count(), 2);
        QCOMPARE(jarRequests(network), 0);
        accept = true;
        download->click();
        QTRY_COMPARE_WITH_TIMEOUT(selected->count(), 0, 10000);
        QCOMPARE(reviews, 2);
        QCOMPARE(jarRequests(network), 2);
        QVERIFY(QFileInfo::exists(dir.filePath("server/mods/v0.jar")));
        QVERIFY(QFileInfo::exists(dir.filePath("server/mods/v1.jar")));
        review.stop();
    }
    void separatePagesKeepSelectionsAndInvalidateChangedProfile()
    {
        QTemporaryDir dir;
        auto settings = ServerTest::global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        setup(instance);
        ServerTest::Network network;
        routeProjects(network);
        routePage(network, 0, 2, 2);
        ServerContentPage mods(&instance, Kind::Mod, &network), plugins(&instance, Kind::Plugin, &network);
        QCOMPARE(mods.id(), QString("server-mods"));
        QCOMPARE(plugins.id(), QString("server-plugins"));
        QCOMPARE(mods.displayName(), QString("Mods"));
        QCOMPARE(plugins.displayName(), QString("Plugins"));
        QCOMPARE(mods.findChild<QComboBox*>("serverContentKind")->count(), 1);
        QCOMPARE(plugins.findChild<QComboBox*>("serverContentKind")->currentData().toInt(), static_cast<int>(Kind::Plugin));
        QVERIFY(!plugins.findChild<QPushButton*>("serverContentSearch")->isEnabled());
        ServerSettingsPage settingsPage(&instance);
        QVERIFY(!settingsPage.findChild<QPushButton*>("serverContent"));
        mods.opened();
        mods.findChild<QPushButton*>("serverContentSearch")->click();
        auto* list = mods.findChild<QListWidget*>("serverContentResults");
        QTRY_COMPARE(list->count(), 2);
        list->setCurrentRow(0);
        auto* select = mods.findChild<QPushButton*>("serverContentSelect");
        QTRY_VERIFY(select->isEnabled());
        select->click();
        mods.closed();
        plugins.opened();
        plugins.closed();
        mods.opened();
        QCOMPARE(mods.findChild<QListWidget*>("serverContentSelection")->count(), 1);
        auto* running = instance.createLaunchTask({}, {});
        QVERIFY(running);
        QVERIFY(!mods.findChild<QPushButton*>("serverContentInstall")->isEnabled());
        QVERIFY(running->abort());
        instance.settings()->set("ServerCore", "Youer");
        instance.settings()->set("ServerMinecraftVersion", "1.21.1");
        mods.opened();
        QCOMPARE(mods.findChild<QListWidget*>("serverContentSelection")->count(), 0);
        QCOMPARE(list->count(), 0);
    }
    void reviewLocksDependenciesAndIdentifiesDuplicateNamesById()
    {
        auto review = std::unique_ptr<ReviewMessageBox>(ReviewMessageBox::create(nullptr, "Review"));
        review->appendResource({ .name = "Same name", .id = "a" });
        review->appendResource({ .name = "Same name", .id = "b" });
        review->appendResource({ .name = "Required", .required_by = { "a" }, .id = "dep", .locked = true });
        auto* tree = review->findChild<QTreeWidget*>();
        tree->topLevelItem(1)->setCheckState(0, Qt::Unchecked);
        QCOMPARE(review->deselectedResourceIds(), QStringList{ "b" });
        QVERIFY(!tree->topLevelItem(2)->flags().testFlag(Qt::ItemIsUserCheckable));
        QCOMPARE(tree->topLevelItem(2)->checkState(0), Qt::Checked);
    }
    void liveModrinthDescriptionsAndIcons()
    {
        if (!qEnvironmentVariableIsSet("PINECONE_BROWSER_LIVE_TESTS"))
            QSKIP("Opt-in live Modrinth assets.");
        QVERIFY2(QImageReader::supportedImageFormats().contains("webp"), "The packaged Qt WebP plugin is required.");
        QNetworkAccessManager network;
        ProjectIconCache icons(&network);
        for (const auto kind : { Kind::Mod, Kind::Plugin }) {
            const Target profile = kind == Kind::Mod ? fabric : Target{ "1.20.1", {}, { "bukkit", "spigot", "paper" } };
            ServerContentTask::Query query;
            query.text = kind == Kind::Mod ? "fabric-api" : "luckperms";
            ServerContentTask search(&network, profile, kind, query);
            search.start();
            QTRY_VERIFY_WITH_TIMEOUT(search.isFinished(), 60000);
            QVERIFY2(search.wasSuccessful(), qPrintable(search.failReason()));
            QVERIFY(!search.projects().isEmpty());
            const auto project = search.projects().first();
            QTRY_VERIFY_WITH_TIMEOUT(!icons.icon(project.icon).isNull(), 60000);
            query.operation = ServerContentTask::Operation::Versions;
            query.projectId = project.id;
            QString body;
            ServerContentTask versions(&network, profile, kind, query);
            connect(&versions, &ServerContentTask::projectInfoReady, this,
                    [&](const QJsonObject& info) { body = info.value("body").toString(); });
            versions.start();
            QTRY_VERIFY_WITH_TIMEOUT(versions.isFinished(), 60000);
            QVERIFY2(versions.wasSuccessful(), qPrintable(versions.failReason()));
            QVERIFY(body.size() > 100);
            QVERIFY(!markdownToHTML(body).isEmpty());
            qInfo() << project.title << project.icon << body.size() << "description characters";
        }
    }
};
QTEST_MAIN(ServerContentBrowserTest)
#include "ServerContentBrowser_test.moc"
