// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <optional>

class ServerInstance;
namespace ServerContent {
enum class Kind { Mod, Plugin };
struct Target {
    QString minecraft;
    QString modLoader;
    QStringList pluginLoaders;
    bool operator==(const Target&) const = default;
    QStringList loaders(Kind kind) const;
    bool supports(Kind kind) const;
};
struct Project {
    QString id, title, description, slug;
};
struct Dependency {
    QString projectId, versionId, type;
};
struct AddonVersion {
    Target target;
    QString id, projectId, name, number, filename, sha512;
    QUrl url;
    qint64 size = -1;
    QDateTime published;
    QList<Dependency> dependencies;
    Kind kind = Kind::Mod;
};
struct Installed {
    QString projectId, versionId, name, filename, sha512;
    Kind kind = Kind::Mod;
    QString number;
    QList<Dependency> dependencies;
};
Target target(ServerInstance& instance);
QString folder(Kind kind);
bool validId(const QString& id);
bool safeFilename(const QString& filename);
bool validHash(const QString& sha512);
bool serverProject(const QJsonObject& project);
QUrl searchUrl(const Target& target, Kind kind, const QString& query, int offset = 0);
QUrl versionsUrl(const Target& target, Kind kind, const QString& project);
QUrl projectUrl(const QString& project);
QUrl versionUrl(const QString& version);
bool parseSearch(const Target& target, Kind kind, const QJsonDocument& json, QList<Project>* projects, int* total, QString* error);
bool parseVersion(const Target& target, Kind kind, const QJsonObject& json, AddonVersion* version, QString* error);
QList<AddonVersion> parseVersions(const Target& target, Kind kind, const QJsonDocument& json, bool prereleases);
bool validateConstraints(const QList<Installed>& effective, const QList<Installed>& previous, QString* error);
}  // namespace ServerContent
