// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QPointer>
#include <QWidget>
#include <functional>
#include <optional>
#include "server/ServerCatalog.h"
class QComboBox;
class QLabel;
class QPushButton;
class QNetworkAccessManager;
class ServerCatalogTask;

// Shared selection UI for new instances and software changes. Networking and
// provider parsing stay in ServerCatalogTask/ServerCatalog.
class ServerSoftwareWidget : public QWidget {
    Q_OBJECT
   public:
    explicit ServerSoftwareWidget(QNetworkAccessManager* network, QWidget* parent = nullptr);
    void setSelection(ServerCatalog::Core core, QString minecraft, QString build);
    void setActive(bool active);
    ServerCatalog::Core core() const;
    const std::optional<ServerCatalog::Download>& download() const { return m_download; }
   signals:
    void selectionChanged();

   private:
    void cancel();
    void loadVersions();
    void loadBuilds();
    void resolve();
    void start(ServerCatalogTask* task, std::function<void()> success);
    void resetSelection(const QString& status);
    QNetworkAccessManager* m_network;
    QComboBox* m_core;
    QComboBox* m_version;
    QComboBox* m_build;
    QPushButton* m_reload;
    QLabel* m_status;
    QPointer<ServerCatalogTask> m_task;
    QList<ServerCatalog::Version> m_versions;
    QList<ServerCatalog::Build> m_builds;
    std::optional<ServerCatalog::Download> m_download;
    ServerCatalog::Core m_initialCore = ServerCatalog::Core::Vanilla;
    QString m_preferredMinecraft;
    QString m_initialMinecraft;
    QString m_initialBuild;
    bool m_active = false;
};
