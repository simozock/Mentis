#pragma once

// FEAT-070 (docs/features/FEAT-070-editor-svg.md): the sticker editor without its window, shared by
// the desktop's StickerEditorDialog (Qt Widgets) and the phone's MobileStickerEditor (Qt Quick).
// It holds the edited SvgDocument, its history (whole file per step) and the preview's StickerCanvas,
// and does what the editor's controls do: recolour, turn, flip, crop, the label, the style of new and
// selected shapes. The controls' values that live before there is a label (its font, size, colour,
// weight) or a shape (the style) are kept here, so both front ends start new ones alike.

#include "stickercanvas.h"
#include "svgdocument.h"

#include <QByteArray>
#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QTimer;

class StickerEdit {
public:
    StickerEdit();
    ~StickerEdit();
    StickerEdit(const StickerEdit &) = delete;
    StickerEdit &operator=(const StickerEdit &) = delete;

    // Reads the sticker; false when it is not an SVG this app can draw (then nothing is offered).
    bool load(const QString &path);
    bool isValid() const { return m_valid; }
    bool isModified() const { return m_history.value(m_step) != m_history.value(0); }
    bool canUndo() const { return m_step > 0; }
    bool canRedo() const { return m_step + 1 < m_history.size(); }
    QByteArray svg() const { return m_document.data(); }
    const SvgDocument &document() const { return m_document; }
    QList<QColor> colors() const { return m_document.colors(); }
    StickerCanvas &canvas() { return m_canvas; }
    const StickerCanvas &canvas() const { return m_canvas; }

    // The file changed (a step, Undo, Redo): the swatches and the controls follow it.
    std::function<void()> changed;
    // The style of new shapes changed (one shape selected shows its own).
    std::function<void()> styleChanged;
    // The selection changed.
    std::function<void()> selectionChanged;

    void setColor(int index, const QColor &color);  // a colour of the file, replaced everywhere
    void replaceColor(const QColor &from, const QColor &to);
    void undo();
    void redo();

    // Stage 2: the frame, turns and flips.
    void rotateBy(qreal degrees);
    void setRotation(qreal degrees);  // the angle field: its steps are one change
    void flipHorizontally();
    void flipVertically();
    void setFrame(const QRectF &viewBox);
    void fitToDrawing();
    void addMargin(qreal percent);  // a share of the longer side, on every side

    // The label: the text (empty removes it) and its look. Without a text the look is kept for the
    // label that a text will make, in the middle of the frame.
    struct LabelLook {
        QString family;
        qreal sizePercent = 25;  // of the longer side
        QColor color = Qt::black;
        bool bold = false;
    };
    QString labelText() const { return m_document.label().text; }
    LabelLook labelLook() const { return m_labelLook; }
    // The look a new label takes, without writing it (the controls' values, read when it is made).
    void setLabelLook(const LabelLook &look) { m_labelLook = look; }
    void setLabelText(const QString &text);
    // Typing: on a large sticker the text waits for a pause to become one step (each step rewrites
    // the whole file); settle() writes it before any other change.
    void typeLabelText(const QString &text);
    void setLabelPosition(const QPointF &position);
    void setLabelFamily(const QString &family);
    void setLabelSizePercent(qreal percent);
    void setLabelColor(const QColor &color);
    void setLabelBold(bool bold);

    // Stages 3 and 4: the style of new shapes and of the selected ones (outline width in px of the
    // sticker at 256 px). One selected shape shows its own.
    SvgDocument::ShapeStyle style() const { return m_style; }
    QColor lastFill() const { return m_lastFill; }  // back when the fill is set to Color again
    void setFillNone(bool none);
    void setFillColor(const QColor &color);
    void setStrokeColor(const QColor &color);
    void setStrokeWidth(qreal width);
    // Rounded corners in px of the sticker at 256 px, the polygon's sides, the star's points.
    void setToolOptions(qreal cornerPx, int sides, int points);
    void removeShapes(const QStringList &ids);
    void reorderShapes(SvgDocument::Order order);  // the selected shapes

    // Writes what waits for a pause (arrow moves, typing, a pen path) before another change, Undo or saving.
    void settle();

    // The bundled fonts a label can use (NodeGeometry::loadBundledFonts), so the sticker looks the
    // same everywhere in the app.
    static QStringList labelFamilies();
    // A name a file can hold on Windows, macOS and Linux.
    static QString cleanName(const QString &name);

    static constexpr int kTypingPauseMs = 400;
    static constexpr qsizetype kLargeBytes = 256 * 1024;      // from here typing waits for a pause
    static constexpr qsizetype kHistoryBytes = 64 * 1024 * 1024;

private:
    // One step of the history; steps with the same non-empty key in a row merge (typing, a field).
    void commit(const QString &key = QString());
    void apply();
    void applyLabel(const std::function<void(SvgDocument::Label &)> &change, const QString &key);
    void readLabelLook();
    void addShape(const QList<SvgDocument::SubPath> &path);
    void writeStyle(const SvgDocument::ShapeStyle &style, int parts);
    void flushText();
    qreal unitsPerPx() const;  // viewBox units per px of the sticker at 256 px

    SvgDocument m_document;
    bool m_valid = false;
    QList<QByteArray> m_history;  // whole file per step: stickers are small (2 MB at most)
    int m_step = 0;
    QString m_lastKey;
    StickerCanvas m_canvas;
    SvgDocument::ShapeStyle m_style{QColor(QStringLiteral("#4a90d9")), QColor(), 0};  // stroke width in px at 256 px
    QColor m_lastFill;
    LabelLook m_labelLook;
    qreal m_cornerPx = 0;
    int m_sides = 5;
    int m_points = 5;
    std::unique_ptr<QTimer> m_textTimer;
    QString m_pendingText;
    bool m_textPending = false;
};
