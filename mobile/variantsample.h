#pragma once

#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

class MapSnapshot;
class MindMapModel;

// FEAT-119 stage B2: a map type variant's sample map, as the desktop's VariantGallery::thumbnail
// (renderSample): the central topic, the variant's main topics and two subtopics under the first,
// without text, laid out and drawn by the phone's own scene (MapSnapshot, MapScene), fitted in the
// item with 16 px around it. mono: the panel's grey version. Built when the variant changes; the
// white backdrop is the page's.
class VariantSample : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString variant READ variant WRITE setVariant NOTIFY variantChanged)
    Q_PROPERTY(bool mono READ mono WRITE setMono NOTIFY variantChanged)
public:
    explicit VariantSample(QQuickItem *parent = nullptr);
    ~VariantSample() override;
    QString variant() const { return m_variant; }
    void setVariant(const QString &variant);
    bool mono() const { return m_mono; }
    void setMono(bool mono);
    // The sample's bounds in map px (for tests); empty before it is built.
    Q_INVOKABLE QRectF sampleBounds() const;
    // How many times the scene graph content was built (for tests).
    int sceneBuilds() const { return m_sceneBuilds; }

signals:
    void variantChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void rebuild();
    QString m_variant;
    bool m_mono = false;
    std::unique_ptr<MindMapModel> m_model;
    std::unique_ptr<MapSnapshot> m_snapshot;
    bool m_sceneDirty = true;
    std::atomic<int> m_sceneBuilds = 0;  // written on the render thread
};
