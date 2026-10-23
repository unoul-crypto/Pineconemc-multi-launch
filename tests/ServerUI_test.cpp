// SPDX-License-Identifier: GPL-3.0-only
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include "ServerNetworkFixture.h"
#include "server/PropertiesFile.h"
#include "server/ServerInstance.h"
#include "server/ServerLaunchTask.h"
#include "server/content/ServerContent.h"
#include "settings/INISettingsObject.h"
#include "ui/dialogs/ServerContentDialog.h"
#include "ui/pages/instance/ServerSettingsPage.h"
#include "ui/widgets/ServerOptionsWidget.h"
#include "ui/widgets/ServerSoftwareWidget.h"

class ServerUITest : public QObject {
    Q_OBJECT
   private slots:
    void serverContentSearchFiltersVersionsAndClearsStaleSelection()
    {
        using namespace ServerContent;
        QTemporaryDir dir;
        auto settings = ServerTest::global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        instance.settings()->set("ServerCore", "Mohist");
        instance.settings()->set("ServerMinecraftVersion", "1.16.5");
        const auto profile = target(instance);
        ServerTest::Network network;
        const QJsonObject project{ { "id", "project" }, { "title", "Server mod" }, { "server_side", "optional" } };
        auto hit = project;
        hit.insert("project_id", "project");
        hit.insert("versions", QJsonArray{ "1.16.5" });
        hit.insert("categories", QJsonArray{ "forge" });
        network.routes.insert(searchUrl(profile, Kind::Mod, "").toString(),
                              { QJsonDocument(QJsonObject{ { "hits", QJsonArray{ hit } }, { "total_hits", 1 } }).toJson() });
        network.routes.insert(projectUrl("project").toString(), { QJsonDocument(project).toJson() });
        const QByteArray jar("PK\003\004UI install fixture");
        const QJsonObject file{ { "filename", "project.jar" },
                                { "primary", true },
                                { "size", jar.size() },
                                { "url", "https://cdn.modrinth.com/data/project/project.jar" },
                                { "hashes", QJsonObject{ { "sha512", ServerTest::hash(jar, QCryptographicHash::Sha512) } } } };
        QJsonObject version{ { "id", "good" },
                             { "project_id", "project" },
                             { "name", "Compatible Forge version" },
                             { "version_number", "1.0" },
                             { "version_type", "release" },
                             { "game_versions", QJsonArray{ "1.16.5" } },
                             { "loaders", QJsonArray{ "forge" } },
                             { "files", QJsonArray{ file } } };
        auto wrong = version;
        wrong.insert("id", "wrong");
        wrong.insert("loaders", QJsonArray{ "neoforge" });
        network.routes.insert(versionsUrl(profile, Kind::Mod, "project").toString(),
                              { QJsonDocument(QJsonArray{ wrong, version }).toJson() });
        network.routes.insert(versionUrl("good").toString(), { QJsonDocument(version).toJson() });
        network.routes.insert("https://cdn.modrinth.com/data/project/project.jar", { jar });
        ServerContentDialog dialog(&instance, &network);
        QCOMPARE(dialog.findChild<QComboBox*>("serverContentKind")->count(), 2);
        QVERIFY(!dialog.findChild<QComboBox*>("contentPluginApi")->isEnabled());
        dialog.findChild<QPushButton*>("serverContentSearch")->click();
        auto* results = dialog.findChild<QListWidget*>("serverContentResults");
        QTRY_COMPARE(results->count(), 1);
        results->setCurrentRow(0);
        auto* versions = dialog.findChild<QComboBox*>("serverContentVersion");
        QTRY_COMPARE(versions->count(), 1);
        QCOMPARE(versions->currentData().toString(), QString("good"));
        if (!qEnvironmentVariable("PINECONE_CONTENT_PREVIEW").isEmpty()) {
            dialog.show();
            QTest::qWait(30);
            QVERIFY(dialog.grab().save(qEnvironmentVariable("PINECONE_CONTENT_PREVIEW")));
        }
        QVERIFY(dialog.findChild<QPushButton*>("serverContentInstall")->isEnabled());
        QTimer confirm;
        connect(&confirm, &QTimer::timeout, this, [] {
            for (auto* widget : QApplication::topLevelWidgets())
                if (auto* box = qobject_cast<QMessageBox*>(widget))
                    if (box->standardButtons().testFlag(QMessageBox::Yes))
                        box->button(QMessageBox::Yes)->click();
        });
        confirm.start(10);
        dialog.findChild<QPushButton*>("serverContentInstall")->click();
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(dir.filePath("server/mods/project.jar")), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!instance.isChangingSoftware() && dialog.findChild<QPushButton*>("serverContentInstall")->isEnabled(),
                                 10000);
        confirm.stop();
        dialog.findChild<QComboBox*>("serverContentKind")->setCurrentIndex(1);
        QCOMPARE(results->count(), 0);
        QCOMPARE(versions->count(), 0);
        QVERIFY(!dialog.findChild<QPushButton*>("serverContentInstall")->isEnabled());
    }
    void customJarContentProfileCanBeDeclaredWithoutReplacingJar()
    {
        QTemporaryDir dir;
        auto settings = ServerTest::global(dir.filePath("global.cfg"));
        ServerInstance instance(settings.get(), std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        ServerTest::Network network;
        ServerContentDialog dialog(&instance, &network);
        auto* search = dialog.findChild<QPushButton*>("serverContentSearch");
        QVERIFY(!search->isEnabled());
        auto* minecraft = dialog.findChild<QLineEdit*>("contentMinecraft");
        QVERIFY(!minecraft->isReadOnly());
        QTest::keyClicks(minecraft, "1.16.5");
        auto* loader = dialog.findChild<QComboBox*>("contentModLoader");
        loader->setCurrentIndex(loader->findData("forge"));
        QVERIFY(search->isEnabled());
        search->click();
        QCOMPARE(ServerContent::target(instance).minecraft, QString("1.16.5"));
        QCOMPARE(ServerContent::target(instance).modLoader, QString("forge"));
        QCOMPARE(instance.settings()->get("ServerCore").toString(), QString("Custom JAR"));
    }
    void hybridSelectorRestoresInstalledBuildAndLoader()
    {
        using namespace ServerCatalog;
        for (const auto core : { Core::Mohist, Core::Youer, Core::Arclight }) {
            ServerTest::Network network;
            const ServerCatalog::Version version{ "1.21.1", {} };
            QString installed = "12";
            if (core == Core::Arclight) {
                QJsonArray assets;
                for (const auto& loader : { QString("neoforge"), QString("forge"), QString("fabric") }) {
                    const auto filename = "arclight-" + loader + "-1.21.1-1.0.1.jar";
                    assets.append(QJsonObject{
                        { "name", filename },
                        { "size", 123 },
                        { "browser_download_url", "https://github.com/IzzelAliz/Arclight/releases/download/test/" + filename } });
                }
                const QJsonDocument releases(QJsonArray{ QJsonObject{ { "assets", assets } } });
                network.routes.insert(versionsUrl(core).toString(), { releases.toJson() });
                installed = "1.0.1 (Forge)";
            } else {
                network.routes.insert(versionsUrl(core).toString(), { "[{\"name\":\"1.21.1\"}]" });
                const QJsonObject old{ { "id", 12 }, { "file_sha256", QString(64, 'a') } };
                const QJsonDocument builds(QJsonArray{ QJsonObject{ { "id", 920 } }, old });
                network.routes.insert(buildsUrl(core, version).toString(), { builds.toJson() });
                network.routes.insert(metadataUrl(core, version, installed).toString(), { QJsonDocument(old).toJson() });
            }
            ServerSoftwareWidget widget(&network);
            auto* selector = widget.findChild<QComboBox*>("serverSoftware");
            QCOMPARE(selector->count(), cores().size());
            widget.setSelection(core, version.id, installed);
            widget.setActive(true);
            QTRY_VERIFY(widget.download().has_value());
            QCOMPARE(widget.download()->core, core);
            QCOMPARE(widget.download()->build, installed);
            if (core == Core::Arclight)
                QCOMPARE(widget.download()->loader, QString("Forge"));
        }
    }
    void softwareSelectorKeepsInstalledBuildAndRejectsStaleResults()
    {
        using namespace ServerCatalog;
        ServerTest::Network network;
        const ServerCatalog::Version version{ "1.20.1", {} };
        network.routes.insert(versionsUrl(Core::Paper).toString(), { "{\"versions\":{\"1.20\":[\"1.20.1\"]}}" });
        network.routes.insert(buildsUrl(Core::Paper, version).toString(),
                              { "[{\"id\":196,\"channel\":\"STABLE\"},{\"id\":195,\"channel\":\"STABLE\"}]" });
        for (const auto& build : { QString("195"), QString("196") }) {
            const QJsonDocument metadata(QJsonObject{
                { "id", build.toInt() },
                { "channel", "STABLE" },
                { "downloads",
                  QJsonObject{ { "server:default", QJsonObject{ { "url", "https://example.test/paper.jar" },
                                                                { "checksums", QJsonObject{ { "sha256", QString(64, 'a') } } } } } } } });
            network.routes.insert(metadataUrl(Core::Paper, version, build).toString(), { metadata.toJson() });
        }
        ServerSoftwareWidget widget(&network);
        widget.setSelection(Core::Paper, "1.20.1", "195");
        widget.setActive(true);
        QTRY_VERIFY(widget.download().has_value());
        QCOMPARE(widget.download()->build, QString("195"));
        auto* build = widget.findChild<QComboBox*>("serverCoreVersion");
        QVERIFY(build);
        QCOMPARE(build->currentData().toString(), QString("195"));
        network.routes[metadataUrl(Core::Paper, version, "196").toString()].delay = 100;
        build->setCurrentIndex(build->findData("196"));
        QVERIFY(!widget.download().has_value());
        auto* software = widget.findChild<QComboBox*>("serverSoftware");
        software->setCurrentIndex(software->findData(static_cast<int>(Core::Custom)));
        QTest::qWait(120);
        QCOMPARE(widget.core(), Core::Custom);
        QVERIFY(!widget.download().has_value());
        QVERIFY(!widget.findChild<QComboBox*>("serverMinecraftVersion")->isEnabled());
        widget.setActive(false);
    }
    void optionsRoundTripAndValidation()
    {
        QTemporaryDir dir;
        const auto jar = dir.filePath("my server.jar");
        {
            QFile file(jar);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("PK\003\004fixture");
        }
        ServerOptionsWidget widget;
        widget.setOptions({ jar, "java", 512, 2048, "nogui --custom value", false });
        QVERIFY(widget.validationError().isEmpty());
        QCOMPARE(widget.options().jar, jar);
        QCOMPARE(widget.options().maxMemory, 2048);
        QVERIFY(!widget.options().acceptEula);
        widget.findChild<QSpinBox*>("serverMaxMemory")->setValue(256);
        QVERIFY(!widget.validationError().isEmpty());
        widget.findChild<QSpinBox*>("serverMaxMemory")->setValue(2048);
        widget.findChild<QLineEdit*>("serverJavaPath")->clear();
        QVERIFY(!widget.validationError().isEmpty());
        widget.findChild<QLineEdit*>("serverJavaPath")->setText("java");
        widget.findChild<QLineEdit*>("serverJarPath")->clear();
        QVERIFY(!widget.validationError().isEmpty());
        widget.setLocalJarRequired(false);
        QVERIFY(widget.validationError().isEmpty());
        widget.setLocalJarRequired(true);
        QVERIFY(!widget.validationError().isEmpty());
    }
    void propertiesEditorAppliesOnlyChangedFieldsAndLocksDuringRun()
    {
        QTemporaryDir dir;
        INISettingsObject global(dir.filePath("global.cfg"));
        for (const auto& key :
             { "ShowGameTime", "RecordGameTime", "ShowConsole", "AutoCloseConsole", "ShowConsoleOnError", "LogPrePostOutput" })
            global.registerSetting(key, false);
        for (const auto& key : { "PreLaunchCommand", "WrapperCommand", "PostExitCommand", "JavaPath" })
            global.registerSetting(key, QString());
        global.registerSetting("ConsoleMaxLines", 1000);
        global.registerSetting("ConsoleOverflowStop", false);
        ServerInstance instance(&global, std::make_unique<INISettingsObject>(dir.filePath("instance.cfg")), dir.path());
        const auto source = dir.filePath("source.jar");
        {
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("PK\003\004fixture");
        }
        QString error;
        QVERIFY(instance.importJar(source, &error));
        instance.settings()->set("ServerJavaPath", "java");
        const auto properties = QDir(instance.gameRoot()).filePath("server.properties");
        const QByteArray original = "# user comment\nserver-port=25565\nunknown-setting=keep\ngamemode=1\n";
        {
            QFile file(properties);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(original);
        }
        ServerSettingsPage page(&instance);
        QVERIFY(page.findChild<QLineEdit*>("serverJarPath")->isReadOnly());
        auto* port = page.findChild<QSpinBox*>("server-port");
        QVERIFY(port);
        port->setValue(25566);
        QVERIFY(page.apply());
        QFile file(properties);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto saved = file.readAll();
        file.close();
        QVERIFY(saved.contains("# user comment\n"));
        QVERIFY(saved.contains("unknown-setting=keep\n"));
        QVERIFY(saved.contains("gamemode=1\n"));
        QVERIFY(saved.contains("server-port=25566\n"));
        QVERIFY(!saved.contains("online-mode"));
        auto* task = instance.createLaunchTask({}, {});
        QVERIFY(task);
        QVERIFY(!port->isEnabled());
        QVERIFY(page.apply());
        QVERIFY(task->abort());
        QVERIFY(port->isEnabled());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), saved);
        file.close();
        // A missing JAR must not trap users in an untouched settings page when hiding the console.
        QVERIFY(QFile::remove(instance.jarPath()));
        QVERIFY(page.apply());
    }
};
QTEST_MAIN(ServerUITest)
#include "ServerUI_test.moc"
