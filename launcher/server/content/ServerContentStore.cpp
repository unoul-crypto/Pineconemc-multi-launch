// SPDX-License-Identifier: GPL-3.0-only
#include "ServerContentStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

using namespace ServerContent;
namespace {
QString journalPath(const QString& root)
{
    return QDir(root).filePath("server-content-transaction.json");
}
QByteArray bytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.size() <= 16 * 1024 * 1024 ? file.readAll() : QByteArray();
}
QString hash(const QString& path)
{
    QFile file(path);
    QCryptographicHash digest(QCryptographicHash::Sha512);
    if (!file.open(QIODevice::ReadOnly) || !digest.addData(&file))
        return {};
    return QString::fromLatin1(digest.result().toHex());
}
bool write(const QString& path, const QByteArray& data)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
bool copy(const QString& source, const QString& destination)
{
    QFile input(source);
    QSaveFile output(destination);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
        return false;
    while (!input.atEnd()) {
        const auto data = input.read(1024 * 1024);
        if ((data.isEmpty() && input.error() != QFile::NoError) || output.write(data) != data.size())
            return false;
    }
    return output.commit();
}
bool plainFile(const QString& path)
{
    const QFileInfo info(path);
    return !info.isSymLink() && (!info.exists() || info.isFile());
}
bool safeDirectory(const QString& parent, const QString& name)
{
    const auto expected = QDir(QDir(parent).canonicalPath()).filePath(name);
    const auto path = QDir(parent).filePath(name);
    if (!QDir().mkpath(path))
        return false;
    auto actual = QFileInfo(path).canonicalFilePath();
#ifdef Q_OS_WIN
    return actual.compare(expected, Qt::CaseInsensitive) == 0;
#else
    return actual == expected;
#endif
}
QJsonObject entry(const Installed& item)
{
    QJsonArray dependencies;
    for (const auto& dependency : item.dependencies)
        dependencies.append(
            QJsonObject{ { "project", dependency.projectId }, { "version", dependency.versionId }, { "type", dependency.type } });
    return { { "project", item.projectId }, { "version", item.versionId }, { "name", item.name },     { "filename", item.filename },
             { "sha512", item.sha512 },     { "kind", folder(item.kind) }, { "number", item.number }, { "dependencies", dependencies } };
}
QString key(Kind kind, const QString& filename)
{
    return folder(kind) + "/" + filename.toCaseFolded();
}
QString contentPath(const QString& root, const QJsonObject& operation)
{
    return QDir(root).filePath("server/" + operation.value("kind").toString() + "/" + operation.value("filename").toString());
}
bool validOperation(const QJsonObject& operation)
{
    return QStringList{ "mods", "plugins" }.contains(operation.value("kind").toString()) &&
           safeFilename(operation.value("filename").toString()) &&
           (operation.value("before").toString().isEmpty() || validHash(operation.value("before").toString())) &&
           (operation.value("after").toString().isEmpty() || validHash(operation.value("after").toString()));
}
ServerContentStore::Result failure(const QString& error, const QString& backup = {})
{
    return { false, error, backup };
}
}  // namespace
namespace ServerContentStore {
QString manifestPath(const QString& root)
{
    return QDir(root).filePath("server-content.json");
}
bool interrupted(const QString& root)
{
    return QFileInfo::exists(journalPath(root));
}
bool read(const QString& root, QList<Installed>* installed, QString* error)
{
    installed->clear();
    const auto path = manifestPath(root);
    if (!QFileInfo::exists(path))
        return true;
    const auto document = QJsonDocument::fromJson(bytes(path));
    if (!plainFile(path) || !document.isObject() || document.object().value("format") != 1 ||
        !document.object().value("installed").isArray()) {
        *error = QObject::tr("Could not read server-content.json. Restore its backup before installing content.");
        return false;
    }
    QSet<QString> projects, filenames;
    for (const auto& value : document.object().value("installed").toArray()) {
        const auto object = value.toObject();
        const auto kind = object.value("kind").toString();
        Installed item{ object.value("project").toString(),  object.value("version").toString(), object.value("name").toString(),
                        object.value("filename").toString(), object.value("sha512").toString(),  kind == "mods" ? Kind::Mod : Kind::Plugin,
                        object.value("number").toString() };
        for (const auto& dependency : object.value("dependencies").toArray()) {
            const auto dep = dependency.toObject();
            item.dependencies.append({ dep.value("project").toString(), dep.value("version").toString(), dep.value("type").toString() });
        }
        if (!QStringList{ "mods", "plugins" }.contains(kind) || !validId(item.projectId) || !validId(item.versionId) ||
            !safeFilename(item.filename) || !validHash(item.sha512) || projects.contains(item.projectId) ||
            filenames.contains(key(item.kind, item.filename))) {
            *error = QObject::tr("The installed server content index is invalid. Existing files were not changed.");
            return false;
        }
        projects.insert(item.projectId);
        filenames.insert(key(item.kind, item.filename));
        installed->append(item);
    }
    return true;
}
Result recover(const QString& root)
{
    if (!interrupted(root))
        return { true, {}, {} };
    const auto journal = QJsonDocument::fromJson(bytes(journalPath(root))).object();
    const auto backup = journal.value("backup").toString();
    static const QRegularExpression backupName(QStringLiteral("^server-content-backups/[A-Za-z0-9-]+$"));
    if (!plainFile(journalPath(root)) || journal.value("format") != 1 || !journal.value("operations").isArray() ||
        !backupName.match(backup).hasMatch() || !safeDirectory(root, "server") || !safeDirectory(root, "server-content-backups") ||
        !safeDirectory(QDir(root).filePath("server-content-backups"), QFileInfo(backup).fileName()))
        return failure(QObject::tr("The interrupted content transaction cannot be read safely. Restore the content backup manually."));
    const auto backupRoot = QDir(root).filePath(backup);
    const auto operations = journal.value("operations").toArray();
    for (int i = operations.size() - 1; i >= 0; --i) {
        const auto operation = operations[i].toObject();
        if (!validOperation(operation) || !safeDirectory(QDir(root).filePath("server"), operation.value("kind").toString()))
            return failure(QObject::tr("Unsafe content path in the interrupted installation."), backupRoot);
        const auto path = contentPath(root, operation);
        if (!plainFile(path))
            return failure(QObject::tr("A content file was replaced by a directory or symbolic link."), backupRoot);
        const auto before = operation.value("before").toString();
        const auto after = operation.value("after").toString();
        const auto current = QFileInfo::exists(path) ? hash(path) : QString();
        if (current == before)
            continue;
        if ((!current.isEmpty() && current != after) || (QFileInfo::exists(path) && current.isEmpty()))
            return failure(QObject::tr("%1 changed after the interrupted installation. Restore it from %2 manually.").arg(path, backupRoot),
                           backupRoot);
        if (before.isEmpty()) {
            if (QFileInfo::exists(path) && !QFile::remove(path))
                return failure(QObject::tr("Could not roll back %1.").arg(path), backupRoot);
        } else {
            const auto source = QDir(backupRoot).filePath(QString::number(i) + ".jar");
            if (hash(source) != before || !copy(source, path))
                return failure(QObject::tr("Could not restore %1 from the content backup.").arg(path), backupRoot);
        }
    }
    const auto manifest = manifestPath(root);
    const auto old = QByteArray::fromBase64(journal.value("oldManifest").toString().toLatin1());
    const auto next = QByteArray::fromBase64(journal.value("newManifest").toString().toLatin1());
    const auto current = bytes(manifest);
    if (!plainFile(manifest) || (current != old && current != next))
        return failure(QObject::tr("The server content index changed after the interruption. Restore its backup manually."), backupRoot);
    if (journal.value("hadManifest").toBool() ? !write(manifest, old) : QFileInfo::exists(manifest) && !QFile::remove(manifest))
        return failure(QObject::tr("Could not restore the server content index."), backupRoot);
    if (!QFile::remove(journalPath(root)))
        return failure(QObject::tr("Could not finish content recovery."), backupRoot);
    return { true, {}, backupRoot };
}
Result install(const QString& root, const QList<AddonVersion>& plan, const QStringList& staged)
{
    auto recovery = recover(root);
    if (!recovery.success || plan.isEmpty())
        return recovery;
    if (plan.size() != staged.size() || !QDir(root).exists() || !safeDirectory(root, "server"))
        return failure(QObject::tr("Invalid content installation plan or server folder."));
    QList<Installed> installed;
    QString error;
    if (!read(root, &installed, &error))
        return failure(error);
    const auto previous = installed;
    installed.removeIf([&root](const auto& item) {
        const QFileInfo file(QDir(root).filePath("server/" + folder(item.kind) + "/" + item.filename));
        return !file.exists() && !file.isSymLink();
    });
    QMap<QString, QJsonObject> changes;
    QMap<QString, QString> sources, owners;
    for (const auto& item : installed)
        owners.insert(key(item.kind, item.filename), item.projectId);
    QSet<QString> projects;
    for (int i = 0; i < plan.size(); ++i) {
        const auto& version = plan[i];
        if (!validId(version.id) || !validId(version.projectId) || !safeFilename(version.filename) || !validHash(version.sha512) ||
            projects.contains(version.projectId) || hash(staged[i]) != version.sha512 ||
            !safeDirectory(QDir(root).filePath("server"), folder(version.kind)))
            return failure(QObject::tr("The staged content or installation plan is invalid."));
        projects.insert(version.projectId);
        const auto newKey = key(version.kind, version.filename);
        if (owners.contains(newKey) && owners.value(newKey) != version.projectId)
            return failure(QObject::tr("Two projects use the same filename: %1.").arg(version.filename));
        for (const auto& old : installed) {
            if (old.projectId != version.projectId)
                continue;
            const auto path = QDir(root).filePath("server/" + folder(old.kind) + "/" + old.filename);
            if (old.kind != version.kind) {
                if (QFileInfo::exists(path) || QFileInfo(path).isSymLink())
                    return failure(QObject::tr("This project is installed as another content type."));
                continue;
            }
            if (!plainFile(path) || (QFileInfo::exists(path) && hash(path) != old.sha512))
                return failure(
                    QObject::tr("%1 was modified outside the launcher. Move it aside before changing this project.").arg(old.filename));
            if (old.filename != version.filename && key(old.kind, old.filename) == newKey &&
                !QFileInfo::exists(QDir(root).filePath("server/" + folder(version.kind) + "/" + version.filename)))
                return failure(QObject::tr("A filename differs only by letter case. Rename the existing file before installing."));
            if (key(old.kind, old.filename) != newKey && QFileInfo::exists(path))
                changes.insert(key(old.kind, old.filename),
                               { { "kind", folder(old.kind) }, { "filename", old.filename }, { "before", old.sha512 }, { "after", "" } });
        }
        const auto destination = QDir(root).filePath("server/" + folder(version.kind) + "/" + version.filename);
        if (!plainFile(destination))
            return failure(QObject::tr("The destination is not a regular file: %1.").arg(version.filename));
        const auto before = QFileInfo::exists(destination) ? hash(destination) : QString();
        if (QFileInfo::exists(destination) && before != version.sha512 && owners.value(newKey) != version.projectId)
            return failure(
                QObject::tr("%1 already exists and is not managed by this project. It was not overwritten.").arg(version.filename));
        if (before != version.sha512) {
            changes.insert(
                newKey,
                { { "kind", folder(version.kind) }, { "filename", version.filename }, { "before", before }, { "after", version.sha512 } });
            sources.insert(newKey, staged[i]);
        }
        owners.insert(newKey, version.projectId);
        installed.removeIf([&version](const auto& old) { return old.projectId == version.projectId; });
        installed.append({ version.projectId, version.id, version.name, version.filename, version.sha512, version.kind, version.number,
                           version.dependencies });
    }
    if (!validateConstraints(installed, previous, &error))
        return failure(error);
    if (!safeDirectory(root, "server-content-backups"))
        return failure(QObject::tr("Could not create a safe server content backup folder."));
    const auto backupRelative = "server-content-backups/" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + "-" +
                                QUuid::createUuid().toString(QUuid::Id128);
    const auto backup = QDir(root).filePath(backupRelative);
    if (!QDir().mkpath(backup))
        return failure(QObject::tr("Could not create the content backup."));
    QJsonArray operations, entries;
    for (const auto& item : installed)
        entries.append(entry(item));
    const auto manifest = manifestPath(root);
    if (!plainFile(manifest))
        return failure(QObject::tr("The server content index is not a regular file."));
    const auto oldManifest = bytes(manifest);
    const auto newManifest = QJsonDocument(QJsonObject{ { "format", 1 }, { "installed", entries } }).toJson();
    if (QFileInfo::exists(manifest) && !write(QDir(backup).filePath("server-content.json"), oldManifest))
        return failure(QObject::tr("Could not back up the server content index."), backup);
    for (const auto& operation : changes) {
        const auto before = operation.value("before").toString();
        if (!before.isEmpty()) {
            const auto copyPath = QDir(backup).filePath(QString::number(operations.size()) + ".jar");
            if (!copy(contentPath(root, operation), copyPath) || hash(copyPath) != before)
                return failure(QObject::tr("Could not back up the previous content files."), backup);
        }
        operations.append(operation);
    }
    const QJsonObject journal{ { "format", 1 },
                               { "backup", backupRelative },
                               { "operations", operations },
                               { "hadManifest", QFileInfo::exists(manifest) },
                               { "oldManifest", QString::fromLatin1(oldManifest.toBase64()) },
                               { "newManifest", QString::fromLatin1(newManifest.toBase64()) } };
    if (!write(journalPath(root), QJsonDocument(journal).toJson()))
        return failure(QObject::tr("Could not save the content recovery journal. No content files were changed."), backup);
    auto rollback = [&root, &backup](const QString& reason) {
        const auto restored = recover(root);
        return failure(restored.success ? reason : reason + "\n" + restored.error, backup);
    };
    for (const auto& value : operations) {
        const auto operation = value.toObject();
        if (!safeDirectory(root, "server") || !safeDirectory(QDir(root).filePath("server"), operation.value("kind").toString()))
            return rollback(QObject::tr("A server content folder changed during installation."));
        const auto path = contentPath(root, operation);
        const auto before = operation.value("before").toString();
        if (!plainFile(path) || (QFileInfo::exists(path) ? hash(path) : QString()) != before)
            return rollback(QObject::tr("A content file changed during installation."));
        if (operation.value("after").toString().isEmpty()) {
            if (!QFile::remove(path))
                return rollback(QObject::tr("Could not remove the previous content version."));
        } else {
            const auto kind = operation.value("kind") == "mods" ? Kind::Mod : Kind::Plugin;
            if (!copy(sources.value(key(kind, operation.value("filename").toString())), path))
                return rollback(QObject::tr("Could not install a content file."));
        }
    }
    if (!plainFile(manifest) || bytes(manifest) != oldManifest || !write(manifest, newManifest))
        return rollback(QObject::tr("Could not save the server content index."));
    if (!QFile::remove(journalPath(root)))
        return rollback(QObject::tr("Could not finish the content installation."));
    return { true, {}, backup };
}
}  // namespace ServerContentStore
