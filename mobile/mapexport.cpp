#include "mapexport.h"

#include "document.h"
#include "ganttexport.h"
#include "ganttoffice.h"
#include "maplibrary.h"
#include "mapscene.h"
#include "mappages.h"
#include "mapsnapshot.h"
#include "mindmapmodel.h"
#include "mobileshare.h"
#include "pitch.h"
#include "pitchexport.h"
#include "pitchtemplate.h"
#include "zipwriter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
QString windowText(const char *text) { return QCoreApplication::translate("MainWindow", text); }
QString mapText(const char *text) { return QCoreApplication::translate("MobileMap", text); }

// C2: a sheet drawn by the phone for MapPages, as MapExporter::Rendering draws the desktop's scene:
// the sheet's model from its flat content, laid out by MapSnapshot, kMarginPx around what is drawn.
class PhoneDrawing final : public MapPages::Drawing {
public:
    PhoneDrawing(const MapPages::Page &page, qreal imageScale) : m_model(std::make_unique<MindMapModel>()), m_imageScale(imageScale) {
        if (!m_model->fromFlatSnapshot(page.content, &m_error)) {
            if (m_error.isEmpty()) m_error = QCoreApplication::translate("MapExporter", "The sheet cannot be read.");
            return;
        }
        m_snapshot = std::make_unique<MapSnapshot>(m_model.get());
        const QRectF content = m_snapshot->bounds();
        if (content.isEmpty()) {
            m_error = QCoreApplication::translate("MapExporter", "There is nothing to export.");
            return;
        }
        constexpr qreal margin = MapPages::kMarginPx;
        m_source = content.adjusted(-margin, -margin, margin, margin);
        m_background = m_model->backdrop().color;
    }
    void drawPart(QPainter *painter, const QRectF &target, const QRectF &part, bool background) const override {
        if (!isValid() || part.isEmpty()) return;
        // QGraphicsScene::render with Qt::KeepAspectRatio: part fitted in target, centred.
        const qreal k = std::min(target.width() / part.width(), target.height() / part.height());
        const QSizeF shown = part.size() * k;
        painter->save();
        painter->translate(target.center() - QPointF(shown.width() / 2, shown.height() / 2));
        painter->scale(k, k);
        painter->translate(-part.topLeft());
        painter->setClipRect(part, Qt::IntersectClip);
        if (background) painter->fillRect(part, m_background);
        MapScene::paint(painter, *m_snapshot, m_imageScale);
        painter->restore();
    }
    const MindMapModel *model() const override { return m_model.get(); }

private:
    std::unique_ptr<MindMapModel> m_model;
    std::unique_ptr<MapSnapshot> m_snapshot;
    qreal m_imageScale = 2;
};

MapPages::DrawingFactory phoneDrawing() {
    return [](const MapPages::Page &page, qreal imageScale, const std::function<QString(const MindNode *)> &) {
        return std::unique_ptr<MapPages::Drawing>(std::make_unique<PhoneDrawing>(page, imageScale));
    };
}

ExportFrame frameFrom(const QVariantMap &options) {
    const QVariantMap map = options.value(QStringLiteral("frame")).toMap();
    ExportFrame frame;
    frame.style = ExportFrame::Style(std::clamp(map.value(QStringLiteral("style"), 0).toInt(), 0, ExportFrame::kStyles - 1));
    frame.thickness = std::clamp(map.value(QStringLiteral("thickness"), ExportFrame::kDefaultThickness).toDouble(), ExportFrame::kMinThickness,
                                 ExportFrame::kMaxThickness);
    if (const QColor color(map.value(QStringLiteral("color")).toString()); color.isValid()) frame.color = color;
    frame.rounded = map.value(QStringLiteral("rounded")).toBool();
    return frame;
}

// FEAT-122 E: the Gantt tab's Content and Time unit.
GanttPages::Content contentFrom(const QVariantMap &options) {
    return GanttPages::Content(std::clamp(options.value(QStringLiteral("content"), 0).toInt(), 0, int(GanttPages::Content::Costs)));
}
GanttCanvas::Scale unitFrom(const QVariantMap &options) {
    return GanttCanvas::Scale(std::clamp(options.value(QStringLiteral("unit"), 0).toInt(), 0, int(GanttCanvas::Scale::Months)));
}
// PageDialog::ganttOptions.
GanttPages::Options ganttPageOptions(const QVariantMap &options) {
    GanttPages::Options values;
    values.content = contentFrom(options);
    values.size = MapPages::PageSize(std::clamp(options.value(QStringLiteral("pageSize"), 0).toInt(), 0, int(MapPages::PageSize::Legal)));
    values.landscape = options.value(QStringLiteral("landscape"), true).toBool();
    values.fitWidth = !options.value(QStringLiteral("actualSize")).toBool();
    values.scale = unitFrom(options);
    return values;
}

// Files into one .zip at path (the names only), or the error.
QString zipFiles(const QStringList &files, const QString &path) {
    ZipWriter zip;
    for (const QString &file : files) {
        QFile input(file);
        if (!input.open(QIODevice::ReadOnly)) return input.errorString();
        zip.add(QFileInfo(file).fileName(), input.readAll());
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(zip.archive()) < 0 || !output.commit()) return output.errorString();
    return {};
}
}  // namespace

MapExport::MapExport(QObject *parent) : QObject(parent) {}

MapExport::~MapExport() { finish(); }

void MapExport::setLibrary(MapLibrary *library) {
    if (m_library == library) return;
    m_library = library;
    emit libraryChanged();
}

QStringList MapExport::sheets() const {
    QStringList names;
    if (m_document)
        for (const Document::Sheet &sheet : m_document->sheets()) names.append(sheet.name);
    return names;
}

bool MapExport::prepared() const { return m_document != nullptr; }

QString MapExport::exportFolder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("exported-files"));
}

void MapExport::setError(const QString &error) {
    if (error == m_lastError) return;
    m_lastError = error;
    emit lastErrorChanged();
}

bool MapExport::prepare(const QUrl &url) {
    finish();
    setError(QString());
    if (!m_library) return false;
    bool created = false;
    const QString path = m_library->localCopy(url, &created);
    if (path.isEmpty()) {
        setError(m_library->lastError());
        return false;
    }
    auto document = std::make_unique<Document>();
    // As MobileMap::open: the integrity check only when SQLite left a journal.
    const bool integrityCheck = QFileInfo::exists(path + QStringLiteral("-journal")) || QFileInfo::exists(path + QStringLiteral("-wal"));
    const Document::Result result = document->open(path, integrityCheck);
    if (!result.ok) {
        if (created) m_library->discard(path);
        setError(result.fileLocked ? mapText("The file is already open in another Mentis window.") : result.error);
        return false;
    }
    m_document = std::move(document);
    m_copy = created ? path : QString();
    emit preparedChanged();
    return true;
}

void MapExport::finish() {
    if (!m_document) return;
    m_document->close();  // nothing was changed: no save, the lock is released
    m_document.reset();
    if (!m_copy.isEmpty() && m_library) m_library->discard(m_copy);
    m_copy.clear();
    emit preparedChanged();
}

QStringList MapExport::frameStyleNames() const {
    QStringList names;
    for (int s = int(ExportFrame::Style::Solid); s < ExportFrame::kStyles; ++s) names.append(ExportFrame::name(ExportFrame::Style(s)));
    return names;
}

int MapExport::currentSheet() const { return m_document ? m_document->currentSheet() : 0; }

bool MapExport::startExport(int sheet, QString *name) {
    setError(QString());
    m_lastFile.clear();
    if (!m_document || sheet < -1 || sheet >= m_document->sheets().size()) return false;
    if (sheet >= 0) {
        const Document::Result shown = m_document->switchSheet(sheet);
        if (!shown.ok) {
            setError(shown.error);
            return false;
        }
    }
    const QString documentName = QFileInfo(m_document->filePath()).completeBaseName();
    const QString sheetName = sheet >= 0 ? m_document->sheets().value(sheet).name : QString();
    *name = Document::exportFileName(sheetName.isEmpty() ? documentName : sheetName, QDateTime::currentDateTime());
    QDir(exportFolder()).removeRecursively();
    if (!QDir().mkpath(exportFolder())) {
        setError(windowText("The file could not be exported: %1").arg(QDir::toNativeSeparators(exportFolder())));
        return false;
    }
    return true;
}

bool MapExport::share(const QString &file) {
    m_lastFile = file;
    const QString shown = QFileInfo(file).fileName();
    if (!MobileShare::share(file, mapText("Share \"%1\"").arg(shown))) {
        setError(mapText("\"%1\" could not be shared.").arg(shown));
        return false;
    }
    return true;
}

namespace {
// The pages of one sheet or (sheet -1) every sheet, as MainWindow::exportPages: the active sheet from
// the model, the others from the document.
QList<MapPages::Page> pagesOf(Document &document, int sheet) {
    QList<MapPages::Page> pages;
    const QVector<Document::Sheet> &sheets = document.sheets();
    for (int i = 0; i < sheets.size(); ++i) {
        if (sheet >= 0 && i != sheet) continue;
        if (i == document.currentSheet()) {
            pages.append({sheets[i].name, document.model()->toFlatSnapshot(), {}});
            continue;
        }
        const Document::Result loaded = document.ensureSheetLoaded(i);
        pages.append({sheets[i].name, loaded.ok ? document.sheetContent(i) : MapHistory::Snapshot(), {}});
    }
    return pages;
}
}  // namespace

bool MapExport::runImage(const QVariantMap &options) {
    const int sheet = options.value(QStringLiteral("sheet"), -1).toInt();
    QString name;
    if (!startExport(sheet, &name)) return false;
    const bool svg = options.value(QStringLiteral("svg")).toBool();
    if (options.value(QStringLiteral("gantt")).toBool()) {
        // ExportDialog's Gantt tab: one picture of the sheet's table and chart.
        GanttExportSource source;
        if (sheet < 0 || !ganttSource(sheet, &source)) return false;
        const QString path = QDir(exportFolder()).filePath(name + (svg ? QStringLiteral(".svg") : QStringLiteral(".png")));
        const MapPages::Result result = GanttPages::write(svg ? MapPages::Format::Svg : MapPages::Format::Png, source, unitFrom(options), path,
                                                          std::clamp(options.value(QStringLiteral("scale"), 2).toInt(), 1, 3),
                                                          contentFrom(options), frameFrom(options));
        if (!result.ok) {
            setError(result.error);
            return false;
        }
        return share(path);
    }
    MapPages::Options values;
    values.background = options.value(QStringLiteral("background"), true).toBool();
    values.scale = std::clamp(options.value(QStringLiteral("scale"), 2).toInt(), 1, 3);
    values.frame = frameFrom(options);
    const QList<MapPages::Page> pages = pagesOf(*m_document, sheet);
    const QString path = QDir(exportFolder()).filePath(name + (svg ? QStringLiteral(".svg") : QStringLiteral(".png")));
    const MapPages::Result result = MapPages::write(svg ? MapPages::Format::Svg : MapPages::Format::Png, pages, path, values, phoneDrawing());
    if (!result.ok) {
        setError(result.error);
        return false;
    }
    if (result.written.size() == 1) return share(result.written.front());
    const QString zip = QDir(exportFolder()).filePath(name + QStringLiteral(".zip"));
    if (const QString error = zipFiles(result.written, zip); !error.isEmpty()) {
        setError(windowText("The file could not be exported: %1").arg(error));
        return false;
    }
    for (const QString &file : result.written) QFile::remove(file);
    return share(zip);
}

bool MapExport::runPdf(const QVariantMap &options) {
    const bool pitch = options.value(QStringLiteral("pitch")).toBool();
    const int sheet = options.value(QStringLiteral("sheet"), -1).toInt();
    if (pitch && sheet < 0) return false;  // a presentation is one sheet's
    QString name;
    if (!startExport(sheet, &name)) return false;
    const QString path = QDir(exportFolder()).filePath(name + QStringLiteral(".pdf"));
    const ExportFrame frame = frameFrom(options);
    if (pitch) {
        // PageDialog::run's Pitch: the slides keep the presentation's theme, ratio and colours.
        const MindMapModel &model = *m_document->model();
        const QList<Pitch::Slide> slides = Pitch::slides(model);
        if (slides.isEmpty()) {
            setError(windowText("No slides: every slide of this sheet is hidden"));
            return false;
        }
        const QString error = PitchExport::writePdf(path, slides, Pitch::sheetLook(model),
                                                    PitchExport::fileRatio(Pitch::aspectFromKey(model.pitchAspect)), name, frame);
        if (!error.isEmpty()) {
            setError(error);
            return false;
        }
        return share(path);
    }
    if (options.value(QStringLiteral("gantt")).toBool()) {
        // PageDialog's Gantt tab: the sheet's chart and costs on pages.
        GanttExportSource source;
        if (sheet < 0 || !ganttSource(sheet, &source)) return false;
        std::unique_ptr<MapPages::PageSource> pages = std::make_unique<GanttPages>(source, ganttPageOptions(options));
        if (frame.enabled()) pages = std::make_unique<MapPages::FramedPages>(std::move(pages), frame);
        const MapPages::Result result = MapPages::writePdf(*pages, path, name, options.value(QStringLiteral("monochrome")).toBool());
        if (!result.ok) {
            setError(result.error);
            return false;
        }
        return share(path);
    }
    // PageDialog::options and its plan, framed when the frame is on.
    MapPages::PageOptions values;
    values.size = MapPages::PageSize(std::clamp(options.value(QStringLiteral("pageSize"), 0).toInt(), 0, int(MapPages::PageSize::Legal)));
    values.landscape = options.value(QStringLiteral("landscape")).toBool();
    values.actualSize = options.value(QStringLiteral("actualSize")).toBool();
    values.background = options.value(QStringLiteral("background"), true).toBool();
    values.outliner = options.value(QStringLiteral("outliner")).toBool();  // FEAT-121 C: the indented titles and notes
    std::unique_ptr<MapPages::PageSource> plan = std::make_unique<MapPages::PagePlan>(pagesOf(*m_document, sheet), values, phoneDrawing());
    if (frame.enabled()) plan = std::make_unique<MapPages::FramedPages>(std::move(plan), frame);
    const MapPages::Result result = MapPages::writePdf(*plan, path, name, options.value(QStringLiteral("monochrome")).toBool());
    if (!result.ok) {
        setError(result.error);
        return false;
    }
    return share(path);
}

bool MapExport::run(int format, int sheet) {
    setError(QString());
    m_lastFile.clear();
    if (!m_document || format < Word || format > Markdown) return false;
    const bool wholeDocument = format == Word || format == Excel;
    if (!wholeDocument) {
        if (sheet < 0 || sheet >= m_document->sheets().size()) return false;
        const Document::Result shown = m_document->switchSheet(sheet);
        if (!shown.ok) {
            setError(shown.error);
            return false;
        }
    }
    // MainWindow::exportName: the sheet's name, the document's for the exports of every sheet.
    const QString documentName = QFileInfo(m_document->filePath()).completeBaseName();
    const QString sheetName = m_document->sheets().value(m_document->currentSheet()).name;
    const QString name = Document::exportFileName(wholeDocument || sheetName.isEmpty() ? documentName : sheetName,
                                                  QDateTime::currentDateTime());
    // One export at a time in the folder: what was shared before is not kept.
    QDir(exportFolder()).removeRecursively();
    if (!QDir().mkpath(exportFolder())) {
        setError(windowText("The file could not be exported: %1").arg(QDir::toNativeSeparators(exportFolder())));
        return false;
    }
    const QString file = write(format, exportFolder(), name);
    if (file.isEmpty()) return false;
    return share(file);
}

QString MapExport::write(int format, const QString &folder, const QString &name) {
    const QDir dir(folder);
    switch (format) {
    case Word:
    case Excel: {
        // MainWindow::exportOffice.
        const bool word = format == Word;
        const QString path = dir.filePath(name + (word ? QStringLiteral(".docx") : QStringLiteral(".xlsx")));
        const Document::ExportResult result = m_document->exportOffice(path, word ? Document::OfficeFormat::Word : Document::OfficeFormat::Excel, name);
        if (!result.ok) {
            setError(windowText("The file could not be exported: %1").arg(result.error));
            return {};
        }
        return path;
    }
    case PitchPowerPoint:
    case PitchPdf: {
        // MainWindow::exportPresentation and the PDF dialog's Pitch: the sheet's slides, look and ratio.
        const MindMapModel &model = *m_document->model();
        const QList<Pitch::Slide> slides = Pitch::slides(model);
        if (slides.isEmpty()) {
            setError(windowText("No slides: every slide of this sheet is hidden"));
            return {};
        }
        const bool pptx = format == PitchPowerPoint;
        const QString path = dir.filePath(name + (pptx ? QStringLiteral(".pptx") : QStringLiteral(".pdf")));
        const Pitch::Look look = Pitch::sheetLook(model);
        const qreal ratio = PitchExport::fileRatio(Pitch::aspectFromKey(model.pitchAspect));
        const QString error = pptx ? PitchExport::writePptx(path, slides, look, ratio, name) : PitchExport::writePdf(path, slides, look, ratio, name);
        if (!error.isEmpty()) {
            setError(windowText("The presentation could not be exported: %1").arg(error));
            return {};
        }
        return path;
    }
    case Markdown: {
        // MainWindow::exportMarkdown's folder (the .md and assets/), then that folder as one .zip.
        const QString markdown = Document::exportMarkdownPath(dir.filePath(QStringLiteral("markdown/") + name + QStringLiteral(".md")));
        if (!QDir().mkpath(QFileInfo(markdown).absolutePath())) {
            setError(windowText("The file could not be exported: %1").arg(QDir::toNativeSeparators(markdown)));
            return {};
        }
        const Document::ExportResult result = m_document->exportMarkdown(markdown);
        if (!result.ok) {
            setError(result.error);
            return {};
        }
        const QDir content(QFileInfo(markdown).absolutePath());
        ZipWriter zip;
        QDirIterator files(content.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString file = files.next();
            QFile input(file);
            if (!input.open(QIODevice::ReadOnly)) {
                setError(windowText("The file could not be exported: %1").arg(input.errorString()));
                return {};
            }
            zip.add(content.relativeFilePath(file), input.readAll());
        }
        const QString path = dir.filePath(name + QStringLiteral(".zip"));
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) || output.write(zip.archive()) < 0 || !output.commit()) {
            setError(windowText("The file could not be exported: %1").arg(output.errorString()));
            return {};
        }
        QDir(dir.filePath(QStringLiteral("markdown"))).removeRecursively();
        return path;
    }
    default: return {};
    }
}

bool MapExport::ganttSource(int sheet, GanttExportSource *source) {
    if (!m_document || sheet < 0 || sheet >= m_document->sheets().size()) return false;
    const Document::Result shown = m_document->switchSheet(sheet);
    if (!shown.ok) {
        setError(shown.error);
        return false;
    }
    // The Gantt's shown columns (FEAT-122 stage F), every task, no snapshot compared.
    *source = GanttExportSource::fromDocument(*m_document, GanttColumns::shownFields(GanttColumns::numbered(m_document->model()->root())));
    return true;
}

bool MapExport::hasGantt(int sheet) {
    // Only the plan: the Export modal asks this of every sheet.
    if (!m_document || sheet < 0 || sheet >= m_document->sheets().size() || !m_document->switchSheet(sheet).ok) return false;
    return !m_document->model()->gantt().tasks().isEmpty();
}

QString MapExport::ganttNote(const QVariantMap &options) {
    GanttExportSource source;
    if (!ganttSource(options.value(QStringLiteral("sheet"), 0).toInt(), &source) || source.rows.isEmpty()) return {};
    const GanttPages::Options chosen = ganttPageOptions(options);
    if (chosen.content == GanttPages::Content::Costs) return {};
    const GanttPages pages(source, chosen);
    if (!pages.isValid()) return {};
    // PageDialog::rebuild's note, in its words.
    const auto pageText = [](const char *text) { return QCoreApplication::translate("PageDialog", text); };
    const QString units[] = {pageText("days"), pageText("weeks"), pageText("months")};
    QStringList note;
    if (pages.chosenScale() != chosen.scale)
        note.append(pageText("In %1, to fit the page width with legible text.").arg(units[int(pages.chosenScale())]));
    if (pages.splitInWidth()) note.append(pageText("The timeline goes on more pages in width; the names repeat on each."));
    return note.join(QLatin1Char(' '));
}

bool MapExport::runGanttFile(int format, int sheet, int content, int unit) {
    if (format != Word && format != Excel && format != Markdown) return false;
    QString name;
    if (sheet < 0 || !startExport(sheet, &name)) return false;
    GanttExportSource source;
    if (!ganttSource(sheet, &source)) return false;
    // MainWindow::exportGanttFile: the sheet's name, one file (Markdown too: no attachments).
    const GanttOffice::Format kind = format == Word ? GanttOffice::Format::Word : format == Excel ? GanttOffice::Format::Excel : GanttOffice::Format::Markdown;
    const QString suffix = format == Word ? QStringLiteral(".docx") : format == Excel ? QStringLiteral(".xlsx") : QStringLiteral(".md");
    const QString path = QDir(exportFolder()).filePath(name + suffix);
    const QString error = GanttOffice::write(kind, path, source, GanttPages::Content(std::clamp(content, 0, int(GanttPages::Content::Costs))),
                                             GanttCanvas::Scale(std::clamp(unit, 0, int(GanttCanvas::Scale::Months))), name);
    if (!error.isEmpty()) {
        setError(windowText("The file could not be exported: %1").arg(error));
        return false;
    }
    return share(path);
}
