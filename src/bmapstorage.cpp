#include "bmapstorage.h"

#include "mindmapmodel.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QHash>
#include <QBuffer>
#include <QImageReader>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QPair>
#include <QRegularExpression>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <functional>

namespace {
constexpr int kSchemaVersion = 3;
constexpr int kApplicationId = 0x42434d50; // "BCMP"
constexpr int kMaximumSnapshots = 10;
constexpr int kLargeDeletionThreshold = 10;

struct AssetCacheEntry { qint64 size = -1; qint64 modified = -1; QString id; };
QHash<QString, AssetCacheEntry> assetCache;
// 2026-10-14: la cache è usata dal thread del salvataggio automatico e dal thread dell'interfaccia
// (anteprime della Home, apertura dei fogli): QHash non è sicura tra thread senza un lucchetto
// (https://doc.qt.io/qt-6/threads-reentrancy.html). Si tiene solo per lettura e scrittura della voce.
QMutex assetCacheMutex;
AssetCacheEntry cachedAsset(const QString &path) { QMutexLocker lock(&assetCacheMutex); return assetCache.value(path); }
void rememberAsset(const QString &path, const AssetCacheEntry &entry) { QMutexLocker lock(&assetCacheMutex); assetCache.insert(path, entry); }

QString connectionName() {
    return QStringLiteral("mentis-storage-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool exec(QSqlQuery &query, const QString &sql, QString *error) {
    if (query.exec(sql)) return true;
    if (error) *error = query.lastError().text();
    return false;
}

// BUG-044: a .mentis may come from someone else, who can add triggers (SQL that SQLite runs by
// itself when Mentis writes, e.g. to empty the attachments at each save) or views. Mentis never
// creates either, so a file with them is refused; trusted_schema=OFF also keeps any schema SQL
// from calling functions with side effects (https://www.sqlite.org/pragma.html#pragma_trusted_schema).
bool onlyMentisSchema(QSqlDatabase &db, QString *error) {
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA trusted_schema=OFF")) ||
        !query.exec(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type IN ('trigger','view') LIMIT 1"))) {
        if (error) *error = query.lastError().text();
        return false;
    }
    if (!query.next()) return true;
    if (error) *error = QObject::tr("The file is not a valid Mentis document.");
    return false;
}

bool createSchema(QSqlDatabase &db, QString *error) {
    if (!onlyMentisSchema(db, error)) return false;
    QSqlQuery q(db);
    const QStringList statements{
        QStringLiteral("PRAGMA foreign_keys=ON"),
        QStringLiteral("PRAGMA busy_timeout=5000"),
        // P9 (2026-10-13): le mappe potranno stare su NAS o cartelle condivise, e la
        // documentazione di SQLite dice che WAL non funziona su file system di rete
        // (https://www.sqlite.org/wal.html). DELETE è il giornale predefinito di SQLite e
        // funziona ovunque; un file già in WAL viene convertito al primo salvataggio.
        QStringLiteral("PRAGMA journal_mode=DELETE"),
        QStringLiteral("PRAGMA synchronous=FULL"),
        QStringLiteral("PRAGMA application_id=%1").arg(kApplicationId),
        QStringLiteral("CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS maps(id TEXT PRIMARY KEY,title TEXT NOT NULL,sort_order INTEGER NOT NULL,created_at INTEGER NOT NULL,modified_at INTEGER NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS nodes(id TEXT PRIMARY KEY,map_id TEXT NOT NULL,parent_id TEXT,sort_order INTEGER NOT NULL,payload BLOB NOT NULL,FOREIGN KEY(map_id) REFERENCES maps(id) ON DELETE CASCADE)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS nodes_tree ON nodes(map_id,parent_id,sort_order)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS map_state(map_id TEXT PRIMARY KEY,payload BLOB NOT NULL,FOREIGN KEY(map_id) REFERENCES maps(id) ON DELETE CASCADE)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS assets(id TEXT PRIMARY KEY,sha256 BLOB NOT NULL UNIQUE,mime_type TEXT NOT NULL,file_name TEXT,byte_size INTEGER NOT NULL,data BLOB NOT NULL,thumbnail BLOB,thumbnail_mime TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS snapshots(id INTEGER PRIMARY KEY AUTOINCREMENT,created_at INTEGER NOT NULL,reason TEXT NOT NULL,payload BLOB NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS schema_migrations(version INTEGER PRIMARY KEY,applied_at INTEGER NOT NULL)"),
        // Anteprima JPEG della Home (2026-10-14): la Home la legge senza caricare la mappa.
        QStringLiteral("CREATE TABLE IF NOT EXISTS preview(id INTEGER PRIMARY KEY CHECK(id=1),jpeg BLOB NOT NULL,created_at INTEGER NOT NULL)")
    };
    for (const QString &statement : statements) if (!exec(q, statement, error)) return false;
    // Migrazione idempotente dei documenti SQLite creati dalla prima versione.
    bool hasThumbnail = false;
    if (q.exec(QStringLiteral("PRAGMA table_info(assets)"))) while (q.next())
        if (q.value(1).toString() == QStringLiteral("thumbnail")) hasThumbnail = true;
    if (!hasThumbnail) {
        if (!exec(q, QStringLiteral("ALTER TABLE assets ADD COLUMN thumbnail BLOB"), error)) return false;
        if (!exec(q, QStringLiteral("ALTER TABLE assets ADD COLUMN thumbnail_mime TEXT"), error)) return false;
    }
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO schema_migrations(version,applied_at) VALUES(?,?)"));
    q.addBindValue(kSchemaVersion); q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    return q.exec();
}

QString storeAsset(QSqlDatabase &db, const QString &path, QString *error) {
    if (path.isEmpty()) return {};
    const QFileInfo info(path);
    const AssetCacheEntry cached = cachedAsset(info.absoluteFilePath());
    if (!cached.id.isEmpty() && cached.size == info.size() && cached.modified == info.lastModified().toMSecsSinceEpoch()) {
        QSqlQuery present(db); present.prepare(QStringLiteral("SELECT 1 FROM assets WHERE id=?")); present.addBindValue(cached.id);
        if (present.exec() && present.next()) return cached.id;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return {}; }
    const QByteArray data = file.readAll();
    const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    const QString id = QString::fromLatin1(hash.toHex());
    const QString mime = QMimeDatabase().mimeTypeForFile(path).name();
    QByteArray thumbnail;
    QString thumbnailMime;
    if (mime.startsWith(QStringLiteral("image/"))) {
        QBuffer sourceBuffer; sourceBuffer.setData(data); sourceBuffer.open(QIODevice::ReadOnly);
        QImageReader reader(&sourceBuffer); reader.setAutoTransform(true);
        const QSize original = reader.size();
        if (original.isValid()) reader.setScaledSize(original.scaled(QSize(512, 512), Qt::KeepAspectRatio));
        const QImage image = reader.read();
        if (!image.isNull()) {
            // JPEG has no alpha: a transparent sticker would reopen on black, so those stay PNG.
            const bool alpha = image.hasAlphaChannel();
            QBuffer thumbnailBuffer(&thumbnail); thumbnailBuffer.open(QIODevice::WriteOnly);
            image.save(&thumbnailBuffer, alpha ? "PNG" : "JPG", alpha ? -1 : 72);
            thumbnailMime = alpha ? QStringLiteral("image/png") : QStringLiteral("image/jpeg");
        }
    }
    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO assets(id,sha256,mime_type,file_name,byte_size,data,thumbnail,thumbnail_mime) VALUES(?,?,?,?,?,?,?,?)"));
    query.addBindValue(id); query.addBindValue(hash);
    query.addBindValue(mime);
    query.addBindValue(QFileInfo(path).fileName()); query.addBindValue(data.size()); query.addBindValue(data);
    query.addBindValue(thumbnail); query.addBindValue(thumbnail.isEmpty() ? QString() : thumbnailMime);
    if (!query.exec() && error) *error = query.lastError().text();
    if (query.lastError().type() != QSqlError::NoError) return {};
    rememberAsset(info.absoluteFilePath(), {info.size(), info.lastModified().toMSecsSinceEpoch(), id});
    return id;
}

// B5/B11 (2026-10-13): funzioni di supporto per non perdere gli asset già incorporati.
bool hasAsset(QSqlDatabase &db, const QString &id) {
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT 1 FROM main.assets WHERE id=?")); query.addBindValue(id);
    return query.exec() && query.next();
}

// Copia un asset dal file di origine collegato come "source" (vedi saveDocument).
bool copyAssetFromSource(QSqlDatabase &db, const QString &id) {
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO main.assets(id,sha256,mime_type,file_name,byte_size,data,thumbnail,thumbnail_mime) "
        "SELECT id,sha256,mime_type,file_name,byte_size,data,thumbnail,thumbnail_mime FROM source.assets WHERE id=?"));
    query.addBindValue(id);
    return query.exec() && hasAsset(db, id);
}

// Asset salvato in precedenza per lo stesso nodo in questo file (campo idField/nameField del payload).
QPair<QString, QString> previousNodeAsset(QSqlDatabase &db, const QString &nodeId, const QString &idField, const QString &nameField) {
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT payload FROM main.nodes WHERE id=?")); query.addBindValue(nodeId);
    if (!query.exec() || !query.next()) return {};
    const QJsonObject payload = QJsonDocument::fromJson(query.value(0).toByteArray()).object();
    const QString id = payload.value(idField).toString();
    if (id.isEmpty() || !hasAsset(db, id)) return {};
    return {id, payload.value(nameField).toString()};
}

QString materializeAsset(QSqlDatabase &db, const QString &id, const QString &fileName, bool thumbnail, QString *error) {
    // BUG-043: the id and the name come from the document and form the cache path. An id is always
    // a SHA-256 in hex (storeAsset); anything else ("../..") could write outside the cache.
    static const QRegularExpression assetId(QStringLiteral("^[0-9a-f]{64}$"));
    static const QRegularExpression plainSuffix(QStringLiteral("^[A-Za-z0-9]{1,16}$"));
    if (!assetId.match(id).hasMatch()) return {};
    const QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/embedded-assets/") + QFileInfo(db.databaseName()).completeBaseName();
    if (!QDir().mkpath(root)) { if (error) *error = QObject::tr("Cannot create the attachment cache."); return {}; }
    QString suffix = QFileInfo(fileName).suffix();
    if (!plainSuffix.match(suffix).hasMatch()) suffix.clear();
    if (thumbnail) {  // the thumbnail is JPEG or, for images with transparency, PNG
        QSqlQuery type(db); type.prepare(QStringLiteral("SELECT thumbnail_mime FROM assets WHERE id=?")); type.addBindValue(id);
        if (type.exec() && type.next() && !type.value(0).toString().isEmpty())
            suffix = type.value(0).toString() == QStringLiteral("image/png") ? QStringLiteral("png") : QStringLiteral("jpg");
    }
    const QString path = root + QLatin1Char('/') + id + (thumbnail ? QStringLiteral("-thumb") : QString())
        + (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);
    if (QFileInfo::exists(path)) {
        const QFileInfo info(path);
        rememberAsset(info.absoluteFilePath(), {info.size(), info.lastModified().toMSecsSinceEpoch(), id});
        return path;
    }
    QSqlQuery query(db);
    query.prepare(thumbnail ? QStringLiteral("SELECT CASE WHEN length(thumbnail)>0 THEN thumbnail ELSE data END FROM assets WHERE id=?")
                            : QStringLiteral("SELECT data FROM assets WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) { if (error) *error = query.lastError().text(); return {}; }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(query.value(0).toByteArray()) < 0 || !output.commit()) {
        if (error) *error = output.errorString();
        return {};
    }
    const QFileInfo info(path);
    rememberAsset(info.absoluteFilePath(), {info.size(), info.lastModified().toMSecsSinceEpoch(), id});
    return path;
}

bool addSnapshot(QSqlDatabase &db, const QString &reason, const QByteArray &payload, QString *error) {
    const QByteArray compressed = qCompress(payload, 6);
    QSqlQuery latest(db);
    latest.exec(QStringLiteral("SELECT payload FROM snapshots ORDER BY id DESC LIMIT 1"));
    if (latest.next() && latest.value(0).toByteArray() == compressed) return true;
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("INSERT INTO snapshots(created_at,reason,payload) VALUES(?,?,?)"));
    insert.addBindValue(QDateTime::currentMSecsSinceEpoch()); insert.addBindValue(reason);
    insert.addBindValue(compressed);
    if (!insert.exec()) { if (error) *error = insert.lastError().text(); return false; }
    QSqlQuery trim(db);
    return exec(trim, QStringLiteral("DELETE FROM snapshots WHERE id NOT IN (SELECT id FROM snapshots ORDER BY id DESC LIMIT %1)").arg(kMaximumSnapshots), error);
}
}

namespace {
bool embedNodeFiles(QSqlDatabase &db, bool sourceAttached, QJsonObject &node, QString *error);
QJsonArray loadedNodeFiles(const QJsonArray &stored);
}

bool BmapStorage::save(const QString &path, const MindMapModel &model, SaveReason reason, QString *error, const QString &assetSourcePath) {
    return saveDocument(path, model.toJson(), reason, error, assetSourcePath);
}

bool BmapStorage::saveDocument(const QString &path, const QJsonObject &document, SaveReason reason, QString *error,
                               const QString &assetSourcePath, const QString &mapId,
                               const QString &mapTitle, int sortOrder) {
    const QString name = connectionName();
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        // B11 (2026-10-13): con "Salva con nome" i nodi aperti da un altro .mentis hanno già l'id
        // dell'asset, che però sta nel file vecchio: il file nuovo restava senza immagini. Il file
        // di origine si collega con ATTACH (fuori dalla transazione, come richiede SQLite) e se
        // ne leggono solo le righe di assets. Se il collegamento fallisce si salva come prima.
        const auto attachSource = [&] {
            if (assetSourcePath.isEmpty() || !QFileInfo::exists(assetSourcePath) ||
                QFileInfo(assetSourcePath).absoluteFilePath() == QFileInfo(path).absoluteFilePath()) return false;
            QSqlQuery attach(db);
            attach.prepare(QStringLiteral("ATTACH DATABASE ? AS source"));
            attach.addBindValue(QFileInfo(assetSourcePath).absoluteFilePath());
            return attach.exec();
        };
        bool sourceAttached = false;
        if (!db.open()) { if (error) *error = db.lastError().text(); }
        else if (createSchema(db, error) && (sourceAttached = attachSource(), db.transaction())) {
            QJsonObject root = document;
            const QJsonObject documentRoot = root.value(QStringLiteral("root")).toObject();
            QJsonArray nodes;
            // Garantisce che l'asset di un nodo sia nel file che si sta scrivendo.
            // - id già noto ma assente qui (B11, "Salva con nome"): copiato dal file di origine;
            //   se non c'è, e il percorso è un file esistente, lo si incorpora di nuovo da lì.
            // - id vuoto e file originale sparito (B5): prima ogni salvataggio, anche quello
            //   automatico e quello di chiusura, falliva. Si riusa l'asset già salvato per lo
            //   stesso nodo in questo file, se c'è.
            // Restituisce false (con *error) solo se l'asset è nuovo e il file non si legge.
            const auto ensureAsset = [&](QString &id, QString &assetName, const QString &filePath, const QString &nodeId,
                                         const QString &idField, const QString &nameField) {
                if (!id.isEmpty()) {
                    if (hasAsset(db, id) || (sourceAttached && copyAssetFromSource(db, id))) return true;
                    if (!QFileInfo(filePath).isFile()) return true;  // nessuna alternativa: come prima, si tiene l'id
                    const QString stored = storeAsset(db, filePath, error);
                    if (stored.isEmpty()) return false;
                    id = stored;  // assetName resta quello originale
                    return true;
                }
                if (filePath.isEmpty()) return true;
                if (!QFileInfo::exists(filePath)) {
                    const auto previous = previousNodeAsset(db, nodeId, idField, nameField);
                    if (!previous.first.isEmpty()) { id = previous.first; assetName = previous.second; return true; }
                    return true;  // BUG-037: as ensureNodeAsset with keepMissing (this is only for images)
                }
                id = storeAsset(db, filePath, error);
                if (id.isEmpty()) return false;
                assetName = QFileInfo(filePath).fileName();
                return true;
            };
            // 2026-10-14: con error == nullptr un allegato illeggibile faceva saltare il ramo, che poi
            // veniva cancellato dal file (DELETE dei nodi mancanti). Ora l'errore è un flag locale.
            bool assetFailed = false;
            std::function<void(QJsonObject, const QString &, int)> flatten = [&](QJsonObject node, const QString &parent, int row) {
                if (assetFailed) return;
                QJsonArray children = node.take(QStringLiteral("children")).toArray();
                const QString imagePath = node.value(QStringLiteral("image")).toString();
                QString imageId = node.value(QStringLiteral("imageAssetId")).toString();
                QString imageName = node.value(QStringLiteral("imageOriginalName")).toString();
                const QString nodeId = node.value(QStringLiteral("id")).toString();
                // B5/B11 (2026-10-13): vedi ensureAsset.
                if (!ensureAsset(imageId, imageName, imagePath, nodeId, QStringLiteral("imageAsset"), QStringLiteral("imageName"))) { assetFailed = true; return; }
                if (!imageId.isEmpty()) { node[QStringLiteral("imageAsset")] = imageId; node[QStringLiteral("imageName")] = imageName; node.remove(QStringLiteral("image")); }
                if (!embedNodeFiles(db, sourceAttached, node, error)) { assetFailed = true; return; }  // FEAT-049
                node[QStringLiteral("parentId")] = parent; node[QStringLiteral("row")] = row; nodes.append(node);
                for (int i = 0; i < children.size(); ++i) flatten(children[i].toObject(), node.value(QStringLiteral("id")).toString(), i);
            };
            flatten(root.take(QStringLiteral("root")).toObject(), QString(), 0);
            if (assetFailed || (error && !error->isEmpty())) { db.rollback(); if (error && error->isEmpty()) *error = QObject::tr("Cannot embed an attachment."); }
            else {
                QSqlQuery old(db); old.prepare(QStringLiteral("SELECT payload FROM map_state WHERE map_id=?"));
                old.addBindValue(mapId); old.exec();
                QByteArray oldState; if (old.next()) oldState = old.value(0).toByteArray();
                // P1 (2026-10-13): prima, a OGNI salvataggio (anche quello automatico dopo ogni
                // modifica), si rileggevano e riserializzavano tutti i nodi del file per costruire
                // oldPayload, che serve solo quando si stanno per cancellare molti nodi. Ora si
                // conta prima e la copia si costruisce solo in quel caso.
                QSqlQuery count(db); count.prepare(QStringLiteral("SELECT COUNT(*) FROM nodes WHERE map_id=?"));
                count.addBindValue(mapId); count.exec(); int oldCount = count.next() ? count.value(0).toInt() : 0;
                const bool largeDeletion = oldCount - nodes.size() >= kLargeDeletionThreshold;
                const auto oldPayload = [&] {
                    QJsonArray oldNodes;
                    QSqlQuery oldNodeQuery(db); oldNodeQuery.prepare(QStringLiteral("SELECT payload FROM nodes WHERE map_id=? ORDER BY parent_id,sort_order"));
                    oldNodeQuery.addBindValue(mapId); oldNodeQuery.exec();
                    while (oldNodeQuery.next()) oldNodes.append(QJsonDocument::fromJson(oldNodeQuery.value(0).toByteArray()).object());
                    return QJsonDocument(QJsonObject{{QStringLiteral("state"), QJsonDocument::fromJson(oldState).object()},
                                                     {QStringLiteral("nodes"), oldNodes}}).toJson(QJsonDocument::Compact);
                };
                if (largeDeletion && !oldState.isEmpty() && !addSnapshot(db, QStringLiteral("before-large-deletion"), oldPayload(), error)) db.rollback();
                else {
                    const qint64 now = QDateTime::currentMSecsSinceEpoch();
                    QSqlQuery map(db); map.prepare(QStringLiteral("INSERT INTO maps(id,title,sort_order,created_at,modified_at) VALUES(?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET title=excluded.title,sort_order=excluded.sort_order,modified_at=excluded.modified_at"));
                    map.addBindValue(mapId);
                    map.addBindValue(mapTitle.isEmpty() ? documentRoot.value(QStringLiteral("title")).toString() : mapTitle);
                    map.addBindValue(sortOrder); map.addBindValue(now); map.addBindValue(now);
                    bool writeOk = map.exec();
                    QSqlQuery liveIds(db);
                    if (writeOk) writeOk = liveIds.exec(QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS current_node_ids(id TEXT PRIMARY KEY)"));
                    if (writeOk) writeOk = liveIds.exec(QStringLiteral("DELETE FROM current_node_ids"));
                    QSqlQuery rememberId(db); rememberId.prepare(QStringLiteral("INSERT INTO current_node_ids(id) VALUES(?)"));
                    QSqlQuery nodeQuery(db);
                    nodeQuery.prepare(QStringLiteral(
                        "INSERT INTO nodes(id,map_id,parent_id,sort_order,payload) VALUES(?,?,?,?,?) "
                        "ON CONFLICT(id) DO UPDATE SET map_id=excluded.map_id,parent_id=excluded.parent_id,sort_order=excluded.sort_order,payload=excluded.payload "
                        "WHERE nodes.map_id<>excluded.map_id OR nodes.parent_id<>excluded.parent_id OR nodes.sort_order<>excluded.sort_order OR nodes.payload<>excluded.payload"));
                    for (const QJsonValue &value : nodes) {
                        if (!writeOk) break;
                        const QJsonObject node = value.toObject();
                        const QString id = node.value(QStringLiteral("id")).toString();
                        rememberId.bindValue(0, id); writeOk = rememberId.exec();
                        nodeQuery.bindValue(0, id); nodeQuery.bindValue(1, mapId);
                        nodeQuery.bindValue(2, node.value(QStringLiteral("parentId")).toString()); nodeQuery.bindValue(3, node.value(QStringLiteral("row")).toInt());
                        nodeQuery.bindValue(4, QJsonDocument(node).toJson(QJsonDocument::Compact));
                        if (writeOk) writeOk = nodeQuery.exec();
                    }
                    QSqlQuery removeMissing(db);
                    if (writeOk) { removeMissing.prepare(QStringLiteral("DELETE FROM nodes WHERE map_id=? AND id NOT IN (SELECT id FROM current_node_ids)")); removeMissing.addBindValue(mapId); writeOk = removeMissing.exec(); }
                    root[QStringLiteral("format")] = QStringLiteral("mentis-sqlite-1");
                    root[QStringLiteral("rootId")] = documentRoot.value(QStringLiteral("id")).toString();
                    const QByteArray state = QJsonDocument(root).toJson(QJsonDocument::Compact);
                    QSqlQuery stateQuery(db); stateQuery.prepare(QStringLiteral("INSERT INTO map_state(map_id,payload) VALUES(?,?) ON CONFLICT(map_id) DO UPDATE SET payload=excluded.payload"));
                    stateQuery.addBindValue(mapId); stateQuery.addBindValue(state);
                    if (writeOk) writeOk = stateQuery.exec();
                    QString completedReason;
                    if (reason == SaveReason::Explicit) completedReason = QStringLiteral("explicit");
                    else if (reason == SaveReason::Closing) completedReason = QStringLiteral("closing");
                    const QByteArray currentPayload = QJsonDocument(QJsonObject{{QStringLiteral("state"), root},
                                                                                 {QStringLiteral("nodes"), nodes}}).toJson(QJsonDocument::Compact);
                    if (writeOk && !completedReason.isEmpty()) writeOk = addSnapshot(db, completedReason, currentPayload, error);
                    if (writeOk) ok = db.commit();
                    else {
                        if (error && error->isEmpty()) *error = nodeQuery.lastError().isValid() ? nodeQuery.lastError().text() : db.lastError().text();
                        db.rollback();
                    }
                }
            }
            if (!ok && error && error->isEmpty()) *error = db.lastError().text();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

bool BmapStorage::load(const QString &path, MindMapModel &model, QString *error, bool integrityCheck, const QString &mapId) {
    const QString name = connectionName(); bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name); db.setDatabaseName(path);
        if (!db.open()) { if (error) *error = db.lastError().text(); }
        else if (!onlyMentisSchema(db, error)) {}
        else {
            QSqlQuery validation(db);
            bool valid = validation.exec(QStringLiteral("PRAGMA application_id")) && validation.next()
                && validation.value(0).toInt() == kApplicationId;
            if (!valid) {
                if (error) *error = QObject::tr("The file is not a valid Mentis document.");
            }
            if (valid && integrityCheck) valid = validation.exec(QStringLiteral("PRAGMA quick_check")) && validation.next()
                && validation.value(0).toString() == QStringLiteral("ok");
            if (error && valid == false && error->isEmpty())
                if (error) *error = QObject::tr("The document failed the integrity check.");
            QSqlQuery state(db); state.prepare(QStringLiteral("SELECT payload FROM map_state WHERE map_id=?")); state.addBindValue(mapId);
            if (!valid) { }
            else if (!state.exec() || !state.next()) { if (error) *error = QObject::tr("The Mentis document does not contain a valid map."); }
            else {
                QJsonObject root = QJsonDocument::fromJson(state.value(0).toByteArray()).object();
                const QString rootId = root.take(QStringLiteral("rootId")).toString();
                QHash<QString, QJsonObject> byId; QHash<QString, QList<QJsonObject>> byParent;
                QSqlQuery nodes(db); nodes.prepare(QStringLiteral("SELECT payload FROM nodes WHERE map_id=? ORDER BY sort_order")); nodes.addBindValue(mapId); nodes.exec();
                while (nodes.next()) {
                    QJsonObject node = QJsonDocument::fromJson(nodes.value(0).toByteArray()).object();
                    const QString imageId = node.take(QStringLiteral("imageAsset")).toString();
                    const QString imageName = node.take(QStringLiteral("imageName")).toString();
                    if (!imageId.isEmpty()) {
                        node[QStringLiteral("image")] = materializeAsset(db, imageId, imageName, true, error);
                        node[QStringLiteral("imageAssetId")] = imageId; node[QStringLiteral("imageOriginalName")] = imageName;
                    }
                    if (node.contains(QStringLiteral("files")))  // FEAT-049: si estraggono solo quando si aprono
                        node[QStringLiteral("files")] = loadedNodeFiles(node.value(QStringLiteral("files")).toArray());
                    byId.insert(node.value(QStringLiteral("id")).toString(), node);
                    byParent[node.value(QStringLiteral("parentId")).toString()].append(node);
                }
                if (rootId.isEmpty() || !byId.contains(rootId)) {
                    if (error) *error = QObject::tr("The map root is missing from the document.");
                } else {
                    // BUG-046: a topic met twice (a cycle, a repeated id) or a tree deeper than
                    // kMaxTreeDepth ended in endless recursion or a stack overflow: refused.
                    QSet<QString> built;
                    bool malformed = false;
                    std::function<QJsonObject(const QString &, int)> build = [&](const QString &id, int depth) {
                        QJsonObject node = byId.value(id); QJsonArray children;
                        if (malformed || depth > kMaxTreeDepth || built.contains(id)) { malformed = true; return node; }
                        built.insert(id);
                        auto list = byParent.value(id); std::sort(list.begin(), list.end(), [](const QJsonObject &a, const QJsonObject &b) { return a.value(QStringLiteral("row")).toInt() < b.value(QStringLiteral("row")).toInt(); });
                        for (const QJsonObject &child : list) children.append(build(child.value(QStringLiteral("id")).toString(), depth + 1));
                        node.remove(QStringLiteral("parentId")); node.remove(QStringLiteral("row")); node[QStringLiteral("children")] = children; return node;
                    };
                    root[QStringLiteral("root")] = build(rootId, 1);
                    if (malformed) { if (error) *error = QObject::tr("The file is not a valid Mentis document."); }
                    else ok = model.fromJson(root, error);
                }
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name); return ok;
}

QList<BmapStorage::MapInfo> BmapStorage::maps(const QString &path, QString *error) {
    const QString name = connectionName();
    QList<MapInfo> result;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (!db.open()) { if (error) *error = db.lastError().text(); }
        else if (!onlyMentisSchema(db, error)) {}
        else {
            QSqlQuery query(db);
            if (!query.exec(QStringLiteral("SELECT id,title,sort_order FROM maps ORDER BY sort_order,id"))) {
                if (error) *error = query.lastError().text();
            } else {
                while (query.next()) result.append({query.value(0).toString(), query.value(1).toString(), query.value(2).toInt()});
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return result;
}

bool BmapStorage::removeMapsExcept(const QString &path, const QStringList &mapIds, QString *error) {
    const QString name = connectionName();
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (!db.open()) { if (error) *error = db.lastError().text(); }
        else if (createSchema(db, error) && db.transaction()) {
            QSqlQuery query(db);
            QString sql = QStringLiteral("DELETE FROM maps");
            QStringList placeholders;
            for (qsizetype i = 0; i < mapIds.size(); ++i) placeholders.append(QStringLiteral("?"));
            if (!mapIds.isEmpty()) sql += QStringLiteral(" WHERE id NOT IN (%1)").arg(placeholders.join(QLatin1Char(',')));
            query.prepare(sql);
            for (const QString &id : mapIds) query.addBindValue(id);
            if (query.exec()) ok = db.commit();
            else { if (error) *error = query.lastError().text(); db.rollback(); }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

bool BmapStorage::isBmapDatabase(const QString &path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return false;
    return file.read(16) == QByteArrayLiteral("SQLite format 3\0");
}

// ---------------------------------------------------------------------------------------------
// Salvataggio del documento con più fogli in una sola transazione (2026-10-14).
// ---------------------------------------------------------------------------------------------
namespace {
// Stringa in formato JSON (con virgolette ed escape), per comporre il punto di ripristino senza
// rileggere e riserializzare i payload dei nodi, già JSON nel database.
QByteArray jsonString(const QString &text) {
    const QByteArray array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return array.mid(1, array.size() - 2);
}

// L'intero documento (tutti i fogli) come JSON compatto: è il contenuto dei punti di ripristino.
// Prima ogni foglio aggiungeva un punto proprio senza indicare a quale foglio apparteneva, e con
// più fogli il limite di 10 punti si esauriva in pochi salvataggi.
QByteArray documentPayload(QSqlDatabase &db) {
    QByteArray out = QByteArrayLiteral("{\"format\":\"mentis-document-snapshot-2\",\"maps\":[");
    QSqlQuery maps(db);
    maps.exec(QStringLiteral("SELECT m.id,m.title,m.sort_order,s.payload FROM maps m LEFT JOIN map_state s ON s.map_id=m.id ORDER BY m.sort_order,m.id"));
    bool firstMap = true;
    while (maps.next()) {
        const QString id = maps.value(0).toString();
        if (!firstMap) out += ',';
        firstMap = false;
        const QByteArray state = maps.value(3).toByteArray();
        out += "{\"id\":" + jsonString(id) + ",\"title\":" + jsonString(maps.value(1).toString())
             + ",\"sortOrder\":" + QByteArray::number(maps.value(2).toInt())
             + ",\"state\":" + (state.isEmpty() ? QByteArrayLiteral("{}") : state) + ",\"nodes\":[";
        QSqlQuery nodes(db);
        nodes.prepare(QStringLiteral("SELECT payload FROM nodes WHERE map_id=? ORDER BY parent_id,sort_order,id"));
        nodes.addBindValue(id);
        nodes.exec();
        bool firstNode = true;
        while (nodes.next()) {
            if (!firstNode) out += ',';
            firstNode = false;
            out += nodes.value(0).toByteArray();
        }
        out += "]}";
    }
    out += "]";
    // FEAT-079: the document's resources belong to the recovery point too.
    QSqlQuery resources(db);
    if (resources.exec(QStringLiteral("SELECT value FROM metadata WHERE key='resources'")) && resources.next())
        out += ",\"resources\":" + resources.value(0).toString().toUtf8();
    out += "}";
    return out;
}

// FEAT-079: the resources as compact JSON in metadata; an empty object removes the key.
bool writeResources(QSqlDatabase &db, const QJsonObject &resources, QString *error) {
    QSqlQuery query(db);
    if (resources.isEmpty()) {
        query.prepare(QStringLiteral("DELETE FROM metadata WHERE key='resources'"));
    } else {
        query.prepare(QStringLiteral("INSERT INTO metadata(key,value) VALUES('resources',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
        query.addBindValue(QString::fromUtf8(QJsonDocument(resources).toJson(QJsonDocument::Compact)));
    }
    if (query.exec()) return true;
    if (error) *error = query.lastError().text();
    return false;
}

bool mapHasContent(QSqlDatabase &db, const QString &mapId) {
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT 1 FROM map_state WHERE map_id=?"));
    query.addBindValue(mapId);
    return query.exec() && query.next();
}

int nodeCount(QSqlDatabase &db, const QString &mapId) {
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM nodes WHERE map_id=?"));
    query.addBindValue(mapId);
    return query.exec() && query.next() ? query.value(0).toInt() : 0;
}

// Garantisce che l'asset di un nodo sia nel file che si sta scrivendo: stesse regole della lambda
// ensureAsset di saveDocument (B5/B11), qui come funzione per il salvataggio a fogli.
// BUG-037: keepMissing (a node's image): a new file that is gone from the disk and was never
// embedded leaves the path in the node instead of failing the save; before, every save of the map
// failed until the file came back.
bool ensureNodeAsset(QSqlDatabase &db, bool sourceAttached, QString &id, QString &assetName, const QString &filePath,
                     const QString &nodeId, const QString &idField, const QString &nameField, bool keepMissing,
                     QString *error) {
    if (!id.isEmpty()) {
        if (hasAsset(db, id) || (sourceAttached && copyAssetFromSource(db, id))) return true;
        if (!QFileInfo(filePath).isFile()) return true;  // nessuna alternativa: si tiene l'id
        const QString stored = storeAsset(db, filePath, error);
        if (stored.isEmpty()) return false;
        id = stored;
        return true;
    }
    if (filePath.isEmpty()) return true;
    if (!QFileInfo::exists(filePath)) {
        const auto previous = previousNodeAsset(db, nodeId, idField, nameField);
        if (!previous.first.isEmpty()) { id = previous.first; assetName = previous.second; return true; }
        if (keepMissing) return true;
    }
    id = storeAsset(db, filePath, error);
    if (id.isEmpty()) return false;
    assetName = QFileInfo(filePath).fileName();
    return true;
}

// FEAT-049: file allegati del nodo. Nel modello {path, assetId, name, size}; nel file {asset, name,
// size}: ogni file finisce negli assets (deduplicato con SHA-256, letto una volta grazie alla cache
// di storeAsset). Un file gia' incorporato che non si trova piu' (ne' qui ne' nel documento di
// origine) e senza file sul disco si perde: non c'e' altro da cui recuperarlo.
bool embedNodeFiles(QSqlDatabase &db, bool sourceAttached, QJsonObject &node, QString *error) {
    if (!node.contains(QStringLiteral("files"))) return true;
    QJsonArray stored;
    for (const QJsonValue &value : node.value(QStringLiteral("files")).toArray()) {
        const QJsonObject file = value.toObject();
        QString id = file.value(QStringLiteral("assetId")).toString();
        QString name = file.value(QStringLiteral("name")).toString();
        if (!ensureNodeAsset(db, sourceAttached, id, name, file.value(QStringLiteral("path")).toString(), QString(), QString(), QString(), false, error))
            return false;
        if (id.isEmpty()) continue;
        stored.append(QJsonObject{{QStringLiteral("asset"), id}, {QStringLiteral("name"), name},
                                  {QStringLiteral("size"), file.value(QStringLiteral("size"))}});
    }
    node[QStringLiteral("files")] = stored;
    return true;
}

QJsonArray loadedNodeFiles(const QJsonArray &stored) {
    QJsonArray files;
    for (const QJsonValue &value : stored) {
        const QJsonObject file = value.toObject();
        files.append(QJsonObject{{QStringLiteral("assetId"), file.value(QStringLiteral("asset"))},
                                 {QStringLiteral("name"), file.value(QStringLiteral("name"))},
                                 {QStringLiteral("size"), file.value(QStringLiteral("size"))}});
    }
    return files;
}

// Scrive nodi e stato di un foglio (dentro la transazione aperta dal chiamante).
bool writeSheetContent(QSqlDatabase &db, const BmapStorage::SheetWrite &sheet, bool sourceAttached, QString *error) {
    const QJsonObject mapObject = sheet.content.value(QStringLiteral("@map"));
    const QString rootId = mapObject.value(QStringLiteral("rootId")).toString();
    if (rootId.isEmpty() || !sheet.content.contains(rootId)) {
        if (error) *error = QObject::tr("The sheet \"%1\" does not contain a valid root.").arg(sheet.title);
        return false;
    }
    QSqlQuery remember(db);
    if (!remember.exec(QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS current_node_ids(id TEXT PRIMARY KEY)")) ||
        !remember.exec(QStringLiteral("DELETE FROM current_node_ids"))) {
        if (error) *error = remember.lastError().text();
        return false;
    }
    remember.prepare(QStringLiteral("INSERT INTO current_node_ids(id) VALUES(?)"));
    QSqlQuery nodeQuery(db);
    // Come saveDocument: le righe identiche non vengono riscritte (meno pagine sporche su disco).
    nodeQuery.prepare(QStringLiteral(
        "INSERT INTO nodes(id,map_id,parent_id,sort_order,payload) VALUES(?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET map_id=excluded.map_id,parent_id=excluded.parent_id,sort_order=excluded.sort_order,payload=excluded.payload "
        "WHERE nodes.map_id<>excluded.map_id OR nodes.parent_id<>excluded.parent_id OR nodes.sort_order<>excluded.sort_order OR nodes.payload<>excluded.payload"));
    for (auto it = sheet.content.cbegin(); it != sheet.content.cend(); ++it) {
        if (it.key() == QStringLiteral("@map")) continue;
        QJsonObject node = it.value();
        const QString parent = node.take(QStringLiteral("parent")).toString();
        const int row = node.take(QStringLiteral("row")).toInt();
        const QString nodeId = node.value(QStringLiteral("id")).toString(it.key());
        QString imageId = node.value(QStringLiteral("imageAssetId")).toString();
        QString imageName = node.value(QStringLiteral("imageOriginalName")).toString();
        if (!ensureNodeAsset(db, sourceAttached, imageId, imageName, node.value(QStringLiteral("image")).toString(), nodeId,
                             QStringLiteral("imageAsset"), QStringLiteral("imageName"), true, error)) return false;
        if (!imageId.isEmpty()) { node[QStringLiteral("imageAsset")] = imageId; node[QStringLiteral("imageName")] = imageName; node.remove(QStringLiteral("image")); }
        if (!embedNodeFiles(db, sourceAttached, node, error)) return false;  // FEAT-049
        node[QStringLiteral("parentId")] = parent;
        node[QStringLiteral("row")] = row;
        remember.bindValue(0, nodeId);
        nodeQuery.bindValue(0, nodeId); nodeQuery.bindValue(1, sheet.id);
        nodeQuery.bindValue(2, parent); nodeQuery.bindValue(3, row);
        nodeQuery.bindValue(4, QJsonDocument(node).toJson(QJsonDocument::Compact));
        if (!remember.exec() || !nodeQuery.exec()) {
            if (error) *error = nodeQuery.lastError().isValid() ? nodeQuery.lastError().text() : remember.lastError().text();
            return false;
        }
    }
    QSqlQuery removeMissing(db);
    removeMissing.prepare(QStringLiteral("DELETE FROM nodes WHERE map_id=? AND id NOT IN (SELECT id FROM current_node_ids)"));
    removeMissing.addBindValue(sheet.id);
    if (!removeMissing.exec()) { if (error) *error = removeMissing.lastError().text(); return false; }
    QJsonObject state = mapObject;
    state[QStringLiteral("format")] = QStringLiteral("mentis-sqlite-1");
    QSqlQuery stateQuery(db);
    stateQuery.prepare(QStringLiteral("INSERT INTO map_state(map_id,payload) VALUES(?,?) ON CONFLICT(map_id) DO UPDATE SET payload=excluded.payload WHERE map_state.payload<>excluded.payload"));
    stateQuery.addBindValue(sheet.id);
    stateQuery.addBindValue(QJsonDocument(state).toJson(QJsonDocument::Compact));
    if (!stateQuery.exec()) { if (error) *error = stateQuery.lastError().text(); return false; }
    return true;
}

bool writePreview(QSqlDatabase &db, const QByteArray &jpeg, QString *error) {
    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO preview(id,jpeg,created_at) VALUES(1,?,?) ON CONFLICT(id) DO UPDATE SET jpeg=excluded.jpeg,created_at=excluded.created_at"));
    query.addBindValue(jpeg);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (query.exec()) return true;
    if (error) *error = query.lastError().text();
    return false;
}
}

bool BmapStorage::saveSheets(const QString &path, const QList<SheetWrite> &sheets, SaveReason reason, QString *error,
                             const QString &assetSourcePath, const QByteArray &previewJpeg,
                             const std::optional<QJsonObject> &resources) {
    if (sheets.isEmpty()) { if (error) *error = QObject::tr("The document contains no sheets."); return false; }
    const QString name = connectionName();
    bool ok = false;
    QString localError;
    QString *message = error ? error : &localError;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (!db.open()) {
            *message = db.lastError().text();
        } else if (createSchema(db, message)) {
            // Come saveDocument (B11): il file di origine si collega fuori dalla transazione.
            bool sourceAttached = false;
            if (!assetSourcePath.isEmpty() && QFileInfo::exists(assetSourcePath) &&
                QFileInfo(assetSourcePath).absoluteFilePath() != QFileInfo(path).absoluteFilePath()) {
                QSqlQuery attach(db);
                attach.prepare(QStringLiteral("ATTACH DATABASE ? AS source"));
                attach.addBindValue(QFileInfo(assetSourcePath).absoluteFilePath());
                sourceAttached = attach.exec();
            }
            if (!db.transaction()) {
                *message = db.lastError().text();
            } else {
                bool writeOk = true;
                // Fogli da scrivere: quelli modificati e quelli che nel file non esistono ancora.
                QList<const SheetWrite *> contentSheets;
                for (const SheetWrite &sheet : sheets) {
                    if (sheet.writeContent || !mapHasContent(db, sheet.id)) {
                        if (sheet.content.isEmpty()) {
                            *message = QObject::tr("The sheet \"%1\" was not loaded and is not in the file.").arg(sheet.title);
                            writeOk = false;
                            break;
                        }
                        contentSheets.append(&sheet);
                    }
                }
                // Punto di ripristino prima di una cancellazione grande (almeno 10 nodi in un foglio).
                if (writeOk) {
                    bool largeDeletion = false;
                    for (const SheetWrite *sheet : std::as_const(contentSheets))
                        if (nodeCount(db, sheet->id) - (sheet->content.size() - 1) >= kLargeDeletionThreshold) largeDeletion = true;
                    // FEAT-004: a deleted sheet always leaves a recovery point (deleting a sheet cannot be undone).
                    QSet<QString> listed;
                    for (const SheetWrite &sheet : sheets) listed.insert(sheet.id);
                    QSqlQuery stored(db);
                    if (stored.exec(QStringLiteral("SELECT id FROM maps")))
                        while (!largeDeletion && stored.next())
                            if (!listed.contains(stored.value(0).toString())) largeDeletion = true;
                    if (largeDeletion) writeOk = addSnapshot(db, QStringLiteral("before-large-deletion"), documentPayload(db), message);
                }
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                QSqlQuery map(db);
                map.prepare(QStringLiteral("INSERT INTO maps(id,title,sort_order,created_at,modified_at) VALUES(?,?,?,?,?) "
                                           "ON CONFLICT(id) DO UPDATE SET title=excluded.title,sort_order=excluded.sort_order,"
                                           "modified_at=CASE WHEN ? THEN excluded.modified_at ELSE maps.modified_at END"));
                for (const SheetWrite &sheet : sheets) {
                    if (!writeOk) break;
                    const bool content = contentSheets.contains(&sheet);
                    QString title = sheet.title;
                    if (title.isEmpty()) {
                        const QJsonObject mapObject = sheet.content.value(QStringLiteral("@map"));
                        title = sheet.content.value(mapObject.value(QStringLiteral("rootId")).toString()).value(QStringLiteral("title")).toString();
                    }
                    map.bindValue(0, sheet.id); map.bindValue(1, title); map.bindValue(2, sheet.sortOrder);
                    map.bindValue(3, now); map.bindValue(4, now); map.bindValue(5, content ? 1 : 0);
                    writeOk = map.exec();
                    if (!writeOk) *message = map.lastError().text();
                    else if (content) writeOk = writeSheetContent(db, sheet, sourceAttached, message);
                }
                // Fogli eliminati dal documento (le righe di nodes e map_state seguono per ON DELETE CASCADE).
                if (writeOk) {
                    QStringList placeholders;
                    for (qsizetype i = 0; i < sheets.size(); ++i) placeholders.append(QStringLiteral("?"));
                    QSqlQuery remove(db);
                    remove.prepare(QStringLiteral("DELETE FROM maps WHERE id NOT IN (%1)").arg(placeholders.join(QLatin1Char(','))));
                    for (const SheetWrite &sheet : sheets) remove.addBindValue(sheet.id);
                    writeOk = remove.exec();
                    if (!writeOk) *message = remove.lastError().text();
                }
                if (writeOk && !previewJpeg.isEmpty()) writeOk = writePreview(db, previewJpeg, message);
                if (writeOk && resources) writeOk = writeResources(db, *resources, message);
                // Un solo punto di ripristino per salvataggio esplicito o chiusura, con tutto il documento.
                // addSnapshot non ne aggiunge uno identico all'ultimo.
                QString reasonName;
                if (reason == SaveReason::Explicit) reasonName = QStringLiteral("explicit");
                else if (reason == SaveReason::Closing) reasonName = QStringLiteral("closing");
                if (writeOk && !reasonName.isEmpty()) writeOk = addSnapshot(db, reasonName, documentPayload(db), message);
                if (writeOk) ok = db.commit();
                if (!ok) {
                    if (message->isEmpty()) *message = db.lastError().text();
                    db.rollback();
                }
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

QJsonObject BmapStorage::loadResources(const QString &path) {
    if (!QFileInfo(path).isFile()) return {};
    const QString name = connectionName();
    QJsonObject resources;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=2000"));
        if (db.open() && onlyMentisSchema(db, nullptr)) {
            QSqlQuery query(db);
            if (query.exec(QStringLiteral("SELECT value FROM metadata WHERE key='resources'")) && query.next())
                resources = QJsonDocument::fromJson(query.value(0).toString().toUtf8()).object();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return resources;
}

QByteArray BmapStorage::loadPreview(const QString &path) {
    if (!QFileInfo(path).isFile()) return {};
    const QString name = connectionName();
    QByteArray jpeg;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        // Sola lettura; se il salvataggio automatico sta scrivendo si attende fino a 2 s invece di
        // fallire subito (https://doc.qt.io/qt-6/qsqldatabase.html#setConnectOptions).
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=2000"));
        if (db.open() && onlyMentisSchema(db, nullptr)) {
            QSqlQuery query(db);
            if (query.exec(QStringLiteral("SELECT jpeg FROM preview WHERE id=1")) && query.next()) jpeg = query.value(0).toByteArray();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return jpeg;
}

QString BmapStorage::originalAssetPath(const QString &path, const QString &assetId, const QString &fileName, QString *error) {
    if (assetId.isEmpty() || !QFileInfo(path).isFile()) return {};
    const QString name = connectionName();
    QString result;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        // Come loadPreview: sola lettura, attesa breve se il salvataggio automatico sta scrivendo.
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=2000"));
        if (!db.open()) { if (error) *error = db.lastError().text(); }
        else if (onlyMentisSchema(db, error)) result = materializeAsset(db, assetId, fileName, false, error);
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return result;
}

bool BmapStorage::savePreview(const QString &path, const QByteArray &jpeg, QString *error) {
    if (jpeg.isEmpty()) return true;
    const QString name = connectionName();
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (!db.open()) { if (error) *error = db.lastError().text(); }
        else if (createSchema(db, error)) ok = writePreview(db, jpeg, error);
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}
