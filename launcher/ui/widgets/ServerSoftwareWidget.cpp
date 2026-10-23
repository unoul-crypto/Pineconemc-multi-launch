// SPDX-License-Identifier: GPL-3.0-only
#include "ServerSoftwareWidget.h"
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include "server/ServerCatalogTask.h"

ServerSoftwareWidget::ServerSoftwareWidget(QNetworkAccessManager* network, QWidget* parent) : QWidget(parent), m_network(network)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* form = new QFormLayout();
    m_core = new QComboBox(this);
    m_core->setObjectName("serverSoftware");
    for (const auto value : ServerCatalog::cores())
        m_core->addItem(ServerCatalog::name(value), static_cast<int>(value));
    form->addRow(tr("Server software:"), m_core);
    auto* row = new QWidget(this);
    auto* horizontal = new QHBoxLayout(row);
    horizontal->setContentsMargins(0, 0, 0, 0);
    m_version = new QComboBox(row);
    m_version->setObjectName("serverMinecraftVersion");
    m_reload = new QPushButton(tr("Reload"), row);
    horizontal->addWidget(m_version, 1);
    horizontal->addWidget(m_reload);
    form->addRow(tr("Minecraft version:"), row);
    m_build = new QComboBox(this);
    m_build->setObjectName("serverCoreVersion");
    form->addRow(tr("Software version / build:"), m_build);
    for (auto* combo : { m_version, m_build }) {
        combo->setMaxVisibleItems(15);
        combo->setStyleSheet("combobox-popup: 0;");
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    }
    layout->addLayout(form);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    connect(m_core, &QComboBox::currentIndexChanged, this, &ServerSoftwareWidget::loadVersions);
    connect(m_version, &QComboBox::currentIndexChanged, this, &ServerSoftwareWidget::loadBuilds);
    connect(m_build, &QComboBox::currentIndexChanged, this, &ServerSoftwareWidget::resolve);
    connect(m_reload, &QPushButton::clicked, this, &ServerSoftwareWidget::loadVersions);
}
ServerCatalog::Core ServerSoftwareWidget::core() const
{
    return static_cast<ServerCatalog::Core>(m_core->currentData().toInt());
}
void ServerSoftwareWidget::setSelection(ServerCatalog::Core value, QString minecraft, QString build)
{
    m_initialCore = value;
    m_initialMinecraft = minecraft;
    m_preferredMinecraft = std::move(minecraft);
    m_initialBuild = std::move(build);
    {
        QSignalBlocker blocker(m_core);
        m_core->setCurrentIndex(m_core->findData(static_cast<int>(value)));
    }
    {
        QSignalBlocker blocker(m_version);
        m_version->clear();
    }
    if (m_active)
        loadVersions();
}
void ServerSoftwareWidget::setActive(bool active)
{
    m_active = active;
    if (active)
        loadVersions();
    else
        cancel();
}
void ServerSoftwareWidget::cancel()
{
    auto* task = m_task.data();
    m_task = nullptr;
    if (task)
        task->abort();
}
void ServerSoftwareWidget::resetSelection(const QString& status)
{
    cancel();
    m_download.reset();
    m_status->setText(status);
    emit selectionChanged();
}
void ServerSoftwareWidget::start(ServerCatalogTask* task, std::function<void()> success)
{
    task->setParent(this);
    m_task = task;
    connect(task, &Task::finished, this, [this, task, success = std::move(success)] {
        task->deleteLater();
        if (m_task != task || !m_active)
            return;
        m_task = nullptr;
        if (!task->wasSuccessful()) {
            m_status->setText(task->failReason());
            emit selectionChanged();
            return;
        }
        success();
    });
    task->start();
}
void ServerSoftwareWidget::loadVersions()
{
    if (!m_active)
        return;
    if (!m_version->currentText().isEmpty())
        m_preferredMinecraft = m_version->currentText();
    m_versions.clear();
    m_builds.clear();
    {
        QSignalBlocker blocker(m_version);
        m_version->clear();
    }
    {
        QSignalBlocker blocker(m_build);
        m_build->clear();
    }
    const bool custom = core() == ServerCatalog::Core::Custom;
    m_version->setEnabled(!custom);
    m_build->setEnabled(!custom);
    m_reload->setEnabled(!custom);
    resetSelection(custom ? tr("Choose your own runnable server JAR.") : tr("Loading Minecraft versions…"));
    if (custom)
        return;
    auto* task = new ServerCatalogTask(m_network, core(), ServerCatalogTask::Operation::Versions);
    start(task, [this, task] {
        m_versions = task->versions();
        {
            QSignalBlocker blocker(m_version);
            for (const auto& version : m_versions)
                m_version->addItem(version.id);
            const auto index = m_version->findText(m_preferredMinecraft);
            if (index >= 0)
                m_version->setCurrentIndex(index);
        }
        loadBuilds();
    });
}
void ServerSoftwareWidget::loadBuilds()
{
    if (!m_active || core() == ServerCatalog::Core::Custom)
        return;
    m_builds.clear();
    {
        QSignalBlocker blocker(m_build);
        m_build->clear();
    }
    resetSelection(tr("Loading software versions…"));
    const auto index = m_version->currentIndex();
    if (index < 0 || index >= m_versions.size())
        return;
    auto* task = new ServerCatalogTask(m_network, core(), ServerCatalogTask::Operation::Builds, m_versions[index]);
    start(task, [this, task] {
        m_builds = task->builds();
        {
            QSignalBlocker blocker(m_build);
            int selected = -1;
            for (const auto& build : m_builds) {
                m_build->addItem(build.id + (core() == ServerCatalog::Core::Fabric && build.recommended ? tr(" (recommended)") : QString()),
                                 build.id);
                if (selected < 0 && build.recommended)
                    selected = m_build->count() - 1;
            }
            if (selected >= 0)
                m_build->setCurrentIndex(selected);
            if (core() == m_initialCore && m_version->currentText() == m_initialMinecraft) {
                const auto installed = m_build->findData(m_initialBuild);
                if (installed >= 0)
                    m_build->setCurrentIndex(installed);
            }
        }
        resolve();
    });
}
void ServerSoftwareWidget::resolve()
{
    if (!m_active || core() == ServerCatalog::Core::Custom)
        return;
    resetSelection(tr("Checking the selected server build…"));
    const auto index = m_version->currentIndex();
    if (index < 0 || index >= m_versions.size() || m_build->currentIndex() < 0)
        return;
    auto* task = new ServerCatalogTask(m_network, core(), ServerCatalogTask::Operation::Resolve, m_versions[index],
                                       m_build->currentData().toString());
    start(task, [this, task] {
        m_download = task->download();
        const auto& download = *m_download;
        auto info = ServerCatalog::name(core()) + " " + download.minecraftVersion;
        if (!download.build.isEmpty())
            info += tr(" · build %1").arg(download.build);
        if (!download.loader.isEmpty())
            info += tr(" · loader %1").arg(download.loader);
        if (download.size > 0)
            info += tr(" · %1 MiB").arg(download.size / (1024.0 * 1024.0), 0, 'f', 1);
        if (core() == ServerCatalog::Core::Fabric && !m_builds.value(m_build->currentIndex()).recommended)
            info += tr("\nThis loader is not currently recommended by Fabric. Check compatibility with your mods.");
        if (core() == ServerCatalog::Core::Arclight)
            info +=
                tr("\nThe software version includes its mod loader: Forge, NeoForge or Fabric. Choose the variant required by your mods.");
        info += tr("\nChoose compatible Java. The first start may download additional libraries.");
        m_status->setText(info);
        emit selectionChanged();
    });
}
