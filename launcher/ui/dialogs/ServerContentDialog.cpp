// SPDX-License-Identifier: GPL-3.0-only
#include "ServerContentDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "server/ServerInstance.h"
#include "server/content/ServerContentInstallTask.h"
#include "server/content/ServerContentTask.h"

using namespace ServerContent;
ServerContentDialog::ServerContentDialog(ServerInstance* instance, QNetworkAccessManager* network, QWidget* parent)
    : QDialog(parent), m_instance(instance), m_network(network)
{
    setWindowTitle(tr("Server mods and plugins — Modrinth"));
    setWindowModality(Qt::WindowModal);
    resize(900, 700);
    auto* layout = new QVBoxLayout(this);
    m_profile = new QLabel(this);
    m_profile->setTextFormat(Qt::PlainText);
    m_profile->setWordWrap(true);
    layout->addWidget(m_profile);
    auto* form = new QFormLayout;
    m_minecraft = new QLineEdit(this);
    m_minecraft->setObjectName("contentMinecraft");
    form->addRow(tr("Minecraft version:"), m_minecraft);
    m_loader = new QComboBox(this);
    m_loader->setObjectName("contentModLoader");
    m_loader->addItem(tr("No mods"), "");
    for (const auto& loader : { QString("fabric"), QString("forge"), QString("neoforge") })
        m_loader->addItem(loader, loader);
    form->addRow(tr("Mod loader:"), m_loader);
    m_pluginApi = new QComboBox(this);
    m_pluginApi->setObjectName("contentPluginApi");
    m_pluginApi->addItem(tr("No plugins"), "");
    m_pluginApi->addItem("Bukkit / Spigot", "spigot");
    m_pluginApi->addItem("Paper", "paper");
    m_pluginApi->addItem("Purpur", "purpur");
    form->addRow(tr("Plugin API:"), m_pluginApi);
    layout->addLayout(form);
    auto* tabs = new QTabWidget(this);
    auto* browse = new QWidget(tabs);
    auto* browseLayout = new QVBoxLayout(browse);
    auto* searchRow = new QHBoxLayout;
    m_kind = new QComboBox(browse);
    m_kind->setObjectName("serverContentKind");
    searchRow->addWidget(m_kind);
    m_query = new QLineEdit(browse);
    m_query->setObjectName("serverContentQuery");
    m_query->setPlaceholderText(tr("Search Modrinth…"));
    searchRow->addWidget(m_query, 1);
    m_search = new QPushButton(tr("Search"), browse);
    m_search->setObjectName("serverContentSearch");
    searchRow->addWidget(m_search);
    browseLayout->addLayout(searchRow);
    auto* splitter = new QSplitter(browse);
    m_results = new QListWidget(splitter);
    m_results->setObjectName("serverContentResults");
    m_results->setMinimumWidth(230);
    m_details = new QTextBrowser(splitter);
    m_details->setOpenExternalLinks(false);
    browseLayout->addWidget(splitter, 1);
    auto* versionsRow = new QHBoxLayout;
    m_version = new QComboBox(browse);
    m_version->setObjectName("serverContentVersion");
    m_version->setMaxVisibleItems(15);
    m_version->setStyleSheet("combobox-popup: 0;");
    m_version->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    versionsRow->addWidget(new QLabel(tr("Version:"), browse));
    versionsRow->addWidget(m_version, 1);
    m_prereleases = new QCheckBox(tr("Include alpha / beta versions"), browse);
    versionsRow->addWidget(m_prereleases);
    browseLayout->addLayout(versionsRow);
    auto* actions = new QHBoxLayout;
    m_previous = new QPushButton(tr("Previous"), browse);
    m_next = new QPushButton(tr("Next"), browse);
    m_website = new QPushButton(tr("Project page"), browse);
    m_install = new QPushButton(tr("Install selected version…"), browse);
    m_install->setObjectName("serverContentInstall");
    for (auto* button : { m_previous, m_next, m_website })
        actions->addWidget(button);
    actions->addStretch();
    actions->addWidget(m_install);
    browseLayout->addLayout(actions);
    tabs->addTab(browse, tr("Search and install"));
    auto* installed = new QWidget(tabs);
    auto* installedLayout = new QVBoxLayout(installed);
    m_files = new QTreeWidget(installed);
    m_files->setHeaderLabels({ tr("Type"), tr("Project"), tr("Version"), tr("File") });
    m_files->setRootIsDecorated(false);
    installedLayout->addWidget(m_files);
    auto* folders = new QHBoxLayout;
    for (const auto type : { Kind::Mod, Kind::Plugin }) {
        auto* button = new QPushButton(type == Kind::Mod ? tr("Open mods folder") : tr("Open plugins folder"), installed);
        folders->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, type] {
            if (!m_instance)
                return;
            const auto path = QDir(m_instance->gameRoot()).filePath(folder(type));
            QDir().mkpath(path);
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
    }
    installedLayout->addLayout(folders);
    tabs->addTab(installed, tr("Installed files"));
    layout->addWidget(tabs, 1);
    auto* hint = new QLabel(tr("Compatibility follows authors' metadata. Hybrid servers may have additional limits; some mods also need "
                               "installation on connecting clients. Required Modrinth dependencies are included in the install preview. "
                               "Existing manually installed projects are not identified automatically."),
                            this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);
    layout->addWidget(m_progress);
    auto* bottom = new QHBoxLayout;
    m_recover = new QPushButton(tr("Recover interrupted installation"), this);
    m_recover->setObjectName("serverContentRecover");
    bottom->addWidget(m_recover);
    bottom->addStretch();
    m_cancel = new QPushButton(tr("Cancel operation"), this);
    m_close = new QPushButton(tr("Close"), this);
    bottom->addWidget(m_cancel);
    bottom->addWidget(m_close);
    layout->addLayout(bottom);
    connect(m_search, &QPushButton::clicked, this, [this] { search(); });
    connect(m_query, &QLineEdit::returnPressed, this, [this] { search(); });
    connect(m_query, &QLineEdit::textEdited, this, &ServerContentDialog::resetSearch);
    connect(m_previous, &QPushButton::clicked, this, [this] { search(qMax(0, m_offset - 20)); });
    connect(m_next, &QPushButton::clicked, this, [this] { search(m_offset + 20); });
    connect(m_results, &QListWidget::currentRowChanged, this, &ServerContentDialog::loadVersions);
    connect(m_kind, &QComboBox::currentIndexChanged, this, &ServerContentDialog::resetSearch);
    connect(m_prereleases, &QCheckBox::toggled, this, &ServerContentDialog::loadVersions);
    connect(m_version, &QComboBox::currentIndexChanged, this, &ServerContentDialog::updateButtons);
    connect(m_install, &QPushButton::clicked, this, &ServerContentDialog::installSelected);
    connect(m_recover, &QPushButton::clicked, this, &ServerContentDialog::recover);
    connect(m_cancel, &QPushButton::clicked, this, &ServerContentDialog::cancel);
    connect(m_close, &QPushButton::clicked, this, &ServerContentDialog::reject);
    connect(m_website, &QPushButton::clicked, this, [this] {
        const auto index = m_results->currentRow();
        if (index >= 0 && index < m_projects.size())
            QDesktopServices::openUrl(
                QUrl("https://modrinth.com/" + QString(kind() == Kind::Mod ? "mod/" : "plugin/") + m_projects[index].id));
    });
    connect(m_minecraft, &QLineEdit::textEdited, this, &ServerContentDialog::resetSearch);
    connect(m_loader, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_refreshing)
            refreshProfile();
    });
    connect(m_pluginApi, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_refreshing)
            refreshProfile();
    });
    connect(instance, &ServerInstance::softwareChangeChanged, this, [this](bool busy) {
        if (!busy && !m_installing)
            refreshProfile();
        updateButtons();
    });
    connect(instance, &BaseInstance::runningStatusChanged, this, &ServerContentDialog::updateButtons);
    connect(instance, &QObject::destroyed, this, &ServerContentDialog::reject);
    refreshProfile();
    refreshInstalled();
}
ServerContentDialog::~ServerContentDialog()
{
    cancel();
}
Kind ServerContentDialog::kind() const
{
    return static_cast<Kind>(m_kind->currentData().toInt());
}
Target ServerContentDialog::selectedTarget() const
{
    Target result;
    result.minecraft = m_minecraft->text().trimmed();
    result.modLoader = m_loader->currentData().toString();
    const auto api = m_pluginApi->currentData().toString();
    if (!api.isEmpty())
        result.pluginLoaders = { "bukkit", "spigot" };
    if (api == "paper" || api == "purpur")
        result.pluginLoaders.append("paper");
    if (api == "purpur")
        result.pluginLoaders.append("purpur");
    return result;
}
void ServerContentDialog::refreshProfile()
{
    if (!m_instance || m_refreshing)
        return;
    // Preserve custom edits while rebuilding the content-type choices.
    const bool editingCustom = m_custom && sender() && (sender() == m_loader || sender() == m_pluginApi);
    auto current = editingCustom ? selectedTarget() : target(*m_instance);
    m_refreshing = true;
    const auto core = m_instance->settings()->get("ServerCore").toString();
    m_custom = core == "Custom JAR" || core == tr("Custom JAR");
    m_minecraft->setText(current.minecraft);
    m_minecraft->setReadOnly(!m_custom);
    m_loader->setCurrentIndex(qMax(0, m_loader->findData(current.modLoader)));
    const auto api = current.pluginLoaders.isEmpty() ? QString() : current.pluginLoaders.last();
    m_pluginApi->setCurrentIndex(qMax(0, m_pluginApi->findData(api)));
    const auto oldKind = kind();
    m_kind->clear();
    // Missing Minecraft metadata should not hide the selected loader's content type.
    if (!current.modLoader.isEmpty())
        m_kind->addItem(tr("Mods"), static_cast<int>(Kind::Mod));
    if (!current.pluginLoaders.isEmpty())
        m_kind->addItem(tr("Plugins"), static_cast<int>(Kind::Plugin));
    if (m_kind->findData(static_cast<int>(oldKind)) >= 0)
        m_kind->setCurrentIndex(m_kind->findData(static_cast<int>(oldKind)));
    m_profile->setText(m_custom
                           ? tr("Custom JAR: declare the actual Minecraft version, mod loader and plugin API of your server below.")
                           : m_instance->getStatusbarDescription() + tr(" — compatibility is taken from the installed server software."));
    m_refreshing = false;
    resetSearch();
}
void ServerContentDialog::refreshInstalled()
{
    if (!m_instance)
        return;
    m_files->clear();
    m_indexError.clear();
    ServerContentStore::read(m_instance->instanceRoot(), &m_installed, &m_indexError);
    if (!m_indexError.isEmpty())
        m_status->setText(m_indexError);
    for (const auto type : { Kind::Mod, Kind::Plugin }) {
        const auto directory = QDir(m_instance->gameRoot()).filePath(folder(type));
        for (const auto& filename : QDir(directory).entryList({ "*.jar" }, QDir::Files, QDir::Name)) {
            QString name = tr("Manually installed"), version;
            for (const auto& item : m_installed)
                if (item.kind == type && item.filename == filename) {
                    name = item.name;
                    version = item.number.isEmpty() ? item.versionId : item.number;
                }
            new QTreeWidgetItem(m_files, { type == Kind::Mod ? tr("Mod") : tr("Plugin"), name, version, filename });
        }
    }
    for (int i = 0; i < 3; ++i)
        m_files->resizeColumnToContents(i);
    updateButtons();
}
void ServerContentDialog::resetSearch()
{
    if (m_refreshing || m_installing)
        return;
    cancel();
    m_projects.clear();
    m_versions.clear();
    m_results->clear();
    m_version->clear();
    m_details->clear();
    m_offset = m_total = 0;
    m_status->setText(selectedTarget().supports(kind())
                          ? tr("Enter a search term or press Search to browse popular projects.")
                          : tr("This server has no supported content type or Minecraft version. Vanilla does not load mods or plugins."));
    updateButtons();
}
bool ServerContentDialog::saveCustomProfile()
{
    if (!m_custom)
        return true;
    if (!m_instance)
        return false;
    if (m_instance->isRunning() || m_instance->isChangingSoftware())
        return selectedTarget() == target(*m_instance);
    QDir().mkpath(m_instance->gameRoot());
    QLockFile folderLock(QDir(m_instance->gameRoot()).filePath(".server-launch.lock"));
    folderLock.setStaleLockTime(0);
    if (!folderLock.tryLock()) {
        m_status->setText(tr("The server folder is in use by another launcher."));
        return false;
    }
    auto* settings = m_instance->settings();
    const auto previousJar = m_instance->jarPath();
    if (!settings->reload() || m_instance->jarPath() != previousJar ||
        (settings->get("ServerCore") != "Custom JAR" && settings->get("ServerCore") != tr("Custom JAR"))) {
        refreshProfile();
        m_status->setText(tr("The server settings changed in another launcher. Check its compatibility profile again."));
        return false;
    }
    SettingsObject::Lock lock(settings);
    settings->set("ServerContentMinecraftVersion", m_minecraft->text().trimmed());
    settings->set("ServerContentModLoader", m_loader->currentData().toString());
    settings->set("ServerContentPluginApi", m_pluginApi->currentData().toString());
    return true;
}
void ServerContentDialog::search(int offset)
{
    if (!m_instance || m_installing || !selectedTarget().supports(kind()) || !saveCustomProfile())
        return;
    resetSearch();
    m_offset = offset;
    m_searchTarget = selectedTarget();
    ServerContentTask::Query query;
    query.operation = ServerContentTask::Operation::Search;
    query.text = m_query->text().trimmed();
    query.offset = offset;
    auto* task = new ServerContentTask(m_network, m_searchTarget, kind(), query);
    start(task, [this, task] {
        m_projects = task->projects();
        m_total = task->total();
        for (const auto& project : m_projects) {
            auto* item = new QListWidgetItem(project.title, m_results);
            item->setToolTip(project.description);
        }
        m_status->setText(m_projects.isEmpty() ? tr("No matching server projects on this page.")
                                               : tr("%1 projects found. Select a project to load compatible versions.").arg(m_total));
    });
}
void ServerContentDialog::loadVersions()
{
    if (m_refreshing || m_installing)
        return;
    cancel();
    m_versions.clear();
    m_version->clear();
    const auto index = m_results->currentRow();
    if (index < 0 || index >= m_projects.size()) {
        updateButtons();
        return;
    }
    const auto& project = m_projects[index];
    m_details->setPlainText(project.title + "\n\n" + project.description);
    ServerContentTask::Query query;
    query.operation = ServerContentTask::Operation::Versions;
    query.projectId = project.id;
    query.prereleases = m_prereleases->isChecked();
    auto* task = new ServerContentTask(m_network, m_searchTarget, kind(), query);
    start(task, [this, task] {
        m_versions = task->versions();
        for (const auto& version : m_versions)
            m_version->addItem(version.number + " — " + version.name, version.id);
        m_status->setText(
            tr("%1 compatible server versions. Required dependencies will be checked before installation.").arg(m_versions.size()));
    });
}
void ServerContentDialog::installSelected()
{
    if (!m_instance || m_task || m_installing || m_instance->isRunning() || m_instance->isChangingSoftware())
        return;
    const auto index = m_version->currentIndex();
    if (index < 0 || index >= m_versions.size() || target(*m_instance) != m_searchTarget)
        return;
    refreshInstalled();
    if (!m_indexError.isEmpty())
        return;
    ServerContentTask::Query query;
    query.operation = ServerContentTask::Operation::Resolve;
    query.projectId = m_versions[index].projectId;
    query.versionId = m_versions[index].id;
    query.prereleases = m_prereleases->isChecked();
    query.installed = m_installed;
    query.installed.removeIf([this](const auto& item) {
        return !QFileInfo::exists(QDir(m_instance->gameRoot()).filePath(folder(item.kind) + "/" + item.filename));
    });
    m_installing = true;
    auto* task = new ServerContentTask(m_network, m_searchTarget, kind(), query);
    start(task, [this, task] {
        const auto plan = task->plan();
        if (!m_instance || m_instance->isRunning() || target(*m_instance) != m_searchTarget) {
            m_installing = false;
            m_status->setText(tr("The server changed. Search again before installing."));
            return;
        }
        QStringList files;
        for (const auto& version : plan) {
            auto line = version.name + " " + version.number + " → " + folder(version.kind) + "/" + version.filename;
            for (const auto& old : m_installed)
                if (old.projectId == version.projectId)
                    line += tr(" (replaces installed version)");
            files.append(line);
        }
        auto preview = files.mid(0, 5).join('\n');
        if (files.size() > 5)
            preview += tr("\n…and %1 more. Open details for the complete file list.").arg(files.size() - 5);
        QMessageBox confirmation(QMessageBox::Question, tr("Install server content?"),
                                 tr("Install the selected version and its required Modrinth dependencies?\n\n%1\n\n"
                                    "Previous managed files and the content index will be backed up. Unknown files are not overwritten. "
                                    "Changes take effect on the next server start.")
                                     .arg(preview),
                                 QMessageBox::Yes | QMessageBox::Cancel, this);
        confirmation.setDefaultButton(QMessageBox::Cancel);
        confirmation.setDetailedText(files.join('\n'));
        if (confirmation.exec() != QMessageBox::Yes) {
            m_installing = false;
            m_status->setText(tr("Installation cancelled."));
            return;
        }
        auto* install = new ServerContentInstallTask(m_instance, m_network, m_searchTarget, plan);
        start(install, [this, install] {
            m_installing = false;
            refreshInstalled();
            m_status->setText(
                tr("Content installed. Changes take effect on the next server start.\nBackup: %1").arg(install->backupPath()));
        });
    });
}
void ServerContentDialog::recover()
{
    if (!m_instance || m_task || m_instance->isRunning() || m_instance->isChangingSoftware())
        return;
    m_installing = true;
    auto* task = new ServerContentInstallTask(m_instance, m_network, target(*m_instance), {});
    start(task, [this] {
        m_installing = false;
        refreshInstalled();
        m_status->setText(tr("The previous content installation was rolled back. You can start the server or install again."));
    });
}
void ServerContentDialog::start(Task* task, std::function<void()> success)
{
    const auto installing = m_installing;
    cancel();
    m_installing = installing;
    task->setParent(this);
    m_task = task;
    connect(task, &Task::status, this, [this, task](const QString& status) {
        if (m_task == task) {
            m_status->setText(status);
            updateButtons();
        }
    });
    connect(task, &Task::progress, this, [this, task](qint64 current, qint64 total) {
        if (m_task != task)
            return;
        m_progress->setRange(0, total > 0 ? 1000 : 0);
        if (total > 0)
            m_progress->setValue(static_cast<int>(1000.0 * current / total));
    });
    connect(task, &Task::finished, this, [this, task, success = std::move(success)] {
        task->deleteLater();
        if (m_task != task)
            return;
        m_task = nullptr;
        if (task->wasSuccessful())
            success();
        else {
            m_installing = false;
            m_status->setText(task->getState() == Task::State::AbortedByUser ? tr("Operation cancelled.") : task->failReason());
        }
        updateButtons();
    });
    m_progress->setRange(0, 0);
    task->start();
    updateButtons();
}
void ServerContentDialog::cancel()
{
    if (m_task && !m_task->canAbort())
        return;
    auto* task = m_task.data();
    m_task = nullptr;
    if (task)
        task->abort();
    m_installing = false;
    updateButtons();
}
void ServerContentDialog::updateButtons()
{
    const bool present = m_instance;
    const bool busy = !present || m_instance->isChangingSoftware();
    const bool stopped = present && !m_instance->isRunning();
    const bool editable = !m_installing && !busy;
    m_minecraft->setEnabled(editable && (stopped || !m_custom));
    m_loader->setEnabled(editable && m_custom && stopped);
    m_pluginApi->setEnabled(editable && m_custom && stopped);
    const QList<QWidget*> controls{ m_kind, m_query, m_results, m_version, m_prereleases };
    for (auto* control : controls)
        control->setEnabled(editable);
    m_search->setEnabled(editable && selectedTarget().supports(kind()));
    m_previous->setEnabled(editable && !m_task && m_offset > 0);
    m_next->setEnabled(editable && !m_task && m_offset + 20 < m_total);
    m_install->setEnabled(editable && stopped && !m_task && m_indexError.isEmpty() && m_version->currentIndex() >= 0 &&
                          m_version->currentIndex() < m_versions.size() && !ServerContentStore::interrupted(m_instance->instanceRoot()));
    m_recover->setVisible(present && ServerContentStore::interrupted(m_instance->instanceRoot()));
    m_recover->setEnabled(editable && stopped && !m_task);
    m_website->setEnabled(m_results->currentRow() >= 0);
    m_cancel->setEnabled(m_task && m_task->canAbort());
    m_close->setEnabled(!m_task || m_task->canAbort());
    m_progress->setVisible(m_task);
}
void ServerContentDialog::reject()
{
    if (m_task && !m_task->canAbort())
        return;
    cancel();
    QDialog::reject();
}
