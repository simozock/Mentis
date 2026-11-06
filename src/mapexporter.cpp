#include "mapexporter.h"

#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "nodestyle.h"

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QPageLayout>
#include <QPageSize>
#include <QPagedPaintDevice>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSvgGenerator>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>
#include <cmath>

namespace {
constexpr qreal kPxPerPt = MapExporter::kPxPerPt;
constexpr qreal kPtPerMm = 72.0 / 25.4;

void prepare(QPainter &painter) {
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
}

QString writeError(const QString &path, const QString &reason) {
    return MapExporter::tr("Cannot write \"%1\": %2").arg(QDir::toNativeSeparators(path), reason);
}
}  // namespace

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

void MapExporter::Rendering::draw(QPainter *painter, const QRectF &target, bool background) const {
    drawPart(painter, target, m_source, background);
}

void MapExporter::Rendering::drawPart(QPainter *painter, const QRectF &target, const QRectF &part, bool background) const {
    if (!isValid()) return;
    // The collapse badges fill with the scene's brush: without background they stay see-through too.
    m_scene->setBackgroundBrush(background ? QBrush(m_background) : QBrush(Qt::NoBrush));
    m_scene->render(painter, target, part, Qt::KeepAspectRatio);
}

// --- Pages (stage 3) ---

// One sheet of the plan: its map drawing, or its outline laid out as text pages.
struct MapExporter::PagePlan::Sheet {
    std::unique_ptr<Rendering> rendering;
    std::unique_ptr<QTextDocument> outline;
    QString error;
};

namespace {
QPageSize::PageSizeId pageSizeId(MapExporter::PageSize size) {
    switch (size) {
    case MapExporter::PageSize::A3: return QPageSize::A3;
    case MapExporter::PageSize::A5: return QPageSize::A5;
    case MapExporter::PageSize::Letter: return QPageSize::Letter;
    case MapExporter::PageSize::Legal: return QPageSize::Legal;
    default: return QPageSize::A4;
    }
}

// Like the outliner: one title per line, indented by level, its note below in small grey text.
// Sizes in pixels at 96 DPI (the page is drawn in that unit), from points: 16 pt central, 11 pt
// titles, 9 pt notes; 6 mm per level.
std::unique_ptr<QTextDocument> outlineDocument(const MindMapModel &model, const MindNode *top) {
    constexpr qreal kIndentPx = 6 * kPtPerMm * kPxPerPt;
    auto document = std::make_unique<QTextDocument>();
    document->setDocumentMargin(0);
    const QString family = model.sheetStyle.fontFamily.isEmpty() ? NodeStyle::kDefaultMapFontFamily : model.sheetStyle.fontFamily;
    QTextCursor cursor(document.get());
    bool first = true;
    const auto add = [&](const QString &text, qreal leftPx, qreal sizePt, bool bold, const QColor &color, qreal spaceAbovePx) {
        QTextBlockFormat block;
        block.setLeftMargin(leftPx);
        block.setTopMargin(spaceAbovePx);
        QTextCharFormat chars;
        QFont font(family);
        font.setPixelSize(qRound(sizePt * kPxPerPt));
        font.setBold(bold);
        chars.setFont(font);
        chars.setForeground(color);
        if (first) {
            cursor.setBlockFormat(block);
            cursor.setBlockCharFormat(chars);
            first = false;
        } else {
            cursor.insertBlock(block, chars);
        }
        cursor.insertText(text, chars);
    };
    const std::function<void(const MindNode *, int)> visit = [&](const MindNode *node, int depth) {
        const QString title = node->title.simplified();
        if (depth == 0) add(title, 0, 16, true, Qt::black, 0);
        else add(QStringLiteral("•  ") + title, depth * kIndentPx, 11, false, Qt::black, 4);
        const QString notes = node->notes.trimmed();
        if (!notes.isEmpty()) {
            const qreal left = depth == 0 ? 0 : depth * kIndentPx + 14;
            for (const QString &line : notes.split(QLatin1Char('\n')))
                add(line, left, 9, false, QColor(0x6b, 0x72, 0x80), 0);
        }
        for (const auto &child : node->children) visit(child.get(), depth + 1);
    };
    visit(top, 0);
    return document;
}
}  // namespace

MapExporter::FramedPages::FramedPages(std::unique_ptr<PageSource> pages, const ExportFrame &frame)
    : m_pages(std::move(pages)), m_frame(frame) {}

bool MapExporter::FramedPages::grows(int index) const { return m_pages->layout(index).pageSize().id() == QPageSize::Custom; }

QPageLayout MapExporter::FramedPages::layout(int index) const {
    QPageLayout layout = m_pages->layout(index);
    if (!grows(index)) return layout;
    const qreal bandPt = m_frame.width() / kPxPerPt;
    QSizeF points = layout.fullRectPoints().size() + QSizeF(2 * bandPt, 2 * bandPt);
    if (const qreal side = std::max(points.width(), points.height()); side > kMaxPdfPagePt) points *= kMaxPdfPagePt / side;
    return QPageLayout(QPageSize(points, QPageSize::Point, QString(), QPageSize::ExactMatch), QPageLayout::Portrait, QMarginsF());
}

void MapExporter::FramedPages::draw(int index, QPainter *painter, const QRectF &target) const {
    // The page in pixels (96 per inch) mapped to target, the frame and the inner page in it.
    const QSizeF pagePx = layout(index).fullRectPoints().size() * kPxPerPt;
    const qreal k = target.width() / pagePx.width();
    const qreal band = m_frame.width();
    // Grown: the frame on the page's edge, the page inside it. On paper: the frame inside the margins,
    // the page's drawing fitted inside the frame and its gap.
    const bool grow = grows(index);
    const qreal inset = grow ? band : band + kFrameGapPx;
    painter->save();
    painter->fillRect(target, Qt::white);
    painter->restore();
    const QRectF inner = target.adjusted(inset * k, inset * k, -inset * k, -inset * k);
    const QSizeF innerPt = m_pages->layout(index).fullRectPoints().size();
    // Keep the inner page's shape: centred in the room left.
    const QSizeF shown = innerPt.scaled(inner.size(), Qt::KeepAspectRatio);
    m_pages->draw(index, painter, QRectF(inner.center() - QPointF(shown.width() / 2, shown.height() / 2), shown));
    paintFrame(painter, target, pagePx, m_frame, grow);
}

void MapExporter::paintFrame(QPainter *painter, const QRectF &target, const QSizeF &pagePx, const ExportFrame &frame, bool onEdge) {
    if (!frame.enabled()) return;
    const qreal k = target.width() / pagePx.width();
    const qreal margin = onEdge ? 0 : kPageMarginMm * kPtPerMm * kPxPerPt;
    painter->save();
    painter->translate(target.topLeft());
    painter->scale(k, k);
    frame.paint(painter, QRectF(QPointF(margin, margin), pagePx - QSizeF(2 * margin, 2 * margin)));
    painter->restore();
}

QPageLayout MapExporter::pageLayout(PageSize size, bool landscape) {
    return QPageLayout(QPageSize(pageSizeId(size)), landscape ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF());
}

MapExporter::PagePlan::PagePlan(const QList<Page> &pages, const PageOptions &options) : m_options(options) {
    const bool fitToMap = options.size == PageSize::FitToMap && !options.outliner;
    const QPageLayout fixed(QPageSize(pageSizeId(options.size)), options.landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                            QMarginsF());
    const QSizeF fixedPt = fixed.fullRectPoints().size();
    const qreal margin = kPageMarginMm * kPtPerMm;
    const QRectF printablePt(QPointF(margin, margin), fixedPt - QSizeF(2 * margin, 2 * margin));
    for (qsizetype s = 0; s < pages.size(); ++s) {
        auto sheet = std::make_unique<Sheet>();
        sheet->rendering = std::make_unique<Rendering>(pages[s], 2, options.originalImagePath);
        if (!sheet->rendering->isValid()) {
            m_error = tr("The sheet \"%1\" was not exported: %2").arg(pages[s].name, sheet->rendering->error());
            m_pieces.clear();
            return;
        }
        const int index = int(m_sheets.size());
        if (options.outliner) {
            const MindMapModel *model = sheet->rendering->model();
            const MindNode *top = pages[s].branchId.isEmpty() ? model->root() : model->nodeById(pages[s].branchId);
            sheet->outline = outlineDocument(*model, top);
            const QSizeF pagePx = printablePt.size() * kPxPerPt;
            sheet->outline->setPageSize(pagePx);
            for (int p = 0; p < sheet->outline->pageCount(); ++p)
                m_pieces.push_back({index, fixedPt, printablePt, QRectF(0, p * pagePx.height(), pagePx.width(), pagePx.height())});
        } else {
            const QRectF source = sheet->rendering->source();
            const QSizeF contentPt = source.size() / kPxPerPt;
            if (fitToMap) {
                // As big as the map, within the PDF page limit (a larger map is scaled down, still vector).
                const qreal fit = std::min<qreal>(1, kMaxPdfPagePt / std::max(contentPt.width(), contentPt.height()));
                m_pieces.push_back({index, contentPt * fit, QRectF(QPointF(0, 0), contentPt * fit), source});
            } else if (!options.actualSize) {
                const qreal fit = std::min(printablePt.width() / contentPt.width(), printablePt.height() / contentPt.height());
                const QSizeF shown = contentPt * fit;
                const QPointF at = printablePt.center() - QPointF(shown.width() / 2, shown.height() / 2);
                m_pieces.push_back({index, fixedPt, QRectF(at, shown), source});
            } else {
                // 100%: tiles of the printable area, left to right and top to bottom.
                const QSizeF tilePx = printablePt.size() * kPxPerPt;
                for (qreal y = source.top(); y < source.bottom() - 0.5; y += tilePx.height())
                    for (qreal x = source.left(); x < source.right() - 0.5; x += tilePx.width()) {
                        const QRectF part = QRectF(QPointF(x, y), tilePx).intersected(source);
                        m_pieces.push_back({index, fixedPt, QRectF(printablePt.topLeft(), part.size() / kPxPerPt), part});
                    }
            }
        }
        m_sheets.push_back(std::move(sheet));
    }
    if (m_pieces.empty()) m_error = tr("There is nothing to export.");
}

MapExporter::PagePlan::~PagePlan() = default;

QPageLayout MapExporter::PagePlan::layout(int index) const {
    const QSizeF page = m_pieces.at(index).pagePt;
    const bool landscape = page.width() > page.height();
    return QPageLayout(QPageSize(landscape ? page.transposed() : page, QPageSize::Point, QString(), QPageSize::ExactMatch),
                       landscape ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF());
}

void MapExporter::PagePlan::draw(int index, QPainter *painter, const QRectF &target) const {
    if (index < 0 || index >= count()) return;
    const Piece &piece = m_pieces[index];
    const Sheet &sheet = *m_sheets[piece.sheet];
    const qreal k = target.width() / piece.pagePt.width();  // target units per point
    const QRectF content(target.topLeft() + piece.contentPt.topLeft() * k, piece.contentPt.size() * k);
    painter->save();
    painter->fillRect(target, Qt::white);
    if (sheet.outline) {
        painter->translate(content.topLeft());
        const qreal scale = k / kPxPerPt;  // the text is laid out in pixels at 96 DPI
        painter->scale(scale, scale);
        painter->translate(0, -piece.part.top());
        painter->setClipRect(piece.part);
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = piece.part;
        sheet.outline->documentLayout()->draw(painter, context);
    } else {
        if (m_options.background) painter->fillRect(target, sheet.rendering->background());
        sheet.rendering->drawPart(painter, content, piece.part, m_options.background);
    }
    painter->restore();
}

QImage MapExporter::toImage(const Page &page, const Options &options, bool *scaleReduced, QString *error) {
    qreal scale = std::clamp(options.scale, 1, 3);
    const Rendering rendering(page, scale, options.originalImagePath);
    if (!rendering.isValid()) {
        if (error) *error = rendering.error();
        return {};
    }
    const qreal band = options.frame.width();  // FEAT-084: the frame on the edge, around the margin
    const QSizeF source = rendering.source().size() + QSizeF(2 * band, 2 * band);
    const qreal longest = std::max(source.width(), source.height());
    const bool reduced = longest * scale > kMaxPngSidePx;
    if (reduced) scale = kMaxPngSidePx / longest;
    if (scaleReduced) *scaleReduced = reduced;
    const QSize size(std::max(1, int(std::floor(source.width() * scale))), std::max(1, int(std::floor(source.height() * scale))));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {  // not enough memory
        if (error) *error = tr("The image is too large to export.");
        return {};
    }
    image.fill(Qt::transparent);
    QPainter painter(&image);
    prepare(painter);
    if (band > 0 && options.background) painter.fillRect(QRectF(QPointF(0, 0), QSizeF(size)), rendering.background());
    const qreal k = qreal(size.width()) / source.width();
    rendering.draw(&painter, QRectF(QPointF(band, band) * k, (source - QSizeF(2 * band, 2 * band)) * k), options.background);
    painter.save();
    painter.scale(k, k);
    options.frame.paint(&painter, QRectF(QPointF(0, 0), source));
    painter.restore();
    painter.end();
    return image;
}

namespace {
MapExporter::Result writePng(const MapExporter::Page &page, const QString &path, const MapExporter::Options &options) {
    MapExporter::Result result;
    QString error;
    const QImage image = MapExporter::toImage(page, options, &result.scaleReduced, &error);
    if (image.isNull()) return {false, error};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {false, writeError(path, file.errorString())};
    if (!image.save(&file, "PNG") || !file.commit()) return {false, writeError(path, file.errorString())};
    return result;
}

MapExporter::Result writeSvg(const MapExporter::Page &page, const QString &path, const MapExporter::Options &options) {
    // Images inside a vector file at twice their shown size, like the map on a Standard screen.
    const MapExporter::Rendering rendering(page, 2, options.originalImagePath);
    if (!rendering.isValid()) return {false, rendering.error()};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {false, writeError(path, file.errorString())};
    const qreal band = options.frame.width();  // FEAT-084
    const QSizeF size = rendering.source().size() + QSizeF(2 * band, 2 * band);
    QSvgGenerator generator;
    generator.setOutputDevice(&file);
    generator.setSize(QSize(int(std::ceil(size.width())), int(std::ceil(size.height()))));
    generator.setViewBox(QRectF(QPointF(0, 0), size));
    generator.setResolution(96);
    generator.setTitle(page.name);
    generator.setDescription(QStringLiteral("Mentis"));
    QPainter painter;
    if (!painter.begin(&generator)) return {false, writeError(path, file.errorString())};
    prepare(painter);
    if (band > 0 && options.background) painter.fillRect(QRectF(QPointF(0, 0), size), rendering.background());
    rendering.draw(&painter, QRectF(QPointF(band, band), rendering.source().size()), options.background);
    options.frame.paint(&painter, QRectF(QPointF(0, 0), size));
    painter.end();
    if (!file.commit()) return {false, writeError(path, file.errorString())};
    return {};
}

}  // namespace

MapExporter::Result MapExporter::paint(const PageSource &plan, QPagedPaintDevice *device) {
    if (!plan.isValid()) return {false, plan.error()};
    QPainter painter;
    for (int i = 0; i < plan.count(); ++i) {
        const QPageLayout layout = plan.layout(i);
        device->setPageLayout(layout);  // applies to the next page
        if (i == 0) {
            if (!painter.begin(device)) return {false, tr("The pages cannot be drawn.")};
            prepare(painter);
        } else if (!device->newPage()) {
            return {false, tr("The pages cannot be drawn.")};
        }
        plan.draw(i, &painter, QRectF(QPointF(0, 0), layout.fullRectPixels(device->logicalDpiX()).size()));
    }
    painter.end();
    return {};
}

MapExporter::Result MapExporter::writePdf(const PageSource &plan, const QString &path, const QString &title, bool monochrome) {
    if (!plan.isValid()) return {false, plan.error()};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {false, writeError(path, file.errorString())};
    Result result;
    {
        QPdfWriter writer(&file);
        writer.setResolution(96);  // one device unit = one pixel of the map
        writer.setCreator(QStringLiteral("Mentis"));
        writer.setTitle(title);
        if (monochrome) writer.setColorModel(QPdfWriter::ColorModel::Grayscale);  // Qt 6.8
        result = paint(plan, &writer);
    }  // the writer finishes the file before the commit
    if (!result.ok) {
        file.cancelWriting();
        return result;
    }
    if (!file.commit()) return {false, writeError(path, file.errorString())};
    result.written = {path};
    return result;
}

MapExporter::Result MapExporter::write(Format format, const QList<Page> &pages, const QString &chosenPath, const Options &options) {
    if (pages.isEmpty()) return {false, tr("There is nothing to export.")};
    QStringList names;
    for (const Page &page : pages) names.append(page.name);
    const QStringList paths = pages.size() == 1 ? QStringList{chosenPath} : pagePaths(chosenPath, names);
    Result total;
    for (qsizetype i = 0; i < pages.size(); ++i) {
        const Result one = format == Format::Png ? writePng(pages[i], paths[i], options) : writeSvg(pages[i], paths[i], options);
        total.scaleReduced = total.scaleReduced || one.scaleReduced;
        if (!one.ok) {
            total.ok = false;
            total.error = pages.size() == 1 ? one.error : tr("The sheet \"%1\" was not exported: %2").arg(pages[i].name, one.error);
            return total;
        }
        total.written.append(paths[i]);
    }
    return total;
}

QString MapExporter::defaultFileName(const QString &name, const QDateTime &moment) {
    static const QRegularExpression notAllowed(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    QString cleaned = name.simplified().replace(notAllowed, QStringLiteral("-")).trimmed();
    if (cleaned.isEmpty()) cleaned = tr("Untitled");
    return moment.toString(QStringLiteral("yyyyMMdd'T'HHmmss")) + QLatin1Char(' ') + cleaned;
}

QStringList MapExporter::pagePaths(const QString &chosenPath, const QStringList &sheetNames) {
    const QFileInfo chosen(chosenPath);
    const QString suffix = chosen.suffix().isEmpty() ? QString() : QLatin1Char('.') + chosen.suffix();
    const QString base = chosen.dir().filePath(chosen.completeBaseName());
    // Not allowed in Windows file names (the strictest of the three systems), control characters included.
    static const QRegularExpression notAllowed(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    QSet<QString> used;
    QStringList paths;
    for (qsizetype i = 0; i < sheetNames.size(); ++i) {
        QString name = QString(sheetNames[i]).replace(notAllowed, QStringLiteral("-")).trimmed();
        while (name.endsWith(QLatin1Char('.'))) name.chop(1);  // Windows drops trailing dots
        if (name.isEmpty()) name = QString::number(i + 1);
        QString unique = name;
        for (int n = 2; used.contains(unique.toLower()); ++n) unique = name + QLatin1Char('-') + QString::number(n);
        used.insert(unique.toLower());
        paths.append(base + QLatin1Char('-') + unique + suffix);
    }
    return paths;
}
