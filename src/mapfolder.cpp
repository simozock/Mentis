#include "mapfolder.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QVariantMap>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

const QString kExtension = QStringLiteral(".mentis");
const QString kPartial = QStringLiteral(".part");
const QString kSettingsKey = QStringLiteral("mentisFolder");
const QString kPendingMovesKey = QStringLiteral("mentisFolderPendingMoves");  // map -> target folder
const QString kCopiesGroup = QStringLiteral("mentisFolderCopies/");  // original's SHA-256 -> its copy
const QString kFolderFile = QStringLiteral(".mentis-folder");  // FEAT-107: the folder's colour

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

QByteArray sha256Of(QFile &file) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) ? hash.result() : QByteArray();
}

// Writing a file is the only reliable test: QFileInfo::isWritable ignores the Windows ACLs unless
// qt_ntfs_permission_lookup is on (https://doc.qt.io/qt-6/qfileinfo.html#ntfs-permissions).
bool writableDirectory(const QString &path) {
    QTemporaryFile probe(QDir(path).filePath(QStringLiteral(".mentis-write-test-XXXXXX")));
    return probe.open();
}

bool usable(const QString &path, QString *warning) {
    const QFileInfo info(path);
    if (!info.exists()) {
        // A missing folder is made again only when its parent is there (not on a removed disk).
        if (!info.absoluteDir().exists() || !QDir().mkdir(info.absoluteFilePath())) {
            *warning = MapFolder::tr("The Mentis folder %1 cannot be reached.").arg(QDir::toNativeSeparators(path));
            return false;
        }
    } else if (!info.isDir() || !writableDirectory(path)) {
        *warning = MapFolder::tr("The Mentis folder %1 cannot be written.").arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

// Characters not allowed in Windows file names (the strictest of the three systems), control
// characters included, replaced; no trailing dots (Windows drops them); at most 120 characters.
QString cleanName(const QString &name) {
    static const QRegularExpression notAllowed(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    QString cleaned = name.simplified().replace(notAllowed, QStringLiteral("-")).trimmed();
    while (cleaned.endsWith(QLatin1Char('.'))) cleaned.chop(1);
    if (cleaned.size() > 120) cleaned = cleaned.left(120).trimmed();  // well below the 255 of a file name
    return cleaned;
}

QString freeFolderName(const QDir &parent, const QString &name) {
    if (!parent.exists(name)) return name;
    for (int number = 2;; ++number) {
        const QString candidate = QStringLiteral("%1 (%2)").arg(name).arg(number);
        if (!parent.exists(candidate)) return candidate;
    }
}

}  // namespace

QString MapFolder::moved(const QString &path, const QString &from, const QString &to) {
    const QString clean = QDir::cleanPath(path);
    const QString source = QDir::cleanPath(from);
    if (clean.compare(source, kPathCase) == 0) return QDir::cleanPath(to);
    if (clean.startsWith(source + QLatin1Char('/'), kPathCase)) return QDir::cleanPath(to) + clean.mid(source.size());
    return path;
}

namespace {

bool samePathOrInside(const QString &path, const QString &place) {
    const QString clean = QDir::cleanPath(path);
    const QString root = QDir::cleanPath(place);
    return clean.compare(root, kPathCase) == 0 || clean.startsWith(root + QLatin1Char('/'), kPathCase);
}

// A map inside the folder (at any depth) open in a Mentis window. The iterator is gone on return:
// on Windows its open directory handle would block a rename of the folder.
bool holdsOpenMap(const QString &folder) {
    QDirIterator maps(folder, {QLatin1Char('*') + kExtension}, QDir::Files, QDirIterator::Subdirectories);
    while (maps.hasNext())
        if (MapFolder::isOpen(maps.next())) return true;
    return false;
}

// Index entries pointing at place or inside it: stale once something new is written there (a copy
// deleted, then its name taken by another map, would be opened for the original otherwise).
void forgetCopiesAt(const QString &place) {
    QSettings settings;
    settings.beginGroup(kCopiesGroup.chopped(1));
    for (const QString &key : settings.childKeys())
        if (samePathOrInside(settings.value(key).toString(), place)) settings.remove(key);
}

// The copies brought in and the deferred moves follow a map or folder moved inside the Mentis folder.
void followMove(const QString &from, const QString &to) {
    forgetCopiesAt(to);  // to was free: whatever the index still says about it is stale
    QSettings settings;
    settings.beginGroup(kCopiesGroup.chopped(1));
    for (const QString &key : settings.childKeys()) {
        const QString copy = settings.value(key).toString();
        const QString now = MapFolder::moved(copy, from, to);
        if (now != copy) settings.setValue(key, now);
    }
    settings.endGroup();
    const QVariantMap pending = settings.value(kPendingMovesKey).toMap();
    if (pending.isEmpty()) return;
    QVariantMap followed;  // a renamed folder keeps the moves waiting for it
    for (auto entry = pending.cbegin(); entry != pending.cend(); ++entry)
        followed.insert(MapFolder::moved(entry.key(), from, to), MapFolder::moved(entry.value().toString(), from, to));
    if (followed != pending) settings.setValue(kPendingMovesKey, followed);
}

// A map and the SQLite files beside it: a journal left by a crash is the map's rollback data
// (https://www.sqlite.org/tempfiles.html), separated from the map it would be lost.
bool renameMap(const QString &from, const QString &to) {
    if (!QFile::rename(from, to)) return false;
    for (const QString &suffix : {QStringLiteral("-journal"), QStringLiteral("-wal"), QStringLiteral("-shm")}) {
        if (QFileInfo::exists(from + suffix) && !QFile::rename(from + suffix, to + suffix)) {
            QFile::rename(to, from);  // together or not at all
            return false;
        }
    }
    return true;
}

// One level only: the folder may be a large tree the user chose (Documents); each level is cleaned
// when it is listed (maps()).
void removePartials(const QString &folder) {
    for (const QFileInfo &partial : QDir(folder).entryInfoList({QLatin1Char('*') + kExtension + kPartial}, QDir::Files | QDir::Hidden))
        QFile::remove(partial.absoluteFilePath());
}

}  // namespace

QString MapFolder::configuredPath() { return QSettings().value(kSettingsKey).toString(); }

void MapFolder::setConfiguredPath(const QString &path) {
    QSettings settings;
    if (path.isEmpty() || QDir::cleanPath(path) == QDir::cleanPath(defaultPath())) settings.remove(kSettingsKey);
    else settings.setValue(kSettingsKey, QDir::cleanPath(path));
}

#ifdef Q_OS_ANDROID
namespace {

constexpr jint kAllFilesAccessSdk = 30;  // Android 11: MANAGE_EXTERNAL_STORAGE

jint sdkLevel() { return QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT"); }

QString mentisIn(const QJniObject &directory) {
    if (!directory.isValid()) return {};
    return directory.callObjectMethod("getAbsolutePath", "()Ljava/lang/String;").toString() + QStringLiteral("/Mentis");
}

// Mentis in the app's own folder on the shared storage (Context.getExternalFilesDir): no permission;
// empty when there is no shared storage (getExternalFilesDir returns null).
// https://developer.android.com/training/data-storage/app-specific#external
QString appFolderPath() {
    const QJniObject context(QNativeInterface::QAndroidApplication::context().object());
    return mentisIn(context.callObjectMethod("getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;", static_cast<jstring>(nullptr)));
}

QString internalPath() { return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Mentis"); }

}  // namespace
#endif

bool MapFolder::sharedStorageAllowed() {
#ifdef Q_OS_ANDROID
    // https://developer.android.com/training/data-storage/manage-all-files; before Android 11 the
    // storage permission, with requestLegacyExternalStorage on Android 10 (manifest).
    if (sdkLevel() >= kAllFilesAccessSdk)
        return QJniObject::callStaticMethod<jboolean>("android/os/Environment", "isExternalStorageManager", "()Z");
    const QJniObject context(QNativeInterface::QAndroidApplication::context().object());
    constexpr jint kGranted = 0;  // PackageManager.PERMISSION_GRANTED
    return context.callMethod<jint>("checkSelfPermission", "(Ljava/lang/String;)I",
                                    QJniObject::fromString(QStringLiteral("android.permission.WRITE_EXTERNAL_STORAGE")).object<jstring>()) == kGranted;
#else
    return false;
#endif
}

QString MapFolder::defaultPath() {
#ifdef Q_OS_ANDROID
    // FEAT-105 decision 1 (user, 2026-11-11): "Internal storage/Mentis", the root of the shared
    // storage, seen by every file manager and kept when the app is removed; it needs the access to
    // all files, asked on the Home. Until it is given, the app's own folder.
    if (sharedStorageAllowed())
        return mentisIn(QJniObject::callStaticObjectMethod("android/os/Environment", "getExternalStorageDirectory", "()Ljava/io/File;"));
    const QString app = appFolderPath();
    return app.isEmpty() ? internalPath() : app;
#else
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Mentis");
#endif
}

QString MapFolder::fallbackPath() {
#ifdef Q_OS_ANDROID
    const QString app = sharedStorageAllowed() ? appFolderPath() : QString();
    return app.isEmpty() ? internalPath() : app;
#else
    return defaultPath();
#endif
}

MapFolder MapFolder::current() {
    const QString configured = configuredPath();
    if (configured.isEmpty()) return MapFolder(defaultPath(), fallbackPath());
    return MapFolder(configured, defaultPath());
}

MapFolder::MapFolder(const QString &folder, const QString &fallback)
    : m_folder(QDir::cleanPath(QFileInfo(folder.isEmpty() ? fallback : folder).absoluteFilePath())),
      m_fallback(fallback.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(fallback).absoluteFilePath())) {}

MapFolder::Ready MapFolder::ensure() const {
    Ready ready;
    QString warning;
    if (usable(m_folder, &warning)) {
        ready.path = m_folder;
    } else if (!m_fallback.isEmpty() && m_fallback.compare(m_folder, kPathCase) != 0) {
        QString fallbackWarning;
        if (QDir().mkpath(m_fallback) && usable(m_fallback, &fallbackWarning)) {
            ready.path = m_fallback;
            warning += QLatin1Char(' ') + tr("Maps go to %1 for now.").arg(QDir::toNativeSeparators(m_fallback));
        }
    } else if (QDir().mkpath(m_folder) && usable(m_folder, &warning)) {
        ready.path = m_folder;  // the default location with missing parents (first start)
        warning.clear();
    }
    ready.warning = warning;
    if (!ready.path.isEmpty()) removePartials(ready.path);
    return ready;
}

bool MapFolder::contains(const QString &filePath) const {
    if (filePath.isEmpty()) return false;
    // Through links and "..": the real places are compared.
    const QString folder = QFileInfo(m_folder).canonicalFilePath();
    const QString file = QFileInfo(filePath).canonicalFilePath();
    if (folder.isEmpty() || file.isEmpty()) return false;
    return file.startsWith(folder + QLatin1Char('/'), kPathCase);
}

MapFolder::BringIn MapFolder::bringIn(const QUrl &url, const QString &targetDirectory) const {
    BringIn result;
    // A content:// URI is opened by its whole text (Qt's Android content file engine); a file URL by its path.
    const QString source = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (source.isEmpty()) { result.error = tr("No file was chosen."); return result; }
    const QFileInfo sourceInfo(source);
    if (url.isLocalFile() && contains(sourceInfo.absoluteFilePath())) { result.path = sourceInfo.absoluteFilePath(); return result; }

    const QString directory = targetDirectory.isEmpty() ? m_folder : targetDirectory;
    if (!QDir().mkpath(directory)) {
        result.error = tr("The copy cannot be written: %1").arg(tr("the folder %1 cannot be made").arg(QDir::toNativeSeparators(directory)));
        return result;
    }
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) { result.error = tr("The file cannot be read: %1").arg(input.errorString()); return result; }
    QString fileName = sourceInfo.fileName();
    if (!fileName.endsWith(kExtension, Qt::CaseInsensitive))
        fileName = (fileName.isEmpty() ? tr("Map") : QFileInfo(fileName).completeBaseName()) + kExtension;

    // The same map brought in again: its copy is reused. The copy changes once it is opened (Mentis
    // writes into it), so the copy is found by the original's hash, kept in the settings, and not
    // by comparing it with the original. A content:// stream cannot seek back: hashed, then opened again.
    const QByteArray sourceHash = sha256Of(input).toHex();
    input.close();
    const QString copyKey = kCopiesGroup + QString::fromLatin1(sourceHash);
    if (!sourceHash.isEmpty()) {
        const QString known = QSettings().value(copyKey).toString();
        if (!known.isEmpty() && QFileInfo(known).isFile() && contains(known)) { result.path = QFileInfo(known).absoluteFilePath(); return result; }
        if (!known.isEmpty()) QSettings().remove(copyKey);  // the copy is gone
    }
    if (!input.open(QIODevice::ReadOnly)) { result.error = tr("The file cannot be read: %1").arg(input.errorString()); return result; }

    const QString target = QDir(directory).filePath(freeName(directory, fileName));
    const QString partial = target + kPartial;
    QFile output(partial);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = tr("The copy cannot be written: %1").arg(output.errorString());
        return result;
    }
    constexpr qint64 kChunkBytes = 1 << 20;  // streamed: a large map is never held whole in memory
    while (!input.atEnd()) {
        const QByteArray chunk = input.read(kChunkBytes);
        // Nothing read before the end: a read error, or a stream that stopped (never wait on it).
        if (chunk.isEmpty() || output.write(chunk) != chunk.size()) break;
    }
    // A sequential stream has no size: its end is the empty read (a cut copy fails to open as a map).
    const bool complete = (input.atEnd() || input.isSequential()) && input.error() == QFileDevice::NoError
                          && output.error() == QFileDevice::NoError && output.flush();
    output.close();
    if (!complete || !QFile::rename(partial, target)) {
        const QString error = output.error() != QFileDevice::NoError ? output.errorString() : input.errorString();
        QFile::remove(partial);
        result.error = tr("The copy cannot be written: %1").arg(error);
        return result;
    }
    result.path = QFileInfo(target).absoluteFilePath();
    result.created = true;
    forgetCopiesAt(result.path);
    if (!sourceHash.isEmpty()) QSettings().setValue(copyKey, result.path);
    return result;
}

void MapFolder::forgetCopy(const QString &path) { forgetCopiesAt(QFileInfo(path).absoluteFilePath()); }

QList<MapFolder::Map> MapFolder::maps(const QString &directory) const {
    QList<Map> maps;
    removePartials(directory.isEmpty() ? m_folder : directory);
    const QFileInfoList files = QDir(directory.isEmpty() ? m_folder : directory)
                                    .entryInfoList({QLatin1Char('*') + kExtension}, QDir::Files, QDir::Time);
    for (const QFileInfo &file : files) maps.append({file.completeBaseName(), file.absoluteFilePath(), file.lastModified()});
    return maps;
}

QString MapFolder::fileNameFor(const QString &title) {
    const QString name = cleanName(title);
    return (name.isEmpty() ? tr("Map") : name) + kExtension;
}

QList<MapFolder::Folder> MapFolder::folders(const QString &directory) const {
    QList<Folder> folders;
    QDir dir(directory.isEmpty() ? m_folder : directory);
    dir.setSorting(QDir::Name | QDir::IgnoreCase | QDir::LocaleAware);
    // Links and junctions are not followed: one back up the tree would loop, one elsewhere would take
    // maps that are not in the folder.
    for (const QFileInfo &info : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        if (info.isSymLink() || info.isJunction()) continue;
        const QString path = QDir::cleanPath(info.absoluteFilePath());
        folders.append({info.fileName(), path, folderColor(path)});
    }
    return folders;
}

QStringList MapFolder::allFolders() const {
    QStringList all{m_folder};
    for (const Folder &folder : folders()) all += MapFolder(folder.path).allFolders();
    return all;
}

QString MapFolder::createFolder(const QString &parent, const QString &name, QString *error) const {
    const QString cleaned = cleanName(name);
    if (cleaned.isEmpty()) { if (error) *error = tr("The folder needs a name."); return {}; }
    const QDir dir(parent.isEmpty() ? m_folder : parent);
    const QString chosen = freeFolderName(dir, cleaned);
    if (!dir.mkdir(chosen)) { if (error) *error = tr("The folder %1 cannot be made.").arg(chosen); return {}; }
    return QDir::cleanPath(dir.absoluteFilePath(chosen));
}

QString MapFolder::renameFolder(const QString &path, const QString &name, QString *error) const {
    const QString cleaned = cleanName(name);
    if (cleaned.isEmpty()) { if (error) *error = tr("The folder needs a name."); return {}; }
    const QFileInfo info(path);
    const QDir parent = info.absoluteDir();
    if (cleaned == info.fileName()) return QDir::cleanPath(info.absoluteFilePath());
    // A map open from the folder would lose its file. Windows refuses the rename anyway (the lock file
    // is held); macOS and Linux would allow it, so it is checked here for every system.
    if (holdsOpenMap(info.absoluteFilePath())) {
        if (error) *error = tr("The folder cannot be renamed. Close the maps open from it and try again.");
        return {};
    }
    // Only the case changed: on Windows and macOS the same entry, renamed through a free temporary name.
    const bool sameEntry = cleaned.compare(info.fileName(), Qt::CaseInsensitive) == 0 && parent.exists(cleaned);
    const QString chosen = sameEntry ? cleaned : freeFolderName(parent, cleaned);
    bool renamed = false;
    if (sameEntry) {
        const QString step = freeFolderName(parent, cleaned + QStringLiteral(".renaming"));
        renamed = QDir().rename(info.absoluteFilePath(), parent.absoluteFilePath(step))
                  && QDir().rename(parent.absoluteFilePath(step), parent.absoluteFilePath(chosen));
    } else {
        renamed = QDir().rename(info.absoluteFilePath(), parent.absoluteFilePath(chosen));
    }
    if (!renamed) {
        // Windows keeps a folder from being renamed while a map in it is open (its lock file is held).
        if (error) *error = tr("The folder cannot be renamed. Close the maps open from it and try again.");
        return {};
    }
    const QString renamedPath = QDir::cleanPath(parent.absoluteFilePath(chosen));
    followMove(info.absoluteFilePath(), renamedPath);
    return renamedPath;
}

QString MapFolder::moveFolder(const QString &path, const QString &targetDirectory, QString *error) const {
    // From the cleaned path: a trailing slash would leave no name to move.
    const QString source = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QFileInfo info(source);
    const QString target = QDir::cleanPath(QFileInfo(targetDirectory).absoluteFilePath());
    // Only the folder's subfolders move, and only inside it.
    if (source.compare(QDir::cleanPath(m_folder), kPathCase) == 0 || !samePathOrInside(source, m_folder)
        || !samePathOrInside(target, m_folder)) {
        if (error) *error = tr("The folder %1 cannot be moved.").arg(info.fileName());
        return {};
    }
    if (!info.isDir()) { if (error) *error = tr("The folder %1 is not there any more.").arg(info.fileName()); return {}; }
    if (!QFileInfo(target).isDir()) {
        if (error) *error = tr("The folder %1 is not there any more.").arg(QDir::toNativeSeparators(target));
        return {};
    }
    if (QDir::cleanPath(info.absolutePath()).compare(target, kPathCase) == 0) return source;  // already there
    if (samePathOrInside(target, source)) {
        if (error) *error = tr("A folder cannot be moved into itself.");
        return {};
    }
    if (holdsOpenMap(source)) {
        if (error) *error = tr("The folder cannot be moved. Close the maps open from it and try again.");
        return {};
    }
    const QDir parent(target);
    const QString destination = QDir::cleanPath(parent.absoluteFilePath(freeFolderName(parent, info.fileName())));
    // A rename on one disk: the folder, its colour file and its maps move at once or not at all.
    if (!QDir().rename(source, destination)) {
        if (error) *error = tr("The folder %1 cannot be moved.").arg(info.fileName());
        return {};
    }
    followMove(source, destination);
    return destination;
}

bool MapFolder::isEmptyFolder(const QString &path) {
    const QStringList entries = QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    return entries.isEmpty() || entries == QStringList{kFolderFile};
}

bool MapFolder::removeFolder(const QString &path, QString *error) const {
    if (!isEmptyFolder(path)) {
        if (error) *error = tr("Only an empty folder can be deleted.");
        return false;
    }
    const QString color = folderColor(path);
    QFile::remove(QDir(path).filePath(kFolderFile));
    if (!QDir().rmdir(path)) {
        if (!color.isEmpty()) setFolderColor(path, color, nullptr);  // still there: its colour too
        if (error) *error = tr("The folder cannot be deleted.");
        return false;
    }
    return true;
}

QStringList MapFolder::folderColors() {
    // Twelve of the colour picker's grid (ui-foundations.md), each readable on the light and the dark
    // Home: red, orange, amber, yellow, green, teal, cyan, blue, indigo, purple, pink, grey.
    return {QStringLiteral("#ff5252"), QStringLiteral("#ff6f00"), QStringLiteral("#ffc009"), QStringLiteral("#fdd834"),
            QStringLiteral("#2cd551"), QStringLiteral("#00a99d"), QStringLiteral("#2abee0"), QStringLiteral("#05a8f4"),
            QStringLiteral("#7986cb"), QStringLiteral("#ba69c8"), QStringLiteral("#ea8aba"), QStringLiteral("#999999")};
}

QStringList MapFolder::folderColorNames() {
    return {tr("Red"), tr("Orange"), tr("Amber"), tr("Yellow"), tr("Green"), tr("Teal"),
            tr("Cyan"), tr("Blue"), tr("Indigo"), tr("Purple"), tr("Pink"), tr("Gray")};
}

QString MapFolder::folderColor(const QString &folderPath) {
    QFile file(QDir(folderPath).filePath(kFolderFile));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) return {};  // ours is a few bytes
    const QString color = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("color")).toString().toLower();
    return folderColors().contains(color) ? color : QString();
}

bool MapFolder::setFolderColor(const QString &folderPath, const QString &color, QString *error) {
    const QString file = QDir(folderPath).filePath(kFolderFile);
    const QString chosen = color.toLower();
    if (!chosen.isEmpty() && !folderColors().contains(chosen)) { if (error) *error = tr("The color is not one of the folders' colors."); return false; }
    if (chosen.isEmpty()) {
        if (QFileInfo::exists(file) && !QFile::remove(file)) {
            if (error) *error = tr("The color of the folder %1 cannot be changed.").arg(QFileInfo(folderPath).fileName());
            return false;
        }
        return true;
    }
#ifdef Q_OS_WIN
    // Windows refuses to replace a hidden file: the attribute goes first, the replace stays atomic.
    if (QFileInfo::exists(file)) {
        const std::wstring old = QDir::toNativeSeparators(file).toStdWString();
        SetFileAttributesW(old.c_str(), GetFileAttributesW(old.c_str()) & ~DWORD(FILE_ATTRIBUTE_HIDDEN));
    }
#endif
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly)
        || out.write(QJsonDocument(QJsonObject{{QStringLiteral("color"), chosen}}).toJson(QJsonDocument::Compact)) < 0
        || !out.commit()) {
        if (error) *error = tr("The color of the folder %1 cannot be changed.").arg(QFileInfo(folderPath).fileName());
        return false;
    }
#ifdef Q_OS_WIN
    // A dot file is hidden on macOS, Linux and Android, not in Windows Explorer.
    const std::wstring native = QDir::toNativeSeparators(file).toStdWString();
    SetFileAttributesW(native.c_str(), GetFileAttributesW(native.c_str()) | FILE_ATTRIBUTE_HIDDEN);
#endif
    return true;
}

bool MapFolder::isOpen(const QString &mapPath) {
    QLockFile lock(mapPath + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);  // as Document::acquireLock: stale only when its process is gone
    if (lock.tryLock(0)) { lock.unlock(); return false; }
    return lock.error() == QLockFile::LockFailedError;
}

MapFolder::Move MapFolder::moveMap(const QString &mapPath, const QString &targetDirectory) const {
    Move move;
    const QFileInfo map(mapPath);
    const QString target = QDir::cleanPath(QFileInfo(targetDirectory).absoluteFilePath());
    if (!map.isFile()) { move.error = tr("The map %1 is not there any more.").arg(map.fileName()); return move; }
    if (!QFileInfo(target).isDir()) {
        move.error = tr("The folder %1 is not there any more.").arg(QDir::toNativeSeparators(target));
        return move;
    }
    if (QDir::cleanPath(map.absolutePath()).compare(target, kPathCase) == 0) { move.path = map.absoluteFilePath(); return move; }
    if (isOpen(map.absoluteFilePath())) {
        QSettings settings;
        QVariantMap pending = settings.value(kPendingMovesKey).toMap();
        pending.insert(map.absoluteFilePath(), target);
        settings.setValue(kPendingMovesKey, pending);
        move.path = QDir(target).filePath(map.fileName());
        move.deferred = true;
        return move;
    }
    const QString destination = QDir(target).filePath(freeName(target, map.fileName()));
    // A rename on one disk: the whole file moves at once or not at all.
    if (!renameMap(map.absoluteFilePath(), destination)) {
        move.error = tr("The map %1 cannot be moved.").arg(map.fileName());
        return move;
    }
    move.path = QDir::cleanPath(destination);
    followMove(map.absoluteFilePath(), move.path);
    return move;
}

QList<QPair<QString, QString>> MapFolder::runPendingMoves() {
    QSettings settings;
    QVariantMap pending = settings.value(kPendingMovesKey).toMap();
    if (pending.isEmpty()) return {};
    QList<QPair<QString, QString>> moved;
    for (auto entry = pending.begin(); entry != pending.end();) {
        const QString map = entry.key();
        if (QFileInfo(map).isFile() && isOpen(map)) { ++entry; continue; }
        const QString target = entry.value().toString();
        if (QFileInfo(map).isFile() && QFileInfo(target).isDir()) {
            const QString destination = QDir(target).filePath(freeName(target, QFileInfo(map).fileName()));
            if (renameMap(map, destination)) {
                moved.append({map, QDir::cleanPath(destination)});
                followMove(map, destination);
            }
        }
        entry = pending.erase(entry);  // done, or the map or its target is gone: nothing more to do
    }
    if (pending.isEmpty()) settings.remove(kPendingMovesKey);
    else settings.setValue(kPendingMovesKey, pending);
    return moved;
}

MapFolder::MoveAll MapFolder::moveAllTo(const QString &target) const {
    MoveAll result;
    const QString destination = QDir::cleanPath(QFileInfo(target).absoluteFilePath());
    if (destination.compare(m_folder, kPathCase) == 0) return result;
    const auto insideDestination = [&destination](const QString &dir) {
        return dir.compare(destination, kPathCase) == 0 || dir.startsWith(destination + QLatin1Char('/'), kPathCase);
    };
    const QStringList dirs = allFolders();
    // The folders holding a map somewhere below: only those, and the empty ones (decision 5), move.
    // A folder of other files only (the user chose Documents) is neither copied nor touched.
    QSet<QString> withMaps{m_folder};
    for (const QString &dir : dirs)
        if (!QDir(dir).entryList({QLatin1Char('*') + kExtension}, QDir::Files).isEmpty())
            for (QString up = dir; samePathOrInside(up, m_folder) && !withMaps.contains(up); up = QFileInfo(up).absolutePath())
                withMaps.insert(up);
    QStringList walked;
    for (const QString &dir : dirs) {
        if (insideDestination(dir)) continue;  // the new folder chosen inside the old one
        // Kept with the maps: a folder of maps, or one with nothing below it but folders and their
        // colours (empty folders the user made, decision 5).
        const auto onlyFolders = [](const QString &top) {
            QDirIterator files(top, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
            while (files.hasNext())
                if (files.next(), files.fileName() != kFolderFile) return false;
            return true;
        };
        if (!withMaps.contains(dir) && !onlyFolders(dir)) continue;
        walked.append(dir);
        const QString targetDir = QDir::cleanPath(QDir(destination).filePath(QDir(m_folder).relativeFilePath(dir)));
        if (!QDir().mkpath(targetDir)) {
            result.errors.append(tr("The folder %1 cannot be made.").arg(QDir::toNativeSeparators(targetDir)));
            continue;
        }
        // The colour goes with its folder (FEAT-107); a folder already coloured there keeps its own.
        const QString color = folderColor(dir);
        if (!color.isEmpty() && dir.compare(m_folder, kPathCase) != 0) {
            QString error;
            if (!folderColor(targetDir).isEmpty() || setFolderColor(targetDir, color, &error)) QFile::remove(QDir(dir).filePath(kFolderFile));
            else result.errors.append(error);
        }
        for (const Map &map : maps(dir)) {
            const Move move = moveMap(map.path, targetDir);
            if (!move.error.isEmpty()) result.errors.append(move.error);
            else if (move.deferred) ++result.deferred;
            else result.moved.append({map.path, move.path});
        }
    }
    // Deepest first; rmdir refuses a folder that still holds something (a deferred map, other files).
    for (auto dir = walked.crbegin(); dir != walked.crend(); ++dir)
        if (dir->compare(m_folder, kPathCase) != 0 && QDir(*dir).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
            QDir().rmdir(*dir);
    return result;
}

QString MapFolder::freeName(const QString &directory, const QString &fileName) {
    const QDir dir(directory);
    // A name is taken also by the files SQLite and the lock leave beside a map: an orphan journal next
    // to a new map would be played back into it as a hot journal (https://www.sqlite.org/tempfiles.html).
    const auto taken = [&dir](const QString &name) {
        for (const QString &suffix : {QString(), QStringLiteral("-journal"), QStringLiteral("-wal"), QStringLiteral("-shm"), QStringLiteral(".lock")})
            if (dir.exists(name + suffix)) return true;
        return false;
    };
    if (!taken(fileName)) return fileName;
    const QString base = QFileInfo(fileName).completeBaseName();
    for (int number = 2;; ++number) {
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(number).arg(kExtension);
        if (!taken(candidate)) return candidate;
    }
}
