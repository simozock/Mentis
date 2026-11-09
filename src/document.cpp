#include "document.h"

#include <functional>
#include "filevalidation.h"
#include "markers.h"
#include "richresources.h"

#include <QCryptographicHash>

#include "markdownimport.h"
#include "mapstructure.h"
#include "mindmapmodel.h"
#include "officeexport.h"
#include "performance.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QDir>
#include <QHash>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

// FEAT-066 tappa 1 (2026-10-18): stato e operazioni spostati da MainWindow senza cambiarne il
// comportamento; i commenti con data spiegano le scelte fatte allora, ancora valide.

Document::Document(QObject *parent)
    : QObject(parent), m_model(new MindMapModel(this)), m_history(Performance::historyCacheSteps()),
      m_historyTimer(new QTimer(this)), m_autoSaveTimer(new QTimer(this)),
      m_autoSaveWatcher(new QFutureWatcher<QPair<bool, QString>>(this)) {
    m_historyTimer->setSingleShot(true);
    m_historyTimer->setInterval(150);
    // Il passo di storico si registra a modifica conclusa: con un tasto del mouse ancora premuto
    // (trascinamento in corso) si rimanda, cosi' un intero trascinamento e' un solo Ctrl+Z.
    connect(m_historyTimer, &QTimer::timeout, this, [this] {
        if (m_interactionInProgress && m_interactionInProgress()) { m_historyTimer->start(); return; }
        commitHistory();
    });
    m_autoSaveTimer->setSingleShot(true);
    // Il ritardo raggruppa la digitazione senza far attendere l'utente e senza sincronizzare il disco
    // a ogni tasto: 750 ms con il profilo Standard, 2 s con quello Leggero (2026-10-14).
    m_autoSaveTimer->setInterval(Performance::autoSaveDelayMs());
    connect(m_autoSaveTimer, &QTimer::timeout, this, &Document::startAutoSave);
    connect(m_autoSaveWatcher, &QFutureWatcher<QPair<bool, QString>>::finished, this, [this] {
        const auto result = m_autoSaveWatcher->result();
        m_saveInProgress = false;
        if (!result.first) {
            emit saveStateChanged();
            emit autoSaveFailed(result.second);
            return;
        }
        if (m_autoSaveSerial == m_changeSerial && !m_filePath.isEmpty()) {
            setSaved();
        } else if (m_autoSaveSerial == m_changeSerial) {
            // Mappa senza nome: i fogli sono nella copia di lavoro, ma il documento resta da salvare.
            m_dirtySheets.clear();
            emit saveStateChanged();
        } else {
            m_autoSaveTimer->start();
            emit saveStateChanged();
        }
    });
    resetUntitledRecovery();
}

Document::~Document() { waitForAutoSave(); }

void Document::waitForAutoSave() {
    if (m_autoSaveWatcher->isRunning()) m_autoSaveWatcher->waitForFinished();
}

void Document::setAutoSaveDelay(int ms) { m_autoSaveTimer->setInterval(ms); }

// --- Fogli ---

QString Document::currentSheetId() const {
    return m_currentSheet >= 0 && m_currentSheet < m_sheets.size() ? m_sheets[m_currentSheet].id : QString();
}

int Document::sheetIndex(const QString &id) const {
    for (int i = 0; i < m_sheets.size(); ++i) if (m_sheets[i].id == id) return i;
    return -1;
}

MapHistory::Snapshot Document::sheetContent(int index) const {
    if (index < 0 || index >= m_sheets.size()) return {};
    if (index == m_currentSheet) return m_model->toFlatSnapshot();
    const QString &id = m_sheets[index].id;
    return m_history.hasSheet(id) ? m_history.current(id) : m_sheets[index].content;
}

MapHistory::Snapshot Document::historySnapshot(const QString &sheetId) {
    const int index = sheetIndex(sheetId);
    if (index < 0) return {};
    if (!m_history.hasSheet(sheetId)) m_history.setSheet(sheetId, m_sheets[index].content);
    return m_history.current(sheetId);
}

void Document::resetSheets(const QString &name) {
    m_sheets = {{QStringLiteral("main"), name, m_model->toFlatSnapshot(), true}};
    ++m_documentGeneration;
    m_currentSheet = 0;
    m_dirtySheets.clear();
    m_sheetListChanged = false;
    emit sheetsChanged();
}

// 2026-10-14: il contenuto dei fogli e' nella forma piatta dello storico, cosi' il salvataggio
// automatico non riconverte la mappa in JSON sul thread dell'interfaccia.
void Document::captureCurrentSheet() {
    if (m_currentSheet < 0 || m_currentSheet >= m_sheets.size()) return;
    // commitHistory() registra l'eventuale modifica in attesa e copia lo stato nel foglio.
    if (m_historyTimer->isActive()) m_historyTimer->stop();
    commitHistory();
}

Document::Result Document::ensureSheetLoaded(int index) {
    if (index < 0 || index >= m_sheets.size()) return {false, tr("The sheet is not available.")};
    Sheet &sheet = m_sheets[index];
    if (sheet.loaded) return {};
    if (m_filePath.isEmpty()) return {false, tr("The sheet is not available.")};
    // Come open(): si aspetta il salvataggio automatico, che scrive nello stesso file.
    waitForAutoSave();
    MindMapModel loaded;
    QString error;
    if (!BmapStorage::load(m_filePath, loaded, &error, false, sheet.id)) return {false, error};
    sheet.content = loaded.toFlatSnapshot();
    sheet.loaded = true;
    return {};
}

void Document::addSheet(const std::function<void(MindMapModel &)> &prepare, const std::function<void()> &layout) {
    captureCurrentSheet();
    MindMapModel blank;
    blank.copyThemeFrom(*m_model);  // FEAT-036: il foglio nuovo prende il tema del foglio attivo
    if (prepare) prepare(blank);
    const QString name = tr("Sheet %1").arg(m_sheets.size() + 1);
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_sheets.append({id, name, blank.toFlatSnapshot(), true});
    m_history.setSheet(id, m_sheets.last().content);
    m_dirtySheets.insert(id);
    m_sheetListChanged = true;
    switchSheet(m_sheets.size() - 1);
    if (layout) {
        // The blank model has no measured nodes: without this the topics stayed stacked until
        // the next edit laid the map out, and Undo of that edit stacked them again.
        layout();
        m_historyTimer->stop();
        m_sheets.last().content = m_model->toFlatSnapshot();
        m_history.setSheet(id, m_sheets.last().content);
    }
    markChanged();  // avvia il salvataggio automatico del nuovo foglio
}

Document::Result Document::switchSheet(int index) {
    if (index < 0 || index >= m_sheets.size()) return {false, tr("The sheet is not available.")};
    if (index == m_currentSheet) return {};
    captureCurrentSheet();
    Result result = ensureSheetLoaded(index);
    if (result.ok) {
        const Sheet &sheet = m_sheets[index];
        // Lo storico conosce gia' i fogli aperti in questa sessione: il loro stato e' quello giusto.
        if (!m_history.hasSheet(sheet.id)) m_history.setSheet(sheet.id, sheet.content);
        m_restoringHistory = true;
        QString error;
        result.ok = m_model->fromFlatSnapshot(m_history.current(sheet.id), &error);
        result.error = error;
        m_restoringHistory = false;
    }
    if (!result.ok) return result;
    m_currentSheet = index;
    updateDirtyState();
    emitHistoryIfChanged();
    emit sheetsChanged();
    return {};
}

bool Document::renameSheet(int index, const QString &name) {
    const QString clean = name.simplified();
    if (index < 0 || index >= m_sheets.size() || clean.isEmpty() || clean == m_sheets[index].name) return false;
    m_sheets[index].name = clean;
    sheetListModified();
    return true;
}

void Document::sheetListModified() {
    m_sheetListChanged = true;  // every save writes all the titles and orders: modified until then
    emit sheetsChanged();
    // Modified and autosaved like markChanged(), but no history step: Ctrl+Z keeps the change.
    ++m_changeSerial;
    m_autoSaveTimer->start();
    setDirtyFlag(true);
}

bool Document::moveSheet(int from, int to) {
    if (from < 0 || from >= m_sheets.size() || to < 0 || to >= m_sheets.size() || from == to) return false;
    const QString current = currentSheetId();
    m_sheets.move(from, to);
    m_currentSheet = sheetIndex(current);
    sheetListModified();
    return true;
}

namespace {

// FEAT-004: the sheet's flat content with new ids for its topics, relationships, summaries and
// boundaries. Every string equal to an old id is replaced, so the references inside the sheet
// (parents, relationship ends, summary and boundary topics, topic links, Gantt dependencies) follow
// the copy; ids of other sheets are not in the table and stay as they are.
MapHistory::Snapshot withFreshIds(const MapHistory::Snapshot &content) {
    static const QString kMapKey = QStringLiteral("@map");
    QHash<QString, QString> fresh;
    const auto add = [&fresh](const QString &id) {
        if (!id.isEmpty() && !fresh.contains(id)) fresh.insert(id, QUuid::createUuid().toString(QUuid::WithoutBraces));
    };
    for (auto it = content.cbegin(); it != content.cend(); ++it) if (it.key() != kMapKey) add(it.key());
    const QJsonObject map = content.value(kMapKey);
    for (const QString &key : {QStringLiteral("relations"), QStringLiteral("summaries"), QStringLiteral("boundaries")})
        for (const QJsonValue &entry : map.value(key).toArray()) {
            add(entry.toObject().value(QStringLiteral("id")).toString());
            add(entry.toObject().value(QStringLiteral("topic")).toObject().value(QStringLiteral("id")).toString());
        }
    const std::function<QJsonValue(const QJsonValue &)> remap = [&](const QJsonValue &value) -> QJsonValue {
        if (value.isString()) return fresh.value(value.toString(), value.toString());
        if (value.isArray()) {
            QJsonArray array;
            for (const QJsonValue &item : value.toArray()) array.append(remap(item));
            return array;
        }
        if (value.isObject()) {
            QJsonObject object;
            const QJsonObject source = value.toObject();
            for (auto it = source.constBegin(); it != source.constEnd(); ++it) object.insert(it.key(), remap(it.value()));
            return object;
        }
        return value;
    };
    MapHistory::Snapshot copy;
    copy.reserve(content.size());
    for (auto it = content.cbegin(); it != content.cend(); ++it)
        copy.insert(fresh.value(it.key(), it.key()), remap(it.value()).toObject());
    return copy;
}

}  // namespace

Document::Result Document::duplicateSheet(int index) {
    if (index < 0 || index >= m_sheets.size()) return {false, tr("The sheet is not available.")};
    captureCurrentSheet();
    if (const Result loaded = ensureSheetLoaded(index); !loaded.ok) return loaded;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const MapHistory::Snapshot content = withFreshIds(sheetContent(index));
    m_sheets.insert(index + 1, {id, tr("%1 (copy)").arg(m_sheets[index].name), content, true});
    if (m_currentSheet > index) ++m_currentSheet;
    m_history.setSheet(id, content);
    m_dirtySheets.insert(id);
    const Result shown = switchSheet(index + 1);
    sheetListModified();
    return shown;
}

Document::Result Document::removeSheet(int index) {
    if (index < 0 || index >= m_sheets.size() || m_sheets.size() < 2) return {false, tr("The sheet is not available.")};
    captureCurrentSheet();
    const QString id = m_sheets[index].id;
    if (index == m_currentSheet) {
        const Result shown = switchSheet(index > 0 ? index - 1 : 1);
        if (!shown.ok) return shown;
    }
    m_sheets.removeAt(index);
    if (index < m_currentSheet) --m_currentSheet;
    m_history.removeSheet(id);
    m_dirtySheets.remove(id);
    m_sheetListChanged = true;
    emit sheetsChanged();
    markChanged();  // the autosave drops the sheet from the file
    emitHistoryIfChanged();
    return {};
}

// --- File ---

void Document::resetUntitledRecovery() {
    if (!m_untitledRecoveryPath.isEmpty()) QFile::remove(m_untitledRecoveryPath);
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/recovery/untitled");
    QDir().mkpath(directory);
    m_untitledRecoveryPath = directory + QLatin1Char('/')
        + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".mentis");
}

void Document::startUntitled() {
    m_filePath.clear();
    m_fileLock.reset();
    resetUntitledRecovery();
    resetSheets(tr("Sheet %1").arg(1));
    setLoadedResources({});
    resetHistory();
}

Document::Result Document::acquireLock(const QString &path, std::unique_ptr<QLockFile> &lock) const {
    if (!m_filePath.isEmpty() && m_fileLock &&
        QFileInfo(path).absoluteFilePath() == QFileInfo(m_filePath).absoluteFilePath()) return {};  // gia' nostro
    auto candidate = std::make_unique<QLockFile>(path + QStringLiteral(".lock"));
    // Il blocco dura quanto il documento: niente scadenza a tempo. Un blocco rimasto da un crash
    // viene riconosciuto dal processo che non esiste piu' (https://doc.qt.io/qt-6/qlockfile.html).
    candidate->setStaleLockTime(0);
    if (candidate->tryLock(0)) { lock = std::move(candidate); return {}; }
    if (candidate->error() == QLockFile::LockFailedError)
        return {false, tr("The file is already open in another Mentis window."), true};
    // Cartella di sola lettura o file system senza lock: si apre senza blocco, come prima.
    lock.reset();
    return {};
}

Document::Result Document::open(const QString &path, bool integrityCheck, const std::function<void(bool)> &afterLoad) {
    // B10 (2026-10-13): si aspetta il salvataggio automatico in corso, che usa la cache globale
    // degli asset (bmapstorage.cpp) insieme all'apertura del file.
    waitForAutoSave();
    QString error;
    bool ok = false;
    const bool nativeMap = path.endsWith(QStringLiteral(".mentis"), Qt::CaseInsensitive);
    // Due istanze sullo stesso file salvavano entrambe e ne mescolavano il contenuto (2026-10-14).
    std::unique_ptr<QLockFile> newLock;
    if (nativeMap) {
        const Result locked = acquireLock(path, newLock);
        if (!locked.ok) return locked;
    }
    const bool sameFile = nativeMap && !m_filePath.isEmpty() &&
                          QFileInfo(path).absoluteFilePath() == QFileInfo(m_filePath).absoluteFilePath();
    if (path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {false, file.errorString()};
        ok = m_model->fromMarkdown(QString::fromUtf8(file.readAll()), &error);
        if (ok) resetSheets(tr("Sheet %1").arg(1));
    } else {
        const QList<BmapStorage::MapInfo> infos = BmapStorage::maps(path, &error);
        if (infos.isEmpty()) {
            ok = BmapStorage::load(path, *m_model, &error, integrityCheck);
            if (ok) resetSheets(tr("Sheet %1").arg(1));
        } else {
            ok = BmapStorage::load(path, *m_model, &error, integrityCheck, infos.front().id);
            if (ok) {
                QVector<Sheet> sheets;
                // The stored name, also for a file's only sheet (BUG-033: a renamed single sheet read back as "Sheet 1").
                for (int i = 0; i < infos.size(); ++i) {
                    sheets.append({infos[i].id, infos[i].title.isEmpty() ? tr("Sheet %1").arg(i + 1) : infos[i].title,
                                   i == 0 ? m_model->toFlatSnapshot() : MapHistory::Snapshot(), i == 0});
                }
                m_sheets = sheets;
                ++m_documentGeneration;
                m_currentSheet = 0;
                m_dirtySheets.clear();
                m_sheetListChanged = false;
                emit sheetsChanged();
            }
        }
    }
    if (!ok) return {false, error};
    setLoadedResources(nativeMap ? GanttResources::fromJson(BmapStorage::loadResources(path)) : GanttResources());
    if (nativeMap) { if (!sameFile) m_fileLock = std::move(newLock); }
    else m_fileLock.reset();
    if (!m_untitledRecoveryPath.isEmpty()) QFile::remove(m_untitledRecoveryPath);
    m_filePath = nativeMap ? path : QString();
    if (m_filePath.isEmpty()) resetUntitledRecovery();
    // L'interfaccia sistema il modello appena letto (per esempio la disposizione di un Markdown)
    // prima che diventi lo stato iniziale dello storico.
    if (afterLoad) afterLoad(nativeMap);
    resetHistory();
    return {};
}

QList<BmapStorage::SheetWrite> Document::sheetWrites(bool fullWrite) const {
    QList<BmapStorage::SheetWrite> writes;
    for (int i = 0; i < m_sheets.size(); ++i) {
        const Sheet &sheet = m_sheets[i];
        // Il contenuto si passa sempre per i fogli caricati (copia implicita, nessun costo): serve se
        // il file di destinazione non ha ancora quel foglio. writeContent dice se va riscritto.
        writes.append({sheet.id, sheet.name, i, sheet.loaded && (fullWrite || m_dirtySheets.contains(sheet.id)),
                       sheet.loaded ? sheet.content : MapHistory::Snapshot()});
    }
    return writes;
}

Document::Result Document::saveSheets(const QString &path, BmapStorage::SaveReason reason) {
    captureCurrentSheet();
    const bool otherFile = QFileInfo(path).absoluteFilePath() != QFileInfo(m_filePath).absoluteFilePath();
    // "Salva con nome": i fogli non ancora aperti si leggono dal file attuale per copiarli.
    if (otherFile) {
        for (int i = 0; i < m_sheets.size(); ++i) {
            const Result loaded = ensureSheetLoaded(i);
            if (!loaded.ok) return loaded;
        }
    }
    // Salvataggio esplicito e di chiusura: tutti i fogli caricati (la scrittura salta le righe
    // identiche) e l'anteprima della Home.
    const bool fullWrite = reason != BmapStorage::SaveReason::Automatic || otherFile;
    const QByteArray preview = reason == BmapStorage::SaveReason::Automatic || !m_previewJpeg ? QByteArray() : m_previewJpeg();
    QString error;
    if (!BmapStorage::saveSheets(path, sheetWrites(fullWrite), reason, &error, m_filePath, preview, resourcesWrite(fullWrite)))
        return {false, error};
    return {};
}

Document::Result Document::save(const QString &path, BmapStorage::SaveReason reason) {
    waitForAutoSave();
    flushPendingStep();
    std::unique_ptr<QLockFile> newLock;
    const bool otherFile = QFileInfo(path).absoluteFilePath() != QFileInfo(m_filePath).absoluteFilePath();
    if (otherFile) {
        const Result locked = acquireLock(path, newLock);
        if (!locked.ok) return locked;
    }
    m_saveInProgress = true;
    emit saveStateChanged();
    // B11: m_filePath e' ancora il file aperto; con "Salva con nome" le immagini incorporate si copiano da li'.
    const Result result = saveSheets(path, reason);
    if (!result.ok) {
        m_saveInProgress = false;
        emit saveStateChanged();
        return result;
    }
    if (otherFile) m_fileLock = std::move(newLock);
    m_filePath = path;
    m_dirtySheets.clear();
    setSaved();
    if (!m_untitledRecoveryPath.isEmpty() && QFileInfo(m_untitledRecoveryPath).absoluteFilePath() != QFileInfo(path).absoluteFilePath())
        QFile::remove(m_untitledRecoveryPath);
    m_saveInProgress = false;
    emit saveStateChanged();
    return {};
}

Document::Result Document::writeClosingPoint() {
    if (m_filePath.isEmpty()) return {};
    waitForAutoSave();
    return saveSheets(m_filePath, BmapStorage::SaveReason::Closing);
}

void Document::storePreview() {
    if (m_filePath.isEmpty() || m_dirty || !m_previewJpeg) return;
    waitForAutoSave();
    QString error;
    BmapStorage::savePreview(m_filePath, m_previewJpeg(), &error);  // facoltativa: un errore non blocca
}

void Document::close() {
    waitForAutoSave();
    if (m_filePath.isEmpty() && !m_untitledRecoveryPath.isEmpty()) QFile::remove(m_untitledRecoveryPath);
    m_fileLock.reset();
}

void Document::startAutoSave() {
    if (!m_dirty) return;
    if ((m_interactionInProgress && m_interactionInProgress()) || m_autoSaveWatcher->isRunning()) {
        m_autoSaveTimer->start();
        return;
    }
    captureCurrentSheet();  // registra la modifica in attesa e aggiorna il contenuto del foglio
    if (!m_dirty && m_dirtySheets.isEmpty()) return;
    const QString path = assetSourcePath();
    // Solo i fogli cambiati vengono riscritti; per gli altri si aggiornano nome e ordine.
    const QList<BmapStorage::SheetWrite> writes = sheetWrites(false);
    const std::optional<QJsonObject> resources = resourcesWrite(false);
    const QString assetSource = m_filePath;
    m_autoSaveSerial = m_changeSerial;
    m_saveInProgress = true;
    emit saveStateChanged();
    m_autoSaveWatcher->setFuture(QtConcurrent::run([path, writes, resources, assetSource] {
        QString error;
        const bool ok = BmapStorage::saveSheets(path, writes, BmapStorage::SaveReason::Automatic, &error, assetSource, {}, resources);
        return qMakePair(ok, error);
    }));
}

// --- Storico e stato modificato ---

bool Document::canUndo() const { return m_history.canUndo() || m_historyTimer->isActive(); }

void Document::setDirtyFlag(bool dirty) {
    m_dirty = dirty;
    emit dirtyChanged(m_dirty);
}

void Document::emitHistoryIfChanged() { emit historyChanged(); }

void Document::markChanged() {
    if (m_restoringHistory) return;
    // Si segna solo "modificato" e si avvia il timer: il confronto reale con lo stato salvato avviene
    // una volta sola in commitHistory() (prima si serializzava la mappa a ogni movimento del mouse).
    if (m_currentSheet >= 0 && m_currentSheet < m_sheets.size()) m_dirtySheets.insert(m_sheets[m_currentSheet].id);
    ++m_changeSerial;
    m_historyTimer->start();
    m_autoSaveTimer->start();
    setDirtyFlag(true);
    // Il passo non e' ancora registrato (timer), ma Ctrl+Z deve gia' funzionare: undo() registra
    // subito la modifica in attesa e poi la annulla.
    emitHistoryIfChanged();
}

void Document::flushPendingStep() {
    if (m_historyTimer->isActive()) { m_historyTimer->stop(); commitHistory(); }
}

void Document::setSaved() {
    // Le modifiche in attesa diventano un passo prima di segnare il punto salvato: restano annullabili.
    flushPendingStep();
    m_history.markSaved();
    m_dirtySheets.clear();
    m_sheetListChanged = false;  // il file ora contiene anche i fogli aggiunti (2026-10-14)
    m_autoSaveTimer->stop();
    setDirtyFlag(false);
    emit saveStateChanged();
}

void Document::commitHistory() {
    if (m_restoringHistory) return;
    if (m_currentSheet < 0 || m_currentSheet >= m_sheets.size()) return;
    const QString sheet = currentSheetId();
    if (m_history.commit(sheet, m_model->toFlatSnapshot())) m_dirtySheets.insert(sheet);
    // Il foglio condivide i dati con lo storico (copia implicita): nessuna seconda serializzazione.
    m_sheets[m_currentSheet].content = m_history.current(sheet);
    updateDirtyState();
    emitHistoryIfChanged();
}

void Document::commitLinkStep(std::optional<LinkSheetChange> other) {
    if (m_restoringHistory || m_currentSheet < 0 || m_currentSheet >= m_sheets.size()) return;
    m_historyTimer->stop();
    const QString sheet = currentSheetId();
    QList<LinkSheetChange> sheets{{sheet, m_model->toFlatSnapshot()}};
    if (other) sheets.append(*other);
    if (m_history.commit(sheets)) for (const auto &[id, content] : std::as_const(sheets)) m_dirtySheets.insert(id);
    m_sheets[m_currentSheet].content = m_history.current(sheet);
    if (other) {
        const int index = sheetIndex(other->first);
        if (index >= 0) m_sheets[index].content = m_history.current(other->first);
    }
    updateDirtyState();
    emitHistoryIfChanged();
}

int Document::replaceTitles(const QHash<QString, QHash<QString, QString>> &titles, const std::function<void()> &layout) {
    if (m_restoringHistory || m_currentSheet < 0 || m_currentSheet >= m_sheets.size()) return 0;
    flushPendingStep();
    const auto find = [](MindNode *root, const QString &id) {
        const std::function<MindNode *(MindNode *)> visit = [&](MindNode *node) -> MindNode * {
            if (node->id == id) return node;
            for (const auto &child : node->children) if (MindNode *found = visit(child.get())) return found;
            return nullptr;
        };
        return visit(root);
    };
    int count = 0;
    QList<LinkSheetChange> sheets;
    for (auto sheet = titles.cbegin(); sheet != titles.cend(); ++sheet) {
        if (sheet.key() == currentSheetId()) {
            for (auto it = sheet->cbegin(); it != sheet->cend(); ++it)
                if (MindNode *node = find(m_model->root(), it.key()); node && setTitle(node, it.value())) ++count;
            continue;
        }
        const int index = sheetIndex(sheet.key());
        if (index < 0 || !ensureSheetLoaded(index).ok) continue;
        MindMapModel other;
        if (!other.fromFlatSnapshot(historySnapshot(sheet.key()))) continue;
        int changed = 0;
        for (auto it = sheet->cbegin(); it != sheet->cend(); ++it) {
            MindNode *node = find(other.root(), it.key());
            if (!node || node->title == it.value()) continue;
            node->title = it.value();
            node->titleHtml.clear();  // the plain text replaces the formatted title, like setTitle
            ++changed;
        }
        if (changed) { sheets.append({sheet.key(), other.toFlatSnapshot()}); count += changed; }
    }
    if (!count) return 0;
    if (layout) layout();
    commitWithOtherSheets(sheets);
    return count;
}

int Document::replaceText(const std::function<bool(MindNode &)> &edit, const std::function<void()> &layout) {
    if (m_restoringHistory || m_currentSheet < 0 || m_currentSheet >= m_sheets.size()) return 0;
    flushPendingStep();
    const auto editTree = [&edit](MindNode *root, QList<MindNode *> *changed) {
        const std::function<void(MindNode *)> visit = [&](MindNode *node) {
            if (edit(*node)) changed->append(node);
            for (const auto &child : node->children) visit(child.get());
        };
        visit(root);
    };
    QList<MindNode *> active;
    editTree(m_model->root(), &active);
    int count = int(active.size());
    QList<LinkSheetChange> sheets;
    for (int i = 0; i < m_sheets.size(); ++i) {
        if (i == m_currentSheet || !ensureSheetLoaded(i).ok) continue;
        MindMapModel other;
        if (!other.fromFlatSnapshot(historySnapshot(m_sheets[i].id))) continue;
        QList<MindNode *> changed;
        editTree(other.root(), &changed);
        if (!changed.isEmpty()) { sheets.append({m_sheets[i].id, other.toFlatSnapshot()}); count += int(changed.size()); }
    }
    if (!count) return 0;
    if (!active.isEmpty()) {
        m_model->refreshStyles();  // a title's text decides its size
        emit nodesChanged(active);
        markChanged();
    }
    if (layout) layout();
    commitWithOtherSheets(sheets);
    return count;
}

int Document::renameLabel(const QString &from, const QString &to, const std::function<void()> &layout) {
    if (m_restoringHistory || m_currentSheet < 0 || m_currentSheet >= m_sheets.size() || from.trimmed().isEmpty()) return 0;
    flushPendingStep();
    const QString target = to.simplified();
    // The labels of a topic after the change; unchanged without `from`.
    const auto relabel = [&](const QStringList &labels) {
        if (std::none_of(labels.cbegin(), labels.cend(), [&](const QString &label) { return Labels::same(label, from); })) return labels;
        QStringList next;
        for (const QString &label : labels) {
            if (!Labels::same(label, from) && !(!target.isEmpty() && Labels::same(label, target))) next.append(label);
            else if (!target.isEmpty()) next.append(target);
        }
        return Labels::normalized(next, Labels::autoSort());
    };
    const auto forEach = [](MindNode *root, const std::function<void(MindNode *)> &apply) {
        const std::function<void(MindNode *)> visit = [&](MindNode *node) {
            apply(node);
            for (const auto &child : node->children) visit(child.get());
        };
        visit(root);
    };
    int count = 0;
    // Compared first: setLabels would also sort the labels of topics without `from`.
    forEach(m_model->root(), [&](MindNode *node) {
        const QStringList next = relabel(node->labels);
        if (next != node->labels && setLabels(node, next)) ++count;
    });
    QList<LinkSheetChange> sheets;
    for (int index = 0; index < m_sheets.size(); ++index) {
        if (index == m_currentSheet || !ensureSheetLoaded(index).ok) continue;  // unreadable sheet: left as it is
        const QString id = m_sheets[index].id;
        MindMapModel other;
        if (!other.fromFlatSnapshot(historySnapshot(id))) continue;
        int changed = 0;
        forEach(other.root(), [&](MindNode *node) {
            const QStringList next = relabel(node->labels);
            if (next != node->labels) { node->labels = next; ++changed; }
        });
        if (changed) { sheets.append({id, other.toFlatSnapshot()}); count += changed; }
    }
    if (!count) return 0;
    if (layout) layout();
    commitWithOtherSheets(sheets);
    return count;
}

void Document::commitWithOtherSheets(QList<LinkSheetChange> others) {
    m_historyTimer->stop();
    others.prepend({currentSheetId(), m_model->toFlatSnapshot()});
    if (m_history.commit(others)) for (const auto &[id, content] : std::as_const(others)) m_dirtySheets.insert(id);
    for (const auto &[id, content] : std::as_const(others)) {
        const int index = sheetIndex(id);
        if (index >= 0) m_sheets[index].content = m_history.current(id);
    }
    updateDirtyState();
    emitHistoryIfChanged();
}

void Document::updateDirtyState() {
    // Modificato = lo storico unico non e' al punto salvato, oppure ci sono fogli nuovi da scrivere.
    setDirtyFlag(!m_history.isAtSavedState() || m_sheetListChanged);
}

void Document::resetHistory() {
    m_historyTimer->stop();
    // 2026-10-14: storico unico vuoto, con lo stato iniziale di ogni foglio gia' caricato.
    m_history.clear();
    m_history.setSheet(kResourcesSheet, resourcesSnapshot(m_resources));
    if (m_currentSheet >= 0 && m_currentSheet < m_sheets.size()) m_sheets[m_currentSheet].content = m_model->toFlatSnapshot();
    for (const Sheet &sheet : std::as_const(m_sheets))
        if (sheet.loaded) m_history.setSheet(sheet.id, sheet.content);
    m_sheetListChanged = false;
    setDirtyFlag(false);
    emitHistoryIfChanged();
    emit saveStateChanged();
}

void Document::syncOtherHistorySheets(const QStringList &sheets) {
    for (const QString &sheet : sheets) {
        if (sheet == currentSheetId()) continue;
        if (sheet == kResourcesSheet) {
            m_resources = GanttResources::fromJson(m_history.current(sheet).value(kResourcesSheet));
            m_dirtySheets.insert(sheet);
            emit resourcesChanged();
            continue;
        }
        const int index = sheetIndex(sheet);
        if (index < 0) continue;
        m_sheets[index].content = m_history.current(sheet);
        m_sheets[index].loaded = true;
        m_dirtySheets.insert(sheet);
    }
}

Document::StepResult Document::undo() { return applyStep(false); }
Document::StepResult Document::redo() { return applyStep(true); }

Document::StepResult Document::applyStep(bool redo) {
    // Una modifica ancora in attesa del timer diventa subito un passo: Ctrl+Z annulla proprio quella.
    flushPendingStep();
    if (redo ? !m_history.canRedo() : !m_history.canUndo()) return {};
    // 2026-10-14 (richiesta dell'utente): lo storico e' unico per tutti i fogli, ma un passo si
    // applica solo nel suo foglio. FEAT-027: un passo puo' toccare piu' fogli; vale se il foglio
    // attivo e' uno di quelli.
    const QStringList sheets = redo ? m_history.redoSheets() : m_history.undoSheets();
    // FEAT-079: a step left with only the resources (its sheet deleted, MapHistory::removeSheet) is
    // the document's and applies on any sheet.
    const bool resourcesOnly = sheets == QStringList{kResourcesSheet};
    if (!resourcesOnly && !sheets.contains(currentSheetId())) return {StepOutcome::OtherSheet, sheets.value(0), {}};
    if (redo) m_history.redo();
    else m_history.undo();
    syncOtherHistorySheets(sheets);
    const MapHistory::Snapshot &snapshot = m_history.current(currentSheetId());
    // P3 (2026-10-13): prima si applicano solo i nodi cambiati in questo passo (mappa e outliner non
    // si ricostruiscono, i rami chiusi restano chiusi); se il risultato non coincide con lo storico
    // si ricarica tutto. Le modifiche fatte mentre si applica non sono nuovi passi.
    m_restoringHistory = true;
    emit aboutToRestore();
    const bool incremental = m_model->applyFlatChanges(snapshot, m_history.lastChangedKeys(currentSheetId()));
    emit incrementalRestoreApplied();
    QString error;
    StepResult result{StepOutcome::Applied, {}, {}};
    if (incremental || m_model->fromFlatSnapshot(snapshot, &error)) {
        // 2026-10-14: il foglio va riscritto dal salvataggio automatico e il suo contenuto segue lo storico.
        m_dirtySheets.insert(currentSheetId());
        m_sheets[m_currentSheet].content = m_history.current(currentSheetId());
        ++m_changeSerial;
        updateDirtyState();
        emit restored(incremental);
    } else {
        result = {StepOutcome::Failed, {}, error};
    }
    m_restoringHistory = false;
    emitHistoryIfChanged();
    m_autoSaveTimer->start();
    return result;
}

// --- Nodi (tappa 2) ---

MindNode *Document::addChild(MindNode *parent, const QString &title) {
    MindNode *target = parent ? parent : m_model->root();
    MindNode *created = title.isEmpty() ? m_model->addChild(target) : m_model->addChild(target, title);
    if (!created) return nullptr;  // FEAT-052: not under a summary topic
    markChanged();
    return created;
}

MindNode *Document::addSibling(MindNode *node) {
    MindNode *created = m_model->addSibling(node ? node : m_model->root());
    if (!created) return nullptr;  // FEAT-052: not beside a summary topic
    markChanged();
    return created;
}

MindNode *Document::addSiblingBefore(MindNode *node) {
    MindNode *created = m_model->addSiblingBefore(node);
    if (created) markChanged();
    return created;
}

MindNode *Document::insertParent(MindNode *node) {
    MindNode *created = m_model->insertParent(node);
    if (created) markChanged();
    return created;
}

bool Document::moveAmongSiblings(MindNode *node, int direction, const std::function<int(const MindNode *)> &sideOf) {
    if (!node || !node->parent || node->floating || node->summaryTopic || direction == 0) return false;
    MindNode *parent = node->parent;
    const bool mainTopic = parent == m_model->root();
    const int count = static_cast<int>(parent->children.size());
    int from = 0;
    while (from < count && parent->children[from].get() != node) ++from;
    const int step = direction > 0 ? 1 : -1;
    for (int to = from + step; to >= 0 && to < count; to += step) {
        MindNode *other = parent->children[to].get();
        if (other->floating || other->summaryTopic || (mainTopic && sideOf(other) != sideOf(node))) continue;
        // A swap: the siblings between them (on the other side) keep their rows.
        m_model->moveNode(node, parent, step > 0 ? to + 1 : to);
        m_model->moveNode(other, parent, step > 0 ? from : from + 1);
        markChanged();
        return true;
    }
    return false;
}

MindNode *Document::addFloatingTopic(const QPointF &position, const QString &title) {
    MindNode *created = m_model->addChild(m_model->root(), title);
    created->floating = true;
    created->position = position;
    putOnTop(created);
    markChanged();
    emit nodesChanged({created});
    return created;
}

namespace {

// FEAT-052: the selected topics grouped by parent and side (sideOf: +1 right, -1 left), in the order
// of their first selected topic; each group is its run of siblings, first to last. Left out: the
// central topic, summary topics and topics selected with an ancestor (BUG-024).
QList<QList<MindNode *>> siblingRuns(const QList<MindNode *> &nodes, const std::function<int(const MindNode *)> &sideOf) {
    struct Group { MindNode *parent; int side; int first; int last; };
    QList<Group> groups;
    const auto ancestorSelected = [&nodes](const MindNode *node) {
        for (const MindNode *up = node->parent; up; up = up->parent)
            if (nodes.contains(const_cast<MindNode *>(up)) && up->parent) return true;
        return false;
    };
    for (MindNode *node : nodes) {
        if (!node || !node->parent || node->summaryTopic || ancestorSelected(node)) continue;
        const int side = sideOf ? sideOf(node) : 1;
        const auto &siblings = node->parent->children;
        const int row = int(std::find_if(siblings.cbegin(), siblings.cend(), [node](const auto &c) { return c.get() == node; }) - siblings.cbegin());
        auto it = std::find_if(groups.begin(), groups.end(), [&](const Group &g) { return g.parent == node->parent && g.side == side; });
        if (it == groups.end()) groups.append({node->parent, side, row, row});
        else { it->first = std::min(it->first, row); it->last = std::max(it->last, row); }
    }
    QList<QList<MindNode *>> runs;
    for (const Group &group : std::as_const(groups)) {
        QList<MindNode *> run;
        // On the central topic only the children on the group's side.
        for (int row = group.first; row <= group.last; ++row) {
            MindNode *child = group.parent->children[row].get();
            if (child->floating || (sideOf && sideOf(child) != group.side)) continue;
            run.append(child);
        }
        if (!run.isEmpty()) runs.append(run);
    }
    return runs;
}

QStringList idsOf(const QList<MindNode *> &nodes) {
    QStringList ids;
    for (const MindNode *node : nodes) ids.append(node->id);
    return ids;
}

}  // namespace

bool Document::removeNodes(const QList<MindNode *> &nodes) {
    // FEAT-052: a summary topic is removed with its summary (bracket and topic), after the topics:
    // the scene frees the summary topic, so `nodes` is not read once it is gone.
    QStringList summaries;
    for (MindNode *node : nodes)
        if (const Summary *summary = node ? m_model->summaryOfTopic(node) : nullptr) summaries.append(summary->id);
    // B10 (2026-10-13): si scartano i nodi che stanno dentro un ramo gia' da eliminare: eliminando il
    // ramo verrebbero distrutti anche loro e il puntatore successivo non sarebbe piu' valido.
    // Il nodo centrale non si elimina, quindi non conta come ramo da eliminare: prima, selezionandolo
    // insieme ad altri nodi (per esempio con Seleziona tutto), Canc non eliminava niente (2026-10-18).
    QList<MindNode *> toRemove;
    for (MindNode *node : nodes) {
        bool insideSelected = false;
        for (MindNode *ancestor = node ? node->parent : nullptr; ancestor && !insideSelected; ancestor = ancestor->parent)
            insideSelected = ancestor != m_model->root() && nodes.contains(ancestor);
        if (node && !node->summaryTopic && !insideSelected && !toRemove.contains(node)) toRemove.append(node);
    }
    bool removed = false;
    for (MindNode *node : std::as_const(toRemove)) removed = m_model->removeNode(node) || removed;
    if (removed) markChanged();
    return removeSummaries(summaries) || removed;
}

bool Document::indentNode(MindNode *node) {
    if (!node || !m_model->indentNode(node)) return false;
    markChanged();
    return true;
}

bool Document::outdentNode(MindNode *node) {
    if (!node || !m_model->outdentNode(node)) return false;
    markChanged();
    return true;
}

bool Document::moveNode(MindNode *node, MindNode *newParent, int row, int side) {
    if (!node || !newParent) return false;
    MindNode *root = m_model->root();
    const MindNode::Side oldSide = node->side;
    const bool moved = m_model->moveNode(node, newParent, row);
    if (!moved && (node->parent != newParent || side == 0)) return false;  // ciclo o stesso posto
    if (newParent != root) node->side = MindNode::Side::Auto;
    else if (side != 0) node->side = side > 0 ? MindNode::Side::Right : MindNode::Side::Left;
    if (!moved && node->side == oldSide) return false;
    markChanged();
    return true;
}

bool Document::makeFloating(MindNode *node, const QPointF &position) {
    MindNode *root = m_model->root();
    if (!node || node == root || summaryTopicOf(node)) return false;  // FEAT-052: nor out of a summary's branch
    m_model->moveNode(node, root, int(root->children.size()));  // nessun effetto se e' gia' l'ultimo
    node->floating = true;
    node->position = position;
    putOnTop(node);
    node->manualOffset = {};
    node->side = MindNode::Side::Auto;
    markChanged();
    return true;
}

bool Document::setCollapsed(MindNode *node, bool collapsed) {
    if (!node || !node->parent || node->summaryTopic || node->collapsed == collapsed) return false;
    node->collapsed = collapsed;
    markChanged();
    return true;
}

void Document::putOnTop(MindNode *node) {
    int top = -1;
    for (const auto &child : m_model->root()->children)
        if (child.get() != node) top = std::max(top, child->stackOrder);
    node->stackOrder = top + 1;
}

bool Document::canArrange(const MindNode *node) const {
    const MindNode *root = m_model->root();
    if (!node || node->parent != root || node->summaryTopic) return false;
    if (!node->floating && !m_model->sheetStyle.freeBranches) return false;
    return root->children.size() >= 2;
}

bool Document::arrange(MindNode *node, Arrange arrange) {
    if (!canArrange(node)) return false;
    QList<MindNode *> order = m_model->stackingOrder();
    const qsizetype from = order.indexOf(node);
    qsizetype to = from;
    switch (arrange) {
    case Arrange::ToFront: to = order.size() - 1; break;
    case Arrange::Forward: to = std::min(from + 1, order.size() - 1); break;
    case Arrange::Backward: to = std::max<qsizetype>(from - 1, 0); break;
    case Arrange::ToBack: to = 0; break;
    }
    if (to == from) return false;
    order.move(from, to);
    // Ranks rewritten as 0..n-1: the saved order no longer depends on the child order.
    for (qsizetype rank = 0; rank < order.size(); ++rank) order[rank]->stackOrder = int(rank);
    markChanged();
    return true;
}

bool Document::resetMapStyle() {
    if (!m_model->resetMapStyle()) return false;
    markChanged();  // the step may already be closed by a listener of styleEdited: nothing to add then
    emit annotationsChanged();
    return true;
}

bool Document::setCollapsedBelow(MindNode *node, bool collapsed) {
    if (!node) return false;
    bool changed = false;
    const std::function<void(MindNode *, bool)> visit = [&](MindNode *at, bool self) {
        const bool hasChildren = std::any_of(at->children.cbegin(), at->children.cend(), [](const auto &child) { return !child->floating; });
        // Collapsing keeps the node itself open (its children stay visible, closed).
        if (hasChildren && at->parent && (collapsed ? !self : true) && at->collapsed != collapsed) {
            at->collapsed = collapsed;
            changed = true;
        }
        for (const auto &child : at->children) if (!child->floating) visit(child.get(), false);
    };
    visit(node, true);
    if (changed) markChanged();
    return changed;
}

bool Document::moveBranchToSheet(MindNode *node, int targetIndex) {
    if (!node || !node->parent || node->summaryTopic || targetIndex == m_currentSheet || targetIndex >= m_sheets.size()) return false;
    if (targetIndex >= 0 && !ensureSheetLoaded(targetIndex).ok) return false;
    flushPendingStep();
    QSet<QString> ids;
    const std::function<void(const MindNode *)> collect = [&](const MindNode *at) {
        ids.insert(at->id);
        for (const auto &child : at->children) collect(child.get());
    };
    collect(node);
    QList<Relationship> inside;  // relations within the branch go with it
    for (const auto &relation : std::as_const(m_model->relations))
        if (ids.contains(relation.from) && ids.contains(relation.to)) inside.append(relation);
    QJsonObject branch = m_model->subtreeToJson(node);
    const QString title = node->title.trimmed();
    const MapStructure::Type type = MapStructure::typeOf(node);
    const MapStructure::Direction direction = MapStructure::directionOf(node);

    MindMapModel target;
    QString targetId;
    if (targetIndex < 0) {
        // New sheet: blank with the theme of the active one, then the branch as central topic
        // with the map type it had (decision 2026-10-16).
        target.copyThemeFrom(*m_model);
        targetId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString name = title.isEmpty() ? tr("Sheet %1").arg(m_sheets.size() + 1) : title;
        m_sheets.append({targetId, name, target.toFlatSnapshot(), true});
        m_history.setSheet(targetId, m_sheets.last().content);
        m_sheetListChanged = true;
        branch.insert(QStringLiteral("structure"), MapStructure::typeKey(type));
        branch.insert(QStringLiteral("structureDirection"), MapStructure::directionKey(direction));
        target.replaceRoot(branch);
        targetIndex = int(m_sheets.size()) - 1;
    } else {
        targetId = m_sheets[targetIndex].id;
        if (!m_history.hasSheet(targetId)) m_history.setSheet(targetId, m_sheets[targetIndex].content);
        if (!target.fromFlatSnapshot(m_history.current(targetId))) return false;
        // Floating topic above the map, near its top branches (decision 2026-10-17).
        qreal top = 0;
        const std::function<void(const MindNode *)> highest = [&](const MindNode *at) {
            top = std::min(top, at->position.y());
            for (const auto &child : at->children) if (!child->floating) highest(child.get());
        };
        highest(target.root());
        MindNode *created = target.insertSubtree(target.root(), int(target.root()->children.size()), branch, false);
        if (!created) return false;
        created->floating = true;
        created->collapsed = false;
        created->side = MindNode::Side::Auto;
        created->manualOffset = {};
        created->position = {0, top - kMovedBranchGapPx};
    }
    for (auto it = m_model->markerImages.cbegin(); it != m_model->markerImages.cend(); ++it) target.addMarkerImage(it.key(), it.value());
    target.relations.append(inside);
    m_model->removeNode(node);  // also drops the relations, summaries and boundaries pointing into it
    m_dirtySheets.insert(targetId);
    commitLinkStep(LinkSheetChange{targetId, target.toFlatSnapshot()});
    switchSheet(targetIndex);
    emit sheetsChanged();
    return true;
}

bool Document::setStructure(MindNode *node, const QString &typeKey, const QString &directionKey) {
    if (!node || node->summaryTopic) return false;  // FEAT-052: no subtopics to lay out yet
    const QString direction = typeKey.isEmpty() ? QString() : directionKey;
    if (node->structure == typeKey && node->structureDirection == direction) return false;
    node->structure = typeKey;
    node->structureDirection = direction;
    std::function<void(MindNode *)> clear = [&clear](MindNode *at) {
        for (const auto &child : at->children) { child->manualOffset = {}; clear(child.get()); }
    };
    clear(node);
    markChanged();
    return true;
}

MindNode *Document::duplicateNode(MindNode *node) {
    if (!node || !node->parent || node->summaryTopic) return nullptr;  // FEAT-052: a summary topic has no row
    // The row among the children (a topic in a summary's branch has no model index).
    const auto &siblings = node->parent->children;
    const auto at = std::find_if(siblings.cbegin(), siblings.cend(), [node](const auto &child) { return child.get() == node; });
    const int row = int(at - siblings.cbegin()) + 1;
    return insertBranches(node->parent, row, {m_model->subtreeToJson(node)});
}

MindNode *Document::insertBranches(MindNode *parent, int row, const QList<QJsonObject> &branches) {
    MindNode *target = parent ? parent : m_model->root();
    MindNode *first = nullptr;
    for (const QJsonObject &branch : branches) {
        MindNode *created = m_model->insertSubtree(target, row++, branch);
        if (!first) first = created;
    }
    if (first) markChanged();
    return first;
}

bool Document::setTitle(MindNode *node, const QString &title) {
    if (!node || node->title == title) return false;
    node->title = title;
    node->titleHtml.clear();  // il testo semplice sostituisce il titolo formattato
    markChanged();
    emit nodesChanged({node});
    return true;
}

bool Document::setFormattedTitle(MindNode *node, const QString &title, const QString &html) {
    if (!node || (node->title == title && node->titleHtml == html)) return false;
    node->title = title;
    node->titleHtml = RichResources::withoutExternal(html);  // BUG-044
    markChanged();
    emit nodesChanged({node});
    return true;
}

bool Document::setNote(MindNode *node, const QString &plain, const QString &html) {
    if (!node) return false;
    const bool empty = plain.trimmed().isEmpty();
    const QString nextPlain = empty ? QString() : plain;
    const QString nextHtml = empty ? QString() : RichResources::withoutExternal(html);  // BUG-044
    if (node->notes == nextPlain && node->notesHtml == nextHtml) return false;
    node->notes = nextPlain;
    node->notesHtml = nextHtml;
    markChanged();  // il passo resta aperto: chi chiude la nota ridispone e poi lo chiude
    emit nodesChanged({node});
    return true;
}

bool Document::setNotes(MindNode *node, const QString &notes) {
    if (!node || (node->notes == notes && node->notesHtml.isEmpty())) return false;
    node->notes = notes;
    node->notesHtml.clear();  // testo semplice: niente formati (FEAT-059)
    markChanged();
    emit nodesChanged({node});
    return true;
}

bool Document::removeVisual(MindNode *node) {
    if (!node || (node->imagePath.isEmpty() && node->formula.isEmpty())) return false;
    node->imagePath.clear();
    node->imageAssetId.clear();
    node->imageOriginalName.clear();
    node->imageSticker = false;
    node->formula.clear();
    node->imageStyle.rotation = 0;
    markChanged();
    emit nodesChanged({node});
    return true;
}

bool Document::setSticker(MindNode *node, const QString &path, const QSizeF &size) {
    // BUG-029: the sticker the topic already has is not inserted again (no step, no new layout).
    if (node && node->imageSticker && node->imagePath == path) return false;
    if (!setImage(node, path, size)) return false;
    node->imageSticker = true;
    node->imageStyle.outline = true;  // contorno acceso per i nuovi adesivi (si spegne dalla pagina Stile)
    return true;
}

bool Document::setMarker(MindNode *node, const QString &series, const QString &marker) {
    if (!node) return false;
    const QStringList next = Markers::with(node->markers, series, marker);
    if (next == node->markers) return false;
    node->markers = next;
    markChanged();
    emit nodesChanged({node});
    return true;
}

bool Document::setImage(MindNode *node, const QString &path, const QSizeF &size) {
    if (!node || path.isEmpty()) return false;
    node->imagePath = path;
    node->imageSticker = false;  // setSticker lo rimette
    node->imageStyle.rotation = 0;  // un'immagine nuova parte diritta
    node->formula.clear();  // FEAT-064: una cosa sola nel posto dell'immagine
    node->imageAssetId.clear();  // il nuovo file si incorpora al prossimo salvataggio
    node->imageOriginalName = QFileInfo(path).fileName();
    node->imageSize = size;
    markChanged();
    emit nodesChanged({node});
    return true;
}

bool Document::setImageSize(MindNode *node, const QSizeF &size) {
    if (!node || node->imageSize == size) return false;
    node->imageSize = size;
    markChanged();
    emit nodesChanged({node});
    return true;
}

bool Document::setImagePlacement(MindNode *node, MindNode::ImagePlacement placement) {
    if (!node || node->imagePlacement == placement) return false;
    node->imagePlacement = placement;
    markChanged();
    emit nodesChanged({node});
    return true;
}

bool Document::setTask(const QList<MindNode *> &nodes, MindNode::Task state) {
    QList<MindNode *> changed;
    for (MindNode *node : nodes)
        if (node && node->task != state) { node->task = state; changed.append(node); }
    if (changed.isEmpty()) return false;
    markChanged();  // il passo resta aperto: chi aggiunge o toglie la casella ridispone e poi lo chiude
    emit nodesChanged(changed);
    return true;
}

bool Document::toggleTask(MindNode *node) {
    if (!node || node->task == MindNode::Task::None) return false;
    const bool changed = setTask({node}, node->task == MindNode::Task::Done ? MindNode::Task::Open : MindNode::Task::Done);
    if (changed) flushPendingStep();  // una spunta = un passo, anche con clic ravvicinati
    return changed;
}

bool Document::setLabels(MindNode *node, const QStringList &labels) {
    if (!node) return false;
    const QStringList next = Labels::normalized(labels, Labels::autoSort());
    if (next == node->labels) return false;
    node->labels = next;
    markChanged();
    emit nodesChanged({node});
    return true;
}

namespace {
// Id dell'asset come lo calcola BmapStorage (SHA-256 del contenuto, esadecimale): il file resta
// riconoscibile anche se l'originale sparisce prima del salvataggio (copia temporanea, file spostato).
QString contentAssetId(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}
}  // namespace

bool Document::addFiles(MindNode *node, const QStringList &paths) {
    if (!node || paths.isEmpty()) return false;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        node->files.append({info.absoluteFilePath(), contentAssetId(path), info.fileName(), info.size()});
    }
    markChanged();
    emit nodesChanged({node});
    return true;
}

bool Document::setFormula(MindNode *node, const QString &code, const QColor &background, const QSizeF &size) {
    const QString next = code.trimmed();
    if (!node || (next == node->formula && background == node->formulaBackground && (size.isEmpty() || size == node->imageSize)))
        return false;
    const bool fresh = node->formula.isEmpty();
    node->formula = next;
    node->formulaBackground = background;
    if (!next.isEmpty()) {
        // Una formula nuova va sotto il testo; poi si trascina dove si vuole.
        if (fresh) {
            node->imagePlacement = MindNode::ImagePlacement::Below;
            node->imageStyle.rotation = 0;
        }
        node->imagePath.clear();  // una cosa sola nel posto dell'immagine
        node->imageAssetId.clear();
        node->imageOriginalName.clear();
        // La misura chiesta e' quella diritta: con la formula ruotata di 90 o 270 si scambia.
        if (!size.isEmpty()) node->imageSize = node->imageStyle.rotation % 180 ? size.transposed() : size;
    }
    markChanged();
    emit nodesChanged({node});
    return true;
}

bool Document::replaceFile(MindNode *node, int index, const QString &path) {
    if (!node || index < 0 || index >= node->files.size()) return false;
    NodeFile &file = node->files[index];
    const QFileInfo info(path);
    file.path = info.absoluteFilePath();
    file.assetId = contentAssetId(path);  // il contenuto nuovo si incorpora al prossimo salvataggio
    file.size = info.size();
    markChanged();
    emit nodesChanged({node});
    return true;
}

bool Document::renameFile(MindNode *node, int index, const QString &name) {
    if (!node || index < 0 || index >= node->files.size()) return false;
    QString clean = name.simplified();
    if (clean.isEmpty() || clean.contains(QLatin1Char('/')) || clean.contains(QLatin1Char('\\')) || clean.size() > 255) return false;
    const QString suffix = QFileInfo(node->files[index].name).suffix();
    if (QFileInfo(clean).suffix().isEmpty() && !suffix.isEmpty()) clean += QLatin1Char('.') + suffix;
    if (clean == node->files[index].name) return false;
    node->files[index].name = clean;
    markChanged();
    emit nodesChanged({node});
    return true;
}

bool Document::removeFile(MindNode *node, int index) {
    if (!node || index < 0 || index >= node->files.size()) return false;
    node->files.removeAt(index);
    markChanged();
    emit nodesChanged({node});
    return true;
}

QString Document::fileSource(const MindNode *node, int index) {
    if (!node || index < 0 || index >= node->files.size()) return {};
    const NodeFile &file = node->files[index];
    if (QFileInfo(file.path).isFile()) return file.path;
    if (file.assetId.isEmpty()) return {};
    waitForAutoSave();  // l'asset si legge dal file del documento
    const QString original = BmapStorage::originalAssetPath(assetSourcePath(), file.assetId, file.name);
    return QFileInfo(original).isFile() ? original : QString();
}

QStringList Document::documentLabels() const {
    QStringList all;
    std::function<void(const MindNode *)> visit = [&](const MindNode *node) {
        all += node->labels;
        for (const auto &child : node->children) visit(child.get());
    };
    visit(m_model->root());
    for (int i = 0; i < m_sheets.size(); ++i) {
        if (i == m_currentSheet) continue;
        for (const QJsonObject &fields : m_sheets[i].content)
            for (const QJsonValue &label : fields.value(QStringLiteral("labels")).toArray()) all.append(label.toString());
    }
    return Labels::normalized(all, true);
}

bool Document::setBalanced(bool on, const QHash<const MindNode *, int> &sides) {
    MindNode *root = m_model->root();
    if (!root || m_model->balanced == on) return false;
    m_model->balanced = on;
    if (on) {
        std::function<void(MindNode *)> clear = [&clear](MindNode *node) {
            node->manualOffset = {};
            for (const auto &child : node->children) clear(child.get());
        };
        clear(root);
        for (const auto &child : root->children) child->side = MindNode::Side::Auto;
    } else {
        for (const auto &child : root->children)
            child->side = sides.value(child.get(), 1) < 0 ? MindNode::Side::Left : MindNode::Side::Right;
    }
    markChanged();
    return true;
}

bool Document::rotateImage(MindNode *node) {
    if (!node || (node->imagePath.isEmpty() && node->formula.isEmpty())) return false;
    node->imageStyle.rotation = (node->imageStyle.rotation + 90) % 360;
    node->imageSize.transpose();
    markChanged();
    emit nodesChanged({node});
    return true;
}

bool Document::setImageStyle(MindNode *node, const ImageStyle &style) {
    if (!node || (node->imagePath.isEmpty() && node->formula.isEmpty()) || node->imageStyle == style) return false;
    node->imageStyle = style;
    markChanged();
    flushPendingStep();  // una scelta = un passo, anche se la successiva arriva subito
    emit nodesChanged({node});
    return true;
}

bool Document::setPitchTopic(MindNode *node, PitchField field, const QString &value) {
    if (!node) return false;
    QString &stored = field == PitchField::TopicSlide ? node->pitchTopicSlide
                    : field == PitchField::ListSlide  ? node->pitchListSlide
                    : field == PitchField::Delivery   ? node->pitchDelivery
                                                      : node->pitchLayout;
    if (stored == value) return false;
    stored = value;
    markChanged();
    flushPendingStep();  // one choice = one step
    return true;
}

bool Document::setPitchSheet(const QString &theme, const QString &aspect, const std::optional<QJsonObject> &pitchTemplate) {
    const QJsonObject copy = pitchTemplate.value_or(m_model->pitchTemplate);
    if (m_model->pitchTheme == theme && m_model->pitchAspect == aspect && m_model->pitchTemplate == copy) return false;
    m_model->pitchTheme = theme;
    m_model->pitchAspect = aspect;
    m_model->pitchTemplate = copy;
    markChanged();
    flushPendingStep();
    return true;
}

bool Document::editGantt(const std::function<bool(GanttPlan &)> &edit) {
    const GanttPlan before = m_model->gantt();
    GanttPlan plan = before;
    if (!edit(plan) || plan == before) return false;
    // Names, hierarchy and order are the map's: they change through the topic operations.
    if (plan.tasks().size() != before.tasks().size()) return false;
    for (int i = 0; i < plan.tasks().size(); ++i) {
        const GanttTask &a = before.tasks().at(i), &b = plan.tasks().at(i);
        // The colour is the topic's branch colour, changed as a style (MindMapModel::setStyle).
        // A milestone is the topic's checkbox (FEAT-048), added or removed on the map.
        if (a.id != b.id || a.name != b.name || a.parentId != b.parentId || a.color != b.color || a.milestone != b.milestone)
            return false;
    }
    QHash<QString, MindNode *> topics;  // one walk instead of a search per task
    std::function<void(MindNode *)> index = [&](MindNode *node) {
        topics.insert(node->id, node);
        for (const auto &child : node->children) index(child.get());
    };
    index(m_model->root());
    QList<MindNode *> checked;  // milestones whose checkbox changes: redrawn on the map
    for (const GanttTask &task : plan.tasks())
        if (MindNode *node = topics.value(task.id)) {
            node->ganttStart = task.start;
            node->ganttDuration = task.duration;
            node->ganttShape = task.shape;  // FEAT-080
            node->ganttFill = task.fill;
            node->ganttBorder = task.border;
            if (!task.milestone) {
                node->ganttProgress = task.progress;
                continue;
            }
            // A milestone's progress is its checkbox: 100% checks it, anything less empties it.
            const MindNode::Task state = task.progress == GanttPlan::kMaxProgress ? MindNode::Task::Done : MindNode::Task::Open;
            if (node->task != state) {
                node->task = state;
                checked.append(node);
            }
        }
    m_model->setGanttLinks(plan);
    emit m_model->ganttChanged();  // the topics' data changed even when the links did not
    if (!checked.isEmpty()) emit nodesChanged(checked);
    markChanged();
    flushPendingStep();
    return true;
}

bool Document::setPeoplePercent(const QList<MindNode *> &nodes, const QString &person, int percent) {
    percent = std::clamp(percent, MindNode::kMinPercent, MindNode::kMaxPercent);
    bool changed = false;
    for (MindNode *node : nodes) {
        if (!node || !node->ganttPeople.contains(person) || node->percentOf(person) == percent) continue;
        if (percent == MindNode::kMaxPercent) node->ganttPercent.remove(person);
        else node->ganttPercent.insert(person, percent);
        changed = true;
    }
    if (!changed) return false;
    emit m_model->ganttChanged();
    markChanged();
    flushPendingStep();
    return true;
}

MapHistory::Snapshot Document::resourcesSnapshot(const GanttResources &resources) {
    return {{kResourcesSheet, resources.toJson()}};
}

void Document::setLoadedResources(const GanttResources &resources) {
    m_resources = resources;
    emit resourcesChanged();
}

std::optional<QJsonObject> Document::resourcesWrite(bool fullWrite) const {
    if (!fullWrite && !m_dirtySheets.contains(kResourcesSheet)) return std::nullopt;
    return m_resources.toJson();
}

bool Document::setResources(const GanttResources &resources, const GanttCalendar &calendar) {
    if (m_restoringHistory || m_currentSheet < 0 || m_currentSheet >= m_sheets.size()) return false;
    flushPendingStep();
    const GanttResources clean = resources.normalized();
    GanttPlan plan = m_model->gantt();
    const bool calendarChanged = !(calendar == plan.calendar());
    if (calendarChanged && !plan.setCalendar(calendar)) return false;
    const bool listChanged = !(clean == m_resources);
    if (!calendarChanged && !listChanged) return false;
    // A history built before the pseudo-sheet existed would drop it as a new base (MapHistory::commit).
    if (!m_history.hasSheet(kResourcesSheet)) m_history.setSheet(kResourcesSheet, resourcesSnapshot(m_resources));
    if (calendarChanged) m_model->setGanttLinks(plan);
    m_resources = clean;
    markChanged();  // change serial, autosave and the modified state, as every edit
    commitWithOtherSheets({{kResourcesSheet, resourcesSnapshot(m_resources)}});
    if (listChanged) emit resourcesChanged();
    return true;
}

bool Document::setTaskInfo(const QList<MindNode *> &nodes, std::optional<int> priority, const QStringList &assign,
                           const QStringList &unassign, std::optional<int> percent) {
    if (priority) priority = std::clamp(*priority, 0, MindNode::kMaxPriority);
    if (percent) percent = std::clamp(*percent, MindNode::kMinPercent, MindNode::kMaxPercent);
    QSet<QString> known;
    for (const GanttResource &person : m_resources.people) known.insert(person.id);
    bool changed = false;
    for (MindNode *node : nodes) {
        if (!node) continue;
        if (priority && node->ganttPriority != *priority) { node->ganttPriority = *priority; changed = true; }
        QStringList people = node->ganttPeople;
        for (const QString &id : unassign) people.removeAll(id);
        for (const QString &id : assign)
            if (known.contains(id) && !people.contains(id)) people.append(id);
        if (people != node->ganttPeople) {
            // Ids of removed resources stay until the topic's people change (Undo of a removal brings them back).
            people.removeIf([&](const QString &id) { return !known.contains(id); });
            node->ganttPeople = people;
            node->ganttPercent.removeIf([&people](const auto &entry) { return !people.contains(entry.key()); });  // FEAT-083
            changed = true;
        }
        if (!percent) continue;
        for (const QString &id : assign) {
            if (!node->ganttPeople.contains(id) || node->percentOf(id) == *percent) continue;
            if (*percent == MindNode::kMaxPercent) node->ganttPercent.remove(id);
            else node->ganttPercent.insert(id, *percent);
            changed = true;
        }
    }
    if (!changed) return false;
    emit m_model->ganttChanged();
    markChanged();
    flushPendingStep();
    return true;
}

bool Document::setBarLabels(const QList<std::pair<MindNode *, std::optional<bool>>> &labels) {
    bool changed = false;
    for (const auto &[node, shown] : labels)
        if (node && node->ganttLabel != shown) {
            node->ganttLabel = shown;
            changed = true;
        }
    if (!changed) return false;
    emit m_model->ganttChanged();
    markChanged();
    flushPendingStep();
    return true;
}

// --- Relazioni, sommari e confini (tappa 3) ---

bool Document::isRelationEnd(const QString &id) const { return m_model->nodeById(id) != nullptr || boundary(id) != nullptr; }

QString Document::addRelation(const QString &fromId, const QString &toId) {
    if (fromId == toId || !isRelationEnd(fromId) || !isRelationEnd(toId)) return QString();
    Relationship relationship;
    relationship.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    relationship.from = fromId;
    relationship.to = toId;
    m_model->relations.append(relationship);
    markChanged();
    flushPendingStep();
    emit annotationsChanged();
    return relationship.id;
}

bool Document::updateRelations(const QList<Relationship> &relationships) {
    QList<Relationship> updated = m_model->relations;
    bool changed = false;
    for (const Relationship &relationship : relationships) {
        if (relationship.from == relationship.to || !isRelationEnd(relationship.from) || !isRelationEnd(relationship.to)) return false;
        const auto it = std::find_if(updated.begin(), updated.end(), [&](const Relationship &r) { return r.id == relationship.id; });
        if (it == updated.end()) return false;
        if (*it == relationship) continue;
        *it = relationship;
        changed = true;
    }
    if (!changed) return false;
    m_model->relations = updated;
    markChanged();
    flushPendingStep();
    emit annotationsChanged();
    return true;
}

bool Document::removeRelations(const QStringList &ids) {
    if (!m_model->relations.removeIf([&](const Relationship &r) { return ids.contains(r.id); })) return false;
    markChanged();
    flushPendingStep();
    emit annotationsChanged();
    return true;
}

const Relationship *Document::relation(const QString &id) const {
    for (const Relationship &relationship : std::as_const(m_model->relations))
        if (relationship.id == id) return &relationship;
    return nullptr;
}

QList<MindNode *> Document::addSummaries(const QList<MindNode *> &nodes, const std::function<int(const MindNode *)> &sideOf) {
    QList<MindNode *> created;
    for (const QList<MindNode *> &run : siblingRuns(nodes, sideOf)) {
        MindNode *parent = run.front()->parent;
        if (MapStructure::typeOf(parent) == MapStructure::Type::TreeTable || MapStructure::insideTreeTable(parent)) continue;
        if (summaryTopicOf(parent)) continue;  // FEAT-052: not in a summary topic's branch (no rows to cover)
        const QStringList topics = idsOf(run);
        if (std::any_of(m_model->summaries.cbegin(), m_model->summaries.cend(), [&](const Summary &existing) { return existing.topics == topics; }))
            continue;  // the same run already has a summary
        Summary summary;
        summary.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        summary.topics = topics;
        summary.topic = std::make_shared<MindNode>();
        summary.topic->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        summary.topic->title = tr("Summary");
        summary.topic->summaryTopic = true;
        summary.topic->parent = parent;
        m_model->summaries.append(summary);
        created.append(summary.topic.get());
    }
    if (created.isEmpty()) return created;
    m_model->refreshStyles();
    // No flush here: the caller lays the map out for the summary's room, in the same step.
    markChanged();
    emit annotationsChanged();
    return created;
}

bool Document::removeSummaries(const QStringList &ids) {
    QStringList topics;
    for (const Summary &summary : std::as_const(m_model->summaries)) if (ids.contains(summary.id)) topics.append(summary.topic->id);
    if (topics.isEmpty()) return false;
    m_model->summaries.removeIf([&](const Summary &summary) { return ids.contains(summary.id); });
    m_model->relations.removeIf([&](const Relationship &r) { return topics.contains(r.from) || topics.contains(r.to); });
    markChanged();
    emit annotationsChanged();
    return true;
}

QStringList Document::addBoundaries(const QList<MindNode *> &nodes, const std::function<int(const MindNode *)> &sideOf) {
    QStringList created;
    for (const QList<MindNode *> &run : siblingRuns(nodes, sideOf)) {
        if (MapStructure::insideTreeTable(run.front())) continue;  // FEAT-028: it would cover the neighbouring cells
        Boundary boundary;
        boundary.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        boundary.topics = idsOf(run);
        // BUG-024: the same run already has a boundary: a second identical one is not added.
        if (std::any_of(m_model->boundaries.cbegin(), m_model->boundaries.cend(),
                        [&](const Boundary &existing) { return existing.topics == boundary.topics; }))
            continue;
        m_model->boundaries.append(boundary);
        created.append(boundary.id);
    }
    if (created.isEmpty()) return created;
    // No flush here: the caller lays the map out for the boundary's room, in the same step.
    markChanged();
    emit annotationsChanged();
    return created;
}

bool Document::updateBoundaries(const QList<Boundary> &boundaries) {
    QList<Boundary> updated = m_model->boundaries;
    bool changed = false;
    for (const Boundary &boundary : boundaries) {
        const auto it = std::find_if(updated.begin(), updated.end(), [&](const Boundary &b) { return b.id == boundary.id; });
        if (it == updated.end() || boundary.topics.isEmpty()) return false;
        if (*it == boundary) continue;
        *it = boundary;
        changed = true;
    }
    if (!changed) return false;
    m_model->boundaries = updated;
    markChanged();
    emit annotationsChanged();
    return true;
}

bool Document::removeBoundaries(const QStringList &ids) {
    if (!m_model->boundaries.removeIf([&](const Boundary &b) { return ids.contains(b.id); })) return false;
    // Relationships ending on a removed boundary go with it, in the same step.
    m_model->relations.removeIf([&](const Relationship &r) { return ids.contains(r.from) || ids.contains(r.to); });
    markChanged();
    emit annotationsChanged();
    return true;
}

const Boundary *Document::boundary(const QString &id) const {
    for (const Boundary &boundary : std::as_const(m_model->boundaries))
        if (boundary.id == id) return &boundary;
    return nullptr;
}

// --- Import ed export (tappa 4) ---

Document::PasteResult Document::pasteMarkdown(MindNode *parent, int row, const QString &text) {
    const MarkdownImport::Result parsed = MarkdownImport::parse(text, MarkdownImport::kMaxPastedItems);
    if (parsed.tooLarge) return {nullptr, true};
    QList<QJsonObject> branches;
    for (const MarkdownImport::Item &item : parsed.items) branches.append(MarkdownImport::toSubtreeJson(item));
    return {insertBranches(parent, row, branches), false};
}

Document::PasteResult Document::replaceMapWithMarkdown(const QString &text) {
    const MarkdownImport::Result parsed = MarkdownImport::parse(text, MarkdownImport::kMaxPastedItems);
    if (parsed.tooLarge) return {nullptr, true};
    if (parsed.itemCount < 2 || !m_model->fromMarkdown(text)) return {};
    markChanged();  // il reset del modello e' un passo di storico: Ctrl+Z riporta la mappa di prima
    return {m_model->root(), false};
}

QString Document::exportMarkdownPath(const QString &chosenPath) {
    const QFileInfo chosen(chosenPath);
    const QString name = chosen.suffix().compare(QStringLiteral("md"), Qt::CaseInsensitive) == 0
        ? chosen.completeBaseName() : chosen.fileName();
    return chosen.dir().filePath(name + QLatin1Char('/') + name + QStringLiteral(".md"));
}

Document::ExportResult Document::exportMarkdown(const QString &markdownPath) {
    waitForAutoSave();  // gli originali si leggono dal file del documento
    const QDir folder = QFileInfo(markdownPath).dir();
    if (!QDir().mkpath(folder.path())) return {false, tr("Cannot create the folder %1.").arg(QDir::toNativeSeparators(folder.path())), {}};
    const QString assetsPath = folder.filePath(QStringLiteral("assets"));
    QHash<QString, QString> copied;  // file sorgente -> percorso relativo nel .md
    QSet<QString> usedNames;         // nomi gia' usati in assets/, senza distinzione di maiuscole
    ExportResult result;
    // Originale di un allegato: estratto dal documento se incorporato, altrimenti il file scelto.
    const auto sourceFor = [this](const QString &assetId, const QString &name, const QString &path) {
        if (!assetId.isEmpty()) {
            const QString original = BmapStorage::originalAssetPath(assetSourcePath(), assetId, name);
            if (QFileInfo(original).isFile()) return original;
        }
        return QFileInfo(path).isFile() ? path : QString();
    };
    // Copia in assets/ (una volta per file) e restituisce il percorso relativo; vuoto se fallisce.
    // attachment: FEAT-049 files are numbered attach01, attach02... over the whole export (BUG-043:
    // their names come from the document); images keep their name (FEAT-054).
    int attachments = 0;
    const auto copyAsset = [&](const QString &source, const QString &originalName, bool attachment = false) -> QString {
        const QString key = QFileInfo(source).canonicalFilePath();
        if (const auto it = copied.constFind(key); it != copied.cend()) return it.value();
        if (!QDir().mkpath(assetsPath)) {
            result = {false, tr("Cannot create the folder %1.").arg(QDir::toNativeSeparators(assetsPath)), {}};
            return {};
        }
        const QFileInfo info(attachment ? FileValidation::diskFileName(originalName, ++attachments)
                             : originalName.isEmpty() ? QFileInfo(source).fileName() : originalName);
        QString name = info.fileName();
        // Due file diversi con lo stesso nome: "foto-2.png", "foto-3.png"...
        for (int n = 2; usedNames.contains(name.toLower()); ++n)
            name = info.completeBaseName() + QStringLiteral("-%1").arg(n) + (info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix());
        usedNames.insert(name.toLower());
        const QString target = assetsPath + QLatin1Char('/') + name;
        QFile::remove(target);  // cartella gia' esportata: si sostituisce il file con lo stesso nome
        if (!QFile::copy(source, target)) {
            result = {false, tr("Cannot copy %1.").arg(QDir::toNativeSeparators(source)), {}};
            return {};
        }
        const QString relative = QStringLiteral("assets/") + name;
        copied.insert(key, relative);
        return relative;
    };
    MindMapModel::MarkdownExtras extras;
    extras.webLinks = true;
    extras.extraLines = [&](const MindNode *node) {
        QStringList lines;
        if (!result.ok) return lines;
        bool missing = false;
        if (!node->imagePath.isEmpty() || !node->imageAssetId.isEmpty()) {
            const QString source = sourceFor(node->imageAssetId, node->imageOriginalName, node->imagePath);
            if (source.isEmpty()) missing = true;
            else if (const QString relative = copyAsset(source, node->imageOriginalName); !relative.isEmpty())
                lines << QStringLiteral("![%1](%2)").arg(QString(node->title).replace(QLatin1Char('\n'), QLatin1Char(' ')), relative);
        }
        for (int i = 0; i < node->files.size(); ++i) {  // FEAT-049: nella cartella assets/ accanto al .md
            const QString source = fileSource(node, i);
            if (source.isEmpty()) missing = true;
            else if (const QString relative = copyAsset(source, node->files[i].name, true); !relative.isEmpty())
                lines << QStringLiteral("[%1](%2)").arg(node->files[i].name, relative);
        }
        if (missing) result.skipped.append(node->title);
        return lines;
    };
    const QString text = m_model->toMarkdown(false, extras);
    if (!result.ok) return result;
    QSaveFile file(markdownPath);
    if (!file.open(QIODevice::WriteOnly)) return {false, file.errorString(), {}};
    file.write(text.toUtf8());
    if (!file.commit()) return {false, file.errorString(), {}};
    return result;
}

Document::ExportResult Document::exportOffice(const QString &path, OfficeFormat format, const QString &title) {
    waitForAutoSave();  // the images' originals are read from the document's file
    // The active sheet from its model, the others from a model built from their content.
    std::vector<std::unique_ptr<MindMapModel>> others;
    QList<OfficeExport::Sheet> sheets;
    for (int i = 0; i < m_sheets.size(); ++i) {
        if (i == m_currentSheet) {
            sheets.append({m_sheets[i].name, m_model});
            continue;
        }
        const Result loaded = ensureSheetLoaded(i);
        auto model = std::make_unique<MindMapModel>();
        QString error;
        if (!loaded.ok || !model->fromFlatSnapshot(sheetContent(i), &error))
            return {false, tr("The sheet \"%1\" cannot be read.").arg(m_sheets[i].name), {}};
        sheets.append({m_sheets[i].name, model.get()});
        others.push_back(std::move(model));
    }
    ExportResult result;
    if (format == OfficeFormat::Word) {
        const QString source = assetSourcePath();
        const auto original = [&source](const MindNode *node) {
            return source.isEmpty() || node->imageAssetId.isEmpty() ? QString()
                                                                    : BmapStorage::originalAssetPath(source, node->imageAssetId, node->imageOriginalName);
        };
        result.error = OfficeExport::writeDocx(path, sheets, original, title, &result.skipped);
    } else {
        result.error = OfficeExport::writeXlsx(path, sheets, title);
    }
    result.ok = result.error.isEmpty();
    return result;
}

namespace {
// The outermost of the nodes, in map order (topics first, then summary topics, which are not children).
QList<const MindNode *> outermostBranches(const MindNode *root, const QList<MindNode *> &nodes) {
    QSet<const MindNode *> wanted;
    for (const MindNode *node : nodes)
        if (node) wanted.insert(node);
    QList<const MindNode *> branches;
    std::function<void(const MindNode *)> visit = [&](const MindNode *node) {
        if (wanted.contains(node)) { branches.append(node); return; }  // its descendants come with it
        for (const auto &child : node->children) visit(child.get());
    };
    visit(root);
    for (const MindNode *node : nodes) {
        if (!node || branches.contains(node)) continue;
        bool inside = false;
        for (const MindNode *ancestor = node->parent; ancestor && !inside; ancestor = ancestor->parent) inside = wanted.contains(ancestor);
        if (!inside) branches.append(node);
    }
    return branches;
}
}  // namespace

QByteArray Document::branchesData(const QList<MindNode *> &nodes) const {
    QJsonArray branches;
    for (const MindNode *node : outermostBranches(m_model->root(), nodes)) branches.append(m_model->subtreeToJson(node));
    return QJsonDocument(branches).toJson(QJsonDocument::Compact);
}

QString Document::branchesText(const QList<MindNode *> &nodes) const {
    QStringList lines;
    std::function<void(const MindNode *, int)> append = [&](const MindNode *current, int depth) {
        lines << QString(depth, QLatin1Char('\t')) + current->title;
        for (const auto &child : current->children) append(child.get(), depth + 1);
    };
    for (const MindNode *node : outermostBranches(m_model->root(), nodes)) append(node, 0);
    return lines.join(QLatin1Char('\n'));
}

QList<QJsonObject> Document::branchesFromData(const QByteArray &data) {
    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (document.isObject()) return {document.object()};
    QList<QJsonObject> branches;
    for (const QJsonValue &value : document.array())
        if (value.isObject()) branches.append(value.toObject());
    return branches;
}
