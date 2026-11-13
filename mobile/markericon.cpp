#include "markericon.h"

#include "markers.h"

#include <QPainter>

MarkerIcon::MarkerIcon(QQuickItem *parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }

void MarkerIcon::setMarker(const QString &marker) {
    if (marker == m_marker) return;
    m_marker = marker;
    emit markerChanged();
    update();
}

void MarkerIcon::paint(QPainter *painter) {
    if (!m_marker.isEmpty() && Markers::isValid(m_marker)) Markers::paint(painter, boundingRect(), m_marker);
}
