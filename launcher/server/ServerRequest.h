// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QCryptographicHash>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>
#include <memory>
#include "tasks/Task.h"
class QNetworkAccessManager;
class QNetworkReply;

// Shared, cancellable HTTP transport for metadata and streamed JAR downloads.
// Nothing is committed to disk until size, checksum and archive checks pass.
class ServerRequest : public Task {
    Q_OBJECT
   public:
    ServerRequest(QNetworkAccessManager* network,
                  QUrl url,
                  QString destination = {},
                  QString checksum = {},
                  QCryptographicHash::Algorithm algorithm = QCryptographicHash::Sha256,
                  qint64 size = -1);
    ~ServerRequest() override;
    bool canAbort() const override { return true; }
    bool abort() override;
    const QByteArray& response() const { return m_response; }

   protected:
    void executeTask() override;

   private:
    void read();
    void complete();
    void fail(const QString& message);
    QNetworkAccessManager* m_network;
    QUrl m_url;
    QString m_destination;
    QString m_checksum;
    QCryptographicHash m_hash;
    qint64 m_expectedSize;
    qint64 m_received = 0;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QSaveFile> m_file;
    QByteArray m_response;
    QByteArray m_prefix;
};
