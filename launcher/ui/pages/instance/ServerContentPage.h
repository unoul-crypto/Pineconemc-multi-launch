// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QWidget>
#include "server/content/ServerContent.h"
#include "ui/pages/BasePage.h"
class ServerInstance;
class ServerContentWidget;
class QNetworkAccessManager;

class ServerContentPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    ServerContentPage(ServerInstance* instance, ServerContent::Kind kind, QNetworkAccessManager* network, QWidget* parent = nullptr);
    QString id() const override { return m_kind == ServerContent::Kind::Mod ? "server-mods" : "server-plugins"; }
    QString displayName() const override { return m_kind == ServerContent::Kind::Mod ? tr("Mods") : tr("Plugins"); }
    QIcon icon() const override { return QIcon::fromTheme("loadermods"); }
    void openedImpl() override;
    bool apply() override;

   private:
    ServerContent::Kind m_kind;
    ServerContentWidget* m_content;
};
