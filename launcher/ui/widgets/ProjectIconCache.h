// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QHash>
#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QUrl>
class QNetworkAccessManager;
class QNetworkReply;

// Shared by the client and server project lists. Invalid/oversized responses
// leave the placeholder intact; URL-keyed pixmaps and optional disk files are reused.
class ProjectIconCache : public QObject {
    Q_OBJECT
   public:
    explicit ProjectIconCache(QNetworkAccessManager* network, QObject* parent = nullptr);
    ~ProjectIconCache() override;
    QIcon icon(const QUrl& url, const QString& cacheFile = {});
    static QString cacheKey(const QUrl& url);
   signals:
    void ready(const QUrl& url);

   private:
    QPointer<QNetworkAccessManager> m_network;
    QHash<QUrl, QPointer<QNetworkReply>> m_pending;
    QSet<QUrl> m_failed;
};
