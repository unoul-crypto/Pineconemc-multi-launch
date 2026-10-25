// SPDX-License-Identifier: GPL-3.0-only
#include "ProjectIconCache.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPixmapCache>
#include <QSaveFile>
#include <memory>

namespace {
QIcon decode(const QByteArray& data)
{
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const auto size = reader.size();
    if (size.isEmpty() || size.width() > 4096 || size.height() > 4096)
        return {};
    reader.setScaledSize(size.scaled(64, 64, Qt::KeepAspectRatio));
    const auto image = reader.read();
    return image.isNull() ? QIcon() : QIcon(QPixmap::fromImage(image));
}
}  // namespace
ProjectIconCache::ProjectIconCache(QNetworkAccessManager* network, QObject* parent) : QObject(parent), m_network(network) {}
ProjectIconCache::~ProjectIconCache()
{
    for (auto reply : m_pending)
        if (reply) {
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
}
QString ProjectIconCache::cacheKey(const QUrl& url)
{
    return "logos/" + QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha1).toHex());
}
QIcon ProjectIconCache::icon(const QUrl& url, const QString& cacheFile)
{
    if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http") || url.host().isEmpty() || !url.userInfo().isEmpty())
        return {};
    QPixmap cached;
    if (QPixmapCache::find(url.toString(), &cached))
        return QIcon(cached);
    if (!cacheFile.isEmpty()) {
        QFile file(cacheFile);
        if (file.size() <= 5 * 1024 * 1024 && file.open(QIODevice::ReadOnly)) {
            const auto icon = decode(file.readAll());
            if (!icon.isNull()) {
                QPixmapCache::insert(url.toString(), icon.pixmap(64, 64));
                return icon;
            }
        }
    }
    if (!m_network || m_pending.contains(url) || m_failed.contains(url))
        return {};
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply = m_network->get(request);
    m_pending.insert(url, reply);
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes] {
        bytes->append(reply->readAll());
        if (bytes->size() > 5 * 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, cacheFile, bytes] {
        m_pending.remove(url);
        bytes->append(reply->readAll());
        const auto image = reply->error() == QNetworkReply::NoError &&
                                   reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
                                   bytes->size() <= 5 * 1024 * 1024
                               ? decode(*bytes)
                               : QIcon();
        if (image.isNull()) {
            m_failed.insert(url);
            return;
        }
        QPixmapCache::insert(url.toString(), image.pixmap(64, 64));
        if (!cacheFile.isEmpty() && QDir().mkpath(QFileInfo(cacheFile).absolutePath())) {
            QSaveFile file(cacheFile);
            if (file.open(QIODevice::WriteOnly) && file.write(*bytes) == bytes->size())
                file.commit();
        }
        emit ready(url);
    });
    return {};
}
