// FEAT-025: Reset map style of the active sheet (what goes back to the default, what stays, one
// Undo/Redo step) and the button at the bottom of the Map page.
#include "document.h"
#include "mindmapmodel.h"
#include "performance.h"
#include "stylecontrols.h"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QToolButton>

class MapStyleResetTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void resetsTheLookAndKeepsTheRest();
    void panelButton();
};

void MapStyleResetTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisMapStyleResetTest"));
    Performance::setMode(Performance::Mode::Standard);
}

void MapStyleResetTest::resetsTheLookAndKeepsTheRest() {
    Document document;
    document.startUntitled();
    MindMapModel *model = document.model();
    MindNode *a = document.addChild(nullptr, QStringLiteral("A"));
    MindNode *a1 = document.addChild(a, QStringLiteral("A1"));
    MindNode *b = document.addChild(nullptr, QStringLiteral("B"));
    const QString relation = document.addRelation(a->id, b->id);
    const QStringList boundaries = document.addBoundaries({a1});
    document.flushPendingStep();
    QVERIFY(!model->hasMapStyle());
    QVERIFY(!document.resetMapStyle());
    // Chosen by hand: topic styles, a relationship, a boundary, the sheet look, background, a width,
    // a layout switch and the palette.
    a->style.fill = QColor(Qt::red);
    a1->style.fontSize = 20;
    BranchNumbering numbering;
    numbering.format = QStringLiteral("1.2.3");
    model->root()->style.numbering = numbering;
    model->relations.front().lineColor = QColor(Qt::blue);
    model->relations.front().label = QStringLiteral("kept");
    model->boundaries.front().fillColor = QColor(Qt::green);
    model->boundaries.front().title = QStringLiteral("kept too");
    model->sheetStyle.fontFamily = QStringLiteral("Arial");
    model->sheetStyle.branchWidth = 4;
    model->sheetStyle.coloredBranches = false;
    model->sheetStyle.compact = true;
    model->background = QColor(Qt::yellow);
    a->customWidth = 300;
    const QString palette = model->paletteId;
    document.markChanged();
    document.flushPendingStep();
    QVERIFY(model->hasMapStyle());

    QVERIFY(document.resetMapStyle());
    document.flushPendingStep();
    QVERIFY(!model->hasMapStyle());
    QCOMPARE(a->style, NodeStyleOverrides());
    QCOMPARE(a1->style, NodeStyleOverrides());
    QVERIFY(!model->root()->style.numbering);
    QVERIFY(a1->number.isEmpty());
    QVERIFY(!model->relations.front().lineColor);
    QCOMPARE(model->relations.front().label, QStringLiteral("kept"));
    QVERIFY(!model->boundaries.front().fillColor);
    QCOMPARE(model->boundaries.front().title, QStringLiteral("kept too"));
    QVERIFY(model->sheetStyle.fontFamily.isEmpty());
    QCOMPARE(model->sheetStyle.branchWidth, 0.0);
    QVERIFY(model->sheetStyle.coloredBranches);
    QVERIFY(!model->background);
    // Kept: layout switch, manual width, palette.
    QVERIFY(model->sheetStyle.compact);
    QCOMPARE(a->customWidth, 300.0);
    QCOMPARE(model->paletteId, palette);

    // One step brings everything back.
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QCOMPARE(a->style.fill, std::optional(QColor(Qt::red)));
    QVERIFY(model->root()->style.numbering);
    QCOMPARE(model->relations.front().lineColor, std::optional(QColor(Qt::blue)));
    QCOMPARE(model->boundaries.front().fillColor, std::optional(QColor(Qt::green)));
    QCOMPARE(model->sheetStyle.fontFamily, QStringLiteral("Arial"));
    QCOMPARE(model->background, std::optional(QColor(Qt::yellow)));
    QCOMPARE(model->relations.front().id, relation);
    QCOMPARE(model->boundaries.front().id, boundaries.front());
}

void MapStyleResetTest::panelButton() {
    SheetStylePanel panel;
    QSignalSpy requested(&panel, &SheetStylePanel::resetMapRequested);
    panel.setResetMapEnabled(false);
    QVERIFY(!panel.resetMapButton()->isEnabled());
    panel.setResetMapEnabled(true);
    panel.resetMapButton()->click();
    QCOMPARE(requested.size(), 1);
    QCOMPARE(panel.resetMapButton()->text(), QStringLiteral("Reset map style"));
}

QTEST_MAIN(MapStyleResetTest)
#include "map_style_reset_test.moc"
