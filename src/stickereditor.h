#pragma once

// FEAT-070 (docs/features/FEAT-070-editor-svg.md): the modal that edits an SVG sticker.
// Stage 1: preview on a checkerboard, one swatch per colour of the file (the app's colour picker
// replaces it everywhere), Undo/Redo, the new sticker's name and collection.
// Stage 2: the frame (edges dragged on the preview, fit to the drawing, a margin), rotation (quarter
// turns and any angle), flips, one text label dragged on the preview. Stages 3 and 4: shapes, points
// and drawing tools. What the controls do is StickerEdit's (stickeredit.h), shared with the phone.
// The modal saves nothing: whoever opens it reads svg(), stickerName() and the collection after
// exec() == Accepted.

#include "iconlibrary.h"
#include "modaldialog.h"
#include "stickeredit.h"

#include <QList>
#include <QWidget>
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

    bool isValid() const { return m_edit.isValid(); }  // false: not an SVG this app can draw
    bool isModified() const { return m_edit.isModified(); }
    QByteArray svg() const { return m_edit.svg(); }
    const SvgDocument &document() const { return m_edit.document(); }
    QList<QColor> colors() const { return m_edit.colors(); }
    QToolButton *swatch(int index) const { return m_swatches.value(index); }
    StickerPreview *preview() const { return m_preview; }
    // A colour of the file replaced, as a choice in the picker (also for tests).
    void setColor(int index, const QColor &color) { m_edit.setColor(index, color); }
    void undo() { m_edit.undo(); }
    void redo() { m_edit.redo(); }

    // Stage 2 actions, as the buttons do them (also for tests).
    void rotateBy(qreal degrees) { m_edit.rotateBy(degrees); }
    void setRotation(qreal degrees) { m_edit.setRotation(degrees); }
    void flipHorizontally() { m_edit.flipHorizontally(); }
    void flipVertically() { m_edit.flipVertically(); }
    void setFrame(const QRectF &viewBox) { m_edit.setFrame(viewBox); }
    void fitToDrawing() { m_edit.fitToDrawing(); }
    void addMargin();  // the margin field's percentage of the longer side, on every side
    void setLabelText(const QString &text);
    void setLabelPosition(const QPointF &position) { m_edit.setLabelPosition(position); }
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
    void syncStyle();
    void readLabelControls();  // a new label takes the controls' look as they are now
    void updateToolOptions();
    void rebuildSwatches();
    void syncControls();
    void updateState();

    StickerEdit m_edit;
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
    SegmentedControl *m_fillKind = nullptr;  // Color / None
    ColorField *m_fillColor = nullptr;
    ColorField *m_strokeColor = nullptr;
    NumberField *m_strokeWidth = nullptr;
    NumberField *m_angle = nullptr;
    NumberField *m_margin = nullptr;
    QLineEdit *m_labelText = nullptr;
    QComboBox *m_labelFont = nullptr;
    NumberField *m_labelSize = nullptr;
    ColorField *m_labelColor = nullptr;
    QToolButton *m_labelBold = nullptr;
    QLineEdit *m_name = nullptr;
    QComboBox *m_collection = nullptr;
    QLineEdit *m_newCollection = nullptr;
    QLabel *m_newCollectionLabel = nullptr;
};

// The preview on the desktop: the editor's StickerCanvas (gestures, drawing) in a widget, driven by
// the mouse and the keys (stickercanvas.h describes the modes).
class StickerPreview final : public QWidget {
public:
    using Mode = StickerCanvas::Mode;
    static bool draws(Mode mode) { return StickerCanvas::draws(mode); }

    explicit StickerPreview(StickerCanvas *canvas, QWidget *parent = nullptr);
    void setMode(Mode mode) { m_canvas->setMode(mode); }
    Mode mode() const { return m_canvas->mode(); }
    QStringList selection() const { return m_canvas->selection(); }
    void setSelection(const QStringList &ids) { m_canvas->setSelection(ids); }
    QString editedShape() const { return m_canvas->editedShape(); }
    bool penActive() const { return m_canvas->penActive(); }
    // Widget <-> viewBox units, for the area shown now (also for tests).
    QPointF toWidget(const QPointF &point) const { return m_canvas->toView(point); }
    QPointF toDocument(const QPointF &point) const { return m_canvas->toDocument(point); }

    static constexpr int kSizePx = 360;  // as wide as the tools above it

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private:
    StickerCanvas *m_canvas;
};
