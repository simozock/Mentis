#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QString>
#include <QtQml/qqmlregistration.h>

class MobileMap;

// FEAT-121 A3: a picture of an outliner row as one image node, from the phone map's caches
// (MapScene::topicPicture, markerPicture), so a row costs no Canvas texture and a picture is decoded
// once for the map and the outline. With `marker` set the item shows that marker; otherwise the
// picture (image, sticker or formula) of topic `nodeId`, sharp at the item's size on this screen.
// The image is made on the GUI thread (updatePolish, where the model may be read) and only handed to
// the render thread as a texture. `revision` changes when the picture does (its file, size, formula).
class OutlineImage : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MobileMap *map READ map WRITE setMap NOTIFY sourceChanged)
    Q_PROPERTY(QString nodeId READ nodeId WRITE setNodeId NOTIFY sourceChanged)
    Q_PROPERTY(QString marker READ marker WRITE setMarker NOTIFY sourceChanged)
    Q_PROPERTY(QString revision READ revision WRITE setRevision NOTIFY sourceChanged)
    // False when the picture's file cannot be read: the row draws the desktop's dashed frame.
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)

public:
    explicit OutlineImage(QQuickItem *parent = nullptr);

    MobileMap *map() const { return m_map; }
    void setMap(MobileMap *map);
    QString nodeId() const { return m_nodeId; }
    void setNodeId(const QString &id);
    QString marker() const { return m_marker; }
    void setMarker(const QString &marker);
    QString revision() const { return m_revision; }
    void setRevision(const QString &revision);
    bool loaded() const { return m_loaded; }

signals:
    void sourceChanged();
    void loadedChanged();

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void reload();

    QPointer<MobileMap> m_map;
    QString m_nodeId, m_marker, m_revision;
    QImage m_image;
    bool m_imageChanged = false;
    qreal m_decodedWidth = 0;  // the item width the picture was last decoded for (with headroom)
    bool m_loaded = true;
};
