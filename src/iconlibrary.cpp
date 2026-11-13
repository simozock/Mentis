#include "iconlibrary.h"

#include "markers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace IconLibrary {

namespace {
QString tr(const char *text) { return QCoreApplication::translate("IconLibrary", text); }

// FEAT-046 part B: default names follow the interface language. They are saved empty and filled
// in when read; a name the user typed is saved as written.
QString ownDefaultName(Kind kind) { return kind == Kind::Sticker ? tr("My stickers") : tr("My markers"); }

// The names in collections.txt, for lupdate: the file keeps the order, these are translated.
[[maybe_unused]] const char *const kStickerCollectionNames[] = {
    QT_TRANSLATE_NOOP("IconLibrary", "Mentis"), QT_TRANSLATE_NOOP("IconLibrary", "Education"),
    QT_TRANSLATE_NOOP("IconLibrary", "Business"),
    QT_TRANSLATE_NOOP("IconLibrary", "Thinking"), QT_TRANSLATE_NOOP("IconLibrary", "Objects"),
    QT_TRANSLATE_NOOP("IconLibrary", "Animals"), QT_TRANSLATE_NOOP("IconLibrary", "Cute animals"),
    QT_TRANSLATE_NOOP("IconLibrary", "Food"), QT_TRANSLATE_NOOP("IconLibrary", "Health and fitness"),
    QT_TRANSLATE_NOOP("IconLibrary", "Medical"), QT_TRANSLATE_NOOP("IconLibrary", "Travel"),
    QT_TRANSLATE_NOOP("IconLibrary", "Landmarks")};

// Sticker folders in the order of ":/stickers/collections.txt" ("folder|name"), with their
// translated (or English) names; folders not listed come after, named after the folder.
QList<std::pair<QString, QString>> stickerCollections(bool translated = true) {
    const QString root = QStringLiteral(":/stickers");
    QList<std::pair<QString, QString>> list;
    QStringList listed;
    QFile index(root + QStringLiteral("/collections.txt"));
    if (index.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const QString &line : QString::fromUtf8(index.readAll()).split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1Char('#')) || !line.contains(QLatin1Char('|'))) continue;
            const QString folder = line.section(QLatin1Char('|'), 0, 0).trimmed();
            listed.append(folder);
            const QString name = line.section(QLatin1Char('|'), 1).trimmed();
            list.append({folder, translated ? tr(name.toUtf8().constData()) : name});
        }
    }
    for (const QString &folder : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (!listed.contains(folder)) list.append({folder, displayName(folder)});
    return list;
}

// Name a collection has when the user has not renamed it.
QString defaultName(Kind kind, const QString &id, bool builtIn) {
    if (!builtIn) return ownDefaultName(kind);
    const QString key = id.mid(QStringLiteral("builtin:").size());
    if (kind == Kind::Marker) return Markers::seriesLabel(key);
    for (const auto &[folder, name] : stickerCollections())
        if (folder == key) return name;
    return displayName(key);
}

QString kindFolderName(Kind kind) { return kind == Kind::Sticker ? QStringLiteral("stickers") : QStringLiteral("markers"); }
QString libraryFile(Kind kind) { return folder(kind) + QStringLiteral("/library.json"); }
QString filesFolder(Kind kind) { return folder(kind) + QStringLiteral("/files"); }

bool readable(const QString &path) { return QImageReader(path).canRead(); }

// Raccolte di base com'erano all'origine (nome ed elementi).
QList<Collection> builtInCollections(Kind kind) {
    QList<Collection> list;
    if (kind == Kind::Marker) {
        for (const Markers::Series &series : Markers::series()) {
            Collection collection{QStringLiteral("builtin:") + series.id, Markers::seriesLabel(series.id), true, false, {}};
            for (const QString &value : series.values) {
                const QString marker = series.id + QLatin1Char(':') + value;
                collection.items.append({marker, Markers::label(marker)});
            }
            list.append(collection);
        }
        return list;
    }
    // Adesivi: una raccolta per sottocartella delle risorse (":/stickers/<raccolta>/", SVG Repo, CC0).
    const QString root = QStringLiteral(":/stickers");
    for (const auto &[folder, name] : stickerCollections()) {
        Collection collection{QStringLiteral("builtin:") + folder, name, true, false, {}};
        for (const QFileInfo &info : QDir(root + QLatin1Char('/') + folder).entryInfoList(QDir::Files, QDir::Name))
            if (readable(info.filePath())) collection.items.append({info.filePath(), displayName(info.filePath())});
        if (!collection.items.isEmpty()) list.append(collection);
    }
    return list;
}

// Stato salvato: per raccolta id, nome, hidden ed elementi (percorsi) nell'ordine scelto.
bool save(Kind kind, const QList<Collection> &list);

QList<Collection> load(Kind kind) {
    QFile file(libraryFile(kind));
    QList<Collection> saved;
    bool hasFile = false;
    if (file.open(QIODevice::ReadOnly)) {
        hasFile = true;
        for (const QJsonValue &value : QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("collections")).toArray()) {
            const QJsonObject object = value.toObject();
            Collection collection{object.value(QStringLiteral("id")).toString(), object.value(QStringLiteral("name")).toString(),
                                  object.value(QStringLiteral("builtIn")).toBool(), object.value(QStringLiteral("hidden")).toBool(), {}};
            for (const QJsonValue &item : object.value(QStringLiteral("items")).toArray()) collection.items.append({item.toString(), {}});
            saved.append(collection);
        }
    }
    const QList<Collection> origin = builtInCollections(kind);
    QList<Collection> result;
    QStringList seen;
    for (Collection collection : saved) {
        if (collection.builtIn) {
            const auto found = std::find_if(origin.cbegin(), origin.cend(), [&](const Collection &o) { return o.id == collection.id; });
            if (found == origin.cend()) continue;  // raccolta di base non piu' presente
            if (collection.name.isEmpty()) collection.name = found->name;
        } else if (collection.name.isEmpty()) {
            collection.name = ownDefaultName(kind);
        }
        result.append(collection);
    }
    // Raccolte di base nuove (o la prima volta): in fondo, con i loro elementi.
    for (const Collection &o : origin)
        if (std::none_of(result.cbegin(), result.cend(), [&](const Collection &c) { return c.id == o.id; })) result.append(o);
    // Elementi: quelli salvati che esistono ancora, poi quelli di base non sistemati altrove.
    for (Collection &collection : result) {
        QList<Item> items;
        for (const Item &item : collection.items) {
            const bool drawn = kind == Kind::Marker && !item.path.contains(QLatin1Char('/')) && Markers::isValid(item.path);
            if (seen.contains(item.path) || (!drawn && !QFileInfo::exists(item.path))) continue;
            items.append({item.path, drawn ? Markers::label(item.path) : displayName(item.path)});
            seen.append(item.path);
        }
        collection.items = items;
    }
    for (const Collection &o : origin)
        for (const Item &item : o.items)
            if (!seen.contains(item.path)) {
                auto target = std::find_if(result.begin(), result.end(), [&](const Collection &c) { return c.id == o.id; });
                if (target != result.end()) target->items.append(item);
                seen.append(item.path);
            }
    // Prima volta con i file della tappa 1 (adesivi nella cartella, senza library.json): una raccolta.
    if (!hasFile && kind == Kind::Sticker) {
        Collection mine{QUuid::createUuid().toString(QUuid::Id128).left(12), ownDefaultName(kind), false, false, {}};
        for (const QFileInfo &info : QDir(folder(kind)).entryInfoList(QDir::Files, QDir::Name))
            if (readable(info.filePath())) mine.items.append({info.filePath(), displayName(info.filePath())});
        if (!mine.items.isEmpty()) {
            result.prepend(mine);
            save(kind, result);  // l'id della raccolta resta lo stesso alle letture seguenti
        }
    }
    return result;
}

bool save(Kind kind, const QList<Collection> &list) {
    if (!QDir().mkpath(folder(kind))) return false;
    QJsonArray array;
    for (const Collection &collection : list) {
        QJsonArray items;
        for (const Item &item : collection.items) items.append(item.path);
        const bool renamed = collection.name != defaultName(kind, collection.id, collection.builtIn);
        array.append(QJsonObject{{QStringLiteral("id"), collection.id}, {QStringLiteral("name"), renamed ? collection.name : QString()},
                                 {QStringLiteral("builtIn"), collection.builtIn}, {QStringLiteral("hidden"), collection.hidden},
                                 {QStringLiteral("items"), items}});
    }
    QSaveFile file(libraryFile(kind));
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("collections"), array}}).toJson());
    return file.commit();
}

// A name usable as a file name: one line, no path separators or ":", at most 100 characters.
QString cleanName(const QString &name) {
    QString clean = name.simplified();
    clean.replace(QLatin1Char('/'), QLatin1Char('-')).replace(QLatin1Char('\\'), QLatin1Char('-')).replace(QLatin1Char(':'), QLatin1Char('-'));
    return clean.size() > 100 ? QString() : clean;
}

QString copyIntoLibrary(Kind kind, const QString &path, const QString &wanted = QString()) {
    if (!QDir().mkpath(filesFolder(kind))) return {};
    const QFileInfo info(path);
    const QString clean = cleanName(wanted);
    const QString base = clean.isEmpty() ? info.completeBaseName() : clean;
    QString name = base + QLatin1Char('.') + info.suffix();
    for (int n = 2; QFileInfo::exists(filesFolder(kind) + QLatin1Char('/') + name); ++n)
        name = base + QStringLiteral("-%1.").arg(n) + info.suffix();
    const QString target = filesFolder(kind) + QLatin1Char('/') + name;
    return QFile::copy(path, target) ? target : QString();
}

bool insideLibrary(Kind kind, const QString &path) {
    return QFileInfo(path).absoluteFilePath().startsWith(QFileInfo(folder(kind)).absoluteFilePath() + QLatin1Char('/'));
}

// BUG-037: a file taken out of the library may still be shown by an open map that has not embedded
// it yet (no save since it was inserted, or an untitled map): deleting it at once made every save
// of that map fail. Its path goes to removed.txt and the file is deleted at the next start
// (removeDeletedFiles), when every map open now has embedded its copy or was closed without it.
QString removedList(Kind kind) { return folder(kind) + QStringLiteral("/removed.txt"); }

void deleteLater(Kind kind, const QStringList &paths) {
    QStringList own;
    for (const QString &path : paths)
        if (insideLibrary(kind, path)) own.append(path);  // built-in items stay in the resources
    if (own.isEmpty() || !QDir().mkpath(folder(kind))) return;
    QFile list(removedList(kind));
    if (list.open(QIODevice::Append | QIODevice::Text)) list.write((own.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
}
}  // namespace

QString folder(Kind kind) {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1Char('/') + kindFolderName(kind);
}

void removeDeletedFiles(Kind kind) {
    QFile list(removedList(kind));
    if (!list.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QStringList paths = QString::fromUtf8(list.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    list.close();
    QStringList listed;  // a path removed and added again later is in the library once more
    for (const Collection &collection : load(kind))
        for (const Item &item : collection.items) listed.append(item.path);
    for (const QString &path : paths)
        if (insideLibrary(kind, path) && !listed.contains(path)) QFile::remove(path);
    list.remove();
}

QString customGroup(const QString &collectionId) {
    QString group = collectionId;
    return group.replace(QLatin1Char(':'), QLatin1Char('-'));
}

QString markerFor(const Item &item, const QString &collectionId, QByteArray *data) {
    if (!item.path.contains(QLatin1Char('/'))) return item.path;
    QFile file(item.path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    const QString sha = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    Markers::registerImage(sha, bytes);
    Markers::setCustomLabel(sha, item.name);
    if (data) *data = bytes;
    return Markers::customMarker(customGroup(collectionId), sha);
}

QString displayName(const QString &path) {
    QString name = QFileInfo(path).completeBaseName();
    return name.replace(QLatin1Char('_'), QLatin1Char(' ')).replace(QLatin1Char('-'), QLatin1Char(' '));
}

QList<Collection> collections(Kind kind) { return load(kind); }

QString englishName(Kind kind, const QString &collectionId) {
    if (!collectionId.startsWith(QLatin1String("builtin:"))) return {};
    const QString key = collectionId.mid(QStringLiteral("builtin:").size());
    if (kind == Kind::Marker) return Markers::seriesLabel(key, false);
    for (const auto &[folder, name] : stickerCollections(false))
        if (folder == key) return name;
    return displayName(key);
}

QString englishName(Kind kind, const Item &item) {
    const bool drawn = kind == Kind::Marker && !item.path.contains(QLatin1Char('/')) && Markers::isValid(item.path);
    return drawn ? Markers::label(item.path, false) : item.name;
}

Collection collection(Kind kind, const QString &id) {
    for (const Collection &c : load(kind))
        if (c.id == id) return c;
    return {};
}

QString addCollection(Kind kind, const QString &name) {
    QList<Collection> list = load(kind);
    Collection collection{QUuid::createUuid().toString(QUuid::Id128).left(12), name.trimmed(), false, false, {}};
    if (collection.name.isEmpty()) collection.name = ownDefaultName(kind);
    list.append(collection);
    return save(kind, list) ? collection.id : QString();
}

bool renameCollection(Kind kind, const QString &id, const QString &name) {
    QList<Collection> list = load(kind);
    for (Collection &c : list)
        if (c.id == id && !name.trimmed().isEmpty()) { c.name = name.trimmed(); return save(kind, list); }
    return false;
}

bool removeCollection(Kind kind, const QString &id) {
    QList<Collection> list = load(kind);
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].id != id) continue;
        if (list[i].builtIn) {
            list[i].hidden = true;
        } else {
            QStringList paths;
            for (const Item &item : list[i].items) paths.append(item.path);
            list.removeAt(i);
            if (!save(kind, list)) return false;
            deleteLater(kind, paths);
            return true;
        }
        return save(kind, list);
    }
    return false;
}

void restoreBuiltIns(Kind kind) {
    QList<Collection> list = load(kind);
    const QList<Collection> origin = builtInCollections(kind);
    for (Collection &c : list) {
        if (!c.builtIn) continue;
        c.hidden = false;
        for (const Collection &o : origin)
            if (o.id == c.id) c.name = o.name;
    }
    save(kind, list);
}

bool moveCollection(Kind kind, const QString &id, int toIndex) {
    QList<Collection> list = load(kind);
    for (int i = 0; i < list.size(); ++i)
        if (list[i].id == id) {
            list.move(i, std::clamp(toIndex, 0, int(list.size()) - 1));
            return save(kind, list);
        }
    return false;
}

QString addFile(Kind kind, const QString &collectionId, const QString &path, const QString &name) {
    QList<Collection> list = load(kind);
    for (Collection &c : list) {
        if (c.id != collectionId) continue;
        const QString copy = copyIntoLibrary(kind, path, name);
        if (copy.isEmpty()) return {};
        c.items.append({copy, displayName(copy)});
        return save(kind, list) ? copy : QString();
    }
    return {};
}

QString addData(Kind kind, const QString &collectionId, const QString &fileName, const QByteArray &data) {
    QList<Collection> list = load(kind);
    for (Collection &c : list) {
        if (c.id != collectionId) continue;
        if (!QDir().mkpath(filesFolder(kind))) return {};
        const QFileInfo info(fileName);
        QString name = info.fileName();
        for (int n = 2; QFileInfo::exists(filesFolder(kind) + QLatin1Char('/') + name); ++n)
            name = info.completeBaseName() + QStringLiteral("-%1.").arg(n) + info.suffix();
        const QString target = filesFolder(kind) + QLatin1Char('/') + name;
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) return {};
        c.items.append({target, displayName(target)});
        return save(kind, list) ? target : QString();
    }
    return {};
}

bool moveItem(Kind kind, const QString &path, const QString &toCollectionId, int index) {
    QList<Collection> list = load(kind);
    Item moved;
    bool found = false;
    for (Collection &c : list)
        for (int i = 0; i < c.items.size(); ++i)
            if (c.items[i].path == path) { moved = c.items.takeAt(i); found = true; break; }
    if (!found) return false;
    for (Collection &c : list)
        if (c.id == toCollectionId) {
            c.items.insert(index < 0 ? c.items.size() : std::min<qsizetype>(index, c.items.size()), moved);
            return save(kind, list);
        }
    return false;
}

QString renameItem(Kind kind, const QString &path, const QString &name) {
    const QString clean = cleanName(name);
    if (clean.isEmpty() || !insideLibrary(kind, path)) return {};
    QList<Collection> list = load(kind);
    for (Collection &c : list)
        for (Item &item : c.items) {
            if (item.path != path) continue;
            const QFileInfo info(path);
            QString target = info.absolutePath() + QLatin1Char('/') + clean + QLatin1Char('.') + info.suffix();
            for (int n = 2; QFileInfo::exists(target) && target != path; ++n)
                target = info.absolutePath() + QLatin1Char('/') + clean + QStringLiteral("-%1.").arg(n) + info.suffix();
            if (target == path) return path;
            // A copy, not a rename: an open map may still point to the old file (BUG-037).
            if (!QFile::copy(path, target)) return {};
            item = {target, displayName(target)};
            if (!save(kind, list)) { QFile::remove(target); return {}; }
            deleteLater(kind, {path});
            return target;
        }
    return {};
}

bool removeItem(Kind kind, const QString &collectionId, const QString &path) {
    QList<Collection> list = load(kind);
    for (Collection &c : list) {
        if (c.id != collectionId) continue;
        for (int i = 0; i < c.items.size(); ++i)
            if (c.items[i].path == path) {
                c.items.removeAt(i);
                if (!save(kind, list)) return false;
                deleteLater(kind, {path});
                return true;
            }
    }
    return false;
}

QString importFolder(Kind kind, const QString &folderPath, int *imported) {
    QStringList files;
    QDirIterator it(folderPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (checkFile(kind, path).ok) files.append(path);
    }
    std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) { return QFileInfo(a).fileName().localeAwareCompare(QFileInfo(b).fileName()) < 0; });
    if (imported) *imported = int(files.size());
    if (files.isEmpty()) return {};
    const QString id = addCollection(kind, displayName(folderPath));
    if (id.isEmpty()) return {};
    QList<Collection> list = load(kind);
    for (Collection &c : list) {
        if (c.id != id) continue;
        for (const QString &path : files)
            if (const QString copy = copyIntoLibrary(kind, path); !copy.isEmpty()) c.items.append({copy, displayName(copy)});
    }
    save(kind, list);
    return id;
}

Check checkFile(Kind kind, const QString &path) {
    Check check;
    const QFileInfo info(path);
    if (!info.isFile()) { check.reason = tr("The file does not exist."); return check; }
    if (info.size() > kMaxFileBytes) {
        check.reason = tr("It is larger than 2 MB: for markers and stickers use an SVG or a smaller PNG.");
        return check;
    }
    QImageReader reader(path);
    const QByteArray format = reader.format();
    const QImage image = reader.read();
    if (image.isNull()) {
        check.reason = format == "svg" ? tr("The SVG cannot be drawn (unsupported or damaged content).")
                                       : tr("It is not an image this app can read (use SVG or PNG).");
        return check;
    }
    check.ok = true;
    if (format != "svg" && format != "svgz") {
        const int needed = kind == Kind::Marker ? kMinMarkerPx : kMinStickerPx;
        const QSize size = image.size();
        if (std::max(size.width(), size.height()) < needed) {
            check.small = true;
            check.smallReason = tr("It is only %1×%2 px: enlarged it may look blurry (an SVG stays sharp).")
                                    .arg(size.width()).arg(size.height());
        }
    }
    return check;
}

QString fileFilter() {
    QStringList patterns{QStringLiteral("*.svg"), QStringLiteral("*.png")};
    for (const QByteArray &format : QImageReader::supportedImageFormats()) {
        const QString pattern = QStringLiteral("*.") + QString::fromLatin1(format).toLower();
        if (!patterns.contains(pattern) && (format == "jpg" || format == "jpeg" || format == "gif" || format == "webp")) patterns.append(pattern);
    }
    return tr("Images (%1)").arg(patterns.join(QLatin1Char(' ')));
}

}  // namespace IconLibrary
