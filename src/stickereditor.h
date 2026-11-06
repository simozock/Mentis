#pragma once

// FEAT-070 (docs/features/FEAT-070-editor-svg.md): the modal that edits an SVG sticker.
// Stage 1: preview on a checkerboard, one swatch per colour of the file (the app's colour picker
// replaces it everywhere), Undo/Redo, the new sticker's name and collection.
// Stage 2: the frame (edges dragged on the preview, fit to the drawing, a margin), rotation (quarter
// turns and any angle), flips, one text label dragged on the preview.
// The modal saves nothing: whoever opens it reads svg(), stickerName() and the collection after
// exec() == Accepted.

#include "iconlibrary.h"
#include "modaldialog.h"
#include "svgdocument.h"

#include <QList>
#include <functional>

class ColorField;
class NumberField;
class QComboBox;
class QGridLayout;
class QIcon;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class SegmentedControl;
class StickerPreview;

// Layout (user, 2026-11-01: as the rest of the app): the tools in their own section above the
// preview, large; on the right three pages (Style, Frame, Text) of label / control rows as the Export
// dialog, with the app's components (NumberField, ColorField, SegmentedControl, the dark select,
// buttons with an icon); then where the copy is saved.
class StickerEditorDialog final : public ModalDialog {
    Q_OBJECT
public:
    // collections: where the copy can go (the visible ones), collectionId the original's.
    StickerEditorDialog(QWidget *parent, const QString &path, const QList<IconLibrary::Collection> &collections,
                        const QString &collectionId);

    bool isValid() const { return m_valid; }  // false: not an SVG this app can draw
    bool isModified() const { return m_history.value(m_step) != m_history.value(0); }
    QByteArray svg() const { return m_document.data(); }
    const SvgDocument &document() const { return m_document; }
    QList<QColor> colors() const { return m_document.colors(); }
    QToolButton *swatch(int index) const { return m_swatches.value(index); }
    StickerPreview *preview() const { return m_preview; }
    // A colour of the file replaced, as a choice in the picker (also for tests).
    void setColor(int index, const QColor &color);
    void undo();
    void redo();

    // Stage 2 actions, as the buttons do them (also for tests).
    void rotateBy(qreal degrees);
    void setRotation(qreal degrees);
    void flipHorizontally();
    void flipVertically();
    void setFrame(const QRectF &viewBox);
    void fitToDrawing();
    void addMargin();  // the margin field's percentage of the longer side, on every side
    void setLabelText(const QString &text);
    void setLabelPosition(const QPointF &position);
    NumberField *marginField() const { return m_margin; }
    QLineEdit *labelField() const { return m_labelText; }
    QComboBox *labelFontSelect() const { return m_labelFont; }
    NumberField *labelSizeField() const { return m_labelSize; }
    QToolButton *labelBoldButton() const { return m_labelBold; }

    QString stickerName() const;
    QString collectionId() const;         // empty: a new collection, named newCollectionName()
    QString newCollectionName() const;
    QLineEdit *nameField() const { return m_name; }
    QComboBox *collectionSelect() const { return m_collection; }
    QLineEdit *newCollectionField() const { return m_newCollection; }

    // Replaceable in tests: the question before discarding changes (true = discard).
    std::function<bool()> confirmDiscard;

    static constexpr int kToolPx = 40;        // the drawing tools' buttons
    static constexpr int kIconButtonPx = 32;  // rotate, flip, order, Bold, Undo...
    static constexpr int kOptionsRowPx = 30;

public slots:
    void accept() override;
    void reject() override;

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QToolButton *iconButton(const QIcon &icon, const QString &tip, const std::function<void()> &action, bool checkable = false);
    QPushButton *textButton(const QString &text, const std::function<void()> &action);
    // Writes what waits for a pause (arrow moves, typing) before another change, Undo or saving.
    void settle();
    void flushText();
    void replaceColor(const QColor &from, const QColor &to);
    void removeShapes(const QStringList &ids);
    void addShape(const QList<SvgDocument::SubPath> &path);
    // The Style page: kept for new shapes, written on the selected ones (width in px at 256 px).
    void setShapeStyle(const SvgDocument::ShapeStyle &style, int parts);
    void syncStyle();
    void updateToolOptions();
    void reorderShapes(SvgDocument::Order order);
    // One step of the history; steps with the same non-empty key in a row merge (typing, a field).
    void commit(const QString &key = QString());
    void apply();
    void applyLabel(const std::function<void(SvgDocument::Label &)> &change, const QString &key);
    void rebuildSwatches();
    void syncControls();
    void updateState();

    SvgDocument m_document;
    bool m_valid = false;
    QList<QByteArray> m_history;  // whole file per step: stickers are small (2 MB at most)
    int m_step = 0;
    QString m_lastKey;
    StickerPreview *m_preview = nullptr;
    SegmentedControl *m_pageSelect = nullptr;
    QGridLayout *m_swatchGrid = nullptr;
    QList<QToolButton *> m_swatches;
    QToolButton *m_undo = nullptr;
    QToolButton *m_redo = nullptr;
    QList<QToolButton *> m_shapeButtons;  // order and delete: need a selection
    NumberField *m_cornerRadius = nullptr;
    NumberField *m_sides = nullptr;
    NumberField *m_points = nullptr;
    QList<QLabel *> m_optionLabels;  // beside the three fields, in their order
    QLabel *m_toolHint = nullptr;
    SvgDocument::ShapeStyle m_style{QColor(QStringLiteral("#4a90d9")), QColor(), 0};  // stroke width in px at 256 px
    QColor m_lastFill;               // back when the fill is set to Color again
    SegmentedControl *m_fillKind = nullptr;  // Color / None
    ColorField *m_fillColor = nullptr;
    ColorField *m_strokeColor = nullptr;
    NumberField *m_strokeWidth = nullptr;
    NumberField *m_angle = nullptr;
    NumberField *m_margin = nullptr;
    QLineEdit *m_labelText = nullptr;
    class QTimer *m_textTimer = nullptr;
    QString m_pendingText;
    bool m_textPending = false;
    QComboBox *m_labelFont = nullptr;
    NumberField *m_labelSize = nullptr;
    ColorField *m_labelColor = nullptr;
    QToolButton *m_labelBold = nullptr;
    QColor m_labelColorValue = Qt::black;  // the label's colour, also before there is any text
    QLineEdit *m_name = nullptr;
    QComboBox *m_collection = nullptr;
    QLineEdit *m_newCollection = nullptr;
    QLabel *m_newCollectionLabel = nullptr;
};

// The preview: the drawing with some room around the frame. Two modes:
// - Shapes (stage 3): a click selects the shape under the mouse (inside groups too), Shift+click
//   adds or removes, a drag on the empty area selects with a rectangle; the selection moves by
//   dragging, resizes by its 8 handles (Shift keeps the proportions), moves with the arrows (Shift:
//   more), Delete removes it. A double click edits the shape's points: drag points and Bézier
//   handles, click on the outline to add a point, Delete removes the chosen point, Esc or a click on
//   the empty area ends.
// - Frame (stage 2): the frame's edges and corners are dragged.
// The label is dragged in both. Every change is reported once, on release; while dragging only the
// preview moves.
class StickerPreview final : public QWidget {
public:
    // Shapes selects and edits; Frame crops; the others draw a new shape (stage 4).
    enum class Mode { Shapes, Frame, Rectangle, Ellipse, Line, Pen, Polygon, Star };
    static bool draws(Mode mode) { return mode != Mode::Shapes && mode != Mode::Frame; }

    explicit StickerPreview(QWidget *parent = nullptr);
    void setDocument(const SvgDocument &document);
    bool isValid() const;
    void setMode(Mode mode);
    Mode mode() const { return m_mode; }
    QStringList selection() const { return m_selection; }
    void setSelection(const QStringList &ids);
    QString editedShape() const { return m_pointShape; }  // the shape whose points are edited
    void editPoints(const QString &id);                  // empty: stop
    // Arrow moves wait for a pause to become one step; this writes them now (before any other change).
    void flush();

    std::function<void(const QRectF &viewBox)> frameChanged;
    std::function<void(const QPointF &position)> labelMoved;
    // nudge: an arrow key, merged with the next ones into one step.
    std::function<void(const QStringList &ids, const QTransform &matrix, bool nudge)> shapesTransformed;
    std::function<void(const QString &id, const QList<SvgDocument::SubPath> &path)> pathChanged;
    std::function<void(const QStringList &ids)> removeRequested;
    std::function<void()> selectionChanged;
    // A shape drawn with a tool, in viewBox units.
    std::function<void(const QList<SvgDocument::SubPath> &path)> shapeDrawn;
    // The tools' options: rounded corners (viewBox units), the polygon's sides, the star's points.
    void setToolOptions(qreal cornerRadius, int sides, int points);
    bool penActive() const { return m_penActive; }
    void finishPen(bool close = false);  // the pen's path as it is (two points at least)
    void cancelPen();

    // Widget <-> viewBox units, for the area shown now (also for tests).
    QPointF toWidget(const QPointF &point) const;
    QPointF toDocument(const QPointF &point) const;

    static constexpr int kSizePx = 360;  // as wide as the tools above it
    static constexpr int kHandlePx = 8;
    static constexpr int kPickPx = 4;  // how near the mouse must be to an outline, a point or a handle

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private:
    enum Edge { None = 0, Left = 1, Top = 2, Right = 4, Bottom = 8 };
    enum class Drag { None, FrameEdges, Label, Move, Resize, Rubber, Point, Draw, PenHandle };
    // A point of the edited path: its node, or one of the Bézier handles beside it.
    struct PointRef {
        int sub = -1;
        int node = -1;   // 0 = the sub-path's start, k = the end of segment k-1
        int handle = 0;  // 0 the node, -1 its incoming handle (c2 of segment node-1), +1 outgoing (c1 of segment node)
        bool valid() const { return sub >= 0; }
        bool operator==(const PointRef &) const = default;
    };
    int edgesAt(const QRectF &box, const QPointF &position) const;    // edges of box (viewBox units) under the mouse
    int handlesAt(const QRectF &box, const QPointF &position) const;  // the edges of the handle under the mouse
    bool onLabel(const QPointF &position) const;
    PointRef pointAt(const QPointF &position) const;
    QPointF pointPosition(const QList<SvgDocument::SubPath> &path, const PointRef &ref) const;  // shape coordinates
    void movePoint(QList<SvgDocument::SubPath> &path, const PointRef &ref, const QPointF &to) const;
    bool insertPointAt(const QPointF &position);
    void removeChosenPoint();
    QTransform resizeMatrix(const QPointF &at, bool keepRatio) const;
    QPainterPath toolShape(const QPointF &from, const QPointF &to, bool constrained) const;  // the drawn shape
    QPointF snapped(const QPointF &from, const QPointF &to, bool constrained) const;      // 45° with Shift
    QPainterPath penOutline(bool withRubber) const;
    void updateArea();
    void updateCursor(const QPointF &position);
    const QList<SvgDocument::Shape> &shapes();  // read on first need after each change
    void render(class QSvgRenderer *renderer, QImage *image) const;
    // Drag layers: rest drawn once, the moving elements redrawn alone (a 2 MB sticker stays smooth).
    void beginLayers(const QStringList &ids);
    void endLayers();
    void showOnly(const SvgDocument &only);

    SvgDocument m_document;
    QList<SvgDocument::Shape> m_shapes;
    bool m_shapesRead = false;
    class QSvgRenderer *m_renderer = nullptr;
    QImage m_image;  // m_document drawn for m_area: painted many times, drawn once per change
    bool m_layers = false;
    QImage m_restImage;
    SvgDocument m_only;  // the moving elements alone
    SvgDocument m_onlyLive;
    class QSvgRenderer *m_onlyRenderer = nullptr;
    QTransform m_nudge;  // arrow moves not written yet
    class QTimer *m_nudgeTimer = nullptr;
    Mode m_mode = Mode::Shapes;
    QRectF m_area;   // viewBox units shown in the widget
    QRectF m_frame;  // the viewBox, or its dragged version
    Drag m_drag = Drag::None;
    int m_edges = None;
    QPointF m_pressAt;      // viewBox units
    QPointF m_labelAt;
    QTransform m_dragMatrix;
    QRectF m_rubber;        // viewBox units
    QStringList m_selection;
    QStringList m_selectionAtPress;
    QRectF m_selectionBox;  // at the press
    QString m_pointShape;
    QList<SvgDocument::SubPath> m_path;     // the edited shape's points, in its own coordinates
    QList<SvgDocument::SubPath> m_livePath;
    QTransform m_pathMatrix;                // shape coordinates -> viewBox units
    PointRef m_chosen;
    bool m_clickedSelected = false;         // pressed on a shape already selected (Shift+click removes it on release)
    QPainterPath m_drawing;                 // the shape being drawn, viewBox units
    qreal m_cornerRadius = 0;
    int m_sides = 5;
    int m_points = 5;
    // The pen: the path so far, the outgoing handle of its last point, the mouse for the next segment.
    bool m_penActive = false;
    SvgDocument::SubPath m_pen;
    QPointF m_penOut;
    bool m_hasPenOut = false;
    QPointF m_penMouse;
};
