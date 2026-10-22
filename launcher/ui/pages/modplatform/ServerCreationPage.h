// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QPointer>
#include <QWidget>
#include <optional>
#include "server/ServerCatalog.h"
#include "ui/pages/BasePage.h"
class NewInstanceDialog;
class ServerOptionsWidget;
class QLabel;
class QComboBox;
class QPushButton;
class ServerCatalogTask;
class ServerCreationPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit ServerCreationPage(NewInstanceDialog* dialog);
    QString id() const override { return "server"; }
    QString displayName() const override { return tr("Server"); }
    QIcon icon() const override { return QIcon::fromTheme("servers"); }
    void openedImpl() override;
    void closedImpl() override;

   private:
    void suggest();
    void loadVersions();
    void resolveVersion();
    void cancelRequest();
    ServerCatalog::Core core() const;
    NewInstanceDialog* m_dialog;
    ServerOptionsWidget* m_options;
    QLabel* m_error;
    QComboBox* m_core;
    QComboBox* m_version;
    QPushButton* m_reload;
    QLabel* m_info;
    QPointer<ServerCatalogTask> m_catalogTask;
    QList<ServerCatalog::Version> m_versions;
    std::optional<ServerCatalog::Download> m_download;
};
