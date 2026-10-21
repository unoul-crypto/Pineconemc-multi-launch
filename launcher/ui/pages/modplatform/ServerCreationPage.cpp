// SPDX-License-Identifier: GPL-3.0-only
#include "ServerCreationPage.h"
#include <QLabel>
#include <QVBoxLayout>
#include "Application.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/widgets/ServerOptionsWidget.h"

ServerCreationPage::ServerCreationPage(NewInstanceDialog* dialog) : QWidget(dialog), m_dialog(dialog)
{
    auto* layout = new QVBoxLayout(this);
    auto* text = new QLabel(tr("Create a server using your own runnable JAR. The JAR will be copied into a separate server folder. "
                               "Installers and start scripts are not supported in this version."),
                            this);
    text->setWordWrap(true);
    layout->addWidget(text);
    m_options = new ServerOptionsWidget(this);
    ServerOptions options;
    options.java = APPLICATION->settings()->get("JavaPath").toString();
    if (options.java.isEmpty())
        options.java = "java";
    m_options->setOptions(options);
    layout->addWidget(m_options);
    m_error = new QLabel(this);
    m_error->setWordWrap(true);
    layout->addWidget(m_error);
    layout->addStretch();
    connect(m_options, &ServerOptionsWidget::changed, this, &ServerCreationPage::suggest);
}
void ServerCreationPage::openedImpl()
{
    suggest();
}
void ServerCreationPage::suggest()
{
    if (!isOpened)
        return;
    const auto error = m_options->validationError();
    m_error->setText(error);
    m_dialog->setSuggestedPack(tr("Minecraft server"), error.isEmpty() ? new ServerCreationTask(m_options->options()) : nullptr);
}
