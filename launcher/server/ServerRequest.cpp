// SPDX-License-Identifier: GPL-3.0-only
#include "ServerRequest.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "ServerCatalog.h"

ServerRequest::ServerRequest(QNetworkAccessManager* network,
                             QUrl url,
                             QString destination,
                             QString checksum,
                             QCryptographicHash::Algorithm algorithm,
                             qint64 size)
    : m_network(network)
    , m_url(std::move(url))
    , m_destination(std::move(destination))
    , m_checksum(std::move(checksum))
    , m_hash(algorithm)
    , m_expectedSize(size)
{}
ServerRequest::~ServerRequest()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}
void ServerRequest::executeTask()
{
    if (!m_network) {
        fail(tr("Network access is unavailable."));
        return;
    }
    if (!m_destination.isEmpty()) {
        m_file = std::make_unique<QSaveFile>(m_destination);
        if (!m_file->open(QIODevice::WriteOnly)) {
            fail(m_file->errorString());
            return;
        }
    }
    QNetworkRequest request(m_url);
    request.setRawHeader("User-Agent", ServerCatalog::userAgent());
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &ServerRequest::read);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &Task::setProgress);
    connect(m_reply, &QNetworkReply::finished, this, &ServerRequest::complete);
}
void ServerRequest::read()
{
    if (!isRunning() || !m_reply)
        return;
    const auto data = m_reply->readAll();
    m_received += data.size();
    const qint64 limit = m_destination.isEmpty() ? 16 * 1024 * 1024 : m_expectedSize >= 0 ? m_expectedSize : 512LL * 1024 * 1024;
    if (m_received > limit) {
        fail(tr("The server response exceeded the expected size."));
        return;
    }
    if (m_file) {
        m_hash.addData(data);
        if (m_prefix.size() < 4)
            m_prefix += data.left(4 - m_prefix.size());
        if (m_file->write(data) != data.size()) {
            fail(m_file->errorString());
            return;
        }
    } else
        m_response += data;
}
void ServerRequest::complete()
{
    if (!isRunning() || !m_reply)
        return;
    read();
    if (!isRunning())
        return;
    const auto status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (m_reply->error() != QNetworkReply::NoError || status != 200) {
        fail(tr("Could not download from %1 (HTTP %2): %3").arg(m_url.host()).arg(status).arg(m_reply->errorString()));
        return;
    }
    if (m_file) {
        if ((m_expectedSize >= 0 && m_received != m_expectedSize) || !m_prefix.startsWith("PK")) {
            fail(tr("The downloaded server JAR is incomplete or is not an archive."));
            return;
        }
        if (!m_checksum.isEmpty() && QString::fromLatin1(m_hash.result().toHex()).compare(m_checksum, Qt::CaseInsensitive) != 0) {
            fail(tr("The server JAR checksum does not match the official metadata. Please retry."));
            return;
        }
        if (!m_file->commit()) {
            fail(m_file->errorString());
            return;
        }
        m_file.reset();
    }
    m_reply->deleteLater();
    m_reply = nullptr;
    emitSucceeded();
}
void ServerRequest::fail(const QString& message)
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_file.reset();
    emitFailed(message);
}
bool ServerRequest::abort()
{
    if (isFinished())
        return true;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_file.reset();
    emitAborted();
    return true;
}
