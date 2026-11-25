#pragma once

// FEAT-054 stage 2: PNG, SVG and PDF of sheets as they appear on the desktop's map. The pages and
// the files are MapPages' (shared with the phone, FEAT-120 stage C2); here each sheet is drawn from
// a temporary model and scene built from its flat content, on the GUI thread (a QGraphicsScene is
// not drawn elsewhere).

#include "mappages.h"

#include <memory>

class MindMapScene;

class MapExporter : public MapPages {
    Q_DECLARE_TR_FUNCTIONS(MapExporter)
public:
    // A page ready to draw, built once and drawn as often as needed (the dialog's preview).
    class Rendering final : public Drawing {
    public:
        Rendering(const Page &page, qreal imageScale, const std::function<QString(const MindNode *)> &originalImagePath);
        ~Rendering() override;
        void drawPart(QPainter *painter, const QRectF &target, const QRectF &part, bool background) const override;
        const MindMapModel *model() const override { return m_model.get(); }

    private:
        std::unique_ptr<MindMapModel> m_model;
        std::unique_ptr<MindMapScene> m_scene;
    };
    // The desktop scene's drawing of a page.
    static DrawingFactory sceneDrawing();

    class PagePlan final : public MapPages::PagePlan {
    public:
        PagePlan(const QList<Page> &pages, const PageOptions &options) : MapPages::PagePlan(pages, options, sceneDrawing()) {}
    };
    static QImage toImage(const Page &page, const Options &options, bool *scaleReduced = nullptr, QString *error = nullptr) {
        return MapPages::toImage(page, options, sceneDrawing(), scaleReduced, error);
    }
    static Result write(Format format, const QList<Page> &pages, const QString &chosenPath, const Options &options) {
        return MapPages::write(format, pages, chosenPath, options, sceneDrawing());
    }
};
