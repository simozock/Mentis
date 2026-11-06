#pragma once

#include <optional>

#include <QGraphicsObject>
#include <QColor>
#include <QPainterPath>
#include <QPixmap>
#include <QTextLayout>
#include <functional>
#include <memory>

#include "formulatext.h"
#include "nodesketch.h"

struct MindNode;
class QGraphicsTextItem;
class QGraphicsProxyWidget;
class QTextDocument;

class QPainter;
// FEAT-060: testo di una pillola centrato sull'altezza delle maiuscole (mappa e outliner).
void drawLabelText(QPainter *p, const QRectF &pill, const QString &label, qreal padding);

class NodeItem final : public QGraphicsObject {
    Q_OBJECT
public:
    enum { Type = UserType + 1 };
    // Opacità dei nodi (e dei loro rami) durante il trascinamento sulla mappa:
    // 0 = invisibile, 1 = opaco. Prima era 0.4, alzata su richiesta per vedere
    // meglio il nodo trascinato.
    static constexpr qreal kDragOpacity = 0.55;
    explicit NodeItem(MindNode *node, QGraphicsItem *parent = nullptr);
    ~NodeItem() override;
    int type() const override { return Type; }
    QRectF boundingRect() const override;
    // Area cliccabile e di hover: il nodo senza il bordo di selezione esterno (2026-10-13).
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override;
    MindNode *node() const { return m_node; }
    // Font del titolo sulla mappa (FEAT-022: font, dimensione, peso, corsivo, sottolineato,
    // barrato e maiuscole del nodo; predefiniti 13 pt grassetto per la radice, 11 pt per gli
    // altri). L'outliner lo usa quando crea un titolo formattato, così la mappa lo legge uguale.
    static QFont titleFont(const MindNode *node);
    // FEAT-022: peso del grassetto dato a singole parole: 700, oppure 900 se il nodo e' gia' a 700
    // o piu' (come il titolo principale, 2026-10-14). Il testo normale ha il peso del nodo.
    static int boldWeightFor(const MindNode *node);
    // FEAT-022: vero se il font e' installato (elenco letto una volta); un font assente si
    // disegna con quello di sistema ma il nome resta nel nodo.
    static bool isFontInstalled(const QString &family);
    // Registra i caratteri inclusi nell'app (assets/fonts) una sola volta; la chiama isFontInstalled.
    static void loadBundledFonts();
    // Registers only Atkinson Hyperlegible, once: the interface font, set at startup (2026-10-28).
    static void loadInterfaceFont();
    // Modifica del titolo direttamente nel nodo (prima: finestra di dialogo). Da F2 o
    // doppio clic seleziona tutto il testo; da un clic sul testo mette il cursore lì.
    void beginEditing();
    // From Space: no selection, cursor after the last character (2026-11-04).
    void beginEditingAtEnd();
    bool isEditing() const { return m_editor != nullptr; }
    // Posto del titolo e dell'immagine (coordinate del nodo), per le prove; campo del titolo in modifica.
    QRectF titleArea() const { return textArea(); }
    QRectF pictureRect() const { return imageRect(); }
    const QGraphicsTextItem *titleEditor() const { return m_editor; }
    // Conferma subito la modifica aperta (2026-10-14): prima di cambiare foglio, salvare o chiudere,
    // altrimenti il testo scritto e non ancora confermato andava perso con il nodo.
    void commitEditing() { finishEditing(true); }
    // FEAT-033: Esc mentre si trascina un bordo. Rimette larghezza e posizione di partenza,
    // rilascia il mouse e non registra nessun passo. Falso se non c'e' un trascinamento in corso.
    bool cancelResize();
    // FEAT-021: larghezza del riquadro del testo (quella che si trascina dai lati) e minimo che si
    // puo' scegliere a mano, un carattere per riga. Il minimo impagina il titolo: va chiesto solo
    // quando si applica una larghezza, non a ogni aggiornamento del pannello.
    qreal textBoxWidth() const { return textRect().width(); }
    // Larghezza che un'immagine puo' avere senza allargare il nodo (contenuto meno i margini).
    qreal imageRoomWidth() const;
    // FEAT-065: pixel dell'immagine, dell'adesivo o della formula come li disegna la mappa (caricati
    // se non ancora fatto); vuoto senza contenuto o con il file mancante.
    QPixmap picture();
    // FEAT-015: il nodo disegnato da solo, senza anello di selezione o di passaggio, per l'anteprima
    // del trascinamento (devicePixelRatio = scale; offset = angolo di boundingRect()).
    QPixmap dragPreview(qreal scale) const;
    qreal minimumCustomWidth() const;
    // FEAT-037: rettangolo dell'immagine dentro il nodo (vuoto senza immagine) e immagine scelta con
    // un clic (contorno e maniglie agli angoli).
    QRectF imageBounds() const { return imageRect(); }
    bool isImageSelected() const { return m_imageSelected; }
    // FEAT-040: sceglie il nodo e la sua immagine, come un clic sull'immagine (anche per i test).
    void selectImage();
    // Misura d'inserimento di un'immagine: pixel reali del file (con la rotazione EXIF), ridotti in
    // proporzione oltre kMaxImageWidthPx; 180x120 se il file non si legge (FEAT-037, FEAT-040).
    static QSizeF insertionImageSize(const QString &path);
    static constexpr qreal kImageGapPx = 8;          // fra immagine e testo
    static constexpr qreal kMinImageWidthPx = 24;     // ridimensionando con le maniglie
    static constexpr qreal kMaxImageWidthPx = 3000;   // misura iniziale e maniglie
    static constexpr qreal kImageHandlePx = 8;        // maniglia agli angoli
    static constexpr qreal kThumbnailPx = 512;        // miniatura incorporata nel file (AGENTS.md)
    // B7 (2026-10-13): vero se l'ultimo rilascio del mouse ha concluso un trascinamento del nodo
    // (non un semplice clic). La scena lo usa per ricordare lo spostamento manuale.
    bool wasDragged() const { return m_wasDragged; }
    // FEAT-015: vero se trascinare questo nodo cambia ordine o genitore invece della posizione
    // (bilanciamento attivo). Lo decide la scena; senza risposta il trascinamento e' libero.
    std::function<bool(const NodeItem *item)> reorderDrag;
    // FEAT-015: Esc durante un trascinamento strutturale; falso se non ce n'e' uno.
    bool cancelReorder();
    // P6 (2026-10-13): sotto questo livello di zoom (0.4 = 40%) il nodo si disegna senza testo
    // e formule, che a quella scala sono comunque illeggibili.
    // 2026-10-14: il valore dipende dal profilo di prestazioni (Performance::detailZoomThreshold):
    // 0.4 con il profilo Standard, 0.6 con quello Leggero. Questa costante resta il valore Standard.
    static constexpr qreal kDetailZoomThreshold = 0.4;
    // Riallinea l'item ai dati del nodo (posizione, testo, colori, immagine) senza
    // distruggerlo: sostituisce la ricostruzione completa della scena per le modifiche
    // a un singolo argomento.
    void syncFromNode();
    // Vero se l'ultimo syncFromNode() ha cambiato la misura del nodo usata dal layout (testo, forma,
    // spessore del bordo): la scena allora ridispone la mappa (FEAT-020 parte B). Non conta l'area
    // in piu' dell'anello di selezione attorno ai bordi spessi o a mano libera.
    bool lastSyncChangedGeometry() const { return m_lastSyncChangedGeometry; }
    // FEAT-043 Justify: box width the layout gives the node (0 = none). It acts like a width set by
    // hand (the text wraps and aligns in it) and never narrows the node.
    void setJustifiedWidth(qreal width);
    qreal justifiedWidth() const { return m_justifiedWidth; }
    // Box width without justification; measured again only after the node changed.
    qreal naturalWidth();
    // Metà larghezza del riquadro del testo: punto in cui si agganciano i rami.
    // Per i nodi normali è 105 come prima; i nodi con formule possono essere più larghi.
    qreal anchorHalfWidth() const;
    // FEAT-034: lato verso cui il nodo si allarga mentre si scrive (+1 destra, -1 sinistra, 0 i due
    // lati); il lato verso il genitore resta fermo. Lo imposta la scena dopo il layout.
    void setGrowSide(int side) { m_growSide = side; }
    // FEAT-016: controllo per chiudere i rami, fuori dal nodo sul lato dei figli (non sul centrale).
    // Vuoto sul nodo selezionato con figli; con i figli nascosti mostra quanti sono ed e' sempre
    // visibile. Lo imposta la scena dopo il layout.
    struct CollapseControl {
        bool shown = false;   // il nodo ha figli da quel lato
        int hidden = 0;       // discendenti nascosti (0 = aperto)
        QColor color;         // colore del ramo
        // Children above or below (org chart, tree trunk): side -1 = above, +1 = below, and the open
        // circle's centre sits `reach` px from that side, on the branch junction.
        bool vertical = false;
        qreal reach = 0;
        bool operator==(const CollapseControl &) const = default;
    };
    void setCollapseControls(const CollapseControl &left, const CollapseControl &right);
    // FEAT-028: the node as a tree-table cell. The cell (node coordinates) replaces the shape when
    // drawn: filled rectangle, border on its edges (shared with the neighbours), ring and collapse
    // control around it. The layout keeps measuring the node's own shape. Set by the scene.
    struct TableCell {
        QRectF rect;
        QColor fill;
        QColor border;
        // Stage 2: the cell holds the topic's whole branch (a type of its own). The item goes under
        // the lines (kHoldingCellDepth), which run over the cell to the children, and the collapse
        // control stays beside the topic.
        bool holdsBranch = false;
        // Stage 2, a table in a cell: the cell of the outer table, drawn first; rect is the header.
        QRectF frame;
        QColor frameFill;
        QColor frameBorder;
        bool operator==(const TableCell &) const = default;
    };
    static constexpr qreal kTableBorderPx = 1.5;  // measured live 2026-10-25
    static constexpr qreal kHoldingCellDepth = 2;  // the lines are in [-1, 0)
    void setTableCell(const std::optional<TableCell> &cell);
    const std::optional<TableCell> &tableCell() const { return m_cell; }
    // Rettangolo del controllo dal lato side (-1 sinistra, +1 destra) se si vede, altrimenti vuoto.
    QRectF collapseRect(int side) const;
private:
    QRectF collapseRect(int side, bool evenIfHidden) const;
public:
    static constexpr qreal kCollapseSizePx = 18;  // diametro (piu' largo con numeri lunghi)
    static constexpr qreal kCollapseGapPx = 4;    // dal bordo della forma
    static constexpr qreal kCollapseOpenSizePx = 11;    // open branch: circle on the branch junction (FEAT-072)
    static constexpr qreal kCollapseOpenOffsetPx = 12;  // = BranchPath::kStubPx
    int growSide() const { return m_growSide; }
    // Dopo un cambio di misura: il nodo si sposta verso il lato libero di quanto e' cambiata la meta'
    // larghezza (oldHalf = quella di prima). Chi lo chiama scrive la posizione nel modello.
    void keepParentSide(qreal oldHalf);
    // FEAT-020 parte B: rettangolo esterno della forma (contiene il riquadro del testo) e punto
    // di attacco del ramo a destra o a sinistra, in coordinate del nodo.
    QRectF shapeRect() const;
    qreal cornerRadius() const;  // rounded rectangle corner, < 0 = the usual one (FEAT-052)
    QPointF branchAnchor(bool right) const;
    // Altezza occupata dal nodo, usata dal layout automatico per distanziare i nodi.
    qreal layoutHeight() const;
    // Bordo esterno dei nodi selezionati e in hover (richiesta 2026-10-13: un anello
    // con gli angoli concentrici al nodo, staccato di "gap" px. Valori da Impostazioni >
    // Selezione, salvati in QSettings; uguali per tutti i nodi.
    struct SelectionStyle {
        QColor selectedColor{0x2e, 0xbd, 0xff};  // #2ebdff, misurato sulla foto d'esempio
        QColor hoverColor{0xcf, 0xcf, 0xcf};     // #cfcfcf, misurato sulla foto d'esempio
        qreal width = 2;                         // spessore, px (1-4)
        // Seconda foto dell'utente (2026-10-13): lo spazio bianco visibile tra anello e nodo è circa
        // metà dello spessore dell'anello. Ora la distanza si misura dal bordo esterno del nodo
        // (il tratto di 2 px del riquadro compreso), vedi kNodeBorderOutside in nodeitem.cpp.
        qreal gap = 1;                           // spazio bianco visibile tra anello e nodo, px (0-8)
        bool hoverEnabled = true;
        // Sfondo del testo selezionato mentre si scrive (mappa, voci e titolo principale
        // dell'outliner). Richiesta 2026-10-13: colore uguale ovunque e modificabile in Impostazioni.
        // Il testo selezionato resta #202630 (AGENTS.md "Colori").
        QColor textSelectionColor{0xb3, 0xd4, 0xfc};  // #b3d4fc, AGENTS.md "Colori"
    };
    static SelectionStyle &selectionStyle();
    // FEAT-059: "Mostra tutte le note" del foglio attivo (impostato da MainWindow, poi syncFromNode).
    static bool &showAllNotes();
    // Riquadro della nota dentro il nodo, sotto il contenuto (vuoto se spento o senza nota).
    QRectF notePreviewRect() const;
    // FEAT-060: pillole delle etichette sotto la forma (posto "fuori"), nell'ordine di node->labels.
    QList<QRectF> labelRects() const;
    int hoveredLabel() const { return m_hoveredLabel; }  // per i test
    bool isHovered() const { return m_hovered; }
    int labelAt(const QPointF &position) const;  // -1 = nessuna
    // Raggio della linea centrale dell'anello (richiesta 2026-10-13 con foto: angoli meno
    // arrotondati di quelli del nodo, raggio 8). Prima era concentrico: 8 + distanza dal bordo.
    static constexpr qreal kSelectionRadius = 4;
    // z del nodo mentre si scrive: sopra gli altri nodi (z 0), così la barra di formattazione,
    // figlia del nodo, non finisce sotto i nodi aggiunti dopo (bug 2026-10-13).
    static constexpr qreal kEditingZValue = 10;
    // FEAT-057: z of the branch in the stacking order, in [0, 1) (0 = central topic); kept while
    // the title is edited and restored afterwards.
    void setStackZ(qreal z);
    qreal stackZ() const { return m_stackZ; }
    qreal restingZ() const;  // z when not edited: the stacking order, under the lines for a cell holding a branch
    // Da chiamare dopo aver cambiato selectionStyle(): un anello più largo può uscire dal riquadro.
    void selectionStyleChanged();

    // FEAT-027: bottoni dei collegamenti, dentro il nodo a destra del contenuto (prima quello
    // dell'argomento, poi quello web): quadrati arrotondati chiari con l'icona al centro (richiesta
    // dell'utente 2026-10-16, foto 02). Rettangolo vuoto se il nodo non ha quel collegamento.
    enum class LinkKind { Topic, Web };
    QRectF linkIconRect(LinkKind kind) const;
    // FEAT-048: area cliccabile della casella di spunta (22x22, posto a sinistra), vuota senza casella.
    QRectF taskRect() const;
    // FEAT-051: marcatori nel posto a sinistra, prima della casella, nell'ordine di node->markers.
    QList<QRectF> markerRects() const;
    int markerAt(const QPointF &position) const;  // -1 = nessuno
    int hoveredMarker() const { return m_hoveredMarker; }  // marcatore sotto il mouse (Canc lo toglie)
    // FEAT-059: bottone della nota, il primo del posto a destra (prima dei collegamenti); vuoto senza nota.
    QRectF noteRect() const;
    // FEAT-049: graffetta dei file allegati, l'ultimo bottone a destra (dopo nota e collegamenti);
    // vuota senza file. Con due o piu' file porta il numero.
    QRectF filesRect() const;
    // FEAT-064: formula del nodo, nel posto dell'immagine (vuoto senza formula).
    QRectF formulaRect() const;
    // Misura d'inserimento della formula del nodo (riquadro con i margini, font del nodo).
    static QSizeF formulaInsertionSize(const MindNode *node);
    static constexpr qreal kTaskBoxPx = 16;
    // Argomento collegato che non esiste piu' (deciso da chi conosce tutti i fogli): icona grigia.
    void setTopicLinkBroken(bool broken);
    bool isTopicLinkBroken() const { return m_topicLinkBroken; }
    static constexpr qreal kLinkButtonPx = 22;   // bottone di un collegamento (quadrato)
    static constexpr qreal kLinkIconPx = 16;     // icona dentro il bottone
    static constexpr qreal kLinkButtonRadiusPx = 4;
    static constexpr qreal kLinkIconGapPx = 4;   // fra i due bottoni
    static constexpr qreal kLinkTextGapPx = 8;   // fra il testo e il primo bottone

signals:
    void activated(NodeItem *item);
    void moved(NodeItem *item);
    void dragFinished(NodeItem *item);
    void collapseToggled(NodeItem *item);  // FEAT-016: clic sul controllo
    // FEAT-015: trascinamento strutturale (il nodo non si sposta): punto della scena sotto il
    // mouse e fine (dropped falso = annullato con Esc).
    void reorderMoved(NodeItem *item, const QPointF &scenePos);
    void reorderFinished(NodeItem *item, const QPointF &scenePos, bool dropped);
    void imagePlacementChanged(NodeItem *item);
    void titleEdited(NodeItem *item, const QString &text, const QString &html);
    // Fra due titleEdited della stessa sessione di modifica: il passo va registrato subito nello
    // storico, così ogni formattazione si annulla con un Ctrl+Z distinto.
    void historyStepFinished();
    // Emesso quando il nodo diventa trasparente (inizio trascinamento) o torna
    // opaco (rilascio): la scena applica la stessa opacità ai rami collegati.
    void dragFadeChanged(NodeItem *item, bool active);
    // Il riquadro cambia misura mentre si scrive: la scena riallinea solo i rami.
    void editingResized(NodeItem *item);
    // Fine del trascinamento di un lato del nodo (larghezza cambiata) o ritorno alla larghezza
    // automatica con il doppio clic sul bordo (FEAT-018): la scena ridispone la mappa e lo
    // storico registra un passo.
    void resizeFinished(NodeItem *item);
    // FEAT-027: clic sinistro su un'icona di collegamento (rilascio sulla stessa icona) e tasto
    // destro sull'icona (menu Modifica/Rimuovi, mostrato da chi riceve il segnale).
    void linkActivated(NodeItem *item, NodeItem::LinkKind kind);
    void linkMenuRequested(NodeItem *item, NodeItem::LinkKind kind, const QPoint &screenPos);
    // FEAT-048: clic sulla casella (rilascio sulla casella) e tasto destro sulla casella.
    void taskToggled(NodeItem *item);
    void noteActivated(NodeItem *item);  // FEAT-059: clic sull'icona della nota
    void filesActivated(NodeItem *item, const QPoint &screenPos);  // FEAT-049: clic o tasto destro sulla graffetta
    void formulaActivated(NodeItem *item);  // FEAT-064: doppio clic sulla formula
    void formulaMenuRequested(NodeItem *item, const QPoint &screenPos);
    // FEAT-051: clic su un marcatore (valore seguente della serie) e tasto destro (menu).
    void markerClicked(NodeItem *item, const QString &marker);
    // Tasto destro sull'immagine o sull'adesivo (la formula ha formulaMenuRequested).
    void imageMenuRequested(NodeItem *item, const QPoint &screenPos);
    void markerMenuRequested(NodeItem *item, const QString &marker, const QPoint &screenPos);
    // FEAT-060: clic su una pillola (riquadro delle etichette) e tasto destro (menu dell'etichetta).
    void labelActivated(NodeItem *item);
    // FEAT-068: "- [ ] " (1) o "- [x] " (2) scritto a inizio titolo, alla conferma.
    void taskTyped(NodeItem *item, int task);
    void labelMenuRequested(NodeItem *item, const QString &label, const QPoint &screenPos);
    void taskMenuRequested(NodeItem *item, const QPoint &screenPos);
    void nodeMenuRequested(NodeItem *item, const QPoint &screenPos);  // FEAT-042: right-click elsewhere on the topic
    // FEAT-040: l'immagine e' stata scelta o non lo e' piu'.
    void imageSelectionChanged(NodeItem *item);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    // FEAT-027: larghezza della colonna delle icone a destra del testo (0 senza collegamenti) e
    // riquadro del solo testo. La larghezza scelta a mano (customWidth, FEAT-018/021) resta quella
    // di tutto il riquadro: il testo va a capo nel resto (FEAT-037: anche senza l'immagine affiancata).
    qreal linkZoneWidth() const;
    qreal taskZoneWidth() const;  // FEAT-048: colonna a sinistra (0 senza casella)
    bool m_pressedTask = false;
    int m_pressedMarker = -1;  // FEAT-051
    int m_hoveredMarker = -1;
    bool m_hoveredTask = false;
    bool m_pressedNote = false;  // FEAT-059
    bool m_pressedFiles = false;  // FEAT-049
    int m_pressedCollapse = 0;    // FEAT-016: lato del controllo premuto
    CollapseControl m_collapseLeft, m_collapseRight;
    void paintCollapseControls(QPainter *p);
    bool m_hoveredFiles = false;
    int m_pressedLabel = -1;     // FEAT-060
    int m_hoveredLabel = -1;     // pillola sotto il mouse: si accende il suo bordo, non il nodo
    struct LabelLayout {
        QStringList labels;
        qreal width = 0;           // larghezza a cui vanno a capo
        QList<QRectF> pills;       // relative all'angolo in alto a sinistra del blocco
        QSizeF size;
        bool operator==(const LabelLayout &other) const { return labels == other.labels && width == other.width; }
    };
    LabelLayout m_labelLayout;
    void updateLabelLayout();
    qreal labelsBlockHeight() const;  // pillole e spazio sopra (0 senza etichette)
    struct NotePreviewKey {
        bool shown = false;
        QString notes, notesHtml;
        qreal width = 0;
        QRgb color = 0;  // FEAT-064: le formule della nota prendono il colore del testo
        bool operator==(const NotePreviewKey &) const = default;
    };
    NotePreviewKey m_notePreviewKey;
    std::unique_ptr<QTextDocument> m_notePreview;
    qreal m_notePreviewHeight = 0;
    void updateNotePreview();
    bool m_hoveredNote = false;
    qreal manualTextWidth() const;
    QRectF textArea() const;
    // FEAT-037: impaginazione del contenuto (immagine, testo, bottoni dei collegamenti) calcolata
    // dalle misure gia' fatte: riquadro di tutto il nodo, blocco del testo con i suoi margini e
    // immagine. Solo aritmetica, nessuna misura del testo.
    // content = immagine e testo senza la nota (le colonne si centrano qui); note = nota mostrata
    // dentro il nodo (FEAT-059, posto "sotto" del contratto).
    struct ContentLayout { QRectF box, text, image, content, note; };
    ContentLayout contentLayout(bool withNote = true) const;
    bool hasSideImage() const;
    // Larghezza del blocco del testo (margini compresi) per un contenuto largo "content".
    qreal textBlockWidth(qreal content, qreal maxWidth) const;
    // FEAT-037: zone di rilascio dell'immagine attorno al nodo (Sopra, Sotto, Sinistra, Destra).
    QList<QRectF> imageDropZones() const;
    // FEAT-037: maniglia dell'immagine sotto il punto: 1 alto-sinistra, 2 alto-destra,
    // 3 basso-destra, 4 basso-sinistra, 0 nessuna.
    int imageHandleAt(const QPointF &position) const;
    bool m_imageSelected = false;
    bool m_imageMissing = false;  // FEAT-039: file mancante o rovinato, si disegna il segnaposto
    void setImageSelectedState(bool selected);  // aggiorna, ridisegna e avvisa (FEAT-040)
    bool m_imagePressed = false;      // clic sull'immagine non ancora diventato trascinamento
    QPointF m_imagePressPos;
    int m_imageResizeCorner = 0;      // maniglia trascinata
    QSizeF m_imageResizeStartSize;
    qreal m_imageResizeStartX = 0;
    std::optional<LinkKind> linkAt(const QPointF &position) const;
    bool m_topicLinkBroken = false;
    std::optional<LinkKind> m_pressedLink;  // icona premuta, il clic vale al rilascio
    std::optional<LinkKind> m_hoveredLink;  // bottone sotto il mouse (fondo piu' scuro)
    QRectF textRect() const;
    QRectF imageRect() const;
    QRectF normalBoundingRect() const;
    bool m_hovered = false;  // mouse sopra il nodo: anello di hover
    int imageDropZoneAt(const QPointF &position) const;
    void loadImage();
    void updateFormula();
    // Rendering del titolo quando contiene formule LaTeX ($...$); vuoto altrimenti.
    QString m_loadedFormulaKey;  // FEAT-064: formula, sfondo e colore disegnati in m_image
    FormulaText m_formula;  // formule $...$ nel titolo (testo con formule in riga)
    // Documento già impaginato per i titoli con formattazione parziale; viene aggiornato
    // solo quando cambiano i dati del nodo, non durante paint().
    QTextDocument *m_richDocument = nullptr;
    // Dimensione del titolo (testo semplice) misurata in updateFormula(): evita di
    // ricalcolare le metriche del font a ogni paint().
    QSizeF m_textSize;
    qreal m_minContentWidth = 0;  // BUG-003: un carattere medio, vedi updateFormula()
    // P5 (2026-10-13): titolo semplice già impaginato (a capo, centratura) in updateFormula().
    // Prima drawText(..., Qt::TextWordWrap, ...) rifaceva l'impaginazione a ogni paint(), cioè a
    // ogni pan, zoom o passaggio del mouse. Vuoto per formule e titoli formattati.
    std::unique_ptr<QTextLayout> m_plainLayout;
    QSizeF m_plainLayoutSize;
    qreal m_plainLayoutFontHeight = 0;  // altezza del font usato, per riconoscere un dispositivo con altri DPI
    // 2026-10-14: esito del confronto qui sopra per l'ultimo dispositivo di disegno. Prima paint()
    // creava un QFontMetricsF a ogni ridisegno di ogni nodo; ora solo quando cambia il dispositivo
    // (vista, stampante, immagine dell'anteprima) o l'impaginazione. Il puntatore non si dereferenzia.
    const void *m_metricsDevice = nullptr;
    bool m_metricsDeviceMatches = false;
    void updatePlainLayout(const QFont &font, qreal fontHeight);
    // (Larghezza del blocco del testo: textBlockWidth, sopra; FEAT-037 la separa dal riquadro.)
    // Larghezza a cui va a capo il titolo semplice in un blocco largo boxWidth.
    qreal plainWrapWidth(qreal boxWidth) const;
    qreal maxTextNodeWidth() const;  // FEAT-072: wider for the central topic
    // Margini interni del testo per il ruolo del nodo (NodeStyle::textPadding).
    qreal padX() const;
    qreal padY() const;
    qreal textClipX() const;
    qreal textClipY() const;
    // Ridimensionamento dai lati (richiesta 2026-10-13): -1 lato sinistro, 1 destro, 0 nessuno.
    int resizeSideAt(const QPointF &position) const;
    // (minimumCustomWidth, pubblica: il carattere più largo del titolo più i margini, cioè un
    // carattere per riga; con le formule, il pezzo di formula più largo.)
    int m_resizeSide = 0;  // lato trascinato, 0 = nessun ridimensionamento in corso
    qreal m_resizeStartX = 0, m_resizeStartWidth = 0, m_resizeMinWidth = 0;
    QPointF m_resizeStartPos;
    qreal m_resizeStartCustomWidth = 0;
    // BUG-004: vero dal doppio clic sul bordo al rilascio del tasto. Intanto il nodo ignora i
    // movimenti del mouse, che altrimenti lo trascinerebbero di qualche pixel.
    bool m_ignoreDragUntilRelease = false;  // FEAT-033: valore da rimettere con Esc (0 = automatica)
    static QSizeF plainTextLayout(const QString &plain, const QFont &font, qreal width, QTextLayout *layout,
                                  Qt::Alignment alignment = Qt::AlignLeft);
    // P2 (2026-10-13): dati da cui dipendono misura e impaginazione del nodo, com'erano all'ultimo
    // syncFromNode(). Se non cambiano, syncFromNode() non rimisura il testo: il layout automatico
    // chiama syncFromNode() su tutti i nodi a ogni Tab/Invio e prima rimisurava sempre tutto.
    // I dati da cui dipende l'impaginazione del testo (updateFormula()) sono separati dagli altri,
    // che cambiano solo il riquadro intorno al testo.
    struct TextLayoutKey {
        QString title, titleHtml;
        QRgb formulaColor = 0;  // colore del testo, solo per i titoli con formule
        bool isRoot = false;
        qreal customWidth = 0;
        bool strikeOut = false;   // FEAT-020: il testo formattato si reimpagina con il barrato
        // FEAT-022: font, dimensione, peso, corsivo, sottolineato, maiuscole e allineamento.
        QString fontFamily;
        qreal fontSize = 0;
        int fontWeight = 0, textCase = 0, textAlign = 0;
        bool italic = false, underline = false;
        int linkCount = 0;  // FEAT-027: le icone tolgono spazio al testo, che va a capo prima
        qreal sideImageWidth = 0;  // FEAT-037: immagine a sinistra o a destra del testo
        int task = 0;              // FEAT-048/051: casella e marcatori tolgono spazio al testo
        bool operator==(const TextLayoutKey &) const = default;
    };
    struct GeometryKey {
        TextLayoutKey text;
        QString imagePath;
        QSizeF imageSize;
        int imagePlacement = 0;
        qreal borderOutside = 1;  // FEAT-020: il bordo spesso allarga l'area dell'anello
        int shape = 0;            // FEAT-020 parte B: la forma cambia la misura del nodo
        qreal borderWidth = 0;    // BUG-012: con le linee sotto la seconda linea scende di uno spessore
        // FEAT-059: nota mostrata dentro il nodo (cambia la misura).
        bool notesShown = false;
        QString notes, notesHtml;
        QStringList labels;  // FEAT-060: le pillole cambiano lo spazio del nodo nel layout
        QString formula;     // FEAT-064: con la formula il nodo ha il contenuto nel posto dell'immagine
        bool operator==(const GeometryKey &) const = default;
    };
    GeometryKey geometryKey() const;
    GeometryKey m_geometryKey;
    qreal layoutCustomWidth() const;  // width set by hand or by Justify, the larger
    qreal m_justifiedWidth = 0;
    std::optional<TableCell> m_cell;  // FEAT-028
    qreal m_naturalWidth = 0;
    void rememberNaturalWidth();
    std::optional<GeometryKey> m_naturalKey;  // geometry m_naturalWidth was measured with
    // boundingRect() at the end of the last syncFromNode: the area the node may still cover on screen.
    QRectF m_syncedBounds;
    // FEAT-020 parte C: tracciati a mano libera e dei motivi, ricalcolati solo quando cambiano
    // misura, forma, tipo di riempimento o di bordo (paint() li disegna gia' pronti).
    struct SketchKey {
        QRectF box;
        int shape = 0, fillStyle = 0, borderStyle = 0;
        qreal borderWidth = 0;
        bool operator==(const SketchKey &) const = default;
    };
    struct Sketch {
        SketchKey key;
        bool valid = false;
        QPainterPath area;     // Pieno a mano libera
        NodeSketch::Pattern pattern;  // segmenti dei motivi (QPainter::drawLines)
        QPainterPath border;   // bordo o linee sotto a mano libera, gia' tracciati (si riempiono)
        QPainterPath borderArea;  // area dentro il bordo a mano libera, per il riempimento pieno
    };
    const Sketch &sketchFor(const QRectF &shapeBox);
    Sketch m_sketch;
    bool m_lastSyncChangedGeometry = false;
    bool m_wasDragged = false;
    MindNode *m_node;
    QPixmap m_image;
    // Percorso e dimensione con cui m_image è stata caricata: l'immagine viene
    // riletta solo se uno dei due cambia.
    // Durante il trascinamento sulla mappa i nodi selezionati diventano trasparenti,
    // così non coprono gli argomenti sotto il cursore.
    void setDragFade(bool active);
    bool m_dragFaded = false;
    bool m_reordering = false;  // FEAT-015
    QString m_loadedImagePath;
    QSizeF m_loadedImageSize;
    bool m_loadedOutline = false;  // FEAT-051: contorno dell'adesivo nei pixel caricati
    int m_loadedRotation = 0;      // rotazione dei pixel caricati (ImageStyle::rotation)
    int m_growSide = 0;            // FEAT-034
    qreal m_halfBeforeEditing = -1;  // FEAT-034: meta' larghezza prima del campo di modifica
    bool stickerOutline() const;
    bool m_imageLoadPending = true;
    // B9 (2026-10-13): il nodo ha un'immagine secondo i dati (imagePath). Le misure del nodo
    // dipendono da questo e non da m_image, che si carica solo al primo paint(): prima il
    // riquadro cambiava durante paint() senza prepareGeometryChange().
    bool m_hasImage = false;
    bool m_draggingImage = false;
    QPointF m_imageDragPosition;

    // Modifica nel nodo: campo di testo trasparente sopra il titolo, stesso font e colore.
    void startInlineEditor();
    void beginEditingAt(const QPointF &position);
    void finishEditing(bool commit);
    void layoutEditor();
    QGraphicsTextItem *m_editor = nullptr;
    qreal m_stackZ = 0;
    // FEAT-024: title as drawn, with the number before it (the editor shows the title alone).
    QString shownTitle() const;
    void prependNumber(QTextDocument &document) const;
    QGraphicsProxyWidget *m_formatBar = nullptr;
    void placeFormatBar();  // above the topic, centred, like the topic bar
    QSizeF m_editorSize;
    // Clic sul titolo: se il mouse non si sposta (niente trascinamento del nodo),
    // al rilascio parte la modifica nel punto cliccato.
    bool m_pressOnText = false;
    QPointF m_pressScreenPos;
};

