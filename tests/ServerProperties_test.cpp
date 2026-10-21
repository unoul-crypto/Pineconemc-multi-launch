// SPDX-License-Identifier: GPL-3.0-only
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include "server/PropertiesFile.h"

class ServerPropertiesTest : public QObject {
    Q_OBJECT
   private slots:
    void javaPropertiesSyntax()
    {
        const auto file = PropertiesFile::parse(
            "# comment\\\n! comment\n motd : Hello\\ world\\u0021\r\nkey\\=name=value\\:ok\n"
            "continued=first\\\n  second\nempty=\nduplicate=old\nduplicate=new\n");
        QCOMPARE(file.value("motd"), QString("Hello world!"));
        QCOMPARE(file.value("key=name"), QString("value:ok"));
        QCOMPARE(file.value("continued"), QString("firstsecond"));
        QCOMPARE(file.value("empty", "fallback"), QString());
        QCOMPARE(file.value("duplicate"), QString("new"));
        QCOMPARE(file.value("missing", "fallback"), QString("fallback"));
    }
    void editingPreservesUnknownBytesAndBacksUp()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("server.properties");
        const QByteArray original =
            "# original comment\r\ncustom-key=keep\\u0020this\r\nmotd=old\\\r\n "
            "continued\r\nserver-port=25565\r\nmotd=last\r\nunknown-without-newline=yes";
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), original.size());
        }
        QString error;
        const QString motd = QString::fromUtf8("Тест: привет 🌲\nnext=line");
        QVERIFY2(PropertiesFile::update(path, { { "motd", motd }, { "max-players", "12" } }, &error), qPrintable(error));
        QFile output(path);
        QVERIFY(output.open(QIODevice::ReadOnly));
        const auto contents = output.readAll();
        output.close();
        QVERIFY(contents.contains("# original comment\r\ncustom-key=keep\\u0020this\r\n"));
        QVERIFY(contents.contains("server-port=25565\r\n"));
        QVERIFY(contents.contains("unknown-without-newline=yes\r\n"));
        auto parsed = PropertiesFile::parse(contents);
        QCOMPARE(parsed.value("motd"), motd);
        QCOMPARE(parsed.value("max-players"), QString("12"));
        const auto backups = QDir(dir.path()).entryList({ "server.properties.launcher-backup-*" }, QDir::Files);
        QCOMPARE(backups.size(), 1);
        QFile backup(dir.filePath(backups.first()));
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QCOMPARE(backup.readAll(), original);
        // A second edit reads the latest file, preserving edits made by other tools.
        {
            QFile external(path);
            QVERIFY(external.open(QIODevice::Append));
            external.write("external=preserved\r\n");
        }
        QVERIFY2(PropertiesFile::update(path, { { "server-port", "25566" } }, &error), qPrintable(error));
        PropertiesFile current;
        QVERIFY(PropertiesFile::read(path, &current, &error));
        QCOMPARE(current.value("external"), QString("preserved"));
        QCOMPARE(current.value("server-port"), QString("25566"));
    }
    void noChangesDoNotCreateFiles()
    {
        QTemporaryDir dir;
        QVERIFY(PropertiesFile::update(dir.filePath("server.properties"), {}));
        QVERIFY(!QFile::exists(dir.filePath("server.properties")));
    }
};
QTEST_GUILESS_MAIN(ServerPropertiesTest)
#include "ServerProperties_test.moc"
