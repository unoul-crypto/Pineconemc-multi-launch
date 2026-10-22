// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QMap>
#include <QWidget>
#include "server/ServerOptions.h"
#include "ui/pages/BasePage.h"
class ServerInstance;
class ServerOptionsWidget;
class QFormLayout;
class ServerSettingsPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit ServerSettingsPage(ServerInstance* instance, QWidget* parent = nullptr);
    QString id() const override { return "server-settings"; }
    QString displayName() const override { return tr("Server settings"); }
    QIcon icon() const override { return QIcon::fromTheme("settings"); }
    void openedImpl() override;
    bool apply() override;

   private:
    void load();
    QMap<QString, QString> propertyValues() const;
    ServerInstance* m_instance;
    ServerOptionsWidget* m_options;
    QWidget* m_editable;
    QMap<QString, QWidget*> m_properties;
    QMap<QString, QString> m_loaded;
    ServerOptions m_loadedOptions;
    bool m_loadedSuccessfully = false;
};
