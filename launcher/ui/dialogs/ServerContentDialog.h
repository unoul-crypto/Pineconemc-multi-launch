// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QDialog>
#include <QPointer>
#include <functional>
#include "server/content/ServerContent.h"
class ServerInstance;
class Task;
class QNetworkAccessManager;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QLabel;
class QListWidget;
class QTreeWidget;
class QTextBrowser;
class QPushButton;
class QProgressBar;

class ServerContentDialog : public QDialog {
    Q_OBJECT
   public:
    ServerContentDialog(ServerInstance* instance, QNetworkAccessManager* network, QWidget* parent = nullptr);
    ~ServerContentDialog() override;
    void reject() override;

   private:
    void refreshProfile();
    void refreshInstalled();
    void resetSearch();
    void search(int offset = 0);
    void loadVersions();
    void installSelected();
    void recover();
    void updateButtons();
    void start(Task* task, std::function<void()> success);
    void cancel();
    ServerContent::Kind kind() const;
    ServerContent::Target selectedTarget() const;
    bool saveCustomProfile();
    QPointer<ServerInstance> m_instance;
    QNetworkAccessManager* m_network;
    QPointer<Task> m_task;
    ServerContent::Target m_searchTarget;
    QList<ServerContent::Project> m_projects;
    QList<ServerContent::AddonVersion> m_versions;
    QList<ServerContent::Installed> m_installed;
    QString m_indexError;
    bool m_refreshing = false, m_installing = false, m_custom = false;
    int m_offset = 0, m_total = 0;
    QLineEdit *m_query, *m_minecraft;
    QComboBox *m_kind, *m_loader, *m_pluginApi, *m_version;
    QCheckBox* m_prereleases;
    QLabel *m_profile, *m_status;
    QListWidget* m_results;
    QTreeWidget* m_files;
    QTextBrowser* m_details;
    QProgressBar* m_progress;
    QPushButton *m_search, *m_previous, *m_next, *m_install, *m_recover, *m_cancel, *m_close, *m_website;
};
