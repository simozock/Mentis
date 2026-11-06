// FEAT-063 stage A: slides made from the map, their settings saved and undone, the drawing, the
// full-screen presentation and the Pitch tab in the real window (offscreen platform).
#include "document.h"
#include "mainwindow.h"
#include "modaldialog.h"
#include "pagedialog.h"
#include "segmentedcontrol.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "nodeitem.h"
#include "performance.h"
#include "pitch.h"
#include "pitchexport.h"
#include "pitchpage.h"
#include "pitchwindow.h"
#include "ooxmltest.h"
#include "zipwriter.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QFontMetricsF>
#include <QPainter>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <memory>

namespace {
QStringList describe(const QList<Pitch::Slide> &slides) {
    QStringList result;
    for (const Pitch::Slide &slide : slides)
        result.append((slide.kind == Pitch::Slide::Kind::Topic ? QStringLiteral("T:") : QStringLiteral("L:")) + slide.title
                      + (slide.items.isEmpty() ? QString() : QLatin1Char('[') + slide.items.join(QLatin1Char(',')) + QLatin1Char(']')));
    return result;
}

int inkPixels(const QImage &image, const QColor &background) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y) != background) ++count;
    return count;
}
}  // namespace

class PitchTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void slidesFollowTheMap();
    void longListsContinue();
    void deliverySetsTheSteps();
    void settingsAreSavedAndUndone();
    void unknownValuesReadAsDefault();
    void listItemsAppearWhenShown();
    void presentationKeys();
    void allAtOnceShowsEveryItem();
    void tabAndPresentButton();
    void figuresFollowTheNode();
    void figureKeepsTheProportion();
    void listsWithFiguresHoldFewer();
    void slidesDrawTheFigure();
    void themesAreSavedByKey();
    void themesKeepTheContrast();
    void motifsStayOffTheText();
    void longTitleWordShrinks();
    void exportWritesPptxAndPdf();
    void pdfDialogOffersPitch();

private:
    std::unique_ptr<MainWindow> m_window;
    QTemporaryDir m_dir;
};

void PitchTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisPitchTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);  // no animation: the state changes at once
    QVERIFY(m_dir.isValid());
}

void PitchTest::cleanupTestCase() { m_window.reset(); }

void PitchTest::slidesFollowTheMap() {
    MindMapModel model;
    model.root()->title = QStringLiteral("C");
    MindNode *a = model.addChild(model.root(), QStringLiteral("A"));
    MindNode *b = model.addChild(model.root(), QStringLiteral("B"));
    MindNode *a1 = model.addChild(a, QStringLiteral("A1"));
    model.addChild(a1, QStringLiteral("A1x"));
    model.addChild(a, QStringLiteral("A2"));
    MindNode *floating = model.addChild(model.root(), QStringLiteral("F"));
    floating->floating = true;
    a->collapsed = true;  // presented all the same
    // Floating topics come after the branches; leaves only appear in their parent's list; deeper
    // topics have no topic slide by default.
    QCOMPARE(describe(Pitch::slides(model)),
             QStringList({QStringLiteral("T:C"), QStringLiteral("L:C[A,B,F]"), QStringLiteral("T:A"), QStringLiteral("L:A[A1,A2]"),
                          QStringLiteral("L:A1[A1x]"), QStringLiteral("T:B"), QStringLiteral("T:F")}));
    QVERIFY(Pitch::slides(model).front().cover);
    a1->pitchTopicSlide = Pitch::visibilityKey(Pitch::Visibility::Visible);
    a->pitchListSlide = Pitch::visibilityKey(Pitch::Visibility::Hidden);
    b->pitchTopicSlide = Pitch::visibilityKey(Pitch::Visibility::Hidden);
    b->pitchListSlide = Pitch::visibilityKey(Pitch::Visibility::Visible);  // no children: no list all the same
    QCOMPARE(describe(Pitch::slides(model)),
             QStringList({QStringLiteral("T:C"), QStringLiteral("L:C[A,B,F]"), QStringLiteral("T:A"), QStringLiteral("T:A1"),
                          QStringLiteral("L:A1[A1x]"), QStringLiteral("T:F")}));
    // The preview: the list slide when the topic has one, else its topic slide.
    QCOMPARE(Pitch::previewSlide(model, a).kind, Pitch::Slide::Kind::List);
    QCOMPARE(Pitch::previewSlide(model, b).kind, Pitch::Slide::Kind::Topic);
    QCOMPARE(Pitch::previewSlide(model, nullptr).items, QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("F")}));
}

void PitchTest::longListsContinue() {
    MindMapModel model;
    for (int i = 0; i < 30; ++i) model.addChild(model.root(), QStringLiteral("T%1").arg(i));
    QList<Pitch::Slide> lists;
    for (const Pitch::Slide &slide : Pitch::slides(model))
        if (slide.kind == Pitch::Slide::Kind::List) lists.append(slide);
    QCOMPARE(lists.size(), 2);
    QCOMPARE(lists[0].items.size(), Pitch::kMaxListItems);
    QCOMPARE(lists[1].items.size(), 30 - Pitch::kMaxListItems);
    QVERIFY(!lists[0].continued && lists[1].continued);
    QCOMPARE(lists[1].title, lists[0].title);
    // Per slide: org chart 6 (one row), fishbone / timeline / circle 8, grid 9 (3 x 3).
    for (const auto &[layout, expected] : {std::pair{Pitch::Layout::Org, 5}, {Pitch::Layout::Fishbone, 4}, {Pitch::Layout::Timeline, 4},
                                           {Pitch::Layout::Circle, 4}, {Pitch::Layout::Grid, 4}, {Pitch::Layout::Numbered, 2}}) {
        model.root()->pitchLayout = Pitch::layoutKey(layout);
        int count = 0;
        for (const Pitch::Slide &slide : Pitch::slides(model)) count += slide.kind == Pitch::Slide::Kind::List;
        QVERIFY2(count == expected, qPrintable(Pitch::layoutKey(layout)));
    }
    QCOMPARE(Pitch::validLayout(QStringLiteral("timeline")), QStringLiteral("timeline"));
}

void PitchTest::deliverySetsTheSteps() {
    Pitch::Slide slide;
    slide.kind = Pitch::Slide::Kind::List;
    slide.items = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};
    QCOMPARE(slide.steps(), 3);  // one at a time - memorize, the default
    slide.delivery = Pitch::Delivery::One;
    QCOMPARE(slide.steps(), 3);
    slide.delivery = Pitch::Delivery::All;
    QCOMPARE(slide.steps(), 0);
    slide.kind = Pitch::Slide::Kind::Topic;
    slide.delivery = Pitch::Delivery::One;
    QCOMPARE(slide.steps(), 0);
}

void PitchTest::settingsAreSavedAndUndone() {
    const QString file = m_dir.filePath(QStringLiteral("pitch.mentis"));
    {
        Document document;
        document.startUntitled();
        MindNode *topic = document.addChild(nullptr, QStringLiteral("Uno"));
        document.flushPendingStep();
        QVERIFY(document.setPitchTopic(topic, Document::PitchField::Layout, Pitch::layoutKey(Pitch::Layout::Fan)));
        QVERIFY(!document.setPitchTopic(topic, Document::PitchField::Layout, Pitch::layoutKey(Pitch::Layout::Fan)));
        QVERIFY(document.setPitchTopic(topic, Document::PitchField::Delivery, Pitch::deliveryKey(Pitch::Delivery::All)));
        QVERIFY(document.setPitchSheet(Pitch::themeKey(Pitch::Theme::Light), Pitch::aspectKey(Pitch::Aspect::Classic)));
        // Each choice is one step: the sheet's, then the delivery go back, the layout stays.
        QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
        QCOMPARE(document.model()->pitchTheme, QString());
        QCOMPARE(document.model()->pitchAspect, QString());
        QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
        MindNode *restored = document.model()->root()->children.front().get();
        QCOMPARE(restored->pitchDelivery, QString());
        QCOMPARE(restored->pitchLayout, QStringLiteral("fan"));
        QCOMPARE(document.redo().outcome, Document::StepOutcome::Applied);
        QCOMPARE(document.redo().outcome, Document::StepOutcome::Applied);
        QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
    }
    Document reopened;
    QVERIFY(reopened.open(file, false).ok);
    const MindNode *topic = reopened.model()->root()->children.front().get();
    QCOMPARE(topic->pitchLayout, QStringLiteral("fan"));
    QCOMPARE(topic->pitchDelivery, QStringLiteral("all"));
    QCOMPARE(reopened.model()->pitchTheme, QStringLiteral("light"));
    QCOMPARE(reopened.model()->pitchAspect, QStringLiteral("4:3"));
    // Defaults are not written.
    const MapHistory::Snapshot flat = reopened.model()->toFlatSnapshot();
    for (auto it = flat.cbegin(); it != flat.cend(); ++it) QVERIFY(!it.value().contains(QStringLiteral("pitchTopicSlide")));
}

void PitchTest::unknownValuesReadAsDefault() {
    QCOMPARE(Pitch::validLayout(QStringLiteral("spiral")), QString());
    QCOMPARE(Pitch::validLayout(QStringLiteral("org")), QStringLiteral("org"));
    QCOMPARE(Pitch::validVisibility(QStringLiteral("maybe")), QString());
    QCOMPARE(Pitch::validTheme(QStringLiteral("dark")), QString());  // the default is stored as nothing
    QCOMPARE(Pitch::validAspect(QStringLiteral("9:16")), QStringLiteral("9:16"));
    QCOMPARE(Pitch::aspectRatio(Pitch::Aspect::Auto, 0), 16.0 / 9.0);
    QCOMPARE(Pitch::aspectRatio(Pitch::Aspect::Auto, 1.6), 1.6);
}

void PitchTest::listItemsAppearWhenShown() {
    MindMapModel model;
    for (const char *title : {"Uno", "Due", "Tre"}) model.addChild(model.root(), QString::fromLatin1(title));
    const Pitch::Look look = Pitch::look(model, Pitch::Theme::Dark);
    QCOMPARE(look.background, QColor(Qt::black));
    for (int layout = 0; layout < Pitch::kLayoutCount; ++layout) {
        Pitch::Slide slide = Pitch::previewSlide(model, model.root());
        slide.layout = static_cast<Pitch::Layout>(layout);
        QImage none(320, 180, QImage::Format_RGB32), all(320, 180, QImage::Format_RGB32);
        QPainter painter(&none);
        Pitch::paint(&painter, QRectF(none.rect()), slide, look, 0);
        painter.end();
        painter.begin(&all);
        Pitch::paint(&painter, QRectF(all.rect()), slide, look, -1);
        painter.end();
        QVERIFY2(inkPixels(all, look.background) > inkPixels(none, look.background), qPrintable(QString::number(layout)));
        QVERIFY(inkPixels(none, look.background) > 0);  // the title
    }
}

void PitchTest::presentationKeys() {
    MindMapModel model;
    MindNode *a = model.addChild(model.root(), QStringLiteral("A"));
    model.addChild(a, QStringLiteral("A1"));
    model.addChild(a, QStringLiteral("A2"));
    const QList<Pitch::Slide> slides = Pitch::slides(model);  // T:central, L:central[A], T:A, L:A[A1,A2]
    QCOMPARE(slides.size(), 4);
    PitchWindow window(slides, Pitch::look(model, Pitch::Theme::Dark), Pitch::Aspect::Wide, nullptr);
    window.resize(640, 360);
    QSignalSpy finished(&window, &PitchWindow::finished);
    QTest::keyClick(&window, Qt::Key_Right);     // the central topic's list, nothing shown yet
    QCOMPARE(window.slideIndex(), 1);
    QCOMPARE(window.shownItems(), 0);
    QTest::keyClick(&window, Qt::Key_Space);     // A appears
    QCOMPARE(window.shownItems(), 1);
    QTest::keyClick(&window, Qt::Key_Space);     // next slide
    QCOMPARE(window.slideIndex(), 2);
    QTest::keyClick(&window, Qt::Key_Left);      // back to the list, all shown
    QCOMPARE(window.slideIndex(), 1);
    QCOMPARE(window.shownItems(), 1);
    QTest::keyClick(&window, Qt::Key_End);
    QCOMPARE(window.slideIndex(), 3);
    QCOMPARE(window.shownItems(), 2);
    QTest::keyClick(&window, Qt::Key_Right);     // the last: stays
    QCOMPARE(window.slideIndex(), 3);
    QTest::keyClick(&window, Qt::Key_Home);
    QCOMPARE(window.slideIndex(), 0);
    QTest::keyClick(&window, Qt::Key_Escape);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.front().front().toString(), model.root()->id);
}

// BUG-032: a list delivered all at once shows every item as soon as it appears, and the next
// advance goes to the next slide.
void PitchTest::allAtOnceShowsEveryItem() {
    MindMapModel model;
    MindNode *a = model.addChild(model.root(), QStringLiteral("A"));
    model.addChild(a, QStringLiteral("A1"));
    model.addChild(a, QStringLiteral("A2"));
    a->pitchDelivery = Pitch::deliveryKey(Pitch::Delivery::All);
    const QList<Pitch::Slide> slides = Pitch::slides(model);  // T:central, L:central[A], T:A, L:A[A1,A2]
    QCOMPARE(slides.size(), 4);
    QCOMPARE(slides[3].delivery, Pitch::Delivery::All);
    const Pitch::Look look = Pitch::look(model, Pitch::Theme::Dark);
    PitchWindow window(slides, look, Pitch::Aspect::Wide, nullptr);
    window.resize(640, 360);
    window.goTo(3);
    const QImage shot = window.grab().toImage().convertToFormat(QImage::Format_RGB32);
    QImage all(shot.size(), QImage::Format_RGB32), none(shot.size(), QImage::Format_RGB32);
    QPainter painter(&all);
    Pitch::paint(&painter, QRectF(all.rect()), slides[3], look, -1);
    painter.end();
    painter.begin(&none);
    Pitch::paint(&painter, QRectF(none.rect()), slides[3], look, 0);
    painter.end();
    const int allInk = inkPixels(all, look.background), noneInk = inkPixels(none, look.background);
    QVERIFY(allInk > noneInk);
    // Not equal: on Linux the window's text has subpixel (coloured) edges, the image's grey ones.
    const int shotInk = inkPixels(shot, look.background);
    QVERIFY2(qAbs(shotInk - allInk) < (allInk - noneInk) / 2, qPrintable(QStringLiteral("%1 %2 %3").arg(shotInk).arg(allInk).arg(noneInk)));
    window.next();
    QCOMPARE(window.slideIndex(), 3);  // the last slide: nothing more to show, it stays
    window.goTo(1);
    window.next();
    QCOMPARE(window.shownItems(), 1);  // the central topic's list still goes one at a time
}

// Present is first in the top-right group, F5 is its shortcut; the Pitch tab changes the selected
// topic's layout as a history step.
void PitchTest::tabAndPresentButton() {
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    qobject_cast<QStackedWidget *>(m_window->centralWidget())->setCurrentIndex(1);  // editor
    auto *present = m_window->findChild<QToolButton *>(QStringLiteral("PresentButton"));
    auto *icons = m_window->findChild<QToolButton *>(QStringLiteral("IconPanelToggle"));
    QVERIFY(present && icons);
    QCOMPARE(present->parentWidget(), icons->parentWidget());
    QVERIFY(present->x() < icons->x());
    QAction *presentAction = nullptr;
    for (auto *action : m_window->findChildren<QAction *>())
        if (action->text() == QStringLiteral("Present")) presentAction = action;
    QVERIFY(presentAction);
    QCOMPARE(presentAction->shortcut(), QKeySequence(Qt::Key_F5));
    // Triggered twice (F5 auto-repeat): one window only, so one Esc ends it (user's report 2026-10-26).
    const auto presentations = [] {
        int count = 0;
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (qobject_cast<PitchWindow *>(widget) && widget->isVisible()) ++count;
        return count;
    };
    presentAction->trigger();
    presentAction->trigger();
    QApplication::processEvents();
    QCOMPARE(presentations(), 1);
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (auto *window = qobject_cast<PitchWindow *>(widget); window && window->isVisible()) QTest::keyClick(window, Qt::Key_Escape);
    QApplication::processEvents();
    QCOMPARE(presentations(), 0);

    auto *model = m_window->findChild<MindMapModel *>();
    auto *scene = m_window->findChild<MindMapScene *>();
    QVERIFY(model && scene);
    MindNode *topic = model->addChild(model->root(), QStringLiteral("Tema"));
    QApplication::processEvents();
    scene->selectNode(topic);
    QApplication::processEvents();
    QVERIFY(m_window->findChild<QToolButton *>(QStringLiteral("PitchLayout9")));  // two rows of five
    auto *fan = m_window->findChild<QToolButton *>(QStringLiteral("PitchLayout3"));
    QVERIFY(fan);
    QVERIFY(fan->isEnabled());
    fan->click();
    QCOMPARE(topic->pitchLayout, QStringLiteral("fan"));
    QVERIFY(fan->isChecked());
    auto *listSlide = m_window->findChild<QComboBox *>(QStringLiteral("PitchListSlide"));
    QVERIFY(listSlide);
    QVERIFY(listSlide->itemText(0).startsWith(QStringLiteral("Auto (")));
    // The preview is at most 16:9 and shrinks again when the panel narrows (it used to only grow).
    PitchPreview preview;
    preview.showSlide(Pitch::previewSlide(*model, topic), Pitch::look(*model, Pitch::Theme::Dark), 3.0 / 4.0);  // a tall slide
    preview.show();
    preview.resize(480, preview.height());
    QApplication::processEvents();
    QCOMPARE(preview.height(), 270);
    preview.resize(240, preview.height());
    QApplication::processEvents();
    QCOMPARE(preview.height(), 135);
    // Nothing selected: the rows work on the central topic (user's request 2026-10-26).
    scene->clearSelection();
    QApplication::processEvents();
    QVERIFY(fan->isEnabled());
    auto *org = m_window->findChild<QToolButton *>(QStringLiteral("PitchLayout4"));
    QVERIFY(org);
    org->click();
    QCOMPARE(model->root()->pitchLayout, QStringLiteral("org"));
    QCOMPARE(topic->pitchLayout, QStringLiteral("fan"));
    // Several selected: the rows are off.
    MindNode *other = model->addChild(model->root(), QStringLiteral("Altro"));
    QApplication::processEvents();
    QCOMPARE(scene->selectTargets(SelectionCommands::Targets{{topic, other}, {}, {}}), 2);
    QApplication::processEvents();
    QVERIFY(!fan->isEnabled());
    // Cambia tema (stage B): the basic and the illustrated themes, then the templates (FEAT-089); a card sets the theme.
    auto *themeButton = m_window->findChild<QPushButton *>(QStringLiteral("PitchTheme"));
    QVERIFY(themeButton);
    int cards = 0;
    int groups = 0;
    QTimer::singleShot(0, this, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) return;
        groups = int(menu->findChildren<QLabel *>(QStringLiteral("PitchThemeGroup")).size());
        for (auto *card : menu->findChildren<QToolButton *>())
            cards += card->objectName().startsWith(QStringLiteral("PitchThemeCard"));
        if (auto *launch = menu->findChild<QToolButton *>(QStringLiteral("PitchThemeCard%1").arg(int(Pitch::Theme::Launch)))) launch->click();
        else menu->close();
    });
    themeButton->click();
    QCOMPARE(groups, 3);
    QCOMPARE(cards, Pitch::kThemeCount);
    QCOMPARE(model->pitchTheme, QStringLiteral("launch"));
    // Stage C (user's request 2026-10-27): no Export button in the Pitch tab; "Export > Pitch…" saves
    // the .pptx at once, without a dialog.
    QVERIFY(!m_window->findChild<QPushButton *>(QStringLiteral("PitchExport")));
    QAction *exportPitch = nullptr;
    for (auto *action : m_window->findChildren<QAction *>())
        if (action->text() == QStringLiteral("Export Pitch…")) exportPitch = action;
    QVERIFY(exportPitch);
    const QString deck = m_dir.filePath(QStringLiteral("from-menu"));
    m_window->setExportFilePicker([deck] { return deck; });
    exportPitch->trigger();
    QVERIFY(QFile::exists(deck + QStringLiteral(".pptx")));
    m_window->setExportFilePicker({});
}

// Stage C: "Export > PDF…" offers Pitch beside Mind map and Outliner (PDF only); its pages are the
// slides, and the page rows are off.
void PitchTest::pdfDialogOffersPitch() {
    MindMapModel model;
    for (const char *title : {"A", "B"}) model.addChild(model.root(), QString::fromLatin1(title));
    const PageDialog::PitchSource pitch{Pitch::slides(model), Pitch::look(model, Pitch::Theme::Halftone), 16.0 / 9.0};
    const auto noPages = [](PageDialog::Scope) { return QList<MapExporter::Page>(); };
    PageDialog print(nullptr, PageDialog::Mode::Print, QString(), false, noPages, {}, pitch);
    QCOMPARE(print.findChild<SegmentedControl *>(QStringLiteral("PageKind"))->findChildren<QToolButton *>().size(), 2);
    PageDialog dialog(nullptr, PageDialog::Mode::Pdf, QStringLiteral("Deck"), false, noPages, {}, pitch);
    auto *kind = dialog.findChild<SegmentedControl *>(QStringLiteral("PageKind"));
    QVERIFY(kind);
    const QList<QToolButton *> segments = kind->findChildren<QToolButton *>();
    QCOMPARE(segments.size(), 3);
    QCOMPARE(segments[2]->text(), QStringLiteral("Pitch"));
    segments[2]->click();
    QCOMPARE(kind->currentIndex(), 2);
    QVERIFY(!dialog.findChild<QWidget *>(QStringLiteral("PageSize"))->isEnabled());
    QVERIFY(dialog.acceptButton()->isEnabled());
    QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("PageNumber"))->text(), QStringLiteral("1/%1").arg(pitch.slides.size()));
    const QString pdf = m_dir.filePath(QStringLiteral("pitch-from-dialog.pdf"));
    dialog.pickPath = [pdf](const QString &) { return pdf; };
    dialog.acceptButton()->click();
    QVERIFY(QFile::exists(pdf));
    QVERIFY(dialog.outcome().contains(QStringLiteral("pitch-from-dialog.pdf")));
}

namespace {
// A plain image of colour, saved as PNG in dir.
QString savedImage(const QTemporaryDir &dir, const QString &name, const QColor &colour) {
    QImage image(40, 20, QImage::Format_RGB32);
    image.fill(colour);
    const QString path = dir.filePath(name);
    return image.save(path) ? path : QString();
}

Pitch::Extras figureExtras(qreal lines, Pitch::Extras::Side side) {
    Pitch::Extras extras;
    extras.figure = QImage(40, 20, QImage::Format_RGB32);
    extras.figure.fill(Qt::red);
    extras.nodeFigureSize = QSizeF(40, 20);
    extras.figureLines = lines;
    extras.side = side;
    return extras;
}
}  // namespace

// Stage A2: the figure (image or formula) and the markers come from the node; a missing file shows none.
void PitchTest::figuresFollowTheNode() {
    MindMapModel model;
    MindNode *topic = model.addChild(model.root(), QStringLiteral("A"));
    topic->imagePath = savedImage(m_dir, QStringLiteral("figure.png"), Qt::red);
    QVERIFY(!topic->imagePath.isEmpty());
    topic->imageSize = QSizeF(160, 80);
    topic->imagePlacement = MindNode::ImagePlacement::Left;
    topic->markers = {QStringLiteral("priority:1")};
    const Pitch::Extras extras = Pitch::extrasOf(topic);
    QVERIFY(extras.hasFigure());
    QCOMPARE(extras.side, Pitch::Extras::Side::Left);
    QCOMPARE(extras.figureLines, 80 / QFontMetricsF(NodeItem::titleFont(topic)).height());
    QCOMPARE(extras.markers, topic->markers);
    topic->imagePath = m_dir.filePath(QStringLiteral("missing.png"));
    QVERIFY(!Pitch::extrasOf(topic).hasFigure());
    topic->imagePath.clear();
    topic->formula = QStringLiteral("x^2");
    QVERIFY(Pitch::extrasOf(topic).hasFigure());
    // The list slide of the central carries the item's figure and markers.
    const Pitch::Slide list = Pitch::previewSlide(model, nullptr);
    QCOMPARE(list.itemExtras.size(), 1);
    QVERIFY(list.itemExtras.front().hasFigure());
    QCOMPARE(list.itemExtras.front().markers, topic->markers);
}

// As tall as so many lines of the text, up to the cap; beside the text only when the text keeps room.
void PitchTest::figureKeepsTheProportion() {
    QFont font;
    font.setPixelSize(40);
    const qreal line = QFontMetricsF(font).height();
    const QRectF box(0, 0, 2000, 2000);
    const int align = Qt::AlignLeft | Qt::AlignVCenter;
    Pitch::EntryLayout entry = Pitch::layoutEntry(box, align, font, QStringLiteral("Text"), figureExtras(4, Pitch::Extras::Side::Above), box.size());
    QCOMPARE(entry.figure.height(), 4 * line);
    QCOMPARE(entry.figure.width(), 8 * line);  // the node's proportions
    QVERIFY(entry.figure.bottom() <= entry.text.top());
    entry = Pitch::layoutEntry(box, align, font, QStringLiteral("Text"), figureExtras(100, Pitch::Extras::Side::Above), QSizeF(2000, 500));
    QCOMPARE(entry.figure.height(), 500.0);
    // Left: the figure before the text; in a narrow box it goes above.
    entry = Pitch::layoutEntry(box, align, font, QStringLiteral("Text"), figureExtras(2, Pitch::Extras::Side::Left), box.size());
    QVERIFY(entry.figure.right() <= entry.text.left());
    const QRectF narrow(0, 0, 4 * line, 2000);
    entry = Pitch::layoutEntry(narrow, align, font, QStringLiteral("Some text"), figureExtras(2, Pitch::Extras::Side::Left), narrow.size());
    QVERIFY(entry.figure.bottom() <= entry.text.top());
    QVERIFY(narrow.contains(entry.figure));
    // A box too low for the figure shrinks it; the markers are as tall as the text, before it.
    Pitch::Extras marked = figureExtras(4, Pitch::Extras::Side::Below);
    marked.markers = {QStringLiteral("priority:1"), QStringLiteral("flag:red")};
    const QRectF low(0, 0, 2000, 3 * line);
    entry = Pitch::layoutEntry(low, align, font, QStringLiteral("Text"), marked, low.size());
    QVERIFY(low.contains(entry.block));
    QVERIFY(entry.figure.height() < 4 * line);
    QCOMPARE(entry.markers.size(), 2);
    QCOMPARE(entry.markers[0].height(), 40.0);
    QVERIFY(entry.markers[1].right() <= entry.text.left());
}

void PitchTest::listsWithFiguresHoldFewer() {
    MindMapModel model;
    for (int i = 0; i < 12; ++i) model.addChild(model.root(), QStringLiteral("T%1").arg(i));
    model.root()->children[3]->formula = QStringLiteral("x");
    QList<Pitch::Slide> lists;
    for (const Pitch::Slide &slide : Pitch::slides(model))
        if (slide.kind == Pitch::Slide::Kind::List) lists.append(slide);
    QCOMPARE(lists.size(), 2);
    QCOMPARE(lists[0].items.size(), Pitch::kMaxListItemsWithFigures);
    QCOMPARE(lists[0].itemExtras.size(), lists[0].items.size());
    QCOMPARE(Pitch::maxItems(Pitch::Layout::Grid, true), Pitch::kMaxGridItemsWithFigures);
    QCOMPARE(Pitch::maxItems(Pitch::Layout::Org, true), Pitch::kMaxRowItems);
}

// Every layout draws the figures inside the slide, the title's too.
void PitchTest::slidesDrawTheFigure() {
    Pitch::Look look;
    look.background = Qt::black;
    look.text = Qt::white;
    look.line = Qt::gray;
    for (int index = 0; index < Pitch::kLayoutCount; ++index) {
        Pitch::Slide slide;
        slide.kind = Pitch::Slide::Kind::List;
        slide.layout = Pitch::Layout(index);
        slide.title = QStringLiteral("Title");
        slide.titleExtras = figureExtras(3, Pitch::Extras::Side::Right);
        for (int i = 0; i < 6; ++i) {
            slide.items.append(QStringLiteral("Item %1").arg(i));
            slide.itemExtras.append(figureExtras(i % 2 ? 6 : 2, Pitch::Extras::Side(i % 4)));
        }
        QImage image(640, 360, QImage::Format_RGB32);
        image.fill(Qt::black);
        QPainter painter(&image);
        Pitch::paint(&painter, QRectF(0, 0, 640, 360), slide, look);
        painter.end();
        QVERIFY2(inkPixels(image, Qt::black) > 0, qPrintable(Pitch::layoutKey(slide.layout)));
        int red = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (const QColor c = image.pixelColor(x, y); c.red() > 200 && c.green() < 60 && c.blue() < 60) ++red;
        QVERIFY2(red > 200, qPrintable(Pitch::layoutKey(slide.layout)));
    }
    Pitch::Slide topic;
    topic.title = QStringLiteral("Cover");
    topic.cover = true;
    topic.titleExtras = figureExtras(50, Pitch::Extras::Side::Above);
    QImage image(640, 360, QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    Pitch::paint(&painter, QRectF(0, 0, 640, 360), topic, look);
    painter.end();
    // Capped at half the height: the red stays within 180 rows.
    int top = image.height(), bottom = -1;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y) == QColor(Qt::red)) { top = std::min(top, y); bottom = std::max(bottom, y); }
    QVERIFY(bottom >= top);
    QVERIFY(bottom - top + 1 <= 181);
}

namespace {
// Relative luminance and contrast ratio of WCAG 2.1 (https://www.w3.org/TR/WCAG21/#dfn-contrast-ratio).
qreal luminance(const QColor &colour) {
    const auto linear = [](qreal c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(colour.redF()) + 0.7152 * linear(colour.greenF()) + 0.0722 * linear(colour.blueF());
}
qreal contrast(const QColor &a, const QColor &b) {
    const qreal la = luminance(a);
    const qreal lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

Pitch::Slide sampleSlide(Pitch::Layout layout) {
    Pitch::Slide slide;
    slide.kind = Pitch::Slide::Kind::List;
    slide.layout = layout;
    slide.title = QStringLiteral("Market opportunity");
    for (const char *item : {"Target market", "Market size", "Trend", "Partners", "Pricing", "Channels"})
        slide.items.append(QString::fromLatin1(item));
    return slide;
}

QImage rendered(const Pitch::Slide &slide, const Pitch::Look &look, const QSize &size) {
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::magenta);
    QPainter painter(&image);
    Pitch::paint(&painter, QRectF(QPointF(0, 0), size), slide, look);
    return image;
}

int distance(QRgb a, QRgb b) {
    return std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b));
}
}  // namespace

void PitchTest::themesAreSavedByKey() {
    QStringList keys;
    for (int index = 0; index < Pitch::kThemeCount; ++index) {
        const auto theme = Pitch::Theme(index);
        QCOMPARE(Pitch::themeFromKey(Pitch::themeKey(theme)), theme);
        QCOMPARE(Pitch::validTheme(Pitch::themeKey(theme)), Pitch::themeKey(theme));
        keys.append(Pitch::themeKey(theme));
    }
    QCOMPARE(keys.mid(Pitch::kBasicThemeCount),
             QStringList({QStringLiteral("proposal"), QStringLiteral("playful"), QStringLiteral("launch"), QStringLiteral("manifesto"),
                          QStringLiteral("halftone"), QStringLiteral("paper"), QStringLiteral("night"), QStringLiteral("soft"),
                          QStringLiteral("portfolio"), QStringLiteral("editorial")}));
    QCOMPARE(Pitch::themeFromKey(QStringLiteral("unknown")), Pitch::Theme::Dark);
}

// Text and titles at least 4.5:1 on every ground of every illustrated theme (WCAG 2.1, 1.4.3): the
// list, the cover and the topic slides, the badges, the Clouds frame under the titles, and the
// darkest point of the Launch smudge.
void PitchTest::themesKeepTheContrast() {
    MindMapModel model;
    for (int index = Pitch::kBasicThemeCount; index < Pitch::kThemeCount; ++index) {
        const Pitch::Look look = Pitch::look(model, Pitch::Theme(index));
        const QString name = Pitch::themeKey(Pitch::Theme(index));
        Pitch::Slide list = sampleSlide(Pitch::Layout::Bullets);
        Pitch::Slide cover;
        cover.cover = true;
        Pitch::Slide topic;
        for (const Pitch::Slide &slide : {list, cover, topic}) {
            const Pitch::Look on = Pitch::lookOn(look, slide);
            QColor ground = on.motif == Pitch::Motif::Clouds && slide.kind == Pitch::Slide::Kind::Topic ? on.fill : on.background;
            QVERIFY2(contrast(on.title, ground) >= 4.5, qPrintable(name));
            if (slide.kind == Pitch::Slide::Kind::List) {
                QVERIFY2(contrast(on.text, ground) >= 4.5, qPrintable(name));
                if (on.itemStyle == Pitch::ItemStyle::Badge) QVERIFY2(contrast(on.text, on.fill) >= 4.5, qPrintable(name));
                if (on.itemStyle == Pitch::ItemStyle::Label)
                    for (int item = 0; item < 3; ++item)
                        QVERIFY2(contrast(Pitch::labelText(on, item), Pitch::labelFill(on, item)) >= 4.5, qPrintable(name));
            }
            if (on.motif == Pitch::Motif::Grain)  // the darkest speck of the grain: 17 of 255 black
                QVERIFY2(contrast(on.text, ground.darker(100 * 255 / (255 - 17))) >= 4.5, qPrintable(name));
            if (on.motif == Pitch::Motif::Smudge) {  // all the layers of the stroke over the ground
                const bool dark = ground.lightnessF() < 0.5;
                const QColor ink = dark ? Qt::white : Qt::black;
                const qreal alpha = 1 - std::pow(1 - (dark ? Pitch::kSmudgeAlphaDark : Pitch::kSmudgeAlphaLight), Pitch::kSmudgeLayers);
                const QColor darkest = QColor::fromRgbF(ground.redF() + (ink.redF() - ground.redF()) * alpha,
                                                        ground.greenF() + (ink.greenF() - ground.greenF()) * alpha,
                                                        ground.blueF() + (ink.blueF() - ground.blueF()) * alpha);
                QVERIFY2(contrast(on.text, darkest) >= 4.5, qPrintable(name));
            }
        }
    }
}

// The motif stays off the text and the lines: drawn with and without it, a slide changes only away
// from the content, in every layout, in a wide and a tall ratio. The Clouds frame under a topic
// title and the translucent Launch smudge are meant to lie under the text (contrast above).
void PitchTest::motifsStayOffTheText() {
    MindMapModel model;
    NodeItem::loadBundledFonts();
    for (int index = Pitch::kBasicThemeCount; index < Pitch::kThemeCount; ++index) {
        const Pitch::Look look = Pitch::look(model, Pitch::Theme(index));
        if (look.motif == Pitch::Motif::Smudge) continue;
        Pitch::Look bare = look;
        bare.motif = Pitch::Motif::None;
        QList<Pitch::Slide> slides;
        for (int layout = 0; layout < Pitch::kLayoutCount; ++layout) slides.append(sampleSlide(Pitch::Layout(layout)));
        if (look.motif != Pitch::Motif::Clouds) {
            Pitch::Slide cover;
            cover.title = QStringLiteral("Pitch deck");
            cover.cover = true;
            slides.append(cover);
        }
        for (const QSize &size : {QSize(480, 270), QSize(270, 480)}) {
            for (const Pitch::Slide &slide : std::as_const(slides)) {
                const QImage with = rendered(slide, look, size);
                const QImage without = rendered(slide, bare, size);
                const QRgb ground = Pitch::lookOn(look, slide).background.rgb();
                int content = 0;
                int touched = 0;
                for (int y = 0; y < size.height(); ++y)
                    for (int x = 0; x < size.width(); ++x) {
                        if (distance(without.pixel(x, y), ground) < 60) continue;
                        ++content;
                        bool near = false;
                        for (int dy = -2; dy <= 2 && !near; ++dy)
                            for (int dx = -2; dx <= 2 && !near; ++dx) {
                                const QPoint at(x + dx, y + dy);
                                near = with.rect().contains(at) && distance(with.pixel(at), without.pixel(at)) >= 60;
                            }
                        touched += near;
                    }
                const QString where = QStringLiteral("%1 %2 %3x%4: %5 of %6")
                                          .arg(Pitch::themeKey(Pitch::Theme(index)), slide.cover ? QStringLiteral("cover") : Pitch::layoutKey(slide.layout))
                                          .arg(size.width()).arg(size.height()).arg(touched).arg(content);
                QVERIFY2(content > 0 && touched <= content / 100, qPrintable(where));
            }
        }
    }
}

using OoxmlTest::unzip;

// Stage C: the .pptx package, its slides, animations and notes; the PDF's pages.
// BUG-034: a long one-word title on a list slide of the default layout shrinks until the word fits
// in the room the title's markers leave it, in every theme, instead of breaking inside the word.
void PitchTest::longTitleWordShrinks() {
    MindMapModel model;
    model.root()->title = QStringLiteral("PHOTOSYNTHESIS");
    for (const char *title : {"Light", "Water", "Carbon dioxide"}) model.addChild(model.root(), QString::fromLatin1(title));
    Pitch::Slide slide = Pitch::previewSlide(model, model.root());
    QCOMPARE(slide.kind, Pitch::Slide::Kind::List);
    QCOMPARE(slide.layout, Pitch::Layout::Bracket);
    for (const QStringList &markers : {QStringList(), QStringList{QStringLiteral("priority:1"), QStringLiteral("priority:2")}}) {
        slide.titleExtras.markers = markers;
        for (int index = 0; index < Pitch::kThemeCount; ++index) {
            const Pitch::Look look = Pitch::look(model, Pitch::Theme(index));
            for (const QSize size : {QSize(1280, 720), QSize(960, 720)}) {
                QImage image(size, QImage::Format_RGB32);
                QPainter painter(&image);
                QList<Pitch::TextRun> texts;
                Pitch::paint(&painter, QRectF(image.rect()), slide, look, -1, 1, &texts);
                painter.end();
                const auto title = std::find_if(texts.cbegin(), texts.cend(), [](const Pitch::TextRun &run) { return run.item < 0; });
                QVERIFY(title != texts.cend());
                const QString name = QStringLiteral("%1 %2 markers %3").arg(Pitch::themeKey(Pitch::Theme(index))).arg(size.width()).arg(markers.size());
                QVERIFY2(!(title->flags & Qt::TextWrapAnywhere), qPrintable(name));
                QVERIFY2(QFontMetricsF(title->font).horizontalAdvance(title->text) <= title->rect.width() + 1, qPrintable(name));
            }
        }
    }
}

void PitchTest::exportWritesPptxAndPdf() {
    QCOMPARE(ZipWriter::crc32("123456789"), 0xCBF43926u);  // the CRC-32 check value
    MindMapModel model;
    model.root()->title = QStringLiteral("Plan & <goals>");
    model.root()->notes = QStringLiteral("First line\nSecond & last");
    for (const char *title : {"A", "B", "C"}) model.addChild(model.root(), QString::fromLatin1(title));
    model.root()->children[0]->formula = QStringLiteral("x^2");
    model.root()->children[1]->notes = QStringLiteral("B's notes");
    model.root()->pitchLayout = Pitch::layoutKey(Pitch::Layout::Bullets);
    const Pitch::Look look = Pitch::look(model, Pitch::Theme::Launch);
    QList<Pitch::Slide> slides = Pitch::slides(model);  // cover, list of the centre, topic A, B, C
    QCOMPARE(slides.size(), 5);
    QCOMPARE(slides[0].notes, model.root()->notes);
    QVERIFY(slides[1].notes.isEmpty());  // the notes go on the topic's first slide only
    const QMap<QString, QByteArray> parts = unzip(PitchExport::pptx(slides, look, 16.0 / 9.0, QStringLiteral("Deck & co")));
    QVERIFY(!parts.isEmpty());
    for (const char *name : {"[Content_Types].xml", "_rels/.rels", "ppt/presentation.xml", "ppt/slideMasters/slideMaster1.xml",
                             "ppt/slideLayouts/slideLayout1.xml", "ppt/theme/theme1.xml", "ppt/notesMasters/notesMaster1.xml", "docProps/core.xml"})
        QVERIFY2(parts.contains(QString::fromLatin1(name)), name);
    // Well formed, every part with its content type, every relationship to a part of the package.
    QCOMPARE(OoxmlTest::packageProblem(parts), QString());
    QCOMPARE(parts.keys().filter(QRegularExpression(QStringLiteral("^ppt/slides/slide\\d+\\.xml$"))).size(), 5);
    QCOMPARE(parts.keys().filter(QRegularExpression(QStringLiteral("^ppt/notesSlides/notesSlide\\d+\\.xml$"))).size(), 2);
    QVERIFY(parts.value(QStringLiteral("ppt/notesSlides/notesSlide1.xml")).contains("Second &amp; last"));
    QVERIFY(parts.value(QStringLiteral("docProps/core.xml")).contains("Deck &amp; co"));
    QVERIFY(parts.value(QStringLiteral("ppt/presentation.xml")).contains("cx=\"12192000\" cy=\"6858000\""));
    // The list: a title and three items as text boxes, one click per item; the cover has no clicks.
    const QByteArray list = parts.value(QStringLiteral("ppt/slides/slide2.xml"));
    QCOMPARE(list.count("txBox=\"1\""), 4);
    QVERIFY(list.contains("Plan &amp; &lt;goals&gt;"));
    QCOMPARE(list.count("nodeType=\"clickEffect\""), 3);
    QVERIFY(!parts.value(QStringLiteral("ppt/slides/slide1.xml")).contains("<p:timing"));
    model.root()->pitchDelivery = Pitch::deliveryKey(Pitch::Delivery::All);
    const QMap<QString, QByteArray> all = unzip(PitchExport::pptx(Pitch::slides(model), look, 4.0 / 3.0, {}));
    QVERIFY(!all.value(QStringLiteral("ppt/slides/slide2.xml")).contains("<p:timing"));
    QCOMPARE(all.value(QStringLiteral("ppt/slides/slide2.xml")).count("txBox=\"1\""), 4);
    QVERIFY(all.value(QStringLiteral("ppt/presentation.xml")).contains("cx=\"9144000\" cy=\"6858000\""));

    // The PDF: a page per slide, of the slide's shape; a file that cannot be written gives an error.
    const QString pdf = m_dir.filePath(QStringLiteral("deck.pdf"));
    QCOMPARE(PitchExport::writePdf(pdf, slides, look, 16.0 / 9.0, QStringLiteral("Deck")), QString());
    QFile file(pdf);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    QVERIFY(bytes.startsWith("%PDF"));
    int pages = 0;
    for (auto match = QRegularExpression(QStringLiteral("/Type\\s*/Page[^s]")).globalMatch(QString::fromLatin1(bytes)); match.hasNext(); match.next())
        ++pages;
    QCOMPARE(pages, 5);
    QVERIFY(!PitchExport::writePptx(m_dir.filePath(QStringLiteral("missing/folder/deck.pptx")), slides, look, 16.0 / 9.0, {}).isEmpty());
    QVERIFY(!QFile::exists(m_dir.filePath(QStringLiteral("missing/folder/deck.pptx"))));
}

QTEST_MAIN(PitchTest)
#include "pitch_test.moc"
