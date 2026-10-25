// SPDX-License-Identifier: GPL-3.0-only
#include "ServerContentDialog.h"
#include <QPushButton>
#include <QVBoxLayout>
ServerContentDialog::ServerContentDialog(ServerInstance* instance, QNetworkAccessManager* network, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Server mods and plugins — Modrinth"));
    setWindowModality(Qt::WindowModal);
    resize(1000, 750);
    auto* layout = new QVBoxLayout(this);
    m_content = new ServerContentWidget(instance, network, this);
    layout->addWidget(m_content);
    auto* close = new QPushButton(tr("Close"), this);
    layout->addWidget(close, 0, Qt::AlignRight);
    connect(close, &QPushButton::clicked, this, &ServerContentDialog::reject);
}
void ServerContentDialog::reject()
{
    if (m_content->prepareClose())
        QDialog::reject();
}
