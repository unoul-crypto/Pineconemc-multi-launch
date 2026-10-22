// SPDX-License-Identifier: GPL-3.0-only
#include "ServerCatalog.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QVersionNumber>
#include <algorithm>

namespace ServerCatalog {
namespace {
QString path(const QString& value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}
bool release(const QString& id)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9]+\\.[0-9]+(?:\\.[0-9]+)?$"));
    return pattern.match(id).hasMatch();
}
bool secure(const QUrl& url)
{
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty() && url.userInfo().isEmpty();
}
bool hex(const QString& value, int length)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-fA-F]+$"));
    return value.size() == length && pattern.match(value).hasMatch();
}
}  // namespace
QString name(Core core)
{
    switch (core) {
        case Core::Vanilla:
            return "Vanilla";
        case Core::Paper:
            return "Paper";
        case Core::Purpur:
            return "Purpur";
        case Core::Fabric:
            return "Fabric";
        case Core::Custom:
            return QObject::tr("Custom JAR");
    }
    return {};
}
QByteArray userAgent()
{
    return "PineconeMC-MultiLaunch/11.1.1 (https://github.com/unoul-crypto/Pineconemc-multi-launch)";
}
QUrl versionsUrl(Core core)
{
    switch (core) {
        case Core::Vanilla:
            return QUrl("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json");
        case Core::Paper:
            return QUrl("https://fill.papermc.io/v3/projects/paper");
        case Core::Purpur:
            return QUrl("https://api.purpurmc.org/v2/purpur");
        case Core::Fabric:
            return QUrl("https://meta.fabricmc.net/v2/versions/game");
        case Core::Custom:
            return {};
    }
    return {};
}
QUrl buildsUrl(Core core, const Version& version)
{
    if (core == Core::Purpur)
        return QUrl("https://api.purpurmc.org/v2/purpur/" + path(version.id));
    return metadataUrl(core, version);
}
QUrl metadataUrl(Core core, const Version& version, const QString& build)
{
    switch (core) {
        case Core::Vanilla:
            return version.metadataUrl;
        case Core::Paper:
            return QUrl("https://fill.papermc.io/v3/projects/paper/versions/" + path(version.id) + "/builds" +
                        (build.isEmpty() ? QString() : "/" + path(build)));
        case Core::Purpur:
            return QUrl("https://api.purpurmc.org/v2/purpur/" + path(version.id) + "/" + (build.isEmpty() ? "latest" : path(build)));
        case Core::Fabric:
            return QUrl("https://meta.fabricmc.net/v2/versions/loader/" + path(version.id));
        case Core::Custom:
            return {};
    }
    return {};
}
bool parseVersions(Core core, const QJsonDocument& json, QList<Version>* result, QString* error)
{
    result->clear();
    const auto object = json.object();
    auto add = [result](const QString& id, const QUrl& url = {}) {
        if (!release(id))
            return;
        for (const auto& existing : *result)
            if (existing.id == id)
                return;
        result->append({ id, url });
    };
    if (core == Core::Vanilla) {
        for (const auto& item : object.value("versions").toArray()) {
            const auto version = item.toObject();
            const QUrl url(version.value("url").toString());
            if (version.value("type") == "release" && secure(url))
                add(version.value("id").toString(), url);
        }
    } else if (core == Core::Paper) {
        const auto groups = object.value("versions").toObject();
        for (const auto& group : groups)
            for (const auto& version : group.toArray())
                add(version.toString());
    } else if (core == Core::Purpur) {
        for (const auto& version : object.value("versions").toArray())
            add(version.toString());
    } else if (core == Core::Fabric) {
        for (const auto& item : json.array()) {
            const auto version = item.toObject();
            if (version.value("stable").toBool())
                add(version.value("version").toString());
        }
    }
    std::sort(result->begin(), result->end(), [](const Version& a, const Version& b) {
        return QVersionNumber::compare(QVersionNumber::fromString(a.id), QVersionNumber::fromString(b.id)) > 0;
    });
    if (result->isEmpty()) {
        *error = QObject::tr("The service returned no supported release versions. Try reloading or select another server software.");
        return false;
    }
    return true;
}
bool parseBuilds(Core core, const Version& version, const QJsonDocument& json, QList<Build>* result, QString* error)
{
    result->clear();
    if (core == Core::Vanilla)
        result->append({ version.id, true });
    else if (core == Core::Paper) {
        for (const auto& value : json.array()) {
            const auto build = value.toObject();
            if (build.value("channel") == "STABLE" && build.value("id").toInt() > 0)
                result->append({ QString::number(build.value("id").toInt()), true });
        }
    } else if (core == Core::Purpur && json.object().value("version").toString() == version.id) {
        for (const auto& value : json.object().value("builds").toObject().value("all").toArray()) {
            const auto id = value.toString();
            bool ok = false;
            if (id.toInt(&ok) > 0 && ok)
                result->append({ id, id == json.object().value("builds").toObject().value("latest").toString() });
        }
    } else if (core == Core::Fabric) {
        for (const auto& value : json.array()) {
            const auto loader = value.toObject().value("loader").toObject();
            const auto id = loader.value("version").toString();
            // Fabric's unattended server launcher requires loader 0.12 or newer.
            if (release(id) && QVersionNumber::compare(QVersionNumber::fromString(id), QVersionNumber(0, 12)) >= 0)
                result->append({ id, loader.value("stable").toBool() });
        }
    }
    std::sort(result->begin(), result->end(), [](const Build& a, const Build& b) {
        return QVersionNumber::compare(QVersionNumber::fromString(a.id), QVersionNumber::fromString(b.id)) > 0;
    });
    if (result->isEmpty()) {
        *error = QObject::tr("No supported server builds are available for this Minecraft version.");
        return false;
    }
    return true;
}
bool validDownload(const Download& download)
{
    if (download.core == Core::Custom || !release(download.minecraftVersion) || !secure(download.url) || download.size < -1)
        return false;
    if (download.core == Core::Fabric)
        return !download.loader.isEmpty() && !download.installer.isEmpty();
    const auto length = download.core == Core::Vanilla ? 40 : download.core == Core::Paper ? 64 : 32;
    const auto algorithm = download.core == Core::Vanilla ? QCryptographicHash::Sha1
                           : download.core == Core::Paper ? QCryptographicHash::Sha256
                                                          : QCryptographicHash::Md5;
    return download.hashAlgorithm == algorithm && hex(download.checksum, length);
}
bool parseDownload(Core core,
                   const Version& version,
                   const QJsonDocument& json,
                   Download* result,
                   QString* error,
                   const QString& requestedBuild)
{
    Download download;
    download.core = core;
    download.minecraftVersion = version.id;
    const auto object = json.object();
    if (core == Core::Purpur && object.contains("result") && object.value("result") != "SUCCESS") {
        *error = QObject::tr("Purpur build %1 did not finish successfully and has no installable JAR. Select another build.")
                     .arg(object.value("build").toString());
        return false;
    }
    if (core == Core::Vanilla && object.value("id").toString() == version.id) {
        const auto server = object.value("downloads").toObject().value("server").toObject();
        download.url = QUrl(server.value("url").toString());
        download.checksum = server.value("sha1").toString();
        download.hashAlgorithm = QCryptographicHash::Sha1;
        download.size = server.value("size").toInteger(-1);
    } else if (core == Core::Paper) {
        QJsonObject latest;
        const auto builds = json.isObject() ? QJsonArray{ json.object() } : json.array();
        for (const auto& item : builds) {
            const auto build = item.toObject();
            if (build.value("channel") == "STABLE" &&
                (requestedBuild.isEmpty() || QString::number(build.value("id").toInt()) == requestedBuild) &&
                build.value("id").toInt() > latest.value("id").toInt())
                latest = build;
        }
        const auto server = latest.value("downloads").toObject().value("server:default").toObject();
        download.url = QUrl(server.value("url").toString());
        download.checksum = server.value("checksums").toObject().value("sha256").toString();
        download.size = server.value("size").toInteger(-1);
        if (!latest.isEmpty())
            download.build = QString::number(latest.value("id").toInt());
    } else if (core == Core::Purpur && object.value("version").toString() == version.id && object.value("result") == "SUCCESS") {
        download.build = object.value("build").toString();
        static const QRegularExpression numeric(QStringLiteral("^[0-9]+$"));
        if (numeric.match(download.build).hasMatch() && (requestedBuild.isEmpty() || requestedBuild == download.build))
            download.url = QUrl("https://api.purpurmc.org/v2/purpur/" + path(version.id) + "/" + download.build + "/download");
        download.checksum = object.value("md5").toString();
        download.hashAlgorithm = QCryptographicHash::Md5;
    }
    if (!validDownload(download)) {
        *error = QObject::tr("No valid stable server download is available for %1 %2. Select another version or server software.")
                     .arg(name(core), version.id);
        return false;
    }
    *result = download;
    return true;
}
QString stableFabricVersion(const QJsonDocument& json, bool loader)
{
    for (const auto& item : json.array()) {
        const auto version = loader ? item.toObject().value("loader").toObject() : item.toObject();
        if (version.value("stable").toBool() && !version.value("version").toString().isEmpty())
            return version.value("version").toString();
    }
    return {};
}
}  // namespace ServerCatalog
