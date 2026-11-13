#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// FEAT-097: the collections for the map page's markers and stickers modal, the desktop's
// (IconLibrary, FEAT-051): the built-in ones and the user's own, in the chosen order, hidden ones
// left out. The collections are arranged on the desktop; the modal only adds files (MobileMap).
class StickerLibrary : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // true: the marker collections; false: the sticker ones.
    Q_PROPERTY(bool markers READ markers WRITE setMarkers NOTIFY markersChanged)

public:
    explicit StickerLibrary(QObject *parent = nullptr) : QObject(parent) {}

    bool markers() const { return m_markers; }
    void setMarkers(bool markers);

    // [{id, name, items: [{path, name, source, marker}]}]: path for MobileMap (insertSticker,
    // toggleMarker), source the URL an Image shows a sticker from, marker the id a topic keeps for
    // a marker item (IconLibrary::markerFor), drawn by MarkerIcon. Collections without items are
    // left out.
    Q_INVOKABLE QVariantList collections() const;

signals:
    void markersChanged();

private:
    bool m_markers = false;
};
