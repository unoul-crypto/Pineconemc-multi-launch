// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QMap>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>
#include "server/content/ServerContent.h"
class ServerInstance;
class ServerContentTask;
class ServerRequest;
class Task;
class QNetworkAccessManager;
class QLineEdit;
class QComboBox;
class QLabel;
class QListWidget;
class QTreeWidget;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QSplitter;
class ModFilterWidget;
class ProjectDescriptionPage;
class ProjectIconCache;

class ServerContentWidget : public QWidget {
    Q_OBJECT
   public:
    ServerContentWidget(ServerInstance* instance,
                        QNetworkAccessManager* network,
                        QWidget* parent = nullptr,
                        std::optional<ServerContent::Kind> fixedKind = {});
    ~ServerContentWidget() override;
    bool canClose() const;
    bool prepareClose();
    void refresh();

   private:
    void refreshProfile();
    void refreshInstalled();
    void resetSearch();
    void search(bool append = false);
    void cancelSearch();
    void loadVersions();
    void loadCategories();
    void rebuildFilters();
    void showProject(const QJsonObject& project);
    void toggleSelection();
    void toggleRow(int row);
    void updateSelection();
    void updateProjectIcons();
    void installSelected();
    void recover();
    void updateButtons();
    void start(Task* task, std::function<void()> success);
    void cancel();
    ServerContent::Kind kind() const;
    ServerContent::Target selectedTarget() const;
    ServerContent::SearchOptions searchOptions() const;
    QStringList releaseFilters() const;
    bool saveCustomProfile();
    QPointer<ServerInstance> m_instance;
    QNetworkAccessManager* m_network;
    QPointer<Task> m_task;
    QPointer<ServerContentTask> m_searchTask;
    QPointer<ServerRequest> m_categoriesTask;
    std::optional<ServerContent::Kind> m_fixedKind;
    ServerContent::Target m_searchTarget, m_profileTarget, m_queueTarget;
    ServerContent::Kind m_queueKind = ServerContent::Kind::Mod;
    QList<ServerContent::Project> m_projects;
    QList<ServerContent::AddonVersion> m_versions;
    QList<ServerContent::Installed> m_installed;
    QMap<QString, ServerContent::AddonVersion> m_selected;
    QString m_indexError, m_selectAfterLoad;
    bool m_refreshing = false, m_installing = false, m_custom = false;
    bool m_reviewing = false;
    bool m_hasSearched = false;
    int m_offset = 0, m_total = 0;
    QTimer m_searchTimer;
    QWidget* m_profileEditor;
    QLineEdit *m_query, *m_minecraft;
    QComboBox *m_kind, *m_loader, *m_pluginApi, *m_version, *m_sort;
    QLabel *m_profile, *m_status, *m_count;
    QListWidget *m_results, *m_selection;
    QTreeWidget* m_files;
    ProjectDescriptionPage* m_details;
    ProjectIconCache* m_icons;
    ModFilterWidget* m_filters = nullptr;
    QSplitter* m_splitter;
    QTabWidget* m_tabs;
    QProgressBar* m_progress;
    QPushButton *m_filterButton, *m_more, *m_toggle, *m_install, *m_recover, *m_cancel, *m_website, *m_search, *m_remove, *m_clear;
};
