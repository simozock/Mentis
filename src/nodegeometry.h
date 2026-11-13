#pragma once

#include "formularendering.h"

#include <QFont>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTextLayout>
#include <QTextDocument>

#include <functional>
#include <memory>
#include <optional>

struct MindNode;
class QPainter;

// FEAT-096: a topic's measures without the scene and its items. Text layout of the title (plain,
// formatted or with formulas), content (image, formula, buttons, note shown inside, labels below)
// and shape, read by the map layout and drawn by NodeItem. Qt Gui only: no QWidget, no QGraphicsItem.
// The values and rules are those NodeItem had (moved unchanged, see the regression test
// tests/layout_regression_test.cpp).
class NodeGeometry {
public:
    // Formulas through FormulaRendering's hooks (the desktop: JKQTMathText).
    using TitleFormula = FormulaRendering::TitleFormula;

    static constexpr qreal kImageGapPx = 8;          // between image and text
    static constexpr qreal kLinkButtonPx = 22;       // a button of the side columns (square)
    static constexpr qreal kLinkIconGapPx = 4;       // between two buttons
    static constexpr qreal kLinkTextGapPx = 8;       // between the content and the first button
    static constexpr qreal kMaxFormulaNodeWidth = 440;  // a topic with formulas wraps beyond
    // Text topics: as wide as the title, wrapped beyond. BUG-003 (2026-10-15): no fixed minimum size;
    // only an empty title stays as wide as one character (minContentWidth).
    static constexpr qreal kMaxTextNodeWidth = 280;
    static constexpr qreal kMaxCentralNodeWidth = 330;  // FEAT-072: measured live 2026-10-23: 329
    static constexpr qreal kLabelHeightPx = 20;         // FEAT-060: a label pill
    static constexpr qreal kLabelPaddingPx = 8;
    static constexpr qreal kNotePreviewPaddingPx = 8;   // FEAT-059: inside the shown note's box
    static QFont labelFont();

    explicit NodeGeometry(const MindNode *node);
    ~NodeGeometry();
    NodeGeometry(const NodeGeometry &) = delete;
    NodeGeometry &operator=(const NodeGeometry &) = delete;

    // Called before a measure changes (NodeItem: prepareGeometryChange, which Qt wants first).
    std::function<void()> beforeChange;

    // Title font on the map (FEAT-022) and the fonts bundled with the app.
    static QFont titleFont(const MindNode *node);
    static Qt::Alignment textAlignment(const MindNode *node);
    static bool isFontInstalled(const QString &family);
    static void loadBundledFonts();
    static void loadInterfaceFont();
    // FEAT-059: "Show all notes" of the active sheet.
    static bool &showAllNotes();
    // FEAT-020: how far the border (or the freehand stroke, the pattern tips) sticks out of the shape.
    static qreal borderOutside(const MindNode *node);
    // FEAT-037/040: an image's insertion size, the file's pixels (with the EXIF rotation) scaled down
    // in proportion beyond kMaxImageWidthPx; 180x120 when the file cannot be read.
    static constexpr qreal kMaxImageWidthPx = 3000;
    static constexpr qreal kMinImageWidthPx = 24;  // resizing with the handles (FEAT-037, FEAT-106)
    static QSizeF insertionImageSize(const QString &path);
    // User's request 2026-10-20: a new image (not a sticker, which stays small) is at most as wide
    // as the topic's room (imageRoomWidth), but at least kMinInsertWidthPx, in proportion; a smaller
    // one keeps its size. Shared by the desktop and the mobile app (FEAT-097).
    static constexpr qreal kMinInsertWidthPx = 200;
    static QSizeF fittedImageSize(const QString &path, qreal roomWidth);
    // Plain title layout, the same steps as qt_format_text() (P5); layout may be null for the size only.
    static QSizeF plainTextLayout(const QString &plain, const QFont &font, qreal width, QTextLayout *layout,
                                  Qt::Alignment alignment = Qt::AlignLeft);

    // P2: data the measures depend on. TextLayoutKey: the title's text layout (updateText); the rest
    // only moves the box around the text.
    struct TextLayoutKey {
        QString title, titleHtml;
        QRgb formulaColor = 0;  // the text colour, only for titles with formulas
        bool isRoot = false;
        qreal customWidth = 0;
        bool strikeOut = false;   // FEAT-020: the formatted text is laid out again with the strike-out
        // FEAT-022: font, size, weight, italic, underline, case and alignment.
        QString fontFamily;
        qreal fontSize = 0;
        int fontWeight = 0, textCase = 0, textAlign = 0;
        bool italic = false, underline = false;
        int linkCount = 0;  // FEAT-027: the buttons take room from the text, which wraps earlier
        qreal sideImageWidth = 0;  // FEAT-037: image left or right of the text
        int task = 0;              // FEAT-048/051: task box and markers take room from the text
        bool operator==(const TextLayoutKey &) const = default;
    };
    struct GeometryKey {
        TextLayoutKey text;
        QString imagePath;
        QSizeF imageSize;
        int imagePlacement = 0;
        qreal borderOutside = 1;  // FEAT-020: a thick border widens the ring's area
        int shape = 0;            // FEAT-020 part B: the shape changes the measure
        qreal borderWidth = 0;    // BUG-012: with lines below, the second line goes one width down
        bool notesShown = false;  // FEAT-059: note shown inside the topic
        QString notes, notesHtml;
        QStringList labels;  // FEAT-060
        QString formula;     // FEAT-064: formula content in the image's place
        bool operator==(const GeometryKey &) const = default;
    };
    GeometryKey geometryKey() const;

    // Measuring steps, in NodeItem::syncFromNode's order.
    void updateText();         // title text layout (only when TextLayoutKey changed)
    void setHasImage(bool hasImage) { m_hasImage = hasImage; }  // B9: from the data, pixels loaded later
    void updateNotePreview(const QFont &baseFont);  // FEAT-059
    void updateLabelLayout();  // FEAT-060
    // FEAT-043 Justify: box width the layout gives (0 = none); acts like a hand width.
    void setJustifiedWidth(qreal width) { m_justifiedWidth = width; }
    qreal justifiedWidth() const { return m_justifiedWidth; }
    // Size of the title field while it is edited (nullopt = not edited): the box follows the text typed.
    void setEditingSize(std::optional<QSizeF> size) { m_editingSize = size; }

    bool hasImage() const { return m_hasImage; }
    QString shownTitle() const;
    void prependNumber(QTextDocument &document) const;
    qreal layoutCustomWidth() const;  // width set by hand or by Justify, the larger

    // FEAT-037: content laid out from the measures already made (arithmetic only). box = the whole
    // topic, text = the text block with its padding, image, content = image and text without the
    // note, note = the note shown inside (FEAT-059).
    struct ContentLayout { QRectF box, text, image, content, note; };
    ContentLayout contentLayout(bool withNote = true) const;
    QRectF textRect() const { return contentLayout().box; }
    QRectF textArea() const { return contentLayout().text; }
    QRectF imageRect() const;
    QRectF shapeRect() const;
    QPointF branchAnchor(bool right) const;
    qreal anchorHalfWidth() const;
    qreal layoutHeight() const;
    QRectF normalBoundingRect() const;  // shape and image with 4 px of margin
    qreal cornerRadius() const;
    QList<QRectF> labelRects() const;
    qreal labelsBlockHeight() const;
    QList<QRectF> markerRects() const;
    // FEAT-037: where a dragged picture can drop, in MindNode::ImagePlacement's order (Above, Below,
    // Left, Right), inside the topic; imageDropZoneAt: the zone at a point, -1 = none. Both front ends.
    QList<QRectF> imageDropZones() const;
    int imageDropZoneAt(const QPointF &position) const;
    QRectF taskRect() const;
    QRectF linkIconRect(bool topic) const;  // empty without that link
    QRectF noteRect() const;
    QRectF filesRect() const;
    QRectF notePreviewRect() const { return m_notePreview ? contentLayout().note : QRectF(); }
    qreal minimumCustomWidth() const;
    qreal imageRoomWidth() const;
    bool hasSideImage() const;
    qreal linkZoneWidth() const;
    qreal taskZoneWidth() const;
    qreal manualTextWidth() const;
    qreal textBlockWidth(qreal content, qreal maxWidth) const;
    qreal plainWrapWidth(qreal boxWidth) const;
    qreal maxTextNodeWidth() const;
    qreal padX() const;
    qreal padY() const;

    // What NodeItem draws.
    const TitleFormula *formula() const { return m_formula.get(); }
    QTextDocument *richDocument() const { return m_richDocument.get(); }
    const QTextLayout *plainLayout() const { return m_plainLayout.get(); }
    QSizeF plainLayoutSize() const { return m_plainLayoutSize; }
    qreal plainLayoutFontHeight() const { return m_plainLayoutFontHeight; }
    // Changes with every new plain layout (the painter's metrics check is then made again).
    int plainLayoutGeneration() const { return m_plainLayoutGeneration; }
    QTextDocument *notePreview() const { return m_notePreview.get(); }

private:
    void changing() const { if (beforeChange) beforeChange(); }
    void updatePlainLayout(const QFont &font, qreal fontHeight);

    const MindNode *m_node;
    bool m_hasImage = false;
    qreal m_justifiedWidth = 0;
    std::optional<QSizeF> m_editingSize;
    std::unique_ptr<TitleFormula> m_formula;  // titles with $...$
    // Formatted titles, laid out when the data change, not in paint().
    std::unique_ptr<QTextDocument> m_richDocument;
    QSizeF m_textSize;            // plain title measured in updateText(), not at every paint()
    qreal m_minContentWidth = 0;  // BUG-003: one average character
    // P5 (2026-10-13): plain title already laid out (wraps, centring); empty for formulas and formatted titles.
    std::unique_ptr<QTextLayout> m_plainLayout;
    QSizeF m_plainLayoutSize;
    qreal m_plainLayoutFontHeight = 0;
    int m_plainLayoutGeneration = 0;
    struct LabelLayout {
        QStringList labels;
        qreal width = 0;           // width they wrap at
        QList<QRectF> pills;       // from the block's top-left corner
        QSizeF size;
        bool operator==(const LabelLayout &other) const { return labels == other.labels && width == other.width; }
    };
    LabelLayout m_labelLayout;
    struct NotePreviewKey {
        bool shown = false;
        QString notes, notesHtml;
        qreal width = 0;
        QRgb color = 0;  // FEAT-064: the note's formulas take the text colour
        bool operator==(const NotePreviewKey &) const = default;
    };
    NotePreviewKey m_notePreviewKey;
    std::unique_ptr<QTextDocument> m_notePreview;
    qreal m_notePreviewHeight = 0;
};
