// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "InstanceTask.h"

struct ServerOptions {
    QString jar;
    QString java;
    int minMemory = 1024;
    int maxMemory = 2048;
    QString arguments = "nogui";
    bool acceptEula = false;
};

class ServerCreationTask : public InstanceTask {
    Q_OBJECT
   public:
    explicit ServerCreationTask(ServerOptions options) : m_options(std::move(options)) {}
    void executeTask() override;

   private:
    ServerOptions m_options;
};
