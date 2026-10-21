// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "GitHubRelease.h"

#include <QDir>

namespace ReleasePolicy {
// Keep launcher-specific tag ordering separate from FlexVer used by Minecraft/mods.
Version versionForTag(const QString& tag);
bool needsUpdate(const QString& installedVersion, const GitHubRelease& release);
GitHubRelease latestRelease(const QList<GitHubRelease>& releases, bool allowPreRelease);
QList<GitHubReleaseAsset> compatibleAssets(const GitHubRelease& release,
                                           const QString& buildArtifact,
                                           bool isPortable,
                                           bool isAppImage,
                                           bool isArm,
                                           bool allowWindowsArchives);
// A ZIP package must not switch an AppData installation into portable mode.
bool preservePortableMarker(const QDir& extractedPackage, bool hadPortableMarker);
}  // namespace ReleasePolicy
