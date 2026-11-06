#pragma once

#include <QAbstractItemModel>
#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QMimeData>
#include <QPointF>
#include <QSizeF>
#include <QStringList>
#include "nodestyle.h"
#include "ganttcosts.h"
#include "ganttplan.h"
#include "boundary.h"
#include "relationship.h"
#include "summary.h"

#include <functional>
#include <memory>
#include <vector>

// FEAT-018: larghezza massima che si puo' dare a un nodo a mano (trascinando il bordo o, in
// futuro, dal pannello). Decisione dell'utente 2026-10-15: 3000 px, oltre non si va.
inline constexpr qreal kMaxManualNodeWidthPx = 3000;
// Larghezza manuale letta da file o da storico: un valore negativo, nullo o non numerico vale
// "automatica" (0); un valore oltre il massimo viene portato al massimo.
qreal sanitizedManualNodeWidth(double widthPx);

// FEAT-060: regole delle etichette (uguali a meno di maiuscole e spazi ai lati).
namespace Labels {
// Tolti spazi ai lati, vuoti e doppioni (resta la prima scrittura); ordine alfabetico se sorted.
QStringList normalized(const QStringList &labels, bool sorted);
bool same(const QString &a, const QString &b);
// Ordinamento automatico (impostazione dell'app, acceso di base).
bool autoSort();
void setAutoSort(bool on);
}  // namespace Labels

// FEAT-049: file del computer allegato al nodo, sempre incorporato nel documento (tabella assets
// del .mentis). path = file da incorporare al prossimo salvataggio (scelto, trascinato o aggiornato
// dopo una modifica fuori dall'app); assetId = asset gia' nel documento (vuoto finche' non salvato).
struct NodeFile {
    QString path;
    QString assetId;
    QString name;    // nome originale, con l'estensione
    qint64 size = 0; // byte
    bool operator==(const NodeFile &) const = default;
};

struct MindNode {
    enum class ImagePlacement { Above, Below, Left, Right };
    // FEAT-048: casella di spunta nel posto a sinistra (docs/architecture/node-content-contract.md).
    enum class Task { None, Open, Done };
    QString id;
    QString title;
    // HTML limitato prodotto dall'editor inline per la formattazione di porzioni del titolo.
    // Vuoto significa testo semplice e mantiene compatibili i file creati in precedenza.
    QString titleHtml;
    QString notes;
    // FEAT-059: nota formattata (HTML limitato, come titleHtml); vuota = solo testo semplice. notes
    // resta il testo semplice (ricerca, esportazione, suggerimento).
    QString notesHtml;
    // FEAT-019: valori effettivi dello stile, calcolati dalla cascata (MindMapModel::refreshStyles,
    // NodeStyle::resolveTree). Non si salvano: nel file e nello storico va solo "style", cioe' i
    // valori scelti a mano. Chi disegna legge questi campi; chi modifica usa MindMapModel::setStyle.
    QColor fill{"#fff4cf"};
    QColor text{"#172033"};
    QColor border{"#d69727"};
    QColor branchColor{"#d69727"};  // colore della linea che entra nel nodo (BUG-005)
    QColor lineColor{"#d69727"};    // FEAT-043: colore disegnato della linea (Ramo colorato spento = Colore del foglio)
    // Spessore e stile delle linee che partono dal nodo verso i figli (BUG-006).
    qreal branchWidth = 2.0;
    Qt::PenStyle branchStyle = Qt::SolidLine;
    // FEAT-020: stile predefinito, tipo di riempimento e di bordo, spessore del bordo effettivi.
    NodePreset preset = NodePreset::Predefinito;
    NodeFillStyle fillStyle = NodeFillStyle::Pieno;
    NodeBorderStyle borderStyle = NodeBorderStyle::Continuo;
    qreal borderWidth = 2.0;
    bool strikeOut = false;  // FEAT-020: titolo barrato dallo stile Depenna
    NodeShape shape = NodeShape::RettangoloArrotondato;  // FEAT-020 parte B
    // FEAT-022: testo del nodo effettivo. Font vuoto = quello di sistema.
    QString fontFamily;
    qreal fontSize = NodeStyle::kDefaultFontSizePt;
    int fontWeight = NodeStyle::kDefaultFontWeight;
    bool italic = false;
    bool underline = false;
    TextCase textCase = TextCase::Nessuna;
    TextAlign textAlign = TextAlign::Sinistra;
    BranchShape branchShape = BranchShape::Auto;  // FEAT-072: lines towards its own children
    BranchEnd branchStart = BranchEnd::None;      // FEAT-023: marks on the lines towards the children
    BranchEnd branchEnd = BranchEnd::None;
    NodeStyleOverrides style;  // FEAT-019: valori scelti a mano (vuoti = ricavati)
    bool floating = false;
    // FEAT-024: number shown before the title (empty = none), computed by refreshStyles; not saved.
    QString number;
    // FEAT-057: stacking rank of a child of the central topic (higher = drawn above); equal ranks
    // keep the child order. Key "stack".
    int stackOrder = 0;
    // FEAT-052: the topic of a summary (see Summary), not one of its parent's children; not saved.
    bool summaryTopic = false;
    // FEAT-016: discendenti nascosti nella mappa (non nell'outliner), chiave "collapsed" = "yes". Il
    // centrale non si chiude (decisione dell'utente 2026-10-21).
    bool collapsed = false;
    QPointF position;
    // B7 (2026-10-13): spostamento fatto a mano rispetto al posto calcolato dal layout
    // automatico. Il layout lo riapplica, così aggiungere un nodo non annulla il trascinamento;
    // "Disponi automaticamente" lo azzera. (0,0) = nessuno spostamento manuale.
    QPointF manualOffset;
    // FEAT-012 (passo 4a): lato scelto di un ramo principale; Auto = lo decide il bilanciamento
    // (MapStructure::balancedSides). Vale solo per i figli del centrale; chiave "side".
    enum class Side { Auto, Left, Right };
    Side side = Side::Auto;
    // FEAT-026 fase B: tipo di mappa e direzione propri del nodo (MapStructure); vuoti = ereditati.
    // Sul nodo centrale sono quelli del foglio. Chiavi "structure" e "structureDirection".
    QString structure;
    QString structureDirection;
    // Larghezza del riquadro scelta trascinando i lati del nodo sulla mappa (richiesta 2026-10-13).
    // 0 = automatica (su misura del testo fino al massimo). Cambia solo dove il testo va a capo
    // nel nodo: il titolo resta lo stesso e l'outliner non ne è toccato.
    qreal customWidth = 0;
    QSizeF imageSize{180, 120};
    QString imagePath;
    // Per gli asset incorporati imagePath punta alla miniatura estratta nella cache;
    // l'originale resta nel database ed e' identificato senza dipendere dalla cache.
    QString imageAssetId;
    QString imageOriginalName;
    ImagePlacement imagePlacement = ImagePlacement::Above;
    ImageStyle imageStyle;  // FEAT-040
    Task task = Task::None;  // FEAT-048
    // FEAT-075: the topic's planning data in the Gantt view (key "gantt"; defaults are not written).
    QDate ganttStart;        // null = no date: the row has no bar
    int ganttDuration = 1;   // working days, at least 1; kept while the topic is a milestone (task)
    int ganttProgress = 0;  // the bar's colour is the branch colour (branchColor)
    int ganttPriority = 0;  // FEAT-078: 0 = none, 1-5 = very low ... very high (user 2026-10-28)
    QStringList ganttPeople;  // FEAT-081: ids of the resources assigned (FEAT-079), no duplicates
    // FEAT-083: each person's share of the task, kMinPercent-kMaxPercent; only those not at 100 % are
    // kept, and only for ids in ganttPeople.
    QHash<QString, int> ganttPercent;
    static constexpr int kMinPercent = 1;
    static constexpr int kMaxPercent = 100;
    static constexpr int kPercentStep = 5;
    int percentOf(const QString &person) const { return ganttPercent.value(person, kMaxPercent); }
    // FEAT-075: the name on the bar; empty = the default (summaries yes, tasks no; user 2026-10-27).
    std::optional<bool> ganttLabel;
    // FEAT-080: the bar's own shape, fill and border, GanttPlan::styleKey; empty = the sheet's
    // (GanttPlan checks the keys).
    QString ganttShape;
    QString ganttFill;
    QString ganttBorder;
    static constexpr int kMaxPriority = 5;
    QStringList labels;      // FEAT-060: etichette, gia' ripulite (Labels::normalized)
    QList<NodeFile> files;   // FEAT-049: file allegati, nell'ordine in cui sono stati aggiunti
    // FEAT-064: formula LaTeX del nodo (codice senza $). Sta nel posto dell'immagine (imageSize,
    // imagePlacement, imageStyle): un nodo ha l'immagine oppure la formula.
    QString formula;
    QColor formulaBackground = Qt::white;
    // FEAT-051: l'immagine del nodo e' un adesivo (sticker): stesso posto, misura e maniglie
    // dell'immagine, scelto dalla raccolta degli adesivi; senza cornice di base.
    bool imageSticker = false;
    QStringList markers;     // FEAT-051: "serie:valore", al massimo uno per serie (Markers::series)
    // FEAT-027: collegamento a un altro argomento dello stesso file (id del nodo di arrivo, anche in
    // un altro foglio) e a una pagina web (indirizzo gia' completo, NodeLinks::webAddress). Vuoti =
    // nessun collegamento; al massimo uno per tipo.
    QString topicLink;
    QString webLink;
    // FEAT-063: Pitch settings of the topic (Pitch::*Key); empty = Auto / default. Keys
    // "pitchTopicSlide", "pitchListSlide", "pitchDelivery", "pitchLayout".
    QString pitchTopicSlide;
    QString pitchListSlide;
    QString pitchDelivery;
    QString pitchLayout;
    MindNode *parent = nullptr;
    std::vector<std::unique_ptr<MindNode>> children;
};

// FEAT-052: the summary topic a topic belongs to (itself, or the one whose branch holds it); nullptr in
// the main tree. Such a branch is outside the outliner rows.
inline MindNode *summaryTopicOf(const MindNode *node) {
    for (const MindNode *at = node; at; at = at->parent)
        if (at->summaryTopic) return const_cast<MindNode *>(at);
    return nullptr;
}

class MindMapModel final : public QAbstractItemModel {
    Q_OBJECT
public:
    // Ruolo che restituisce MindNode::titleHtml (titolo formattato dalla mappa): l'outliner ne
    // mostra corsivo, barrato e sottolineato (richiesta 2026-10-13).
    enum Roles { TitleHtmlRole = Qt::UserRole + 1, NumberRole };  // NumberRole: FEAT-024

    explicit MindMapModel(QObject *parent = nullptr);
    ~MindMapModel() override;
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex & = {}) const override { return 1; }
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    // Testo e titolo formattato insieme (html vuoto = testo semplice). A differenza di setData
    // non svuota titleHtml: lo usa l'outliner, che conserva la formattazione fatta sulla mappa.
    // Con valori identici a quelli salvati non cambia nulla e non segna la mappa come modificata.
    bool setTitle(const QModelIndex &index, const QString &title, const QString &html);
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }

    MindNode *root() const { return m_root.get(); }
    QList<Relationship> relations;  // FEAT-053; changed through Document
    // FEAT-052; changed through Document. relinkSummaries keeps their topics' parents.
    QList<Summary> summaries;
    QList<MindNode *> summaryTopics() const;
    const Summary *summaryOfTopic(const MindNode *topic) const;
    // Drops the covered ids no longer in the tree and the summaries left empty; each topic's parent
    // becomes its first covered topic's parent.
    void relinkSummaries();
    // A topic that gets children is a summary of the Gantt: it loses its people and its links
    // (user, 2026-10-30). Part of the change that made it one, so Undo brings them back.
    void dropSummaryPlanning();
    QList<Boundary> boundaries;     // FEAT-052; changed through Document
    // Palette del foglio (FEAT-031): id (di base, NodeStyle::basePalettes, o di un modello o
    // personalizzata) e i suoi colori, salvati entrambi cosi' il foglio si apre uguale ovunque. I
    // rami principali prendono i colori in ordine. Per cambiarla usare setPalette(), che ricalcola i
    // colori ricavati ed e' un passo di storico.
    QString paletteId = NodeStyle::kDefaultPaletteId;
    QStringList activePalette = NodeStyle::findPalette(NodeStyle::kDefaultPaletteId)->colors;
    // FEAT-051 tappa 2: immagini dei marcatori propri usati nel foglio (sha256 -> contenuto del
    // file), salvate nel documento cosi' la mappa resta completa. Si aggiungono con addMarkerImage.
    QHash<QString, QByteArray> markerImages;
    void addMarkerImage(const QString &sha, const QByteArray &data);
    // FEAT-032: name = nome di una palette personalizzata, salvato nel foglio per mostrarlo anche dove
    // la palette non e' installata; vuoto per le palette di base.
    QString paletteName;
    // FEAT-036: variante del tema (0-5, -1 = nessuna) e sfondo libero sopra la variante; senza
    // nessuno dei due lo sfondo e' bianco. Salvati per foglio.
    int paletteVariant = -1;
    std::optional<QColor> background;
    // FEAT-043 parte stile: font, spessore delle linee, Ramo colorato e Colore del foglio.
    NodeStyle::SheetStyle sheetStyle;
    // FEAT-012: bilanciamento automatico del foglio (attivo nelle mappe nuove). Chiave "balanced",
    // sempre scritta; assente = file di prima: attivo se nessun nodo e' stato spostato a mano,
    // altrimenti spento con tutti i rami principali a destra (come allora).
    bool balanced = true;
    // FEAT-063: Pitch theme and aspect ratio of the sheet (Pitch::*Key); empty = default (dark, auto).
    // Keys "pitchTheme", "pitchAspect"; changed through Document.
    QString pitchTheme;
    QString pitchAspect;
    // FEAT-089: a copy of the sheet's Pitch template (Pitch::toJson; empty = none, pitchTheme
    // applies), so the sheet opens the same where the template is not installed. Key "pitchTemplate".
    QJsonObject pitchTemplate;
    // FEAT-075: the sheet's Gantt plan. Its tasks are the topics (all but the central one, in map
    // order, topics with children as summaries) with their planning data and their branch colour
    // (the colour of the bar; it is changed with setStyle, BranchColor); dependencies and calendar
    // are the sheet's (key "gantt" of the sheet state, written only when set). Built on each call.
    GanttPlan gantt() const;
    // FEAT-083: the people of every topic with their share, for GanttCosts.
    GanttAssignments ganttAssignments() const;
    // Dependencies and calendar of the plan; the tasks' data goes to the topics (Document::editGantt).
    void setGanttLinks(const GanttPlan &plan);
    // Un passo di storico (styleEdited); falso se non cambia nulla.
    bool setSheetStyle(const NodeStyle::SheetStyle &style);
    NodeStyle::Backdrop backdrop() const { return NodeStyle::backdropFor(paletteId, activePalette, paletteVariant, background); }
    // Cambia la palette e toglie variante e sfondo libero (decisione dell'utente 2026-10-18).
    void setPalette(const QString &id, const QStringList &colors, const QString &name = QString());
    // FEAT-036: una variante (-1 = nessuna); toglie lo sfondo libero. Un passo di storico
    // (styleEdited); falso se non cambia nulla o la variante non esiste.
    bool setBackdrop(int variant);
    // Sfondo libero sopra la variante attuale (nullopt = toglierlo): i rami non cambiano. Un passo.
    bool setBackground(const std::optional<QColor> &freeBackground);
    // Palette e variante di partenza (nuova mappa dal tema predefinito, nuovo foglio dal foglio
    // attivo, FEAT-036); non e' un passo di storico.
    void setTheme(const QString &id, const QStringList &colors, const QString &name, int variant,
                  const std::optional<QColor> &freeBackground = std::nullopt);
    void copyThemeFrom(const MindMapModel &other);
    // FEAT-019: imposta o toglie il valore scelto a mano di una proprieta' su piu' nodi, tutto o
    // niente. Falso (e nessun segnale) se l'elenco e' vuoto, il valore non e' valido o non cambia
    // nulla. Se cambia qualcosa emette styleEdited(): MainWindow lo registra come un solo passo.
    bool setStyle(const QList<MindNode *> &nodes, StyleProperty property, const QVariant &value);
    bool clearStyle(const QList<MindNode *> &nodes, StyleProperty property);
    // FEAT-027: collegamenti di piu' nodi in una volta (un passo di storico): per ogni nodo il nuovo
    // collegamento ad argomento e/o web (std::nullopt = invariato, stringa vuota = rimosso). Falso
    // (e nessun segnale) con un nodo nullo o se non cambia nulla; altrimenti linksEdited().
    struct LinkChange {
        MindNode *node = nullptr;
        std::optional<QString> topicLink;
        std::optional<QString> webLink;
    };
    bool setLinks(const QList<LinkChange> &changes);
    // FEAT-021: larghezze manuali di piu' nodi (0 = automatica; valori ripuliti come quelli letti
    // da file, sanitizedManualNodeWidth), tutto o niente. Il minimo di ogni nodo dipende dalla
    // misura del testo e lo applica chi chiama (MindMapScene::manualWidthFor). Falso (e nessun
    // segnale) con un nodo nullo o se non cambia nulla; altrimenti stylesChanged() per i nodi
    // cambiati (la scena li rimisura e ridispone la mappa) e styleEdited() (un passo di storico).
    bool setCustomWidths(const QList<std::pair<MindNode *, qreal>> &widths);
    // Ricalcola i valori effettivi di tutto l'albero ed emette stylesChanged() per quelli cambiati.
    // Chiamata da sola a ogni structureChanged() (struttura, ordine dei fratelli, caricamento).
    void refreshStyles();
    MindNode *nodeFromIndex(const QModelIndex &index) const;
    MindNode *nodeById(const QString &id) const;  // nullptr if no topic of the sheet has it (summary topics included)
    QModelIndex indexFromNode(MindNode *node) const;
    // Senza titolo = quello predefinito (2026-10-21, richiesta dell'utente, vedi defaultTitle); con un
    // titolo, anche vuoto, quello.
    MindNode *addChild(MindNode *parent);
    MindNode *addChild(MindNode *parent, const QString &title);
    MindNode *addSibling(MindNode *node);
    // FEAT-086: a new sibling at the node's row, above it. Null on the central, a floating or a
    // summary topic.
    MindNode *addSiblingBefore(MindNode *node);
    // FEAT-086: a new topic at the node's row (with its side and manual position), the node its only
    // child; summaries and boundaries that covered the node cover the new topic. Null on the
    // central, a floating or a summary topic.
    MindNode *insertParent(MindNode *node);
    // Nome di un nodo nuovo: "Argomento principale N" sotto il centrale,
    // "Sotto-argomento N" altrove (inglese "Main Topic N", "Subtopic N"); N = quanti figli ha il
    // genitore con il nodo nuovo.
    static QString defaultTitle(const MindNode *parent, int number);
    // Riga del primo argomento mobile fra i figli del centrale (= numero dei figli se non ce ne sono).
    int firstFloatingRow() const;
    // FEAT-057: children of the central topic from the bottom of the stack to the top (rank, then
    // child order).
    QList<MindNode *> stackingOrder() const;
    int siblingCountFor(const MindNode *parent) const;  // figli che contano per il nome dei nodi nuovi
    // pruneAnnotations (2026-10-14): toglie anche relazioni, sommari e confini che puntano al ramo
    // eliminato (prima restavano nel file per sempre). Lo storico li registra nel passo, quindi
    // Ctrl+Z li rimette; l'annulla incrementale (applyFlatChanges) passa false.
    bool removeNode(MindNode *node, bool pruneAnnotations = true);
    bool indentNode(MindNode *node);
    bool outdentNode(MindNode *node);
    bool moveNode(MindNode *node, MindNode *newParent, int row = -1);
    // Copia/incolla e duplica dell'outliner: il ramo completo (nodo + discendenti) in JSON,
    // nello stesso formato dei file .mentis. L'inserimento assegna id nuovi a tutta la copia.
    QJsonObject subtreeToJson(const MindNode *node) const;
    MindNode *insertSubtree(MindNode *parentNode, int row, const QJsonObject &json, bool freshIds = true);
    // FEAT-029: the central topic becomes this branch (ids kept, the rest of the sheet unchanged).
    void replaceRoot(const QJsonObject &json);
    // FEAT-042: manual style of the topics removed (Reset style), one step (styleEdited).
    bool resetStyle(const QList<MindNode *> &nodes);
    // FEAT-072 B15: the manual style of source given to targets (Apply to related), one step.
    bool copyStyle(const MindNode *source, const QList<MindNode *> &targets);
    // FEAT-085: Copy style / Paste style. The look as it is seen: every style property's effective
    // value, and whether the Gantt bar shows the name.
    struct TopicStyle {
        NodeStyleOverrides style;
        bool nameOnBar = false;
    };
    // With coloured branches off, a line that shows the sheet's neutral colour gives no branch colour:
    // a manual one would colour the target's lines.
    TopicStyle topicStyle(const MindNode *node) const;
    // The style becomes the manual style of every target; one step (styleEdited), false if nothing changes.
    bool pasteStyle(const TopicStyle &style, const QList<MindNode *> &targets);
    // Apply to children: the manual style of each source given to all its descendants (floating
    // ones apart); a selected descendant passes its own style on to its branch. One step.
    bool copyStyleToDescendants(const QList<MindNode *> &sources);
    // FEAT-025: Reset map style of the sheet: the manual style of every topic (summary topics
    // included), relationship and boundary, the free background and the look rules of the Map page
    // (font, line width, Tapered, Colored branch, line colour). Palette, variant, layout switches and
    // Show all notes stay. One step (styleEdited); false if there was nothing to reset.
    bool resetMapStyle();
    bool hasMapStyle() const;  // something resetMapStyle would change
    void clear(const QString &rootTitle = tr("Central Topic"));
    QString toIndentedText() const;
    // FEAT-054: righe aggiunte sotto una voce (immagine e file allegati esportati con percorsi relativi) e
    // titolo scritto come collegamento quando la voce ha un collegamento web.
    struct MarkdownExtras {
        std::function<QStringList(const MindNode *)> extraLines;
        bool webLinks = false;
    };
    // FEAT-054: solo testo (titoli, livelli, note); con includeStyleMetadata anche le righe tecniche
    // di colori e posizioni e le immagini, che l'importazione ignora.
    QString toMarkdown(bool includeStyleMetadata = false) const { return toMarkdown(includeStyleMetadata, MarkdownExtras()); }
    QString toMarkdown(bool includeStyleMetadata, const MarkdownExtras &extras) const;
    bool fromMarkdown(const QString &markdown, QString *error = nullptr);
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &object, QString *error = nullptr);
    // Istantanea "piatta" per annulla/ripristina: una voce per nodo (chiave = id,
    // con "parent" e "row" al posto dei figli annidati) più la voce "@map" per
    // relazioni, sommari, confini e palette. Permette a MapHistory di confrontare e
    // salvare solo i nodi cambiati invece dell'intera mappa. Ricostruisce solo i nodi cambiati
    // dall'istantanea precedente; gli altri condividono il JSON gia' fatto (cache per nodo).
    QHash<QString, QJsonObject> toFlatSnapshot() const;
    bool fromFlatSnapshot(const QHash<QString, QJsonObject> &snapshot, QString *error = nullptr);
    // P3 (2026-10-13): annulla/ripristina incrementale. Porta il modello allo stato "target"
    // toccando solo le voci in changedKeys (nodi o "@map"), con i segnali di riga di Qt
    // (inserimento, rimozione, spostamento, dati cambiati) invece del reset completo. Così
    // scena e outliner non vengono ricostruiti e i rami chiusi restano chiusi. Restituisce
    // false se lo stato ottenuto non coincide con target: il chiamante usa allora
    // fromFlatSnapshot(), che rimette sempre tutto a posto.
    bool applyFlatChanges(const QHash<QString, QJsonObject> &target, const QStringList &changedKeys);
    // B7 (2026-10-13): azzera gli spostamenti manuali di tutti i nodi ("Disponi automaticamente").
    void clearManualOffsets();

signals:
    void structureChanged();
    // FEAT-019: nodi i cui valori effettivi di stile sono cambiati (la scena li ridisegna).
    void stylesChanged(const QList<MindNode *> &nodes);
    // FEAT-019: modifica di stile fatta dall'utente (setStyle, clearStyle, setActivePalette).
    void styleEdited();
    // FEAT-036: lo sfondo effettivo del foglio e' cambiato (scelta, storico, file, foglio nuovo).
    void backdropChanged();
    // FEAT-027: collegamenti cambiati dall'utente (setLinks); la scena ridisegna i nodi, MainWindow
    // registra il passo di storico.
    void linksEdited(const QList<MindNode *> &nodes);
    void ganttChanged();  // FEAT-075
    // FEAT-052: the branch of a summary topic (not in the rows) is about to change and has changed:
    // the scene drops and remakes its items.
    void summaryBranchAboutToChange(MindNode *topic);
    void summaryBranchChanged(MindNode *topic);

private:
    // Palette letta da file o storico: id di base noto, oppure i colori salvati con il loro id (anche
    // vuoto, palette personalizzata); i file precedenti a FEAT-031, senza "paletteId", prendono la
    // palette di base con gli stessi colori, altrimenti quella predefinita.
    void readPalette(const QJsonObject &object);
    void readMarkerImages(const QJsonObject &object);
    QJsonObject markerImagesJson() const;
    void readGantt(const QJsonObject &object);  // FEAT-075
    QJsonObject m_ganttLinks;  // {"dependencies": [...], "calendar": {...}}, as in GanttPlan::toJson
    mutable QJsonObject m_markerImagesJson;  // cache: si ricodifica solo quando cambiano
    mutable bool m_markerImagesDirty = true;
    std::unique_ptr<MindNode> m_root;
    struct FlatSnapshotCache;
    std::unique_ptr<FlatSnapshotCache> m_flatCache;  // mutable nel contenuto: toFlatSnapshot() e' const
};

