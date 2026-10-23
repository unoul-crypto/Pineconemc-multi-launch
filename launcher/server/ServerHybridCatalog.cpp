// SPDX-License-Identifier: GPL-3.0-only
#include "ServerHybridCatalog.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVersionNumber>
#include <algorithm>

using namespace ServerCatalog;
namespace {
QString project(Core core)
{
    return core == Core::Mohist ? "mohist" : "youer";
}
QString path(const QString& value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}
struct ArclightAsset {
    QString minecraft;
    QString build;
    QString loader;
    QJsonObject metadata;
};
QList<ArclightAsset> arclightAssets(const QJsonDocument& json)
{
    QList<ArclightAsset> result;
    static const QRegularExpression filename(QStringLiteral(
        "^arclight-(forge|neoforge|fabric)-([0-9]+\\.[0-9]+(?:\\.[0-9]+)?)-([0-9]+\\.[0-9]+(?:\\.[0-9]+)?(?:-[0-9a-f]+)?)\\.jar$"));
    for (const auto& value : json.array()) {
        const auto release = value.toObject();
        if (release.value("draft").toBool() || release.value("prerelease").toBool())
            continue;
        for (const auto& entry : release.value("assets").toArray()) {
            const auto asset = entry.toObject();
            const auto name = asset.value("name").toString();
            const auto match = filename.match(name);
            const QUrl url(asset.value("browser_download_url").toString());
            if (!match.hasMatch() || url.scheme() != "https" || url.host() != "github.com" || !url.userInfo().isEmpty() ||
                !url.path().startsWith("/IzzelAliz/Arclight/releases/download/") || !url.path().endsWith("/" + name) ||
                asset.value("size").toInteger() <= 0)
                continue;
            const auto loader = match.captured(1) == "neoforge" ? QString("NeoForge")
                                : match.captured(1) == "forge"  ? QString("Forge")
                                                                : QString("Fabric");
            result.append({ match.captured(2), match.captured(3) + " (" + loader + ")", loader, asset });
        }
    }
    // Preserve GitHub's release order for equal versions and their explicit loader variants.
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return QVersionNumber::compare(QVersionNumber::fromString(a.build), QVersionNumber::fromString(b.build)) > 0;
    });
    return result;
}
}  // namespace
namespace ServerHybridCatalog {
bool handles(Core core)
{
    return core == Core::Mohist || core == Core::Youer || core == Core::Arclight;
}
QUrl versionsUrl(Core core)
{
    if (core == Core::Arclight)
        return QUrl("https://api.github.com/repos/IzzelAliz/Arclight/releases?per_page=100&page=1");
    return QUrl("https://api.mohistmc.com/project/" + project(core) + "/versions");
}
QUrl metadataUrl(Core core, const ServerCatalog::Version& version, const QString& build)
{
    if (core == Core::Arclight)
        return ServerHybridCatalog::versionsUrl(core);
    return QUrl("https://api.mohistmc.com/project/" + project(core) + "/" + path(version.id) + "/builds" +
                (build.isEmpty() ? QString() : "/" + path(build)));
}
QList<ServerCatalog::Version> versions(Core core, const QJsonDocument& json)
{
    QList<ServerCatalog::Version> result;
    if (core == Core::Arclight) {
        for (const auto& asset : arclightAssets(json))
            result.append({ asset.minecraft, {} });
    } else {
        for (const auto& value : json.array())
            result.append({ value.toObject().value("name").toString(), {} });
    }
    return result;
}
QList<Build> builds(Core core, const ServerCatalog::Version& version, const QJsonDocument& json)
{
    QList<Build> result;
    if (core == Core::Arclight) {
        for (const auto& asset : arclightAssets(json))
            if (asset.minecraft == version.id &&
                std::none_of(result.begin(), result.end(), [&asset](const auto& b) { return b.id == asset.build; }))
                result.append({ asset.build, true });
    } else {
        for (const auto& value : json.array()) {
            const auto id = value.toObject().value("id").toInteger();
            if (id > 0)
                result.append({ QString::number(id), true });
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id.toLongLong() > b.id.toLongLong(); });
    }
    return result;
}
Download download(Core core, const ServerCatalog::Version& version, const QJsonDocument& json, const QString& requested)
{
    Download result;
    result.core = core;
    result.minecraftVersion = version.id;
    if (core == Core::Arclight) {
        for (const auto& asset : arclightAssets(json)) {
            if (asset.minecraft != version.id || (!requested.isEmpty() && asset.build != requested))
                continue;
            result.build = asset.build;
            result.loader = asset.loader;
            result.url = QUrl(asset.metadata.value("browser_download_url").toString());
            result.size = asset.metadata.value("size").toInteger(-1);
            const auto digest = asset.metadata.value("digest").toString();
            // Older GitHub assets have no digest. Never discard an unsupported or malformed supplied digest.
            result.checksum = digest.startsWith("sha256:") && digest.size() > 7 ? digest.mid(7) : digest.isEmpty() ? QString() : "invalid";
            return result;
        }
    } else {
        QJsonObject selected;
        const auto entries = json.isObject() ? QJsonArray{ json.object() } : json.array();
        for (const auto& entry : entries) {
            const auto object = entry.toObject();
            const auto id = object.value("id").toInteger();
            if (id > selected.value("id").toInteger() && (requested.isEmpty() || requested == QString::number(id)))
                selected = object;
        }
        if (!selected.isEmpty()) {
            result.build = QString::number(selected.value("id").toInteger());
            result.url = QUrl(ServerHybridCatalog::metadataUrl(core, version, result.build).toString() + "/download");
            result.checksum = selected.value("file_sha256").toString();
        }
    }
    return result;
}
}  // namespace ServerHybridCatalog
