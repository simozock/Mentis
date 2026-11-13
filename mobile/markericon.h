#pragma once

#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// FEAT-097: a marker as the desktop draws it (Markers::paint), for the cells of the marker page.
// A few dozen small items, painted once when the marker is set.
class MarkerIcon : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    // "series:value" or "custom:<group>:<sha256>" (an image registered with Markers).
    Q_PROPERTY(QString marker READ marker WRITE setMarker NOTIFY markerChanged)

public:
    explicit MarkerIcon(QQuickItem *parent = nullptr);

    QString marker() const { return m_marker; }
    void setMarker(const QString &marker);
    void paint(QPainter *painter) override;

signals:
    void markerChanged();

private:
    QString m_marker;
};
