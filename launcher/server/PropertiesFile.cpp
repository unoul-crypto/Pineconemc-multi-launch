// SPDX-License-Identifier: GPL-3.0-only
#include "PropertiesFile.h"
#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace {
bool white(QChar c)
{
    return c == ' ' || c == '\t' || c == '\f';
}
QString unescape(const QString& input)
{
    QString out;
    for (qsizetype i = 0; i < input.size(); ++i) {
        auto c = input[i];
        if (c != '\\' || i + 1 == input.size()) {
            out += c;
            continue;
        }
        c = input[++i];
        if (c == 'n')
            out += '\n';
        else if (c == 'r')
            out += '\r';
        else if (c == 't')
            out += '\t';
        else if (c == 'f')
            out += '\f';
        else if (c == 'u' && i + 4 < input.size()) {
            bool ok = false;
            const auto code = input.mid(i + 1, 4).toUShort(&ok, 16);
            if (ok) {
                out += QChar(code);
                i += 4;
            } else
                out += "\\u";
        } else
            out += c;
    }
    return out;
}
QByteArray escape(const QString& value)
{
    QByteArray out;
    for (auto c : value) {
        const auto code = c.unicode();
        if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else if (c == '\f')
            out += "\\f";
        else if (code < 32 || code > 126)
            out += QString("\\u%1").arg(static_cast<uint>(code), 4, 16, QLatin1Char('0')).toLatin1();
        else {
            if (QString("\\ =:#!").contains(c))
                out += '\\';
            out += static_cast<char>(code);
        }
    }
    return out;
}
}  // namespace

PropertiesFile PropertiesFile::parse(const QByteArray& contents)
{
    PropertiesFile file;
    if (contents.contains("\r\n"))
        file.m_newline = "\r\n";
    qsizetype offset = 0;
    while (offset < contents.size()) {
        Record record;
        QByteArray logical;
        bool continued = false;
        do {
            auto end = contents.indexOf('\n', offset);
            if (end < 0)
                end = contents.size() - 1;
            auto physical = contents.mid(offset, end - offset + 1);
            record.raw += physical;
            offset = end + 1;
            if (physical.endsWith('\n'))
                physical.chop(1);
            if (physical.endsWith('\r'))
                physical.chop(1);
            if (continued) {
                while (!physical.isEmpty() && (physical.front() == ' ' || physical.front() == '\t' || physical.front() == '\f'))
                    physical.remove(0, 1);
            }
            int slashes = 0;
            for (auto i = physical.size(); i > 0 && physical[i - 1] == '\\'; --i)
                ++slashes;
            const auto trimmed = physical.trimmed();
            const bool comment = logical.isEmpty() && (trimmed.startsWith('#') || trimmed.startsWith('!'));
            continued = !comment && slashes % 2 && offset < contents.size();
            if (continued)
                physical.chop(1);
            logical += physical;
        } while (continued);
        auto text = QString::fromUtf8(logical);
        qsizetype start = 0;
        while (start < text.size() && white(text[start]))
            ++start;
        if (start < text.size() && text[start] != '#' && text[start] != '!') {
            auto end = start;
            bool escaped = false;
            for (; end < text.size(); ++end) {
                if (!escaped && (white(text[end]) || text[end] == '=' || text[end] == ':'))
                    break;
                if (text[end] == '\\')
                    escaped = !escaped;
                else
                    escaped = false;
            }
            record.key = unescape(text.mid(start, end - start));
            auto pos = end;
            while (pos < text.size() && white(text[pos]))
                ++pos;
            if (pos < text.size() && (text[pos] == '=' || text[pos] == ':'))
                ++pos;
            while (pos < text.size() && white(text[pos]))
                ++pos;
            record.value = unescape(text.mid(pos));
        }
        file.m_records.append(record);
    }
    return file;
}

QString PropertiesFile::value(const QString& key, const QString& fallback) const
{
    for (auto i = m_records.crbegin(); i != m_records.crend(); ++i)
        if (i->key == key)
            return i->value;
    return fallback;
}

bool PropertiesFile::read(const QString& path, PropertiesFile* result, QString* error)
{
    QFile file(path);
    if (!file.exists()) {
        *result = {};
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    *result = parse(file.readAll());
    return true;
}

bool PropertiesFile::update(const QString& path, const QMap<QString, QString>& changes, QString* error)
{
    if (changes.isEmpty())
        return true;
    PropertiesFile file;
    if (!read(path, &file, error))
        return false;
    QByteArray contents;
    QSet<QString> written;
    for (const auto& record : file.m_records) {
        if (changes.contains(record.key)) {
            contents += escape(record.key) + '=' + escape(changes.value(record.key)) + file.m_newline;
            written.insert(record.key);
        } else
            contents += record.raw;
    }
    for (auto i = changes.cbegin(); i != changes.cend(); ++i) {
        if (written.contains(i.key()))
            continue;
        if (!contents.isEmpty() && !contents.endsWith('\n'))
            contents += file.m_newline;
        contents += escape(i.key()) + '=' + escape(i.value()) + file.m_newline;
    }
    if (QFile::exists(path)) {
        const auto backup = path + ".launcher-backup-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + "-" +
                            QUuid::createUuid().toString(QUuid::Id128).left(8);
        if (!QFile::copy(path, backup)) {
            if (error)
                *error = QObject::tr("Could not back up %1").arg(path);
            return false;
        }
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(contents) != contents.size() || !output.commit()) {
        if (error)
            *error = output.errorString();
        return false;
    }
    return true;
}
