// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ServerCatalog.h"

// Provider-specific metadata parsing stays independent of networking and widgets.
namespace ServerHybridCatalog {
bool handles(ServerCatalog::Core core);
QUrl versionsUrl(ServerCatalog::Core core);
QUrl metadataUrl(ServerCatalog::Core core, const ServerCatalog::Version& version, const QString& build = {});
QList<ServerCatalog::Version> versions(ServerCatalog::Core core, const QJsonDocument& json);
QList<ServerCatalog::Build> builds(ServerCatalog::Core core, const ServerCatalog::Version& version, const QJsonDocument& json);
ServerCatalog::Download download(ServerCatalog::Core core,
                                 const ServerCatalog::Version& version,
                                 const QJsonDocument& json,
                                 const QString& build);
}  // namespace ServerHybridCatalog
