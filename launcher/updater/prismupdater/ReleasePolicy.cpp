// SPDX-License-Identifier: GPL-3.0-only
#include "ReleasePolicy.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>

namespace ReleasePolicy {
Version versionForTag(const QString& tag)
{
    static const QRegularExpression forkTag(R"(^v?(\d+\.\d+\.\d+)-multi-launch\.(\d+)$)");
    static const QRegularExpression legacyTag(R"(^multi-launch-v(\d+\.\d+\.\d+)$)");
    static const QRegularExpression numericTag(R"(^v?(\d+(?:\.\d+)*(?:-[A-Za-z0-9][A-Za-z0-9.-]*)?(?:\+[A-Za-z0-9.-]+)?)$)");
    if (auto match = forkTag.match(tag); match.hasMatch())
        return Version(match.captured(1) + "." + match.captured(2));
    if (auto match = legacyTag.match(tag); match.hasMatch())
        return Version(match.captured(1));
    if (auto match = numericTag.match(tag); match.hasMatch())
        return Version(match.captured(1));
    return {};
}

bool needsUpdate(const QString& installedVersion, const GitHubRelease& release)
{
    auto current = versionForTag(installedVersion);
    if (current.isEmpty()) {
        // Older development builds printed a branch name instead of the fork revision.
        static const QRegularExpression oldDevelopmentVersion(R"(^(\d+\.\d+\.\d+)-.+$)");
        const auto match = oldDevelopmentVersion.match(installedVersion);
        if (match.hasMatch())
            current = Version(match.captured(1));
    }
    return release.isValid() && !release.version.isEmpty() && !current.isEmpty() && current < release.version;
}

GitHubRelease latestRelease(const QList<GitHubRelease>& releases, bool allowPreRelease)
{
    GitHubRelease latest;
    for (const auto& release : releases) {
        if (release.draft || (release.prerelease && !allowPreRelease) || release.version.isEmpty())
            continue;
        if (!latest.isValid() || release.version > latest.version)
            latest = release;
    }
    return latest;
}

QList<GitHubReleaseAsset> compatibleAssets(const GitHubRelease& release,
                                           const QString& buildArtifact,
                                           bool isPortable,
                                           bool isAppImage,
                                           bool isArm,
                                           bool allowWindowsArchives)
{
    QList<GitHubReleaseAsset> valid;
    const auto platformParts = buildArtifact.toLower().split("-qt");
    const auto platform = platformParts.first();
    const auto qtVersion = platformParts.size() > 1 ? platformParts.at(1).toInt() : 0;
    static const QRegularExpression qtPattern(R"(-qt(\d+))");
    for (const auto& asset : release.assets) {
        const auto name = asset.name.toLower();
        const bool archive = name.endsWith(".zip") || name.endsWith(".tar.gz");
        const bool windowsArchive = allowWindowsArchives && platform.contains("windows") && archive;
        if (!archive && !name.endsWith(".exe") && !name.endsWith("appimage"))
            continue;
        if (name.endsWith(".zsync") || name.endsWith("appimage") != isAppImage)
            continue;
        if (platform.isEmpty() || !name.contains(platform))
            continue;
        if (name.contains("legacy") && !platform.contains("legacy"))
            continue;
        if (name.contains("arm64") != isArm)
            continue;
        if (platform.contains("windows") && !isPortable && archive && !windowsArchive)
            continue;
        if (!windowsArchive && name.contains("portable") != isPortable)
            continue;
        if (auto match = qtPattern.match(name); match.hasMatch() && match.captured(1).toInt() != qtVersion)
            continue;
        valid.append(asset);
    }
    return valid;
}

bool preservePortableMarker(const QDir& extractedPackage, bool hadPortableMarker)
{
    if (hadPortableMarker)
        return true;
    const auto markerPath = extractedPackage.absoluteFilePath("portable.txt");
    if (QFile::exists(markerPath) && !QFile::remove(markerPath))
        return false;
    const auto manifestPath = extractedPackage.absoluteFilePath("manifest.txt");
    QFile manifest(manifestPath);
    if (!manifest.exists())
        return true;
    if (!manifest.open(QIODevice::ReadOnly))
        return false;
    auto lines = QString::fromUtf8(manifest.readAll()).split('\n');
    manifest.close();
    lines.removeIf([](const QString& line) { return line.trimmed() == "portable.txt"; });
    const auto contents = lines.join('\n').toUtf8();
    QSaveFile output(manifestPath);
    return output.open(QIODevice::WriteOnly) && output.write(contents) == contents.size() && output.commit();
}
}  // namespace ReleasePolicy
