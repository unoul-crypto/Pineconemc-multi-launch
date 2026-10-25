// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QDialog>
#include "ui/widgets/ServerContentWidget.h"
class ServerContentDialog : public QDialog {
    Q_OBJECT
   public:
    ServerContentDialog(ServerInstance* instance, QNetworkAccessManager* network, QWidget* parent = nullptr);
    void reject() override;

   private:
    ServerContentWidget* m_content;
};
