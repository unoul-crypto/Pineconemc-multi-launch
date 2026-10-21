// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QWidget>
#include "ui/pages/BasePage.h"
class NewInstanceDialog;
class ServerOptionsWidget;
class QLabel;
class ServerCreationPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit ServerCreationPage(NewInstanceDialog* dialog);
    QString id() const override { return "server"; }
    QString displayName() const override { return tr("Server (JAR)"); }
    QIcon icon() const override { return QIcon::fromTheme("servers"); }
    void openedImpl() override;

   private:
    void suggest();
    NewInstanceDialog* m_dialog;
    ServerOptionsWidget* m_options;
    QLabel* m_error;
};
