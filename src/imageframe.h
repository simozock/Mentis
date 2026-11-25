#pragma once

// FEAT-040, FEAT-129: a topic's picture drawn in its box with its ImageStyle: the shadow, the
// picture cut to its shape with its opacity, then the border along the shape (inside the box, the
// size does not change). Shared by the map (NodeItem), the phone (MapScene) and the Pitch; no widget.

#include "nodestyle.h"

class QImage;
class QPainter;
class QPixmap;
class QRectF;
class QString;

namespace ImageFrame {

// scale: the Pitch draws the figure larger than in the topic; the shadow, border and corners grow with it.
void paint(QPainter *painter, const QRectF &box, const QPixmap &picture, const ImageStyle &style, qreal scale = 1);
void paint(QPainter *painter, const QRectF &box, const QImage &picture, const ImageStyle &style, qreal scale = 1);

// The picture alone, without shadow, border or shape: drawn as a plain image.
bool isPlain(const ImageStyle &style);
// FEAT-131: the picture's pixels with the effect, alpha kept; applied once where the pixels are
// decoded (and cached with the effect in the key), never while painting.
QImage withEffect(const QImage &picture, ImageEffect effect);
// The file's imageEffect values: empty for None.
QString effectKey(ImageEffect effect);
ImageEffect effectFromKey(const QString &key);  // unknown or empty = None

// How far the shadow and the marks of the open shapes reach outside the box, at scale 1.
qreal reach(const ImageStyle &style);

}  // namespace ImageFrame
