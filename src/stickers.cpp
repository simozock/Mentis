#include "stickers.h"

#include <QImageReader>
#include <QPainter>
#include <QtMath>

namespace Stickers {

QSizeF insertionSize(const QString &path) {
    QSizeF size = QImageReader(path).size();
    if (size.isEmpty()) size = {120, 120};
    return size.scaled(120, 120, Qt::KeepAspectRatio);
}

namespace {
// La sagoma (alfa del disegno) in un colore solo.
QImage silhouette(const QImage &content, const QColor &color) {
    QImage mask = content;
    QPainter painter(&mask);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(mask.rect(), color);
    return mask;
}

// Sagoma allargata di radius: copie spostate tutt'attorno (un passo per pixel di circonferenza),
// anche a meta' raggio per le parti sottili.
void spread(QPainter &painter, const QImage &mask, qreal radius) {
    for (const qreal r : {radius, radius / 2}) {
        const int steps = std::max(12, int(std::ceil(2 * M_PI * r)));
        for (int i = 0; i < steps; ++i) {
            const qreal angle = 2 * M_PI * i / steps;
            painter.drawImage(QPointF(r * std::cos(angle), r * std::sin(angle)), mask);
        }
    }
}
}  // namespace

QImage outlined(const QImage &source) {
    if (source.isNull()) return source;
    const qreal side = std::max(source.width(), source.height());
    const qreal white = std::max(1.0, side * kOutlineWhite);
    const qreal ring = white + std::max(1.0, side * kOutlineDark);
    QImage content(source.size(), QImage::Format_ARGB32_Premultiplied);
    content.setDevicePixelRatio(source.devicePixelRatio());
    content.fill(Qt::transparent);
    {
        QPainter painter(&content);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSizeF room(source.width() - 2 * ring, source.height() - 2 * ring);
        const QSizeF drawn = QSizeF(source.size()).scaled(room, Qt::KeepAspectRatio);
        const QRectF target(QPointF((source.width() - drawn.width()) / 2, (source.height() - drawn.height()) / 2), drawn);
        painter.drawImage(target, source, QRectF(source.rect()));
    }
    content.setDevicePixelRatio(1.0);  // spostamenti in pixel dell'immagine
    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    spread(painter, silhouette(content, QColor(0x20, 0x26, 0x30)), ring);
    spread(painter, silhouette(content, Qt::white), white);
    painter.drawImage(0, 0, content);
    painter.end();
    result.setDevicePixelRatio(source.devicePixelRatio());
    return result;
}

}  // namespace Stickers
