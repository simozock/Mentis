#pragma once

#include <QGraphicsScene>
#include <QByteArray>
#include <QJsonObject>
#include "boundaryitem.h"
#include "maplayout.h"
#include "nodeitem.h"
#include "reordertarget.h"
#include "selectioncommands.h"
#include "relationshipitem.h"
#include "summary.h"

class QKeyEvent;

#include <functional>
#include <memory>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QModelIndex>

class Document;
class MindMapModel;
class NodeItem;
class QGraphicsPathItem;
class QGraphicsItem;
struct MindNode;

class QImage;

class MindMapScene final : public QGraphicsScene {
    Q_OBJECT
public:
    explicit MindMapScene(MindMapModel *model, QObject *parent = nullptr);
    // The Home's preview of a sheet (JPEG 480x272, AGENTS.md "Persistenza") on the sheet's own
    // background (theme variant); empty for an empty or unreadable sheet. Used by the app and by the
    // agents' server after a change (FEAT-045).
    static constexpr int kPreviewWidth = 480;
    static constexpr int kPreviewHeight = 272;
    static QByteArray homePreviewJpeg(const QHash<QString, QJsonObject> &sheet);
    static QByteArray previewJpeg(const QImage &preview);  // the Home's JPEG quality
    MindMapModel *model() const { return m_model; }
    // FEAT-096 stage 3: the document that owns the model. The title typed on a topic and the topics
    // dragged go through it (one history step each, as from any front end); a scene without one
    // (export, read-only uses) does not keep them.
    void setDocument(Document *document) { m_document = document; }
    MindNode *selectedNode() const;
    QList<MindNode *> selectedNodes() const;
    void rebuild();
    void autoLayout();
    void selectNode(MindNode *node);
    // Aggiornamenti incrementali: modificano solo gli item interessati invece di
    // distruggere e ricreare tutta la scena come fa rebuild().
    void updateNode(MindNode *node);   // un argomento: aspetto, posizione, rami collegati
    void refreshAll();                 // tutti gli argomenti, senza ricrearli (es. palette)
    void refreshSelectionStyle();      // dopo un cambio di NodeItem::selectionStyle()
    void refreshAnnotations();         // relazioni, sommari, confini
    // FEAT-057: each child of the central topic, with its branch and lines, drawn at its place in the
    // stacking order (MindMapModel::stackingOrder). Items already in place are not touched.
    void refreshStacking();
    // FEAT-053: relationships. Items persist across refreshes (selection kept) and follow their
    // elements; a relationship with a missing or hidden end is not drawn.
    QStringList selectedRelationIds() const;
    RelationshipItem *relationItem(const QString &id) const;
    void selectRelation(const QString &id);
    // Selects it when the mouse button that is selecting the target topic is released (at once
    // without a pressed button, e.g. the target chosen with the keyboard).
    void selectRelationAfterClick(const QString &id);
    std::optional<Relationships::Geometry> relationGeometry(const Relationship &relationship) const;
    // FEAT-052: boundaries, kept like the relationships; one with no visible topic is not drawn.
    QStringList selectedBoundaryIds() const;
    BoundaryItem *boundaryItem(const QString &id) const;
    void selectBoundary(const QString &id);
    // FEAT-056: replaces the selection with the drawn ones among targets; returns how many (with
    // apply false nothing changes: an empty "Select" entry is greyed out).
    int selectTargets(const SelectionCommands::Targets &targets, bool apply = true);
    // Topic or boundary under a scene point (empty on the background): the ends of relationships.
    QString elementAt(const QPointF &scenePos) const;
    // P3 (2026-10-13): durante l'annulla incrementale molti nodi cambiano insieme. Con
    // setDeferredUpdates(true) updateNode() aggiorna solo l'item; rami, annotazioni e area
    // della scena si ricalcolano una volta sola a setDeferredUpdates(false).
    void setDeferredUpdates(bool deferred);
    // Conferma la modifica del titolo aperta in un nodo, se c'è (2026-10-14). Restituisce true se
    // ce n'era una.
    bool commitEditing();
    // FEAT-027: stato delle icone dei collegamenti ad argomento. exists(id) dice se il nodo di
    // arrivo esiste in un foglio gia' letto (o se non si puo' sapere: foglio non letto = esiste).
    void refreshTopicLinkStates(const std::function<bool(const QString &targetId)> &exists);
    bool isTopicLinkBroken(const MindNode *node) const;
    // FEAT-027: rettangolo in coordinate della scena dell'icona di un collegamento (vuoto se manca).
    QRectF linkIconSceneRect(const MindNode *node, NodeItem::LinkKind kind) const;
    // Riquadro del nodo nella scena (vuoto se il nodo non e' nella scena).
    QRectF nodeSceneRect(const MindNode *node) const;
    // Visible shape of the node in the scene (no margins or selection ring), empty if missing.
    QRectF nodeShapeSceneRect(const MindNode *node) const;
    QRectF nodeOutlineSceneRect(const MindNode *node) const;  // FEAT-028: its table cell, otherwise its shape
    // FEAT-028: after a step of the history (positions restored without a layout) the table cells are
    // worked out again for the restored types and sizes (user's report 2026-10-25).
    void refreshTableCells();
    // The node's title is being edited on the map (its format bar may hold the focus).
    bool isEditing(const MindNode *node) const;
    // BUG-039: a topic's title is being edited on the map. Its live layout is not a step of its own:
    // the history and the autosave wait for the edit to end (see onEditingResized).
    bool editingTitle() const { return m_editingItem && m_editingItem->isEditing(); }
    // Collapse control drawn on one side (-1 left, 1 right) of the node in the scene, empty if none.
    QRectF collapseSceneRect(const MindNode *node, int side) const;
    // FEAT-072: shape actually drawn towards the children of parent. The one chosen on it, else the
    // map's default: from the central topic curves (fans) in the logic chart and rule A7 in
    // the mind map (Auto), the rounded bracket below it; Auto for variants with another line.
    BranchShape effectiveBranchShape(const MindNode *parent) const { return m_layout.effectiveBranchShape(parent); }
    // FEAT-054: a scene drawn for export: the text at every scale (no low-zoom shortcut) and images and
    // formulas decoded at `scale` times their shown size instead of the performance profile's factor.
    // Set before the first drawing (images load then); 0 = the map on screen.
    void setExportImageScale(qreal scale) { m_exportImageScale = scale; }
    qreal exportImageScale() const { return m_exportImageScale; }
    // FEAT-054: only `top` and its descendants stay shown, with the relationships, boundaries and summaries
    // that lie wholly among them. For an export scene: nothing shows them again.
    void isolateBranch(const MindNode *top);
    // FEAT-037: percorso dell'originale di un'immagine incorporata (estratto nella cache), per
    // disegnarla nitida oltre la miniatura. Impostato da MainWindow, che conosce il file aperto.
    std::function<QString(const MindNode *node)> originalImagePath;
    // FEAT-094: Ctrl+V while the title of `node` is typed; true when the clipboard went onto the node
    // (image, files, branch) instead of into the text. Set by MainWindow.
    std::function<bool(MindNode *node)> pasteWhileEditing;
    // FEAT-095: the RichTitle::CharStyle whose key the event is (Ctrl+B...), -1 for another key. Set by
    // MainWindow, which knows the shortcuts.
    std::function<int(const QKeyEvent *event)> charStyleKey;
    // FEAT-040: nodo la cui immagine e' scelta (al massimo uno), nullptr se nessuna.
    MindNode *selectedImageNode() const;
    // FEAT-049: nodo sotto il punto della scena (il piu' in alto), nullptr sullo sfondo.
    MindNode *nodeAt(const QPointF &scenePos) const;
    // Marcatore sotto il mouse (nodo e "serie:valore"); nodo nullptr se nessuno.
    std::pair<MindNode *, QString> hoveredMarker() const;
    // FEAT-060: "Evidenzia argomenti collegati": nodi e rami senza l'etichetta diventano opachi;
    // etichetta vuota = tutto normale.
    void setLabelHighlight(const QString &label);
    QString labelHighlight() const { return m_labelHighlight; }
    // FEAT-021: larghezza attuale del riquadro del testo del nodo (0 se il nodo non e' nella scena).
    qreal nodeWidth(const MindNode *node) const;
    int sideOf(const MindNode *node) const { return m_layout.sideOf(node); }  // FEAT-012: +1 destra, -1 sinistra, 0 centrale
    qreal imageRoomWidth(const MindNode *node) const;  // NodeItem::imageRoomWidth
    QPixmap nodePicture(const MindNode *node) const;  // FEAT-065: NodeItem::picture
    // FEAT-021: larghezza manuale da dare al nodo per una richiesta di requestedPx: intera, fra il
    // minimo del nodo (come trascinando il bordo) e kMaxManualNodeWidthPx.
    qreal manualWidthFor(const MindNode *node, qreal requestedPx) const;
    // FEAT-015: con il bilanciamento attivo (mappa mentale su due lati) trascinare un nodo cambia
    // ordine o genitore, non la posizione. I nodi liberi restano liberi.
    bool isReorderDrag(const MindNode *node) const;
    // Navigazione con le frecce (richiesta dell'utente 2026-10-21): il nodo visibile in quella direzione
    // da node, nullptr se non ce n'e'. Verso i figli il figlio piu' vicino (dal centrale il primo ramo di
    // quel lato), verso il genitore il genitore; di lato i nodi dello stesso livello (e lato), anche di
    // rami diversi, nell'ordine in cui si vedono. On a timeline Left/Right follow the axis and Up/Down
    // the column of subtopics (FEAT-026 phase E).
    MindNode *neighbour(MindNode *node, Qt::Key key) const;
    // Percorso del ramo entrante del nodo (vuoto per il centrale, i mobili e i nascosti), per le prove.
    QPainterPath edgePath(const MindNode *child) const;
    // FEAT-015: destinazione di un nodo trascinato sul punto della scena (regole in reordertarget.h,
    // condivise con l'app mobile).
    using ReorderTarget = ::ReorderTarget;
    ReorderTarget reorderTargetAt(const MindNode *dragged, const QPointF &scenePos) const;

signals:
    void nodeSelected(MindNode *node);
    void nodeChanged();
    void historyStepFinished();  // vedi NodeItem::historyStepFinished
    // FEAT-027: clic e tasto destro sulle icone dei collegamenti (vedi NodeItem).
    void linkActivated(MindNode *node, NodeItem::LinkKind kind);
    void linkMenuRequested(MindNode *node, NodeItem::LinkKind kind, const QPoint &screenPos);
    void imageSelectionChanged();  // FEAT-040: vedi selectedImageNode()
    void taskToggled(MindNode *node);  // FEAT-048
    void noteActivated(MindNode *node);  // FEAT-059
    void labelActivated(MindNode *node);  // FEAT-060
    void taskTyped(MindNode *node, int task);  // FEAT-068
    void filesActivated(MindNode *node, const QPoint &screenPos);  // FEAT-049
    void formulaActivated(MindNode *node);  // FEAT-064
    void formulaMenuRequested(MindNode *node, const QPoint &screenPos);
    void markerClicked(MindNode *node, const QString &marker);  // FEAT-051
    void imageMenuRequested(MindNode *node, const QPoint &screenPos);
    void markerMenuRequested(MindNode *node, const QString &marker, const QPoint &screenPos);
    void labelMenuRequested(MindNode *node, const QString &label, const QPoint &screenPos);
    void taskMenuRequested(MindNode *node, const QPoint &screenPos);
    void nodeMenuRequested(MindNode *node, const QPoint &screenPos);  // FEAT-042
    // FEAT-015: rilascio valido di un trascinamento strutturale (vedi ReorderTarget).
    void reorderRequested(MindNode *node, MindNode *newParent, int row, int side);
    // Rilascio sullo sfondo: il nodo diventa un argomento mobile con il centro in position.
    void floatRequested(MindNode *node, const QPointF &position);
    void collapseRequested(MindNode *node);  // FEAT-016: clic sul controllo di chiusura
    // FEAT-053: a relationship changed on the map (handle, end or label): the document applies it.
    void relationEdited(const Relationship &relationship);
    void relationMenuRequested(const QString &id, const QPoint &screenPos);
    // FEAT-052: a boundary title written on the map: the document applies it.
    void boundaryEdited(const Boundary &boundary);
    void boundaryMenuRequested(const QString &id, const QPoint &screenPos);

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    QString m_relationToSelect;  // FEAT-053: see selectRelationAfterClick
    // Nodi da ridisegnare dopo un cambio di stile o di collegamenti; se uno cambia misura la mappa
    // si ridispone (FEAT-019/020, FEAT-027).
    void refreshChangedNodes(const QList<MindNode *> &nodes);
    void addNodeRecursive(MindNode *node);
    void drawConnections(MindNode *node);
    // Non più usata: ricreava tutti i rami a ogni movimento. Sostituita da
    // updateEdgesFor(). Lasciata in attesa di conferma per la rimozione.
    void refreshConnections();
    void drawAnnotations();
    void syncRelationItems();  // FEAT-053
    std::optional<Relationships::End> relationEnd(const QString &id) const;
    RelationshipItem::Look relationLook(const Relationship &relationship) const;
    QHash<QString, RelationshipItem *> m_relationItems;
    void syncBoundaryItems();  // FEAT-052
    BoundaryItem::Look boundaryLook(const Boundary &boundary) const;
    // Union of the visible shapes of the topics and their branches (empty: none visible).
    QRectF branchesCovered(const QStringList &topics) const;
    // FEAT-028: none around table cells (it would cover the neighbouring cells).
    QRectF boundaryCovered(const Boundary &boundary) const;
    // FEAT-052: summaries. The topic's item is a NodeItem (in m_items, created on demand); the shared
    // node keeps it alive while the item exists. Placed after the layout, beside the bracket.
    struct SummaryItems {
        std::shared_ptr<MindNode> topic;
        QGraphicsPathItem *bracket = nullptr;
    };
    QHash<QString, SummaryItems> m_summaryItems;
    void syncSummaryItems();
    void onSummaryBranchAboutToChange(MindNode *topic);
    void onSummaryBranchChanged(MindNode *topic);
    Summaries::Side summarySide(const Summary &summary, const QRectF &covered, const QPointF &parentCentre) const;
    QHash<QString, BoundaryItem *> m_boundaryItems;
    // FEAT-016: nodi nascosti dalla chiusura dei rami, controlli di chiusura e rami nascosti.
    void applyCollapse();
    void collapseBranch(MindNode *node, bool hidden);  // applyCollapse on one branch
    using TablePlace = MapLayout::TablePlace;
    bool isTableCell(const MindNode *node) const;  // in a table, not its header
    void applyTableCells();
    bool tablePlaceStale(const MindNode *node) const;  // its cell no longer matches the types
    static bool childrenHidden(const MindNode *node) { return MapLayout::childrenHidden(node); }  // nodo non centrale chiuso
    // B7 (2026-10-13): posizioni calcolate dal layout automatico (MapLayout), poi i lati di crescita.
    void computeAutoPositions();
    void justifyLevels();  // FEAT-043
    // BUG-013: posizione = automatica + spostamenti a mano del nodo e di tutti gli antenati.
    void placeBranch(MindNode *node, QPointF inheritedOffset);
    static QPointF inheritedOffset(const MindNode *node);
    // BUG-013: durante il trascinamento di un nodo, i discendenti che lo seguono e la loro distanza
    // da lui all'inizio (per nodo trascinato; vuoto fuori dal trascinamento).
    struct Follower {
        NodeItem *item;
        QPointF offset;
    };
    void collectFollowers(NodeItem *dragged);
    QHash<MindNode *, QList<Follower>> m_dragFollowers;
    void onDragFinished(NodeItem *item);
    // FEAT-015: anteprima del nodo sotto il mouse e indicatore della destinazione.
    void onReorderMoved(NodeItem *item, const QPointF &scenePos);
    void onReorderFinished(NodeItem *item, const QPointF &scenePos, bool dropped);
    QGraphicsItem *m_reorderGhost = nullptr;
    QGraphicsPathItem *m_reorderIndicator = nullptr;
    QPointF m_reorderGrabOffset;  // dal mouse al centro del nodo all'inizio del trascinamento
    bool m_deferredUpdates = false;
    QSet<MindNode *> m_deferredNodes;
    void updateEdge(MindNode *child);
    void updateEdgesFor(MindNode *node);
    void onNodeMoved(NodeItem *item);
    void onEditingResized(NodeItem *item);
    // syncFromNode() su tutti gli item, con un solo nodeChanged() se qualcuno si e' spostato.
    void syncAllItems();
    void onRowsInserted(const QModelIndex &parent, int first, int last);
    void onRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last);
    void onRowsRemoved();
    void onRowsMoved(const QModelIndex &sourceParent, int start, int end, const QModelIndex &destination, int row);
    void removeNodeItems(MindNode *node);
    qreal stackZ(const MindNode *node) const;  // FEAT-057: z of the branch the node belongs to
    void setStackZBelow(MindNode *node, qreal z);
    void updateSceneRect();
    MindMapModel *m_model;
    Document *m_document = nullptr;
    QHash<MindNode *, NodeItem *> m_items;
    QString m_labelHighlight;  // FEAT-060
    qreal m_exportImageScale = 0;  // FEAT-054
    // Indice id -> item per le annotazioni (prima ricerca lineare su tutti i nodi).
    QHash<QString, NodeItem *> m_itemsById;
    // Ramo che entra in ciascun nodo figlio: permette di aggiornare solo i rami
    // del nodo spostato con setPath(), senza ricrearli.
    QHash<MindNode *, QGraphicsPathItem *> m_edgeByChild;
    QList<QGraphicsPathItem *> m_edges;
    QList<QGraphicsItem *> m_annotations;
    // Vero durante gli aggiornamenti di massa (layout automatico): i singoli
    // spostamenti non ricalcolano i rami, lo fa una sola passata finale.
    bool m_batchUpdate = false;
    bool m_batchMoved = false;  // un nodo si e' spostato durante syncAllItems()
    // BUG-039: the topic whose title is being edited, its size at the last live layout, whether a
    // layout is queued for this turn of the event loop and whether one ran during this edit.
    QPointer<NodeItem> m_editingItem;
    QSizeF m_editingLayoutSize;
    bool m_editingLayoutQueued = false;
    bool m_editingLaidOut = false;
    // FEAT-072: branch shapes at the last layout, to relayout when one changes.
    QHash<const MindNode *, BranchShape> m_laidOutShapes;
    // FEAT-096 stage 2: the automatic layout, run by autoLayout and applied here.
    MapLayout m_layout;
};

