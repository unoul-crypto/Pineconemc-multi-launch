// SPDX-License-Identifier: GPL-3.0-only
#include "ServerOptionsWidget.h"
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

ServerOptionsWidget::ServerOptionsWidget(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QFormLayout(this);
    auto pathRow = [this, layout](const QString& label, const QString& name, bool jar) {
        auto* row = new QWidget(this);
        auto* line = new QLineEdit(row);
        line->setObjectName(name);
        auto* browse = new QPushButton(tr("Browse…"), row);
        auto* horizontal = new QHBoxLayout(row);
        horizontal->setContentsMargins(0, 0, 0, 0);
        horizontal->addWidget(line, 1);
        horizontal->addWidget(browse);
        connect(browse, &QPushButton::clicked, this, [this, line, jar] {
            const auto file = QFileDialog::getOpenFileName(this, jar ? tr("Select runnable server JAR") : tr("Select Java executable"),
                                                           line->text(), jar ? tr("JAR files (*.jar)") : tr("All files (*)"));
            if (!file.isEmpty())
                line->setText(file);
        });
        connect(line, &QLineEdit::textChanged, this, &ServerOptionsWidget::changed);
        layout->addRow(label, row);
        return line;
    };
    m_jar = pathRow(tr("Server JAR:"), "serverJarPath", true);
    m_java = pathRow(tr("Java executable:"), "serverJavaPath", false);
    auto* memory = new QWidget(this);
    auto* memoryLayout = new QHBoxLayout(memory);
    memoryLayout->setContentsMargins(0, 0, 0, 0);
    m_minMemory = new QSpinBox(memory);
    m_maxMemory = new QSpinBox(memory);
    m_minMemory->setObjectName("serverMinMemory");
    m_maxMemory->setObjectName("serverMaxMemory");
    for (auto* box : { m_minMemory, m_maxMemory }) {
        box->setRange(128, 1048576);
        box->setSuffix(tr(" MiB"));
        box->setSingleStep(256);
        connect(box, &QSpinBox::valueChanged, this, &ServerOptionsWidget::changed);
    }
    memoryLayout->addWidget(new QLabel(tr("Minimum"), memory));
    memoryLayout->addWidget(m_minMemory);
    memoryLayout->addWidget(new QLabel(tr("Maximum"), memory));
    memoryLayout->addWidget(m_maxMemory);
    layout->addRow(tr("Memory:"), memory);
    m_arguments = new QLineEdit(this);
    m_arguments->setObjectName("serverArguments");
    layout->addRow(tr("Server arguments:"), m_arguments);
    connect(m_arguments, &QLineEdit::textChanged, this, &ServerOptionsWidget::changed);
    m_eula = new QCheckBox(tr("I have read and accept the Minecraft EULA"), this);
    m_eula->setObjectName("serverEulaAccepted");
    layout->addRow(m_eula);
    auto* link = new QLabel("<a href=\"https://www.minecraft.net/eula\">Minecraft EULA</a>", this);
    link->setOpenExternalLinks(true);
    layout->addRow(link);
    connect(m_eula, &QCheckBox::toggled, this, &ServerOptionsWidget::changed);
    setOptions({});
}
ServerOptions ServerOptionsWidget::options() const
{
    return { m_jar->text().trimmed(), m_java->text().trimmed(), m_minMemory->value(),
             m_maxMemory->value(),    m_arguments->text(),      m_eula->isChecked() };
}
void ServerOptionsWidget::setOptions(const ServerOptions& options)
{
    m_jar->setText(options.jar);
    m_java->setText(options.java);
    m_minMemory->setValue(options.minMemory);
    m_maxMemory->setValue(options.maxMemory);
    m_arguments->setText(options.arguments);
    m_eula->setChecked(options.acceptEula);
}
QString ServerOptionsWidget::validationError() const
{
    const auto value = options();
    if (m_localJarRequired && (!QFileInfo(value.jar).isFile() || QFileInfo(value.jar).suffix().compare("jar", Qt::CaseInsensitive) != 0))
        return tr("Select a runnable server .jar file.");
    if (value.java.isEmpty())
        return tr("Select Java or enter its executable name, for example java.");
    if (value.maxMemory < value.minMemory)
        return tr("Maximum memory must not be below minimum memory.");
    return {};
}
void ServerOptionsWidget::setLocalJarRequired(bool required)
{
    m_localJarRequired = required;
    auto* row = m_jar->parentWidget();
    row->setVisible(required);
    if (auto* label = qobject_cast<QFormLayout*>(layout())->labelForField(row))
        label->setVisible(required);
    emit changed();
}
void ServerOptionsWidget::setJarEditable(bool editable)
{
    m_jar->setReadOnly(!editable);
    if (auto* browse = m_jar->parentWidget()->findChild<QPushButton*>())
        browse->setVisible(editable);
}
