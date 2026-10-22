// SPDX-License-Identifier: GPL-3.0-only
#include "ServerCreationPage.h"
#include <QLabel>
#include <QVBoxLayout>
#include "Application.h"
#include "server/ServerCreationTask.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/widgets/ServerOptionsWidget.h"
#include "ui/widgets/ServerSoftwareWidget.h"

ServerCreationPage::ServerCreationPage(NewInstanceDialog* dialog) : QWidget(dialog), m_dialog(dialog)
{
    auto* layout = new QVBoxLayout(this);
    m_software = new ServerSoftwareWidget(APPLICATION->network(), this);
    layout->addWidget(m_software);
    m_options = new ServerOptionsWidget(this);
    ServerOptions options;
    options.java = APPLICATION->settings()->get("JavaPath").toString();
    if (options.java.isEmpty())
        options.java = "java";
    m_options->setOptions(options);
    m_options->setLocalJarRequired(false);
    layout->addWidget(m_options);
    m_error = new QLabel(this);
    m_error->setWordWrap(true);
    layout->addWidget(m_error);
    layout->addStretch();
    connect(m_options, &ServerOptionsWidget::changed, this, &ServerCreationPage::suggest);
    connect(m_software, &ServerSoftwareWidget::selectionChanged, this, [this] {
        m_options->setLocalJarRequired(m_software->core() == ServerCatalog::Core::Custom);
        suggest();
    });
}
void ServerCreationPage::openedImpl()
{
    m_software->setActive(true);
}
void ServerCreationPage::closedImpl()
{
    m_software->setActive(false);
}
void ServerCreationPage::suggest()
{
    if (!isOpened)
        return;
    const auto error = m_options->validationError();
    m_error->setText(error);
    auto options = m_options->options();
    options.download = m_software->download();
    const bool ready = error.isEmpty() && (m_software->core() == ServerCatalog::Core::Custom || options.download.has_value());
    auto name = tr("Minecraft server");
    if (options.download)
        name = ServerCatalog::name(options.download->core) + " " + options.download->minecraftVersion;
    m_dialog->setSuggestedPack(name, ready ? new ServerCreationTask(options, APPLICATION->network()) : nullptr);
}