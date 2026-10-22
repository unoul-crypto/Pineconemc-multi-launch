// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <cstring>
#include "settings/INISettingsObject.h"
namespace ServerTest {
struct Response {
    QByteArray body;
    int status = 200;
    int delay = 0;
};
class Reply : public QNetworkReply {
   public:
    Reply(const QNetworkRequest& request, Response response, QObject* parent) : QNetworkReply(parent), m_body(response.body)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(response.delay, this, [this, response] {
            if (isFinished())
                return;
            if (response.status != 200)
                setError(QNetworkReply::ContentNotFoundError, "fixture HTTP error");
            setFinished(true);
            emit readyRead();
            emit downloadProgress(m_body.size(), m_body.size());
            emit finished();
        });
    }
    void abort() override
    {
        if (isFinished())
            return;
        setFinished(true);
        setError(QNetworkReply::OperationCanceledError, "cancelled");
        QTimer::singleShot(0, this, [this] { emit finished(); });
    }
    qint64 bytesAvailable() const override { return m_body.size() - m_offset + QNetworkReply::bytesAvailable(); }

   protected:
    qint64 readData(char* data, qint64 max) override
    {
        const auto size = qMin(max, static_cast<qint64>(m_body.size() - m_offset));
        if (!size)
            return -1;
        std::memcpy(data, m_body.constData() + m_offset, static_cast<size_t>(size));
        m_offset += size;
        return size;
    }

   private:
    QByteArray m_body;
    qint64 m_offset = 0;
};
class Network : public QNetworkAccessManager {
   public:
    QHash<QString, Response> routes;
    QList<QNetworkRequest> requests;

   protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
    {
        requests.append(request);
        return new Reply(request, routes.value(request.url().toString(), { "not found", 404, 0 }), this);
    }
};
QJsonDocument json(const QByteArray& bytes)
{
    return QJsonDocument::fromJson(bytes);
}
QString hash(const QByteArray& data, QCryptographicHash::Algorithm algorithm)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, algorithm).toHex());
}
std::unique_ptr<INISettingsObject> global(const QString& path)
{
    auto settings = std::make_unique<INISettingsObject>(path);
    for (const auto& key :
         { "ShowGameTime", "RecordGameTime", "ShowConsole", "AutoCloseConsole", "ShowConsoleOnError", "LogPrePostOutput" })
        settings->registerSetting(key, false);
    for (const auto& key : { "PreLaunchCommand", "WrapperCommand", "PostExitCommand", "JavaPath" })
        settings->registerSetting(key, QString());
    settings->registerSetting("ConsoleMaxLines", 1000);
    settings->registerSetting("ConsoleOverflowStop", false);
    return settings;
}
}  // namespace ServerTest