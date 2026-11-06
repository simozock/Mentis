#include "pitch.h"

#include "formulatext.h"
#include "markers.h"
#include "mindmapmodel.h"
#include "nodeitem.h"
#include "nodestyle.h"
#include "performance.h"
#include "stickers.h"

#include <QCache>
#include <QFontMetricsF>
#include <QHash>
#include <QImageReader>
#include <QLineF>
#include <QMutex>
#include <QPaintEngine>
#include <QPolygonF>
#include <QRandomGenerator>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numeric>
#include <numbers>
#include <utility>

namespace Pitch {

namespace {
template <typename Enum, std::size_t N>
QString keyOf(Enum value, const std::array<std::pair<Enum, const char *>, N> &keys) {
    for (const auto &[candidate, key] : keys)
        if (candidate == value) return QString::fromLatin1(key);
    return {};
}
template <typename Enum, std::size_t N>
Enum fromKeyOf(const QString &key, const std::array<std::pair<Enum, const char *>, N> &keys, Enum fallback) {
    for (const auto &[candidate, stored] : keys)
        if (key == QLatin1String(stored)) return candidate;
    return fallback;
}
// The default of each setting is stored as nothing (empty key).
const std::array<std::pair<Visibility, const char *>, 3> kVisibility{{{Visibility::Auto, ""}, {Visibility::Visible, "visible"}, {Visibility::Hidden, "hidden"}}};
const std::array<std::pair<Delivery, const char *>, 3> kDelivery{{{Delivery::OneMemorize, ""}, {Delivery::One, "one"}, {Delivery::All, "all"}}};
const std::array<std::pair<Layout, const char *>, kLayoutCount> kLayout{
    {{Layout::Bracket, ""}, {Layout::Bullets, "bullets"}, {Layout::Right, "right"}, {Layout::Fan, "fan"}, {Layout::Org, "org"},
     {Layout::Fishbone, "fishbone"}, {Layout::Timeline, "timeline"}, {Layout::Grid, "grid"}, {Layout::Numbered, "numbered"},
     {Layout::Circle, "circle"}}};
const std::array<std::pair<Theme, const char *>, kThemeCount> kTheme{
    {{Theme::Dark, ""}, {Theme::Light, "light"}, {Theme::Sheet, "sheet"}, {Theme::Proposal, "proposal"}, {Theme::Playful, "playful"},
     {Theme::Launch, "launch"}, {Theme::Manifesto, "manifesto"}, {Theme::Halftone, "halftone"}, {Theme::Paper, "paper"},
     {Theme::Night, "night"}, {Theme::Soft, "soft"}, {Theme::Portfolio, "portfolio"}, {Theme::Editorial, "editorial"}}};
const std::array<std::pair<Aspect, const char *>, 5> kAspect{{{Aspect::Auto, ""}, {Aspect::Wide, "16:9"}, {Aspect::Classic, "4:3"},
                                                             {Aspect::Tall, "9:16"}, {Aspect::TallClassic, "3:4"}}};
}  // namespace

QString visibilityKey(Visibility value) { return keyOf(value, kVisibility); }
Visibility visibilityFromKey(const QString &key) { return fromKeyOf(key, kVisibility, Visibility::Auto); }
QString deliveryKey(Delivery value) { return keyOf(value, kDelivery); }
Delivery deliveryFromKey(const QString &key) { return fromKeyOf(key, kDelivery, Delivery::OneMemorize); }
QString layoutKey(Layout value) { return keyOf(value, kLayout); }
Layout layoutFromKey(const QString &key) { return fromKeyOf(key, kLayout, Layout::Bracket); }
QString themeKey(Theme value) { return keyOf(value, kTheme); }
Theme themeFromKey(const QString &key) { return fromKeyOf(key, kTheme, Theme::Dark); }
QString aspectKey(Aspect value) { return keyOf(value, kAspect); }
Aspect aspectFromKey(const QString &key) { return fromKeyOf(key, kAspect, Aspect::Auto); }
QString validVisibility(const QString &key) { return visibilityKey(visibilityFromKey(key)); }
QString validDelivery(const QString &key) { return deliveryKey(deliveryFromKey(key)); }
QString validLayout(const QString &key) { return layoutKey(layoutFromKey(key)); }
QString validTheme(const QString &key) { return themeKey(themeFromKey(key)); }
QString validAspect(const QString &key) { return aspectKey(aspectFromKey(key)); }

int maxItems(Layout layout, bool withFigures) {
    switch (layout) {
    case Layout::Org: return kMaxRowItems;
    case Layout::Fishbone: case Layout::Timeline: case Layout::Circle: return kMaxSpreadItems;
    case Layout::Grid: return withFigures ? kMaxGridItemsWithFigures : kMaxGridItems;
    default: return withFigures ? kMaxListItemsWithFigures : kMaxListItems;
    }
}

namespace {
// Pixels per node pixel of a decoded figure: twice the map's (Performance::imageScaleFactor, 4 with
// Standard, 2 with Light), since the proportion to the text makes a figure about 2-4x its node size
// on a 1080p slide. At most kMaxFigurePx on the long side: a full-screen slide shows no more, and a
// big node image does not cost tens of MB.
constexpr qreal kFigureResolutionPerMapScale = 2;
constexpr qreal kMaxFigurePx = 1280;

bool hasFigure(const MindNode *node) { return !node->formula.isEmpty() || !node->imagePath.isEmpty(); }
}  // namespace

Extras extrasOf(const MindNode *node) {
    Extras extras;
    extras.markers = node->markers;
    if (!hasFigure(node) || node->imageSize.isEmpty()) return extras;
    // The same pixels as the node (NodeItem::loadImage): read upright at the unrotated size, then turned.
    const QFont font = NodeItem::titleFont(node);
    const int rotation = node->imageStyle.rotation;
    const QSizeF upright = rotation % 180 ? node->imageSize.transposed() : node->imageSize;
    const qreal resolution = std::min(kFigureResolutionPerMapScale * Performance::imageScaleFactor(),
                                      kMaxFigurePx / std::max(upright.width(), upright.height()));
    QImage image;
    if (!node->formula.isEmpty()) {
        image = FormulaText::boxImage(node->formula, font, node->formulaBackground,
                                      FormulaText::textColorOn(node->formulaBackground, node->text), upright, resolution);
    } else {
        QImageReader reader(node->imagePath);
        reader.setAutoTransform(true);
        const QSize target = (upright * resolution).toSize().expandedTo(QSize(1, 1));
        const QSize source = reader.size();
        if (source.isValid() && (source.width() > target.width() || source.height() > target.height())) reader.setScaledSize(target);
        image = reader.read();
        if (!image.isNull() && node->imageSticker && node->imageStyle.outline) image = Stickers::outlined(image);
    }
    if (image.isNull()) return extras;  // a missing file: the entry shows no figure, as the spec says
    if (rotation != 0) image = image.transformed(QTransform().rotate(rotation), Qt::SmoothTransformation);
    extras.figure = image;
    extras.figureLines = node->imageSize.height() / QFontMetricsF(font).height();
    extras.nodeFigureSize = node->imageSize;
    extras.figureStyle = node->imageStyle;
    switch (node->imagePlacement) {
    case MindNode::ImagePlacement::Above: extras.side = Extras::Side::Above; break;
    case MindNode::ImagePlacement::Below: extras.side = Extras::Side::Below; break;
    case MindNode::ImagePlacement::Left: extras.side = Extras::Side::Left; break;
    case MindNode::ImagePlacement::Right: extras.side = Extras::Side::Right; break;
    }
    return extras;
}

namespace {
// Markers as tall as the text, spaced like the node's (4 px apart, 8 px from the text, with an
// 18 px icon in a 22 px button: 8/18 and 10/18 of the icon).
constexpr qreal kMarkerGap = 8.0 / 18.0;
constexpr qreal kMarkerTextGap = 10.0 / 18.0;
constexpr qreal kFigureGapLines = 0.3;   // between the figure and the text, in lines
constexpr qreal kMinTextShare = 0.4;     // a figure beside the text leaves it this much of the box
}  // namespace

EntryLayout layoutEntry(const QRectF &box, int alignment, const QFont &font, const QString &text, const Extras &extras,
                        const QSizeF &figureCap, qreal figureScale) {
    EntryLayout result;
    const QFontMetricsF metrics(font);
    const qreal line = metrics.height();
    const qreal icon = font.pixelSize() > 0 ? font.pixelSize() : line;
    const qsizetype markerCount = extras.markers.size();
    const qreal markersWidth = markerCount == 0 ? 0 : markerCount * icon + (markerCount - 1) * icon * kMarkerGap + icon * kMarkerTextGap;
    const qreal gap = line * kFigureGapLines;
    const bool wrap = alignment & Qt::TextWordWrap;
    const qreal advance = metrics.horizontalAdvance(text);

    QSizeF figure;
    if (extras.hasFigure()) {
        const qreal aspect = extras.nodeFigureSize.width() / extras.nodeFigureSize.height();
        qreal height = std::min(extras.figureLines * line * figureScale, figureCap.height());
        height = std::min(height, std::min(figureCap.width(), box.width()) / aspect);
        figure = QSizeF(height * aspect, height);
    }
    Extras::Side side = extras.side;
    bool beside = side == Extras::Side::Left || side == Extras::Side::Right;
    if (beside && !figure.isEmpty() && box.width() - figure.width() - gap - markersWidth < std::min(advance, box.width() * kMinTextShare)) {
        side = Extras::Side::Above;
        beside = false;
    }
    const qreal textRoom = std::max<qreal>(1, box.width() - markersWidth - (beside && !figure.isEmpty() ? figure.width() + gap : 0));
    // A word wider than the room breaks inside, as a last resort (the font is fitted first).
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const bool breakWords = wrap && std::any_of(words.begin(), words.end(), [&](const QString &word) { return metrics.horizontalAdvance(word) > textRoom; });
    result.textFlags = (alignment & Qt::AlignHorizontal_Mask) | Qt::AlignVCenter | (breakWords ? Qt::TextWrapAnywhere : wrap ? Qt::TextWordWrap : 0);
    QSizeF textSize = wrap ? metrics.boundingRect(QRectF(0, 0, textRoom, 1e6), result.textFlags & ~Qt::AlignVertical_Mask, text).size()
                           : QSizeF(std::min(advance, textRoom), line);
    textSize = QSizeF(std::min(textSize.width(), textRoom), std::max(textSize.height(), line));
    // The figure shrinks to the room the box leaves it; too small to read, it is left out.
    if (!figure.isEmpty()) {
        const qreal room = beside ? box.height() : box.height() - textSize.height() - gap;
        if (figure.height() > room) figure *= std::max<qreal>(0, room) / figure.height();
        if (figure.height() < icon * 0.5) figure = QSizeF();
    }
    const bool shown = !figure.isEmpty();
    const qreal rowWidth = markersWidth + textSize.width();
    const QSizeF block = !shown ? QSizeF(rowWidth, textSize.height())
                         : beside ? QSizeF(rowWidth + gap + figure.width(), std::max(figure.height(), textSize.height()))
                                  : QSizeF(std::max(figure.width(), rowWidth), figure.height() + gap + textSize.height());
    const auto alignedX = [&](qreal left, qreal room, qreal width) {
        if (alignment & Qt::AlignRight) return left + room - width;
        if (alignment & Qt::AlignHCenter) return left + (room - width) / 2;
        return left;
    };
    const qreal top = alignment & Qt::AlignTop ? box.top() : alignment & Qt::AlignBottom ? box.bottom() - block.height()
                                                                                       : box.center().y() - block.height() / 2;
    result.block = QRectF(QPointF(alignedX(box.left(), box.width(), block.width()), top), block);

    // The row: markers then text, with the figure on its side.
    qreal rowLeft = result.block.left();
    qreal rowCentre = result.block.center().y();
    if (shown) {
        switch (side) {
        case Extras::Side::Above:
            result.figure = QRectF(QPointF(alignedX(result.block.left(), block.width(), figure.width()), result.block.top()), figure);
            rowLeft = alignedX(result.block.left(), block.width(), rowWidth);
            rowCentre = result.block.bottom() - textSize.height() / 2;
            break;
        case Extras::Side::Below:
            result.figure = QRectF(QPointF(alignedX(result.block.left(), block.width(), figure.width()), result.block.bottom() - figure.height()), figure);
            rowLeft = alignedX(result.block.left(), block.width(), rowWidth);
            rowCentre = result.block.top() + textSize.height() / 2;
            break;
        case Extras::Side::Left:  // as in the node: markers, figure, text
            result.figure = QRectF(QPointF(result.block.left() + markersWidth, result.block.center().y() - figure.height() / 2), figure);
            break;
        case Extras::Side::Right:
            result.figure = QRectF(QPointF(result.block.right() - figure.width(), result.block.center().y() - figure.height() / 2), figure);
            break;
        }
    }
    for (qsizetype i = 0; i < markerCount; ++i)
        result.markers.append(QRectF(rowLeft + i * icon * (1 + kMarkerGap), rowCentre - icon / 2, icon, icon));
    const qreal textLeft = shown && side == Extras::Side::Left ? result.figure.right() + gap : rowLeft + markersWidth;
    result.text = QRectF(textLeft, rowCentre - textSize.height() / 2, textSize.width(), textSize.height());
    return result;
}

qreal aspectRatio(Aspect aspect, qreal screenRatio) {
    switch (aspect) {
    case Aspect::Wide: return 16.0 / 9.0;
    case Aspect::Classic: return 4.0 / 3.0;
    case Aspect::Tall: return 9.0 / 16.0;
    case Aspect::TallClassic: return 3.0 / 4.0;
    case Aspect::Auto: break;
    }
    return screenRatio > 0 ? screenRatio : 16.0 / 9.0;
}

namespace {
// The children in map order: the branches, then the floating topics.
QList<const MindNode *> orderedChildren(const MindNode *node) {
    QList<const MindNode *> attached, floating;
    for (const auto &child : node->children) (child->floating ? floating : attached).append(child.get());
    return attached + floating;
}

// One Extras per node for a whole build: a figure appears on the topic slide, as the title of its
// list and as an item of its parent's list, decoded once and shared (QImage is implicitly shared).
class ExtrasCache {
public:
    const Extras &of(const MindNode *node) {
        auto found = m_extras.find(node);
        if (found == m_extras.end()) found = m_extras.insert(node, extrasOf(node));
        return *found;
    }

private:
    QHash<const MindNode *, Extras> m_extras;
};

QList<Slide> listSlides(const MindNode *node, ExtrasCache &cache) {
    const QList<const MindNode *> children = orderedChildren(node);
    const Layout layout = layoutFromKey(node->pitchLayout);
    const Extras &titleExtras = cache.of(node);
    QList<Slide> result;
    for (qsizetype first = 0; first < children.size();) {
        // A slide with a figure among its items keeps fewer of them (stage A2).
        qsizetype count = std::min<qsizetype>(children.size() - first, maxItems(layout));
        if (std::any_of(children.begin() + first, children.begin() + first + count, hasFigure))
            count = std::min<qsizetype>(count, maxItems(layout, true));
        Slide slide;
        slide.kind = Slide::Kind::List;
        slide.topicId = node->id;
        slide.title = node->title.simplified();
        slide.titleExtras = titleExtras;
        slide.layout = layout;
        slide.delivery = deliveryFromKey(node->pitchDelivery);
        slide.continued = first > 0;
        for (qsizetype i = first; i < first + count; ++i) {
            slide.items.append(children[i]->title.simplified());
            slide.itemExtras.append(cache.of(children[i]));
        }
        result.append(slide);
        first += count;
    }
    return result;
}

Slide topicSlide(const MindNode *node, ExtrasCache &cache) {
    Slide slide;
    slide.topicId = node->id;
    slide.title = node->title.simplified();
    slide.titleExtras = cache.of(node);
    slide.cover = !node->parent;
    return slide;
}
}  // namespace

// The central topic (cover) and the main topics.
bool autoTopicSlide(const MindNode *node) { return !node->parent || !node->parent->parent; }

bool autoListSlide(const MindNode *node) { return !node->children.empty(); }

bool showsTopicSlide(const MindNode *node) {
    switch (visibilityFromKey(node->pitchTopicSlide)) {
    case Visibility::Visible: return true;
    case Visibility::Hidden: return false;
    case Visibility::Auto: break;
    }
    return autoTopicSlide(node);
}

// A topic without children has no list to show, whatever is chosen.
bool showsListSlide(const MindNode *node) {
    return autoListSlide(node) && visibilityFromKey(node->pitchListSlide) != Visibility::Hidden;
}

QList<Slide> slides(const MindMapModel &model) {
    QList<Slide> result;
    ExtrasCache cache;
    const std::function<void(const MindNode *)> visit = [&](const MindNode *node) {
        const qsizetype first = result.size();
        if (showsTopicSlide(node)) result.append(topicSlide(node, cache));
        if (showsListSlide(node)) result.append(listSlides(node, cache));
        if (result.size() > first) result[first].notes = node->notes.trimmed();  // FEAT-059's plain text
        for (const MindNode *child : orderedChildren(node)) visit(child);
    };
    if (model.root()) visit(model.root());
    for (qsizetype i = 0; i < result.size(); ++i) result[i].number = int(i) + 1;
    return result;
}

Slide previewSlide(const MindMapModel &model, const MindNode *node) {
    if (!node) node = model.root();
    if (!node) return {};
    if (!node->children.empty()) {
        ExtrasCache cache;
        const QList<Slide> lists = listSlides(node, cache);
        if (!lists.isEmpty()) return lists.front();
    }
    ExtrasCache cache;
    return topicSlide(node, cache);
}

Look look(const MindMapModel &model, Theme theme) {
    Look result;
    result.family = model.sheetStyle.fontFamily.isEmpty() ? NodeStyle::kDefaultMapFontFamily : model.sheetStyle.fontFamily;
    switch (theme) {
    case Theme::Dark:
        result.background = Qt::black;
        result.text = Qt::white;
        result.line = QColor(255, 255, 255, 150);
        break;
    case Theme::Light:
        result.background = Qt::white;
        result.text = QColor(0x1f, 0x23, 0x29);
        result.line = QColor(0x8a, 0x8f, 0x98);
        break;
    case Theme::Sheet: {
        const NodeStyle::Backdrop backdrop = model.backdrop();
        result.background = backdrop.color;
        result.text = NodeStyle::annotationColor(QColor(0x30, 0x36, 0x40), backdrop);
        result.line = result.text;
        result.line.setAlpha(150);
        for (const QString &name : model.activePalette)
            if (const QColor color(name); color.isValid()) result.branches.append(color);
        break;
    }
    // Stage B: our own themes, after ten Canva templates the user chose (ideas only: colours, type,
    // one motif). Colours checked for 4.5:1 text contrast (WCAG 2.1, 1.4.3) by PitchTest.
    case Theme::Proposal:  // sand and night blue, heavy capitals, a band on the right edge
        result.background = QColor(0xF1, 0xEC, 0xE2);
        result.text = QColor(0x34, 0x39, 0x7A);
        result.line = result.text;
        result.titleFont = {QStringLiteral("Montserrat"), QFont::Black, 100, true};
        result.itemFont = {QStringLiteral("Montserrat"), QFont::Medium, 100, false};
        result.itemStyle = ItemStyle::Pill;
        result.motif = Motif::Band;
        break;
    case Theme::Playful:  // cream and periwinkle, rounded lettering, a scalloped frame and doodles
        result.background = QColor(0xF7, 0xF3, 0xEA);
        result.text = QColor(0x2E, 0x34, 0x70);
        result.line = QColor(0x8F, 0x96, 0xD6);
        result.coverBackground = result.line;
        result.fill = QColor(0xDC, 0xDF, 0xF5);
        result.titleFont = {QStringLiteral("Atkinson Hyperlegible"), QFont::Bold, 100, true};
        result.itemFont = {QStringLiteral("Atkinson Hyperlegible"), QFont::Normal, 100, false};
        result.itemStyle = ItemStyle::Badge;
        result.motif = Motif::Clouds;
        break;
    case Theme::Launch:  // pale yellow cover, black topic slides, grey lists; narrow black capitals; a soft smudge
        result.background = QColor(0xEC, 0xEC, 0xEA);
        result.text = QColor(0x11, 0x11, 0x11);
        result.line = result.text;
        result.coverBackground = QColor(0xF4, 0xF1, 0xA8);
        result.topicBackground = QColor(0x15, 0x15, 0x15);
        result.topicText = QColor(0xF4, 0xF4, 0xF2);
        result.titleFont = {QStringLiteral("Roboto"), QFont::Black, 75, true};
        result.itemFont = {QStringLiteral("Roboto"), QFont::Normal, 100, false};
        result.motif = Motif::Smudge;
        break;
    case Theme::Manifesto:  // cream, rust and brown; narrow capitals in rust; solid bars and arrows
        result.background = QColor(0xF3, 0xEB, 0xDD);
        result.text = QColor(0x1E, 0x15, 0x13);
        result.title = QColor(0xA9, 0x40, 0x1F);
        result.line = result.title;
        result.coverBackground = result.title;
        result.coverText = result.background;
        result.topicBackground = result.text;
        result.topicText = result.background;
        result.titleFont = {QStringLiteral("Roboto"), QFont::Black, 75, true};
        result.itemFont = {QStringLiteral("Roboto"), QFont::Medium, 100, false};
        result.motif = Motif::Bars;
        break;
    case Theme::Halftone:  // white and blue, halftone dots and diagonal tiles in the corners
        result.background = Qt::white;
        result.text = QColor(0x1F, 0x2A, 0x44);
        result.title = QColor(0x4F, 0x6F, 0xC4);
        result.line = result.title;
        result.fill = QColor(0xC9, 0xD6, 0xF2);
        result.titleFont = {QStringLiteral("Montserrat"), QFont::Bold, 100, true};
        result.itemFont = {QStringLiteral("Montserrat"), QFont::Normal, 100, false};
        result.motif = Motif::Halftone;
        break;
    // Stage B2.
    case Theme::Paper:  // paper grey with a fine grain, black type, a green highlight
        result.background = QColor(0xE9, 0xE8, 0xE4);
        result.text = QColor(0x15, 0x15, 0x15);
        result.line = result.text;
        result.fill = QColor(0x7C, 0xC4, 0x7F);
        result.titleFont = {QStringLiteral("Montserrat"), QFont::ExtraBold, 100, false};
        result.itemFont = {QStringLiteral("Montserrat"), QFont::Normal, 100, false};
        result.motif = Motif::Grain;
        break;
    case Theme::Night:  // near black, warm white narrow capitals, hairlines and an arrow
        result.background = QColor(0x12, 0x12, 0x12);
        result.text = QColor(0xED, 0xEB, 0xE6);
        result.line = QColor(0xED, 0xEB, 0xE6, 170);
        result.titleFont = {QStringLiteral("Roboto"), QFont::Bold, 75, true};
        result.itemFont = {QStringLiteral("Roboto"), QFont::Light, 100, false};
        result.motif = Motif::Lines;
        break;
    case Theme::Soft:  // ivory, sage and blush, spaced capitals, soft shapes, rounded cards
        result.background = QColor(0xF4, 0xF0, 0xE8);
        result.text = QColor(0x3B, 0x3A, 0x36);
        result.title = QColor(0x5E, 0x66, 0x50);
        result.line = QColor(0x8B, 0x94, 0x79);
        result.fill = QColor(0xEF, 0xE2, 0xD6);
        result.titleFont = {QStringLiteral("Montserrat"), QFont::Medium, 100, true, 112};
        result.itemFont = {QStringLiteral("Montserrat"), QFont::Normal, 100, false};
        result.itemStyle = ItemStyle::Badge;
        result.motif = Motif::Blobs;
        break;
    case Theme::Portfolio:  // cream and three colours, narrow capitals, ruled header, coloured labels
        result.background = QColor(0xF4, 0xEE, 0xDD);
        result.text = QColor(0x2A, 0x25, 0x22);
        result.line = result.text;
        result.branches = {QColor(0xB2, 0x3A, 0x5A), QColor(0x2D, 0x5A, 0x57), QColor(0xE3, 0xB1, 0x3C)};
        result.caption = model.root() ? model.root()->title.simplified() : QString();
        result.titleFont = {QStringLiteral("Roboto"), QFont::Black, 75, true};
        result.itemFont = {QStringLiteral("Roboto"), QFont::Medium, 100, false};
        result.itemStyle = ItemStyle::Label;
        result.motif = Motif::Rules;
        break;
    case Theme::Editorial:  // marble white, serif titles, a frame of hairlines, red dots by the title
        result.background = QColor(0xF2, 0xF1, 0xEE);
        result.text = QColor(0x15, 0x15, 0x15);
        result.line = result.text;
        result.fill = QColor(0xE5, 0x48, 0x4D);
        result.titleFont = {QStringLiteral("Arvo"), QFont::Bold, 100, false};
        result.itemFont = {QStringLiteral("Montserrat"), QFont::Normal, 100, false};
        result.itemStyle = ItemStyle::Pill;
        result.motif = Motif::Frame;
        break;
    }
    if (theme >= Theme::Proposal) NodeItem::loadBundledFonts();  // the themes' own fonts
    return result;
}

Look lookOn(const Look &look, const Slide &slide) {
    Look result = look;
    const bool topic = slide.kind == Slide::Kind::Topic;
    const QColor background = slide.cover ? look.coverBackground : topic ? look.topicBackground : QColor();
    const QColor text = slide.cover ? look.coverText : topic ? look.topicText : QColor();
    if (background.isValid()) result.background = background;
    if (text.isValid()) result.text = result.title = text;
    if (!result.title.isValid()) result.title = result.text;
    return result;
}

namespace {
// WCAG 2.1 contrast ratio (https://www.w3.org/TR/WCAG21/#dfn-contrast-ratio).
qreal contrastOf(const QColor &a, const QColor &b) {
    const auto luminance = [](const QColor &c) {
        const auto linear = [](qreal v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * linear(c.redF()) + 0.7152 * linear(c.greenF()) + 0.0722 * linear(c.blueF());
    };
    const qreal la = luminance(a);
    const qreal lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
}  // namespace

QColor labelFill(const Look &look, int item) {
    return look.branches.isEmpty() ? look.line : look.branches[item % look.branches.size()];
}

QColor labelText(const Look &look, int item) {
    const QColor fill = labelFill(look, item);
    const QColor light(Qt::white);
    return contrastOf(look.text, fill) >= contrastOf(light, fill) ? look.text : light;
}

namespace {
// bold: a title (the theme's title type), otherwise an item.
QFont fontOf(const Look &look, qreal pixels, bool bold) {
    const FontSpec &spec = bold ? look.titleFont : look.itemFont;
    QFont font(spec.family.isEmpty() ? look.family : spec.family);
    font.setPixelSize(std::max(1, qRound(pixels)));
    font.setWeight(spec.weight >= 0 ? QFont::Weight(spec.weight) : bold ? QFont::Bold : QFont::Normal);
    font.setStretch(spec.stretch);  // Roboto's width axis (checked with Qt 6.12 on Windows, 2026-10-27)
    if (spec.uppercase) font.setCapitalization(QFont::AllUppercase);
    if (spec.spacing != 100) font.setLetterSpacing(QFont::PercentageSpacing, spec.spacing);
    return font;
}

// font, smaller (down to half) while breaksWord says a word of the entry would break inside (its
// layoutEntry wraps anywhere): a wrapped title or item never cuts a word, e.g. uppercase titles in
// a narrow box of a tall slide. The room counts the markers and a figure beside the text (BUG-034:
// measured against the whole box, a title with markers still broke). Only then a word breaks.
QFont fittedFont(QFont font, const std::function<bool(const QFont &)> &breaksWord) {
    const int full = font.pixelSize();
    while (font.pixelSize() > std::max(1, full / 2) && breaksWord(font)) font.setPixelSize(std::max(full / 2, int(font.pixelSize() * 0.9)));
    return font;
}

bool breaksWord(const EntryLayout &entry) { return entry.textFlags & Qt::TextWrapAnywhere; }

// A curly bracket opening to the right, from y0 to y1, its tip at x.
QPainterPath brace(qreal x, qreal width, qreal y0, qreal y1) {
    const qreal mid = (y0 + y1) / 2;
    const qreal stem = x + width / 2;
    const qreal r = std::min(width / 2, (y1 - y0) / 4);
    QPainterPath path(QPointF(x + width, y0));
    path.quadTo(stem, y0, stem, y0 + r);
    path.lineTo(stem, mid - r);
    path.quadTo(stem, mid, x, mid);
    path.quadTo(stem, mid, stem, mid + r);
    path.lineTo(stem, y1 - r);
    path.quadTo(stem, y1, x + width, y1);
    return path;
}

// Draws an entry where layoutEntry put its parts. The figure keeps the node's look (FEAT-040):
// the shadow and the border scale with it, the opacity multiplies the painter's.
// texts: when given, the text is captured there (as item) instead of drawn.
void paintEntry(QPainter *painter, const EntryLayout &layout, int alignment, const QFont &font, const QString &text,
                const Extras &extras, const QColor &textColor, QList<TextRun> *texts = nullptr, int item = -1) {
    if (!layout.figure.isEmpty()) {
        const ImageStyle &style = extras.figureStyle;
        const qreal scale = layout.figure.height() / extras.nodeFigureSize.height();
        if (style.shadow) {
            painter->save();
            painter->setPen(Qt::NoPen);
            const int alphas[] = {10, 8, 6, 4};
            for (int i = 0; i < 4; ++i) {
                const qreal grow = (i + 1) * scale;
                painter->setBrush(QColor(15, 23, 42, alphas[i]));
                painter->drawRoundedRect(layout.figure.adjusted(-grow, -grow + 2 * scale, grow, grow + 2 * scale), grow, grow);
            }
            painter->restore();
        }
        const qreal opacity = painter->opacity();
        painter->setOpacity(opacity * style.opacity / 100.0);
        painter->drawImage(layout.figure, extras.figure);
        painter->setOpacity(opacity);
        if (style.border != ImageStyle::Border::None) {
            const Qt::PenStyle pen = style.border == ImageStyle::Border::Dashed ? Qt::DashLine
                                     : style.border == ImageStyle::Border::Dotted ? Qt::DotLine : Qt::SolidLine;
            const qreal width = style.borderWidth * scale;
            painter->setPen(QPen(style.borderColor, width, pen, Qt::FlatCap, Qt::MiterJoin));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(layout.figure.adjusted(width / 2, width / 2, -width / 2, -width / 2));
        }
    }
    for (qsizetype i = 0; i < layout.markers.size(); ++i) Markers::paint(painter, layout.markers[i], extras.markers.value(i));
    painter->setFont(font);
    painter->setPen(textColor);
    const bool wrap = alignment & Qt::TextWordWrap;
    const int flags = layout.textFlags;
    const QString shown = wrap ? text : QFontMetricsF(font).elidedText(text, Qt::ElideRight, layout.text.width() + 1);
    if (texts) {
        texts->append({painter->transform().mapRect(layout.text), flags, font, textColor, shown, item});
        return;
    }
    painter->drawText(layout.text, flags, shown);
}

// The rows of a vertical list: lineHeight each, taller for a figure. When they would not fit in
// room, the figures shrink together (scale < 1) until they do.
struct Rows {
    QList<qreal> heights;
    qreal scale = 1;
    qreal total() const { return std::accumulate(heights.begin(), heights.end(), qreal(0)); }
};
Rows listRows(const Slide &slide, const QFont &font, qreal lineHeight, qreal width, qreal room, const QSizeF &figureCap) {
    const qreal padding = lineHeight - QFontMetricsF(font).height();
    const auto rowsAt = [&](qreal scale) {
        Rows rows;
        rows.scale = scale;
        for (qsizetype i = 0; i < slide.items.size(); ++i) {
            const Extras extras = slide.itemExtras.value(i);
            qreal height = lineHeight;
            if (extras.hasFigure()) {
                const EntryLayout entry = layoutEntry(QRectF(0, 0, width, 1e6), Qt::AlignLeft | Qt::AlignTop, font, slide.items[i], extras, figureCap, scale);
                height = std::max(lineHeight, entry.block.height() + padding);
            }
            rows.heights.append(height);
        }
        return rows;
    };
    Rows rows = rowsAt(1);
    if (rows.total() <= room) return rows;
    qreal low = 0;
    qreal high = 1;
    for (int step = 0; step < 16; ++step) {  // halving: the scale is right to 1/65536
        const qreal mid = (low + high) / 2;
        (rowsAt(mid).total() <= room ? low : high) = mid;
    }
    return rowsAt(low);
}
// A scalloped cloud: rect filled, with round bumps along its edges (Playful).
void paintCloud(QPainter *painter, const QRectF &rect, const QColor &color) {
    const qreal bump = std::max<qreal>(2, std::min(rect.width(), rect.height()) * 0.2);
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);  // the overlaps of the bumps stay filled
    path.addRect(rect);
    const auto along = [&](QPointF from, QPointF to) {
        const qreal length = QLineF(from, to).length();
        const int steps = std::max(2, int(length / (bump * 1.4)));
        for (int i = 0; i <= steps; ++i) path.addEllipse(from + (to - from) * (qreal(i) / steps), bump, bump);
    };
    along(rect.topLeft(), rect.topRight());
    along(rect.topRight(), rect.bottomRight());
    along(rect.bottomRight(), rect.bottomLeft());
    along(rect.bottomLeft(), rect.topLeft());
    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPath(path.simplified());
    painter->restore();
}

// A loose spiral and a four-pointed spark, drawn as a doodle around centre (Playful).
void paintDoodle(QPainter *painter, const QPointF &centre, qreal size, const QColor &color, qreal stroke) {
    QPainterPath spiral;
    for (int i = 0; i <= 60; ++i) {
        const qreal t = i / 60.0;
        const qreal angle = t * 4 * std::numbers::pi;
        const QPointF point = centre + QPointF(std::cos(angle), std::sin(angle)) * size * (0.15 + 0.85 * t);
        if (i == 0) spiral.moveTo(point);
        else spiral.lineTo(point);
    }
    painter->setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(spiral);
    const QPointF spark = centre + QPointF(size * 1.6, -size * 0.9);
    const qreal r = size * 0.35;
    QPainterPath star(spark + QPointF(0, -r));
    star.quadTo(spark, spark + QPointF(r, 0));
    star.quadTo(spark, spark + QPointF(0, r));
    star.quadTo(spark, spark + QPointF(-r, 0));
    star.quadTo(spark, spark + QPointF(0, -r));
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPath(star);
}

// An arrow pointing up and to the right, in a square of side size from its bottom-left corner.
void paintArrow(QPainter *painter, const QPointF &bottomLeft, qreal size, const QColor &color, qreal stroke) {
    const QPointF tip = bottomLeft + QPointF(size, -size);
    painter->setPen(QPen(color, stroke, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(bottomLeft, tip);
    painter->drawPolyline(QPolygonF({tip + QPointF(-size * 0.6, 0), tip, tip + QPointF(0, size * 0.6)}));
}

// A fine paper grain: a small tile of faint specks, made once and repeated (Paper). At most 7%
// black: the contrast of the text holds (PitchTest).
const QImage &grainTile() {
    static const QImage tile = [] {
        QImage image(128, 128, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QRandomGenerator random(63);  // the same grain every time
        for (int i = 0; i < 1400; ++i) {
            const int alpha = 4 + int(random.bounded(14));
            image.setPixelColor(int(random.bounded(128)), int(random.bounded(128)), QColor(0, 0, 0, alpha));
        }
        return image;
    }();
    return tile;
}

// A soft rounded shape around centre, radius about r (Soft).
QPainterPath blob(const QPointF &centre, qreal r) {
    QPainterPath path(centre + QPointF(r, 0));
    path.cubicTo(centre + QPointF(r, r * 0.7), centre + QPointF(r * 0.3, r * 1.1), centre + QPointF(-r * 0.4, r * 0.9));
    path.cubicTo(centre + QPointF(-r * 1.1, r * 0.7), centre + QPointF(-r * 1.0, -r * 0.4), centre + QPointF(-r * 0.5, -r * 0.8));
    path.cubicTo(centre + QPointF(0, -r * 1.2), centre + QPointF(r, -r * 0.8), centre + QPointF(r, 0));
    return path;
}

// The theme's decoration, behind the content and in the margins (stage B). A few shapes in
// proportion to the slide: sharp at any size, no image files.
void paintMotif(QPainter *painter, const QRectF &rect, const Look &look, const Slide &slide) {
    const qreal w = rect.width();
    const qreal h = rect.height();
    const qreal side = std::min(w, h);
    const bool dark = look.background.lightnessF() < 0.5;
    painter->save();
    switch (look.motif) {
    case Motif::None:
        break;
    case Motif::Band:  // a solid band along the right edge
        painter->fillRect(QRectF(rect.right() - w * 0.03, rect.top(), w * 0.03, h), look.line);
        break;
    case Motif::Clouds: {  // doodles in two corners; light ones on the coloured cover
        const QColor color = slide.cover ? look.fill : look.line;
        const qreal stroke = std::max<qreal>(1, side * 0.006);
        paintDoodle(painter, rect.topLeft() + QPointF(side * 0.07, side * 0.08), side * 0.045, color, stroke);
        paintDoodle(painter, rect.bottomRight() - QPointF(side * 0.16, side * 0.08), side * 0.045, color, stroke);
        break;
    }
    case Motif::Smudge: {  // a soft brush stroke in the bottom-right corner: the same curve, wider and fainter
        QPainterPath stroke(rect.bottomRight() + QPointF(-w * 0.34, h * 0.06));
        stroke.cubicTo(rect.bottomRight() + QPointF(-w * 0.22, -h * 0.30), rect.bottomRight() + QPointF(-w * 0.05, -h * 0.02),
                       rect.bottomRight() + QPointF(w * 0.02, -h * 0.42));
        for (int i = 0; i < kSmudgeLayers; ++i) {
            QColor color = dark ? QColor(255, 255, 255) : QColor(0, 0, 0);
            color.setAlphaF(dark ? kSmudgeAlphaDark : kSmudgeAlphaLight);
            painter->setPen(QPen(color, h * (0.22 - i * 0.015), Qt::SolidLine, Qt::RoundCap));
            painter->drawPath(stroke);
        }
        break;
    }
    case Motif::Bars: {  // solid bars on two edges and an arrow in the top-left corner
        const QColor bar = dark || slide.cover ? look.text : QColor(0x1E, 0x15, 0x13);
        painter->fillRect(QRectF(rect.right() - w * 0.42, rect.top(), w * 0.42, h * 0.035), bar);
        painter->fillRect(QRectF(rect.left(), rect.bottom() - h * 0.035, w * 0.3, h * 0.035), bar);
        paintArrow(painter, rect.topLeft() + QPointF(w * 0.035, h * 0.13), h * 0.04, slide.cover ? look.text : look.line,
                   std::max<qreal>(1.5, h * 0.005));
        break;
    }
    case Motif::Halftone: {  // dots fading out from two corners, and diagonal tiles
        const qreal step = side * 0.022;
        const qreal reach = side * 0.24;
        painter->setPen(Qt::NoPen);
        for (const QPointF &corner : {rect.topLeft(), rect.bottomRight()}) {
            const qreal sign = corner == rect.topLeft() ? 1 : -1;
            painter->setBrush(look.fill);
            for (qreal dy = step / 2; dy < reach; dy += step)
                for (qreal dx = step / 2; dx < reach - dy; dx += step) {
                    const qreal radius = step * 0.45 * (1 - (dx + dy) / reach);
                    painter->drawEllipse(corner + QPointF(dx, dy) * sign, radius, radius);
                }
            for (int i = 0; i < 3; ++i) {
                const QPointF centre = corner + QPointF(side * (0.05 + i * 0.07), side * (0.05 + (2 - i) * 0.025)) * sign;
                const qreal r = side * (0.035 - i * 0.007);
                painter->setBrush(i == 1 ? look.line : look.fill);
                painter->drawPolygon(QPolygonF({centre + QPointF(0, -r), centre + QPointF(r, 0), centre + QPointF(0, r), centre + QPointF(-r, 0)}));
            }
        }
        break;
    }
    case Motif::Grain:  // the grain, and a green highlight over the top-left margin
        painter->setBrushOrigin(rect.topLeft());
        painter->fillRect(rect, QBrush(grainTile()));
        painter->setPen(Qt::NoPen);
        painter->setBrush(look.fill);
        painter->drawRoundedRect(QRectF(rect.left() + w * 0.08, rect.top() + h * 0.025, w * 0.07, h * 0.014), h * 0.007, h * 0.007);
        break;
    case Motif::Lines: {  // a hairline above the bottom edge and an arrow in the top-right corner
        const qreal hairline = std::max<qreal>(1, h * 0.002);
        painter->setPen(QPen(look.line, hairline));
        painter->drawLine(QPointF(rect.left() + w * 0.06, rect.bottom() - h * 0.045), QPointF(rect.right() - w * 0.06, rect.bottom() - h * 0.045));
        paintArrow(painter, QPointF(rect.right() - w * 0.06 - h * 0.035, rect.top() + h * 0.085), h * 0.035, look.line,
                   std::max<qreal>(1, h * 0.003));
        break;
    }
    case Motif::Blobs:  // sage in the top-right corner, blush in the bottom-left one, half outside
        painter->setPen(Qt::NoPen);
        painter->setBrush(look.line);
        painter->drawPath(blob(rect.topRight() + QPointF(-side * 0.02, -side * 0.03), side * 0.15));
        painter->setBrush(QColor(0xE8, 0xC9, 0xB5));
        painter->drawPath(blob(rect.bottomLeft() + QPointF(side * 0.03, side * 0.03), side * 0.17));
        break;
    case Motif::Rules: {  // a ruled header with the central topic's title and the slide's number, a rule at the foot
        const qreal hairline = std::max<qreal>(1, h * 0.002);
        painter->setPen(QPen(look.line, hairline));
        painter->drawLine(QPointF(rect.left() + w * 0.04, rect.top() + h * 0.05), QPointF(rect.right() - w * 0.04, rect.top() + h * 0.05));
        painter->drawLine(QPointF(rect.left() + w * 0.04, rect.bottom() - h * 0.05), QPointF(rect.right() - w * 0.04, rect.bottom() - h * 0.05));
        QFont small = fontOf(look, h * 0.022, false);
        small.setCapitalization(QFont::AllUppercase);
        painter->setFont(small);
        painter->setPen(look.text);
        const QRectF header(rect.left() + w * 0.04, rect.top() + h * 0.012, w * 0.92, h * 0.034);
        painter->drawText(header, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetricsF(small).elidedText(look.caption, Qt::ElideRight, header.width() * 0.7));
        if (slide.number > 0) painter->drawText(header, Qt::AlignRight | Qt::AlignVCenter, QString::number(slide.number));
        break;
    }
    case Motif::Frame: {  // a frame of hairlines just inside the edges
        QColor frame = look.text;
        frame.setAlpha(70);
        painter->setPen(QPen(frame, std::max<qreal>(1, h * 0.002)));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(rect.adjusted(side * 0.025, side * 0.025, -side * 0.025, -side * 0.025));
        break;
    }
    }
    painter->restore();
}

// FEAT-089: image scaled to size, cropped from its centre to fill it. The scaled copies are kept
// (the preview and the presentation repaint the same slide often); a mutex because the exports may
// paint from another thread.
QImage coverScaled(const QImage &image, const QSize &size) {
    static QMutex mutex;
    static QCache<QString, QImage> cache(64 * 1024);  // KB: a 4K slide is about 33 MB
    const QString key = QStringLiteral("%1-%2x%3").arg(image.cacheKey()).arg(size.width()).arg(size.height());
    const QMutexLocker lock(&mutex);
    if (const QImage *hit = cache.object(key)) return *hit;
    const QImage scaled = image.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QImage result = scaled.copy(QRect(QPoint((scaled.width() - size.width()) / 2, (scaled.height() - size.height()) / 2), size));
    cache.insert(key, new QImage(result), std::max<qsizetype>(1, result.sizeInBytes() / 1024));
    return result;
}

void paintPicture(QPainter *painter, const QRectF &rect, const Look &look, const Slide &slide) {
    const QImage &picture = slide.cover || slide.kind == Slide::Kind::Topic ? (look.coverPicture.isNull() ? look.picture : look.coverPicture)
                                                                            : look.picture;
    if (picture.isNull() || rect.isEmpty()) return;
    const qreal scale = painter->device()->devicePixelRatioF() * std::sqrt(std::abs(painter->worldTransform().determinant()));
    const QSize pixels = (rect.size() * scale).toSize();
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    // A PDF, SVG or printer page: drawn from the picture itself, at the device's resolution, not from
    // a huge scaled copy. Screens and images use the cached copy, also larger than the picture (4K).
    if (painter->paintEngine()->type() != QPaintEngine::Raster || pixels.isEmpty()) {
        const QSizeF crop = rect.size().scaled(QSizeF(picture.size()), Qt::KeepAspectRatio);
        painter->drawImage(rect, picture, QRectF(QPointF((picture.width() - crop.width()) / 2, (picture.height() - crop.height()) / 2), crop));
    } else {
        painter->drawImage(rect, coverScaled(picture, pixels));
    }
    painter->restore();
}

// FEAT-089: the logo in its corner, the footer at the foot and the slide's number, over the slide.
void paintFoot(QPainter *painter, const QRectF &rect, const Look &look, const Slide &slide) {
    const bool number = look.numbers && slide.number > 0;
    if (look.logo.isNull() && look.footer.isEmpty() && !number) return;
    const qreal w = rect.width();
    const qreal h = rect.height();
    const qreal margin = std::min(w, h) * 0.035;
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    painter->setClipRect(rect);
    bool logoBottomLeft = false, logoBottomRight = false;
    if (!look.logo.isNull()) {
        QSizeF size(look.logo.width(), look.logo.height());
        // At most a fifth of the width: the footer, centred on half the width, keeps clear of it.
        size.scale(QSizeF(w * 0.2, h * std::clamp(look.logoSize, 0.02, 0.4)), Qt::KeepAspectRatio);
        const bool left = look.logoCorner == Corner::TopLeft || look.logoCorner == Corner::BottomLeft;
        const bool top = look.logoCorner == Corner::TopLeft || look.logoCorner == Corner::TopRight;
        const QPointF origin(left ? rect.left() + margin : rect.right() - margin - size.width(),
                             top ? rect.top() + margin : rect.bottom() - margin - size.height());
        painter->drawImage(QRectF(origin, size), look.logo);
        logoBottomLeft = !top && left;
        logoBottomRight = !top && !left;
    }
    const QFont font = fontOf(look, h * 0.028, false);
    painter->setFont(font);
    QColor ink = look.text;
    ink.setAlphaF(ink.alphaF() * 0.75);
    painter->setPen(ink);
    const qreal lineHeight = QFontMetricsF(font).height();
    const QRectF foot(rect.left() + margin, rect.bottom() - margin - lineHeight, w - 2 * margin, lineHeight);
    // The number in the corner the logo leaves free; the footer in the middle, elided to half the width.
    if (number)
        painter->drawText(foot, (logoBottomRight && !logoBottomLeft ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter,
                          QString::number(slide.number));
    if (!look.footer.isEmpty())
        painter->drawText(foot, Qt::AlignHCenter | Qt::AlignVCenter, QFontMetricsF(font).elidedText(look.footer.simplified(), Qt::ElideRight, w * 0.5));
    painter->restore();
}

void paintBody(QPainter *painter, const QRectF &rect, const Slide &slide, const Look &theme, int shown, qreal newest, QList<TextRun> *texts) {
    const Look look = lookOn(theme, slide);
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    painter->setClipRect(rect);
    painter->fillRect(rect, look.background);
    paintPicture(painter, rect, look, slide);
    paintMotif(painter, rect, look, slide);
    const qreal w = rect.width();
    const qreal h = rect.height();
    const qreal stroke = std::max<qreal>(1.5, h * 0.004);
    painter->setPen(look.text);
    // The title's font in box, fitted when it wraps.
    const auto titleFontIn = [&](const QRectF &box, int alignment, const QFont &titleFont, const QSizeF &cap) {
        if (!(alignment & Qt::TextWordWrap)) return titleFont;
        return fittedFont(titleFont, [&](const QFont &font) { return breaksWord(layoutEntry(box, alignment, font, slide.title, slide.titleExtras, cap)); });
    };
    // The title with its figure and markers; the figure at most cap (stage A2).
    const auto drawTitle = [&](const QRectF &box, int alignment, const QFont &titleFont, const QSizeF &cap) {
        const QFont font = titleFontIn(box, alignment, titleFont, cap);
        const EntryLayout entry = layoutEntry(box, alignment, font, slide.title, slide.titleExtras, cap);
        // A spot of red dots just after the title's first line, when it fits in the title's box (so
        // it never reaches the lines or the items).
        if (const qreal r = h * 0.05; look.motif == Motif::Frame && entry.text.right() + r * 2.3 <= box.right()) {
            const QPointF centre(entry.text.right() + r * 1.3, entry.text.top() + r * 0.6);
            const qreal step = r * 0.2;
            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(look.fill);
            for (qreal y = -r; y <= r; y += step)
                for (qreal x = -r; x <= r; x += step)
                    if (const qreal d = std::hypot(x, y); d < r) {
                        const qreal dot = step * 0.5 * std::sqrt(1 - d / r);
                        painter->drawEllipse(centre + QPointF(x, y), dot, dot);
                    }
            painter->restore();
        }
        paintEntry(painter, entry, alignment, font, slide.title, slide.titleExtras, look.title, texts);
        return entry;
    };
    if (slide.kind == Slide::Kind::Topic) {
        const QRectF box = rect.adjusted(w * 0.1, h * 0.1, -w * 0.1, -h * 0.1);
        const int alignment = Qt::AlignCenter | Qt::TextWordWrap;
        const QFont font = fontOf(look, h * (slide.cover ? 0.09 : 0.06), true);
        const QSizeF cap(w * 0.6, h * 0.5);
        if (look.motif == Motif::Clouds) {  // the title on a scalloped cloud
            const QRectF block = layoutEntry(box, alignment, titleFontIn(box, alignment, font, cap), slide.title, slide.titleExtras, cap).block;
            paintCloud(painter, block.adjusted(-h * 0.08, -h * 0.06, h * 0.08, h * 0.06), look.fill);
        }
        drawTitle(box, alignment, font, cap);
        painter->restore();
        return;
    }
    const QSizeF titleCap(w, h * 0.25);  // the title of a list slide
    const int count = int(slide.items.size());
    const int visible = shown < 0 ? count : std::clamp(shown, 0, count);
    // Opacity of item i: hidden until shown, the earlier ones dimmed while memorizing, the newest fading in.
    const auto opacity = [&](int i) -> qreal {
        if (i >= visible) return 0;
        if (shown >= 0 && i == visible - 1) return newest;
        if (shown >= 0 && slide.delivery == Delivery::OneMemorize) return kDimmedOpacity;
        return 1;
    };
    const auto lineColor = [&](int i) { return look.branches.isEmpty() ? look.line : look.branches[i % look.branches.size()]; };
    const QFont titleFont = fontOf(look, h * 0.06, true);
    // Stage B: items in a pill or on a badge need taller rows in the vertical lists.
    const qreal rowFactor = look.itemStyle == ItemStyle::Plain ? 1.6 : 2.0;
    // The items' font, fitted when they wrap: one size for all the items of the slide, so fitted
    // once per paint for the box width, flags and size the items share.
    struct FittedItems { qreal width = -1; int flags = 0; int pixels = 0; QFont font; } fittedItems;
    const auto itemFontIn = [&](const QRectF &box, int flags, const QFont &itemFont, const QSizeF &cap, qreal scale) {
        if (!(flags & Qt::TextWordWrap)) return itemFont;
        if (fittedItems.width == box.width() && fittedItems.flags == flags && fittedItems.pixels == itemFont.pixelSize()) return fittedItems.font;
        const QFont font = fittedFont(itemFont, [&](const QFont &candidate) {
            for (int j = 0; j < count; ++j)
                if (breaksWord(layoutEntry(box, flags, candidate, slide.items[j], slide.itemExtras.value(j), cap, scale))) return true;
            return false;
        });
        fittedItems = {box.width(), flags, itemFont.pixelSize(), font};
        return font;
    };
    // Item i in box with its figure (at most the box, or cap) and markers; styled: in the theme's
    // pill or on its badge (not on the grid's cards).
    const auto drawItem = [&](int i, const QRectF &box, int flags, const QFont &itemFont, QSizeF cap = {}, qreal scale = 1, bool styled = true) {
        if (cap.isEmpty()) cap = box.size();
        const QFont font = itemFontIn(box, flags, itemFont, cap, scale);
        const Extras extras = slide.itemExtras.value(i);
        const EntryLayout entry = layoutEntry(box, flags, font, slide.items[i], extras, cap, scale);
        painter->setOpacity(opacity(i));
        if (styled && look.itemStyle != ItemStyle::Plain) {
            const qreal line = QFontMetricsF(font).height();
            const QRectF frame = entry.block.adjusted(-line * 0.5, -line * 0.15, line * 0.5, line * 0.15);
            const bool pill = look.itemStyle == ItemStyle::Pill;
            const qreal radius = pill ? std::min(frame.height(), line * 1.3) / 2 : look.itemStyle == ItemStyle::Label ? line * 0.12 : line * 0.3;
            painter->setPen(pill ? QPen(look.line, std::max<qreal>(1, line * 0.05)) : QPen(Qt::NoPen));
            painter->setBrush(pill ? QBrush(Qt::NoBrush) : QBrush(look.itemStyle == ItemStyle::Label ? labelFill(look, i) : look.fill));
            painter->drawRoundedRect(frame, radius, radius);
            painter->setBrush(Qt::NoBrush);
        }
        const QColor text = styled && look.itemStyle == ItemStyle::Label ? labelText(look, i) : look.text;
        paintEntry(painter, entry, flags, font, slide.items[i], extras, text, texts, i);
        painter->setOpacity(1);
        return entry;
    };

    if (slide.layout == Layout::Org) {
        const QRectF titleBox(rect.left() + w * 0.1, rect.top() + h * 0.08, w * 0.8, h * 0.24);
        drawTitle(titleBox, Qt::AlignCenter | Qt::TextWordWrap, titleFont, titleCap);
        const qreal barY = rect.top() + h * 0.45;
        const qreal cellsTop = rect.top() + h * 0.55;
        const qreal cell = w * 0.86 / std::max(1, count);
        const auto cellCentre = [&](int i) { return rect.left() + w * 0.07 + cell * (i + 0.5); };
        painter->setPen(QPen(look.line, stroke));
        painter->drawLine(QPointF(rect.center().x(), titleBox.bottom() + h * 0.02), QPointF(rect.center().x(), barY));
        if (count > 1) painter->drawLine(QPointF(cellCentre(0), barY), QPointF(cellCentre(count - 1), barY));
        const QFont itemFont = fontOf(look, std::min(h * 0.04, cell * 0.12), false);
        for (int i = 0; i < count; ++i) {
            if (opacity(i) <= 0) continue;
            painter->setOpacity(opacity(i));
            painter->setPen(QPen(lineColor(i), stroke));
            painter->drawLine(QPointF(cellCentre(i), barY), QPointF(cellCentre(i), cellsTop - h * 0.02));
            painter->setOpacity(1);
            drawItem(i, QRectF(cellCentre(i) - cell * 0.45, cellsTop, cell * 0.9, h * 0.35), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, itemFont);
        }
        painter->restore();
        return;
    }

    if (slide.layout == Layout::Fishbone) {
        // Like the map's fishbone: the title is the head on the right, the bones go up and down
        // alternately along the spine, the first ones nearest the head.
        const QRectF head(rect.left() + w * 0.72, rect.top() + h * 0.3, w * 0.22, h * 0.4);
        drawTitle(head, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, fontOf(look, h * 0.05, true), titleCap);
        const qreal spineY = rect.center().y();
        const qreal spineRight = head.left() - w * 0.02;
        const qreal spineLeft = rect.left() + w * 0.06;
        painter->setPen(QPen(look.line, stroke * 1.5, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(spineLeft, spineY), QPointF(spineRight, spineY));
        const int pairs = std::max(1, (count + 1) / 2);
        const qreal step = (spineRight - spineLeft) / (pairs + 0.5);
        const QFont itemFont = fontOf(look, h * 0.035, false);
        for (int i = 0; i < count; ++i) {
            if (opacity(i) <= 0) continue;
            const bool up = i % 2 == 0;
            const QPointF root(spineRight - step * (i / 2 + 0.6), spineY);
            const QPointF tip(root.x() - w * 0.07, spineY + (up ? -h * 0.24 : h * 0.24));
            painter->setOpacity(opacity(i));
            painter->setPen(QPen(lineColor(i), stroke, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(root, tip);
            painter->setOpacity(1);
            const QRectF box(tip.x() - w * 0.1, up ? tip.y() - h * 0.14 : tip.y() + h * 0.01, w * 0.2, h * 0.13);
            drawItem(i, box, Qt::AlignHCenter | (up ? Qt::AlignBottom : Qt::AlignTop) | Qt::TextWordWrap, itemFont);
        }
        painter->restore();
        return;
    }

    if (slide.layout == Layout::Timeline) {
        // A line left to right with a dot per item, the items above and below it in turn.
        drawTitle(QRectF(rect.left() + w * 0.08, rect.top() + h * 0.06, w * 0.84, h * 0.2), Qt::AlignCenter | Qt::TextWordWrap, titleFont, titleCap);
        const qreal lineY = rect.top() + h * 0.58;
        const qreal left = rect.left() + w * 0.06;
        const qreal span = w * 0.88;
        painter->setPen(QPen(look.line, stroke * 1.5, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(left, lineY), QPointF(left + span, lineY));
        const qreal slot = span / std::max(1, count);
        const qreal textWidth = std::min(slot * 1.8, w * 0.24);
        const QFont itemFont = fontOf(look, h * 0.035, false);
        for (int i = 0; i < count; ++i) {
            if (opacity(i) <= 0) continue;
            const bool up = i % 2 == 0;
            const qreal x = left + slot * (i + 0.5);
            painter->setOpacity(opacity(i));
            painter->setPen(QPen(lineColor(i), stroke, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(QPointF(x, lineY), QPointF(x, lineY + (up ? -h * 0.07 : h * 0.07)));
            painter->setPen(Qt::NoPen);
            painter->setBrush(lineColor(i));
            painter->drawEllipse(QPointF(x, lineY), h * 0.012, h * 0.012);
            painter->setBrush(Qt::NoBrush);
            painter->setOpacity(1);
            // Kept inside the margins: in a tall slide the first and last items would cross the edges.
            const qreal margin = w * 0.07;
            const qreal left = std::clamp(x - textWidth / 2, rect.left() + margin, std::max(rect.left() + margin, rect.right() - margin - textWidth));
            const QRectF box(left, up ? lineY - h * 0.26 : lineY + h * 0.08, std::min(textWidth, w - 2 * margin), h * 0.18);
            drawItem(i, box, Qt::AlignHCenter | (up ? Qt::AlignBottom : Qt::AlignTop) | Qt::TextWordWrap, itemFont);
        }
        painter->restore();
        return;
    }

    if (slide.layout == Layout::Grid) {
        // Cards in two columns (up to four items) or three, under the title.
        drawTitle(QRectF(rect.left() + w * 0.08, rect.top() + h * 0.06, w * 0.84, h * 0.18), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, titleFont, titleCap);
        const int columns = count <= 4 ? 2 : 3;
        const int rows = std::max(1, (count + columns - 1) / columns);
        const QRectF area(rect.left() + w * 0.08, rect.top() + h * 0.3, w * 0.84, h * 0.62);
        const qreal gap = h * 0.025;
        const QSizeF card((area.width() - gap * (columns - 1)) / columns, std::min(h * 0.22, (area.height() - gap * (rows - 1)) / rows));
        const QFont itemFont = fontOf(look, std::min(h * 0.035, card.height() * 0.22), false);
        for (int i = 0; i < count; ++i) {
            if (opacity(i) <= 0) continue;
            const QRectF box(area.left() + (i % columns) * (card.width() + gap), area.top() + (i / columns) * (card.height() + gap), card.width(), card.height());
            QColor fill = lineColor(i);
            fill.setAlphaF(0.18);
            painter->setOpacity(opacity(i));
            painter->setPen(QPen(lineColor(i), stroke));
            painter->setBrush(fill);
            painter->drawRoundedRect(box, h * 0.015, h * 0.015);
            painter->setBrush(Qt::NoBrush);
            painter->setOpacity(1);
            drawItem(i, box.adjusted(card.width() * 0.06, card.height() * 0.08, -card.width() * 0.06, -card.height() * 0.08), Qt::AlignCenter | Qt::TextWordWrap, itemFont, {}, 1, false);
        }
        painter->restore();
        return;
    }

    if (slide.layout == Layout::Circle) {
        // The title in the middle, the items around it from the top, clockwise.
        const QRectF centreBox(rect.center().x() - w * 0.14, rect.center().y() - h * 0.12, w * 0.28, h * 0.24);
        const QRectF title = drawTitle(centreBox, Qt::AlignCenter | Qt::TextWordWrap, fontOf(look, h * 0.05, true), titleCap).block;
        const qreal rx = w * 0.34;
        const qreal ry = h * 0.36;
        const QFont itemFont = fontOf(look, h * 0.035, false);
        for (int i = 0; i < count; ++i) {
            if (opacity(i) <= 0) continue;
            const qreal angle = -std::numbers::pi / 2 + 2 * std::numbers::pi * i / std::max(1, count);
            const QPointF at(rect.center().x() + rx * std::cos(angle), rect.center().y() + ry * std::sin(angle));
            const QPointF ring(rect.center().x() + w * 0.15 * std::cos(angle), rect.center().y() + h * 0.13 * std::sin(angle));
            const QPointF to(rect.center().x() + (rx - w * 0.08) * std::cos(angle), rect.center().y() + (ry - h * 0.07) * std::sin(angle));
            // The line starts outside the title's block (a figure makes it larger than the ring).
            const QPointF centre = rect.center();
            const QPointF ray = to - centre;
            const qreal length = std::hypot(ray.x(), ray.y());
            const qreal exit = std::min(std::abs(ray.x()) > 0 ? title.width() / 2 * length / std::abs(ray.x()) : 1e9,
                                        std::abs(ray.y()) > 0 ? title.height() / 2 * length / std::abs(ray.y()) : 1e9);
            const QPointF ringRay = ring - centre;
            const qreal start = exit + h * 0.02;
            const QPointF from = start > std::hypot(ringRay.x(), ringRay.y()) ? centre + ray * (start / length) : ring;
            painter->setOpacity(opacity(i));
            painter->setPen(QPen(lineColor(i), stroke, Qt::SolidLine, Qt::RoundCap));
            if (start < length) painter->drawLine(from, to);
            painter->setOpacity(1);
            drawItem(i, QRectF(at.x() - w * 0.1, at.y() - h * 0.07, w * 0.2, h * 0.14), Qt::AlignCenter | Qt::TextWordWrap, itemFont);
        }
        painter->restore();
        return;
    }

    if (slide.layout == Layout::Bullets || slide.layout == Layout::Right || slide.layout == Layout::Numbered) {
        const bool right = slide.layout == Layout::Right;
        const bool numbered = slide.layout == Layout::Numbered;
        const int align = right ? Qt::AlignRight : Qt::AlignLeft;
        drawTitle(QRectF(rect.left() + w * 0.08, rect.top() + h * 0.06, w * 0.84, h * 0.2), align | Qt::AlignVCenter | Qt::TextWordWrap, titleFont, titleCap);
        const qreal top = rect.top() + h * 0.3;
        const qreal itemPx = std::clamp(h * 0.62 / (rowFactor * std::max(1, count)), h * 0.02, h * 0.04);
        const qreal lineH = itemPx * rowFactor;
        const QFont itemFont = fontOf(look, itemPx, false);
        const Rows rows = listRows(slide, itemFont, lineH, w * 0.79, h * 0.66, titleCap);
        qreal y = top;
        for (int i = 0; i < count; y += rows.heights[i], ++i) {
            if (opacity(i) <= 0) continue;
            const QRectF box = right ? QRectF(rect.left() + w * 0.08, y, w * 0.79, rows.heights[i]) : QRectF(rect.left() + w * 0.13, y, w * 0.79, rows.heights[i]);
            // The dot or the number on the line of the text, whatever the figure's side.
            const qreal textY = layoutEntry(box, align | Qt::AlignVCenter, itemFont, slide.items[i], slide.itemExtras.value(i), titleCap, rows.scale).text.center().y();
            const qreal dotX = right ? rect.right() - w * 0.10 : rect.left() + w * 0.10;
            painter->setOpacity(opacity(i));
            if (numbered) {  // "1." in the branch colour, right-aligned before the text
                painter->setFont(fontOf(look, itemPx, true));
                painter->setPen(lineColor(i));
                painter->drawText(QRectF(rect.left() + w * 0.04, textY - lineH / 2, w * 0.08, lineH), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("%1.").arg(i + 1));
            } else {
                painter->setPen(Qt::NoPen);
                painter->setBrush(lineColor(i));
                painter->drawEllipse(QPointF(dotX, textY), itemPx * 0.18, itemPx * 0.18);
                painter->setBrush(Qt::NoBrush);
            }
            painter->setOpacity(1);
            drawItem(i, box, align | Qt::AlignVCenter, itemFont, titleCap, rows.scale);
        }
        painter->restore();
        return;
    }

    // Bracket and fan: the title on the left, the items in a column on the right.
    const QRectF titleBox(rect.left() + w * 0.07, rect.top() + h * 0.1, w * 0.33, h * 0.8);
    drawTitle(titleBox, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, titleFont, titleCap);
    const qreal itemPx = std::clamp(h * 0.8 / (rowFactor * std::max(1, count)), h * 0.02, h * 0.04);
    const qreal lineH = itemPx * rowFactor;
    const QFont itemFont = fontOf(look, itemPx, false);
    const qreal itemsX = rect.left() + w * 0.5;
    const Rows rows = listRows(slide, itemFont, lineH, w * 0.43, h * 0.84, titleCap);
    const qreal total = rows.total();
    const qreal startY = rect.center().y() - total / 2;
    if (slide.layout == Layout::Bracket && count > 0) {
        painter->setPen(QPen(look.line, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(brace(rect.left() + w * 0.44, w * 0.025, startY + lineH * 0.15, startY + total - lineH * 0.15));
    }
    qreal y = startY;
    for (int i = 0; i < count; y += rows.heights[i], ++i) {
        if (opacity(i) <= 0) continue;
        const QRectF box(itemsX, y, w * 0.43, rows.heights[i]);
        if (slide.layout == Layout::Fan) {  // the branch ends at the line of the text
            const qreal textY = layoutEntry(box, Qt::AlignLeft | Qt::AlignVCenter, itemFont, slide.items[i], slide.itemExtras.value(i), titleCap, rows.scale).text.center().y();
            const QPointF from(rect.left() + w * 0.42, rect.center().y());
            const QPointF to(itemsX - w * 0.015, textY);
            QPainterPath branch(from);
            branch.cubicTo(QPointF((from.x() + to.x()) / 2, from.y()), QPointF((from.x() + to.x()) / 2, to.y()), to);
            painter->setOpacity(opacity(i));
            painter->setPen(QPen(lineColor(i), stroke, Qt::SolidLine, Qt::RoundCap));
            painter->drawPath(branch);
            painter->setOpacity(1);
        }
        drawItem(i, box, Qt::AlignLeft | Qt::AlignVCenter, itemFont, titleCap, rows.scale);
    }
    painter->restore();
}
}  // namespace

void paint(QPainter *painter, const QRectF &rect, const Slide &slide, const Look &theme, int shown, qreal newest, QList<TextRun> *texts) {
    paintBody(painter, rect, slide, theme, shown, newest, texts);
    paintFoot(painter, rect, lookOn(theme, slide), slide);
}

}  // namespace Pitch
