// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QDialog>
#include <QPointer>
class ServerInstance;
class ServerSoftwareWidget;
class ServerChangeTask;
class QLineEdit;
class QLabel;
class QPushButton;
class QProgressBar;
class ServerSoftwareDialog : public QDialog {
    Q_OBJECT
   public:
    explicit ServerSoftwareDialog(ServerInstance* instance, QWidget* parent = nullptr);
    ~ServerSoftwareDialog() override;
    void reject() override;
   signals:
    void softwareChanged();

   private:
    void updateButtons();
    void install();
    QPointer<ServerInstance> m_instance;
    ServerSoftwareWidget* m_software;
    QWidget* m_localRow;
    QLineEdit* m_jar;
    QLabel* m_status;
    QPushButton* m_install;
    QPushButton* m_cancel;
    QProgressBar* m_progress;
    QPointer<ServerChangeTask> m_task;
};
