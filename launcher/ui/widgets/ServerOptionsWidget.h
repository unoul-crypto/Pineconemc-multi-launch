// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QWidget>
#include "server/ServerOptions.h"
class QLineEdit;
class QSpinBox;
class QCheckBox;
class ServerOptionsWidget : public QWidget {
    Q_OBJECT
   public:
    explicit ServerOptionsWidget(QWidget* parent = nullptr);
    ServerOptions options() const;
    void setOptions(const ServerOptions& options);
    QString validationError() const;
    void setLocalJarRequired(bool required);
    void setJarEditable(bool editable);
   signals:
    void changed();

   private:
    QLineEdit* m_jar;
    QLineEdit* m_java;
    QSpinBox* m_minMemory;
    QSpinBox* m_maxMemory;
    QLineEdit* m_arguments;
    QCheckBox* m_eula;
    bool m_localJarRequired = true;
};
