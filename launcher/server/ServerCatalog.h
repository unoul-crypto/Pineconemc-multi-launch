// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QList>
#include <QUrl>

namespace ServerCatalog {
enum class Core { Vanilla, Paper, Purpur, Fabric, Custom };
struct Version {
    QString id;
    QUrl metadataUrl;
};
struct Download {
    Core core = Core::Custom;
    QString minecraftVersion;
    QString build;
    QString loader;
    QString installer;
    QUrl url;
    QString checksum;
    QCryptographicHash::Algorithm hashAlgorithm = QCryptographicHash::Sha256;
    qint64 size = -1;
};
QString name(Core core);
QByteArray userAgent();
QUrl versionsUrl(Core core);
QUrl metadataUrl(Core core, const Version& version);
bool parseVersions(Core core, const QJsonDocument& json, QList<Version>* result, QString* error);
bool parseDownload(Core core, const Version& version, const QJsonDocument& json, Download* result, QString* error);
QString stableFabricVersion(const QJsonDocument& json, bool loader);
bool validDownload(const Download& download);
}  // namespace ServerCatalog
