#include "outlineimage.h"

#include "document.h"
#include "mapscene.h"
#include "mindmapmodel.h"
#include "mobilemap.h"
#include "performance.h"

#include <QQuickWindow>
#include <QSGImageNode>

#include <algorithm>
#include <cmath>

OutlineImage::OutlineImage(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents);
}

void OutlineImage::setMap(MobileMap *map) {
    if (m_map == map) return;
    m_map = map;
    emit sourceChanged();
    reload();
}

void OutlineImage::setNodeId(const QString &id) {
    if (m_nodeId == id) return;
    m_nodeId = id;
    emit sourceChanged();
    reload();
}

void OutlineImage::setMarker(const QString &marker) {
    if (m_marker == marker) return;
    m_marker = marker;
    emit sourceChanged();
    reload();
}

void OutlineImage::setRevision(const QString &revision) {
    if (m_revision == revision) return;
    m_revision = revision;
    emit sourceChanged();
    reload();
}

void OutlineImage::reload() {
    polish();
}

void OutlineImage::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    // A picture is decoded at its shown size. During a pinch (FEAT-121 A4) the size changes at every
    // step: the texture is scaled (linear, mipmaps) while it stays between half and all of the decoded
    // width, and decoded again only past those bounds, so a pinch decodes a few times, not at each step.
    if (newGeometry.size() == oldGeometry.size()) return;
    if (!m_marker.isEmpty() || newGeometry.width() > m_decodedWidth || newGeometry.width() < m_decodedWidth / 2) reload();
    else update();
}

void OutlineImage::itemChange(ItemChange change, const ItemChangeData &value) {
    QQuickItem::itemChange(change, value);
    // The screen's pixel ratio is known (a window) or has changed (another screen): sharp again.
    if ((change == ItemSceneChange && value.window) || change == ItemDevicePixelRatioHasChanged) reload();
}

void OutlineImage::updatePolish() {
    QImage image;
    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1;
    if (width() > 0 && height() > 0) {
        if (!m_marker.isEmpty()) {
            image = MapScene::markerPicture(m_marker, int(std::ceil(std::max(width(), height()) * ratio)));
        } else if (m_map && m_map->document() && !m_nodeId.isEmpty()) {
            const MindNode *node = m_map->document()->model()->nodeById(m_nodeId);
            if (node && (!node->imagePath.isEmpty() || !node->formula.isEmpty()) && !node->imageSize.isEmpty()) {
                // As the map's build: at least the screen's pixel ratio for the shown size, a formula at
                // twice that (drawn from vectors, sharp when zoomed in). Growing: 25% of headroom, so a
                // pinch that zooms in decodes again only every 25%.
                m_decodedWidth = width() > m_decodedWidth ? width() * 1.25 : width();
                const qreal shown = m_decodedWidth / node->imageSize.width();
                const qreal scale = shown * std::max<qreal>(Performance::imageScaleFactor(), ratio);
                image = MapScene::topicPicture(node, node->formula.isEmpty() ? scale : 2 * scale);
            }
        }
    }
    m_image = image;
    m_imageChanged = true;
    const bool loaded = !m_image.isNull() || !m_marker.isEmpty();
    if (loaded != m_loaded) {
        m_loaded = loaded;
        emit loadedChanged();
    }
    update();
}

QSGNode *OutlineImage::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    auto *node = static_cast<QSGImageNode *>(old);
    if (m_image.isNull()) {
        delete node;
        return nullptr;
    }
    if (!node) {
        node = window()->createImageNode();
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
        node->setMipmapFiltering(QSGTexture::Linear);
        m_imageChanged = true;
    }
    if (m_imageChanged) {
        node->setTexture(window()->createTextureFromImage(m_image, QQuickWindow::TextureHasMipmaps));
        m_imageChanged = false;
    }
    node->setRect(boundingRect());
    return node;
}
