#include "mapexporter.h"

#include "mindmapmodel.h"
#include "mindmapscene.h"

#include <QGraphicsItem>
#include <QPainter>

MapExporter::Rendering::Rendering(const Page &page, qreal imageScale, const std::function<QString(const MindNode *)> &originalImagePath)
    : m_model(std::make_unique<MindMapModel>()) {
    if (!m_model->fromFlatSnapshot(page.content, &m_error)) {
        if (m_error.isEmpty()) m_error = tr("The sheet cannot be read.");
        return;
    }
    m_scene = std::make_unique<MindMapScene>(m_model.get());
    m_scene->setExportImageScale(imageScale);
    m_scene->originalImagePath = originalImagePath;
    if (!page.branchId.isEmpty()) {
        const MindNode *top = m_model->nodeById(page.branchId);
        if (!top) {
            m_error = tr("The selected topic is no longer in the map.");
            return;
        }
        m_scene->isolateBranch(top);
    }
    // Visible items only: itemsBoundingRect would also count the ones hidden by a collapsed branch.
    QRectF content;
    for (const QGraphicsItem *item : m_scene->items())
        if (item->isVisible()) content |= item->sceneBoundingRect();
    if (content.isEmpty()) {
        m_error = tr("There is nothing to export.");
        return;
    }
    m_source = content.adjusted(-kMarginPx, -kMarginPx, kMarginPx, kMarginPx);
    m_background = m_model->backdrop().color;
    m_branchId = page.branchId;
}

MapExporter::Rendering::~Rendering() = default;

void MapExporter::Rendering::drawPart(QPainter *painter, const QRectF &target, const QRectF &part, bool background) const {
    if (!isValid()) return;
    // The collapse badges fill with the scene's brush: without background they stay see-through too.
    m_scene->setBackgroundBrush(background ? QBrush(m_background) : QBrush(Qt::NoBrush));
    m_scene->render(painter, target, part, Qt::KeepAspectRatio);
}

MapPages::DrawingFactory MapExporter::sceneDrawing() {
    return [](const Page &page, qreal imageScale, const std::function<QString(const MindNode *)> &originalImagePath) {
        return std::unique_ptr<Drawing>(std::make_unique<Rendering>(page, imageScale, originalImagePath));
    };
}
