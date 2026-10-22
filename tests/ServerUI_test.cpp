// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include "server/PropertiesFile.h"
#include "server/ServerInstance.h"
#include "server/ServerLaunchTask.h"
#include "settings/INISettingsObject.h"
#include "ui/pages/instance/ServerSettingsPage.h"
#include "ui/widgets/ServerOptionsWidget.h"

class ServerUITest : public QObject {
    Q_OBJECT
   private slots:
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
