#include "imageframe.h"

#include "nodeshape.h"

#include <QBrush>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QString>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <utility>
#include <type_traits>

namespace ImageFrame {

namespace {

// The topics' rounded corner (nodeshape.cpp), scaled with the figure in the Pitch.
constexpr qreal kRoundedRadiusPx = 8;
// FEAT-040: the modals' light shadow, 4 rounded rectangles with alpha 10/8/6/4, 2 px lower.
constexpr int kShadowLayers = 4;
constexpr int kShadowAlphas[kShadowLayers] = {10, 8, 6, 4};
constexpr qreal kShadowDropPx = 2;

// Closed shapes cut the picture; Rectangle and the open shapes (lines below, Punctual) keep it whole.
bool cutsPicture(NodeShape shape) { return shape != NodeShape::Rettangolo && !NodeShapes::hasOpenBorder(shape); }

void paintShadow(QPainter *painter, const QRectF &box, const ImageStyle &style, qreal scale) {
    painter->save();
    painter->setPen(Qt::NoPen);
    const bool cut = cutsPicture(style.shape);
    if (cut) painter->setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < kShadowLayers; ++i) {
        const qreal grow = (i + 1) * scale;
        const QRectF layer = box.adjusted(-grow, -grow + kShadowDropPx * scale, grow, grow + kShadowDropPx * scale);
        painter->setBrush(QColor(15, 23, 42, kShadowAlphas[i]));
        if (cut) painter->drawPath(NodeShapes::outline(style.shape, layer, kRoundedRadiusPx * scale + grow));
        else painter->drawRoundedRect(layer, grow, grow);
    }
    painter->restore();
}

template <class Picture>
void paintPicture(QPainter *painter, const QRectF &box, const Picture &picture, const ImageStyle &style, qreal scale) {
    const qreal opacity = painter->opacity();
    painter->setOpacity(opacity * style.opacity / qreal(ImageStyle::kMaxOpacity));
    if (cutsPicture(style.shape) && !picture.isNull()) {
        // The picture as the brush of the outline: antialiased edges, no off-screen buffer (AGENTS.md,
        // "Prestazioni"); a clip path would not be antialiased. The raster engine divides a texture
        // brush, translation included, by the picture's pixel ratio (formulas carry one above 1).
        QBrush brush(picture);
        const qreal ratio = picture.devicePixelRatio();
        const QSizeF size = picture.deviceIndependentSize();
        brush.setTransform(QTransform::fromTranslate(box.left() * ratio, box.top() * ratio)
                               .scale(box.width() / size.width(), box.height() / size.height()));
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(brush);
        painter->drawPath(NodeShapes::outline(style.shape, box, kRoundedRadiusPx * scale));
        painter->restore();
    } else if constexpr (std::is_same_v<Picture, QPixmap>) {
        painter->drawPixmap(box, picture, picture.rect());
    } else {
        painter->drawImage(box, picture, picture.rect());
    }
    painter->setOpacity(opacity);
}

void paintBorder(QPainter *painter, const QRectF &box, const ImageStyle &style, qreal scale) {
    if (style.border == ImageStyle::Border::None) return;
    const Qt::PenStyle pattern = style.border == ImageStyle::Border::Dashed ? Qt::DashLine
                                 : style.border == ImageStyle::Border::Dotted ? Qt::DotLine : Qt::SolidLine;
    const qreal width = style.borderWidth * scale;
    const qreal half = width / 2;
    const QRectF inner = box.adjusted(half, half, -half, -half);
    painter->save();
    painter->setBrush(Qt::NoBrush);
    if (style.shape == NodeShape::Rettangolo) {
        painter->setPen(QPen(style.borderColor, width, pattern, Qt::FlatCap, Qt::MiterJoin));
        painter->drawRect(inner);
        painter->restore();
        return;
    }
    // As a topic's border (NodeItem::paint): the outline, or the lines and marks of an open shape.
    const qreal radius = std::max<qreal>(0, kRoundedRadiusPx * scale - half);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(style.borderColor, width, pattern, Qt::FlatCap, Qt::RoundJoin));
    if (!NodeShapes::hasOpenBorder(style.shape)) painter->drawPath(NodeShapes::outline(style.shape, inner, radius));
    for (const QLineF &line : NodeShapes::borderLines(style.shape, box, width)) painter->drawLine(line);
    for (const QPainterPath &line : NodeShapes::innerLines(style.shape, inner, radius)) painter->drawPath(line);
    for (const QPainterPath &mark : NodeShapes::marks(style.shape, inner)) painter->drawPath(mark);
    painter->fillPath(NodeShapes::filledMarks(style.shape, inner), style.borderColor);
    painter->restore();
}

template <class Picture>
void paintFramed(QPainter *painter, const QRectF &box, const Picture &picture, const ImageStyle &style, qreal scale) {
    if (style.shadow) paintShadow(painter, box, style, scale);
    paintPicture(painter, box, picture, style, scale);
    paintBorder(painter, box, style, scale);
}

}  // namespace

void paint(QPainter *painter, const QRectF &box, const QPixmap &picture, const ImageStyle &style, qreal scale) {
    paintFramed(painter, box, picture, style, scale);
}

void paint(QPainter *painter, const QRectF &box, const QImage &picture, const ImageStyle &style, qreal scale) {
    paintFramed(painter, box, picture, style, scale);
}

QImage withEffect(const QImage &picture, ImageEffect effect) {
    if (effect == ImageEffect::None || picture.isNull()) return picture;
    // Straight (not premultiplied) colours, so the effect does not darken half-transparent pixels.
    QImage image = picture.convertToFormat(QImage::Format_ARGB32);
    const auto clamped = [](qreal value) { return std::clamp(int(std::lround(value)), 0, 255); };
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = line[x];
            const qreal r = qRed(pixel), g = qGreen(pixel), b = qBlue(pixel);
            switch (effect) {
            case ImageEffect::Grayscale: {
                // CSS Filter Effects 1, grayscale(1): https://www.w3.org/TR/filter-effects-1/#grayscaleEquivalent
                const int luma = clamped(0.2126 * r + 0.7152 * g + 0.0722 * b);
                line[x] = qRgba(luma, luma, luma, qAlpha(pixel));
                break;
            }
            case ImageEffect::Sepia:
                // CSS Filter Effects 1, sepia(1): https://www.w3.org/TR/filter-effects-1/#sepiaEquivalent
                line[x] = qRgba(clamped(0.393 * r + 0.769 * g + 0.189 * b), clamped(0.349 * r + 0.686 * g + 0.168 * b),
                                clamped(0.272 * r + 0.534 * g + 0.131 * b), qAlpha(pixel));
                break;
            case ImageEffect::Invert:
                line[x] = qRgba(255 - qRed(pixel), 255 - qGreen(pixel), 255 - qBlue(pixel), qAlpha(pixel));
                break;
            default: {
                // Faded, a chosen look with no external reference: half the saturation (toward the
                // grayscale above), 70% contrast around the middle, then 8% lighter.
                constexpr qreal kSaturation = 0.5, kContrast = 0.7, kLighter = 1.08;
                const qreal luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                const auto faded = [&](qreal c) {
                    const qreal saturated = luma + (c - luma) * kSaturation;
                    return clamped(((saturated - 127.5) * kContrast + 127.5) * kLighter);
                };
                line[x] = qRgba(faded(r), faded(g), faded(b), qAlpha(pixel));
                break;
            }
            }
        }
    }
    // Premultiplied, as the raster engine draws it: no conversion at every paint.
    image = std::move(image).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(picture.devicePixelRatio());
    return image;
}

namespace {
constexpr std::pair<ImageEffect, const char *> kEffectKeys[] = {
    {ImageEffect::Grayscale, "grayscale"}, {ImageEffect::Sepia, "sepia"}, {ImageEffect::Invert, "invert"}, {ImageEffect::Faded, "faded"}};
}

QString effectKey(ImageEffect effect) {
    for (const auto &[value, key] : kEffectKeys)
        if (value == effect) return QString::fromLatin1(key);
    return {};
}

ImageEffect effectFromKey(const QString &key) {
    for (const auto &[value, name] : kEffectKeys)
        if (key == QLatin1StringView(name)) return value;
    return ImageEffect::None;
}

bool isPlain(const ImageStyle &style) {
    return style.shape == NodeShape::Rettangolo && style.border == ImageStyle::Border::None && !style.shadow;
}

qreal reach(const ImageStyle &style) {
    const qreal shadow = style.shadow ? kShadowLayers + kShadowDropPx : 0;
    const qreal marks = style.border == ImageStyle::Border::None ? 0 : NodeShapes::markOverhang(style.shape) + style.borderWidth;
    return std::max(shadow, marks);
}

}  // namespace ImageFrame
