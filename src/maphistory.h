#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QSqlDatabase>
#include <vector>

// Storico unico di annulla/ripristina con indice del passo attivo.
//
// - Ogni passo contiene solo le differenze rispetto al passo precedente: per ogni
//   nodo cambiato i soli campi modificati (valore prima/dopo), oppure il nodo intero
//   se è stato aggiunto o rimosso. Prima si conservavano fino a 100 copie complete
//   della mappa in JSON.
// - m_index indica quanti passi sono applicati: Ctrl+Z lo decrementa, Ctrl+Y lo
//   incrementa. Una nuova modifica dopo un annulla cancella i passi successivi
//   (stesso comportamento di QUndoStack::push).
// - L'unica copia completa tenuta in memoria è lo stato corrente (m_current).
class MapHistory {
public:
    using Snapshot = QHash<QString, QJsonObject>;

    explicit MapHistory(int cacheSteps = 100);
    ~MapHistory();
    MapHistory(const MapHistory &) = delete;
    MapHistory &operator=(const MapHistory &) = delete;

    // 2026-10-14: storico unico per tutti i fogli del documento (richiesta dell'utente). Ogni passo
    // appartiene a un foglio; l'ordine dei passi è uno solo, quindi Ctrl+Z annulla sempre l'ultima
    // modifica fatta, in qualunque foglio. Chi chiama controlla con undoSheet()/redoSheet() che il
    // passo sia del foglio attivo prima di applicarlo. Lo stato "salvato" vale per tutto il documento.

    // Nuovo documento o file appena aperto: storico vuoto, nessun foglio, stato considerato salvato.
    void clear();
    // Stato iniziale di un foglio (aperto o creato), senza aggiungere passi.
    void setSheet(const QString &sheet, Snapshot current);
    bool hasSheet(const QString &sheet) const { return m_sheets.contains(sheet); }
    // FEAT-004: a deleted sheet leaves the history. Its changes are dropped from every step and a step
    // left empty disappears, so Undo/Redo go on with the other sheets as if it had never existed.
    void removeSheet(const QString &sheet);
    // Registra un passo del foglio con le differenze tra il suo stato corrente e `next`.
    // Restituisce false se non è cambiato nulla (nessun passo aggiunto).
    bool commit(const QString &sheet, Snapshot next);
    // FEAT-027 (2026-10-16): un solo passo con le differenze di piu' fogli (es. collegamento inverso
    // in un altro foglio): Ctrl+Z li annulla insieme. Il primo foglio e' quello del passo
    // (undoSheet/redoSheet). Un foglio mai registrato diventa la sua base senza differenze, come in
    // commit(sheet, next): chi chiama registra prima i fogli con setSheet().
    bool commit(QList<std::pair<QString, Snapshot>> sheets);
    // Foglio a cui appartiene il passo che undo()/redo() applicherebbe (vuoto se non c'è).
    QString undoSheet();
    QString redoSheet();
    // FEAT-027: tutti i fogli toccati da quel passo (il primo e' undoSheet/redoSheet).
    QStringList undoSheets();
    QStringList redoSheets();

    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index < m_stepCount; }
    // Applicano il passo all'indietro/in avanti al suo foglio e restituiscono lo stato risultante
    // di quel foglio.
    const Snapshot &undo();
    const Snapshot &redo();

    const Snapshot &current(const QString &sheet) const;
    void markSaved() { m_savedIndex = m_index; }
    bool isAtSavedState() const { return m_index == m_savedIndex; }
    int index() const { return m_index; }
    int stepCount() const { return m_stepCount; }
    // P3 (2026-10-13): chiavi (id dei nodi o "@map") toccate dall'ultimo undo()/redo(), per
    // aggiornare il modello solo in quei punti: del foglio del passo, oppure (FEAT-027) di un foglio
    // dato fra quelli del passo.
    QStringList lastChangedKeys() const { return m_lastChangedKeys.value(m_lastSheet); }
    QStringList lastChangedKeys(const QString &sheet) const { return m_lastChangedKeys.value(sheet); }

    // Interfaccia a foglio unico (prima del 2026-10-14), usata dai test: foglio con id vuoto.
    void reset(Snapshot current) { clear(); setSheet(QString(), std::move(current)); }
    bool commit(Snapshot next) { return commit(QString(), std::move(next)); }
    const Snapshot &current() const { return current(QString()); }

private:
    enum class Kind { Added, Removed, Modified };
    struct Change {
        Kind kind;
        QString key;
        QJsonObject before; // Removed: nodo intero; Modified: solo i campi cambiati
        QJsonObject after;  // Added: nodo intero; Modified: solo i campi cambiati
        QString sheet;      // FEAT-027: foglio della modifica (quello del passo, o un altro)
    };
    struct Step {
        QString sheet;
        std::vector<Change> changes;
        QStringList sheets() const;  // sheet per primo, poi gli altri fogli toccati
    };
    // Differenze di un foglio fra `state` e `next`, aggiunte a `changes`.
    static void diffSheet(const QString &sheet, const Snapshot &state, const Snapshot &next, std::vector<Change> &changes);
    // Step as stored in the session journal.
    static QByteArray encodeStep(const Step &step);
    static Step decodeStep(const QByteArray &payload);

    void apply(const Step &step, bool forward);
    void initializeSessionStore();
    void flushWindow();
    const Step &stepAt(int sequence);
    void loadWindow(int sequence);

    std::vector<Step> m_steps;
    int m_cacheStart = 0;
    int m_stepCount = 0;
    int m_index = 0;
    int m_savedIndex = 0; // -1 se lo stato salvato non è più raggiungibile
    int m_cacheSteps;
    QHash<QString, Snapshot> m_sheets;
    QHash<QString, QStringList> m_lastChangedKeys;  // per foglio, dell'ultimo undo()/redo()
    QString m_lastSheet;
    QString m_connectionName;
    QString m_sessionPath;
    QSqlDatabase m_database;
};
