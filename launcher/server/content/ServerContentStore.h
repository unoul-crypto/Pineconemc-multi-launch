// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ServerContent.h"

// Filesystem-only operations. Call mutations while holding the server folder lock.
namespace ServerContentStore {
struct Result {
    bool success = false;
    QString error, backupPath;
};
QString manifestPath(const QString& root);
bool interrupted(const QString& root);
bool read(const QString& root, QList<ServerContent::Installed>* installed, QString* error);
Result recover(const QString& root);
Result install(const QString& root, const QList<ServerContent::AddonVersion>& plan, const QStringList& staged);
}  // namespace ServerContentStore
