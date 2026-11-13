#include "maplibrary.h"

#include "mapfolder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantMap>

MapLibrary::MapLibrary(const QString &folder, QObject *parent) : QObject(parent), m_followsSettings(folder.isEmpty()) {
    const MapFolder chosen = folder.isEmpty() ? MapFolder::current() : MapFolder(folder);
    m_folder = chosen.path();
    QDir().mkpath(m_folder);
    // Also removes the .part file of a copy cut short (the app killed while copying).
    const MapFolder::Ready ready = chosen.ensure();
    if (!ready.path.isEmpty()) m_folder = ready.path;
    m_lastError = ready.warning;  // the shared storage missing: the maps go to the fallback for now
    refresh();
}

int MapLibrary::reload() {
    if (!m_followsSettings) return 0;
    const MapFolder::Ready ready = MapFolder::current().ensure();
    if (ready.path.isEmpty() || QDir::cleanPath(ready.path) == QDir::cleanPath(m_folder)) return 0;
    const MapFolder::MoveAll moved = MapFolder(m_folder).moveAllTo(ready.path);
    m_folder = ready.path;
    m_shown.clear();  // back at the top of the new folder
    emit folderChanged();
    QStringList problems;
    if (!ready.warning.isEmpty()) problems.append(ready.warning);
    problems.append(moved.errors);
    setError(problems.join(QLatin1Char('\n')));
    refresh();
    return int(moved.moved.size());
}

void MapLibrary::refresh() {
    if (m_followsSettings) MapFolder::runPendingMoves();
    const MapFolder root(m_folder);
    const QString before = m_shown;
    if (m_shown.isEmpty() || !QFileInfo(m_shown).isDir() || (QDir::cleanPath(m_shown) != QDir::cleanPath(m_folder) && !root.contains(m_shown)))
        m_shown = QDir::cleanPath(m_folder);
    if (m_shown != before) emit shownChanged();
    QVariantList folders;
    for (const MapFolder::Folder &folder : root.folders(m_shown))
        folders.append(QVariantMap{{QStringLiteral("name"), folder.name},
                                   {QStringLiteral("path"), folder.path},
                                   {QStringLiteral("color"), folder.color}});
    QVariantList maps;
    for (const MapFolder::Map &map : root.maps(m_shown))
        maps.append(QVariantMap{{QStringLiteral("name"), map.name},
                                {QStringLiteral("path"), map.path},
                                {QStringLiteral("url"), QUrl::fromLocalFile(map.path)},
                                {QStringLiteral("modified"), map.modified}});
    if (maps == m_maps && folders == m_folders) return;
    m_maps = maps;
    m_folders = folders;
    emit mapsChanged();
}

QString MapLibrary::shownName() const { return QFileInfo(m_shown).fileName(); }

bool MapLibrary::atRoot() const { return QDir::cleanPath(m_shown) == QDir::cleanPath(m_folder); }

QVariantList MapLibrary::folderColors() {
    QVariantList colors{QVariantMap{{QStringLiteral("color"), QString()}, {QStringLiteral("name"), tr("No color")}}};
    const QStringList values = MapFolder::folderColors();
    const QStringList names = MapFolder::folderColorNames();
    for (qsizetype i = 0; i < values.size(); ++i)
        colors.append(QVariantMap{{QStringLiteral("color"), values[i]}, {QStringLiteral("name"), names.value(i)}});
    return colors;
}

void MapLibrary::enter(const QString &folder) {
    const QString before = m_shown;
    m_shown = QDir::cleanPath(folder);
    refresh();  // a path outside the folder falls back to the folder
    if (m_shown != before) emit shownChanged();
}

void MapLibrary::up() {
    if (!atRoot()) enter(QFileInfo(m_shown).absolutePath());
}

bool MapLibrary::createFolder(const QString &name) {
    QString error;
    const bool made = !MapFolder(m_folder).createFolder(m_shown, name, &error).isEmpty();
    setError(error);
    refresh();
    return made;
}

bool MapLibrary::renameFolder(const QString &folder, const QString &name) {
    QString error;
    const bool renamed = !MapFolder(m_folder).renameFolder(folder, name, &error).isEmpty();
    setError(error);
    refresh();
    return renamed;
}

bool MapLibrary::setFolderColor(const QString &folder, const QString &color) {
    QString error;
    const bool set = MapFolder::setFolderColor(folder, color, &error);
    setError(error);
    refresh();
    return set;
}

bool MapLibrary::removeFolder(const QString &folder) {
    QString error;
    const bool removed = MapFolder(m_folder).removeFolder(folder, &error);
    setError(error);
    refresh();
    return removed;
}

bool MapLibrary::isEmptyFolder(const QString &folder) { return MapFolder::isEmptyFolder(folder); }

QString MapLibrary::folderColor(const QString &folder) { return MapFolder::folderColor(folder); }

QVariantList MapLibrary::moveTargets(const QString &entry) const {
    const MapFolder root(m_folder);
    const QString rootName = QFileInfo(m_folder).fileName();
    const QFileInfo info(entry);
    const QString mapDir = QDir::cleanPath(info.absolutePath());
    const QString self = info.isDir() ? QDir::cleanPath(info.absoluteFilePath()) : QString();
    // With the system's path case (MapFolder::moved): moved to "" leaves nothing for the same path,
    // only the rest for a path inside.
    const auto samePath = [](const QString &path, const QString &place) { return MapFolder::moved(path, place, QString()).isEmpty(); };
    const auto insideOrSame = [](const QString &path, const QString &place) { return MapFolder::moved(path, place, QString()) != path; };
    QVariantList targets;
    for (const QString &folder : root.allFolders()) {
        if (!self.isEmpty() && insideOrSame(folder, self)) continue;
        const QString relative = QDir(m_folder).relativeFilePath(folder);
        QString label = rootName;
        if (relative != QLatin1String(".")) label += QStringLiteral(" › ") + relative.split(QLatin1Char('/')).join(QStringLiteral(" › "));
        targets.append(QVariantMap{{QStringLiteral("label"), label},
                                   {QStringLiteral("path"), folder},
                                   {QStringLiteral("here"), samePath(folder, mapDir)}});
    }
    return targets;
}

bool MapLibrary::moveMap(const QString &map, const QString &folder) {
    const MapFolder::Move move = MapFolder(m_folder).moveMap(map, folder);
    setError(move.error);
    refresh();
    return move.error.isEmpty();
}

bool MapLibrary::moveFolder(const QString &folder, const QString &target) {
    QString error;
    const bool moved = !MapFolder(m_folder).moveFolder(folder, target, &error).isEmpty();
    setError(error);
    refresh();
    return moved;
}

QString MapLibrary::localCopy(const QUrl &url, bool *created) {
    const MapFolder::BringIn copy = MapFolder(m_folder).bringIn(url);
    if (created) *created = copy.created;
    setError(copy.error);
    if (copy.created) refresh();
    return copy.path;
}

bool MapLibrary::discard(const QString &path) {
    const QFileInfo info(path);
    if (!MapFolder(m_folder).contains(info.absoluteFilePath())) return false;  // only the storage's own files
    const bool removed = QFile::remove(info.absoluteFilePath());
    if (removed) MapFolder::forgetCopy(info.absoluteFilePath());
    refresh();
    return removed;
}

void MapLibrary::setError(const QString &error) {
    if (error == m_lastError) return;
    m_lastError = error;
    emit lastErrorChanged();
}
