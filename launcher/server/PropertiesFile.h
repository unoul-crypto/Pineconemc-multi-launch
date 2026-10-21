// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>

// Java .properties, not INI: preserve unknown settings, comments and original bytes.
class PropertiesFile {
   public:
    static PropertiesFile parse(const QByteArray& contents);
    QString value(const QString& key, const QString& fallback = {}) const;
    static bool update(const QString& path, const QMap<QString, QString>& changes, QString* error = nullptr);
    static bool read(const QString& path, PropertiesFile* result, QString* error = nullptr);

   private:
    struct Record {
        QByteArray raw;
        QString key;
        QString value;
    };
    QList<Record> m_records;
    QByteArray m_newline = "\n";
};
