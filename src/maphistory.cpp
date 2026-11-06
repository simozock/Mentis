#include "maphistory.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

#include <type_traits>

namespace {
// Campo di un oggetto JSON cercato con la chiave di un altro oggetto, senza copiarla in una QString
// (QJsonObject::constFind non accetta QAnyStringView ne' UTF-8).
QJsonObject::const_iterator findField(const QJsonObject &object, QAnyStringView key) {
    return key.visit([&object](auto view) {
        if constexpr (std::is_same_v<decltype(view), QUtf8StringView>) return object.constFind(view.toString());
        else return object.constFind(view);
    });
}

// The key without copying it (keyView and toStringView exist since Qt 6.10,
// https://doc.qt.io/qt-6/qjsonobject-const-iterator.html#keyView); Qt 6.8 and 6.9 copy it.
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
QAnyStringView keyOf(QJsonObject::const_iterator field) { return field.keyView(); }
#else
QString keyOf(QJsonObject::const_iterator field) { return field.key(); }
#endif

// Uguaglianza di QJsonValue (stesso risultato di operator==), ma i testi si confrontano sul dato
// interno dell'oggetto (toStringView): operator== li copiava prima in due QString.
bool sameValue(QJsonValueConstRef a, QJsonValueConstRef b) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    if (a.type() == QJsonValue::String && b.type() == QJsonValue::String) return a.toStringView() == b.toStringView();
#endif
    return a == b;
}

// Campi cambiati fra due stati di un nodo: in `before` e `after` i soli campi diversi; un campo
// assente in uno dei due stati vale Null (B12, vedi MapHistory::commit e apply).
void diffFields(const QJsonObject &previous, const QJsonObject &next, QJsonObject &before, QJsonObject &after) {
    qsizetype shared = 0;  // campi presenti in entrambi gli stati
    for (auto field = next.constBegin(); field != next.constEnd(); ++field) {
        const auto old = findField(previous, keyOf(field));
        if (old == previous.constEnd()) {
            before.insert(field.key(), QJsonValue::Null);
            after.insert(field.key(), field.value());
            continue;
        }
        ++shared;
        if (!sameValue(old.value(), field.value())) {
            before.insert(field.key(), old.value());
            after.insert(field.key(), field.value());
        }
    }
    // Campi spariti: solo se qualche campo di prima non e' stato trovato nel nuovo stato.
    if (shared == previous.size()) return;
    for (auto field = previous.constBegin(); field != previous.constEnd(); ++field)
        if (findField(next, keyOf(field)) == next.constEnd()) {
            before.insert(field.key(), field.value());
            after.insert(field.key(), QJsonValue::Null);
        }
}

// Come diffFields quando i due stati hanno le stesse chiavi nello stesso ordine: i campi si
// confrontano per posizione, senza cercarli. Falso (e risultato da scartare) se le chiavi non
// coincidono; non presuppone nessun ordine delle chiavi in QJsonObject.
bool diffSameKeys(const QJsonObject &previous, const QJsonObject &next, QJsonObject &before, QJsonObject &after) {
    if (previous.size() != next.size()) return false;
    for (auto old = previous.constBegin(), field = next.constBegin(); field != next.constEnd(); ++old, ++field) {
        if (keyOf(old) != keyOf(field)) return false;
        if (!sameValue(old.value(), field.value())) {
            before.insert(field.key(), old.value());
            after.insert(field.key(), field.value());
        }
    }
    return true;
}
}

MapHistory::MapHistory(int cacheSteps) : m_cacheSteps(qMax(1, cacheSteps)) { initializeSessionStore(); }

MapHistory::~MapHistory() {
    const QString connection = m_connectionName;
    if (m_database.isValid()) m_database.close();
    m_database = {};
    if (!connection.isEmpty()) QSqlDatabase::removeDatabase(connection);
    // Chiusura regolare: lo storico non deve sopravvivere alla sessione.
    if (!m_sessionPath.isEmpty()) QFile::remove(m_sessionPath);
}

void MapHistory::initializeSessionStore() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/sessions");
    QDir().mkpath(directory);
    // P8 (2026-10-13): i giornali dell'annulla rimasti da sessioni chiuse male (una chiusura
    // regolare li cancella già) si eliminano dopo 7 giorni. Quelli recenti potrebbero essere di
    // un'altra finestra aperta, quindi non si toccano.
    const QDateTime limit = QDateTime::currentDateTime().addDays(-7);
    for (const QFileInfo &file : QDir(directory).entryInfoList({QStringLiteral("*.undo"), QStringLiteral("*.undo-wal"), QStringLiteral("*.undo-shm")}, QDir::Files))
        if (file.lastModified() < limit) QFile::remove(file.absoluteFilePath());
    m_sessionPath = directory + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".undo");
    m_connectionName = QStringLiteral("mentis-history-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(m_sessionPath);
    if (!m_database.open()) return;
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    query.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    query.exec(QStringLiteral("CREATE TABLE steps(sequence INTEGER PRIMARY KEY,payload BLOB NOT NULL)"));
    query.exec(QStringLiteral("CREATE TABLE session(key TEXT PRIMARY KEY,value TEXT NOT NULL)"));
    query.prepare(QStringLiteral("INSERT INTO session(key,value) VALUES('started_at',?)"));
    query.addBindValue(QString::number(QDateTime::currentMSecsSinceEpoch())); query.exec();
}

void MapHistory::clear() {
    m_steps.clear();
    m_cacheStart = 0;
    m_stepCount = 0;
    m_index = 0;
    m_savedIndex = 0;
    m_sheets.clear();
    m_lastChangedKeys.clear();
    if (m_database.isOpen()) { QSqlQuery query(m_database); query.exec(QStringLiteral("DELETE FROM steps")); }
}

void MapHistory::setSheet(const QString &sheet, Snapshot current) { m_sheets.insert(sheet, std::move(current)); }

void MapHistory::removeSheet(const QString &sheet) {
    m_sheets.remove(sheet);
    m_lastChangedKeys.remove(sheet);
    // Every step, oldest first: after flushWindow() the journal holds them all.
    std::vector<Step> all;
    if (m_database.isOpen()) {
        flushWindow();
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral("SELECT payload FROM steps WHERE sequence<? ORDER BY sequence"));
        query.addBindValue(m_stepCount);
        if (query.exec()) while (query.next()) all.push_back(decodeStep(query.value(0).toByteArray()));
        if (static_cast<int>(all.size()) != m_stepCount) {
            // Unreadable journal: losing Undo is safe, applying a partial history is not.
            const QHash<QString, Snapshot> sheets = m_sheets;
            clear();
            m_sheets = sheets;
            m_savedIndex = -1;
            return;
        }
    } else {
        all = std::move(m_steps);
    }
    std::vector<Step> kept;
    int index = 0;
    int saved = m_savedIndex < 0 ? -1 : 0;
    for (int sequence = 0; sequence < static_cast<int>(all.size()); ++sequence) {
        Step &step = all[sequence];
        std::erase_if(step.changes, [&sheet](const Change &change) { return change.sheet == sheet; });
        if (step.changes.empty()) continue;
        if (step.sheet == sheet) step.sheet = step.changes.front().sheet;
        if (sequence < m_index) ++index;
        if (saved >= 0 && sequence < m_savedIndex) ++saved;
        kept.push_back(std::move(step));
    }
    m_stepCount = static_cast<int>(kept.size());
    m_index = index;
    m_savedIndex = saved;
    m_cacheStart = 0;
    m_steps = std::move(kept);
    if (!m_database.isOpen()) return;
    QSqlQuery(m_database).exec(QStringLiteral("DELETE FROM steps"));
    flushWindow();
    m_steps.clear();
    if (m_stepCount) loadWindow(qMax(0, m_index - 1));
}

const MapHistory::Snapshot &MapHistory::current(const QString &sheet) const {
    static const Snapshot empty;
    const auto it = m_sheets.constFind(sheet);
    return it == m_sheets.cend() ? empty : it.value();
}

QString MapHistory::undoSheet() { return canUndo() ? stepAt(m_index - 1).sheet : QString(); }
QString MapHistory::redoSheet() { return canRedo() ? stepAt(m_index).sheet : QString(); }

QStringList MapHistory::Step::sheets() const {
    QStringList result{sheet};
    for (const Change &change : changes)
        if (!result.contains(change.sheet)) result.append(change.sheet);
    return result;
}

QStringList MapHistory::undoSheets() { return canUndo() ? stepAt(m_index - 1).sheets() : QStringList(); }
QStringList MapHistory::redoSheets() { return canRedo() ? stepAt(m_index).sheets() : QStringList(); }

void MapHistory::diffSheet(const QString &sheet, const Snapshot &state, const Snapshot &next, std::vector<Change> &changes) {
    // Nodi aggiunti o modificati: si confrontano campo per campo e si salvano solo
    // i campi diversi, così spostare un nodo registra appena "x" e "y".
    for (auto it = next.cbegin(); it != next.cend(); ++it) {
        const auto previous = state.constFind(it.key());
        if (previous == state.cend()) {
            changes.push_back({Kind::Added, it.key(), {}, it.value(), sheet});
            continue;
        }
        if (previous.value() == it.value()) continue;
        const QJsonObject &before = previous.value();
        const QJsonObject &after = it.value();
        Change change{Kind::Modified, it.key(), {}, {}, sheet};
        // (B12, 2026-10-13: un campo che compare o sparisce, come titleHtml che esiste solo per i
        // titoli formattati, non veniva registrato: QJsonObject::insert con un valore Undefined
        // toglie la chiave invece di salvarla. Ctrl+Z non toglieva né rimetteva la formattazione.
        // Ora "campo assente" si registra come Null e apply() lo toglie.)
        // Di solito i due stati hanno le stesse chiavi nello stesso ordine (cambia solo qualche
        // valore): allora i campi si confrontano in coppia per posizione. Altrimenti ogni chiave si
        // cerca nell'altro stato senza copiarla in una QString (keyView). Prima si allocava una
        // QString per ogni campo di ogni nodo cambiato e la si cercava: con 1.000 nodi spostati
        // era la maggior parte del confronto (misura in FEAT-022). Passo identico a prima.
        if (!diffSameKeys(before, after, change.before, change.after)) {
            change.before = {};
            change.after = {};
            diffFields(before, after, change.before, change.after);
        }
        changes.push_back(std::move(change));
    }
    // Nodi rimossi.
    for (auto it = state.cbegin(); it != state.cend(); ++it)
        if (!next.contains(it.key())) changes.push_back({Kind::Removed, it.key(), it.value(), {}, sheet});
}

bool MapHistory::commit(const QString &sheet, Snapshot next) {
    QList<std::pair<QString, Snapshot>> sheets;
    sheets.append({sheet, std::move(next)});
    return commit(std::move(sheets));
}

bool MapHistory::commit(QList<std::pair<QString, Snapshot>> sheets) {
    Step step;
    // Fogli mai registrati: il loro primo stato diventa la base, senza differenze.
    for (auto it = sheets.begin(); it != sheets.end();) {
        if (!m_sheets.contains(it->first)) { m_sheets.insert(it->first, std::move(it->second)); it = sheets.erase(it); }
        else ++it;
    }
    if (sheets.isEmpty()) return false;
    step.sheet = sheets.front().first;
    for (const auto &[sheet, next] : std::as_const(sheets)) diffSheet(sheet, m_sheets.value(sheet), next, step.changes);
    if (step.changes.empty()) return false;
    // Stato corrente dei fogli (prima: il membro m_current), aggiornato quando il passo e' registrato.
    auto adopt = [this, &sheets] { for (auto &[sheet, next] : sheets) m_sheets[sheet] = std::move(next); };

    // Una modifica fatta dopo uno o più annulla cancella i passi successivi.
    if (canRedo()) {
        if (m_database.isOpen()) { QSqlQuery query(m_database); query.prepare(QStringLiteral("DELETE FROM steps WHERE sequence>=?")); query.addBindValue(m_index); query.exec(); }
        m_stepCount = m_index;
        if (m_index <= m_cacheStart) { m_steps.clear(); m_cacheStart = m_index; }
        else if (m_index < m_cacheStart + static_cast<int>(m_steps.size())) m_steps.resize(m_index - m_cacheStart);
        if (m_savedIndex > m_index) m_savedIndex = -1;
    }
    // Se la cartella dati non e' scrivibile, Undo resta disponibile con una cache
    // puramente in memoria. Si perdono solo i passi oltre il limite, mai il documento.
    if (!m_database.isOpen()) {
        m_steps.push_back(std::move(step));
        m_stepCount = static_cast<int>(m_steps.size());
        m_index = m_stepCount;
        if (m_stepCount > m_cacheSteps) {
            m_steps.erase(m_steps.begin());
            --m_stepCount; --m_index;
            if (m_savedIndex >= 0) --m_savedIndex;
        }
        adopt();
        return true;
    }
    if (!m_steps.empty() && m_stepCount >= m_cacheStart + m_cacheSteps) {
        // Il blocco pieno viene scritto con una sola transazione. Durante la normale
        // creazione di una mappa non c'e' quindi una scrittura SQLite per ogni comando.
        flushWindow();
        m_steps.clear();
        m_cacheStart = m_stepCount;
    }
    m_steps.push_back(std::move(step));
    ++m_stepCount;
    ++m_index;
    adopt();
    return true;
}

const MapHistory::Snapshot &MapHistory::undo() {
    if (!canUndo()) return current(QString());
    --m_index;
    const Step &step = stepAt(m_index);
    const QString sheet = step.sheet;  // stepAt può ricaricare la finestra: si copia prima
    apply(step, false);
    return current(sheet);
}

const MapHistory::Snapshot &MapHistory::redo() {
    if (!canRedo()) return current(QString());
    const Step &step = stepAt(m_index);
    const QString sheet = step.sheet;
    apply(step, true);
    ++m_index;
    return current(sheet);
}

void MapHistory::flushWindow() {
    if (!m_database.isOpen()) return;
    if (!m_database.transaction()) return;
    QSqlQuery query(m_database); query.prepare(QStringLiteral("INSERT OR REPLACE INTO steps(sequence,payload) VALUES(?,?)"));
    bool ok = true;
    for (int offset = 0; offset < static_cast<int>(m_steps.size()) && ok; ++offset) {
        query.bindValue(0, m_cacheStart + offset);
        query.bindValue(1, encodeStep(m_steps[offset]));
        ok = query.exec();
    }
    ok ? m_database.commit() : m_database.rollback();
}

QByteArray MapHistory::encodeStep(const Step &step) {
    QJsonArray array;
    for (const auto &change : step.changes) {
        QJsonObject object{{QStringLiteral("kind"), static_cast<int>(change.kind)}, {QStringLiteral("key"), change.key},
                           {QStringLiteral("before"), change.before}, {QStringLiteral("after"), change.after}};
        // FEAT-027: foglio della modifica solo se diverso da quello del passo (passi di piu' fogli).
        if (change.sheet != step.sheet) object.insert(QStringLiteral("sheet"), change.sheet);
        array.append(object);
    }
    const QJsonObject payload{{QStringLiteral("sheet"), step.sheet}, {QStringLiteral("changes"), array}};
    return qCompress(QJsonDocument(payload).toJson(QJsonDocument::Compact), 3);
}

MapHistory::Step MapHistory::decodeStep(const QByteArray &payload) {
    Step step;
    const QJsonObject object = QJsonDocument::fromJson(qUncompress(payload)).object();
    step.sheet = object.value(QStringLiteral("sheet")).toString();
    const QJsonArray array = object.value(QStringLiteral("changes")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject change = value.toObject();
        step.changes.push_back({static_cast<Kind>(change.value(QStringLiteral("kind")).toInt()),
                                change.value(QStringLiteral("key")).toString(),
                                change.value(QStringLiteral("before")).toObject(),
                                change.value(QStringLiteral("after")).toObject(),
                                change.value(QStringLiteral("sheet")).toString(step.sheet)});
    }
    return step;
}

const MapHistory::Step &MapHistory::stepAt(int sequence) {
    if (sequence < m_cacheStart || sequence >= m_cacheStart + static_cast<int>(m_steps.size())) loadWindow(sequence);
    static const Step empty;
    const int offset = sequence - m_cacheStart;
    return offset >= 0 && offset < static_cast<int>(m_steps.size()) ? m_steps[offset] : empty;
}

void MapHistory::loadWindow(int sequence) {
    flushWindow();
    m_steps.clear();
    m_cacheStart = qMax(0, (sequence / m_cacheSteps) * m_cacheSteps);
    if (!m_database.isOpen()) return;
    QSqlQuery query(m_database); query.prepare(QStringLiteral("SELECT payload FROM steps WHERE sequence>=? AND sequence<? ORDER BY sequence"));
    query.addBindValue(m_cacheStart); query.addBindValue(m_cacheStart + m_cacheSteps);
    if (!query.exec()) return;
    while (query.next()) m_steps.push_back(decodeStep(query.value(0).toByteArray()));
}

void MapHistory::apply(const Step &step, bool forward) {
    m_lastChangedKeys.clear();
    m_lastSheet = step.sheet;
    for (const auto &change : step.changes) {
        // Ogni modifica si applica al suo foglio (2026-10-14; FEAT-027: anche piu' fogli per passo).
        Snapshot &state = m_sheets[change.sheet];
        m_lastChangedKeys[change.sheet].append(change.key);
        const bool insertWhole = (change.kind == Kind::Added && forward) || (change.kind == Kind::Removed && !forward);
        const bool removeWhole = (change.kind == Kind::Added && !forward) || (change.kind == Kind::Removed && forward);
        if (insertWhole) {
            state.insert(change.key, change.kind == Kind::Added ? change.after : change.before);
        } else if (removeWhole) {
            state.remove(change.key);
        } else {
            QJsonObject &target = state[change.key];
            const QJsonObject &fields = forward ? change.after : change.before;
            // B12: Null = campo assente in quello stato (vedi commit()).
            for (auto field = fields.begin(); field != fields.end(); ++field) {
                if (field.value().isNull()) target.remove(field.key());
                else target.insert(field.key(), field.value());
            }
        }
    }
}
