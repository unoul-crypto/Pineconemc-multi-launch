// SPDX-License-Identifier: GPL-3.0-only
#include "ServerContent.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QUrlQuery>
#include <algorithm>
#include "server/ServerInstance.h"

namespace ServerContent {
namespace {
QString compact(const QStringList& values)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(values)).toJson(QJsonDocument::Compact));
}
bool serverEnvironment(const QString& environment)
{
    return QStringList{
        "client_and_server", "client_only_server_optional",  "server_only", "server_only_client_optional", "dedicated_server_only",
        "client_or_server",  "client_or_server_prefers_both"
    }
        .contains(environment);
}
bool intersects(const QJsonArray& values, const QStringList& accepted)
{
    return std::any_of(values.begin(), values.end(), [&accepted](const auto& value) { return accepted.contains(value.toString()); });
}
QString path(const QString& value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}
}  // namespace
QStringList Target::loaders(Kind kind) const
{
    return kind == Kind::Plugin ? pluginLoaders : modLoader.isEmpty() ? QStringList() : QStringList{ modLoader };
}
bool Target::supports(Kind kind) const
{
    static const QRegularExpression release(QStringLiteral("^[0-9]+\\.[0-9]+(?:\\.[0-9]+)?$"));
    const auto accepted = loaders(kind);
    const QStringList allowed =
        kind == Kind::Mod ? QStringList{ "fabric", "forge", "neoforge" } : QStringList{ "bukkit", "spigot", "paper", "purpur" };
    return release.match(minecraft).hasMatch() && !accepted.isEmpty() &&
           std::all_of(accepted.begin(), accepted.end(), [&allowed](const auto& value) { return allowed.contains(value); });
}
Target target(ServerInstance& instance)
{
    auto* settings = instance.settings();
    Target result;
    result.minecraft = settings->get("ServerMinecraftVersion").toString();
    const auto core = settings->get("ServerCore").toString();
    if (core == "Fabric")
        result.modLoader = "fabric";
    if (core == "Mohist")
        result.modLoader = "forge";
    if (core == "Youer")
        result.modLoader = "neoforge";
    if (core == "Arclight")
        result.modLoader = settings->get("ServerLoaderVersion").toString().toLower();
    if (QStringList{ "Paper", "Purpur", "Mohist", "Youer", "Arclight" }.contains(core))
        result.pluginLoaders = { "bukkit", "spigot" };
    if (QStringList{ "Paper", "Purpur", "Youer" }.contains(core))
        result.pluginLoaders.append("paper");
    if (core == "Purpur" || core == "Youer")
        result.pluginLoaders.append("purpur");
    if (core == "Custom JAR" || core == QObject::tr("Custom JAR")) {
        result.minecraft = settings->get("ServerContentMinecraftVersion").toString();
        result.modLoader = settings->get("ServerContentModLoader").toString();
        const auto api = settings->get("ServerContentPluginApi").toString();
        if (!api.isEmpty()) {
            result.pluginLoaders = { "bukkit", "spigot" };
            if (api == "paper" || api == "purpur")
                result.pluginLoaders.append("paper");
            if (api == "purpur")
                result.pluginLoaders.append("purpur");
            if (api != "spigot" && api != "paper" && api != "purpur")
                result.pluginLoaders.clear();
        }
    }
    return result;
}
QString folder(Kind kind)
{
    return kind == Kind::Mod ? "mods" : "plugins";
}
bool validId(const QString& id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
    return pattern.match(id).hasMatch();
}
bool safeFilename(const QString& filename)
{
    static const QRegularExpression forbidden(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    static const QRegularExpression device(QStringLiteral("^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\\.|$)"),
                                           QRegularExpression::CaseInsensitiveOption);
    return filename.size() <= 200 && filename.trimmed() == filename && !filename.startsWith('.') &&
           filename.endsWith(".jar", Qt::CaseInsensitive) && !forbidden.match(filename).hasMatch() && !device.match(filename).hasMatch();
}
bool validHash(const QString& sha512)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-fA-F]{128}$"));
    return pattern.match(sha512).hasMatch();
}
bool serverProject(const QJsonObject& project)
{
    const auto side = project.value("server_side").toString();
    if (side == "unsupported")
        return false;
    if (side == "required" || side == "optional")
        return true;
    const auto environment = project.value("environment");
    if (environment.isString())
        return serverEnvironment(environment.toString());
    const auto environments = environment.toArray();
    return std::any_of(environments.begin(), environments.end(), [](const auto& value) { return serverEnvironment(value.toString()); });
}
QUrl searchUrl(const Target& target, Kind kind, const QString& text, int offset, const SearchOptions& options)
{
    QUrl url("https://api.modrinth.com/v2/search");
    QJsonArray loaders;
    for (const auto& loader : target.loaders(kind))
        loaders.append("categories:" + loader);
    QJsonArray facets{ QJsonArray{ kind == Kind::Mod ? "project_type:mod" : "project_type:plugin" },
                       QJsonArray{ "versions:" + target.minecraft }, loaders,
                       QJsonArray{ "server_side:required", "server_side:optional" } };
    QUrlQuery query;
    if (!options.categories.isEmpty()) {
        QJsonArray categories;
        for (const auto& category : options.categories)
            categories.append("categories:" + category);
        facets.append(categories);
    }
    if (options.openSource)
        facets.append(QJsonArray{ "open_source:true" });
    for (const auto& id : options.excludedProjects)
        if (validId(id))
            facets.append(QJsonArray{ "project_id!=" + id });
    query.addQueryItem("query", text);
    query.addQueryItem("facets", QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact)));
    const QStringList sorts{ "relevance", "downloads", "follows", "newest", "updated" };
    query.addQueryItem("index", sorts.contains(options.sort) ? options.sort : text.isEmpty() ? "downloads" : "relevance");
    query.addQueryItem("limit", QString::number(qBound(1, options.limit, 100)));
    query.addQueryItem("offset", QString::number(qMax(0, offset)));
    url.setQuery(query);
    return url;
}
QUrl projectUrl(const QString& project)
{
    return QUrl("https://api.modrinth.com/v2/project/" + path(project));
}
QUrl versionUrl(const QString& version)
{
    return QUrl("https://api.modrinth.com/v2/version/" + path(version));
}
QUrl versionsUrl(const Target& target, Kind kind, const QString& project)
{
    auto url = projectUrl(project);
    url.setPath(url.path() + "/version");
    QUrlQuery query;
    query.addQueryItem("loaders", compact(target.loaders(kind)));
    query.addQueryItem("game_versions", compact({ target.minecraft }));
    query.addQueryItem("include_changelog", "false");
    url.setQuery(query);
    return url;
}
bool parseSearch(const Target& target, Kind kind, const QJsonDocument& json, QList<Project>* projects, int* total, QString* error)
{
    projects->clear();
    if (!json.isObject() || !json.object().value("hits").isArray()) {
        *error = QObject::tr("Modrinth returned an invalid search result.");
        return false;
    }
    *total = json.object().value("total_hits").toInt();
    for (const auto& value : json.object().value("hits").toArray()) {
        const auto item = value.toObject();
        if (!serverProject(item) || !item.value("versions").toArray().contains(target.minecraft) ||
            !intersects(item.value("categories").toArray(), target.loaders(kind)) || !validId(item.value("project_id").toString()))
            continue;
        projects->append({ item.value("project_id").toString(), item.value("title").toString(), item.value("description").toString(),
                           item.value("slug").toString(), QUrl(item.value("icon_url").toString()) });
    }
    return true;
}
bool parseVersion(const Target& target, Kind kind, const QJsonObject& json, AddonVersion* result, QString* error)
{
    auto fail = [error](const QString& reason) {
        *error = reason;
        return false;
    };
    const auto environment = json.value("environment").toString();
    if (!target.supports(kind) || !json.value("game_versions").toArray().contains(target.minecraft) ||
        !intersects(json.value("loaders").toArray(), target.loaders(kind)) ||
        (!environment.isEmpty() && environment != "unknown" && !serverEnvironment(environment)))
        return fail(QObject::tr("This file does not support the server's Minecraft version, mod loader or server environment."));
    const auto status = json.value("status").toString();
    if (!status.isEmpty() && status != "listed" && status != "archived" && status != "unlisted")
        return fail(QObject::tr("This version is not publicly available."));
    AddonVersion version;
    version.target = target;
    version.id = json.value("id").toString();
    version.projectId = json.value("project_id").toString();
    version.name = json.value("name").toString();
    version.number = json.value("version_number").toString();
    version.kind = kind;
    version.releaseType = json.value("version_type").toString();
    version.published = QDateTime::fromString(json.value("date_published").toString(), Qt::ISODateWithMs);
    QList<QJsonObject> files, primary;
    for (const auto& item : json.value("files").toArray()) {
        const auto file = item.toObject();
        if (safeFilename(file.value("filename").toString()) && file.value("file_type").toString().isEmpty()) {
            files.append(file);
            if (file.value("primary").toBool())
                primary.append(file);
        }
    }
    if (primary.size() == 1)
        files = primary;
    if (files.size() != 1)
        return fail(QObject::tr("The version has no unambiguous server JAR. Open its project page to check its files."));
    const auto file = files.first();
    version.filename = file.value("filename").toString();
    version.sha512 = file.value("hashes").toObject().value("sha512").toString().toLower();
    version.url = QUrl(file.value("url").toString());
    version.size = file.value("size").toInteger(-1);
    if (!validId(version.id) || !validId(version.projectId) || !validHash(version.sha512) || version.size <= 0 ||
        version.size > 512LL * 1024 * 1024 || version.url.scheme() != "https" || version.url.host() != "cdn.modrinth.com" ||
        !version.url.userInfo().isEmpty())
        return fail(QObject::tr("The version contains invalid download metadata or has no SHA-512 checksum."));
    for (const auto& item : json.value("dependencies").toArray()) {
        const auto dependency = item.toObject();
        version.dependencies.append({ dependency.value("project_id").toString(), dependency.value("version_id").toString(),
                                      dependency.value("dependency_type").toString() });
    }
    *result = version;
    return true;
}
QList<AddonVersion> parseVersions(const Target& target, Kind kind, const QJsonDocument& json, bool prereleases)
{
    QList<AddonVersion> result;
    for (const auto& value : json.array()) {
        const auto item = value.toObject();
        if (!prereleases && item.value("version_type") != "release")
            continue;
        AddonVersion version;
        QString error;
        if (parseVersion(target, kind, item, &version, &error))
            result.append(version);
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.published > b.published; });
    return result;
}
bool validateConstraints(const QList<Installed>& effective, const QList<Installed>& previous, QString* error)
{
    for (const auto& owner : effective) {
        for (const auto& dependency : owner.dependencies) {
            if (dependency.type == "incompatible") {
                for (const auto& item : effective)
                    if ((!dependency.projectId.isEmpty() || !dependency.versionId.isEmpty()) &&
                        (dependency.projectId.isEmpty() || dependency.projectId == item.projectId) &&
                        (dependency.versionId.isEmpty() || dependency.versionId == item.versionId)) {
                        *error = QObject::tr("%1 declares an incompatibility with %2.").arg(owner.name, item.name);
                        return false;
                    }
            }
            if (dependency.type != "required" || dependency.versionId.isEmpty())
                continue;
            auto requiredProject = dependency.projectId;
            if (requiredProject.isEmpty())
                for (const auto& item : previous)
                    if (item.versionId == dependency.versionId)
                        requiredProject = item.projectId;
            for (const auto& item : effective)
                if (!requiredProject.isEmpty() && item.projectId == requiredProject && item.versionId != dependency.versionId) {
                    *error =
                        QObject::tr("%1 requires a different version of %2. Update the dependent project first or choose another version.")
                            .arg(owner.name, item.name);
                    return false;
                }
        }
    }
    return true;
}
}  // namespace ServerContent
