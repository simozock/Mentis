#include "nodegeometry.h"

#include "formulatext.h"
#include "mindmapmodel.h"
#include "nodeshape.h"
#include "nodesketch.h"
#include "nodestyle.h"
#include "numbering.h"
#include "richtitle.h"
#include "summary.h"

#include <QFontDatabase>
#include <QImageReader>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QSet>
#include <QTextCursor>
#include <QTextOption>

#include <algorithm>
#include <cmath>

namespace {
bool hasNote(const MindNode *node) { return !node->notes.trimmed().isEmpty(); }  // FEAT-059

constexpr qreal kLabelGapPx = 4;
constexpr qreal kLabelTopGapPx = 4;      // under the shape
constexpr qreal kLabelMinWidthPx = 160;  // narrowest width they wrap at
constexpr int kLabelFontPx = 12;

constexpr qreal kNoteSideMarginPx = 16;
constexpr qreal kNotePreviewMinTextPx = 128;
constexpr qreal kNotePreviewMaxTextPx = 328;  // box at most 344 px wide
constexpr int kNotePreviewMaxLines = 6;
constexpr int kNotePreviewFontPx = 12;
}  // namespace

NodeGeometry::NodeGeometry(const MindNode *node) : m_node(node) {}

QFont NodeGeometry::labelFont() {
    QFont font;
    font.setPixelSize(kLabelFontPx);
    return font;
}

NodeGeometry::~NodeGeometry() = default;

QFont NodeGeometry::titleFont(const MindNode *node) {
    QFont font = QGuiApplication::font();
    // FEAT-022: the topic's text (defaults in the cascade, see NodeStyle::kRootFontSizePt). Without a
    // chosen font the maps' default, bundled with the app; a font not installed stays the default.
    const QString family = node->fontFamily.isEmpty() ? NodeStyle::kDefaultMapFontFamily : node->fontFamily;
    font.setFamilies({isFontInstalled(family) ? family : NodeStyle::kDefaultMapFontFamily});
    font.setPointSizeF(node->fontSize);
    font.setWeight(static_cast<QFont::Weight>(node->fontWeight));
    font.setItalic(node->italic);
    font.setUnderline(node->underline);
    font.setStrikeOut(node->strikeOut);  // FEAT-020: Strike-out style, or chosen by hand (FEAT-022)
    // Case only when drawn: the title stays as written (https://doc.qt.io/qt-6/qfont.html#Capitalization-enum).
    font.setCapitalization(node->textCase == TextCase::Maiuscolo   ? QFont::AllUppercase
                           : node->textCase == TextCase::Minuscolo ? QFont::AllLowercase
                                                                   : QFont::MixedCase);
    return font;
}

// FEAT-022: the title's alignment for QTextOption and QTextDocument.
Qt::Alignment NodeGeometry::textAlignment(const MindNode *node) {
    switch (node->textAlign) {
    case TextAlign::Centro: return Qt::AlignHCenter;
    case TextAlign::Destra: return Qt::AlignRight;
    case TextAlign::Giustificato: return Qt::AlignJustify;
    case TextAlign::Sinistra: break;
    }
    return Qt::AlignLeft;
}

void NodeGeometry::loadBundledFonts() {
    // Registered once, before the font list is read.
    static const bool loaded = [] {
        // FEAT-072: the fonts of the mind map variants (all SIL OFL 1.1, see each LEGGIMI.txt).
        for (const char *path : {":/assets/fonts/roboto/Roboto[wdth,wght].ttf", ":/assets/fonts/montserrat/Montserrat[wght].ttf",
                                 ":/assets/fonts/arvo/Arvo-Regular.ttf", ":/assets/fonts/arvo/Arvo-Bold.ttf"})
            QFontDatabase::addApplicationFont(QString::fromLatin1(path));
        // Fonts for readability, offered in the font list: Atkinson Hyperlegible and OpenDyslexic (SIL OFL 1.1).
        loadInterfaceFont();
        for (const char *name : {"Regular", "Italic", "Bold", "Bold-Italic"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/assets/fonts/opendyslexic/OpenDyslexic-%1.otf").arg(QLatin1String(name)));
        // Atkinson Hyperlegible Next and Mono, every weight with its italic.
        for (const char *weight : {"ExtraLight", "Light", "Regular", "Medium", "SemiBold", "Bold", "ExtraBold"})
            for (const char *family : {"Next", "Mono"})
                for (const char *italic : {"", "Italic"}) {
                    const QString name = QLatin1String(weight) + QLatin1String(italic);
                    QFontDatabase::addApplicationFont(QStringLiteral(":/assets/fonts/atkinson-hyperlegible-%1/AtkinsonHyperlegible%2-%3.otf")
                                                          .arg(QString::fromLatin1(family).toLower(), QLatin1String(family), name));
                }
        return true;
    }();
    Q_UNUSED(loaded);
}

void NodeGeometry::loadInterfaceFont() {
    static const bool loaded = [] {
        for (const char *name : {"Regular", "Italic", "Bold", "BoldItalic"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/assets/fonts/atkinson-hyperlegible/AtkinsonHyperlegible-%1.ttf").arg(QLatin1String(name)));
        return true;
    }();
    Q_UNUSED(loaded);
}

bool NodeGeometry::isFontInstalled(const QString &family) {
    loadBundledFonts();
    // Read once: QFontDatabase::families() is costly and titleFont() runs at every measure.
    static const QSet<QString> families = [] {
        const QStringList list = QFontDatabase::families();
        return QSet<QString>(list.cbegin(), list.cend());
    }();
    return families.contains(family);
}

bool &NodeGeometry::showAllNotes() {
    static bool shown = false;
    return shown;
}

// FEAT-020: with borders up to 6 px thick the border sticks out by half its width. Without a border
// nothing sticks out: the ring is "gap" px from the shape, 1 px (measured live, FEAT-072; before 1 px
// more). The freehand stroke (part C) moves NodeSketch::kBorderWobble further: the ring and the
// topic's area keep it inside. The same for the pattern tips (NodeSketch::kPatternReach).
qreal NodeGeometry::borderOutside(const MindNode *node) {
    qreal outside = 0.0;
    if (node->borderStyle != NodeBorderStyle::Nessuno) {
        const qreal wobble = NodeSketch::isFreehand(node->borderStyle) ? NodeSketch::kBorderWobble : 0;
        // FEAT-030 stage B: round and curly brackets reach above and below the rectangle.
        outside = std::max<qreal>(1.0, NodeShapes::markOverhang(node->shape) + node->borderWidth / 2 + wobble);
    }
    // The pattern tips stick out of the shape (photo 24): they stay inside the ring too.
    if (NodeStyle::isPatternFill(node->fillStyle) && !NodeShapes::isUnderline(node->shape))
        outside = std::max(outside, NodeSketch::kPatternReach);
    return outside;
}

NodeGeometry::GeometryKey NodeGeometry::geometryKey() const {
    // The text colour counts only for formulas, already drawn in their colour by updateText(); plain
    // and formatted text take the colour in paint(). Before, every text colour change measured the
    // topic again and laid the map out (FEAT-020 part C: a pattern darkens the text of 1,000 topics).
    const QRgb formulaColor = FormulaText::containsMath(m_node->title) ? m_node->text.rgba() : 0;
    return {{shownTitle(), m_node->titleHtml, formulaColor, !m_node->parent, layoutCustomWidth(), m_node->strikeOut,
             m_node->fontFamily, m_node->fontSize, m_node->fontWeight, static_cast<int>(m_node->textCase),
             static_cast<int>(m_node->textAlign), m_node->italic, m_node->underline,
             int(!m_node->topicLink.isEmpty()) + int(!m_node->webLink.isEmpty()) + int(hasNote(m_node) && !showAllNotes())
                 + int(!m_node->files.isEmpty()),
             hasSideImage() ? m_node->imageSize.width() : 0.0, int(m_node->task != MindNode::Task::None) + int(m_node->markers.size())},
            m_node->imagePath, m_node->imageSize,
            static_cast<int>(m_node->imagePlacement), borderOutside(m_node), static_cast<int>(m_node->shape),
            m_node->borderWidth, showAllNotes() && hasNote(m_node),
            showAllNotes() ? m_node->notes : QString(), showAllNotes() ? m_node->notesHtml : QString(), m_node->labels,
            m_node->formula};
}

QString NodeGeometry::shownTitle() const { return Numbering::withNumber(m_node->number, m_node->title); }

void NodeGeometry::prependNumber(QTextDocument &document) const {
    if (m_node->number.isEmpty()) return;
    // FEAT-024: in the format of the title's first character, like the rest of its first word.
    QTextCursor cursor(&document);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    const QTextCharFormat format = cursor.charFormat();
    cursor.setPosition(0);
    cursor.insertText(m_node->title.isEmpty() ? m_node->number : m_node->number + u' ', format);
}

qreal NodeGeometry::layoutCustomWidth() const { return std::max(m_node->customWidth, m_justifiedWidth); }

// FEAT-060: label pills in centred rows, wrapped at the topic's width.
void NodeGeometry::updateLabelLayout() {
    LabelLayout next;
    next.labels = m_node->labels;
    next.width = std::max(shapeRect().width(), kLabelMinWidthPx);
    if (next == m_labelLayout) return;
    // Without labels before and after the geometry does not change (only the spare width).
    if (!next.labels.isEmpty() || !m_labelLayout.labels.isEmpty()) changing();
    if (!next.labels.isEmpty()) {
        const QFontMetricsF metrics(labelFont());
        QList<QList<QRectF>> rows(1);
        qreal x = 0;
        for (const QString &label : next.labels) {
            const qreal width = std::min(next.width, std::ceil(metrics.horizontalAdvance(label)) + 2 * kLabelPaddingPx);
            if (x > 0 && x + width > next.width) { rows.append(QList<QRectF>()); x = 0; }
            rows.back().append(QRectF(x, (rows.size() - 1) * (kLabelHeightPx + kLabelGapPx), width, kLabelHeightPx));
            x += width + kLabelGapPx;
        }
        for (const QList<QRectF> &row : rows) {
            const qreal rowWidth = row.back().right() - row.front().left();
            const qreal shift = (next.width - rowWidth) / 2;  // every row centred
            for (const QRectF &pill : row) next.pills.append(pill.translated(shift, 0));
        }
        next.size = {next.width, rows.size() * kLabelHeightPx + (rows.size() - 1) * kLabelGapPx};
    }
    m_labelLayout = next;
}

qreal NodeGeometry::labelsBlockHeight() const {
    return m_labelLayout.pills.isEmpty() ? 0 : kLabelTopGapPx + m_labelLayout.size.height();
}

QList<QRectF> NodeGeometry::labelRects() const {
    QList<QRectF> rects;
    if (m_labelLayout.pills.isEmpty()) return rects;
    const QRectF shape = shapeRect();
    const QPointF origin(shape.center().x() - m_labelLayout.width / 2, shape.bottom() + kLabelTopGapPx);
    for (const QRectF &pill : m_labelLayout.pills) rects.append(pill.translated(origin));
    return rects;
}

// FEAT-059 (revised after the trial, 2026-10-19): with "Show all notes" the note is INSIDE the topic,
// under image and text (the contract's "below" place), instead of the button; the topic grows and the
// map is laid out again. As wide as the content, 128 to 328 px of text.
void NodeGeometry::updateNotePreview(const QFont &baseFont) {
    NotePreviewKey key;
    key.shown = showAllNotes() && hasNote(m_node);
    if (key.shown) {
        key.notes = m_node->notes;
        key.notesHtml = m_node->notesHtml;
        key.color = m_node->text.rgba();
        const qreal inner = contentLayout(false).content.width() - taskZoneWidth() - linkZoneWidth();
        key.width = std::clamp(inner - 2 * kNoteSideMarginPx - 2 * kNotePreviewPaddingPx, kNotePreviewMinTextPx, kNotePreviewMaxTextPx);
    }
    if (key == m_notePreviewKey) return;
    changing();
    m_notePreviewKey = key;
    m_notePreview.reset();
    m_notePreviewHeight = 0;
    if (!key.shown) return;
    m_notePreview = std::make_unique<QTextDocument>();
    QFont font = baseFont;
    font.setPixelSize(kNotePreviewFontPx);
    m_notePreview->setDefaultFont(font);
    m_notePreview->setDocumentMargin(0);
    if (key.notesHtml.isEmpty()) m_notePreview->setPlainText(key.notes);
    else m_notePreview->setHtml(key.notesHtml);
    if (FormulaRendering::hooks().renderInDocument) FormulaRendering::hooks().renderInDocument(m_notePreview.get(), font, m_node->text);  // FEAT-064
    m_notePreview->setTextWidth(key.width);
    const qreal maxText = QFontMetricsF(font).lineSpacing() * kNotePreviewMaxLines;
    m_notePreviewHeight = std::min(m_notePreview->size().height(), maxText) + 2 * kNotePreviewPaddingPx;
}

void NodeGeometry::updateText() {
    // FEAT-022: the topic's font and metrics prepared once per measure.
    const QFont font = titleFont(m_node);
    const QFontMetricsF metrics(font);
    // BUG-003: narrowest content = one average character of the topic's font, so an empty title is not
    // a box of padding only. Measured here, when the data change.
    m_minContentWidth = metrics.averageCharWidth();
    // The rendering is prepared only for titles with $...$: text-only topics pay nothing more.
    m_formula.reset();
    if (FormulaText::containsMath(m_node->title) && FormulaRendering::hooks().layoutTitle)
        m_formula = FormulaRendering::hooks().layoutTitle(shownTitle(), font, m_node->text,
                                               layoutCustomWidth() > 0 ? std::max<qreal>(1, manualTextWidth() - 2 * padX())
                                                                       : kMaxFormulaNodeWidth - 22);
    if (!m_node->titleHtml.isEmpty() && !m_formula) {
        if (!m_richDocument) m_richDocument = std::make_unique<QTextDocument>();
        m_richDocument->setDocumentMargin(0);
        m_richDocument->setDefaultFont(font);
        m_richDocument->setHtml(RichTitle::withoutDocumentFont(m_node->titleHtml));  // FEAT-022: the topic's font holds
        prependNumber(*m_richDocument);
        QTextOption richOption = m_richDocument->defaultTextOption();
        richOption.setAlignment(textAlignment(m_node));
        // (request 2026-10-13: a word wider than the topic left the box and was cut. Now it wraps inside
        // the word too, only when there is no other way: https://doc.qt.io/qt-6/qtextoption.html#WrapMode-enum)
        richOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        m_richDocument->setDefaultTextOption(richOption);
        m_richDocument->setTextWidth(-1);
        const qreal naturalWidth = std::ceil(m_richDocument->idealWidth());
        m_richDocument->setTextWidth(layoutCustomWidth() > 0 ? std::max<qreal>(1, manualTextWidth() - 2 * padX())
                                                             : std::min(naturalWidth, maxTextNodeWidth() - 2 * padX()));
        m_textSize = m_richDocument->size();
        return;
    }
    m_richDocument.reset();
    // The plain text measured for the box that fits it (see textRect()).
    const QString plain = shownTitle().replace(QStringLiteral("\\$"), QStringLiteral("$"));
    const qreal singleLine = metrics.horizontalAdvance(plain);
    // A title with a line break (Shift+Enter) is never one line. A width chosen by dragging the sides:
    // the text wraps at that width minus the padding.
    if (layoutCustomWidth() > 0)
        m_textSize = plainTextLayout(plain, font, plainWrapWidth(manualTextWidth()), nullptr);
    else if (!plain.contains(QLatin1Char('\n')) && singleLine + 2 * padX() <= maxTextNodeWidth())
        m_textSize = {singleLine, metrics.height()};
    else
        // The usable width is the box limit minus the padding of the role (FEAT-072: was a fixed 22, the
        // old padding, so a title of the central topic was measured on one line and drawn on two).
        m_textSize = plainTextLayout(plain, font, plainWrapWidth(maxTextNodeWidth()), nullptr);
    updatePlainLayout(font, metrics.height());  // P5
}

// P5 (2026-10-13): the same layout as QPainter::drawText(rect, Qt::AlignCenter | Qt::TextWordWrap,
// text), made before at every paint(). It follows Qt's qt_format_text() (src/gui/painting/qpainter.cpp):
// lines as wide as the box minus the padding, leading between lines, every line brought to a whole
// pixel (qCeil) and ascent + descent high. The horizontal centring line by line is done in paint().
void NodeGeometry::updatePlainLayout(const QFont &font, qreal fontHeight) {
    if (m_formula || m_richDocument) { m_plainLayout.reset(); return; }
    const QString plain = shownTitle().replace(QStringLiteral("\\$"), QStringLiteral("$"));
    const qreal width = plainWrapWidth(textBlockWidth(m_textSize.width(), maxTextNodeWidth()));
    m_plainLayout = std::make_unique<QTextLayout>();
    const QSizeF size = plainTextLayout(plain, font, width, m_plainLayout.get());
    m_plainLayoutSize = {width, size.height()};
    // FEAT-022: justified. The lines widen up to the box's usable width: with the automatic width it is
    // the longest line, with a hand width the wrap limit. The breaks do not change.
    if (m_node->textAlign == TextAlign::Giustificato && m_plainLayout->lineCount() > 1) {
        const qreal usable = textBlockWidth(m_textSize.width(), maxTextNodeWidth()) - 2 * padX();
        plainTextLayout(plain, font, std::max(size.width(), std::min(usable, width)), m_plainLayout.get(), Qt::AlignJustify);
    }
    m_plainLayoutFontHeight = fontHeight;
    ++m_plainLayoutGeneration;  // a new layout: the painter's metrics check is made again
}

// The plain title's layout (P5), for measuring and drawing: the steps of qt_format_text() above. The
// only difference from drawText(..., Qt::TextWordWrap) is the break inside a word wider than the line
// (request 2026-10-13): WrapAtWordBoundaryOrAnywhere breaks the word only when there is no better place.
// Returns the longest line's width and the total height.
QSizeF NodeGeometry::plainTextLayout(const QString &plain, const QFont &font, qreal width, QTextLayout *layout,
                                     Qt::Alignment alignment) {
    QTextLayout local;
    QTextLayout *target = layout ? layout : &local;
    target->setText(QString(plain).replace(QLatin1Char('\n'), QChar::LineSeparator));
    target->setFont(font);
    // As qt_format_text: left alignment in the layout, centre and right made by hand in paint(); the
    // layout justifies (FEAT-022).
    QTextOption option(alignment);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    target->setTextOption(option);
    const qreal leading = QFontMetricsF(font).leading();
    qreal height = -leading;
    qreal widest = 0;
    target->beginLayout();
    for (QTextLine line = target->createLine(); line.isValid(); line = target->createLine()) {
        line.setLineWidth(width);
        height += leading;
        height = std::ceil(height);
        line.setPosition(QPointF(0, height));
        height += line.ascent() + line.descent();
        widest = std::max(widest, line.naturalTextWidth());
    }
    target->endLayout();
    return {widest, std::max<qreal>(0, height)};
}

bool NodeGeometry::hasSideImage() const {
    return m_hasImage && !m_node->title.trimmed().isEmpty()
           && (m_node->imagePlacement == MindNode::ImagePlacement::Left || m_node->imagePlacement == MindNode::ImagePlacement::Right);
}

qreal NodeGeometry::textBlockWidth(qreal content, qreal maxWidth) const {
    // A width chosen by the user: the block fills the room left by a side image and the buttons; it
    // widens only if the content cannot wrap narrower (a formula), so nothing leaves the border.
    if (layoutCustomWidth() > 0) return std::max(manualTextWidth(), std::ceil(content) + 2 * padX());
    return std::clamp(std::max(content, m_minContentWidth) + 2 * padX(), 0.0, maxWidth);
}

NodeGeometry::ContentLayout NodeGeometry::contentLayout(bool withNote) const {
    // Text block: while edited it follows the text typed; with formulas it grows to hold them; an empty
    // title with an image has no block (only the image).
    QSizeF block;
    bool hasText = true;
    if (m_editingSize) block = {textBlockWidth(m_editingSize->width(), std::max(maxTextNodeWidth(), kMaxFormulaNodeWidth)), m_editingSize->height() + 2 * padY()};
    else if (m_node->title.trimmed().isEmpty() && m_hasImage) hasText = false;
    else if (m_formula) block = {textBlockWidth(m_formula->size().width(), kMaxFormulaNodeWidth), m_formula->size().height() + 2 * padY()};
    else block = {textBlockWidth(m_textSize.width(), maxTextNodeWidth()), m_textSize.height() + 2 * padY()};
    // BUG-020 (user's photo): a topic without text and image is a square as high as a one-line topic,
    // so it stays visible and clickable; with a hand width that width holds.
    if (!m_editingSize && hasText && m_node->title.trimmed().isEmpty() && !m_hasImage && layoutCustomWidth() <= 0)
        block.setWidth(block.height());
    // FEAT-037: image inside the topic, 16/8 px of margin like the text, 8 px from the text; the text
    // block has its padding already, which is the room towards an image above or below. Link buttons
    // (FEAT-027) always right of everything.
    const QSizeF image = m_hasImage ? m_node->imageSize : QSizeF();
    const bool side = hasText && hasSideImage();
    qreal inner = 0, height = 0;
    if (!m_hasImage) { inner = block.width(); height = block.height(); }
    else if (!hasText) { inner = image.width() + 2 * padX(); height = image.height() + 2 * padY(); }
    else if (side) { inner = image.width() + kImageGapPx + block.width(); height = std::max(block.height(), image.height() + 2 * padY()); }
    else { inner = std::max(block.width(), image.width() + 2 * padX()); height = block.height() + image.height() + padY(); }
    const qreal links = linkZoneWidth();
    const qreal task = taskZoneWidth();  // FEAT-048: the task box column, mirror of the links
    if (layoutCustomWidth() > 0) inner = std::max(inner, layoutCustomWidth() - links - task);
    // FEAT-059: note shown inside the topic, under the content (8 px below, side margins 16).
    qreal noteWidth = 0, noteHeight = 0;
    if (withNote && m_notePreview) {
        noteWidth = m_notePreviewKey.width + 2 * kNotePreviewPaddingPx;
        noteHeight = m_notePreviewHeight;
        inner = std::max(inner, noteWidth + 2 * padX());
    }
    const qreal extra = noteHeight > 0 ? noteHeight + padY() : 0;
    ContentLayout layout;
    layout.box = {-(task + inner + links) / 2, -(height + extra) / 2, task + inner + links, height + extra};
    const QRectF &box = layout.box;
    layout.content = {box.left(), box.top(), box.width(), height};
    const QRectF &content = layout.content;
    const qreal left0 = box.left() + task;  // where the content (image and text) starts
    const qreal centerX = left0 + inner / 2;
    if (noteHeight > 0) layout.note = {centerX - noteWidth / 2, content.bottom(), noteWidth, noteHeight};
    if (!m_hasImage) {
        layout.text = {left0, content.top(), inner, height};
    } else if (!hasText) {
        layout.image = {centerX - image.width() / 2, content.center().y() - image.height() / 2, image.width(), image.height()};
    } else if (side) {
        // The text block's left padding (16 px) covers the 8 px from the image.
        const qreal textWidth = inner - image.width() - kImageGapPx;
        const bool left = m_node->imagePlacement == MindNode::ImagePlacement::Left;
        const qreal imageX = left ? left0 + padX() : left0 + textWidth - padX() + kImageGapPx;
        layout.image = {imageX, content.center().y() - image.height() / 2, image.width(), image.height()};
        layout.text = {left ? layout.image.right() + kImageGapPx - padX() : left0, content.center().y() - block.height() / 2,
                       textWidth, block.height()};
    } else {
        const bool above = m_node->imagePlacement == MindNode::ImagePlacement::Above;
        const qreal imageY = above ? content.top() + padY() : content.top() + block.height();
        layout.image = {centerX - image.width() / 2, imageY, image.width(), image.height()};
        layout.text = {centerX - block.width() / 2, above ? layout.image.bottom() : content.top(), block.width(), block.height()};
    }
    return layout;
}

qreal NodeGeometry::linkZoneWidth() const {
    // Right column: note (FEAT-059), links (FEAT-027) and files (FEAT-049).
    const int count = int(!m_node->topicLink.isEmpty()) + int(!m_node->webLink.isEmpty()) + int(hasNote(m_node) && !showAllNotes())
                      + int(!m_node->files.isEmpty());
    return count == 0 ? 0 : kLinkTextGapPx + count * kLinkButtonPx + (count - 1) * kLinkIconGapPx;
}

qreal NodeGeometry::manualTextWidth() const {
    // FEAT-037: an image on the left or right takes its width (and the gap) from the text.
    const qreal sideImage = hasSideImage() ? m_node->imageSize.width() + kImageGapPx : 0;
    return std::max<qreal>(1, layoutCustomWidth() - linkZoneWidth() - taskZoneWidth() - sideImage);
}

qreal NodeGeometry::taskZoneWidth() const {
    // As the links: 8 px from the content and a 22 px button (plus 4 between two); the text's 16 px
    // padding does the rest. FEAT-051: markers first, then the task box.
    const int count = int(m_node->markers.size()) + int(m_node->task != MindNode::Task::None);
    return count == 0 ? 0 : kLinkTextGapPx + count * kLinkButtonPx + (count - 1) * kLinkIconGapPx;
}

QList<QRectF> NodeGeometry::imageDropZones() const {
    // Nell'ordine di MindNode::ImagePlacement. Come nella foto 3 (dell'utente, 2026-10-17) il nodo
    // stesso si divide: a sinistra e a destra due strisce larghe un quarto, alte quanto il nodo; in
    // mezzo la meta' alta (Sopra) e quella bassa (Sotto). Prima erano quattro riquadri attorno al
    // nodo con il nome della posizione.
    const QRectF box = textRect();
    const qreal side = box.width() / 4;
    const qreal middle = box.width() - 2 * side;
    const qreal half = box.height() / 2;
    return {{box.left() + side, box.top(), middle, half}, {box.left() + side, box.top() + half, middle, half},
            {box.left(), box.top(), side, box.height()}, {box.right() - side, box.top(), side, box.height()}};
}

int NodeGeometry::imageDropZoneAt(const QPointF &position) const {
    const QList<QRectF> zones = imageDropZones();
    for (int i = 0; i < zones.size(); ++i) if (zones[i].contains(position)) return i;
    return -1;
}

QList<QRectF> NodeGeometry::markerRects() const {
    QList<QRectF> rects;
    if (m_node->markers.isEmpty()) return rects;
    const QRectF box = textRect();
    if (box.isEmpty()) return rects;
    const qreal y = contentLayout().content.center().y() - kLinkButtonPx / 2;
    for (int i = 0; i < m_node->markers.size(); ++i)
        rects.append({box.left() + padX() + i * (kLinkButtonPx + kLinkIconGapPx), y, kLinkButtonPx, kLinkButtonPx});
    return rects;
}

QRectF NodeGeometry::taskRect() const {
    if (m_node->task == MindNode::Task::None) return {};
    const QRectF box = textRect();
    if (box.isEmpty()) return {};
    const qreal markers = m_node->markers.size() * (kLinkButtonPx + kLinkIconGapPx);  // FEAT-051: after the markers
    return {box.left() + padX() + markers, contentLayout().content.center().y() - kLinkButtonPx / 2, kLinkButtonPx, kLinkButtonPx};
}

QRectF NodeGeometry::linkIconRect(bool topic) const {
    const bool hasTopic = !m_node->topicLink.isEmpty(), web = !m_node->webLink.isEmpty();
    if ((topic && !hasTopic) || (!topic && !web)) return {};
    const QRectF box = textRect();
    if (box.isEmpty()) return {};
    // From the right: the text's padding, the files' paperclip (FEAT-049), the web button, then (further
    // left) the topic's.
    qreal right = box.right() - padX() - (m_node->files.isEmpty() ? 0 : kLinkButtonPx + kLinkIconGapPx);
    if (topic && web) right -= kLinkButtonPx + kLinkIconGapPx;
    const qreal centerY = contentLayout().content.center().y();  // FEAT-059: not on the shown note
    return {right - kLinkButtonPx, centerY - kLinkButtonPx / 2, kLinkButtonPx, kLinkButtonPx};
}

QRectF NodeGeometry::noteRect() const {
    if (!hasNote(m_node) || showAllNotes()) return {};  // note shown: no button
    const QRectF box = textRect();
    if (box.isEmpty()) return {};
    // Before the links: from the right, padding, the link buttons there are, then the note.
    const int links = int(!m_node->topicLink.isEmpty()) + int(!m_node->webLink.isEmpty()) + int(!m_node->files.isEmpty());
    const qreal right = box.right() - padX() - links * (kLinkButtonPx + kLinkIconGapPx);
    return {right - kLinkButtonPx, contentLayout().content.center().y() - kLinkButtonPx / 2, kLinkButtonPx, kLinkButtonPx};
}

QRectF NodeGeometry::filesRect() const {
    if (m_node->files.isEmpty()) return {};
    const QRectF box = textRect();
    if (box.isEmpty()) return {};
    const qreal right = box.right() - padX();
    return {right - kLinkButtonPx, contentLayout().content.center().y() - kLinkButtonPx / 2, kLinkButtonPx, kLinkButtonPx};
}

qreal NodeGeometry::cornerRadius() const { return m_node->summaryTopic ? Summaries::kTopicRadiusPx : -1; }

qreal NodeGeometry::padX() const { return NodeStyle::textPadding(m_node).x(); }

qreal NodeGeometry::padY() const { return NodeStyle::textPadding(m_node).y(); }

qreal NodeGeometry::maxTextNodeWidth() const { return m_node && !m_node->parent ? kMaxCentralNodeWidth : kMaxTextNodeWidth; }

qreal NodeGeometry::plainWrapWidth(qreal boxWidth) const {
    // Automatic: lines as wide as the box minus the padding, with 10 px of slack (P5, as drawText before:
    // with the 16 px padding it was box - 22). Chosen by the user: minus the whole padding, so the text
    // always has the same margin and a long line does not come close to the border.
    return std::max<qreal>(1, boxWidth - (layoutCustomWidth() > 0 ? 2 * padX() : 2 * padX() - 10));
}

qreal NodeGeometry::minimumCustomWidth() const {
    // The title laid out at width 1: every line holds one character (Qt always puts at least one per
    // line), and the widest line is the widest character.
    const QFont font = titleFont(m_node);
    qreal content = 0;
    if (m_formula) {
        if (const std::unique_ptr<TitleFormula> narrow = FormulaRendering::hooks().layoutTitle(shownTitle(), font, m_node->text, 1))
            content = narrow->size().width();
    } else if (m_richDocument) {
        QTextDocument narrow;
        narrow.setDocumentMargin(0);
        narrow.setDefaultFont(font);
        narrow.setHtml(RichTitle::withoutDocumentFont(m_node->titleHtml));
        prependNumber(narrow);
        narrow.setDefaultTextOption(m_richDocument->defaultTextOption());
        narrow.setTextWidth(1);
        content = narrow.idealWidth();
    } else {
        const QString plain = shownTitle().replace(QStringLiteral("\\$"), QStringLiteral("$"));
        content = plainTextLayout(plain, font, 1, nullptr).width();
    }
    // FEAT-027: the buttons stay; FEAT-037: the image does not shrink when the border is dragged.
    const qreal text = std::ceil(content) + 2 * padX();
    qreal inner = text;
    if (m_hasImage) {
        const qreal image = m_node->imageSize.width();
        if (m_node->title.trimmed().isEmpty()) inner = image + 2 * padX();
        else if (hasSideImage()) inner = image + kImageGapPx + text;
        else inner = std::max(text, image + 2 * padX());
    }
    return inner + linkZoneWidth() + taskZoneWidth();
}

qreal NodeGeometry::imageRoomWidth() const {
    return textRect().width() - taskZoneWidth() - linkZoneWidth() - 2 * padX();
}

qreal NodeGeometry::anchorHalfWidth() const {
    const QRectF shape = shapeRect();
    return shape.isEmpty() ? 105.0 : shape.width() / 2;
}

QRectF NodeGeometry::shapeRect() const {
    const QRectF content = textRect();
    return NodeShapes::outerRect(m_node->shape, content, m_node->borderWidth, std::max<qreal>(0, content.width() - 2 * padX()));
}

QPointF NodeGeometry::branchAnchor(bool right) const {
    const QRectF shape = shapeRect();
    if (shape.isEmpty()) return {right ? 105.0 : -105.0, 0};
    return NodeShapes::branchAnchor(m_node->shape, shape, m_node->borderWidth, right);
}

qreal NodeGeometry::layoutHeight() const {
    // FEAT-060: the layout treats the topic as centred on its height: the labels' room under the shape
    // counts above and below, so they do not cover the next topic.
    return normalBoundingRect().height() + 2 * labelsBlockHeight();
}

QRectF NodeGeometry::imageRect() const {
    // FEAT-037: the image is inside the topic (before outside the shape, 10 px from the box).
    return m_hasImage ? contentLayout().image : QRectF();
}

QRectF NodeGeometry::normalBoundingRect() const {
    QRectF bounds = shapeRect();
    if (!imageRect().isEmpty()) bounds = bounds.isEmpty() ? imageRect() : bounds.united(imageRect());
    if (bounds.isEmpty()) bounds = {-105, -34, 210, 68};
    return bounds.adjusted(-4, -4, 4, 4);
}

QSizeF NodeGeometry::insertionImageSize(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSizeF size = reader.size();
    if (reader.transformation() & QImageIOHandler::TransformationRotate90) size.transpose();
    if (size.isEmpty()) size = {180, 120};
    if (size.width() > kMaxImageWidthPx) size = size.scaled(kMaxImageWidthPx, size.height(), Qt::KeepAspectRatio);
    return {std::round(size.width()), std::round(size.height())};
}

QSizeF NodeGeometry::fittedImageSize(const QString &path, qreal roomWidth) {
    const QSizeF natural = insertionImageSize(path);
    const qreal room = std::max<qreal>(std::floor(roomWidth), kMinInsertWidthPx);
    if (natural.width() <= room) return natural;
    return {room, std::max<qreal>(1, std::round(natural.height() * room / natural.width()))};
}
