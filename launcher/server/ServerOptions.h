// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <optional>
#include "ServerCatalog.h"

struct ServerOptions {
    QString jar;
    QString java;
    int minMemory = 1024;
    int maxMemory = 2048;
    QString arguments = "nogui";
    bool acceptEula = false;
    std::optional<ServerCatalog::Download> download;
};
