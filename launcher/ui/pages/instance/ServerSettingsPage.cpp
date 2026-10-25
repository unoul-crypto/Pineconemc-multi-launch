// SPDX-License-Identifier: GPL-3.0-only
#include "ServerSettingsPage.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include "server/PropertiesFile.h"
#include "server/ServerInstance.h"
#include "ui/dialogs/ServerSoftwareDialog.h"
#include "ui/widgets/ServerOptionsWidget.h"

ServerSettingsPage::ServerSettingsPage(ServerInstance* instance, QWidget* parent) : QWidget(parent), m_instance(instance)
{
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Stop the server before changing settings. Properties not shown here are preserved. "
                               "Changes to server.properties take effect on the next start."),
                            this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_editable = new QWidget(scroll);
    auto* contents = new QVBoxLayout(m_editable);
    m_softwareLabel = new QLabel(m_editable);
    m_softwareLabel->setWordWrap(true);
    contents->addWidget(m_softwareLabel);
    auto* changeSoftware = new QPushButton(tr("Change software / version…"), m_editable);
    changeSoftware->setObjectName("changeServerSoftware");
    contents->addWidget(changeSoftware);
    connect(changeSoftware, &QPushButton::clicked, this, [this] {
        if (m_instance->isRunning() || m_instance->isChangingSoftware() || !apply())
            return;
        auto* dialog = new ServerSoftwareDialog(m_instance, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &ServerSoftwareDialog::softwareChanged, this, &ServerSettingsPage::load);
        dialog->show();
    });
    m_options = new ServerOptionsWidget(m_editable);
    m_options->setJarEditable(false);
    contents->addWidget(m_options);
    auto* properties = new QGroupBox("server.properties", m_editable);
    auto* form = new QFormLayout(properties);
    auto text = [this, form, properties](const QString& key, const QString& title) {
        auto* editor = new QLineEdit(properties);
        editor->setObjectName(key);
        form->addRow(title, editor);
        m_properties.insert(key, editor);
    };
    auto number = [this, form, properties](const QString& key, const QString& title, int min, int max) {
        auto* editor = new QSpinBox(properties);
        editor->setRange(min, max);
        editor->setObjectName(key);
        form->addRow(title, editor);
        m_properties.insert(key, editor);
    };
    auto choice = [this, form, properties](const QString& key, const QString& title, const QStringList& values) {
        auto* editor = new QComboBox(properties);
        editor->setObjectName(key);
        editor->addItems(values);
        form->addRow(title, editor);
        m_properties.insert(key, editor);
    };
    auto boolean = [this, form, properties](const QString& key, const QString& title) {
        auto* editor = new QCheckBox(title, properties);
        editor->setObjectName(key);
        form->addRow(editor);
        m_properties.insert(key, editor);
    };
    text("motd", tr("Server description:"));
    number("server-port", tr("Port:"), 1, 65535);
    number("max-players", tr("Maximum players:"), 1, 100000);
    choice("gamemode", tr("Default game mode:"), { "survival", "creative", "adventure", "spectator" });
    choice("difficulty", tr("Difficulty:"), { "peaceful", "easy", "normal", "hard" });
    number("view-distance", tr("View distance (chunks):"), 2, 32);
    number("spawn-protection", tr("Spawn protection (blocks):"), 0, 100000);
    boolean("pvp", tr("Player versus player damage"));
    boolean("white-list", tr("Use whitelist"));
    boolean("online-mode", tr("Verify player accounts (online-mode)"));
    contents->addWidget(properties);
    contents->addStretch();
    scroll->setWidget(m_editable);
    layout->addWidget(scroll);
    connect(instance, &BaseInstance::runningStatusChanged, this, [this](bool running) {
        m_editable->setEnabled(!running && !m_instance->isChangingSoftware());
        if (!running)
            load();
    });
    connect(instance, &ServerInstance::softwareChangeChanged, this, [this](bool changing) {
        m_editable->setEnabled(!changing && !m_instance->isRunning());
        if (!changing)
            load();
    });
    load();
}
void ServerSettingsPage::openedImpl()
{
    load();
}
void ServerSettingsPage::load()
{
    auto* settings = m_instance->settings();
    auto software = m_instance->getStatusbarDescription();
    if (!settings->get("ServerBuild").toString().isEmpty())
        software += tr(" · build %1").arg(settings->get("ServerBuild").toString());
    if (!settings->get("ServerLoaderVersion").toString().isEmpty())
        software += tr(" · loader %1").arg(settings->get("ServerLoaderVersion").toString());
    m_softwareLabel->setText(software);
    m_options->setOptions({ m_instance->jarPath(), settings->get("ServerJavaPath").toString(), settings->get("ServerMinMem").toInt(),
                            settings->get("ServerMaxMem").toInt(), settings->get("ServerArgs").toString(), m_instance->eulaAccepted() });
    m_loadedOptions = m_options->options();
    m_editable->setEnabled(!m_instance->isRunning() && !m_instance->isChangingSoftware());
    PropertiesFile file;
    QString error;
    m_loadedSuccessfully = PropertiesFile::read(QDir(m_instance->gameRoot()).filePath("server.properties"), &file, &error);
    if (!m_loadedSuccessfully) {
        QMessageBox::warning(this, tr("Could not read server.properties"), error);
        return;
    }
    const QMap<QString, QString> defaults{ { "motd", "A Minecraft Server" }, { "server-port", "25565" }, { "max-players", "20" },
                                           { "gamemode", "survival" },       { "difficulty", "easy" },   { "view-distance", "10" },
                                           { "spawn-protection", "16" },     { "pvp", "true" },          { "white-list", "false" },
                                           { "online-mode", "true" } };
    for (auto i = m_properties.cbegin(); i != m_properties.cend(); ++i) {
        const auto value = file.value(i.key(), defaults.value(i.key()));
        if (auto* text = qobject_cast<QLineEdit*>(i.value()))
            text->setText(value);
        else if (auto* number = qobject_cast<QSpinBox*>(i.value())) {
            bool ok = false;
            const auto parsed = value.toInt(&ok);
            number->setValue(ok ? parsed : defaults.value(i.key()).toInt());
        } else if (auto* boolean = qobject_cast<QCheckBox*>(i.value()))
            boolean->setChecked(value.compare("true", Qt::CaseInsensitive) == 0);
        else if (auto* choice = qobject_cast<QComboBox*>(i.value())) {
            auto index = choice->findText(value);
            if (index < 0) {
                bool ok = false;
                const auto numeric = value.toInt(&ok);
                if (ok && numeric >= 0 && numeric < choice->count())
                    index = numeric;
            }
            choice->setCurrentIndex(index < 0 ? choice->findText(defaults.value(i.key())) : index);
        }
    }
    m_loaded = propertyValues();
}
QMap<QString, QString> ServerSettingsPage::propertyValues() const
{
    QMap<QString, QString> values;
    for (auto i = m_properties.cbegin(); i != m_properties.cend(); ++i) {
        if (auto* text = qobject_cast<QLineEdit*>(i.value()))
            values.insert(i.key(), text->text());
        else if (auto* number = qobject_cast<QSpinBox*>(i.value()))
            values.insert(i.key(), QString::number(number->value()));
        else if (auto* boolean = qobject_cast<QCheckBox*>(i.value()))
            values.insert(i.key(), boolean->isChecked() ? "true" : "false");
        else if (auto* choice = qobject_cast<QComboBox*>(i.value()))
            values.insert(i.key(), choice->currentText());
    }
    return values;
}
bool ServerSettingsPage::apply()
{
    if (m_instance->isRunning() || m_instance->isChangingSoftware())
        return true;
    if (!m_loadedSuccessfully)
        return false;
    const auto options = m_options->options();
    const auto values = propertyValues();
    if (values == m_loaded && options.jar == m_loadedOptions.jar && options.java == m_loadedOptions.java &&
        options.minMemory == m_loadedOptions.minMemory && options.maxMemory == m_loadedOptions.maxMemory &&
        options.arguments == m_loadedOptions.arguments && options.acceptEula == m_loadedOptions.acceptEula)
        return true;
    QString error = m_options->validationError();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Server settings"), error);
        return false;
    }
    QMap<QString, QString> changes;
    for (auto i = values.cbegin(); i != values.cend(); ++i)
        if (m_loaded.value(i.key()) != i.value())
            changes.insert(i.key(), i.value());
    if (!m_instance->importJar(options.jar, &error) || !m_instance->setEulaAccepted(options.acceptEula, &error) ||
        !PropertiesFile::update(QDir(m_instance->gameRoot()).filePath("server.properties"), changes, &error)) {
        QMessageBox::warning(this, tr("Could not save server settings"), error);
        return false;
    }
    auto* settings = m_instance->settings();
    {
        SettingsObject::Lock lock(settings);
        settings->set("ServerJavaPath", options.java);
        settings->set("ServerMinMem", options.minMemory);
        settings->set("ServerMaxMem", options.maxMemory);
        settings->set("ServerArgs", options.arguments);
    }
    m_loaded = values;
    m_options->setOptions(
        { m_instance->jarPath(), options.java, options.minMemory, options.maxMemory, options.arguments, options.acceptEula });
    m_loadedOptions = m_options->options();
    return true;
}
