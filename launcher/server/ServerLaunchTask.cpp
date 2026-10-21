// SPDX-License-Identifier: GPL-3.0-only
#include "ServerLaunchTask.h"
#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include "ServerInstance.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

ServerLaunchTask::ServerLaunchTask(ServerInstance* instance) : LaunchTask(instance), m_server(instance)
{
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { readOutput(false); });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { readOutput(true); });
    connect(&m_process, &QProcess::started, this, [this] {
        setPid(m_process.processId());
        m_server->setMinecraftRunning(this, true);
        emit commandAvailabilityChanged();
        onLogLine(tr("Server process started (PID %1).").arg(pid()), MessageLevel::Launcher);
    });
    connect(&m_process, &QProcess::finished, this, &ServerLaunchTask::finish);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !m_finished) {
            m_finished = true;
            m_lock.reset();
            const auto message = tr("Could not start Java: %1").arg(m_process.errorString());
            onLogLine(message, MessageLevel::Error);
            emit commandAvailabilityChanged();
            emitFailed(message);
        }
    });
}
ServerLaunchTask::~ServerLaunchTask()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.disconnect(this);
        m_process.kill();
        m_process.waitForFinished(3000);
    }
}
void ServerLaunchTask::executeTask()
{
    state = LaunchTask::Running;
    const auto error = m_server->validateLaunch();
    if (!error.isEmpty()) {
        onLogLine(error, MessageLevel::Error);
        m_finished = true;
        emitFailed(error);
        return;
    }
    QDir().mkpath(m_server->gameRoot());
    m_lock = std::make_unique<QLockFile>(QDir(m_server->gameRoot()).filePath(".server-launch.lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock()) {
        m_finished = true;
        m_lock.reset();
        const auto message = tr("This server folder is already in use by another launcher, or its lock cannot be created.");
        onLogLine(message, MessageLevel::Error);
        emitFailed(message);
        return;
    }
    const auto logDir = QDir(m_server->gameRoot()).filePath("logs");
    QDir().mkpath(logDir);
    m_consoleLog.setFileName(
        QDir(logDir).filePath("launcher-console-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + ".log"));
    if (!m_consoleLog.open(QIODevice::WriteOnly)) {
        m_finished = true;
        m_lock.reset();
        const auto message = tr("Could not create the server console log: %1").arg(m_consoleLog.errorString());
        onLogLine(message, MessageLevel::Error);
        emitFailed(message);
        return;
    }
    m_process.setProgram(m_server->javaPath());
    m_process.setArguments(m_server->launchArguments());
    m_process.setWorkingDirectory(m_server->gameRoot());
    m_process.setProcessEnvironment(m_server->createLaunchEnvironment());
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    onLogLine(tr("Starting server with %1").arg(m_server->javaPath()), MessageLevel::Launcher);
    m_process.start();
}
void ServerLaunchTask::appendOutput(const QByteArray& line, MessageLevel level)
{
    auto text = QString::fromUtf8(line);
    if (text.endsWith('\r'))
        text.chop(1);
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-?]*[ -/]*[@-~]"));
    text.remove(ansi);
    onLogLine(text, level);
    m_consoleLog.write(text.toUtf8() + '\n');
}
void ServerLaunchTask::readOutput(bool stderrStream, bool flush)
{
    auto& buffer = stderrStream ? m_stderr : m_stdout;
    buffer += stderrStream ? m_process.readAllStandardError() : m_process.readAllStandardOutput();
    const auto level = stderrStream ? MessageLevel::StdErr : MessageLevel::StdOut;
    qsizetype newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        appendOutput(buffer.left(newline), level);
        buffer.remove(0, newline + 1);
    }
    if ((flush || buffer.size() > 65536) && !buffer.isEmpty()) {
        appendOutput(buffer, level);
        buffer.clear();
    }
    m_consoleLog.flush();
}
bool ServerLaunchTask::acceptsCommands() const
{
    return m_process.state() == QProcess::Running && !m_stopping && !m_finished;
}
bool ServerLaunchTask::sendCommand(const QString& command)
{
    if (!acceptsCommands() || command.contains('\n') || command.contains('\r') || command.contains(QChar::Null))
        return false;
    auto text = command.trimmed();
    if (text.startsWith('/'))
        text.remove(0, 1);
    if (text.isEmpty())
        return false;
    const auto data = text.toUtf8() + '\n';
    if (m_process.write(data) != data.size())
        return false;
    if (text.compare("stop", Qt::CaseInsensitive) == 0) {
        m_stopping = true;
        onLogLine(tr("Stop requested. Waiting for the server to save and exit…"), MessageLevel::Launcher);
        emit commandAvailabilityChanged();
    }
    return true;
}
bool ServerLaunchTask::canAbort() const
{
    return !m_finished;
}
bool ServerLaunchTask::abort()
{
    if (m_finished)
        return true;
    m_killed = true;
    if (m_process.state() == QProcess::NotRunning) {
        m_finished = true;
        m_lock.reset();
        emitAborted();
    } else
        m_process.kill();
    return true;
}
void ServerLaunchTask::finish(int code, QProcess::ExitStatus status)
{
    if (m_finished)
        return;
    m_finished = true;
    readOutput(false, true);
    readOutput(true, true);
    m_consoleLog.close();
    m_lock.reset();
    emit commandAvailabilityChanged();
    onLogLine(tr("Server process exited with code %1.").arg(code), MessageLevel::Launcher);
    if (m_killed)
        emitAborted();
    else if (status == QProcess::NormalExit && code == 0)
        emitSucceeded();
    else
        emitFailed(tr("Server stopped unexpectedly (exit code %1). See the console log for details.").arg(code));
}
