#include "mapscene.h"

#include "branchpath.h"
#include "formularendering.h"
#include "formulatext.h"
#include "imageframe.h"
#include "mapsnapshot.h"
#include "maplayout.h"
#include "mapstructure.h"
#include "markers.h"
#include "nodegeometry.h"
#include "mindmapmodel.h"
#include "nodeshape.h"
#include "nodesketch.h"
#include "numbering.h"
#include "nodestyle.h"
#include "performance.h"
#include "stickers.h"

#include <QAbstractTextDocumentLayout>
#include <QCache>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QImageReader>
#include <QPainter>
#include <QPen>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGGeometryNode>
#include <QSGOpacityNode>
#include <QSGTextNode>
#include <QSGVertexColorMaterial>
#include <QTextDocument>
#include <QTextLayout>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace MapScene {

namespace {

constexpr int kCapSegments = 8;     // a round cap is half a 16-sided circle
constexpr qreal kMiterLimit = 2;    // times the half width, as QPen's default miter limit

// NodeItem's loadScaledPixmap: decoded at the shown size times `scale` (never above the file's own
// pixels), the sticker's outline, then the rotation. imageSize is already the rotated size.
// The decoded pictures are shared by the builds of the sheet (each edit builds it again), so a
// picture is read once, not at every edit; cost in KB, the profile's QPixmapCache size (32 MB
// Standard, 10 MB Light), as the desktop's pictures.
// FEAT-116: a formula content is its box (FormulaRendering::boxImage, as NodeItem and the office
// export), drawn at `scale` and rotated, in the same cache.
constexpr qreal kMaxFormulaSidePx = 2048;

// The decoded and the framed pictures (FEAT-129) share one budget.
QCache<QString, QImage> &pictureCache() {
    static QCache<QString, QImage> cache(Performance::pixmapCacheKb());
    return cache;
}

QImage pictureOf(const MindNode *node, qreal scale) {
    QCache<QString, QImage> &cache = pictureCache();
    const int rotation = node->imageStyle.rotation;
    const QSizeF upright = rotation % 180 ? node->imageSize.transposed() : node->imageSize;
    const QSize target = (upright * scale).toSize().expandedTo(QSize(1, 1));
    if (!node->formula.isEmpty()) {
        // At most kMaxFormulaSidePx on the longest side: a large box at the phone's 6x would not
        // fit the cache (redrawn at every build) nor many GPUs' 4096 px textures.
        scale = std::min(scale, kMaxFormulaSidePx / std::max<qreal>(1, std::max(upright.width(), upright.height())));
        const QFont font = NodeGeometry::titleFont(node);
        const QString key = QStringLiteral("formula:%1:%2:%3:%4:%5x%6:%7:%8").arg(node->formula, font.key(),
            node->formulaBackground.name(QColor::HexArgb), node->text.name(QColor::HexArgb)).arg(upright.width()).arg(upright.height())
            .arg(scale).arg(rotation) + QLatin1Char(':') + ImageFrame::effectKey(node->imageStyle.effect);
        if (const QImage *cached = cache.object(key)) return *cached;
        QImage image = FormulaRendering::boxImage(node->formula, font, node->formulaBackground,
                                                  FormulaText::textColorOn(node->formulaBackground, node->text), upright, scale);
        if (image.isNull()) return image;
        image = ImageFrame::withEffect(image, node->imageStyle.effect);  // FEAT-131
        if (rotation) image = image.transformed(QTransform().rotate(rotation), Qt::SmoothTransformation);
        cache.insert(key, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() / 1024));
        return image;
    }
    const bool outline = node->imageSticker && node->imageStyle.outline;
    const QFileInfo info(node->imagePath);
    const QString key = QStringLiteral("%1:%2x%3:%4:%5:%6").arg(info.absoluteFilePath()).arg(target.width()).arg(target.height())
                            .arg(info.lastModified().toMSecsSinceEpoch()).arg(outline).arg(rotation)
                            + QLatin1Char(':') + ImageFrame::effectKey(node->imageStyle.effect);
    if (const QImage *cached = cache.object(key)) return *cached;
    QImageReader reader(node->imagePath);
    reader.setAutoTransform(true);
    const QSize source = reader.size();
    if (source.isValid() && (source.width() > target.width() || source.height() > target.height())) reader.setScaledSize(target);
    QImage image = reader.read();
    if (image.isNull()) return image;
    image = ImageFrame::withEffect(image, node->imageStyle.effect);  // FEAT-131, before the outline
    if (outline) image = Stickers::outlined(image);
    if (rotation) image = image.transformed(QTransform().rotate(rotation), Qt::SmoothTransformation);
    cache.insert(key, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() / 1024));
    return image;
}

// FEAT-129: as NodeItem, a transparent picture still shows its shadow and border.
bool shown(const ImageStyle &style) { return style.opacity > 0 || style.shadow || style.border != ImageStyle::Border::None; }

// FEAT-129: a picture with a shape, border or shadow, composed once at `scale` as NodeItem draws it
// (ImageFrame), with ImageFrame::reach around the box; the builds of the sheet share it.
QImage framedPictureOf(const QImage &picture, const QSizeF &box, const ImageStyle &style, qreal scale) {
    QCache<QString, QImage> &cache = pictureCache();
    const qreal reach = ImageFrame::reach(style);
    const QString key = QStringLiteral("%1:%2x%3:%4:%5:%6:%7:%8:%9").arg(picture.cacheKey()).arg(box.width()).arg(box.height())
                            .arg(scale).arg(int(style.shape)).arg(int(style.border)).arg(style.borderWidth)
                            .arg(style.borderColor.name(QColor::HexArgb)).arg(QStringLiteral("%1:%2").arg(style.shadow).arg(style.opacity));
    if (const QImage *cached = cache.object(key)) return *cached;
    QImage image(((box + QSizeF(2 * reach, 2 * reach)) * scale).toSize().expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        painter.scale(scale, scale);
        ImageFrame::paint(&painter, QRectF(QPointF(reach, reach), box), picture, style);
    }
    cache.insert(key, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() / 1024));
    return image;
}

// FEAT-051 on the phone: a marker drawn by Markers::paint into an image `pixels` wide, once per
// marker and size; the builds of the sheet share them. QImage, not QPixmap: this runs on the
// render thread (and on the GUI thread for the outliner, see topicPicture in mapscene.h).
QImage markerImage(const QString &marker, int pixels) {
    static QCache<QString, QImage> cache(1024);  // KB: a 108 px marker is 46 KB
    const QString key = marker + QLatin1Char('@') + QString::number(pixels);
    if (const QImage *cached = cache.object(key)) return *cached;
    QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        Markers::paint(&painter, QRectF(0, 0, pixels, pixels), marker);
    }
    cache.insert(key, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() / 1024));
    return image;
}

// FEAT-059 Show all notes: NodeItem::paint's note in the topic, drawn into an image at `scale`: a
// box a little lighter or darker than the topic, the note in the topic's text colour, clipped, "…"
// when cut.
QImage notePreviewImage(const MapSnapshot::Topic &topic, qreal scale) {
    const QRectF note = topic.geometry->notePreviewRect();
    QTextDocument *document = topic.geometry->notePreview();
    const QSize pixels(int(std::ceil(note.width() * scale)), int(std::ceil(note.height() * scale)));
    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (!document || pixels.isEmpty()) return image;
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    painter.scale(scale, scale);
    const QRectF box(QPointF(0, 0), note.size());
    const QColor text = topic.node->text;
    const bool lightText = NodeStyle::readableTextOn(text) != QColor(Qt::white);
    painter.setPen(Qt::NoPen);
    painter.setBrush(lightText ? QColor(255, 255, 255, 46) : QColor(15, 23, 42, 13));
    painter.drawRoundedRect(box, 6, 6);
    const qreal padding = NodeGeometry::kNotePreviewPaddingPx;
    const QRectF area = box.adjusted(padding, padding, -padding, -padding);
    painter.save();
    painter.translate(area.topLeft());
    painter.setClipRect(QRectF(QPointF(0, 0), area.size()));
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, text);
    context.clip = QRectF(QPointF(0, 0), area.size());
    document->documentLayout()->draw(&painter, context);
    painter.restore();
    if (document->size().height() > area.height() + 1) {
        painter.setPen(text);
        painter.drawText(QRectF(box.right() - 24, box.bottom() - 18, 18, 14), Qt::AlignRight | Qt::AlignBottom, QStringLiteral("…"));
    }
    return image;
}

// B3: NodeItem's note button (FEAT-059), and since FEAT-108 its web link button (FEAT-027), drawn
// into an image `pixels` wide: the floating groups' white with a 1 px rgba(15,23,42,13) border,
// the Hugeicons icon in `tint` (#27303b; FEAT-114: grey for a broken topic link). Made once per
// icon, colour and size.
QImage buttonImage(const QString &iconPath, int pixels, const QColor &tint = QColor(0x27, 0x30, 0x3b)) {
    static QCache<QString, QImage> cache(512);  // KB
    const QString key = iconPath + QLatin1Char('@') + QString::number(pixels) + tint.name();
    if (const QImage *cached = cache.object(key)) return *cached;
    QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    const qreal scale = pixels / NodeGeometry::kLinkButtonPx;
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(scale, scale);
        const QRectF frame = QRectF(0, 0, NodeGeometry::kLinkButtonPx, NodeGeometry::kLinkButtonPx).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(QColor(15, 23, 42, 13), 1));
        painter.setBrush(QColor(255, 255, 255, 214));
        painter.drawRoundedRect(frame, kNoteButtonRadiusPx, kNoteButtonRadiusPx);
    }
    // The icon tinted as NodeItem's tintedIconPixmap, read at its drawn pixels.
    const int iconPixels = int(std::ceil(kNoteIconPx * scale));
    QImageReader reader(iconPath);
    reader.setScaledSize(QSize(iconPixels, iconPixels));
    QImage icon = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (!icon.isNull()) {
        {
            QPainter tinting(&icon);
            tinting.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tinting.fillRect(icon.rect(), tint);
        }
        QPainter painter(&image);
        const qreal inset = (pixels - iconPixels) / 2.0;
        painter.drawImage(QPointF(inset, inset), icon);
    }
    cache.insert(key, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() / 1024));
    return image;
}

// FEAT-114: NodeItem's paper clip (FEAT-049): the button with the attachment icon and, with two
// files or more, their count (9+ above nine) in a small dark pill at the bottom right, 9 px
// DemiBold white text. The image covers the button and kFilesBadgeOverflowPx past its right and
// bottom edges, where the pill reaches.
QImage filesImage(int count, int pixels) {
    static QCache<QString, QImage> cache(256);  // KB
    const QString key = QString::number(count) + QLatin1Char('@') + QString::number(pixels);
    if (const QImage *cached = cache.object(key)) return *cached;
    const qreal scale = pixels / NodeGeometry::kLinkButtonPx;
    const int side = int(std::ceil((NodeGeometry::kLinkButtonPx + kFilesBadgeOverflowPx) * scale));
    QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.drawImage(QPointF(0, 0), buttonImage(QStringLiteral(":/assets/hugeicons/attachment.svg"), pixels));
        if (count > 1) {
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::TextAntialiasing);
            painter.scale(scale, scale);
            QFont small;
            small.setPixelSize(9);
            small.setWeight(QFont::DemiBold);
            const QString text = count > 9 ? QStringLiteral("9+") : QString::number(count);
            const qreal width = std::max<qreal>(12, QFontMetricsF(small).horizontalAdvance(text) + 4);
            const qreal button = NodeGeometry::kLinkButtonPx;
            const QRectF badge(button - width + 4, button - 8, width, 12);
            painter.setPen(QPen(Qt::white, 1));
            painter.setBrush(QColor(0x27, 0x30, 0x3b));
            painter.drawRoundedRect(badge, 6, 6);
            painter.setFont(small);
            painter.drawText(badge, Qt::AlignCenter, text);
        }
    }
    cache.insert(key, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() / 1024));
    return image;
}

// FEAT-108: NodeItem's checkbox (FEAT-048) in its kLinkButtonPx place, drawn into an image `pixels`
// wide. Empty: a 1.5 px border in the text colour; ticked: filled with the branch colour, the tick
// in the colour readable on it. Made once per state, colours and size.
// FEAT-116: a title with $...$ formulas, laid out by NodeGeometry (FormulaRendering::TitleFormula),
// drawn once into a transparent image `scale` times its size; the builds of the sheet share them.
// kTitleFormulaMarginPx around it: the desktop does not clip the drawing, and italic letters or
// integral signs reach past the measured size (FormulaText::toImage adds 2 px too).
constexpr qreal kTitleFormulaMarginPx = 2;

QImage titleFormulaImage(const MindNode *node, const FormulaRendering::TitleFormula &formula, qreal wrapWidth, qreal scale) {
    static QCache<QString, QImage> cache(4 * 1024);  // KB
    const QSizeF size = formula.size();
    const QString key = QStringLiteral("%1:%2:%3:%4x%5:%6:%7").arg(Numbering::withNumber(node->number, node->title), NodeGeometry::titleFont(node).key(),
        node->text.name(QColor::HexArgb)).arg(size.width()).arg(size.height()).arg(wrapWidth).arg(scale);
    if (const QImage *cached = cache.object(key)) return *cached;
    const QSizeF framed = size + 2 * QSizeF(kTitleFormulaMarginPx, kTitleFormulaMarginPx);
    QImage image((framed * scale).toSize().expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.scale(scale, scale);
        formula.draw(&painter, QRectF(QPointF(kTitleFormulaMarginPx, kTitleFormulaMarginPx), size), Qt::AlignLeft | Qt::AlignVCenter);
    }
    cache.insert(key, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() / 1024));
    return image;
}

QImage checkboxImage(const MindNode *node, int pixels) {
    static QCache<QString, QImage> cache(512);  // KB
    const bool done = node->task == MindNode::Task::Done;
    const QString key = QString::number(done) + (done ? node->branchColor : node->text).name(QColor::HexArgb) + QLatin1Char('@')
                        + QString::number(pixels);
    if (const QImage *cached = cache.object(key)) return *cached;
    QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(pixels / NodeGeometry::kLinkButtonPx, pixels / NodeGeometry::kLinkButtonPx);
        const qreal inset = (NodeGeometry::kLinkButtonPx - kTaskBoxPx) / 2;
        const QRectF check(inset, inset, kTaskBoxPx, kTaskBoxPx);
        if (done) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(node->branchColor);
            painter.drawRoundedRect(check, 4, 4);
            QPainterPath tick;
            tick.moveTo(check.left() + 3.5, check.center().y() + 0.5);
            tick.lineTo(check.left() + 6.5, check.bottom() - 4);
            tick.lineTo(check.right() - 3.5, check.top() + 4.5);
            painter.setPen(QPen(NodeStyle::readableTextOn(node->branchColor), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(tick);
        } else {
            painter.setPen(QPen(node->text, 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(check.adjusted(0.75, 0.75, -0.75, -0.75), 4, 4);
        }
    }
    cache.insert(key, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() / 1024));
    return image;
}

qreal cross(const QPointF &a, const QPointF &b) { return a.x() * b.y() - a.y() * b.x(); }

QPointF normalized(const QPointF &v) {
    const qreal length = std::hypot(v.x(), v.y());
    return length > 0 ? v / length : QPointF();
}

// The polygon's points without repeated neighbours and without the closing copy of the first one.
QList<QPointF> distinctPoints(const QPolygonF &polygon) {
    QList<QPointF> points;
    points.reserve(polygon.size());
    for (const QPointF &point : polygon)
        if (points.isEmpty() || QLineF(points.constLast(), point).length() > 1e-6) points.append(point);
    if (points.size() > 1 && QLineF(points.constFirst(), points.constLast()).length() <= 1e-6) points.removeLast();
    return points;
}

// QSGVertexColorMaterial expects premultiplied colours.
QSGGeometry::ColoredPoint2D vertex(const QPointF &point, const QColor &color) {
    QSGGeometry::ColoredPoint2D v;
    const int alpha = color.alpha();
    v.set(float(point.x()), float(point.y()), uchar(color.red() * alpha / 255), uchar(color.green() * alpha / 255),
          uchar(color.blue() * alpha / 255), uchar(alpha));
    return v;
}

void appendTriangle(Vertices &out, const QPointF &a, const QPointF &b, const QPointF &c, const QColor &color) {
    out.append(vertex(a, color));
    out.append(vertex(b, color));
    out.append(vertex(c, color));
}

bool insideTriangle(const QPointF &p, const QPointF &a, const QPointF &b, const QPointF &c) {
    const qreal d1 = cross(b - a, p - a), d2 = cross(c - b, p - b), d3 = cross(a - c, p - c);
    const bool negative = d1 < 0 || d2 < 0 || d3 < 0;
    const bool positive = d1 > 0 || d2 > 0 || d3 > 0;
    return !(negative && positive);
}

void appendRoundCap(Vertices &out, const QPointF &centre, const QPointF &from, qreal radius, const QColor &color) {
    // Half a circle turning counter-clockwise from `from` (a unit normal) to its opposite.
    const qreal start = std::atan2(from.y(), from.x());
    QPointF previous = centre + from * radius;
    for (int i = 1; i <= kCapSegments; ++i) {
        const qreal angle = start + std::numbers::pi * i / kCapSegments;
        const QPointF next = centre + QPointF(std::cos(angle), std::sin(angle)) * radius;
        appendTriangle(out, centre, previous, next, color);
        previous = next;
    }
}

}  // namespace

QList<QSGGeometryNode *> geometryNodes(const Vertices &vertices) {
    QList<QSGGeometryNode *> nodes;
    for (qsizetype first = 0; first < vertices.size(); first += kMaxNodeVertices) {
        const int count = int(std::min<qsizetype>(kMaxNodeVertices, vertices.size() - first));
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), count);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        std::memcpy(geometry->vertexDataAsColoredPoint2D(), vertices.constData() + first, count * sizeof(QSGGeometry::ColoredPoint2D));
        auto *node = new QSGGeometryNode;
        node->setGeometry(geometry);
        node->setMaterial(new QSGVertexColorMaterial);
        node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        nodes.append(node);
    }
    return nodes;
}

QList<QPolygonF> fillPolygons(const QPainterPath &path) {
    QList<QPolygonF> polygons = path.toFillPolygons(QTransform::fromScale(kFlattenScale, kFlattenScale));
    const QTransform back = QTransform::fromScale(1 / kFlattenScale, 1 / kFlattenScale);
    for (QPolygonF &polygon : polygons) polygon = back.map(polygon);
    return polygons;
}

QList<QPolygonF> strokePolylines(const QPainterPath &path) {
    QList<QPolygonF> polylines = path.toSubpathPolygons(QTransform::fromScale(kFlattenScale, kFlattenScale));
    const QTransform back = QTransform::fromScale(1 / kFlattenScale, 1 / kFlattenScale);
    for (QPolygonF &polyline : polylines) polyline = back.map(polyline);
    return polylines;
}

void appendFill(Vertices &out, const QPolygonF &polygon, const QColor &color) {
    const QList<QPointF> points = distinctPoints(polygon);
    if (points.size() < 3) return;
    qreal area = 0;
    for (qsizetype i = 0; i < points.size(); ++i) area += cross(points.at(i), points.at((i + 1) % points.size()));
    const qreal orientation = area >= 0 ? 1 : -1;
    QList<qsizetype> left(points.size());
    for (qsizetype i = 0; i < points.size(); ++i) left[i] = i;
    // Ear clipping: cut a convex corner whose triangle holds no other point, until three are left.
    qsizetype misses = 0;
    qsizetype i = 0;
    while (left.size() > 3 && misses < left.size()) {
        const qsizetype n = left.size();
        const QPointF &a = points.at(left.at((i + n - 1) % n));
        const QPointF &b = points.at(left.at(i % n));
        const QPointF &c = points.at(left.at((i + 1) % n));
        bool ear = cross(b - a, c - b) * orientation > 0;
        for (qsizetype k = 0; ear && k < n; ++k) {
            const qsizetype index = left.at(k);
            if (index == left.at((i + n - 1) % n) || index == left.at(i % n) || index == left.at((i + 1) % n)) continue;
            ear = !insideTriangle(points.at(index), a, b, c);
        }
        if (ear) {
            appendTriangle(out, a, b, c, color);
            left.removeAt(i % n);
            misses = 0;
        } else {
            ++i;
            ++misses;
        }
    }
    // Three points left, or a polygon that cannot be cut (it crosses itself): a fan of the rest.
    for (qsizetype k = 1; k + 1 < left.size(); ++k)
        appendTriangle(out, points.at(left.at(0)), points.at(left.at(k)), points.at(left.at(k + 1)), color);
}

void appendStroke(Vertices &out, const QPolygonF &polyline, qreal width, bool closed, bool roundCaps, const QColor &color) {
    const QList<QPointF> points = distinctPoints(polyline);
    const qsizetype n = points.size();
    if (n < 2 || width <= 0) return;
    closed = closed && n > 2;
    const qreal half = width / 2;
    const auto direction = [&](qsizetype from) { return normalized(points.at((from + 1) % n) - points.at(from)); };
    const auto normalOf = [](const QPointF &d) { return QPointF(-d.y(), d.x()); };
    // Offset of each point from the centre line: the mitre of its two segments.
    QList<QPointF> offsets(n);
    for (qsizetype i = 0; i < n; ++i) {
        const bool first = i == 0 && !closed, last = i == n - 1 && !closed;
        const QPointF before = normalOf(direction(first ? 0 : (i + n - 1) % n));
        const QPointF after = normalOf(direction(last ? n - 2 : i));
        const QPointF mitre = normalized(before + after);
        const qreal cosine = QPointF::dotProduct(mitre, after);
        const qreal length = std::min(cosine > 1e-6 ? half / cosine : half * kMiterLimit, half * kMiterLimit);
        offsets[i] = (mitre.isNull() ? after : mitre) * length;
    }
    const qsizetype segments = closed ? n : n - 1;
    for (qsizetype s = 0; s < segments; ++s) {
        const qsizetype a = s, b = (s + 1) % n;
        const QPointF la = points.at(a) + offsets.at(a), ra = points.at(a) - offsets.at(a);
        const QPointF lb = points.at(b) + offsets.at(b), rb = points.at(b) - offsets.at(b);
        appendTriangle(out, la, ra, lb, color);
        appendTriangle(out, ra, rb, lb, color);
    }
    if (roundCaps && !closed) {
        appendRoundCap(out, points.constFirst(), normalOf(direction(0)), half, color);
        appendRoundCap(out, points.constLast(), -normalOf(direction(n - 2)), half, color);
    }
}

QList<QPolygonF> dashes(const QPolygonF &polyline, int penStyle, qreal width) {
    return dashes(polyline, QPen(QBrush(Qt::black), width, static_cast<Qt::PenStyle>(penStyle)));
}

QList<QPolygonF> dashes(const QPolygonF &polyline, const QPen &pen) {
    const Qt::PenStyle style = pen.style();
    const qreal width = pen.widthF();
    if (style == Qt::NoPen || polyline.size() < 2) return {};
    const QList<qreal> pattern = pen.dashPattern();
    if (style == Qt::SolidLine || pattern.size() < 2) return {polyline};
    QList<QPolygonF> out;
    QPolygonF current;
    qsizetype entry = 0;
    qreal left = pattern.at(0) * width;  // of the current dash or gap
    bool drawing = true;
    current.append(polyline.first());
    for (qsizetype i = 1; i < polyline.size(); ++i) {
        QPointF from = polyline.at(i - 1);
        const QPointF to = polyline.at(i);
        qreal length = QLineF(from, to).length();
        while (length > 0) {
            const qreal step = std::min(left, length);
            const QPointF at = from + (to - from) * (step / length);
            if (drawing) current.append(at);
            from = at;
            length -= step;
            left -= step;
            if (left > 1e-9) continue;
            if (drawing && current.size() > 1) out.append(current);
            current.clear();
            drawing = !drawing;
            entry = (entry + 1) % pattern.size();
            left = pattern.at(entry) * width;
            if (drawing) current.append(from);
        }
    }
    if (drawing && current.size() > 1) out.append(current);
    return out;
}

namespace {

// QPen's square cap: an open line reaches half its width past each end.
QPolygonF squareCapped(QPolygonF line, qreal half) {
    const QList<QPointF> points = distinctPoints(line);
    if (points.size() < 2) return line;
    line = QPolygonF(points);
    line.first() -= normalized(points.at(1) - points.at(0)) * half;
    line.last() += normalized(points.last() - points.at(points.size() - 2)) * half;
    return line;
}

void appendCapped(Vertices &out, const QPolygonF &polyline, const QPen &pen, const QColor &color) {
    const qreal width = pen.widthF();
    switch (pen.capStyle()) {
    case Qt::RoundCap: appendStroke(out, polyline, width, false, true, color); break;
    case Qt::SquareCap: appendStroke(out, squareCapped(polyline, width / 2), width, false, false, color); break;
    default: appendStroke(out, polyline, width, false, false, color); break;
    }
}

QColor withOpacity(QColor color, qreal opacity) {
    if (opacity < 1) color.setAlphaF(color.alphaF() * opacity);
    return color;
}

void appendFilled(Vertices &out, const QPainterPath &path, const QColor &color) {
    for (const QPolygonF &polygon : fillPolygons(path)) appendFill(out, polygon, color);
}

// FEAT-028, NodeItem::paint's drawShape on a cell: the outer table's cell first (a table in a cell),
// then the cell, filled, its border on its edges (shared with the neighbours), square corners.
void appendCell(Vertices &out, const MapLayout::TableLook &cell, qreal opacity) {
    const auto rectangle = [&](const QRectF &rect, const QColor &fill, const QColor &border) {
        const QPolygonF corners{rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
        appendFill(out, corners, withOpacity(fill, opacity));
        appendStroke(out, corners, NodeGeometry::kTableBorderPx, true, false, withOpacity(border, opacity));
    };
    if (!cell.frame.isNull()) rectangle(cell.frame, cell.frameFill, cell.frameBorder);
    rectangle(cell.rect, cell.fill, cell.border);
}

}  // namespace

void appendPen(Vertices &out, const QPainterPath &path, const QPen &pen, const QColor &color) {
    if (pen.style() == Qt::NoPen || pen.widthF() <= 0 || path.isEmpty()) return;
    for (const QPolygonF &polyline : strokePolylines(path)) {
        if (pen.style() != Qt::SolidLine) {
            for (const QPolygonF &dash : dashes(polyline, pen)) appendCapped(out, dash, pen, color);
        } else if (polyline.isClosed()) {
            appendStroke(out, polyline, pen.widthF(), true, false, color);
        } else {
            appendCapped(out, polyline, pen, color);
        }
    }
}

void appendShape(Vertices &out, const MindNode *node, const QRectF &shape, qreal cornerRadius, const QColor &background, qreal opacity) {
    // NodeItem::paint's drawShape: the plain outline, or the freehand and pattern parts of
    // NodeSketch::drawing, with the same pens.
    QPen pen(node->border, node->borderWidth, NodeStyle::borderPenStyle(node->borderStyle));
    if (node->border.alpha() == 0) pen = Qt::NoPen;
    const bool penShown = pen.style() != Qt::NoPen && node->borderWidth > 0;
    const bool filled = node->fill.alpha() > 0;
    const QColor fill = withOpacity(node->fill, opacity);
    const QColor border = withOpacity(node->border, opacity);
    const bool underline = NodeShapes::isUnderline(node->shape);
    const bool open = NodeShapes::hasOpenBorder(node->shape);
    const bool freehandBorder = penShown && NodeSketch::isFreehand(node->borderStyle);
    const bool sketchedFill = filled && !underline
                              && (node->fillStyle == NodeFillStyle::PienoManoLibera || NodeStyle::isPatternFill(node->fillStyle));
    const QPainterPath filledMarks = penShown ? NodeShapes::filledMarks(node->shape, shape) : QPainterPath();
    NodeSketch::Drawing sketch;
    if (freehandBorder || sketchedFill)
        sketch = NodeSketch::drawing(node->shape, node->fillStyle, node->borderStyle, node->borderWidth, shape, cornerRadius,
                                     NodeSketch::seedFor(node->id));
    if (sketchedFill) {
        if (node->fillStyle == NodeFillStyle::PienoManoLibera) {
            appendFilled(out, sketch.area, fill);
        } else {
            const QColor color = withOpacity(NodeSketch::opaqueOver(node->fill, background), opacity);  // FEAT-036
            for (const QLineF &line : std::as_const(sketch.pattern.lines))
                appendStroke(out, QPolygonF{line.p1(), line.p2()}, sketch.pattern.penWidth, false, false, color);
        }
    }
    if (freehandBorder) {
        if (!sketchedFill && !underline && filled) appendFilled(out, open ? NodeShapes::outline(node->shape, shape) : sketch.borderArea, fill);
        // The desktop fills the stroke's outline: round caps and joins, the border's dashes.
        appendPen(out, sketch.borderLine, QPen(QBrush(border), node->borderWidth, pen.style(), Qt::RoundCap, Qt::RoundJoin), border);
        appendFilled(out, filledMarks, border);
        return;
    }
    const QPainterPath outline = NodeShapes::outline(node->shape, shape, cornerRadius);
    if (filled && !sketchedFill && !underline) appendFilled(out, outline, fill);
    if (!penShown) return;
    if (!open) appendPen(out, outline, pen, border);
    for (const QLineF &line : NodeShapes::borderLines(node->shape, shape, node->borderWidth)) {
        QPainterPath path(line.p1());
        path.lineTo(line.p2());
        appendPen(out, path, pen, border);
    }
    for (const QPainterPath &line : NodeShapes::innerLines(node->shape, shape, cornerRadius)) appendPen(out, line, pen, border);
    for (const QPainterPath &mark : NodeShapes::marks(node->shape, shape)) appendPen(out, mark, pen, border);
    appendFilled(out, filledMarks, border);
}

void appendBranch(Vertices &out, const MapSnapshot::Branch &branch, qreal opacity) {
    // EdgeItem::paint: the line (or the tapered area), then the marks filled, or stroked solid and round.
    if (branch.brush.style() != Qt::NoBrush) appendFilled(out, branch.path, withOpacity(branch.brush.color(), opacity));
    appendPen(out, branch.path, branch.pen, withOpacity(branch.pen.color(), opacity));
    const QColor mark = withOpacity(branch.markColor, opacity);
    appendFilled(out, branch.filledMarks, mark);
    appendPen(out, branch.strokedMarks, QPen(QBrush(mark), branch.markWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin), mark);
}

void appendRelation(Vertices &out, const MapSnapshot::Relation &relation, bool withLabel, qreal opacity) {
    Relationships::Look look = relation.look;
    if (opacity < 1) {  // FEAT-114: faded by a tag's highlight
        look.lineColor.setAlphaF(look.lineColor.alphaF() * opacity);
        look.backdrop.setAlphaF(look.backdrop.alphaF() * opacity);
    }
    if (look.linePattern != Qt::NoPen) {
        for (const QPolygonF &polyline : strokePolylines(relation.geometry.path))
            for (const QPolygonF &dash : dashes(polyline, look.linePattern, look.lineWidth))
                appendStroke(out, dash, look.lineWidth, false, false, look.lineColor);
        // RelationshipItem::paint: the marks solid, an open arrow stroked, the others filled.
        const auto mark = [&](BranchEnd kind, const QPointF &tip, const QPointF &outward) {
            if (kind == BranchEnd::None) return;
            const QPainterPath path = BranchPath::endMark(kind, tip, outward, look.lineWidth);
            if (BranchPath::endIsStroke(kind))
                for (const QPolygonF &polyline : strokePolylines(path))
                    appendStroke(out, polyline, look.lineWidth, polyline.isClosed(), true, look.lineColor);
            else
                for (const QPolygonF &polygon : fillPolygons(path)) appendFill(out, polygon, look.lineColor);
        };
        mark(look.startMark, relation.geometry.start, relation.geometry.startDirection);
        mark(look.endMark, relation.geometry.end, relation.geometry.endDirection);
    }
    if (withLabel && !relation.labelRect.isEmpty()) {
        QPainterPath box;
        box.addRoundedRect(relation.labelRect, Relationships::kLabelRadiusPx, Relationships::kLabelRadiusPx);
        for (const QPolygonF &polygon : fillPolygons(box)) appendFill(out, polygon, look.backdrop);
    }
}

void appendBoundary(Vertices &out, const MapSnapshot::BoundaryShape &shape, bool withTab, qreal opacity, const QColor &background) {
    const auto faded = [opacity](QColor color) {
        color.setAlphaF(color.alphaF() * opacity);
        return color;
    };
    for (const QPolygonF &polygon : fillPolygons(shape.fill.area)) appendFill(out, polygon, faded(shape.look.fill));
    // FEAT-134: a pattern's strokes as the topics' (appendShape).
    const QColor patternColor = faded(NodeSketch::opaqueOver(shape.look.fill, background));
    for (const QLineF &line : std::as_const(shape.fill.pattern.lines))
        appendStroke(out, QPolygonF{line.p1(), line.p2()}, shape.fill.pattern.penWidth, false, false, patternColor);
    const QColor line = faded(shape.look.pen.color());
    const QList<QPolygonF> border = shape.look.pen.style() == Qt::NoPen
                                        ? QList<QPolygonF>()
                                        : strokePolylines(Boundaries::border(shape.boundary.shape, shape.stroke));
    for (const QPolygonF &polyline : border) {
        if (shape.look.pen.style() == Qt::SolidLine) {
            appendStroke(out, polyline, shape.look.pen.widthF(), polyline.isClosed(), false, line);
            continue;
        }
        for (const QPolygonF &dash : dashes(polyline, shape.look.pen)) appendStroke(out, dash, shape.look.pen.widthF(), false, false, line);
    }
    // The tab has the border's colour; with no border (pattern none) it still shows.
    if (withTab && !shape.titleRect.isEmpty())
        for (const QPolygonF &polygon : fillPolygons(Boundaries::titlePath(shape.titleRect))) appendFill(out, polygon, line);
}

QSGTextNode *labelNode(QQuickWindow *window, const QFont &font, const QString &text, const QColor &color, const QRectF &box) {
    // QPainter::drawText(box, Qt::AlignCenter): the line centred in the box.
    QTextLayout layout(text, font);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid()) line.setLineWidth(1e6);
    layout.endLayout();
    if (!line.isValid()) return nullptr;
    const qreal width = line.naturalTextWidth(), height = line.ascent() + line.descent();
    QSGTextNode *node = window->createTextNode();
    node->setColor(color);
    node->setRenderType(QSGTextNode::QtRendering);
    node->addTextLayout(QPointF(box.center().x() - width / 2, box.center().y() - height / 2), &layout, -1, -1, 0, 1);
    return node;
}

namespace {
// FEAT-114: a tag pill's colours, as NodeItem::paint: light pills on a light map, inverted on a dark one.
struct PillLook {
    QColor fill, border, text;
};
PillLook pillLook(const QColor &background) {
    if (NodeStyle::isDarkBackground(background)) return {QColor(255, 255, 255, 20), QColor(255, 255, 255, 110), QColor(255, 255, 255, 220)};
    return {QColor(255, 255, 255, 230), QColor(15, 23, 42, 40), QColor(0x6b, 0x72, 0x80)};
}

// FEAT-114: a tag's text in its pill as drawLabelText: elided inside the padding, centred on the
// capital letters' height.
QSGTextNode *pillTextNode(QQuickWindow *window, const QString &label, const QColor &color, const QRectF &pill) {
    const QFont font = NodeGeometry::labelFont();
    const QFontMetricsF metrics(font);
    const QString text = metrics.elidedText(label, Qt::ElideRight, pill.width() - 2 * NodeGeometry::kLabelPaddingPx);
    QTextLayout layout(text, font);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid()) line.setLineWidth(1e6);
    layout.endLayout();
    if (!line.isValid()) return nullptr;
    const qreal baseline = pill.center().y() + metrics.capHeight() / 2;
    QSGTextNode *node = window->createTextNode();
    node->setColor(color);
    node->setRenderType(QSGTextNode::QtRendering);
    node->addTextLayout(QPointF(pill.center().x() - line.naturalTextWidth() / 2, baseline - line.ascent()), &layout, -1, -1, 0, 1);
    return node;
}
}  // namespace

Nodes build(const MapSnapshot &snapshot, QQuickWindow *window, const MindNode *withoutTitle, const MindNode *onlyTopic,
            const QString &withoutLabel, const QString &withoutRelation, const QString &highlight) {
    Nodes nodes;
    nodes.content = new QSGNode;
    const auto skipped = [onlyTopic](const MapSnapshot::Topic &topic) { return onlyTopic && topic.node != onlyTopic; };
    // FEAT-114: a faded item's triangles get a lower alpha; its pictures, icons and texts go under
    // one opacity node each (fadedImages, fadedTexts), so they stay in few batches.
    const bool highlighting = !highlight.isEmpty();
    const auto faded = [&](const MapSnapshot::Topic &topic) {
        return highlighting && std::none_of(topic.node->labels.cbegin(), topic.node->labels.cend(),
                                            [&](const QString &own) { return Labels::same(own, highlight); });
    };
    const auto fade = [](QColor color, bool on) {
        if (on) color.setAlphaF(color.alphaF() * kFadedOpacity);
        return color;
    };
    QSGOpacityNode *fadedImages = nullptr;
    const auto imageParent = [&](const MapSnapshot::Topic &topic) -> QSGNode * {
        if (!faded(topic)) return nodes.content;
        if (!fadedImages) {
            fadedImages = new QSGOpacityNode;
            fadedImages->setOpacity(kFadedOpacity);
            nodes.content->appendChildNode(fadedImages);
        }
        return fadedImages;
    };
    // FEAT-115: the boundaries under the lines (BoundaryItem's z value), nested ones above.
    Vertices boundaries;
    if (!onlyTopic)
        for (const MapSnapshot::BoundaryShape &shape : snapshot.boundaries())
            appendBoundary(boundaries, shape, shape.boundary.id != withoutLabel, highlighting ? kFadedOpacity : 1, snapshot.background());
    Vertices lines;
    // FEAT-115 step 2: the summaries' brackets above the boundaries and under the topics (z -2),
    // flat ends and mitred corners as the desktop's pen.
    if (!onlyTopic)
        for (const MapSnapshot::SummaryShape &summary : snapshot.summaries())
            for (const QPolygonF &polyline : strokePolylines(summary.bracket))
                appendStroke(lines, polyline, Summaries::kLineWidth, polyline.isClosed(), false, fade(summary.color, highlighting));
    // FEAT-028: a cell holding its topic's branch goes under the lines (NodeItem::kHoldingCellDepth).
    for (const MapSnapshot::Topic &topic : snapshot.topics())
        if (!skipped(topic) && topic.cell && topic.cell->holdsBranch) appendCell(lines, *topic.cell, faded(topic) ? kFadedOpacity : 1);
    if (!onlyTopic)
        for (const MapSnapshot::Branch &branch : snapshot.branches()) appendBranch(lines, branch, highlighting ? kFadedOpacity : 1);

    // NodeItem::paint's shape (FEAT-119 stage 0: patterns, freehand and dashed borders, inner lines, marks).
    Vertices shapes;
    for (const MapSnapshot::Topic &topic : snapshot.topics()) {
        if (skipped(topic)) continue;
        if (topic.cell) {  // FEAT-028: the cell instead of the shape
            if (!topic.cell->holdsBranch) appendCell(shapes, *topic.cell, faded(topic) ? kFadedOpacity : 1);
            continue;
        }
        const QRectF shape = topic.geometry->shapeRect().translated(topic.position);
        if (shape.isEmpty()) continue;
        appendShape(shapes, topic.node, shape, topic.geometry->cornerRadius(), snapshot.background(), faded(topic) ? kFadedOpacity : 1);
    }
    // FEAT-114: the tag pills under the shapes (NodeItem::paint): height 20, radius 10, 1 px border.
    const PillLook pills = pillLook(snapshot.background());
    for (const MapSnapshot::Topic &topic : snapshot.topics()) {
        if (skipped(topic)) continue;
        const bool dim = faded(topic);
        for (const QRectF &place : topic.geometry->labelRects()) {
            const QRectF pill = place.translated(topic.position).adjusted(0.5, 0.5, -0.5, -0.5);
            QPainterPath outline;
            const qreal radius = NodeGeometry::kLabelHeightPx / 2;
            outline.addRoundedRect(pill, radius, radius);
            for (const QPolygonF &polygon : fillPolygons(outline)) appendFill(shapes, polygon, fade(pills.fill, dim));
            for (const QPolygonF &polyline : strokePolylines(outline))
                appendStroke(shapes, polyline, 1, polyline.isClosed(), false, fade(pills.border, dim));
        }
    }
    for (const Vertices *vertices : {&boundaries, &lines, &shapes}) {
        if (vertices->isEmpty()) continue;
        for (QSGGeometryNode *node : geometryNodes(*vertices)) nodes.content->appendChildNode(node);
        nodes.vertexCount += int(vertices->size());
    }

    // The pictures (images and stickers) in the topic's image place, as NodeItem::paint. Sharp at
    // 100% on the phone's screen: at least its pixel ratio, as the desktop's imageScaleFactor.
    // FEAT-116: a formula is drawn, as NodeItem, from its vector drawing at twice that scale (sharp
    // when zoomed in). FEAT-129: a shape, border or shadow is composed into the texture with the
    // opacity (framedPictureOf), the texture then covering ImageFrame::reach around the box.
    const qreal pictureScale = std::max<qreal>(Performance::imageScaleFactor(), window->effectiveDevicePixelRatio());
    for (const MapSnapshot::Topic &topic : snapshot.topics()) {
        if (skipped(topic)) continue;
        const MindNode *node = topic.node;
        const ImageStyle &style = node->imageStyle;
        if ((node->imagePath.isEmpty() && node->formula.isEmpty()) || node->imageSize.isEmpty() || !shown(style)) continue;
        const qreal scale = node->formula.isEmpty() ? pictureScale : 2 * pictureScale;
        QImage image = pictureOf(node, scale);
        if (image.isNull()) continue;
        QRectF rect = topic.geometry->imageRect().translated(topic.position);
        const bool framed = !ImageFrame::isPlain(style);
        if (framed) {
            // A formula's picture may be decoded below `scale` (kMaxFormulaSidePx): composed at its own.
            const qreal used = node->formula.isEmpty() ? scale : image.width() / std::max<qreal>(1, rect.width());
            image = framedPictureOf(image, rect.size(), style, used);
            const qreal reach = ImageFrame::reach(style);
            rect.adjust(-reach, -reach, reach, reach);
        }
        QSGImageNode *picture = window->createImageNode();
        picture->setTexture(window->createTextureFromImage(image, QQuickWindow::TextureHasMipmaps));
        picture->setOwnsTexture(true);
        picture->setFiltering(QSGTexture::Linear);
        picture->setMipmapFiltering(QSGTexture::Linear);  // smooth below 100% too
        picture->setRect(rect);
        if (framed || style.opacity >= ImageStyle::kMaxOpacity) {
            imageParent(topic)->appendChildNode(picture);
            continue;
        }
        auto *opacity = new QSGOpacityNode;
        opacity->setOpacity(style.opacity / qreal(ImageStyle::kMaxOpacity));
        opacity->appendChildNode(picture);
        imageParent(topic)->appendChildNode(opacity);
    }

    // The markers in their places beside the title, as NodeItem::paint: kMarkerDrawPx inside each
    // kLinkButtonPx square, from an image sharp up to kMarkerSharpZoom on this screen.
    const int markerPixels = int(std::ceil(kMarkerDrawPx * window->effectiveDevicePixelRatio() * kMarkerSharpZoom));
    for (const MapSnapshot::Topic &topic : snapshot.topics()) {
        if (skipped(topic) || topic.node->markers.isEmpty()) continue;
        const QList<QRectF> places = topic.geometry->markerRects();
        for (qsizetype i = 0; i < places.size() && i < topic.node->markers.size(); ++i) {
            const QImage image = markerImage(topic.node->markers.at(i), markerPixels);
            QSGImageNode *marker = window->createImageNode();
            marker->setTexture(window->createTextureFromImage(image, QQuickWindow::TextureHasMipmaps));
            marker->setOwnsTexture(true);
            marker->setFiltering(QSGTexture::Linear);
            marker->setMipmapFiltering(QSGTexture::Linear);
            const qreal inset = (NodeGeometry::kLinkButtonPx - kMarkerDrawPx) / 2;
            marker->setRect(places.at(i).translated(topic.position).adjusted(inset, inset, -inset, -inset));
            imageParent(topic)->appendChildNode(marker);
        }
    }

    // B3: the note's button where the desktop draws it (NodeGeometry::noteRect, before the links);
    // FEAT-108: the web link's button and the checkbox in their places too.
    const int buttonPixels = int(std::ceil(NodeGeometry::kLinkButtonPx * window->effectiveDevicePixelRatio() * kMarkerSharpZoom));
    const MapSnapshot::Topic *buttonsOf = nullptr;  // the topic whose buttons are appended
    const auto appendImage = [&](const QImage &image, const QRectF &rect) {
        QSGImageNode *button = window->createImageNode();
        button->setTexture(window->createTextureFromImage(image, QQuickWindow::TextureHasMipmaps));
        button->setOwnsTexture(true);
        button->setFiltering(QSGTexture::Linear);
        button->setMipmapFiltering(QSGTexture::Linear);
        button->setRect(rect);
        imageParent(*buttonsOf)->appendChildNode(button);
    };
    for (const MapSnapshot::Topic &topic : snapshot.topics()) {
        if (skipped(topic)) continue;
        buttonsOf = &topic;
        if (const QRectF place = topic.geometry->notePreviewRect(); !place.isEmpty() && topic.geometry->notePreview())
            appendImage(notePreviewImage(topic, window->effectiveDevicePixelRatio() * kMarkerSharpZoom), place.translated(topic.position));
        if (const QRectF place = topic.geometry->noteRect(); !place.isEmpty())
            appendImage(buttonImage(QStringLiteral(":/assets/hugeicons/note.svg"), buttonPixels), place.translated(topic.position));
        if (const QRectF place = topic.geometry->linkIconRect(true); !place.isEmpty())  // FEAT-114, grey when broken
            appendImage(buttonImage(QStringLiteral(":/assets/hugeicons/link.svg"), buttonPixels,
                                    snapshot.linkBroken(topic.node) ? QColor(0x6b, 0x72, 0x80) : QColor(0x27, 0x30, 0x3b)),
                        place.translated(topic.position));
        if (const QRectF place = topic.geometry->linkIconRect(false); !place.isEmpty())
            appendImage(buttonImage(QStringLiteral(":/assets/hugeicons/link-web.svg"), buttonPixels), place.translated(topic.position));
        if (const QRectF place = topic.geometry->taskRect(); !place.isEmpty())
            appendImage(checkboxImage(topic.node, buttonPixels), place.translated(topic.position));
        if (const QRectF place = topic.geometry->filesRect(); !place.isEmpty())  // FEAT-114
            appendImage(filesImage(int(topic.node->files.size()), buttonPixels),
                        place.translated(topic.position).adjusted(0, 0, kFilesBadgeOverflowPx, kFilesBadgeOverflowPx));
    }

    // FEAT-110: the relationships above the topics (RelationshipItem's z value), their labels with
    // the titles below.
    Vertices relations;
    if (!onlyTopic)
        for (const MapSnapshot::Relation &relation : snapshot.relations())
            if (relation.relationship.id != withoutRelation)
                appendRelation(relations, relation, relation.relationship.id != withoutLabel, highlighting ? kFadedOpacity : 1);
    if (!relations.isEmpty()) {
        for (QSGGeometryNode *node : geometryNodes(relations)) nodes.content->appendChildNode(node);
        nodes.vertexCount += int(relations.size());
    }

    // The titles, placed as NodeItem::paint places them: the formatted document centred in the text
    // area inside the padding, or the plain layout's lines centred and aligned as the topic says.
    nodes.text = new QSGNode;
    nodes.content->appendChildNode(nodes.text);
    QSGOpacityNode *fadedTexts = nullptr;
    const auto appendText = [&](QSGNode *text, bool dim) {
        if (!dim) {
            nodes.text->appendChildNode(text);
            return;
        }
        if (!fadedTexts) {
            fadedTexts = new QSGOpacityNode;
            fadedTexts->setOpacity(kFadedOpacity);
            nodes.text->appendChildNode(fadedTexts);
        }
        fadedTexts->appendChildNode(text);
    };
    for (const MapSnapshot::Topic &topic : snapshot.topics()) {
        const MindNode *node = topic.node;
        if (node == withoutTitle || skipped(topic)) continue;
        const NodeGeometry &geometry = *topic.geometry;
        const QRectF textBox = geometry.textArea().translated(topic.position);
        if (const FormulaRendering::TitleFormula *formula = geometry.formula(); formula && !textBox.isEmpty()) {
            // FEAT-116: placed as NodeItem::paint places it (6 px above and below, the left inset
            // at most the padding, 11 px on the right when aligned left; justified = left).
            const QSizeF size = formula->size();
            const qreal inset = std::min(geometry.padX(), (textBox.width() - size.width()) / 2);
            Qt::Alignment horizontal = NodeGeometry::textAlignment(node) & Qt::AlignHorizontal_Mask;
            if (horizontal == Qt::AlignJustify) horizontal = Qt::AlignLeft;
            const QRectF box = textBox.adjusted(inset, 6, horizontal == Qt::AlignLeft ? -11 : -inset, -6);
            const qreal left = horizontal & Qt::AlignHCenter ? box.center().x() - size.width() / 2
                             : horizontal & Qt::AlignRight   ? box.right() - size.width() : box.left();
            QSGImageNode *picture = window->createImageNode();
            picture->setTexture(window->createTextureFromImage(
                titleFormulaImage(node, *formula, textBox.width(), window->effectiveDevicePixelRatio() * kMarkerSharpZoom),
                QQuickWindow::TextureHasMipmaps));
            picture->setOwnsTexture(true);
            picture->setFiltering(QSGTexture::Linear);
            picture->setMipmapFiltering(QSGTexture::Linear);
            picture->setRect(QRectF(QPointF(left, box.center().y() - size.height() / 2), size)
                                 .adjusted(-kTitleFormulaMarginPx, -kTitleFormulaMarginPx, kTitleFormulaMarginPx, kTitleFormulaMarginPx));
            appendText(picture, faded(topic));
            continue;
        }
        QSGTextNode *text = nullptr;
        const auto textNode = [&] {
            text = window->createTextNode();
            text->setColor(node->text);
            text->setRenderType(QSGTextNode::QtRendering);
            return text;
        };
        if (QTextDocument *rich = geometry.richDocument()) {
            const QRectF target = textBox.adjusted(geometry.padX(), geometry.padY(), -geometry.padX(), -geometry.padY());
            textNode()->addTextDocument(QPointF(target.left(), target.center().y() - rich->size().height() / 2), rich);
        } else if (const QTextLayout *layout = geometry.plainLayout(); layout && layout->lineCount() > 0) {
            qreal height = 0, width = 0;
            for (int i = 0; i < layout->lineCount(); ++i) {
                const QTextLine line = layout->lineAt(i);
                height = line.y() + line.ascent() + line.descent();
                width = std::max(width, line.naturalTextWidth());
            }
            const qreal left = textBox.center().x() - width / 2;
            const qreal top = textBox.center().y() - height / 2;
            const qreal factor = node->textAlign == TextAlign::Centro ? 0.5 : node->textAlign == TextAlign::Destra ? 1.0 : 0.0;
            textNode();
            // addTextLayout does not change the layout: it reads its glyphs.
            auto *glyphs = const_cast<QTextLayout *>(layout);
            for (int i = 0; i < layout->lineCount(); ++i)
                text->addTextLayout(QPointF(left + (width - layout->lineAt(i).naturalTextWidth()) * factor, top), glyphs, -1, -1, i, 1);
        }
        if (text) appendText(text, faded(topic));
    }
    for (const MapSnapshot::Topic &topic : snapshot.topics()) {  // FEAT-114: the tags' texts
        if (skipped(topic)) continue;
        const QList<QRectF> places = topic.geometry->labelRects();
        for (qsizetype i = 0; i < places.size() && i < topic.node->labels.size(); ++i)
            if (QSGTextNode *label = pillTextNode(window, topic.node->labels.at(i), pills.text, places.at(i).translated(topic.position)))
                appendText(label, faded(topic));
    }
    if (!onlyTopic)  // FEAT-115: the boundaries' titles, centred in their tabs (BoundaryItem::paint)
        for (const MapSnapshot::BoundaryShape &shape : snapshot.boundaries())
            if (!shape.titleRect.isEmpty() && shape.boundary.id != withoutLabel)
                if (QSGTextNode *title = labelNode(window, shape.look.font, shape.boundary.title, shape.look.titleText, shape.titleRect))
                    appendText(title, highlighting);
    if (!onlyTopic)
        for (const MapSnapshot::Relation &relation : snapshot.relations())
            if (!relation.labelRect.isEmpty() && relation.relationship.id != withoutLabel && relation.relationship.id != withoutRelation)
                if (QSGTextNode *label = labelNode(window, relation.look.font, relation.relationship.label, relation.look.textColor, relation.labelRect))
                    appendText(label, highlighting);
    return nodes;
}

void paint(QPainter *painter, const MapSnapshot &snapshot, qreal imageScale) {
    QPainter &p = *painter;
    p.save();
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    const qreal scale = std::max<qreal>(1, imageScale);
    const auto image = [&](const QImage &picture, const QRectF &rect) {
        if (!picture.isNull()) p.drawImage(rect, picture);
    };
    // BoundaryItem::paint: the body filled and stroked, the tab in the border's colour.
    for (const MapSnapshot::BoundaryShape &shape : snapshot.boundaries()) {
        p.fillPath(shape.fill.area, shape.look.fill);
        if (!shape.fill.pattern.lines.isEmpty()) {
            p.setPen(QPen(NodeSketch::opaqueOver(shape.look.fill, snapshot.background()), shape.fill.pattern.penWidth, Qt::SolidLine, Qt::FlatCap));
            p.drawLines(shape.fill.pattern.lines);
        }
        if (shape.look.pen.style() != Qt::NoPen) p.strokePath(Boundaries::border(shape.boundary.shape, shape.stroke), shape.look.pen);
        if (!shape.titleRect.isEmpty()) p.fillPath(Boundaries::titlePath(shape.titleRect), shape.look.pen.color());
    }
    // The summaries' brackets (MindMapScene's pen).
    for (const MapSnapshot::SummaryShape &summary : snapshot.summaries())
        p.strokePath(summary.bracket, QPen(summary.color, Summaries::kLineWidth, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
    // FEAT-028: NodeItem::paint's cells, the outer table's cell first; square corners.
    const auto cell = [&](const MapLayout::TableLook &look) {
        const auto rectangle = [&](const QRectF &rect, const QColor &fill, const QColor &border) {
            p.setPen(QPen(border, NodeGeometry::kTableBorderPx, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
            p.setBrush(fill);
            p.drawRect(rect);
        };
        if (!look.frame.isNull()) rectangle(look.frame, look.frameFill, look.frameBorder);
        rectangle(look.rect, look.fill, look.border);
    };
    for (const MapSnapshot::Topic &topic : snapshot.topics())
        if (topic.cell && topic.cell->holdsBranch) cell(*topic.cell);
    // EdgeItem::paint: the line (or the tapered area), then the marks.
    for (const MapSnapshot::Branch &branch : snapshot.branches()) {
        if (branch.brush.style() != Qt::NoBrush) p.fillPath(branch.path, branch.brush.color());
        if (branch.pen.style() != Qt::NoPen && branch.pen.widthF() > 0) p.strokePath(branch.path, branch.pen);
        p.fillPath(branch.filledMarks, branch.markColor);
        if (!branch.strokedMarks.isEmpty())
            p.strokePath(branch.strokedMarks, QPen(branch.markColor, branch.markWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    // NodeItem::paint's drawShape.
    for (const MapSnapshot::Topic &topic : snapshot.topics()) {
        if (topic.cell) {
            if (!topic.cell->holdsBranch) cell(*topic.cell);
            continue;
        }
        const QRectF shape = topic.geometry->shapeRect().translated(topic.position);
        if (shape.isEmpty()) continue;
        const MindNode *node = topic.node;
        const qreal radius = topic.geometry->cornerRadius();
        QPen pen(node->border, node->borderWidth, NodeStyle::borderPenStyle(node->borderStyle));
        if (node->border.alpha() == 0 || node->borderWidth <= 0) pen = Qt::NoPen;
        const QBrush brush = node->fill.alpha() == 0 ? QBrush(Qt::NoBrush) : QBrush(node->fill);
        const bool underline = NodeShapes::isUnderline(node->shape);
        const bool open = NodeShapes::hasOpenBorder(node->shape);
        const bool freehandBorder = pen.style() != Qt::NoPen && NodeSketch::isFreehand(node->borderStyle);
        const bool sketchedFill = brush.style() != Qt::NoBrush && !underline
                                  && (node->fillStyle == NodeFillStyle::PienoManoLibera || NodeStyle::isPatternFill(node->fillStyle));
        const QPainterPath filledMarks = pen.style() == Qt::NoPen ? QPainterPath() : NodeShapes::filledMarks(node->shape, shape);
        NodeSketch::Drawing sketch;
        if (freehandBorder || sketchedFill)
            sketch = NodeSketch::drawing(node->shape, node->fillStyle, node->borderStyle, node->borderWidth, shape, radius, NodeSketch::seedFor(node->id));
        if (sketchedFill) {
            if (node->fillStyle == NodeFillStyle::PienoManoLibera) {
                p.fillPath(sketch.area, brush);
            } else {
                p.setPen(QPen(NodeSketch::opaqueOver(node->fill, snapshot.background()), sketch.pattern.penWidth, Qt::SolidLine, Qt::FlatCap));
                p.drawLines(sketch.pattern.lines);
            }
        }
        if (freehandBorder) {
            if (!sketchedFill && !underline && brush.style() != Qt::NoBrush)
                p.fillPath(open ? NodeShapes::outline(node->shape, shape) : sketch.borderArea, brush);
            p.strokePath(sketch.borderLine, QPen(node->border, node->borderWidth, pen.style(), Qt::RoundCap, Qt::RoundJoin));
            p.fillPath(filledMarks, node->border);
            continue;
        }
        p.setPen(open ? QPen(Qt::NoPen) : pen);
        p.setBrush(sketchedFill ? QBrush(Qt::NoBrush) : brush);
        p.drawPath(NodeShapes::outline(node->shape, shape, radius));
        if (pen.style() == Qt::NoPen) continue;
        p.setPen(pen);
        for (const QLineF &line : NodeShapes::borderLines(node->shape, shape, node->borderWidth)) p.drawLine(line);
        p.setBrush(Qt::NoBrush);
        for (const QPainterPath &line : NodeShapes::innerLines(node->shape, shape, radius)) p.drawPath(line);
        for (const QPainterPath &mark : NodeShapes::marks(node->shape, shape)) p.drawPath(mark);
        p.fillPath(filledMarks, node->border);
    }
    // FEAT-114: the tag pills.
    const PillLook pills = pillLook(snapshot.background());
    for (const MapSnapshot::Topic &topic : snapshot.topics())
        for (const QRectF &place : topic.geometry->labelRects()) {
            p.setPen(QPen(pills.border, 1));
            p.setBrush(pills.fill);
            const qreal radius = NodeGeometry::kLabelHeightPx / 2;
            p.drawRoundedRect(place.translated(topic.position).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        }
    // The pictures, markers and buttons in their places, as build().
    for (const MapSnapshot::Topic &topic : snapshot.topics()) {
        const MindNode *node = topic.node;
        if ((node->imagePath.isEmpty() && node->formula.isEmpty()) || node->imageSize.isEmpty() || !shown(node->imageStyle)) continue;
        const QImage picture = pictureOf(node, node->formula.isEmpty() ? scale : 2 * scale);
        if (!picture.isNull()) ImageFrame::paint(&p, topic.geometry->imageRect().translated(topic.position), picture, node->imageStyle);
    }
    const int buttonPixels = int(std::ceil(NodeGeometry::kLinkButtonPx * scale * kMarkerSharpZoom));
    for (const MapSnapshot::Topic &topic : snapshot.topics()) {
        const QList<QRectF> places = topic.geometry->markerRects();
        const qreal inset = (NodeGeometry::kLinkButtonPx - kMarkerDrawPx) / 2;
        for (qsizetype i = 0; i < places.size() && i < topic.node->markers.size(); ++i)
            Markers::paint(&p, places.at(i).translated(topic.position).adjusted(inset, inset, -inset, -inset), topic.node->markers.at(i));
        if (const QRectF place = topic.geometry->notePreviewRect(); !place.isEmpty() && topic.geometry->notePreview())
            image(notePreviewImage(topic, scale * kMarkerSharpZoom), place.translated(topic.position));
        if (const QRectF place = topic.geometry->noteRect(); !place.isEmpty())
            image(buttonImage(QStringLiteral(":/assets/hugeicons/note.svg"), buttonPixels), place.translated(topic.position));
        if (const QRectF place = topic.geometry->linkIconRect(true); !place.isEmpty())
            image(buttonImage(QStringLiteral(":/assets/hugeicons/link.svg"), buttonPixels,
                              snapshot.linkBroken(topic.node) ? QColor(0x6b, 0x72, 0x80) : QColor(0x27, 0x30, 0x3b)),
                  place.translated(topic.position));
        if (const QRectF place = topic.geometry->linkIconRect(false); !place.isEmpty())
            image(buttonImage(QStringLiteral(":/assets/hugeicons/link-web.svg"), buttonPixels), place.translated(topic.position));
        if (const QRectF place = topic.geometry->taskRect(); !place.isEmpty())
            image(checkboxImage(topic.node, buttonPixels), place.translated(topic.position));
        if (const QRectF place = topic.geometry->filesRect(); !place.isEmpty())
            image(filesImage(int(topic.node->files.size()), buttonPixels),
                  place.translated(topic.position).adjusted(0, 0, kFilesBadgeOverflowPx, kFilesBadgeOverflowPx));
    }
    // RelationshipItem::paint: the line, the end marks solid, the label on its backdrop.
    for (const MapSnapshot::Relation &relation : snapshot.relations()) {
        const Relationships::Look &look = relation.look;
        if (look.linePattern != Qt::NoPen) {
            p.strokePath(relation.geometry.path,
                         QPen(look.lineColor, look.lineWidth, static_cast<Qt::PenStyle>(look.linePattern), Qt::FlatCap, Qt::RoundJoin));
            const auto mark = [&](BranchEnd kind, const QPointF &tip, const QPointF &outward) {
                if (kind == BranchEnd::None) return;
                const bool stroke = BranchPath::endIsStroke(kind);
                p.setPen(stroke ? QPen(look.lineColor, look.lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin) : QPen(Qt::NoPen));
                p.setBrush(stroke ? QBrush() : QBrush(look.lineColor));
                p.drawPath(BranchPath::endMark(kind, tip, outward, look.lineWidth));
            };
            mark(look.startMark, relation.geometry.start, relation.geometry.startDirection);
            mark(look.endMark, relation.geometry.end, relation.geometry.endDirection);
        }
        if (!relation.labelRect.isEmpty()) {
            p.setPen(Qt::NoPen);
            p.setBrush(look.backdrop);
            p.drawRoundedRect(relation.labelRect, Relationships::kLabelRadiusPx, Relationships::kLabelRadiusPx);
        }
    }
    // NodeItem::paintCollapseControls on a closed branch (FEAT-016): the count of its hidden topics
    // in a pill on the children's side, tied to the topic by a short line, on the sheet's backdrop;
    // its place as MapCanvas::controls works it out (org charts and tree trunks above or below).
    if (const MapLayout *layout = snapshot.layout()) {
        QFont countFont = p.font();
        countFont.setPixelSize(12);
        for (const MapSnapshot::Topic &topic : snapshot.topics()) {
            const MindNode *node = topic.node;
            const auto firstChild = std::find_if(node->children.cbegin(), node->children.cend(), [](const auto &child) { return !child->floating; });
            if (!node->collapsed || !node->parent || firstChild == node->children.cend()) continue;
            const std::function<int(const MindNode *)> descendants = [&descendants](const MindNode *n) {
                int count = 0;
                for (const auto &child : n->children) if (!child->floating) count += 1 + descendants(child.get());
                return count;
            };
            const QString count = QString::number(descendants(node));
            const MapStructure::Type type = MapStructure::typeOf(node);
            const bool vertical = type == MapStructure::Type::OrgChart
                               || (type == MapStructure::Type::TreeChart && MapStructure::typeFromKey(node->structure) == MapStructure::Type::TreeChart)
                               || MapLayoutRules::setsTimeline(node->parent);
            const MapSnapshot::Topic *firstTopic = snapshot.topicOf(firstChild->get());
            const QPointF firstPosition = firstTopic ? firstTopic->position : (*firstChild)->position;
            const int side = vertical ? (firstPosition.y() >= topic.position.y() ? 1 : -1) : ((node->floating || layout->sideOf(node) > 0) ? 1 : -1);
            const QRectF shape = topic.cell && !topic.cell->holdsBranch ? topic.cell->rect : topic.geometry->shapeRect().translated(topic.position);
            const qreal width = std::max<qreal>(18, QFontMetricsF(countFont).horizontalAdvance(count) + 8);
            QRectF rect;
            QLineF tie;
            if (vertical) {
                const qreal y = side > 0 ? shape.bottom() + 4 : shape.top() - 4 - 18;
                rect = QRectF(shape.center().x() - width / 2, y, width, 18);
                tie = QLineF(QPointF(rect.center().x(), side > 0 ? shape.bottom() : shape.top()), QPointF(rect.center().x(), side > 0 ? rect.top() : rect.bottom()));
            } else {
                const qreal x = side > 0 ? shape.right() + 4 : shape.left() - 4 - width;
                rect = QRectF(x, shape.center().y() - 9, width, 18);
                tie = QLineF(QPointF(side > 0 ? shape.right() : shape.left(), rect.center().y()), QPointF(side > 0 ? rect.left() : rect.right(), rect.center().y()));
            }
            p.setPen(QPen(node->lineColor, 1.5));
            p.setBrush(snapshot.background());
            p.drawLine(tie);
            p.drawRoundedRect(rect.adjusted(0.75, 0.75, -0.75, -0.75), rect.height() / 2 - 0.75, rect.height() / 2 - 0.75);
            p.setFont(countFont);
            p.drawText(rect, Qt::AlignCenter, count);
        }
    }
    // The titles, placed as build() places them.
    for (const MapSnapshot::Topic &topic : snapshot.topics()) {
        const MindNode *node = topic.node;
        const NodeGeometry &geometry = *topic.geometry;
        const QRectF textBox = geometry.textArea().translated(topic.position);
        if (textBox.isEmpty()) continue;
        p.setPen(node->text);
        p.setFont(NodeGeometry::titleFont(node));
        if (const FormulaRendering::TitleFormula *formula = geometry.formula()) {
            const QSizeF size = formula->size();
            const qreal inset = std::min(geometry.padX(), (textBox.width() - size.width()) / 2);
            Qt::Alignment horizontal = NodeGeometry::textAlignment(node) & Qt::AlignHorizontal_Mask;
            if (horizontal == Qt::AlignJustify) horizontal = Qt::AlignLeft;
            formula->draw(&p, textBox.adjusted(inset, 6, horizontal == Qt::AlignLeft ? -11 : -inset, -6), horizontal | Qt::AlignVCenter);
        } else if (QTextDocument *rich = geometry.richDocument()) {
            const QRectF target = textBox.adjusted(geometry.padX(), geometry.padY(), -geometry.padX(), -geometry.padY());
            p.save();
            p.translate(target.left(), target.center().y() - rich->size().height() / 2);
            QAbstractTextDocumentLayout::PaintContext context;
            context.palette.setColor(QPalette::Text, node->text);
            context.clip = QRectF(QPointF(), rich->size());
            rich->documentLayout()->draw(&p, context);
            p.restore();
        } else if (const QTextLayout *layout = geometry.plainLayout(); layout && layout->lineCount() > 0) {
            qreal height = 0, width = 0;
            for (int i = 0; i < layout->lineCount(); ++i) {
                const QTextLine line = layout->lineAt(i);
                height = line.y() + line.ascent() + line.descent();
                width = std::max(width, line.naturalTextWidth());
            }
            const qreal left = textBox.center().x() - width / 2;
            const qreal top = textBox.center().y() - height / 2;
            const qreal factor = node->textAlign == TextAlign::Centro ? 0.5 : node->textAlign == TextAlign::Destra ? 1.0 : 0.0;
            for (int i = 0; i < layout->lineCount(); ++i) {
                const QTextLine line = layout->lineAt(i);
                line.draw(&p, QPointF(left + (width - line.naturalTextWidth()) * factor, top));
            }
        }
    }
    // The tags' texts (drawLabelText: elided, centred on the capitals' height), the boundaries'
    // titles and the relationships' labels (drawText centred).
    const QFont labelFont = NodeGeometry::labelFont();
    const QFontMetricsF labelMetrics(labelFont);
    p.setFont(labelFont);
    p.setPen(pills.text);
    for (const MapSnapshot::Topic &topic : snapshot.topics()) {
        const QList<QRectF> places = topic.geometry->labelRects();
        for (qsizetype i = 0; i < places.size() && i < topic.node->labels.size(); ++i) {
            const QRectF pill = places.at(i).translated(topic.position);
            const QString text = labelMetrics.elidedText(topic.node->labels.at(i), Qt::ElideRight, pill.width() - 2 * NodeGeometry::kLabelPaddingPx);
            p.drawText(QPointF(pill.center().x() - labelMetrics.horizontalAdvance(text) / 2, pill.center().y() + labelMetrics.capHeight() / 2), text);
        }
    }
    for (const MapSnapshot::BoundaryShape &shape : snapshot.boundaries())
        if (!shape.titleRect.isEmpty()) {
            p.setFont(shape.look.font);
            p.setPen(shape.look.titleText);
            p.drawText(shape.titleRect, Qt::AlignCenter, shape.boundary.title);
        }
    for (const MapSnapshot::Relation &relation : snapshot.relations())
        if (!relation.labelRect.isEmpty()) {
            p.setFont(relation.look.font);
            p.setPen(relation.look.textColor);
            p.drawText(relation.labelRect, Qt::AlignCenter, relation.relationship.label);
        }
    p.restore();
}

QImage topicPicture(const MindNode *node, qreal scale) {
    return pictureOf(node, scale);
}

QImage markerPicture(const QString &marker, int pixels) {
    return markerImage(marker, pixels);
}

}  // namespace MapScene
