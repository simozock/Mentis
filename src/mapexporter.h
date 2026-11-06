#pragma once

// FEAT-054 stage 2 (docs/features/FEAT-054-esportazione.md): PNG, SVG and PDF of sheets as they
// appear on the map. No widgets: each sheet is drawn from a temporary model and scene built from its
// flat content, on the GUI thread (a QGraphicsScene is not drawn elsewhere).

#include "exportframe.h"
#include "maphistory.h"

#include <QColor>
#include <QDateTime>
#include <QCoreApplication>
#include <QImage>
#include <QPageLayout>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

class MindMapModel;
class MindMapScene;
class QPagedPaintDevice;
class QPainter;
struct MindNode;

class MapExporter {
    Q_DECLARE_TR_FUNCTIONS(MapExporter)
public:
    static constexpr qreal kMarginPx = 32;        // around the content (proposal, no external source)
    static constexpr int kMaxPngSidePx = 32768;   // a larger PNG is drawn at a smaller scale
    static constexpr qreal kMaxPdfPagePt = 14400; // PDF 1.x page limit (200 in); a larger map is scaled down

    static constexpr qreal kPageMarginMm = 10;    // fixed page sizes (proposal, no external source)
    static constexpr qreal kPxPerPt = 96.0 / 72.0;  // the map's pixels at 96 DPI

    enum class Format { Png, Svg };
    // Stage 3: paper of the PDF and of the printer. FitToMap = the page is the map (map PDF only).
    enum class PageSize { FitToMap, A3, A4, A5, Letter, Legal };
    struct Page {
        QString name;  // the sheet's name: file names of "All sheets"
        MapHistory::Snapshot content;
        QString branchId;  // empty = the whole sheet, else this topic and its descendants
    };
    struct Options {
        bool background = true;
        int scale = 2;  // PNG only
        ExportFrame frame;  // FEAT-084: on the image's edge, the image larger by its width
        // Original of an embedded image (MindMapScene::originalImagePath); unset = thumbnails.
        std::function<QString(const MindNode *)> originalImagePath;
    };
    struct Result {
        bool ok = true;
        QString error;
        bool scaleReduced = false;  // a PNG over kMaxPngSidePx
        QStringList written;        // files written, in order (the ones before an error included)
    };

    // A page ready to draw, built once and drawn as often as needed (the dialog's preview).
    class Rendering {
    public:
        Rendering(const Page &page, qreal imageScale, const std::function<QString(const MindNode *)> &originalImagePath);
        ~Rendering();
        bool isValid() const { return m_error.isEmpty(); }
        QString error() const { return m_error; }
        QRectF source() const { return m_source; }  // visible content plus kMarginPx, in scene coordinates
        QColor background() const { return m_background; }
        // The source fitted into target (aspect ratio kept); without background nothing fills it.
        void draw(QPainter *painter, const QRectF &target, bool background) const;
        // Part of the source (scene coordinates) into target, e.g. one tile of a map at actual size.
        void drawPart(QPainter *painter, const QRectF &target, const QRectF &part, bool background) const;
        const MindMapModel *model() const { return m_model.get(); }
        QString branchId() const { return m_branchId; }

    private:
        std::unique_ptr<MindMapModel> m_model;
        std::unique_ptr<MindMapScene> m_scene;
        QRectF m_source;
        QColor m_background;
        QString m_error;
        QString m_branchId;
    };

    struct PageOptions {
        bool outliner = false;  // the indented titles and notes instead of the map
        PageSize size = PageSize::FitToMap;
        bool landscape = false;  // fixed sizes only
        bool actualSize = false;  // map on a fixed page: 100% over several pages instead of fitted on one
        bool background = true;   // map only
        std::function<QString(const MindNode *)> originalImagePath;
    };

    // Pages of a PDF, a print or their preview (the map's PagePlan; FEAT-083: the Gantt's pages).
    class PageSource {
    public:
        virtual ~PageSource() = default;
        virtual bool isValid() const = 0;
        virtual QString error() const = 0;
        virtual int count() const = 0;
        virtual QPageLayout layout(int index) const = 0;  // size and orientation, no margins
        // Draws page index on target, the whole page in the painter's coordinates.
        virtual void draw(int index, QPainter *painter, const QRectF &target) const = 0;
    };

    // The pages of a PDF, a print or their preview, worked out once: every sheet in order, each on
    // one page or more. Drawn in points of the page scaled to any target.
    class PagePlan final : public PageSource {
    public:
        PagePlan(const QList<Page> &pages, const PageOptions &options);
        ~PagePlan() override;
        bool isValid() const override { return m_error.isEmpty(); }
        QString error() const override { return m_error; }
        int count() const override { return int(m_pieces.size()); }
        QPageLayout layout(int index) const override;
        void draw(int index, QPainter *painter, const QRectF &target) const override;

    private:
        struct Sheet;
        struct Piece {
            int sheet = 0;
            QSizeF pagePt;
            QRectF contentPt;  // where the content goes on the page
            QRectF part;       // map: the scene rectangle shown; outliner: y and height of the text page
        };
        std::vector<std::unique_ptr<Sheet>> m_sheets;
        std::vector<Piece> m_pieces;
        PageOptions m_options;
        QString m_error;
    };

    // The page as a PNG would be (transparent without background); null with error set on failure.
    static QImage toImage(const Page &page, const Options &options, bool *scaleReduced = nullptr, QString *error = nullptr);
    // chosenPath for one page, else pagePaths. Each file is written atomically; the first error stops.
    static Result write(Format format, const QList<Page> &pages, const QString &chosenPath, const Options &options);
    // The plan's pages in one PDF, written atomically. Monochrome: a grey PDF, still vector.
    static Result writePdf(const PageSource &plan, const QString &path, const QString &title, bool monochrome);
    // The plan's pages on a paged device already set up (printer: name, copies, color mode).
    static Result paint(const PageSource &plan, QPagedPaintDevice *device);
    // FEAT-084: pages with a frame around their content. A page of the content's size (Fit to map)
    // grows by the frame, the frame on its edge; on paper (A4...) the frame goes inside the margins
    // and the page's drawing is fitted inside it.
    class FramedPages final : public PageSource {
    public:
        FramedPages(std::unique_ptr<PageSource> pages, const ExportFrame &frame);
        bool isValid() const override { return m_pages->isValid(); }
        QString error() const override { return m_pages->error(); }
        int count() const override { return m_pages->count(); }
        QPageLayout layout(int index) const override;
        void draw(int index, QPainter *painter, const QRectF &target) const override;
        const PageSource *pages() const { return m_pages.get(); }

    private:
        bool grows(int index) const;  // a page of its content's size, not a paper size
        std::unique_ptr<PageSource> m_pages;
        ExportFrame m_frame;
    };
    // The frame of a page drawn into target: the page of pagePx's size (96 per inch) scaled to it.
    static void paintFrame(QPainter *painter, const QRectF &target, const QSizeF &pagePx, const ExportFrame &frame, bool onEdge);
    static constexpr qreal kFrameGapPx = 15;  // between a frame on paper and the page's drawing (4 mm)

    // A PageSize's page layout without margins (A4 by default, also for FitToMap).
    static QPageLayout pageLayout(PageSize size, bool landscape);
    // "Map.png" and sheets {"A", "B/C"} -> "Map-A.png", "Map-B-C.png": characters not allowed in file
    // names become '-', repeated names get -2, -3 (case ignored).
    static QStringList pagePaths(const QString &chosenPath, const QStringList &sheetNames);
    // The suggested name of every export (user, 2026-10-31): the moment in ISO 8601 basic form, then
    // the name: "20261031T113911 Sheet 1". No ':' (not allowed on Windows); sorts by date. The
    // name's characters not allowed in file names become '-'; no name = "Untitled".
    static QString defaultFileName(const QString &name, const QDateTime &moment);
};
