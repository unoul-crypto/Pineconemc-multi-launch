// SPDX-License-Identifier: GPL-3.0-only
#include "ServerContentPage.h"
#include <QVBoxLayout>
#include "ui/widgets/ServerContentWidget.h"

ServerContentPage::ServerContentPage(ServerInstance* instance, ServerContent::Kind kind, QNetworkAccessManager* network, QWidget* parent)
    : QWidget(parent), m_kind(kind)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_content = new ServerContentWidget(instance, network, this, kind);
    layout->addWidget(m_content);
}
void ServerContentPage::openedImpl()
{
    m_content->refresh();
}
bool ServerContentPage::apply()
{
    return m_content->canClose();
}
