// FEAT-089: Pitch templates. Their file format, the look they give, the sheet's copy (saved, opened
// and undone), the logo and the footer on the slides, the library and the editor of the Pitch tab.
#include "document.h"
#include "mindmapmodel.h"
#include "pitch.h"
#include "pitchpage.h"
#include "pitchtemplate.h"
#include "pitchtemplateeditor.h"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>

namespace {
QByteArray png(const QColor &color, const QSize &size = QSize(40, 20)) {
    QImage image(size, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}

Pitch::Template sample() {
    Pitch::Template value;
    value.id = QStringLiteral("tpl-test");
    value.name = QStringLiteral("School");
    value.base = Pitch::Theme::Proposal;
    value.background = QColor(0x10, 0x20, 0x30);
    value.coverText = QColor(0xfa, 0xfa, 0xfa);
    value.titleFamily = QStringLiteral("Arvo");
    value.itemStyle = Pitch::ItemStyle::Badge;
    value.motif = Pitch::Motif::None;
    value.logo = png(Qt::red);
    value.logoCorner = Pitch::Corner::TopLeft;
    value.logoSize = 15;
    value.footer = QStringLiteral("Liceo Galilei");
    value.numbers = true;
    return value;
}

QImage render(const Pitch::Slide &slide, const Pitch::Look &look) {
    QImage image(320, 180, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    Pitch::paint(&painter, QRectF(0, 0, 320, 180), slide, look);
    painter.end();
    return image;
}
}  // namespace

class PitchTemplateTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void jsonRoundTrip();
    void lookIsTheBaseWithTheTemplate();
    void sheetKeepsItsCopy();
    void logoAndFooterAreDrawn();
    void libraryFiles();
    void editorSavesToTheLibrary();
};

void PitchTemplateTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisPitchTemplateTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
}

void PitchTemplateTest::init() { QDir(PitchTemplates::folder()).removeRecursively(); }

void PitchTemplateTest::jsonRoundTrip() {
    const Pitch::Template value = sample();
    const auto read = Pitch::templateFromJson(Pitch::toJson(value));
    QVERIFY(read);
    QCOMPARE(*read, value);
    // Not a template: no format, or no id. Wrong values read as unset, a broken picture is dropped.
    QJsonObject object = Pitch::toJson(value);
    object.remove(QStringLiteral("format"));
    QVERIFY(!Pitch::templateFromJson(object));
    object = Pitch::toJson(value);
    object.insert(QStringLiteral("id"), QString());
    QVERIFY(!Pitch::templateFromJson(object));
    object = Pitch::toJson(value);
    object.insert(QStringLiteral("motif"), QStringLiteral("spiral"));
    object.insert(QStringLiteral("logoSize"), 400);
    object.insert(QStringLiteral("logo"), QStringLiteral("bm90IGEgcGljdHVyZQ=="));
    const auto lenient = Pitch::templateFromJson(object);
    QVERIFY(lenient);
    QVERIFY(!lenient->motif);
    QCOMPARE(lenient->logoSize, Pitch::kMaxLogoSize);
    QVERIFY(lenient->logo.isEmpty());
}

void PitchTemplateTest::lookIsTheBaseWithTheTemplate() {
    MindMapModel model;
    const Pitch::Template value = sample();
    const Pitch::Look base = Pitch::look(model, value.base);
    const Pitch::Look look = Pitch::look(model, value);
    QCOMPARE(look.background, value.background);
    QCOMPARE(look.text, base.text);  // not set: the base theme's
    QCOMPARE(look.coverText, value.coverText);
    QCOMPARE(look.titleFont.family, QStringLiteral("Arvo"));
    QCOMPARE(look.itemFont.family, base.itemFont.family);
    QCOMPARE(look.itemStyle, Pitch::ItemStyle::Badge);
    QCOMPARE(look.motif, Pitch::Motif::None);
    QCOMPARE(look.logo.size(), QSize(40, 20));
    QCOMPARE(look.logoSize, 0.15);
    QCOMPARE(look.footer, value.footer);
    QVERIFY(look.numbers);
    // The sheet: its template if it has one, otherwise its theme.
    QCOMPARE(Pitch::sheetLook(model).background, Pitch::look(model, Pitch::Theme::Dark).background);
    model.pitchTemplate = Pitch::toJson(value);
    QCOMPARE(Pitch::sheetLook(model).background, value.background);
}

void PitchTemplateTest::sheetKeepsItsCopy() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("deck.mentis"));
    const QJsonObject copy = Pitch::toJson(sample());
    {
        Document document;
        document.startUntitled();
        QVERIFY(document.setPitchSheet(QStringLiteral("proposal"), QString(), copy));
        QVERIFY(!document.setPitchSheet(QStringLiteral("proposal"), QString(), copy));
        // The aspect alone keeps the template; a theme with an empty copy removes it.
        QVERIFY(document.setPitchSheet(QStringLiteral("proposal"), QStringLiteral("4:3")));
        QCOMPARE(document.model()->pitchTemplate, copy);
        QVERIFY(document.setPitchSheet(QStringLiteral("light"), QStringLiteral("4:3"), QJsonObject()));
        QVERIFY(document.model()->pitchTemplate.isEmpty());
        QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
        QCOMPARE(document.model()->pitchTemplate, copy);
        QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
    }
    // Opened where the library has nothing: the same look.
    QVERIFY(PitchTemplates::all().isEmpty());
    Document reopened;
    QVERIFY(reopened.open(file, false).ok);
    QCOMPARE(reopened.model()->pitchTemplate, copy);
    QCOMPARE(Pitch::sheetLook(*reopened.model()).footer, QStringLiteral("Liceo Galilei"));
    // A copy that is not a template is dropped.
    QJsonObject sheet = reopened.model()->toJson();
    sheet.insert(QStringLiteral("pitchTemplate"), QJsonObject{{QStringLiteral("name"), QStringLiteral("x")}});
    MindMapModel broken;
    QVERIFY(broken.fromJson(sheet));
    QVERIFY(broken.pitchTemplate.isEmpty());
}

void PitchTemplateTest::logoAndFooterAreDrawn() {
    MindMapModel model;
    Pitch::Template value = sample();
    value.base = Pitch::Theme::Light;
    value.motif = Pitch::Motif::None;
    value.logoCorner = Pitch::Corner::BottomRight;
    Pitch::Slide slide;
    slide.title = QStringLiteral("Title");
    slide.number = 3;
    const QImage with = render(slide, Pitch::look(model, value));
    // The red logo in the bottom-right corner, inside the margin.
    QCOMPARE(with.pixelColor(320 - 9, 180 - 9), QColor(Qt::red));
    // The footer and the number change the foot of the slide; without them it is the ground only.
    Pitch::Template plain = value;
    plain.logo.clear();
    plain.footer.clear();
    plain.numbers = false;
    const QImage without = render(slide, Pitch::look(model, plain));
    const QRect foot(0, 180 - 20, 320, 20);
    QVERIFY(with.copy(foot) != without.copy(foot));
    plain.footer = value.footer;
    QVERIFY(render(slide, Pitch::look(model, plain)).copy(foot) != without.copy(foot));
    // A background picture covers the slide.
    Pitch::Template pictured = plain;
    pictured.footer.clear();
    pictured.picture = png(Qt::blue, QSize(64, 64));
    Pitch::Slide list;
    list.kind = Pitch::Slide::Kind::List;
    QCOMPARE(render(list, Pitch::look(model, pictured)).pixelColor(2, 2), QColor(Qt::blue));
}

void PitchTemplateTest::libraryFiles() {
    QVERIFY(PitchTemplates::all().isEmpty());
    const Pitch::Template value = sample();
    QVERIFY(PitchTemplates::save(value));
    QCOMPARE(PitchTemplates::all().size(), 1);
    QCOMPARE(*PitchTemplates::find(value.id), value);
    // New templates get a free name and an id of their own.
    const Pitch::Template first = PitchTemplates::create(Pitch::Theme::Night);
    QVERIFY(first.id.startsWith(QStringLiteral("tpl-")));
    QCOMPARE(first.base, Pitch::Theme::Night);
    QVERIFY(PitchTemplates::save(first));
    const Pitch::Template second = PitchTemplates::create(Pitch::Theme::Night);
    QVERIFY(second.name != first.name && second.id != first.id);
    // Export and import through a file; a file that is not a template is refused.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("school.mentis-theme"));
    QVERIFY(PitchTemplates::writeFile(value, path));
    QCOMPARE(*PitchTemplates::readFile(path), value);
    const QString bad = dir.filePath(QStringLiteral("bad.mentis-theme"));
    QFile file(bad);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"name\": \"x\"}");
    file.close();
    QString error;
    QVERIFY(!PitchTemplates::readFile(bad, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(PitchTemplates::remove(value.id));
    QVERIFY(!PitchTemplates::find(value.id));
    QCOMPARE(PitchTemplates::all().size(), 1);
}

void PitchTemplateTest::editorSavesToTheLibrary() {
    MindMapModel model;
    PitchPage page;
    QJsonObject applied;
    int applications = 0;
    page.setSheetTemplate = [&](const QJsonObject &copy) {
        applied = copy;
        ++applications;
        model.pitchTemplate = copy;
    };
    page.showTopic(&model, nullptr);
    page.newTemplate();
    QVERIFY(page.isEditingTemplate());
    PitchTemplateEditor *editor = page.templateEditor();
    auto *name = editor->findChild<QLineEdit *>(QStringLiteral("PitchTemplateName"));
    auto *footer = editor->findChild<QLineEdit *>(QStringLiteral("PitchTemplateFooter"));
    QVERIFY(name && footer);
    name->selectAll();
    QTest::keyClicks(name, QStringLiteral("Company"));
    QTest::keyClicks(footer, QStringLiteral("ACME"));
    QCOMPARE(editor->draft().name, QStringLiteral("Company"));
    QCOMPARE(editor->draft().footer, QStringLiteral("ACME"));
    editor->findChild<QPushButton *>(QStringLiteral("PitchTemplateSave"))->click();
    QVERIFY(!page.isEditingTemplate());
    QCOMPARE(applications, 1);
    QCOMPARE(PitchTemplates::all().size(), 1);
    QCOMPARE(PitchTemplates::all().front().footer, QStringLiteral("ACME"));
    QCOMPARE(Pitch::templateFromJson(applied)->name, QStringLiteral("Company"));
    // Edit the sheet's template, cancel: nothing changes. Delete: the sheet keeps its copy.
    page.showTopic(&model, nullptr);
    page.editSheetTemplate();
    QVERIFY(page.isEditingTemplate());
    QTest::keyClicks(footer, QStringLiteral(" Inc"));
    editor->findChild<QPushButton *>(QStringLiteral("PitchTemplateCancel"))->click();
    QCOMPARE(PitchTemplates::all().front().footer, QStringLiteral("ACME"));
    page.editSheetTemplate();
    editor->confirmDelete = [](const QString &) { return true; };
    editor->findChild<QPushButton *>(QStringLiteral("PitchTemplateDelete"))->click();
    QVERIFY(PitchTemplates::all().isEmpty());
    QVERIFY(!model.pitchTemplate.isEmpty());
    // Export from the editor, import into the library.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("company"));
    page.pickFile = [&](const QString &, const QString &, bool, const QString &) { return path; };
    page.editSheetTemplate();
    editor->findChild<QPushButton *>(QStringLiteral("PitchTemplateExport"))->click();
    const QString written = path + QStringLiteral(".mentis-theme");
    QVERIFY(QFile::exists(written));
    editor->findChild<QPushButton *>(QStringLiteral("PitchTemplateCancel"))->click();
    QVERIFY(page.importTemplate(written));
    QCOMPARE(PitchTemplates::all().size(), 1);
    QCOMPARE(applications, 2);
}

QTEST_MAIN(PitchTemplateTest)
#include "pitch_template_test.moc"
