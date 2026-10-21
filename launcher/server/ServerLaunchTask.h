// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QFile>
#include <QLockFile>
#include <memory>
#include "launch/LaunchTask.h"
class ServerInstance;
class ServerLaunchTask : public LaunchTask {
    Q_OBJECT
   public:
    explicit ServerLaunchTask(ServerInstance* instance);
    ~ServerLaunchTask() override;
    void executeTask() override;
    bool canAbort() const override;
    bool abort() override;
    bool acceptsCommands() const override;
    bool sendCommand(const QString& command) override;

   private:
    void readOutput(bool stderrStream, bool flush = false);
    void finish(int code, QProcess::ExitStatus status);
    void appendOutput(const QByteArray& line, MessageLevel level);
    ServerInstance* m_server;
    QProcess m_process;
    std::unique_ptr<QLockFile> m_lock;
    QFile m_consoleLog;
    QByteArray m_stdout;
    QByteArray m_stderr;
    bool m_killed = false;
    bool m_stopping = false;
    bool m_finished = false;
};
