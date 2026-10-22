// SPDX-License-Identifier: GPL-3.0-only
#include "ServerSoftwareDialog.h"
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include "Application.h"
#include "server/ServerChangeTask.h"
#include "server/ServerInstance.h"
#include "ui/widgets/ServerSoftwareWidget.h"

ServerSoftwareDialog::ServerSoftwareDialog(ServerInstance* instance, QWidget* parent) : QDialog(parent), m_instance(instance)
{
    setWindowTitle(tr("Change server software / version"));
    setWindowModality(Qt::WindowModal);
    resize(620, 420);
    auto* layout = new QVBoxLayout(this);
    auto* warning =
        new QLabel(tr("Changing server software or downgrading Minecraft can make worlds, mods, plugins and settings "
                      "incompatible. Make a full server backup first. The launcher backs up the JAR and instance settings only."),
                   this);
    warning->setWordWrap(true);
    layout->addWidget(warning);
    m_software = new ServerSoftwareWidget(APPLICATION->network(), this);
    auto* settings = instance->settings();
    auto core = ServerCatalog::Core::Custom;
    for (const auto value :
         { ServerCatalog::Core::Vanilla, ServerCatalog::Core::Paper, ServerCatalog::Core::Purpur, ServerCatalog::Core::Fabric })
        if (settings->get("ServerCore").toString() == ServerCatalog::name(value))
            core = value;
    m_software->setSelection(core, settings->get("ServerMinecraftVersion").toString(),
                             settings->get(core == ServerCatalog::Core::Fabric ? "ServerLoaderVersion" : "ServerBuild").toString());
    layout->addWidget(m_software);
    m_localRow = new QWidget(this);
    auto* horizontal = new QHBoxLayout(m_localRow);
    horizontal->setContentsMargins(0, 0, 0, 0);
    horizontal->addWidget(new QLabel(tr("Server JAR:"), m_localRow));
    m_jar = new QLineEdit(m_localRow);
    m_jar->setObjectName("replacementServerJar");
    auto* browse = new QPushButton(tr("Browse…"), m_localRow);
    horizontal->addWidget(m_jar, 1);
    horizontal->addWidget(browse);
    layout->addWidget(m_localRow);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    m_progress = new QProgressBar(this);
    m_progress->hide();
    layout->addWidget(m_progress);
    auto* buttons = new QDialogButtonBox(this);
    m_install = buttons->addButton(tr("Change software"), QDialogButtonBox::AcceptRole);
    m_install->setObjectName("confirmSoftwareChange");
    m_cancel = buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(m_cancel, &QPushButton::clicked, this, &ServerSoftwareDialog::reject);
    connect(m_install, &QPushButton::clicked, this, &ServerSoftwareDialog::install);
    connect(m_software, &ServerSoftwareWidget::selectionChanged, this, &ServerSoftwareDialog::updateButtons);
    connect(m_jar, &QLineEdit::textChanged, this, &ServerSoftwareDialog::updateButtons);
    connect(instance, &ServerInstance::softwareChangeChanged, this, &ServerSoftwareDialog::updateButtons);
    connect(instance, &BaseInstance::runningStatusChanged, this, &ServerSoftwareDialog::updateButtons);
    connect(instance, &QObject::destroyed, this, &ServerSoftwareDialog::reject);
    connect(browse, &QPushButton::clicked, this, [this] {
        const auto file = QFileDialog::getOpenFileName(this, tr("Select runnable server JAR"), {}, tr("JAR files (*.jar)"));
        if (!file.isEmpty())
            m_jar->setText(file);
    });
    m_software->setActive(true);
    updateButtons();
}
ServerSoftwareDialog::~ServerSoftwareDialog()
{
    if (m_task && m_task->isRunning())
        m_task->abort();
}
void ServerSoftwareDialog::reject()
{
    if (m_task && m_task->isRunning())
        m_task->abort();
    m_software->setActive(false);
    QDialog::reject();
}
void ServerSoftwareDialog::updateButtons()
{
    const bool busy = m_task && m_task->isRunning();
    const bool custom = m_software->core() == ServerCatalog::Core::Custom;
    m_localRow->setVisible(custom);
    m_localRow->setEnabled(!busy);
    m_software->setEnabled(!busy);
    const bool valid =
        custom ? QFileInfo(m_jar->text()).isReadable() && QFileInfo(m_jar->text()).suffix().compare("jar", Qt::CaseInsensitive) == 0
               : m_software->download().has_value();
    m_install->setEnabled(m_instance && !m_instance->isRunning() && !m_instance->isChangingSoftware() && !busy && valid);
}
void ServerSoftwareDialog::install()
{
    if (!m_instance || m_instance->isRunning() || m_instance->isChangingSoftware())
        return;
    const auto download = m_software->download();
    auto target = download ? ServerCatalog::name(download->core) + " " + download->minecraftVersion : tr("Custom JAR");
    if (download && !download->build.isEmpty())
        target += tr(" build %1").arg(download->build);
    if (download && !download->loader.isEmpty())
        target += tr(" loader %1").arg(download->loader);
    auto current = m_instance->getStatusbarDescription();
    const auto oldBuild = m_instance->settings()->get("ServerBuild").toString();
    const auto oldLoader = m_instance->settings()->get("ServerLoaderVersion").toString();
    if (!oldBuild.isEmpty())
        current += tr(" build %1").arg(oldBuild);
    if (!oldLoader.isEmpty())
        current += tr(" loader %1").arg(oldLoader);
    QMessageBox warning(QMessageBox::Warning, tr("Change server software?"),
                        tr("Change %1 to %2?\n\nWorlds, mods, plugins and configurations may be incompatible with the new software. "
                           "Downgrading Minecraft can damage a world. Back up the entire server folder before continuing.\n\n"
                           "The launcher will preserve the old JAR and instance settings. This does not back up or roll back worlds, "
                           "mods, plugins or their data. These files remain in the same server folder.")
                            .arg(current, target),
                        QMessageBox::Yes | QMessageBox::Cancel, this);
    warning.setDefaultButton(QMessageBox::Cancel);
    if (warning.exec() != QMessageBox::Yes)
        return;
    auto* task = new ServerChangeTask(m_instance, APPLICATION->network(), m_jar->text(), download);
    task->setParent(this);
    m_task = task;
    m_progress->setRange(0, 0);
    m_progress->show();
    connect(task, &Task::status, m_status, &QLabel::setText);
    connect(task, &Task::progress, this, [this](qint64 value, qint64 total) {
        m_progress->setRange(0, total > 0 ? 1000 : 0);
        if (total > 0)
            m_progress->setValue(static_cast<int>(1000.0 * value / total));
    });
    connect(task, &Task::finished, this, [this, task] {
        m_progress->hide();
        if (task->wasSuccessful()) {
            emit softwareChanged();
            QMessageBox::information(this, tr("Server software changed"),
                                     tr("The server software was changed. Check Java, mods, plugins and configuration before starting.\n\n"
                                        "Previous JAR and instance settings: %1")
                                         .arg(task->backupPath()));
            accept();
        } else if (task->getState() != Task::State::AbortedByUser) {
            m_status->setText(task->failReason());
            QMessageBox::warning(this, tr("Could not change server software"), task->failReason());
        }
        updateButtons();
    });
    task->start();
    updateButtons();
}
