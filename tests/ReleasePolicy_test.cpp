// SPDX-License-Identifier: GPL-3.0-only
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include "BuildConfig.h"
#include "updater/prismupdater/ReleasePolicy.h"

class ReleasePolicyTest : public QObject {
    Q_OBJECT
   private:
    static GitHubRelease release(const QString& tag, int id = 1)
    {
        GitHubRelease result{};
        result.id = id;
        result.tag_name = tag;
        result.version = ReleasePolicy::versionForTag(tag);
        return result;
    }

   private slots:
    void releaseOrdering()
    {
        auto legacy = release("multi-launch-v1.0.0");
        auto first = release("11.1.1-multi-launch.1", 2);
        auto second = release("11.1.1-multi-launch.2", 3);
        auto third = release("11.1.1-multi-launch.3", 4);
        QVERIFY(legacy.version < first.version);
        QVERIFY(first.version < second.version);
        QVERIFY(second.version < third.version);
        QVERIFY(third.version < release("11.1.1-multi-launch.10").version);
        QVERIFY(third.version < release("11.1.2-multi-launch.1").version);
        QCOMPARE(third.version, release("v11.1.1-multi-launch.3").version);
        const QList<GitHubRelease> shuffled{ second, legacy, third, first };
        const auto latest = ReleasePolicy::latestRelease(shuffled, false);
        QCOMPARE(latest.tag_name, third.tag_name);
        QVERIFY(!ReleasePolicy::needsUpdate(third.tag_name, latest));
        QVERIFY(ReleasePolicy::needsUpdate(second.tag_name, latest));
        QVERIFY(!ReleasePolicy::needsUpdate(third.tag_name, legacy));
        QVERIFY(!ReleasePolicy::needsUpdate(third.tag_name, second));
        QVERIFY(ReleasePolicy::needsUpdate("11.1.1-codex/release-11.1.1-multi-launch", latest));
        QVERIFY(!ReleasePolicy::needsUpdate("unknown", latest));
        QVERIFY(!ReleasePolicy::needsUpdate(third.tag_name, {}));
    }

    void ignoresUnsupportedDraftAndPrerelease()
    {
        auto stable = release("11.1.1-multi-launch.3");
        auto draft = release("99.0.0", 2);
        draft.draft = true;
        auto beta = release("12.0.0-beta.1", 3);
        beta.prerelease = true;
        auto unrelated = release("zz-nightly", 4);
        QVERIFY(unrelated.version.isEmpty());
        const QList<GitHubRelease> releases{ draft, beta, unrelated, stable };
        QCOMPARE(ReleasePolicy::latestRelease(releases, false).tag_name, stable.tag_name);
        QCOMPARE(ReleasePolicy::latestRelease(releases, true).tag_name, beta.tag_name);
        QVERIFY(!ReleasePolicy::latestRelease({ draft, unrelated }, false).isValid());
    }

    void windowsZipSupportsBothDataLocations()
    {
        auto r = release("11.1.1-multi-launch.3");
        const QString archive = "PineconeMC-Windows-MinGW-w64-Portable-11.1.1-multi-launch.3.zip";
        for (const auto& name :
             { archive, QString("SHA256SUMS.txt"), QString("PineconeMC-Windows-MSVC-Portable.zip"),
               QString("PineconeMC-Windows-MinGW-w64-Qt5-Portable.zip"), QString("PineconeMC-Windows-MinGW-w64-ARM64-Portable.zip"),
               QString("PineconeMC-Windows-MinGW-w64-Legacy-Portable.zip"), QString("PineconeMC-Windows-MinGW-w64-Portable.zip.zsync"),
               QString("PineconeMC-Windows-MinGW-w64-Portable.zip.sha256") }) {
            GitHubReleaseAsset asset{};
            asset.name = name;
            r.assets.append(asset);
        }
        for (bool portable : { false, true }) {
            const auto assets = ReleasePolicy::compatibleAssets(r, "Windows-MinGW-w64-Qt6", portable, false, false, true);
            QCOMPARE(assets.size(), 1);
            QCOMPARE(assets.first().name, archive);
        }
        QVERIFY(ReleasePolicy::compatibleAssets(r, "Windows-MinGW-w64-Qt6", false, false, false, false).isEmpty());
        QVERIFY(ReleasePolicy::compatibleAssets(r, "", true, false, false, true).isEmpty());
    }

    void archivePreservesAppDataMode()
    {
        QTemporaryDir package;
        QVERIFY(package.isValid());
        auto write = [&](const QString& name, const QByteArray& contents) {
            QFile file(package.filePath(name));
            return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
        };
        QVERIFY(write("portable.txt", "marker"));
        QVERIFY(write("manifest.txt", "elyprismlauncher.exe\r\nportable.txt\r\nQt6Core.dll\r\n"));
        QVERIFY(ReleasePolicy::preservePortableMarker(QDir(package.path()), false));
        QVERIFY(!QFile::exists(package.filePath("portable.txt")));
        QFile manifest(package.filePath("manifest.txt"));
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        const auto contents = manifest.readAll();
        QVERIFY(!contents.contains("portable.txt"));
        QVERIFY(contents.contains("elyprismlauncher.exe"));
        QVERIFY(contents.contains("Qt6Core.dll"));
        manifest.close();
        QVERIFY(ReleasePolicy::preservePortableMarker(QDir(package.path()), false));
        QVERIFY(write("portable.txt", "marker"));
        QVERIFY(ReleasePolicy::preservePortableMarker(QDir(package.path()), true));
        QVERIFY(QFile::exists(package.filePath("portable.txt")));
    }

    void displayedVersionIncludesForkRevision()
    {
        QVERIFY(QRegularExpression(R"(^\d+\.\d+\.\d+-multi-launch\.\d+$)").match(BuildConfig.versionString()).hasMatch());
        QCOMPARE(BuildConfig.printableVersionString(), BuildConfig.versionString());
        QVERIFY(!ReleasePolicy::versionForTag(BuildConfig.versionString()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ReleasePolicyTest)
#include "ReleasePolicy_test.moc"
