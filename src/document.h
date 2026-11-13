#pragma once

// Documento aperto: fogli, storico unico, stato modificato, file, blocco del file, copia di lavoro
// delle mappe senza nome e salvataggio automatico (FEAT-066 tappa 1,
// docs/features/FEAT-066-nucleo-dei-comandi.md). Nessun widget: l'interfaccia chiede le
// operazioni, mostra domande ed errori e ascolta i segnali. Vive sul thread dell'interfaccia; il
// salvataggio automatico scrive su un thread di QtConcurrent copie immutabili dei fogli.

#include "bmapstorage.h"
#include "ganttresources.h"
#include "maphistory.h"
#include "nodestyle.h"  // ImageStyle (FEAT-040)
#include "mindmapmodel.h"  // MindNode::Task (FEAT-048)

#include <QByteArray>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QPair>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

class MindMapModel;
struct MindNode;
class QLockFile;
class QTimer;

class Document final : public QObject {
    Q_OBJECT
public:
    static constexpr qreal kMovedBranchGapPx = 120;  // FEAT-029: floating branch above the target map
    // Contenuto in forma piatta, la stessa dello storico e del salvataggio. loaded = false finche'
    // il foglio non viene aperto: i fogli si leggono dal file solo quando servono.
    struct Sheet {
        QString id;
        QString name;
        MapHistory::Snapshot content;
        bool loaded = true;
    };
    struct Result {
        bool ok = true;
        QString error;
        bool fileLocked = false;  // il file e' aperto da un'altra istanza
    };
    // Esito di Ctrl+Z / Ctrl+Y: lo storico e' unico, ma un passo si applica solo nel suo foglio.
    enum class StepOutcome { Nothing, Applied, OtherSheet, Failed };
    struct StepResult {
        StepOutcome outcome = StepOutcome::Nothing;
        QString sheetId;  // OtherSheet: foglio del passo
        QString error;    // Failed
    };
    using LinkSheetChange = std::pair<QString, MapHistory::Snapshot>;

    explicit Document(QObject *parent = nullptr);
    ~Document() override;

    // Modello del foglio attivo (uno solo, posseduto dal documento).
    MindMapModel *model() const { return m_model; }

    // Funzioni fornite dall'interfaccia. interactionInProgress: vero mentre un tasto del mouse e'
    // premuto (il passo di storico e il salvataggio automatico aspettano la fine del trascinamento).
    // previewJpeg: anteprima della Home per i salvataggi espliciti e di chiusura.
    void setInteractionInProgress(std::function<bool()> query) { m_interactionInProgress = std::move(query); }
    void setPreviewProvider(std::function<QByteArray()> provider) { m_previewJpeg = std::move(provider); }

    // --- Fogli ---
    const QVector<Sheet> &sheets() const { return m_sheets; }
    int currentSheet() const { return m_currentSheet; }
    QString currentSheetId() const;
    // Changes when another document is started or opened: sheet ids repeat across files (the
    // first sheet is often "main"), this with the id names a sheet of this session.
    int documentGeneration() const { return m_documentGeneration; }
    int sheetIndex(const QString &id) const;
    // Legge dal file un foglio non ancora aperto.
    Result ensureSheetLoaded(int index);
    // Stato attuale di un foglio: il modello se attivo, altrimenti lo storico o il contenuto letto.
    MapHistory::Snapshot sheetContent(int index) const;
    // Aggiunge "Sheet N" (tradotto) e lo rende attivo; e' una modifica del documento.
    // FEAT-026 fase D: prepare prepara il foglio vuoto (tipo, variante, rami) prima che entri nello storico.
    // layout runs once the sheet is shown, so its positions become the sheet's starting state.
    void addSheet(const std::function<void(MindMapModel &)> &prepare = {}, const std::function<void()> &layout = {});
    Result switchSheet(int index);
    // FEAT-004: deletes the sheet and its Undo steps (not undoable; the next save keeps a recovery
    // point with it). The last sheet cannot be deleted; deleting the active one shows the previous one.
    Result removeSheet(int index);
    // FEAT-004: the sheet's name (spaces around and line breaks dropped). Not a history step, like
    // adding and deleting; the next save writes it. False for an empty or unchanged name.
    bool renameSheet(int index, const QString &name);
    // FEAT-004: moves a sheet to position to; the active sheet stays the same. Not a history step.
    bool moveSheet(int from, int to);
    // FEAT-004: "<name> (copy)" right after the sheet, shown; same content with fresh ids (node ids
    // are unique in the file). Not a history step, like adding.
    Result duplicateSheet(int index);

    // --- File ---
    QString filePath() const { return m_filePath; }
    // File da cui leggere gli originali delle immagini: il documento o la sua copia di lavoro.
    QString assetSourcePath() const { return m_filePath.isEmpty() ? m_untitledRecoveryPath : m_filePath; }
    // Documento senza nome con un solo foglio, dal contenuto attuale del modello (nuova mappa o
    // modello appena preparato dall'interfaccia).
    void startUntitled();
    // .mentis (blocco del file, fogli letti alla prima apertura) o .md (documento senza nome).
    // In caso di errore il documento resta quello di prima. afterLoad(nativeMap) gira dopo la lettura
    // e prima che il modello diventi lo stato iniziale dello storico (es. disposizione di un .md).
    Result open(const QString &path, bool integrityCheck, const std::function<void(bool)> &afterLoad = {});
    Result save(const QString &path, BmapStorage::SaveReason reason);
    // Chiusura senza modifiche: solo punto di ripristino e anteprima (errore ignorabile).
    Result writeClosingPoint();
    // Anteprima della Home nel file, solo con nome e senza modifiche.
    void storePreview();
    // Chiusura della finestra: toglie la copia di lavoro di un documento senza nome e il blocco.
    void close();

    // --- Storico e stato modificato ---
    bool isDirty() const { return m_dirty; }
    bool isSaving() const { return m_saveInProgress; }
    bool isRestoring() const { return m_restoringHistory; }
    bool canUndo() const;
    bool canRedo() const { return m_history.canRedo(); }
    // Il modello e' cambiato: apre o prolunga il passo di storico (chiuso dopo 150 ms) e avvia il
    // salvataggio automatico. Ignorato mentre si applica lo storico.
    void markChanged();
    // Chiude subito il passo in corso, se c'e'.
    void flushPendingStep();
    // Passo che comprende anche lo stato di un altro foglio (collegamento inverso, FEAT-027).
    void commitLinkStep(std::optional<LinkSheetChange> other);
    // FEAT-061 Find and replace: new titles by sheet id and node id, in one step for every sheet
    // touched. layout runs after the active sheet changed and before the step is recorded.
    int replaceTitles(const QHash<QString, QHash<QString, QString>> &titles, const std::function<void()> &layout);
    // FEAT-060 / FEAT-061 Tags tab: label `from` becomes `to` on every topic of every sheet (an empty `to`
    // removes it), one step. A topic that also has `to` keeps a single one, spelled as `to` (Labels::same).
    // layout as in replaceTitles. Returns how many topics changed.
    int renameLabel(const QString &from, const QString &to, const std::function<void()> &layout);
    // FEAT-045 find and replace: edit changes a topic's texts (title, titleHtml, notes, notesHtml,
    // labels) and says whether it did, on every topic of every sheet; one step for all of them.
    // layout as in replaceTitles. Returns how many topics changed.
    int replaceText(const std::function<bool(MindNode &)> &edit, const std::function<void()> &layout);
    // Stato attuale di un foglio nello storico, aggiunto se non c'e' ancora (FEAT-027).
    MapHistory::Snapshot historySnapshot(const QString &sheetId);
    StepResult undo();
    StepResult redo();
    void setAutoSaveDelay(int ms);

    // --- Nodi (tappa 2) ---
    // Ogni operazione cambia il modello e apre il passo di storico (markChanged): il passo si
    // chiude dopo 150 ms insieme alle conseguenze decise dall'interfaccia (per esempio la
    // disposizione automatica), cosi' un Ctrl+Z toglie tutto. I puntatori restituiti valgono
    // finche' il nodo resta nel modello.
    MindNode *addChild(MindNode *parent, const QString &title = QString());  // parent nullo = centrale
    MindNode *addSibling(MindNode *node);  // nullo o centrale = nuovo figlio del centrale
    // FEAT-086: MindMapModel::addSiblingBefore and insertParent; they open the history step without
    // closing it (the caller lays out and flushes).
    MindNode *addSiblingBefore(MindNode *node);
    MindNode *insertParent(MindNode *node);
    // FEAT-086: the topic swaps with the previous (direction < 0) or next sibling shown on its side
    // (sideOf: +1 right, -1 left, from the layout; only main topics compare sides). Floating and
    // summary topics are skipped and not moved; false at the ends. Opens the step without closing it.
    bool moveAmongSiblings(MindNode *node, int direction, const std::function<int(const MindNode *)> &sideOf);
    MindNode *addFloatingTopic(const QPointF &position, const QString &title);
    // Il nodo centrale non si elimina; un nodo dentro un ramo gia' da eliminare sparisce con il ramo.
    // Vero se almeno un nodo e' stato eliminato.
    bool removeNodes(const QList<MindNode *> &nodes);
    // The topic to select after removeNodes(nodes), chosen before among those that stay (user's
    // request 2026-10-21): a sibling of the first removed topic (the next, else the previous), else
    // its parent; null when none qualifies. Shared by the desktop and the phone (FEAT-111).
    static MindNode *nodeAfterRemoval(const QList<MindNode *> &nodes);
    bool indentNode(MindNode *node);
    bool outdentNode(MindNode *node);
    // FEAT-015: sposta il nodo con il suo ramo sotto newParent alla riga row (come
    // MindMapModel::moveNode). side: lato scelto per un ramo principale (+1 destra, -1 sinistra),
    // 0 = resta com'e'; un nodo che non e' piu' principale perde il lato. Falso se non cambia nulla.
    // Apre il passo di storico senza chiuderlo: la ridisposizione entra nello stesso passo.
    bool moveNode(MindNode *node, MindNode *newParent, int row, int side);
    // FEAT-096 stage 3: topics dragged on the map, each with where it ends and its distance from the
    // automatic place (MindNode::manualOffset, kept by the layout; a floating topic keeps only the
    // position). The front end may have moved them already while dragging. Opens the history step
    // without closing it (it closes after the mouse is released). False for an empty list.
    struct TopicPlacement {
        MindNode *node = nullptr;
        QPointF position;
        QPointF manualOffset;
    };
    bool placeTopics(const QList<TopicPlacement> &placements);
    // Il nodo con il suo ramo diventa un argomento mobile con il centro in position (in fondo ai figli
    // del centrale). Apre il passo di storico senza chiuderlo. Falso per il centrale.
    bool makeFloating(MindNode *node, const QPointF &position);
    // FEAT-016: chiude o riapre nella mappa i discendenti del nodo (non del centrale). Apre il passo
    // di storico senza chiuderlo. Falso se non cambia nulla.
    bool setCollapsed(MindNode *node, bool collapsed);
    // FEAT-042: every branch below the node closed (collapsed) or the node and its branches opened.
    bool setCollapsedBelow(MindNode *node, bool collapsed);
    // FEAT-025: MindMapModel::resetMapStyle on the active sheet, relationships and boundaries redrawn.
    bool resetMapStyle();
    // FEAT-057: stacking order of the branches that can overlap (floating topics, main branches with
    // Free branch positions). canArrange: the node is one of them and has another to overlap.
    // arrange opens the history step without closing it; false if nothing changes.
    enum class Arrange { ToFront, Forward, Backward, ToBack };
    bool canArrange(const MindNode *node) const;
    bool arrange(MindNode *node, Arrange arrange);
    // FEAT-029: moves the branch to the sheet at targetIndex (-1 = a new sheet named after the branch,
    // with the branch as its central topic; otherwise a floating topic above that map). Ids are kept,
    // relations with the rest of the map are dropped, the target becomes the active sheet; one step
    // covers both sheets (one Ctrl+Z).
    bool moveBranchToSheet(MindNode *node, int targetIndex);
    // FEAT-026 fase C: tipo di mappa e direzione del foglio (node = centrale) o di un ramo (tipo vuoto =
    // come il genitore). Gli spostamenti a mano nel ramo si perdono (decisione 2026-10-15); lo stile
    // resta. Apre il passo di storico senza chiuderlo. Falso se non cambia nulla.
    bool setStructure(MindNode *node, const QString &typeKey, const QString &directionKey);
    MindNode *duplicateNode(MindNode *node);  // subito dopo l'originale, con id nuovi
    // Rami nel formato di MindMapModel::subtreeToJson inseriti in parent dalla riga row, con id
    // nuovi; restituisce il primo inserito.
    MindNode *insertBranches(MindNode *parent, int row, const QList<QJsonObject> &branches);
    bool setTitle(MindNode *node, const QString &title);  // testo semplice: toglie il titolo formattato
    // Titolo con il titolo formattato (HTML limitato, MindNode::titleHtml).
    bool setFormattedTitle(MindNode *node, const QString &title, const QString &html);
    // FEAT-096 stage 3: the title typed on the map or in an outliner row. As setFormattedTitle, and
    // the model tells its views as for any edit of the tree (dataChanged for a topic in the tree,
    // structureChanged); a topic outside the tree (central, summary) through nodesChanged. The step
    // stays open: the caller lays the map out when the size changed.
    bool editTitle(MindNode *node, const QString &title, const QString &html);
    bool setNotes(MindNode *node, const QString &notes);
    // FEAT-059: nota con il suo HTML (vuoto = testo semplice); testo vuoto = nota tolta. Apre il passo
    // di storico senza chiuderlo (l'icona cambia la misura del nodo: la ridisposizione entra nel passo).
    bool setNote(MindNode *node, const QString &plain, const QString &html);
    bool setImage(MindNode *node, const QString &path, const QSizeF &size);
    bool setImageSize(MindNode *node, const QSizeF &size);
    // FEAT-045: where the picture (image, sticker or formula) sits by the text, as dragging it there.
    bool setImagePlacement(MindNode *node, MindNode::ImagePlacement placement);
    // FEAT-040: aspetto dell'immagine (bordo, ombra, opacita'); ogni scelta e' un passo di storico a
    // se' (chiuso subito). Falso con un nodo senza immagine o se non cambia nulla.
    bool setImageStyle(MindNode *node, const ImageStyle &style);
    // FEAT-063: Pitch settings (stored keys, empty = Auto / default); each change one history step,
    // closed at once like the image style. False if nothing changes.
    enum class PitchField { TopicSlide, ListSlide, Delivery, Layout };
    bool setPitchTopic(MindNode *node, PitchField field, const QString &value);
    // FEAT-089: pitchTemplate = the sheet's template copy (empty object = none); nullopt = unchanged.
    bool setPitchSheet(const QString &theme, const QString &aspect, const std::optional<QJsonObject> &pitchTemplate = std::nullopt);
    // FEAT-075: one edit of the planning data of the active sheet's Gantt (dates, durations, %,
    // colours on the topics; dependencies and calendar on the sheet), one history step closed at
    // once. edit changes a copy of MindMapModel::gantt(); false from edit, no difference, or a change
    // of names, hierarchy or order (the map's, through the topic operations) records nothing.
    bool editGantt(const std::function<bool(GanttPlan &)> &edit);
    // FEAT-079: the document's resources (every sheet's) and the active sheet's calendar, set
    // together from the Resources dialog: one step of the active sheet, closed at once. The list is
    // normalized (GanttResources::normalized); false when nothing changes or the calendar is refused.
    const GanttResources &resources() const { return m_resources; }
    bool setResources(const GanttResources &resources, const GanttCalendar &calendar);
    // FEAT-078: priority (0-5, clamped; nullopt = unchanged) of the topics' tasks; FEAT-081: the
    // resources (ids) assigned to all of them and removed from all of them. One step; false when
    // nothing changes. A topic whose people change drops the ids no longer in the resources.
    // FEAT-083: the person's share (clamped to 1-100 %) of the topics' tasks that have that person; one
    // step, false when nothing changes.
    bool setPeoplePercent(const QList<MindNode *> &nodes, const QString &person, int percent);
    // FEAT-045: percent (clamped as setPeoplePercent) is the share of the assign people, new or already there.
    bool setTaskInfo(const QList<MindNode *> &nodes, std::optional<int> priority, const QStringList &assign = {},
                     const QStringList &unassign = {}, std::optional<int> percent = std::nullopt);
    // The name on the topics' bars: shown, hidden, or the default (nullopt), all in one step.
    bool setBarLabels(const QList<std::pair<MindNode *, std::optional<bool>>> &labels);
    // FEAT-013: bilanciamento del foglio. Spegnendolo ogni ramo principale tiene il lato che ha ora
    // (sides: +1 destra, -1 sinistra, dal layout), cosi' la disposizione resta la base degli
    // spostamenti a mano; accendendolo spostamenti a mano e lati scelti si tolgono. Chi chiama
    // ridispone e chiude il passo (un solo passo di storico).
    bool setBalanced(bool on, const QHash<const MindNode *, int> &sides);
    // Immagine, adesivo o formula ruotati di 90 gradi in senso orario; la misura si scambia. Un passo.
    bool rotateImage(MindNode *node);
    // FEAT-048: casella di spunta di piu' nodi (None = toglierla); falso se non cambia nulla. Apre il
    // passo di storico senza chiuderlo: la ridisposizione che segue entra nello stesso passo.
    bool setTask(const QList<MindNode *> &nodes, MindNode::Task state);
    // Spunta o toglie la spunta (solo su un nodo che ha la casella), un passo.
    bool toggleTask(MindNode *node);
    // FEAT-060: etichette del nodo (ripulite, ordinate se Labels::autoSort). Apre il passo senza
    // chiuderlo (le pillole cambiano la misura: la ridisposizione entra nel passo).
    bool setLabels(MindNode *node, const QStringList &labels);
    // FEAT-049: file allegati. addFiles li aggiunge in fondo (incorporati al prossimo salvataggio);
    // replaceFile mette il contenuto di path al posto del file index (modifica fuori dall'app);
    // removeFile lo toglie. Aprono il passo senza chiuderlo: la graffetta cambia la misura del nodo.
    bool addFiles(MindNode *node, const QStringList &paths);
    // FEAT-064: formula del nodo nel posto dell'immagine, con lo sfondo e la misura (vuota = tolta).
    // Toglie l'immagine; setImage toglie la formula. Apre il passo senza chiuderlo.
    bool setFormula(MindNode *node, const QString &code, const QColor &background = Qt::white, const QSizeF &size = {});
    // Toglie dal nodo l'immagine, l'adesivo o la formula (il contenuto nel posto dell'immagine).
    // Apre il passo senza chiuderlo (la misura cambia: chi chiama ridispone).
    bool removeVisual(MindNode *node);
    // FEAT-051: adesivo nel posto dell'immagine (toglie immagine o formula); misura size.
    bool setSticker(MindNode *node, const QString &path, const QSizeF &size);
    // FEAT-051: marcatore della serie (vuoto = tolto). Apre il passo senza chiuderlo.
    bool setMarker(MindNode *node, const QString &series, const QString &marker);
    bool replaceFile(MindNode *node, int index, const QString &path);
    bool removeFile(MindNode *node, int index);
    // FEAT-045: the shown name of the attached file index (the extension kept when name has none);
    // false for an empty name, one with a path separator, or no change.
    bool renameFile(MindNode *node, int index, const QString &name);
    // File da leggere per aprire, salvare o esportare il file index: l'originale estratto dal
    // documento (cache) o il file scelto ancora da incorporare. Vuoto se non si trova.
    QString fileSource(const MindNode *node, int index);
    // Etichette usate in tutti i fogli del documento (i fogli non ancora aperti non contano), una
    // per nome, in ordine alfabetico.
    QStringList documentLabels() const;

    // --- Relazioni, sommari e confini (tappa 3) ---
    // Falso se l'operazione non cambia nulla: sommario gia' presente, nessun nodo per il confine.
    // FEAT-053: relationships of the active sheet, each change one history step. The ends must be
    // elements of the sheet (topics, summary topics or boundaries) and different.
    // addRelation returns the new id, empty if refused.
    QString addRelation(const QString &fromId, const QString &toId);
    // FEAT-109: one relationship from `fromId` to each target, all in one step; the refused targets
    // (the start itself, a missing element) are skipped. Returns the new ids.
    QStringList addRelations(const QString &fromId, const QStringList &toIds);
    // Replaces the relationships with the same ids; false (nothing changed) if one is missing,
    // has invalid ends or nothing differs.
    bool updateRelations(const QList<Relationship> &relationships);
    bool removeRelations(const QStringList &ids);
    const Relationship *relation(const QString &id) const;
    bool isRelationEnd(const QString &id) const;  // FEAT-053: an element of the active sheet
    // FEAT-052: summaries, grouped like the boundaries below; every structure but the tree table.
    // The new summary topics ("Summary") are returned; the step stays open for the
    // caller's layout, like the boundaries. removeSummaries also drops the relationships ending on
    // their topics. Deleting a summary topic with removeNodes removes its summary.
    QList<MindNode *> addSummaries(const QList<MindNode *> &nodes, const std::function<int(const MindNode *)> &sideOf = {});
    bool removeSummaries(const QStringList &ids);
    // FEAT-052: boundaries. Each change is one history step once the caller has laid the map out
    // (their room changes the layout) and called flushPendingStep(). The topics are grouped by parent and
    // side (sideOf: +1 right, -1 left); each group gets one boundary over its run of siblings,
    // first to last. Left out: the central topic, topics selected with an ancestor, and a run that
    // already has a boundary. Returns the new ids (empty: nothing created).
    QStringList addBoundaries(const QList<MindNode *> &nodes, const std::function<int(const MindNode *)> &sideOf = {});
    // Replaces the boundaries with the same ids; false if one is missing or nothing differs.
    bool updateBoundaries(const QList<Boundary> &boundaries);
    bool removeBoundaries(const QStringList &ids);
    const Boundary *boundary(const QString &id) const;

    // --- Import ed export (tappa 4) ---
    // Esito di un testo incollato: primo nodo creato (o nuovo nodo centrale) e limite superato.
    struct PasteResult {
        MindNode *first = nullptr;
        bool tooLarge = false;  // oltre MarkdownImport::kMaxPastedItems voci: nulla cambia
    };
    // Testo Markdown (FEAT-055) come rami in parent dalla riga row, in un passo di storico.
    PasteResult pasteMarkdown(MindNode *parent, int row, const QString &text);
    // Markdown di almeno due voci al posto di tutta la mappa (FEAT-055): prima voce = nodo
    // centrale. Con una voce sola non cambia nulla e first resta nullo.
    PasteResult replaceMapWithMarkdown(const QString &text);
    // FEAT-054 (2026-10-18): esportazione in una cartella con il .md e assets/. Dal nome scelto
    // ("Documenti/Biologia.md") il percorso del .md dentro la sua cartella ("Documenti/Biologia/Biologia.md").
    static QString exportMarkdownPath(const QString &chosenPath);
    struct ExportResult {
        bool ok = true;
        QString error;
        QStringList skipped;  // voci con un allegato che non si trova piu' (saltato)
    };
    // Markdown della mappa attiva in markdownPath (titoli, livelli, note, collegamenti web) con
    // immagini e file allegati copiati in assets/ accanto al file, alla qualita' originale e con percorsi
    // relativi. Il .md si scrive per ultimo e in modo atomico: con un errore non viene scritto.
    ExportResult exportMarkdown(const QString &markdownPath);
    // FEAT-054 stage 4: every sheet as a Word (.docx) or Excel (.xlsx) file; title is the document's
    // title in the file's properties. skipped: topics whose image was not found.
    enum class OfficeFormat { Word, Excel };
    ExportResult exportOffice(const QString &path, OfficeFormat format, const QString &title);
    // Copied branches (FEAT-042, 2026-11-05): each topic once and in map order; a topic inside another
    // copied branch travels with it. Data: a JSON array of MindMapModel::subtreeToJson branches; text:
    // one title per line, indented with tabs.
    QByteArray branchesData(const QList<MindNode *> &nodes) const;
    QString branchesText(const QList<MindNode *> &nodes) const;
    // The branches in copied data; a single object (one branch) is read too.
    static QList<QJsonObject> branchesFromData(const QByteArray &data);
    // The clipboard format of branchesData, shared by the desktop and the phone.
    static QString branchesMimeType() { return QStringLiteral("application/x-mentis-subtree"); }

signals:
    // Contenuto di nodi cambiato senza segnali del modello (titolo, note, immagine).
    void nodesChanged(const QList<MindNode *> &nodes);
    // Relazioni, sommari o confini cambiati: la scena ridisegna le annotazioni.
    void annotationsChanged();
    void sheetsChanged();                      // elenco, nomi o foglio attivo
    void resourcesChanged();                   // FEAT-079: the list (edit, Undo/Redo, file opened)
    void dirtyChanged(bool dirty);
    void historyChanged();                     // canUndo / canRedo
    void saveStateChanged();                   // salvataggio in corso, finito o stato modificato
    void autoSaveFailed(const QString &error);
    // Applicazione di un passo di storico: prima, dopo le modifiche incrementali, alla fine.
    void aboutToRestore();
    void incrementalRestoreApplied();
    void restored(bool incremental);

private:
    // FEAT-057: a new floating topic starts on top of the stack (it is inserted before the older ones).
    // FEAT-079: the resources are a pseudo-sheet of the history (one key), so a step can carry them
    // with the active sheet; the id cannot be a sheet's (UUIDs or "main").
    static inline const QString kResourcesSheet = QStringLiteral("@resources");
    static MapHistory::Snapshot resourcesSnapshot(const GanttResources &resources);
    // The sheet list changed without a history step (rename, move): modified and autosaved.
    void sheetListModified();
    void setLoadedResources(const GanttResources &resources);
    std::optional<QJsonObject> resourcesWrite(bool fullWrite) const;
    void putOnTop(MindNode *node);
    void commitHistory();
    // One step with the active sheet and the other sheets given (replaceTitles, renameLabel).
    void commitWithOtherSheets(QList<LinkSheetChange> others);
    void updateDirtyState();
    void setSaved();
    void resetSheets(const QString &name);
    void resetHistory();
    void resetUntitledRecovery();
    void waitForAutoSave();
    void captureCurrentSheet();
    void syncOtherHistorySheets(const QStringList &sheets);
    StepResult applyStep(bool redo);
    QList<BmapStorage::SheetWrite> sheetWrites(bool fullWrite) const;
    Result saveSheets(const QString &path, BmapStorage::SaveReason reason);
    Result acquireLock(const QString &path, std::unique_ptr<QLockFile> &lock) const;
    void startAutoSave();
    void setDirtyFlag(bool dirty);
    void emitHistoryIfChanged();

    MindMapModel *m_model;
    QVector<Sheet> m_sheets;
    int m_currentSheet = 0;
    int m_documentGeneration = 0;
    MapHistory m_history;
    GanttResources m_resources;
    bool m_dirty = false;
    QSet<QString> m_dirtySheets;   // fogli cambiati e non ancora scritti dal salvataggio automatico
    bool m_sheetListChanged = false;  // fogli aggiunti dall'ultimo salvataggio: non stanno nello storico
    bool m_restoringHistory = false;
    QString m_filePath;
    QString m_untitledRecoveryPath;
    std::unique_ptr<QLockFile> m_fileLock;
    QTimer *m_historyTimer;
    QTimer *m_autoSaveTimer;
    QFutureWatcher<QPair<bool, QString>> *m_autoSaveWatcher;
    quint64 m_changeSerial = 0;
    quint64 m_autoSaveSerial = 0;
    bool m_saveInProgress = false;
    std::pair<bool, bool> m_lastHistoryState{false, false};
    std::function<bool()> m_interactionInProgress;
    std::function<QByteArray()> m_previewJpeg;
};
