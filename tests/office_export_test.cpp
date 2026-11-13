// FEAT-054 stage 4: every sheet as a Word (.docx) or Excel (.xlsx) file (OfficeExport), from the
// document and from "Export > Word…" / "Excel…" in the real MainWindow (offscreen platform).
#include "formulatext.h"
#include "document.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "officeexport.h"
#include "ooxmltest.h"
#include "performance.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using OoxmlTest::unzip;

class OfficeExportTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void wordHeadingsByLevel();
    void wordEmbedsPictures();
    void excelColumnsByLevel();
    void worksheetNamesAreValid();
    void documentExportsEverySheet();
    void menuExportsWordAndExcel();

private:
    QTemporaryDir m_dir;
};

void OfficeExportTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisOfficeExportTest"));
    FormulaText::installRendering();  // FEAT-096: formula pictures, as the app does at startup
    Performance::setMode(Performance::Mode::Light);
    QSettings().clear();
    QVERIFY(m_dir.isValid());
}

// Title, Heading 1-9, a bulleted list deeper; a new page per sheet; link, labels, task, note lines.
void OfficeExportTest::wordHeadingsByLevel() {
    MindMapModel first;
    first.root()->title = QStringLiteral("Plan & co");
    first.root()->notes = QStringLiteral("First line\n\nSecond line");
    MindNode *node = first.root();
    for (int level = 1; level <= 11; ++level) node = first.addChild(node, QStringLiteral("Level %1").arg(level));
    MindNode *linked = first.addChild(first.root(), QStringLiteral("Wiki"));
    linked->webLink = QStringLiteral("https://example.org/?a=1&b=2");
    linked->labels = {QStringLiteral("red"), QStringLiteral("blue")};
    linked->task = MindNode::Task::Done;
    MindMapModel second;
    second.root()->title = QStringLiteral("Second sheet");
    const OfficeExport::Package package = OfficeExport::docx({{QStringLiteral("A"), &first}, {QStringLiteral("B"), &second}}, {}, QStringLiteral("Doc"));
    const QMap<QString, QByteArray> parts = unzip(package.bytes);
    QCOMPARE(OoxmlTest::packageProblem(parts), QString());
    QVERIFY(package.skipped.isEmpty());
    const QByteArray document = parts.value(QStringLiteral("word/document.xml"));
    QCOMPARE(document.count("<w:pStyle w:val=\"Title\"/>"), 2);
    for (int level = 1; level <= 9; ++level)
        QVERIFY2(document.contains(QStringLiteral("<w:pStyle w:val=\"Heading%1\"/>").arg(level).toUtf8()), qPrintable(QString::number(level)));
    QCOMPARE(document.count("<w:pStyle w:val=\"ListBullet\"/>"), 2);  // levels 10 and 11
    QVERIFY(document.contains("<w:ilvl w:val=\"1\"/>"));
    QCOMPARE(document.count("<w:pageBreakBefore/>"), 1);
    QVERIFY(document.contains("Plan &amp; co"));
    QVERIFY(document.contains(">First line<") && document.contains(">Second line<"));
    QVERIFY(document.contains(QStringLiteral(">☑ Wiki<").toUtf8()));
    QVERIFY(document.contains(">red, blue<"));
    QVERIFY(document.contains("<w:hyperlink r:id=\"rId3\">"));
    const QByteArray relations = parts.value(QStringLiteral("word/_rels/document.xml.rels"));
    QVERIFY(relations.contains("Target=\"https://example.org/?a=1&amp;b=2\" TargetMode=\"External\""));
    const QByteArray styles = parts.value(QStringLiteral("word/styles.xml"));
    QVERIFY(styles.contains("w:name w:val=\"heading 9\"") && styles.contains("<w:outlineLvl w:val=\"8\"/>"));
    QVERIFY(parts.value(QStringLiteral("docProps/core.xml")).contains(">Doc<"));
}

// JPEG and PNG as they are, at the map's size (9525 EMU per pixel) and at most the text width; a
// formula as a PNG; a missing image left out and named.
void OfficeExportTest::wordEmbedsPictures() {
    const QString pngPath = m_dir.filePath(QStringLiteral("photo.png"));
    const QString jpegPath = m_dir.filePath(QStringLiteral("photo.jpg"));
    QImage image(40, 20, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(pngPath) && image.save(jpegPath));
    MindMapModel model;
    MindNode *png = model.addChild(model.root(), QStringLiteral("Png"));
    png->imagePath = pngPath;
    png->imageSize = QSizeF(100, 50);
    MindNode *jpeg = model.addChild(model.root(), QStringLiteral("Jpeg"));
    jpeg->imagePath = QStringLiteral("missing-thumbnail.jpg");
    jpeg->imageAssetId = QStringLiteral("asset");
    jpeg->imageSize = QSizeF(2000, 1000);  // wider than the page's text
    MindNode *formula = model.addChild(model.root(), QStringLiteral("Formula"));
    formula->formula = QStringLiteral("x^2");
    formula->imageSize = QSizeF(80, 40);
    MindNode *missing = model.addChild(model.root(), QStringLiteral("Gone"));
    missing->imagePath = m_dir.filePath(QStringLiteral("nowhere.png"));
    const auto source = [&](const MindNode *node) { return node == jpeg ? jpegPath : QString(); };
    const OfficeExport::Package package = OfficeExport::docx({{QStringLiteral("A"), &model}}, source, {});
    QCOMPARE(package.skipped, QStringList({QStringLiteral("Gone")}));
    const QMap<QString, QByteArray> parts = unzip(package.bytes);
    QCOMPARE(OoxmlTest::packageProblem(parts), QString());
    QFile pngFile(pngPath), jpegFile(jpegPath);
    QVERIFY(pngFile.open(QIODevice::ReadOnly) && jpegFile.open(QIODevice::ReadOnly));
    QCOMPARE(parts.value(QStringLiteral("word/media/image1.png")), pngFile.readAll());
    QCOMPARE(parts.value(QStringLiteral("word/media/image2.jpeg")), jpegFile.readAll());
    QVERIFY(parts.value(QStringLiteral("word/media/image3.png")).startsWith("\x89PNG"));
    QCOMPARE(parts.keys().filter(QStringLiteral("word/media/")).size(), 3);
    const QByteArray document = parts.value(QStringLiteral("word/document.xml"));
    QVERIFY(document.contains("<wp:extent cx=\"952500\" cy=\"476250\"/>"));
    QVERIFY(document.contains("<wp:extent cx=\"6120000\" cy=\"3060000\"/>"));
    QVERIFY(document.contains("<wp:extent cx=\"762000\" cy=\"381000\"/>"));
}

// A column per level, the optional columns only when used, text cut at Excel's cell limit.
void OfficeExportTest::excelColumnsByLevel() {
    MindMapModel model;
    model.root()->title = QStringLiteral("Centre");
    MindNode *child = model.addChild(model.root(), QStringLiteral("Child"));
    MindNode *grandchild = model.addChild(child, QStringLiteral("Grandchild"));
    grandchild->notes = QString(40000, QLatin1Char('n'));
    grandchild->task = MindNode::Task::Open;
    child->formula = QStringLiteral("a+b");
    MindMapModel plain;
    plain.root()->title = QStringLiteral("Plain");
    const QMap<QString, QByteArray> parts = unzip(OfficeExport::xlsx({{QStringLiteral("One"), &model}, {QStringLiteral("Two"), &plain}}, QStringLiteral("Book")));
    QCOMPARE(OoxmlTest::packageProblem(parts), QString());
    const QByteArray workbook = parts.value(QStringLiteral("xl/workbook.xml"));
    QVERIFY(workbook.contains("name=\"One\"") && workbook.contains("name=\"Two\""));
    const QByteArray sheet = parts.value(QStringLiteral("xl/worksheets/sheet1.xml"));
    for (const char *header : {"Central topic", "Level 1", "Level 2", "Notes", "Task", "Formula"}) QVERIFY2(sheet.contains(header), header);
    QVERIFY(!sheet.contains("Labels") && !sheet.contains(">Link<") && !sheet.contains("Level 3"));
    QVERIFY(sheet.contains("<c r=\"A2\" t=\"inlineStr\"><is><t xml:space=\"preserve\">Centre</t>"));
    QVERIFY(sheet.contains("<c r=\"B3\" t=\"inlineStr\"><is><t xml:space=\"preserve\">Child</t>"));
    QVERIFY(sheet.contains("<c r=\"C4\" t=\"inlineStr\"><is><t xml:space=\"preserve\">Grandchild</t>"));
    QVERIFY(sheet.contains(">$a+b$<"));
    QVERIFY(sheet.contains(QStringLiteral(">☐<").toUtf8()));
    QVERIFY(sheet.contains(QByteArray(32767, 'n') + '<') && !sheet.contains(QByteArray(32768, 'n')));
    QVERIFY(sheet.contains("state=\"frozen\""));
    const QByteArray other = parts.value(QStringLiteral("xl/worksheets/sheet2.xml"));
    QVERIFY(other.contains("Central topic") && !other.contains("Level 1") && !other.contains("Notes"));
}

void OfficeExportTest::worksheetNamesAreValid() {
    const QString longName(40, QLatin1Char('x'));
    QCOMPARE(OfficeExport::worksheetNames({QStringLiteral("a[b]:c"), QStringLiteral("A[B]:C"), QString(), longName, longName,
                                           QStringLiteral("History"), QStringLiteral("'quoted'")}),
             QStringList({QStringLiteral("a-b--c"), QStringLiteral("A-B--C-2"), QStringLiteral("Sheet 3"), QString(31, QLatin1Char('x')),
                          QString(29, QLatin1Char('x')) + QStringLiteral("-2"), QStringLiteral("History-2"), QStringLiteral("-quoted-")}));
}

// Every sheet, the active one with its latest content; an unwritable file gives an error and no file.
void OfficeExportTest::documentExportsEverySheet() {
    Document document;
    document.startUntitled();
    document.model()->root()->title = QStringLiteral("First sheet");
    document.addSheet();
    document.model()->root()->title = QStringLiteral("Active sheet");
    const QString book = m_dir.filePath(QStringLiteral("book.xlsx"));
    const Document::ExportResult result = document.exportOffice(book, Document::OfficeFormat::Excel, QStringLiteral("Book"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QFile file(book);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QMap<QString, QByteArray> parts = unzip(file.readAll());
    QVERIFY(parts.value(QStringLiteral("xl/worksheets/sheet1.xml")).contains("First sheet"));
    QVERIFY(parts.value(QStringLiteral("xl/worksheets/sheet2.xml")).contains("Active sheet"));
    const QString blocked = m_dir.filePath(QStringLiteral("no/such/folder/doc.docx"));
    const Document::ExportResult failed = document.exportOffice(blocked, Document::OfficeFormat::Word, {});
    QVERIFY(!failed.ok && !failed.error.isEmpty());
    QVERIFY(!QFile::exists(blocked));
    document.close();
}

void OfficeExportTest::menuExportsWordAndExcel() {
    MainWindow window;
    for (const auto &[text, suffix] : {std::pair{"Export Word…", ".docx"}, std::pair{"Export Excel…", ".xlsx"}}) {
        QAction *action = nullptr;
        for (auto *candidate : window.findChildren<QAction *>())
            if (candidate->text() == QString::fromUtf8(text)) action = candidate;
        QVERIFY2(action, text);
        const QString path = m_dir.filePath(QStringLiteral("from-menu"));
        window.setExportFilePicker([path] { return path; });
        action->trigger();
        QVERIFY2(QFile::exists(path + QString::fromLatin1(suffix)), suffix);
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("from-menu")));
    }
}

QTEST_MAIN(OfficeExportTest)
#include "office_export_test.moc"
