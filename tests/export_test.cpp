// FEAT-054 stage 2: PNG, SVG and PDF of the map as it appears (MapExporter) and the Export dialog in
// the real MainWindow (offscreen platform, separate settings).
#include "exportdialog.h"
#include "mainwindow.h"
#include "exportframe.h"
#include "mapexporter.h"
#include "toggleswitch.h"
#include "numberfield.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "pagedialog.h"
#include "performance.h"
#include "segmentedcontrol.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

namespace {
// A laid out sheet: central topic, two main topics, `grandchildren` under the first one and, with
// `far`, a floating topic far from the rest. The flat content is what the document keeps.
MapHistory::Snapshot sheet(int grandchildren = 2, bool far = false, QString *firstId = nullptr) {
    MindMapModel model;
    model.root()->title = QStringLiteral("Centrale");
    MindNode *first = model.addChild(model.root(), QStringLiteral("Primo"));
    model.addChild(model.root(), QStringLiteral("Secondo"));
    for (int i = 0; i < grandchildren; ++i) model.addChild(first, QStringLiteral("Nipote %1").arg(i + 1));
    if (far) {
        MindNode *floating = model.addChild(model.root(), QStringLiteral("Lontano"));
        floating->floating = true;
        floating->position = QPointF(3000, 3000);
    }
    if (firstId) *firstId = first->id;
    MindMapScene scene(&model);
    scene.autoLayout();
    return model.toFlatSnapshot();
}

QList<QPair<qreal, qreal>> mediaBoxes(const QByteArray &pdf) {
    QList<QPair<qreal, qreal>> boxes;
    static const QRegularExpression box(QStringLiteral("/MediaBox \\[0 0 ([0-9.]+) ([0-9.]+)\\]"));
    for (auto it = box.globalMatch(QString::fromLatin1(pdf)); it.hasNext();) {
        const auto match = it.next();
        boxes.append({match.captured(1).toDouble(), match.captured(2).toDouble()});
    }
    return boxes;
}
// The PDF's streams, inflated when Qt wrote them with zlib (FlateDecode), one after the other.
QByteArray pageStreams(const QByteArray &pdf) {
    QByteArray all;
    for (qsizetype at = pdf.indexOf("stream\n"); at >= 0; at = pdf.indexOf("stream\n", at + 1)) {
        if (at >= 3 && pdf.mid(at - 3, 3) == "end") continue;
        const qsizetype start = at + 7;
        const qsizetype end = pdf.indexOf("endstream", start);
        if (end < 0) break;
        const QByteArray data = pdf.mid(start, end - start);
        if (!data.startsWith('\x78')) {  // not a zlib stream
            all += data;
            continue;
        }
        const quint32 room = quint32(data.size()) * 50 + 1024;  // qUncompress wants the size first (a guess is enough)
        const QByteArray sized = QByteArray(1, char(room >> 24)) + char((room >> 16) & 0xff) + char((room >> 8) & 0xff)
                               + char(room & 0xff) + data;
        all += qUncompress(sized);
    }
    return all;
}
}  // namespace

class ExportTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void pagePathsCleanNamesAndRepeats();
    void defaultNameIsIsoBasic();
    void frameStylesStayInTheirBand();
    void frameGrowsImagesAndPages();
    void frameRowsInTheWindows();
    void pngFollowsScaleAndBackground();
    void branchLeavesTheRestOut();
    void svgKeepsTextAndDropsBackground();
    void pdfHasOnePagePerSheetShapedLikeIt();
    void pdfFixedPageFitsOrTiles();
    void pdfOutlinerFlowsOverPages();
    void pdfMonochromeIsGrey();
    void allSheetsWriteOneImagePerSheet();
    void writeErrorLeavesNoFile();
    void dialogExportsPngAndReports();
    void pdfDialogWritesAndPrintDialogOpens();

private:
    std::unique_ptr<MainWindow> m_window;
    QTemporaryDir m_dir;
};

void ExportTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisExportTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);
    QSettings().clear();  // the dialogs remember their choices: start from the defaults
    QVERIFY(m_dir.isValid());
}

void ExportTest::cleanupTestCase() { m_window.reset(); }

void ExportTest::frameStylesStayInTheirBand() {
    // FEAT-084: every style paints in its colour inside the band of its width, nothing outside nor
    // over the content; ornaments are at least 8 px.
    const QRectF outer(20, 20, 300, 160);
    for (int s = int(ExportFrame::Style::Solid); s < ExportFrame::kStyles; ++s)
        for (const bool rounded : {false, true}) {
            ExportFrame frame;
            frame.style = ExportFrame::Style(s);
            frame.thickness = 4;
            frame.rounded = rounded;
            frame.color = QColor(0x10, 0x80, 0x40);
            const double band = frame.width();
            QCOMPARE(band, ExportFrame::isOrnament(frame.style) ? 8.0 : 4.0);
            QImage image(340, 200, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            frame.paint(&painter, outer);
            painter.end();
            // The band: inside the outer edge, outside the inner one, the corners' curve followed.
            const double r = frame.radius();
            QPainterPath outerEdge, innerEdge;
            outerEdge.addRoundedRect(outer.adjusted(-1, -1, 1, 1), r + 1, r + 1);
            const double in = band + 3;
            innerEdge.addRoundedRect(outer.adjusted(in, in, -in, -in), std::max(0.0, r - in), std::max(0.0, r - in));
            int inBand = 0, outside = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    if (image.pixelColor(x, y).alpha() < 128) continue;
                    const QPointF at(x + 0.5, y + 0.5);
                    const bool inside = outerEdge.contains(at) && !innerEdge.contains(at);
                    (inside ? inBand : outside)++;
                }
            QVERIFY2(inBand > 100, qPrintable(ExportFrame::name(frame.style)));
            QVERIFY2(outside == 0, qPrintable(ExportFrame::name(frame.style)));
        }
    ExportFrame none;
    QCOMPARE(none.width(), 0.0);
    ExportFrame thick;
    thick.style = ExportFrame::Style::Solid;
    thick.thickness = 100;
    QCOMPARE(thick.width(), ExportFrame::kMaxThickness);
    thick.rounded = true;
    QCOMPARE(thick.radius(), 32.0);
}

void ExportTest::frameGrowsImagesAndPages() {
    // The image: larger by the frame on each side, the frame on its edge (opaque without the
    // background too), the map inside unchanged.
    const MapExporter::Page page{QStringLiteral("Foglio"), sheet(), {}};
    MapExporter::Options options;
    options.scale = 2;
    options.background = false;
    const QImage plain = MapExporter::toImage(page, options);
    options.frame.style = ExportFrame::Style::Solid;
    options.frame.thickness = 6;
    options.frame.color = QColor(0xe0, 0x7b, 0x1a);
    const QImage framed = MapExporter::toImage(page, options);
    QCOMPARE(framed.size(), plain.size() + QSize(24, 24));
    QCOMPARE(framed.pixelColor(2, framed.height() / 2), options.frame.color);
    QCOMPARE(framed.pixelColor(framed.width() / 2, framed.height() / 2), plain.pixelColor(plain.width() / 2, plain.height() / 2));
    // PDF on paper: the A4 stays A4, a frame inside its margins; Fit to map: the page grows.
    MapExporter::PageOptions fixed;
    fixed.size = MapExporter::PageSize::A4;
    MapExporter::FramedPages onPaper(std::make_unique<MapExporter::PagePlan>(QList<MapExporter::Page>{page}, fixed), options.frame);
    QCOMPARE(onPaper.layout(0).pageSize().id(), QPageSize::A4);
    QImage paper(595, 842, QImage::Format_ARGB32_Premultiplied);
    paper.fill(Qt::white);
    QPainter painter(&paper);
    onPaper.draw(0, &painter, QRectF(0, 0, 595, 842));
    painter.end();
    const int margin = int(std::lround(10 * 72 / 25.4));  // 10 mm in points
    QCOMPARE(paper.pixelColor(margin + 2, 421), options.frame.color);
    QCOMPARE(paper.pixelColor(margin - 6, 421), QColor(Qt::white));
    MapExporter::FramedPages fitted(std::make_unique<MapExporter::PagePlan>(QList<MapExporter::Page>{page}, MapExporter::PageOptions{}), options.frame);
    const MapExporter::PagePlan bare(QList<MapExporter::Page>{page}, MapExporter::PageOptions{});
    QCOMPARE(fitted.layout(0).fullRectPoints().width(), bare.layout(0).fullRectPoints().width() + 2 * 6 * 0.75);
    const QString pdf = m_dir.filePath(QStringLiteral("framed.pdf"));
    QVERIFY(MapExporter::writePdf(onPaper, pdf, QStringLiteral("Framed"), false).ok);
}

void ExportTest::frameRowsInTheWindows() {
    // Off when the window opens; on: Style, Thickness, Colour and Corners, and the pages framed.
    const auto pages = [](ExportDialog::Scope) { return QList<MapExporter::Page>{{QStringLiteral("Foglio"), sheet(), {}}}; };
    PageDialog pdf(nullptr, PageDialog::Mode::Pdf, QStringLiteral("Mappa"), false, pages, {});
    auto *toggle = pdf.findChild<ToggleSwitch *>(QStringLiteral("ExportFrame"));
    QVERIFY(toggle && !toggle->isChecked());
    QVERIFY(!pdf.findChild<QWidget *>(QStringLiteral("ExportFrameStyle"))->isVisibleTo(&pdf));
    QVERIFY(!dynamic_cast<const MapExporter::FramedPages *>(pdf.plan()));
    toggle->setChecked(true);
    QVERIFY(pdf.findChild<QWidget *>(QStringLiteral("ExportFrameStyle"))->isVisibleTo(&pdf));
    QVERIFY(pdf.findChild<QWidget *>(QStringLiteral("ExportFrameCorners"))->isVisibleTo(&pdf));
    QVERIFY(dynamic_cast<const MapExporter::FramedPages *>(pdf.plan()));
    auto *style = pdf.findChild<QComboBox *>(QStringLiteral("ExportFrameStyle"));
    // Chosen in the grid menu, as the user does: the menu emits activated only.
    emit style->activated(style->findData(int(ExportFrame::Style::Dotted)));
    QCOMPARE(style->currentData().toInt(), int(ExportFrame::Style::Dotted));
    auto *framed = dynamic_cast<const MapExporter::FramedPages *>(pdf.plan());
    QVERIFY(framed);
    style->showPopup();  // the grid menu, a tile clicked
    QWidget *grid = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (widget->objectName() == QStringLiteral("GridMenuPopup") && widget->isVisible()) grid = widget;
    QVERIFY(grid);
    QList<QToolButton *> tiles = grid->findChildren<QToolButton *>();
    QCOMPARE(tiles.size(), ExportFrame::kStyles - 1);
    tiles.at(style->findData(int(ExportFrame::Style::GreekKey)))->click();
    QCOMPARE(style->currentData().toInt(), int(ExportFrame::Style::GreekKey));
    QCOMPARE(pdf.findChild<NumberField *>(QStringLiteral("ExportFrameThickness"))->value(), ExportFrame::kMinOrnament);
    // The style is a picture select (user, 2026-10-31): a picture per style, its name as tooltip.
    QCOMPARE(style->count(), ExportFrame::kStyles - 1);
    for (int i = 0; i < style->count(); ++i) {
        QVERIFY(!style->itemIcon(i).isNull() && style->itemText(i).isEmpty());
        QCOMPARE(style->itemData(i, Qt::ToolTipRole).toString(), ExportFrame::name(ExportFrame::Style(style->itemData(i).toInt())));
    }
    ExportDialog image(nullptr, QStringLiteral("Mappa"), false, pages, {});
    QVERIFY(!image.findChild<ToggleSwitch *>(QStringLiteral("ExportFrame"))->isChecked());
    const QString png = m_dir.filePath(QStringLiteral("framed.png"));
    image.findChild<ToggleSwitch *>(QStringLiteral("ExportFrame"))->setChecked(true);
    image.pickPath = [&png](MapExporter::Format, const QString &) { return png; };
    for (QPushButton *button : image.findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("PNG")) button->click();
    MapExporter::Options options;
    QCOMPARE(QImage(png).size(), MapExporter::toImage(pages({}).front(), options).size() + QSize(16, 16));  // 4 px at 2x
}

void ExportTest::defaultNameIsIsoBasic() {
    // User, 2026-10-31: every export suggests "<ISO 8601 basic date and time> <sheet>", no ':'.
    const QDateTime moment(QDate(2026, 10, 31), QTime(11, 39, 11));
    QCOMPARE(MapExporter::defaultFileName(QStringLiteral("Foglio 1"), moment), QStringLiteral("20261031T113911 Foglio 1"));
    QCOMPARE(MapExporter::defaultFileName(QStringLiteral(" A/B: c? "), moment), QStringLiteral("20261031T113911 A-B- c-"));
    QCOMPARE(MapExporter::defaultFileName(QString(), moment), QStringLiteral("20261031T113911 Untitled"));
}

void ExportTest::pagePathsCleanNamesAndRepeats() {
    const QString chosen = m_dir.filePath(QStringLiteral("Mappa.png"));
    const QStringList paths = MapExporter::pagePaths(chosen, {QStringLiteral("Biologia"), QStringLiteral("A/B: c?"), QStringLiteral("biologia"),
                                                              QStringLiteral("Fine."), QString()});
    QCOMPARE(paths, QStringList({m_dir.filePath(QStringLiteral("Mappa-Biologia.png")), m_dir.filePath(QStringLiteral("Mappa-A-B- c-.png")),
                                 m_dir.filePath(QStringLiteral("Mappa-biologia-2.png")), m_dir.filePath(QStringLiteral("Mappa-Fine.png")),
                                 m_dir.filePath(QStringLiteral("Mappa-5.png"))}));
}

void ExportTest::pngFollowsScaleAndBackground() {
    const MapExporter::Page page{QStringLiteral("Foglio"), sheet(), {}};
    const MapExporter::Rendering rendering(page, 1, {});
    QVERIFY2(rendering.isValid(), qPrintable(rendering.error()));
    const QSizeF source = rendering.source().size();
    MapExporter::Options options;
    options.scale = 1;
    const QImage one = MapExporter::toImage(page, options);
    QCOMPARE(one.width(), int(source.width()));
    QCOMPARE(one.height(), int(source.height()));
    options.scale = 3;
    const QImage three = MapExporter::toImage(page, options);
    QCOMPARE(three.width(), int(source.width() * 3));
    // With the background the corner (in the margin) has the sheet's color, without it nothing.
    QCOMPARE(three.pixelColor(1, 1), rendering.background());
    options.background = false;
    QCOMPARE(MapExporter::toImage(page, options).pixelColor(1, 1).alpha(), 0);
}

void ExportTest::branchLeavesTheRestOut() {
    QString firstId;
    const MapHistory::Snapshot content = sheet(2, true, &firstId);
    const MapExporter::Rendering whole({QStringLiteral("Foglio"), content, {}}, 1, {});
    const MapExporter::Rendering branch({QStringLiteral("Foglio"), content, firstId}, 1, {});
    QVERIFY(whole.isValid() && branch.isValid());
    QVERIFY(whole.source().width() > 3000);  // the floating topic is in the sheet
    QVERIFY2(branch.source().width() < 600, qPrintable(QString::number(branch.source().width())));
    QVERIFY(whole.source().contains(branch.source()));
    const MapExporter::Rendering missing({QStringLiteral("Foglio"), content, QStringLiteral("no-such-topic")}, 1, {});
    QVERIFY(!missing.isValid());
}

void ExportTest::svgKeepsTextAndDropsBackground() {
    const MapExporter::Page page{QStringLiteral("Foglio"), sheet(), {}};
    const QString withPath = m_dir.filePath(QStringLiteral("con.svg"));
    const QString withoutPath = m_dir.filePath(QStringLiteral("senza.svg"));
    MapExporter::Options options;
    QVERIFY(MapExporter::write(MapExporter::Format::Svg, {page}, withPath, options).ok);
    options.background = false;
    QVERIFY(MapExporter::write(MapExporter::Format::Svg, {page}, withoutPath, options).ok);
    QFile with(withPath), without(withoutPath);
    QVERIFY(with.open(QIODevice::ReadOnly) && without.open(QIODevice::ReadOnly));
    const QString withText = QString::fromUtf8(with.readAll());
    const QString withoutText = QString::fromUtf8(without.readAll());
    QVERIFY(withoutText.contains(QStringLiteral("Centrale")));  // text, not a picture of it
    QVERIFY(withoutText.contains(QStringLiteral("Nipote 2")));
    const QString background = MapExporter::Rendering(page, 1, {}).background().name();
    QVERIFY2(withText.count(background, Qt::CaseInsensitive) > withoutText.count(background, Qt::CaseInsensitive),
             qPrintable(background));
}

void ExportTest::pdfHasOnePagePerSheetShapedLikeIt() {
    QString firstId;
    const MapHistory::Snapshot wide = sheet(2);
    // A branch of 16 stacked topics: taller than wide.
    const MapHistory::Snapshot tall = sheet(16, false, &firstId);
    const QString path = m_dir.filePath(QStringLiteral("mappa.pdf"));
    const MapExporter::PagePlan plan({{QStringLiteral("Larga"), wide, {}}, {QStringLiteral("Alta"), tall, firstId}}, {});
    const MapExporter::Result result = MapExporter::writePdf(plan, path, QStringLiteral("Mappa"), false);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.written, QStringList{path});
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto boxes = mediaBoxes(file.readAll());
    QCOMPARE(boxes.size(), 2);
    QVERIFY(boxes[0].first > boxes[0].second);   // landscape
    QVERIFY(boxes[1].first < boxes[1].second);   // portrait
    // Fit to map: the page is as big as the map, pixels at 96 DPI in points.
    const MapExporter::Rendering first({QStringLiteral("Larga"), wide, {}}, 1, {});
    QVERIFY(qAbs(boxes[0].first - first.source().width() * 0.75) < 1);
}

void ExportTest::pdfFixedPageFitsOrTiles() {
    const QList<MapExporter::Page> pages{{QStringLiteral("Uno"), sheet(30), {}}};
    MapExporter::PageOptions options;
    options.size = MapExporter::PageSize::A4;
    options.landscape = true;
    const MapExporter::PagePlan fitted(pages, options);
    QVERIFY2(fitted.isValid(), qPrintable(fitted.error()));
    QCOMPARE(fitted.count(), 1);
    const QString path = m_dir.filePath(QStringLiteral("a4.pdf"));
    QVERIFY(MapExporter::writePdf(fitted, path, {}, false).ok);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto boxes = mediaBoxes(file.readAll());
    QCOMPARE(boxes.size(), 1);
    QVERIFY(qAbs(boxes[0].first - 841.9) < 1 && qAbs(boxes[0].second - 595.3) < 1);  // A4 landscape
    // Actual size on A5: a map taller than the page goes on more pages.
    options.size = MapExporter::PageSize::A5;
    options.landscape = false;
    options.actualSize = true;
    const MapExporter::PagePlan tiled(pages, options);
    QVERIFY2(tiled.count() > 1, qPrintable(QString::number(tiled.count())));
}

void ExportTest::pdfOutlinerFlowsOverPages() {
    MapExporter::PageOptions options;
    options.outliner = true;  // Fit to map does not apply: A4
    const MapExporter::PagePlan small({{QStringLiteral("Uno"), sheet(2), {}}}, options);
    QCOMPARE(small.count(), 1);
    QVERIFY(qAbs(small.layout(0).fullRectPoints().width() - 595.3) < 1);
    const MapExporter::PagePlan long_({{QStringLiteral("Uno"), sheet(150), {}}}, options);
    QVERIFY2(long_.count() > 1, qPrintable(QString::number(long_.count())));
    // Something is drawn on the last page, nothing outside the text.
    QImage page(300, 424, QImage::Format_ARGB32);
    QPainter painter(&page);
    long_.draw(long_.count() - 1, &painter, QRectF(page.rect()));
    painter.end();
    bool ink = false;
    for (int y = 0; y < page.height() && !ink; ++y)
        for (int x = 0; x < page.width() && !ink; ++x) ink = page.pixelColor(x, y).lightness() < 128;
    QVERIFY(ink);
}

void ExportTest::pdfMonochromeIsGrey() {
    const MapExporter::PagePlan plan({{QStringLiteral("Uno"), sheet(), {}}}, {});
    const QString colour = m_dir.filePath(QStringLiteral("colore.pdf"));
    const QString grey = m_dir.filePath(QStringLiteral("grigio.pdf"));
    QVERIFY(MapExporter::writePdf(plan, colour, {}, false).ok);
    QVERIFY(MapExporter::writePdf(plan, grey, {}, true).ok);
    QFile colourFile(colour), greyFile(grey);
    QVERIFY(colourFile.open(QIODevice::ReadOnly) && greyFile.open(QIODevice::ReadOnly));
    const QByteArray colourPdf = colourFile.readAll(), greyPdf = greyFile.readAll();
    // Qt sets the colors in the page streams with "scn": three numbers in RGB, one in grey.
    static const QRegularExpression rgbColor(QStringLiteral("[0-9.]+ [0-9.]+ [0-9.]+ (scn|SCN)\\b"));
    static const QRegularExpression greyColor(QStringLiteral("\\n[0-9.]+ (scn|SCN)\\b"));
    const QString colourOps = QString::fromLatin1(pageStreams(colourPdf));
    const QString greyOps = QString::fromLatin1(pageStreams(greyPdf));
    QVERIFY(colourOps.contains(rgbColor));
    QVERIFY2(!greyOps.contains(rgbColor), qPrintable(greyOps.right(1200)));
    QVERIFY(greyOps.contains(greyColor));
}

void ExportTest::allSheetsWriteOneImagePerSheet() {
    const QString chosen = m_dir.filePath(QStringLiteral("Tutti.png"));
    const MapExporter::Result result = MapExporter::write(MapExporter::Format::Png,
        {{QStringLiteral("Uno"), sheet(), {}}, {QStringLiteral("Due"), sheet(1), {}}}, chosen, {});
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.written, QStringList({m_dir.filePath(QStringLiteral("Tutti-Uno.png")), m_dir.filePath(QStringLiteral("Tutti-Due.png"))}));
    for (const QString &path : result.written) QVERIFY(!QImage(path).isNull());
    QVERIFY(!QFile::exists(chosen));
}

void ExportTest::writeErrorLeavesNoFile() {
    const QString path = m_dir.filePath(QStringLiteral("manca/mappa.pdf"));
    const MapExporter::Result result = MapExporter::writePdf(MapExporter::PagePlan({{QStringLiteral("Uno"), sheet(), {}}}, {}), path, {}, false);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(!QFile::exists(path));
    // An unreadable sheet stops a PDF before anything is written.
    const QString pdf = m_dir.filePath(QStringLiteral("rotto.pdf"));
    const MapExporter::PagePlan plan({{QStringLiteral("Uno"), sheet(), {}}, {QStringLiteral("Rotto"), MapHistory::Snapshot(), {}}}, {});
    const MapExporter::Result broken = MapExporter::writePdf(plan, pdf, {}, false);
    QVERIFY(!broken.ok);
    QVERIFY(broken.error.contains(QStringLiteral("Rotto")));
    QVERIFY(!QFile::exists(pdf));
}

// The real window: the dialog opens on the active sheet, the PNG button writes the chosen file and
// the bottom bar says where.
void ExportTest::dialogExportsPngAndReports() {
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    qobject_cast<QStackedWidget *>(m_window->centralWidget())->setCurrentIndex(1);  // editor
    QAction *action = nullptr;
    for (auto *candidate : m_window->findChildren<QAction *>())
        if (candidate->text() == QStringLiteral("Export image…")) action = candidate;
    QVERIFY(action);
    const QString path = m_dir.filePath(QStringLiteral("dialogo.png"));
    bool seen = false;
    QTimer::singleShot(0, this, [&] {
        auto *dialog = qobject_cast<ExportDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        seen = true;
        auto *scope = dialog->findChild<SegmentedControl *>(QStringLiteral("ExportScope"));
        QVERIFY(scope);
        QCOMPARE(scope->currentIndex(), int(ExportDialog::Scope::ActiveSheet));
        // "20261031T113911 <sheet>" (ISO 8601 basic, user 2026-10-31).
        static const QRegularExpression suggested(QStringLiteral("^\\d{8}T\\d{6} \\S"));
        QVERIFY(suggested.match(dialog->findChild<QLineEdit *>(QStringLiteral("ExportFileName"))->text()).hasMatch());
        dialog->pickPath = [&](MapExporter::Format format, const QString &suggested) {
            if (format != MapExporter::Format::Png || !suggested.endsWith(QStringLiteral(".png"))) return QString();
            return path;
        };
        for (auto *button : dialog->findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("PNG")) button->click();
    });
    QTimer::singleShot(5000, this, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
    action->trigger();
    QVERIFY(seen);
    QVERIFY(!QImage(path).isNull());
    QVERIFY(m_window->statusBar()->currentMessage().contains(QStringLiteral("dialogo.png")));
}

// The PDF dialog starts on Fit to map; the Outliner tab moves it to A4 and switches off what does
// not apply; Export writes the chosen file. The print dialog opens with printer and copies.
void ExportTest::pdfDialogWritesAndPrintDialogOpens() {
    QVERIFY(m_window);
    const auto actionNamed = [this](const QString &text) -> QAction * {
        for (auto *candidate : m_window->findChildren<QAction *>())
            if (candidate->text() == text) return candidate;
        return nullptr;
    };
    QAction *pdf = actionNamed(QStringLiteral("Export PDF…"));
    QAction *print = actionNamed(QStringLiteral("Print…"));
    QVERIFY(pdf && print);
    QCOMPARE(print->shortcut(), QKeySequence(QKeySequence::Print));
    const QString path = m_dir.filePath(QStringLiteral("dialogo.pdf"));
    bool seen = false;
    QTimer::singleShot(0, this, [&] {
        auto *dialog = qobject_cast<PageDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        seen = true;
        auto *size = dialog->findChild<QComboBox *>(QStringLiteral("PageSize"));
        auto *kind = dialog->findChild<SegmentedControl *>(QStringLiteral("PageKind"));
        auto *scale = dialog->findChild<SegmentedControl *>(QStringLiteral("PageScale"));
        QVERIFY(size && kind && scale);
        QCOMPARE(size->currentData().toInt(), int(MapExporter::PageSize::FitToMap));
        QVERIFY(!scale->isEnabled());
        QVERIFY(dialog->plan() && dialog->plan()->count() == 1);
        kind->setFocus();
        QTest::keyClick(kind, Qt::Key_Right);  // Outliner
        QCOMPARE(size->currentData().toInt(), int(MapExporter::PageSize::A4));
        QVERIFY(!scale->isEnabled());
        dialog->pickPath = [&](const QString &suggested) { return suggested.endsWith(QStringLiteral(".pdf")) ? path : QString(); };
        dialog->acceptButton()->click();
    });
    QTimer::singleShot(5000, this, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
    pdf->trigger();
    QVERIFY(seen);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().startsWith("%PDF"));
    QVERIFY(m_window->statusBar()->currentMessage().contains(QStringLiteral("dialogo.pdf")));

    seen = false;
    QTimer::singleShot(0, this, [&] {
        auto *dialog = qobject_cast<PageDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        seen = true;
        QVERIFY(dialog->findChild<QComboBox *>(QStringLiteral("PagePrinter")));
        QVERIFY(dialog->findChild<QSpinBox *>(QStringLiteral("PageCopies")));
        // Print has no Fit to map: paper only.
        QCOMPARE(dialog->findChild<QComboBox *>(QStringLiteral("PageSize"))->findData(int(MapExporter::PageSize::FitToMap)), -1);
        dialog->cancelButton()->click();  // nothing is printed by the test
    });
    print->trigger();
    QVERIFY(seen);
}

QTEST_MAIN(ExportTest)
#include "export_test.moc"
