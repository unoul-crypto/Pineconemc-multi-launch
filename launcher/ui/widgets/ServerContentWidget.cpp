// SPDX-License-Identifier: GPL-3.0-only
#include "ServerContentWidget.h"
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
#include <QNetworkAccessManager>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include "Application.h"
#include "Markdown.h"
#include "StringUtils.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "server/ServerInstance.h"
#include "server/ServerRequest.h"
#include "server/content/ServerContentInstallTask.h"
#include "server/content/ServerContentTask.h"
#include "ui/dialogs/ReviewMessageBox.h"
#include "ui/widgets/ModFilterWidget.h"
#include "ui/widgets/ProjectDescriptionPage.h"
#include "ui/widgets/ProjectIconCache.h"
#include "ui/widgets/ProjectItem.h"

using namespace ServerContent;
ServerContentWidget::ServerContentWidget(ServerInstance* instance,
                                         QNetworkAccessManager* network,
                                         QWidget* parent,
                                         std::optional<Kind> fixedKind)
    : QWidget(parent), m_instance(instance), m_network(network), m_fixedKind(fixedKind)
{
    setWindowTitle(tr("Server mods and plugins — Modrinth"));
    resize(900, 700);
    auto* layout = new QVBoxLayout(this);
    m_profile = new QLabel(this);
    m_profile->setTextFormat(Qt::PlainText);
    m_profile->setWordWrap(true);
    layout->addWidget(m_profile);
    m_profileEditor = new QWidget(this);
    auto* form = new QFormLayout(m_profileEditor);
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
    layout->addWidget(m_profileEditor);
    auto* tabs = new QTabWidget(this);
    m_tabs = tabs;
    auto* browse = new QWidget(tabs);
    auto* browseLayout = new QVBoxLayout(browse);
    auto* searchRow = new QHBoxLayout;
    m_kind = new QComboBox(browse);
    m_kind->setObjectName("serverContentKind");
    searchRow->addWidget(m_kind);
    m_filterButton = new QPushButton(tr("Filter options"), browse);
    m_filterButton->setObjectName("serverContentFilters");
    searchRow->addWidget(m_filterButton);
    m_query = new QLineEdit(browse);
    m_query->setObjectName("serverContentQuery");
    m_query->setPlaceholderText(tr("Search Modrinth…"));
    searchRow->addWidget(m_query, 1);
    m_search = new QPushButton(tr("Search"), browse);
    m_search->setObjectName("serverContentSearch");
    searchRow->addWidget(m_search);
    browseLayout->addLayout(searchRow);
    auto* splitter = new QSplitter(browse);
    m_splitter = splitter;
    m_results = new QListWidget(splitter);
    m_results->setObjectName("serverContentResults");
    m_results->setMinimumWidth(230);
    m_details = new ProjectDescriptionPage(splitter);
    m_details->setObjectName("serverContentDescription");
    m_details->setMetaEntry("ModrinthPacks");
    m_details->setOpenLinks(false);
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
    m_sort = new QComboBox(browse);
    m_sort->setObjectName("serverContentSort");
    for (const auto& sort : ModrinthAPI().getSortingMethods())
        m_sort->addItem(sort.readable_name, sort.name);
    m_sort->setCurrentIndex(m_sort->findData("downloads"));
    versionsRow->addWidget(m_sort);
    browseLayout->addLayout(versionsRow);
    auto* actions = new QHBoxLayout;
    m_more = new QPushButton(tr("Load more"), browse);
    m_more->setObjectName("serverContentMore");
    m_count = new QLabel(browse);
    m_count->setObjectName("serverContentCount");
    m_website = new QPushButton(tr("Project page"), browse);
    m_toggle = new QPushButton(tr("Select version for download"), browse);
    m_toggle->setObjectName("serverContentSelect");
    for (auto* button : { m_more, m_website })
        actions->addWidget(button);
    actions->addWidget(m_count);
    actions->addStretch();
    actions->addWidget(m_toggle);
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
    auto* selected = new QWidget(tabs);
    auto* selectedLayout = new QVBoxLayout(selected);
    m_selection = new QListWidget(selected);
    m_selection->setObjectName("serverContentSelection");
    selectedLayout->addWidget(m_selection);
    auto* selectionActions = new QHBoxLayout;
    m_remove = new QPushButton(tr("Remove selected"), selected);
    m_clear = new QPushButton(tr("Clear selection"), selected);
    selectionActions->addWidget(m_remove);
    selectionActions->addWidget(m_clear);
    selectedLayout->addLayout(selectionActions);
    tabs->addTab(selected, tr("Selected (0)"));
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
    m_install = new QPushButton(tr("Review and download (0)…"), this);
    m_install->setObjectName("serverContentInstall");
    bottom->addWidget(m_cancel);
    bottom->addWidget(m_install);
    layout->addLayout(bottom);
    connect(m_search, &QPushButton::clicked, this, [this] { search(); });
    connect(m_query, &QLineEdit::returnPressed, this, [this] { search(); });

    connect(m_more, &QPushButton::clicked, this, [this] { search(true); });
    connect(m_filterButton, &QPushButton::clicked, this, [this] { m_filters->setVisible(!m_filters->isVisible()); });
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this] {
        resetSearch();
        search();
    });
    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(350);
    connect(&m_searchTimer, &QTimer::timeout, this, [this] { search(); });
    connect(m_query, &QLineEdit::textEdited, this, [this] {
        resetSearch();
        m_searchTimer.start();
    });
    connect(m_toggle, &QPushButton::clicked, this, &ServerContentWidget::toggleSelection);
    connect(m_remove, &QPushButton::clicked, this, [this] {
        for (auto* item : m_selection->selectedItems())
            m_selected.remove(item->data(Qt::UserRole).toString());
        updateSelection();
    });
    connect(m_clear, &QPushButton::clicked, this, [this] {
        m_selected.clear();
        updateSelection();
    });
    connect(m_selection, &QListWidget::itemSelectionChanged, this, &ServerContentWidget::updateButtons);
    connect(m_results, &QListWidget::currentRowChanged, this, &ServerContentWidget::loadVersions);
    connect(m_kind, &QComboBox::currentIndexChanged, this, &ServerContentWidget::resetSearch);
    connect(m_version, &QComboBox::currentIndexChanged, this, &ServerContentWidget::updateButtons);
    connect(m_install, &QPushButton::clicked, this, &ServerContentWidget::installSelected);
    connect(m_recover, &QPushButton::clicked, this, &ServerContentWidget::recover);
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        cancel();
        cancelSearch();
        m_status->setText(tr("Operation cancelled."));
    });
    connect(m_website, &QPushButton::clicked, this, [this] {
        const auto index = m_results->currentRow();
        if (index >= 0 && index < m_projects.size())
            QDesktopServices::openUrl(
                QUrl("https://modrinth.com/" + QString(kind() == Kind::Mod ? "mod/" : "plugin/") + m_projects[index].id));
    });
    connect(m_minecraft, &QLineEdit::textEdited, this, [this] {
        if (m_filters)
            m_filters->setServerMinecraft(m_minecraft->text().trimmed());
        resetSearch();
    });
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
    connect(instance, &BaseInstance::runningStatusChanged, this, &ServerContentWidget::updateButtons);
    connect(instance, &QObject::destroyed, this, [this] {
        prepareClose();
        setEnabled(false);
    });
    auto* delegate = new ProjectItemDelegate(m_results);
    m_results->setItemDelegate(delegate);
    m_results->setIconSize(QSize(48, 48));
    m_results->setAlternatingRowColors(true);
    m_results->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(delegate, &ProjectItemDelegate::checkboxClicked, this, [this](const QModelIndex& index) { toggleRow(index.row()); });
    connect(m_results, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { toggleRow(m_results->row(item)); });
    connect(m_results->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (isVisible() && value > 0 && value == m_results->verticalScrollBar()->maximum() && m_more->isEnabled())
            QTimer::singleShot(0, this, [this] { search(true); });
    });
    connect(m_details, &QTextBrowser::anchorClicked, this, [](const QUrl& url) {
        if ((url.scheme() == "https" || url.scheme() == "http") && !url.host().isEmpty() && url.userInfo().isEmpty())
            QDesktopServices::openUrl(url);
    });
    m_icons = new ProjectIconCache(network, this);
    connect(m_icons, &ProjectIconCache::ready, this, &ServerContentWidget::updateProjectIcons);
    refreshProfile();
    refreshInstalled();
}
ServerContentWidget::~ServerContentWidget()
{
    cancelSearch();
    cancel();
    if (m_categoriesTask)
        m_categoriesTask->abort();
}
Kind ServerContentWidget::kind() const
{
    return m_fixedKind.value_or(static_cast<Kind>(m_kind->currentData().toInt()));
}
Target ServerContentWidget::selectedTarget() const
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
void ServerContentWidget::refresh()
{
    if (!m_instance || m_installing)
        return;
    if (target(*m_instance) != m_profileTarget)
        refreshProfile();
    refreshInstalled();
    if (!m_hasSearched && selectedTarget().supports(kind()))
        search();
}
void ServerContentWidget::refreshProfile()
{
    if (!m_instance || m_refreshing)
        return;
    const bool editing = m_custom && (sender() == m_loader || sender() == m_pluginApi);
    const auto current = editing ? selectedTarget() : target(*m_instance);
    if (m_filters && !editing && current == m_profileTarget) {
        refreshInstalled();
        return;
    }
    m_refreshing = true;
    const auto core = m_instance->settings()->get("ServerCore").toString();
    m_custom = core == "Custom JAR" || core == tr("Custom JAR");
    m_minecraft->setText(current.minecraft);
    m_minecraft->setReadOnly(!m_custom);
    m_loader->setCurrentIndex(qMax(0, m_loader->findData(current.modLoader)));
    m_pluginApi->setCurrentIndex(
        qMax(0, m_pluginApi->findData(current.pluginLoaders.isEmpty() ? QString() : current.pluginLoaders.last())));
    const auto oldKind = kind();
    m_kind->clear();
    if ((!m_fixedKind || *m_fixedKind == Kind::Mod) && (!current.modLoader.isEmpty() || m_fixedKind))
        m_kind->addItem(tr("Mods"), static_cast<int>(Kind::Mod));
    if ((!m_fixedKind || *m_fixedKind == Kind::Plugin) && (!current.pluginLoaders.isEmpty() || m_fixedKind))
        m_kind->addItem(tr("Plugins"), static_cast<int>(Kind::Plugin));
    const auto oldIndex = m_kind->findData(static_cast<int>(oldKind));
    if (oldIndex >= 0)
        m_kind->setCurrentIndex(oldIndex);
    m_kind->setVisible(!m_fixedKind);
    m_profileEditor->setVisible(m_custom);
    m_profile->setText(m_custom ? tr("Custom JAR: declare its Minecraft version, mod loader and plugin API below.")
                                : m_instance->getStatusbarDescription() + " · " + current.loaders(kind()).join(", "));
    if (current != m_queueTarget || kind() != m_queueKind)
        m_selected.clear();
    m_queueTarget = current;
    m_queueKind = kind();
    m_profileTarget = current;
    rebuildFilters();
    m_refreshing = false;
    resetSearch();
    updateSelection();
}
void ServerContentWidget::rebuildFilters()
{
    if (m_categoriesTask) {
        auto* old = m_categoriesTask.data();
        m_categoriesTask = nullptr;
        old->abort();
    }
    if (m_filters) {
        m_filters->disconnect(this);
        delete m_filters;
    }
    m_filters = ModFilterWidget::createServer(m_minecraft->text().trimmed()).release();
    m_filters->setObjectName("serverContentFilterWidget");
    m_splitter->insertWidget(0, m_filters);
    connect(m_filters, &ModFilterWidget::filterChanged, this, [this] {
        resetSearch();
        search();
    });
    loadCategories();
}
void ServerContentWidget::loadCategories()
{
    auto populate = [this](const QByteArray& json) {
        auto categories = ModrinthAPI::loadCategories(json, kind() == Kind::Mod ? "mod" : "plugin");
        if (categories.isEmpty() && kind() == Kind::Plugin)
            categories = ModrinthAPI::loadModCategories(json);
        m_filters->setCategories(categories);
    };
    const auto cached = m_network ? m_network->property("_pineconeServerCategories").toByteArray() : QByteArray();
    if (!cached.isEmpty()) {
        populate(cached);
        return;
    }
    auto* request = new ServerRequest(m_network, QUrl("https://api.modrinth.com/v2/tag/category"));
    request->setParent(this);
    m_categoriesTask = request;
    connect(request, &Task::finished, this, [this, request, populate] {
        request->deleteLater();
        if (m_categoriesTask != request)
            return;
        m_categoriesTask = nullptr;
        if (request->wasSuccessful() && QJsonDocument::fromJson(request->response()).isArray()) {
            m_network->setProperty("_pineconeServerCategories", request->response());
            populate(request->response());
        }
    });
    request->start();
}
SearchOptions ServerContentWidget::searchOptions() const
{
    SearchOptions options;
    options.sort = m_sort->currentData().toString();
    if (m_filters) {
        const auto filter = m_filters->getFilter();
        options.categories = filter->categoryIds;
        options.openSource = filter->openSource;
        if (filter->hideInstalled && m_instance)
            for (const auto& item : m_installed)
                if (item.kind == kind() &&
                    QFileInfo::exists(QDir(m_instance->gameRoot()).filePath(folder(item.kind) + "/" + item.filename)))
                    options.excludedProjects.append(item.projectId);
    }
    return options;
}
QStringList ServerContentWidget::releaseFilters() const
{
    QStringList result;
    if (m_filters)
        for (const auto release : m_filters->getFilter()->releases)
            result.append(release.toString());
    return result.isEmpty() ? QStringList{ "release", "beta", "alpha" } : result;
}
void ServerContentWidget::refreshInstalled()
{
    if (!m_instance)
        return;
    m_files->clear();
    m_indexError.clear();
    ServerContentStore::read(m_instance->instanceRoot(), &m_installed, &m_indexError);
    if (!m_indexError.isEmpty())
        m_status->setText(m_indexError);
    for (const auto type : { Kind::Mod, Kind::Plugin }) {
        if (m_fixedKind && type != *m_fixedKind)
            continue;
        for (const auto& filename :
             QDir(QDir(m_instance->gameRoot()).filePath(folder(type))).entryList({ "*.jar" }, QDir::Files, QDir::Name)) {
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
    updateSelection();
}
void ServerContentWidget::resetSearch()
{
    if (m_refreshing || m_installing)
        return;
    cancelSearch();
    cancel();
    m_selectAfterLoad.clear();
    m_projects.clear();
    m_versions.clear();
    m_results->clear();
    m_version->clear();
    m_details->flush();
    m_details->clear();
    m_offset = m_total = 0;
    m_hasSearched = false;
    if (kind() != m_queueKind || selectedTarget() != m_queueTarget) {
        m_selected.clear();
        m_queueKind = kind();
        m_queueTarget = selectedTarget();
        updateSelection();
    }
    m_status->setText(selectedTarget().supports(kind())
                          ? tr("Search or press Search to browse popular projects.")
                          : tr("This server does not support this content type. Configure its software / Minecraft version first."));
    updateButtons();
}
bool ServerContentWidget::saveCustomProfile()
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
        m_status->setText(tr("The server settings changed. Check the compatibility profile again."));
        return false;
    }
    SettingsObject::Lock lock(settings);
    settings->set("ServerContentMinecraftVersion", m_minecraft->text().trimmed());
    settings->set("ServerContentModLoader", m_loader->currentData().toString());
    settings->set("ServerContentPluginApi", m_pluginApi->currentData().toString());
    m_profileTarget = selectedTarget();
    return true;
}
void ServerContentWidget::cancelSearch()
{
    m_searchTimer.stop();
    auto* task = m_searchTask.data();
    m_searchTask = nullptr;
    if (task)
        task->abort();
}
void ServerContentWidget::search(bool append)
{
    if (!m_instance || m_installing || m_searchTask || !selectedTarget().supports(kind()) || !saveCustomProfile())
        return;
    if (append && (m_offset <= 0 || m_offset >= m_total))
        return;
    if (!append)
        resetSearch();
    m_searchTarget = selectedTarget();
    m_hasSearched = true;
    ServerContentTask::Query query;
    query.text = m_query->text().trimmed();
    query.offset = append ? m_offset : 0;
    query.search = searchOptions();
    const auto requestedOffset = query.offset;
    auto* task = new ServerContentTask(m_network, m_searchTarget, kind(), query);
    task->setParent(this);
    m_searchTask = task;
    m_status->setText(append ? tr("Loading more projects…") : tr("Searching Modrinth…"));
    connect(task, &Task::finished, this, [this, task, requestedOffset] {
        task->deleteLater();
        if (m_searchTask != task)
            return;
        m_searchTask = nullptr;
        if (!task->wasSuccessful()) {
            m_status->setText(task->failReason());
            updateButtons();
            return;
        }
        const auto previousCount = m_projects.size();
        for (const auto& project : task->projects()) {
            if (std::any_of(m_projects.begin(), m_projects.end(), [&project](const auto& old) { return old.id == project.id; }))
                continue;
            m_projects.append(project);
            auto* item = new QListWidgetItem(project.title, m_results);
            item->setToolTip(project.description);
            item->setData(UserDataTypes::TITLE, project.title);
            item->setData(UserDataTypes::DESCRIPTION, project.description);
            item->setSizeHint(QSize(0, 64));
        }
        m_total = task->total();
        m_offset = task->nextOffset() > requestedOffset ? task->nextOffset() : m_total;
        updateProjectIcons();
        updateSelection();
        if (!m_task)
            m_status->setText(tr("Loaded %1 projects. Select versions now; files download after confirmation.").arg(m_projects.size()));
        updateButtons();
        if (m_projects.size() == previousCount && m_more->isEnabled() && isVisible())
            QTimer::singleShot(0, this, [this] { search(true); });
    });
    task->start();
    updateButtons();
}
void ServerContentWidget::updateProjectIcons()
{
    for (int row = 0; row < m_projects.size(); ++row) {
        const auto& url = m_projects[row].icon;
        QString cacheFile;
        if (APPLICATION_DYN)
            cacheFile = APPLICATION->metacache()->resolveEntry("ModrinthPacks", ProjectIconCache::cacheKey(url))->getFullPath();
        const auto icon = m_icons->icon(url, cacheFile);
        m_results->item(row)->setIcon(icon.isNull() ? QIcon::fromTheme("screenshot-placeholder") : icon);
    }
}
void ServerContentWidget::showProject(const QJsonObject& project)
{
    m_details->flush();
    const auto header = "<h2>" + project.value("title").toString().toHtmlEscaped() + "</h2>";
    const auto body = project.value("body").toString();
    m_details->setHtml(StringUtils::htmlListPatch(
        header + (body.isEmpty() ? project.value("description").toString().toHtmlEscaped() : markdownToHTML(body))));
}
void ServerContentWidget::loadVersions()
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
    const auto project = m_projects[index];
    if (m_selectAfterLoad != project.id)
        m_selectAfterLoad.clear();
    m_details->flush();
    m_details->setPlainText(project.title + "\n\n" + project.description);
    ServerContentTask::Query query;
    query.operation = ServerContentTask::Operation::Versions;
    query.projectId = project.id;
    query.releases = releaseFilters();
    auto* task = new ServerContentTask(m_network, m_searchTarget, kind(), query);
    connect(task, &ServerContentTask::projectInfoReady, this, [this, task](const QJsonObject& info) {
        if (m_task == task)
            showProject(info);
    });
    start(task, [this, task, project] {
        m_versions = task->versions();
        for (auto& version : m_versions) {
            version.name = project.title;
            m_version->addItem(version.number + " — " + version.releaseType, version.id);
        }
        if (m_selected.contains(project.id)) {
            const auto selected = m_version->findData(m_selected.value(project.id).id);
            if (selected >= 0)
                m_version->setCurrentIndex(selected);
        }
        m_status->setText(tr("%1 compatible versions. Select a version to add it to the download list.").arg(m_versions.size()));
        if (m_selectAfterLoad == project.id) {
            m_selectAfterLoad.clear();
            toggleSelection();
        }
    });
}
void ServerContentWidget::toggleRow(int row)
{
    if (m_installing || row < 0 || row >= m_projects.size())
        return;
    const auto id = m_projects[row].id;
    if (m_selected.remove(id)) {
        updateSelection();
        return;
    }
    if (m_results->currentRow() == row && !m_task && !m_versions.isEmpty()) {
        toggleSelection();
        return;
    }
    m_selectAfterLoad = id;
    if (m_results->currentRow() == row)
        loadVersions();
    else
        m_results->setCurrentRow(row);
}
void ServerContentWidget::toggleSelection()
{
    const auto index = m_version->currentIndex();
    if (m_installing || index < 0 || index >= m_versions.size())
        return;
    const auto version = m_versions[index];
    if (m_selected.contains(version.projectId) && m_selected.value(version.projectId).id == version.id)
        m_selected.remove(version.projectId);
    else {
        if (m_selected.size() >= 128 && !m_selected.contains(version.projectId)) {
            m_status->setText(tr("Select at most 128 projects."));
            return;
        }
        m_selected.insert(version.projectId, version);
        m_queueTarget = m_searchTarget;
        m_queueKind = kind();
    }
    updateSelection();
}
void ServerContentWidget::updateSelection()
{
    m_selection->clear();
    for (const auto& version : m_selected) {
        auto* item = new QListWidgetItem(version.name + " " + version.number, m_selection);
        item->setData(Qt::UserRole, version.projectId);
    }
    m_tabs->setTabText(2, tr("Selected (%1)").arg(m_selected.size()));
    for (int row = 0; row < m_projects.size(); ++row) {
        m_results->item(row)->setData(Qt::CheckStateRole, m_selected.contains(m_projects[row].id) ? Qt::Checked : Qt::Unchecked);
        m_results->item(row)->setData(
            UserDataTypes::INSTALLED, std::any_of(m_installed.begin(), m_installed.end(), [this, row](const auto& item) {
                return item.projectId == m_projects[row].id && item.kind == kind() && m_instance &&
                       QFileInfo::exists(QDir(m_instance->gameRoot()).filePath(folder(item.kind) + "/" + item.filename));
            }));
    }
    updateButtons();
}
void ServerContentWidget::installSelected()
{
    if (!m_instance || m_task || m_installing || m_instance->isRunning() || m_instance->isChangingSoftware() || m_selected.isEmpty())
        return;
    if (target(*m_instance) != m_queueTarget || kind() != m_queueKind) {
        refreshProfile();
        return;
    }
    refreshInstalled();
    if (!m_indexError.isEmpty())
        return;
    cancelSearch();
    ServerContentTask::Query query;
    query.operation = ServerContentTask::Operation::Resolve;
    for (const auto& version : m_selected) {
        query.selections.append({ version.projectId, version.id });
        query.prereleases |= version.releaseType != "release";
    }
    query.installed = m_installed;
    query.installed.removeIf([this](const auto& item) {
        return !QFileInfo::exists(QDir(m_instance->gameRoot()).filePath(folder(item.kind) + "/" + item.filename));
    });
    m_installing = true;
    auto* task = new ServerContentTask(m_network, m_queueTarget, kind(), query);
    start(task, [this, task] {
        const auto plan = task->plan();
        if (!m_instance || m_instance->isRunning() || target(*m_instance) != m_queueTarget) {
            m_installing = false;
            m_status->setText(tr("The server changed. Search again before installing."));
            return;
        }
        auto review = std::unique_ptr<ReviewMessageBox>(ReviewMessageBox::create(this, tr("Review server downloads")));
        review->retranslateUi(kind() == Kind::Mod ? tr("server mods") : tr("server plugins"));
        for (const auto& version : plan) {
            const bool root = m_selected.contains(version.projectId);
            review->appendResource({ .name = version.name + " " + version.number,
                                     .filename = folder(version.kind) + "/" + version.filename,
                                     .provider = "Modrinth",
                                     .required_by = root ? QStringList() : QStringList{ tr("Selected projects") },
                                     .version_type = version.releaseType,
                                     .enabled = true,
                                     .id = version.projectId,
                                     .locked = !root });
        }
        m_reviewing = true;
        const auto accepted = review->exec();
        m_reviewing = false;
        if (accepted != QDialog::Accepted) {
            m_installing = false;
            m_status->setText(tr("Download selection kept; nothing was installed."));
            return;
        }
        const auto removed = review->deselectedResourceIds();
        if (!removed.isEmpty()) {
            for (const auto& id : removed)
                m_selected.remove(id);
            m_installing = false;
            updateSelection();
            m_status->setText(m_selected.isEmpty() ? tr("Nothing selected. No files were downloaded.")
                                                   : tr("Selection changed. Rechecking required dependencies…"));
            if (!m_selected.isEmpty())
                installSelected();
            return;
        }
        auto* install = new ServerContentInstallTask(m_instance, m_network, m_queueTarget, plan);
        start(install, [this, install] {
            m_installing = false;
            m_selected.clear();
            refreshInstalled();
            m_status->setText(tr("Content installed. Changes apply on the next server start.\nBackup: %1").arg(install->backupPath()));
        });
    });
}
void ServerContentWidget::recover()
{
    if (!m_instance || m_task || m_instance->isRunning() || m_instance->isChangingSoftware())
        return;
    m_installing = true;
    auto* task = new ServerContentInstallTask(m_instance, m_network, target(*m_instance), {});
    start(task, [this] {
        m_installing = false;
        refreshInstalled();
        m_status->setText(tr("The interrupted installation was rolled back."));
    });
}
void ServerContentWidget::start(Task* task, std::function<void()> success)
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
void ServerContentWidget::cancel()
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
void ServerContentWidget::updateButtons()
{
    const bool present = m_instance;
    const bool busy = !present || m_instance->isChangingSoftware();
    const bool stopped = present && !m_instance->isRunning();
    const bool editable = !m_installing && !busy;
    m_minecraft->setEnabled(editable && (stopped || !m_custom));
    m_loader->setEnabled(editable && m_custom && stopped);
    m_pluginApi->setEnabled(editable && m_custom && stopped);
    const QList<QWidget*> controls{ m_kind, m_query, m_results, m_version, m_sort, m_filters, m_filterButton, m_selection, m_clear };
    for (auto* control : controls)
        if (control)
            control->setEnabled(editable);
    m_search->setEnabled(editable && !m_searchTask && selectedTarget().supports(kind()));
    m_more->setEnabled(editable && !m_searchTask && m_offset > 0 && m_offset < m_total);
    m_count->setText(tr("Loaded %1 / found %2").arg(m_projects.size()).arg(m_total));
    const auto versionIndex = m_version->currentIndex();
    const bool validVersion = versionIndex >= 0 && versionIndex < m_versions.size();
    m_toggle->setEnabled(editable && !m_task && validVersion);
    m_toggle->setText(validVersion && m_selected.value(m_versions[versionIndex].projectId).id == m_versions[versionIndex].id
                          ? tr("Deselect version")
                          : tr("Select version for download"));
    m_install->setText(tr("Review and download (%1)…").arg(m_selected.size()));
    m_install->setEnabled(editable && stopped && !m_task && !m_selected.isEmpty() && m_indexError.isEmpty() &&
                          !ServerContentStore::interrupted(m_instance->instanceRoot()));
    m_remove->setEnabled(editable && !m_selection->selectedItems().isEmpty());
    m_recover->setVisible(present && ServerContentStore::interrupted(m_instance->instanceRoot()));
    m_recover->setEnabled(editable && stopped && !m_task);
    m_website->setEnabled(m_results->currentRow() >= 0);
    m_cancel->setEnabled((m_task && m_task->canAbort()) || m_searchTask);
    m_progress->setVisible(m_task || m_searchTask);
}
bool ServerContentWidget::canClose() const
{
    return !m_reviewing && (!m_task || m_task->canAbort());
}
bool ServerContentWidget::prepareClose()
{
    if (!canClose())
        return false;
    cancelSearch();
    cancel();
    return true;
}
