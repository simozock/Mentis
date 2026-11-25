#include "variantsample.h"

#include "mapscene.h"
#include "mapsnapshot.h"
#include "mapvariants.h"
#include "mindmapmodel.h"
#include "nodegeometry.h"

#include <QCoreApplication>
#include <QSGTransformNode>

VariantSample::VariantSample(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents);
    setClip(true);  // a variant's long lines (timeline, fishbone) stay inside the tile
}

VariantSample::~VariantSample() = default;

void VariantSample::setVariant(const QString &variant) {
    if (variant == m_variant) return;
    m_variant = variant;
    rebuild();
    emit variantChanged();
}

void VariantSample::setMono(bool mono) {
    if (mono == m_mono) return;
    m_mono = mono;
    rebuild();
    emit variantChanged();
}

QRectF VariantSample::sampleBounds() const { return m_snapshot ? m_snapshot->bounds() : QRectF(); }

void VariantSample::rebuild() {
    // renderSample: a new map with the variant applied, two subtopics under the first main topic,
    // every title a space (in a thumbnail only the shape counts).
    m_snapshot.reset();
    m_model.reset();
    const MapVariants::Variant *variant = MapVariants::find(m_variant);
    if (!variant) variant = &MapVariants::all().first();
    m_model = std::make_unique<MindMapModel>();
    m_model->clear(QCoreApplication::translate("VariantGallery", "Central Topic"));
    if (m_mono) m_model->activePalette = QStringList(6, QStringLiteral("#8a939e"));
    MapVariants::apply(*m_model, variant->id, true);
    MindNode *first = m_model->root()->children.front().get();
    for (int j = 1; j <= 2; ++j) m_model->addChild(first, MindMapModel::defaultTitle(first, j));
    const std::function<void(MindNode *)> blank = [&blank](MindNode *node) {
        node->title = QStringLiteral(" ");
        for (const auto &child : node->children) blank(child.get());
    };
    blank(m_model->root());
    m_model->refreshStyles();
    // MapSnapshot sets the shared Show all notes from its sheet: the open map's setting is kept.
    const bool notes = NodeGeometry::showAllNotes();
    m_snapshot = std::make_unique<MapSnapshot>(m_model.get(), true);
    NodeGeometry::showAllNotes() = notes;
    m_sceneDirty = true;
    update();
}

void VariantSample::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    update();
}

QSGNode *VariantSample::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
    auto *transform = static_cast<QSGTransformNode *>(oldNode);
    if (!transform) {
        // A first paint, or the scene graph dropped the old node (the item left the window, as a
        // closed popup's tiles do): the sample is built again.
        transform = new QSGTransformNode;
        m_sceneDirty = true;
    }
    if (m_sceneDirty && m_snapshot) {
        m_sceneDirty = false;
        while (QSGNode *child = transform->firstChild()) {
            transform->removeChildNode(child);
            delete child;
        }
        transform->appendChildNode(MapScene::build(*m_snapshot, window()).content);
        ++m_sceneBuilds;
    }
    // Fitted with the proportions of the map, centred (renderSample).
    const QRectF source = m_snapshot ? m_snapshot->bounds().adjusted(-16, -16, 16, 16) : QRectF();
    QTransform matrix;
    if (!source.isEmpty() && width() > 0 && height() > 0) {
        const qreal scale = std::min(width() / source.width(), height() / source.height());
        matrix.translate(width() / 2, height() / 2);
        matrix.scale(scale, scale);
        matrix.translate(-source.center().x(), -source.center().y());
    }
    transform->setMatrix(QMatrix4x4(matrix));
    return transform;
}
