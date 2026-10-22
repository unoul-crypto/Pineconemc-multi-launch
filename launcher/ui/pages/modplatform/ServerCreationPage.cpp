// SPDX-License-Identifier: GPL-3.0-only
#include "ServerCreationPage.h"
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include "Application.h"
#include "server/ServerCatalogTask.h"
#include "server/ServerCreationTask.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui/widgets/ServerOptionsWidget.h"

ServerCreationPage::ServerCreationPage(NewInstanceDialog* dialog) : QWidget(dialog), m_dialog(dialog)
{
    auto* layout = new QVBoxLayout(this);
    auto* text = new QLabel(tr("Choose server software and a Minecraft release. The server JAR is downloaded automatically from its "
                               "official service. You can also use your own JAR."),
                            this);
    text->setWordWrap(true);
    layout->addWidget(text);
    auto* form = new QFormLayout();
    m_core = new QComboBox(this);
    m_core->setObjectName("serverSoftware");
    for (const auto core : { ServerCatalog::Core::Vanilla, ServerCatalog::Core::Paper, ServerCatalog::Core::Purpur,
                             ServerCatalog::Core::Fabric, ServerCatalog::Core::Custom })
        m_core->addItem(ServerCatalog::name(core), static_cast<int>(core));
    form->addRow(tr("Server software:"), m_core);
    auto* versionRow = new QWidget(this);
    auto* horizontal = new QHBoxLayout(versionRow);
    horizontal->setContentsMargins(0, 0, 0, 0);
    m_version = new QComboBox(versionRow);
    m_version->setObjectName("serverMinecraftVersion");
    m_version->setMaxVisibleItems(15);
    m_version->setStyleSheet("combobox-popup: 0;");
    m_version->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_reload = new QPushButton(tr("Reload"), versionRow);
    horizontal->addWidget(m_version, 1);
    horizontal->addWidget(m_reload);
    form->addRow(tr("Minecraft version:"), versionRow);
    layout->addLayout(form);
    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    layout->addWidget(m_info);
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
    connect(m_core, &QComboBox::currentIndexChanged, this, &ServerCreationPage::loadVersions);
    connect(m_version, &QComboBox::currentIndexChanged, this, &ServerCreationPage::resolveVersion);
    connect(m_reload, &QPushButton::clicked, this, &ServerCreationPage::loadVersions);
}
ServerCatalog::Core ServerCreationPage::core() const
{
    return static_cast<ServerCatalog::Core>(m_core->currentData().toInt());
}
void ServerCreationPage::cancelRequest()
{
    auto* task = m_catalogTask.data();
    m_catalogTask = nullptr;
    if (task)
        task->abort();
}
void ServerCreationPage::openedImpl()
{
    loadVersions();
}
void ServerCreationPage::closedImpl()
{
    cancelRequest();
}
void ServerCreationPage::loadVersions()
{
    if (!isOpened)
        return;
    cancelRequest();
    m_download.reset();
    m_versions.clear();
    {
        QSignalBlocker blocker(m_version);
        m_version->clear();
    }
    const bool custom = core() == ServerCatalog::Core::Custom;
    m_version->setEnabled(!custom);
    m_reload->setEnabled(!custom);
    m_options->setLocalJarRequired(custom);
    m_info->setText(custom ? tr("Choose a runnable server JAR. Installers and start scripts are not supported.")
                           : tr("Loading Minecraft versions…"));
    m_error->clear();
    suggest();
    if (custom)
        return;
    auto* task = new ServerCatalogTask(APPLICATION->network(), core(), ServerCatalogTask::Operation::Versions);
    task->setParent(this);
    m_catalogTask = task;
    connect(task, &Task::finished, this, [this, task] {
        task->deleteLater();
        if (m_catalogTask != task || !isOpened)
            return;
        m_catalogTask = nullptr;
        if (!task->wasSuccessful()) {
            m_info->setText(task->failReason());
            suggest();
            return;
        }
        m_versions = task->versions();
        {
            QSignalBlocker blocker(m_version);
            for (const auto& version : m_versions)
                m_version->addItem(version.id);
        }
        resolveVersion();
    });
    task->start();
}
void ServerCreationPage::resolveVersion()
{
    if (!isOpened || core() == ServerCatalog::Core::Custom)
        return;
    cancelRequest();
    m_download.reset();
    m_info->setText(tr("Finding a stable server build…"));
    suggest();
    const auto index = m_version->currentIndex();
    if (index < 0 || index >= m_versions.size())
        return;
    auto* task = new ServerCatalogTask(APPLICATION->network(), core(), ServerCatalogTask::Operation::Resolve, m_versions[index]);
    task->setParent(this);
    m_catalogTask = task;
    connect(task, &Task::finished, this, [this, task] {
        task->deleteLater();
        if (m_catalogTask != task || !isOpened)
            return;
        m_catalogTask = nullptr;
        if (!task->wasSuccessful()) {
            m_info->setText(task->failReason());
            suggest();
            return;
        }
        m_download = task->download();
        const auto& download = *m_download;
        auto info = tr("%1 %2").arg(ServerCatalog::name(download.core), download.minecraftVersion);
        if (!download.build.isEmpty())
            info += tr(" · build %1").arg(download.build);
        if (!download.loader.isEmpty())
            info += tr(" · loader %1").arg(download.loader);
        if (download.size > 0)
            info += tr(" · %1 MiB").arg(download.size / (1024.0 * 1024.0), 0, 'f', 1);
        info += tr("\nChoose Java compatible with this Minecraft version. The first start may download additional server libraries.");
        m_info->setText(info);
        suggest();
    });
    task->start();
}
void ServerCreationPage::suggest()
{
    if (!isOpened)
        return;
    const auto error = m_options->validationError();
    m_error->setText(error);
    auto options = m_options->options();
    options.download = m_download;
    const bool ready = error.isEmpty() && (core() == ServerCatalog::Core::Custom || m_download.has_value());
    auto name = tr("Minecraft server");
    if (m_download)
        name = ServerCatalog::name(core()) + " " + m_download->minecraftVersion;
    m_dialog->setSuggestedPack(name, ready ? new ServerCreationTask(options, APPLICATION->network()) : nullptr);
}
