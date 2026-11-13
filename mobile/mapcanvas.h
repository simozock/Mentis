#pragma once

#include "mobilemap.h"
#include "reordertarget.h"

#include <QAbstractAnimation>
#include <QColor>
#include <QFont>
#include <QRectF>
#include <QElapsedTimer>
#include <QList>
#include <QMutex>
#include <QPointer>
#include <QPointF>
#include <QQuickItem>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// FEAT-097 stage A: the map drawn by the Qt Quick scene graph. The sheet's lines, shapes and titles
// are built once into nodes (MapScene) when the sheet changes; pan and zoom only set the transform
// above them. The QQuickPaintedItem canvas measured before repainted everything with QPainter at
// each frame and missed 60 fps by about 15 times (FEAT-097, "Canvas decision").
//
// Stage B1: a tap selects the topic under the finger; the selection ring is a node of its own,
// made again when the selection changes, so selecting does not rebuild the sheet. After an edit
// the sheet is built again and the view stays where it is, moved only to show the selected topic.
//
// Stage B2: a topic picked up by a long press follows the finger as a copy at 60% opacity, with the
// blue slot where it will go (ReorderTarget, the desktop's rules), in a holder above the selection;
// the sheet is not built again until the drop.
class MapCanvas : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MobileMap *map READ map WRITE setMap NOTIFY mapChanged)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(bool benchmarkRunning READ benchmarkRunning NOTIFY benchmarkChanged)
    Q_PROPERTY(QString frameReport READ frameReport NOTIFY benchmarkChanged)
    // The band of the item the map is seen in (item px): what lies above bandTop or below
    // bandBottom is covered (the top groups, the bottom groups or the on-screen keyboard).
    // reveal() and centerSelected() keep the topic inside it. bandBottom <= 0 = the item's height.
    Q_PROPERTY(qreal bandTop READ bandTop WRITE setBandTop NOTIFY bandChanged)
    Q_PROPERTY(qreal bandBottom READ bandBottom WRITE setBandBottom NOTIFY bandChanged)
    // While true the selected topic's title is typed in the page's field, drawn over the topic in
    // its place (ui-text-editing.md, "Mappa"): the scene leaves that title out.
    Q_PROPERTY(bool editing READ editing WRITE setEditing NOTIFY editingChanged)
    // Where the field goes, for the selected topic: editorAnchor (item px) is the left end of the
    // first line for left-aligned and justified titles, its middle for centred ones, its right end
    // for right-aligned ones, at the height of the text block's middle. editorWidth is the width
    // the title wraps at (map px: the field is scaled by the zoom).
    Q_PROPERTY(QPointF editorAnchor READ editorAnchor NOTIFY editorChanged)
    Q_PROPERTY(qreal editorWidth READ editorWidth NOTIFY editorChanged)
    Q_PROPERTY(int editorAlignment READ editorAlignment NOTIFY editorChanged)
    Q_PROPERTY(QFont editorFont READ editorFont NOTIFY editorChanged)
    Q_PROPERTY(QColor editorColor READ editorColor NOTIFY editorChanged)
    // FEAT-115: behind the field while a boundary's title is typed, the tab (BoundaryItem's editor
    // grows the tab with the text): its colour (transparent for a topic or a relationship), its
    // height and the text's padding on each side (map px, scaled by the zoom).
    Q_PROPERTY(QColor editorBackdrop READ editorBackdrop NOTIFY editorChanged)
    Q_PROPERTY(qreal tabHeight READ tabHeight CONSTANT)
    Q_PROPERTY(qreal tabPadding READ tabPadding CONSTANT)
    // B2: true while a topic picked up by a long press is dragged, or (FEAT-106) the selected
    // topic's selected picture.
    Q_PROPERTY(bool moving READ moving NOTIFY movingChanged)
    // FEAT-114: the tag whose topics are highlighted (the others faded); empty = none.
    Q_PROPERTY(QString labelHighlight READ labelHighlight NOTIFY labelHighlightChanged)
    // The desktop's blue "+" knobs of the selected topic (TopicTools, FEAT-072 F3): add a child, add
    // a sibling. Each has its centre and its touch target (item px), the target 48 px on the side of
    // the topic the knob is on, so a tap on the topic never reaches it. An empty target = no knob
    // (no selection; no sibling for the central, a floating or a summary topic).
    Q_PROPERTY(QPointF childKnob READ childKnob NOTIFY editorChanged)
    Q_PROPERTY(QRectF childKnobTarget READ childKnobTarget NOTIFY editorChanged)
    Q_PROPERTY(QPointF siblingKnob READ siblingKnob NOTIFY editorChanged)
    Q_PROPERTY(QRectF siblingKnobTarget READ siblingKnobTarget NOTIFY editorChanged)
    // The desktop's collapse control of the selected topic (NodeItem::collapseRect, FEAT-016): an
    // open branch's circle on the children's side, or a closed branch's pill with the count of its
    // hidden topics, tied to the topic by a short line from `collapseEdge`. Item px, its touch target
    // from the topic's border outwards. An empty target = no control (not the central or a summary
    // topic, no branches).
    Q_PROPERTY(QRectF collapseControl READ collapseControl NOTIFY editorChanged)
    Q_PROPERTY(QRectF collapseTarget READ collapseTarget NOTIFY editorChanged)
    Q_PROPERTY(QPointF collapseEdge READ collapseEdge NOTIFY editorChanged)
    Q_PROPERTY(int collapseCount READ collapseCount NOTIFY editorChanged)
    Q_PROPERTY(QColor collapseColor READ collapseColor NOTIFY editorChanged)
    Q_PROPERTY(QColor mapBackground READ mapBackground NOTIFY editorChanged)  // the control's fill

public:
    // The desktop's zoom range (25-400%, MainWindow's zoom menu); a map larger than the screen may
    // go below it to fit.
    static constexpr qreal kMinZoom = 0.25;
    static constexpr qreal kMaxZoom = 4.0;
    static constexpr qreal kFitMarginPx = 24;  // around the map at fit, on the 4 px grid
    // The selection ring's defaults (NodeItem::SelectionStyle and kSelectionRadius, ui-map.md
    // "Selezione e hover"); the desktop's Settings do not reach the phone.
    static constexpr qreal kSelectionWidth = 2;
    static constexpr qreal kSelectionGap = 1;
    static constexpr qreal kSelectionRadius = 4;
    static inline const QColor kSelectionColor{0x2e, 0xbd, 0xff};
    static constexpr int kBenchmarkFrames = 300;
    static constexpr qreal kEditZoom = 1.0;
    // TopicTools' knobs at zoom 1: centre from the side and from the bottom of the topic; they follow
    // the zoom as the topic (BUG-051). A knob over the collapse control moves just past it, kCollapseKnobGapPx
    // away, as on the desktop.
    static constexpr qreal kChildKnobOffsetPx = 15;
    static constexpr qreal kSiblingKnobOffsetPx = 17;
    static constexpr qreal kKnobSizePx = 18;
    static constexpr qreal kCollapseKnobGapPx = 4;
    // NodeItem's collapse control at zoom 1, scaled by the zoom (as the knobs): the open
    // circle on the branch junction, the closed pill 4 px from the shape, at least 18 px wide.
    static constexpr qreal kCollapseOpenSizePx = 11;
    static constexpr qreal kCollapseOpenOffsetPx = 12;
    static constexpr qreal kCollapseSizePx = 18;
    static constexpr qreal kCollapseGapPx = 4;
    static constexpr int kCollapseCountPx = 12;  // the count's font size
    static constexpr qreal kKnobTargetPx = 48;  // ui-mobile.md, "Touch targets"
    // B2: the desktop's scrolling near the view's edges while dragging (MindMapView, FEAT-033):
    // the zone, the step at its inner end and at the edge or beyond, every interval.
    static constexpr qreal kAutoScrollMarginPx = 24;
    static constexpr qreal kAutoScrollMinStepPx = 4;
    static constexpr qreal kAutoScrollMaxStepPx = 40;
    static constexpr int kAutoScrollIntervalMs = 16;
    // FEAT-106: NodeItem's picture drag: the zones' veil (alpha of the selection colour, the zone
    // under the finger stronger), outlines at 1 px, the dragged picture's place at 55% (NodeItem).
    static constexpr int kZoneAlpha = 36;
    static constexpr int kActiveZoneAlpha = 110;
    static constexpr qreal kPictureOutlinePx = 1;
    static constexpr int kDraggedPictureAlpha = 140;

    explicit MapCanvas(QQuickItem *parent = nullptr);

    MobileMap *map() const { return m_map; }
    void setMap(MobileMap *map);
    qreal zoom() const { return m_zoom; }
    bool benchmarkRunning() const { return m_benchmarkStep >= 0; }
    QString frameReport() const { return m_frameReport; }
    qreal bandTop() const { return m_bandTop; }
    void setBandTop(qreal top);
    qreal bandBottom() const { return m_bandBottom; }
    void setBandBottom(qreal bottom);
    bool editing() const { return m_editing; }
    QString labelHighlight() const { return m_labelHighlight; }
    void setEditing(bool editing);
    QPointF editorAnchor() const;
    qreal editorWidth() const;
    int editorAlignment() const;
    QFont editorFont() const;
    QColor editorColor() const;
    QColor editorBackdrop() const;
    static qreal tabHeight() { return Boundaries::kTitleHeightPx; }
    static qreal tabPadding() { return Boundaries::kTitlePaddingPx; }
    bool moving() const { return !m_movingId.isEmpty() || !m_pictureId.isEmpty() || m_bentHandle != 0 || m_resizedCorner != 0; }
    QPointF childKnob() const { return knob(true).centre; }
    QRectF childKnobTarget() const { return knob(true).target; }
    QPointF siblingKnob() const { return knob(false).centre; }
    QRectF siblingKnobTarget() const { return knob(false).target; }
    QRectF collapseControl() const { return controls().collapse.rect; }
    QRectF collapseTarget() const { return controls().collapse.target; }
    QPointF collapseEdge() const { return controls().collapse.edge; }
    int collapseCount() const { return controls().collapse.hidden; }
    QColor collapseColor() const { return controls().collapse.color; }
    QColor mapBackground() const { return m_map && m_map->snapshot() ? m_map->snapshot()->background() : QColor(Qt::white); }

    // The map point shown at an item point and back (item px, map px).
    QPointF mapPoint(const QPointF &itemPoint) const { return m_origin + itemPoint / m_zoom; }
    QPointF itemPoint(const QPointF &mapPoint) const { return (mapPoint - m_origin) * m_zoom; }

    Q_INVOKABLE void fit();
    // The desktop's zoom keys (MainWindow::zoomShownView) around the canvas's centre: deltaPercent
    // points more or less, 0 = 100%.
    Q_INVOKABLE void zoomStep(int deltaPercent);
    // Pan by an item-px delta (the finger's move).
    Q_INVOKABLE void panBy(qreal dx, qreal dy);
    // Zoom by factor around an item point, which stays under the fingers.
    Q_INVOKABLE void zoomBy(qreal factor, qreal centreX, qreal centreY);
    // Selects the topic at an item point (none on the background); while the format painter is
    // armed, pastes on it instead (MobileMap::paintAt). B3: on a topic's note icon, or within
    // kKnobTargetPx around its centre inside the topic but off the title and the picture, it also
    // asks for the note (noteRequested).
    // FEAT-106: on a marker (the same target, the nearest marker's centre) it asks for its menu
    // (markerRequested); a tap on the topic's picture selects it too (MobileMap::selectAt).
    // FEAT-108: the same on the checkbox (ticked, MobileMap::toggleTaskDone) and on the web link's
    // button (webLinkRequested); among overlapping targets the nearest icon wins.
    // FEAT-110: off the topics a tap selects the relationship within MapSnapshot::kRelationTouchPx
    // of its line; while the Relationship tool is on it creates one instead (MobileMap::relateAt).
    // FEAT-111: in the multiple selection mode it only adds or removes a topic (MobileMap::multiSelectAt).
    // FEAT-106: on a handle of the selected picture it keeps the selection.
    // FEAT-114: on a tag pill it selects the topic and highlights the topics with that tag (the
    // desktop's Highlight related topics); any other tap ends the highlight first, as the desktop's
    // click on the map. On the paper clip (the icons' target) it asks for its file list
    // (filesRequested), on the chain to follow its link (topicLinkRequested).
    Q_INVOKABLE void tapAt(qreal x, qreal y);
    // FEAT-114: a long press on a tag pill selects its topic and asks for the pill's menu
    // (labelMenuRequested) instead of picking the topic up; false elsewhere. It ends the highlight.
    Q_INVOKABLE bool labelMenuAt(qreal x, qreal y);
    Q_INVOKABLE void clearLabelHighlight() { setLabelHighlight(QString()); }
    // FEAT-106: a drag starting on the selected picture moves it (moveTo, endMove) over the four
    // zones of its topic; false (the map pans) elsewhere or with no picture selected.
    Q_INVOKABLE bool beginPictureDrag(qreal x, qreal y);
    // FEAT-106 (NodeItem's handles, FEAT-037): a drag starting within MapSnapshot::kRelationTouchPx
    // of a corner of the selected picture resizes it with its proportions (moveTo, endMove: one Undo
    // step); false elsewhere.
    Q_INVOKABLE bool beginPictureResize(qreal x, qreal y);
    // FEAT-110: a drag starting within MapSnapshot::kRelationTouchPx of a handle of the selected
    // relationship (curved or elbow) bends it (moveTo, endMove: one Undo step); false elsewhere.
    Q_INVOKABLE bool beginRelationBend(qreal x, qreal y);
    // B2. Picks up the topic at an item point after a long press and selects it; false (nothing
    // picked up) on the background or on the central topic, which is only selected. On the
    // selected picture it picks up the picture instead (FEAT-106).
    Q_INVOKABLE bool beginMove(qreal x, qreal y);
    // The finger at an item point: the copy follows it and the slot shows where it would go.
    Q_INVOKABLE void moveTo(qreal x, qreal y);
    // The finger lifted (drop = true: the topic goes to the slot, if any) or the move given up.
    Q_INVOKABLE void endMove(bool drop);
    // The scroll step per interval for the finger at an item point (item px, positive towards the
    // right and the bottom; 0 away from the edges of the band).
    QPointF autoScrollStep(const QPointF &itemPoint) const;
    // Pans just enough to show a map rect with kFitMarginPx around it, as the desktop's reveal.
    void reveal(const QRectF &mapRect);
    // While a title is edited: the selected topic in the middle of the band, at least kEditZoom so
    // the text can be read, smaller only if the topic would not fit the band.
    Q_INVOKABLE void centerSelected();
    // Pans just enough to show the selected topic (a selection moved by a key, as the desktop's
    // ensureVisible after an arrow).
    Q_INVOKABLE void revealSelected();
    // Reproducible measurement: kBenchmarkFrames frames of pan and pinch zoom on a fixed path, one
    // step per frame shown; the result goes to frameReport and the log.
    Q_INVOKABLE void runBenchmark();

signals:
    void mapChanged();
    void viewChanged();
    void benchmarkChanged();
    void bandChanged();
    void editingChanged();
    void editorChanged();
    void movingChanged();
    // B3: the selected topic's note icon was tapped.
    void noteRequested();
    // FEAT-108: the selected topic's web link button was tapped (MobileMap::openWebLink).
    void webLinkRequested();
    // FEAT-114: the selected topic's chain (its link to another topic) was tapped
    // (MobileMap::followTopicLink).
    void topicLinkRequested();
    void labelHighlightChanged();
    // FEAT-114: a tag pill was long pressed; x, y: the item point below its middle, where its menu
    // opens.
    void labelMenuRequested(qreal x, qreal y);
    // FEAT-114: the selected topic's paper clip was tapped; x, y: the item point below its middle,
    // where the file list opens.
    void filesRequested(qreal x, qreal y);
    // FEAT-106: the selected topic's marker was tapped; x, y: the item point below its middle,
    // where its menu opens.
    void markerRequested(const QString &marker, qreal x, qreal y);

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    struct Knob { QPointF centre; QRectF target; };
    struct Collapse {
        QRectF rect;
        QRectF target;
        QPointF edge;     // where the line to a closed branch's pill leaves the topic
        int hidden = 0;   // hidden topics, 0 = open
        QColor color;
        bool vertical = false;
        int side = 0;     // -1 left (above), +1 right (below)
    };
    struct Controls { Knob child, sibling; Collapse collapse; };
    // The selected topic's knobs and collapse control, as TopicTools::update and NodeItem place them.
    Controls controls() const;
    Knob knob(bool child) const { const Controls c = controls(); return child ? c.child : c.sibling; }
    void onSnapshotChanged();
    void onContentChanged();
    void setLabelHighlight(const QString &label);
    // FEAT-114: the tag of the pill at a map point, on the topic it belongs to; empty off the pills.
    QString labelAt(const QPointF &mapPoint, const MapSnapshot::Topic **topic = nullptr) const;
    // FEAT-110: what the editor and the view follow: the selected topic's bounds, or the selected
    // relationship's label box (the placeholder's while it has no label), or (FEAT-115) the
    // selected boundary's frame; empty with none.
    QRectF selectedBounds() const;
    QRectF relationLabelBox(const MapSnapshot::Relation &relation) const;
    // FEAT-115: a boundary's title tab, the placeholder's while it has no title.
    QRectF boundaryTab(const MapSnapshot::BoundaryShape &shape) const;
    // FEAT-110: the selected relationship's handle at an item point: 1 the start's, 2 the target's,
    // 0 none (also for a straight line, which has no handles).
    int relationHandleAt(const QPointF &point) const;
    // FEAT-106: the selected picture's corner at an item point, as NodeItem::imageHandleAt: 1 top
    // left, 2 top right, 3 bottom right, 4 bottom left; 0 none.
    int pictureHandleAt(const QPointF &point) const;
    // FEAT-106: the selected picture's rect as drawn (map px): its new size around its centre while
    // a handle moves.
    QRectF shownPictureRect(const MapSnapshot::Topic &topic) const;
    // The selected relationship as drawn: bent by the handle under the finger while it moves.
    MapSnapshot::Relation shownRelation(const MapSnapshot::Relation &relation) const;
    void benchmarkStep();
    void autoScrollTick();
    qreal minZoom() const;
    qreal fitZoom() const;
    // The band in item px, the item's height for an unset bottom.
    qreal visibleBottom() const { return m_bandBottom > 0 ? std::min<qreal>(m_bandBottom, height()) : height(); }

    QPointer<MobileMap> m_map;
    QPointF m_origin;   // map px at the item's top-left corner
    qreal m_zoom = 1;
    bool m_fitPending = true;  // fit once the item has a size and the map a sheet
    bool m_sceneDirty = true;  // the sheet changed: the nodes are built again
    bool m_selectionDirty = true;  // the selection ring is made again
    qreal m_bandTop = 0;
    qreal m_bandBottom = 0;
    bool m_editing = false;
    QString m_labelHighlight;  // FEAT-114
    int m_vertexCount = 0;     // of the built sheet, for the report
    // B2: the topic being moved (MindNode::id; empty = none), the finger (item px), the map px from
    // the finger to the topic's origin when it was picked up, where it would go now.
    QString m_movingId;
    QPointF m_moveItemPoint;
    QPointF m_grabOffset;
    ReorderTarget m_moveTarget;
    QString m_pictureId;  // FEAT-106: the topic whose picture is dragged; empty = none
    // FEAT-110: the selected relationship's handle being dragged (relationHandleAt; 0 = none) and
    // where it is now (map px).
    int m_bentHandle = 0;
    QPointF m_bentControl;
    // FEAT-106: the picture's corner being dragged (pictureHandleAt; 0 = none), the picture's size
    // when it started and now (map px).
    int m_resizedCorner = 0;
    QSizeF m_resizeStartSize;
    QSizeF m_resizeSize;
    bool m_ghostDirty = false;      // the dragged copy is built again
    bool m_indicatorDirty = false;  // the slot and its branch are made again
    QTimer m_autoScrollTimer;
    // Benchmark: -1 = not running.
    int m_benchmarkStep = -1;
    QElapsedTimer m_frameClock;
    QMutex m_swapMutex;            // m_swapTimesNs is written on the render thread
    QList<qint64> m_swapTimesNs;   // m_frameClock at each frame shown
    QList<qint64> m_updateUs;      // updatePaintNode, on the render thread with this thread blocked
    QString m_frameReport;
    QMetaObject::Connection m_swapConnection;
    // Advanced once per frame by Qt Quick's animation driver, which keeps frames coming while it runs.
    class StepAnimation : public QAbstractAnimation {
    public:
        explicit StepAnimation(MapCanvas *canvas) : m_canvas(canvas) {}
        int duration() const override { return -1; }

    protected:
        void updateCurrentTime(int) override { m_canvas->benchmarkStep(); }

    private:
        MapCanvas *m_canvas;
    };
    StepAnimation m_stepAnimation{this};
};
