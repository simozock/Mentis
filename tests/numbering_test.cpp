// FEAT-024: numbering of the branches (formats, levels, restart, floating topics), numbers in the model,
// the outliner text, Markdown and search, Undo/Redo, save and reopen, and the Style page section.
#include "bmapstorage.h"
#include "document.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "nodesearch.h"
#include "numbering.h"
#include "performance.h"
#include "stylecontrols.h"

#include "toggleswitch.h"

#include <QComboBox>
#include <QLineEdit>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class NumberingTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void symbols();
    void formatsAndLevels();
    void modelNumbersFollowTheTree();
    void shownInOutlinerMarkdownAndSearch();
    void undoAndSave();
    void trailingSeparator();
    void stylePageSection();
    void layoutMakesRoomForTheNumber();

private:
    QTemporaryDir m_dir;
};

namespace {

BranchNumbering numbering(const QString &format, QChar separator = u'.') {
    BranchNumbering result;
    result.format = format;
    result.separator = separator;
    return result;
}

QVariant value(const BranchNumbering &numbering) { return QVariant::fromValue(numbering); }

}  // namespace

void NumberingTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisNumberingTest"));
    Performance::setMode(Performance::Mode::Standard);
    QVERIFY(m_dir.isValid());
}

void NumberingTest::symbols() {
    using Numbering::Symbol;
    QCOMPARE(Numbering::symbolText(26, Symbol::UpperLetter), QStringLiteral("Z"));
    QCOMPARE(Numbering::symbolText(27, Symbol::UpperLetter), QStringLiteral("AA"));
    QCOMPARE(Numbering::symbolText(52, Symbol::LowerLetter), QStringLiteral("az"));
    QCOMPARE(Numbering::symbolText(703, Symbol::UpperLetter), QStringLiteral("AAA"));
    QCOMPARE(Numbering::symbolText(1994, Symbol::UpperRoman), QStringLiteral("MCMXCIV"));
    QCOMPARE(Numbering::symbolText(4, Symbol::LowerRoman), QStringLiteral("iv"));
    const auto parsed = Numbering::parse(QStringLiteral("I-A-1-a-i"), u'-');
    QVERIFY(parsed);
    QCOMPARE(*parsed, (QList<Symbol>{Symbol::UpperRoman, Symbol::UpperLetter, Symbol::Arabic, Symbol::LowerLetter, Symbol::LowerRoman}));
    QVERIFY(!Numbering::parse(QStringLiteral("1..2"), u'.'));
    QVERIFY(!Numbering::parse(QStringLiteral("1.#"), u'.'));
    QVERIFY(!Numbering::isValid(numbering(QStringLiteral("1.2"), u'/')));
}

void NumberingTest::formatsAndLevels() {
    // Preset: every level, no separator after the last symbol.
    QCOMPARE(Numbering::label(numbering(QStringLiteral("1.2.3")), {2, 1, 3, 1}), QStringLiteral("2.1.3.1"));
    QCOMPARE(Numbering::label(numbering(QStringLiteral("I)II)III"), u')'), {3, 2}), QStringLiteral("III)II"));
    // Custom: only the levels it writes.
    BranchNumbering custom = numbering(QStringLiteral("I.A"));
    QCOMPARE(Numbering::label(custom, {2, 3}), QStringLiteral("II.C"));
    QVERIFY(Numbering::label(custom, {2, 3, 1}).isEmpty());
    // By level and a limit of levels.
    BranchNumbering byLevel = numbering(QStringLiteral("1.2.3"));
    byLevel.hierarchical = false;
    QCOMPARE(Numbering::label(byLevel, {4, 2}), QStringLiteral("2"));
    byLevel.levels = 1;
    QVERIFY(Numbering::label(byLevel, {4, 2}).isEmpty());
    // File form.
    BranchNumbering full = numbering(QStringLiteral("a-1"), u'-');
    full.hierarchical = false;
    full.levels = 3;
    full.start = 5;
    QCOMPARE(Numbering::fromJson(Numbering::toJson(full)), std::optional(full));
    QCOMPARE(Numbering::toJson(numbering(QStringLiteral("1.2.3"))).toObject().keys(), QStringList{QStringLiteral("format")});
}

void NumberingTest::modelNumbersFollowTheTree() {
    Document document;
    document.startUntitled();
    MindMapModel *model = document.model();
    MindNode *root = model->root();
    MindNode *a = document.addChild(nullptr, QStringLiteral("A"));
    MindNode *a1 = document.addChild(a, QStringLiteral("A1"));
    MindNode *a2 = document.addChild(a, QStringLiteral("A2"));
    MindNode *b = document.addChild(nullptr, QStringLiteral("B"));
    MindNode *floating = document.addFloatingTopic({400, 400}, QStringLiteral("F"));
    MindNode *f1 = document.addChild(floating, QStringLiteral("F1"));
    document.flushPendingStep();
    BranchNumbering fromCentral = numbering(QStringLiteral("1.2.3"));
    fromCentral.start = 5;
    QVERIFY(model->setStyle({root}, StyleProperty::Numbering, value(fromCentral)));
    QCOMPARE(a->number, QStringLiteral("5"));
    QCOMPARE(a2->number, QStringLiteral("5.2"));
    QCOMPARE(b->number, QStringLiteral("6"));
    QVERIFY(root->number.isEmpty());
    QVERIFY(floating->number.isEmpty() && f1->number.isEmpty());  // floating topics apart
    // A descendant with its own numbering keeps its number and restarts it.
    QVERIFY(model->setStyle({a}, StyleProperty::Numbering, value(numbering(QStringLiteral("a.b.c")))));
    QCOMPARE(a->number, QStringLiteral("5"));
    QCOMPARE(a1->number, QStringLiteral("a"));
    QVERIFY(model->setStyle({a}, StyleProperty::Numbering, value(BranchNumbering())));  // None: inherited again
    QVERIFY(!a->style.numbering);
    QCOMPARE(a1->number, QStringLiteral("5.1"));
    // A floating topic numbers its own branch.
    QVERIFY(model->setStyle({floating}, StyleProperty::Numbering, value(numbering(QStringLiteral("A.B.C")))));
    QCOMPARE(f1->number, QStringLiteral("A"));
    // Inserting, moving and removing renumber.
    MindNode *a0 = document.addChild(a, QStringLiteral("A0"));
    QVERIFY(document.moveNode(a0, a, 0, 0));
    QCOMPARE(a0->number, QStringLiteral("5.1"));
    QCOMPARE(a2->number, QStringLiteral("5.3"));
    QVERIFY(model->removeNode(a0));
    QCOMPARE(a2->number, QStringLiteral("5.2"));
}

void NumberingTest::shownInOutlinerMarkdownAndSearch() {
    Document document;
    document.startUntitled();
    MindMapModel *model = document.model();
    MindNode *a = document.addChild(nullptr, QStringLiteral("Alpha"));
    MindNode *a1 = document.addChild(a, QStringLiteral("Beta"));
    document.flushPendingStep();
    QVERIFY(model->setStyle({model->root()}, StyleProperty::Numbering, value(numbering(QStringLiteral("1.2.3")))));
    const QModelIndex index = model->indexFromNode(a1);
    QCOMPARE(index.data(Qt::DisplayRole).toString(), QStringLiteral("1.1 Beta"));
    QCOMPARE(index.data(Qt::EditRole).toString(), QStringLiteral("Beta"));  // never in the title
    QCOMPARE(a1->title, QStringLiteral("Beta"));
    QVERIFY(model->toMarkdown().contains(QStringLiteral("- 1.1 Beta")));
    const QList<NodeSearch::Entry> entries = NodeSearch::entries(QStringLiteral("s"), model->toFlatSnapshot());
    const auto beta = std::find_if(entries.cbegin(), entries.cend(), [a1](const NodeSearch::Entry &entry) { return entry.nodeId == a1->id; });
    QVERIFY(beta != entries.cend());
    QCOMPARE(NodeSearch::shownTitle(*beta), QStringLiteral("1.1 Beta"));
    QVERIFY(NodeSearch::matchingFields(*beta, QStringLiteral("1.1"), {}) & NodeSearch::Title);
    // On the map the box makes room for the number.
    MindMapScene scene(model);
    const qreal numbered = scene.nodeWidth(a1);
    QVERIFY(model->setStyle({model->root()}, StyleProperty::Numbering, value(BranchNumbering())));
    QVERIFY(scene.nodeWidth(a1) < numbered);
}


void NumberingTest::undoAndSave() {
    const QString file = m_dir.filePath(QStringLiteral("numbers.mentis"));
    {
        Document document;
        document.startUntitled();
        MindNode *a = document.addChild(nullptr, QStringLiteral("A"));
        MindNode *a1 = document.addChild(a, QStringLiteral("A1"));
        document.flushPendingStep();
        BranchNumbering roman = numbering(QStringLiteral("I-A"), u'-');
        QVERIFY(document.model()->setStyle({a}, StyleProperty::Numbering, value(roman)));
        document.markChanged();  // what MainWindow does on styleEdited
        document.flushPendingStep();
        QCOMPARE(a1->number, QStringLiteral("I"));
        QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
        QVERIFY(a1->number.isEmpty());
        QCOMPARE(document.redo().outcome, Document::StepOutcome::Applied);
        QCOMPARE(a1->number, QStringLiteral("I"));
        QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
        document.close();
    }
    Document reopened;
    reopened.startUntitled();
    QVERIFY(reopened.open(file, false).ok);
    const MindNode *a = reopened.model()->root()->children.front().get();
    QCOMPARE(a->children.front()->number, QStringLiteral("I"));
    QCOMPARE(a->style.numbering->separator, QChar(u'-'));
    reopened.close();
}

// The separator also after the last symbol: "1.2.3." instead of "1.2.3" (off by default).
void NumberingTest::trailingSeparator() {
    BranchNumbering trailing = numbering(QStringLiteral("1.2.3"));
    trailing.trailingSeparator = true;
    QCOMPARE(Numbering::label(trailing, {1, 2, 3}), QStringLiteral("1.2.3."));
    trailing.hierarchical = false;
    QCOMPARE(Numbering::label(trailing, {1, 2}), QStringLiteral("2."));
    trailing.hierarchical = true;
    BranchNumbering custom = numbering(QStringLiteral("I)A"), u')');
    custom.trailingSeparator = true;
    QCOMPARE(Numbering::label(custom, {2, 3}), QStringLiteral("II)C)"));
    QVERIFY(Numbering::label(custom, {2, 3, 1}).isEmpty());  // a level not numbered gets no separator either
    QCOMPARE(Numbering::toJson(trailing).toObject().value(QStringLiteral("trailingSeparator")).toBool(), true);
    QCOMPARE(Numbering::fromJson(Numbering::toJson(trailing)), std::optional(trailing));
    QVERIFY(!Numbering::fromJson(Numbering::toJson(numbering(QStringLiteral("1.2.3"))))->trailingSeparator);

    const QString file = m_dir.filePath(QStringLiteral("trailing.mentis"));
    {
        Document document;
        document.startUntitled();
        MindMapModel *model = document.model();
        MindNode *a = document.addChild(nullptr, QStringLiteral("Alpha"));
        MindNode *a1 = document.addChild(a, QStringLiteral("Beta"));
        document.flushPendingStep();
        QVERIFY(model->setStyle({model->root()}, StyleProperty::Numbering, value(numbering(QStringLiteral("1.2.3")))));
        document.markChanged();
        document.flushPendingStep();
        QVERIFY(model->setStyle({model->root()}, StyleProperty::Numbering, value(trailing)));
        document.markChanged();
        document.flushPendingStep();
        QCOMPARE(a->number, QStringLiteral("1."));
        QCOMPARE(model->indexFromNode(a1).data(Qt::DisplayRole).toString(), QStringLiteral("1.1. Beta"));
        // In Markdown "1." after "- " would start an ordered list: the number stays text.
        const QString markdown = model->toMarkdown();
        QVERIFY2(markdown.contains(QStringLiteral("- 1\\. Alpha")), qPrintable(markdown));
        QVERIFY(markdown.contains(QStringLiteral("  - 1.1. Beta")));
        // One step: Undo brings back the numbers without it.
        QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
        QCOMPARE(a1->number, QStringLiteral("1.1"));
        QCOMPARE(document.redo().outcome, Document::StepOutcome::Applied);
        QCOMPARE(a1->number, QStringLiteral("1.1."));
        QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
        document.close();
    }
    Document reopened;
    reopened.startUntitled();
    QVERIFY(reopened.open(file, false).ok);
    QVERIFY(reopened.model()->root()->style.numbering->trailingSeparator);
    QCOMPARE(reopened.model()->root()->children.front()->number, QStringLiteral("1."));
    reopened.close();
}

void NumberingTest::stylePageSection() {
    Document document;
    document.startUntitled();
    MindNode *a = document.addChild(nullptr, QStringLiteral("A"));
    StylePage page;
    page.setNodes({a}, {});
    QList<std::pair<StyleProperty, QVariant>> chosen;
    connect(&page, &StylePage::styleChosen, this, [&chosen](StyleProperty property, const QVariant &v) { chosen.append({property, v}); });
    QVERIFY(!page.numberingFormatField()->isVisibleTo(&page));  // None: no fields
    // The A.B.C preset.
    emit page.numberingField()->activated(page.numberingField()->findText(QStringLiteral("A.B.C")));
    QCOMPARE(chosen.size(), 1);
    QCOMPARE(chosen.front().first, StyleProperty::Numbering);
    const BranchNumbering letters = chosen.front().second.value<BranchNumbering>();
    QCOMPARE(letters.format, QStringLiteral("A.B.C"));
    QVERIFY(document.model()->setStyle({a}, StyleProperty::Numbering, chosen.front().second));
    page.setNodes({a}, {});
    QVERIFY(page.numberingFormatField()->isVisibleTo(&page));
    QCOMPARE(page.numberingFormatField()->text(), QStringLiteral("A.B.C"));
    // Light text on the dark panel, like the other fields with a value of the topic.
    QVERIFY(page.numberingFormatField()->property("manual").toBool());
    QVERIFY(page.numberingStartField()->property("manual").toBool());
    // A separator rewrites the format; a format written by hand shows Custom.
    emit page.numberingSeparatorField()->activated(page.numberingSeparatorField()->findData(QStringLiteral("-")));
    QCOMPARE(chosen.back().second.value<BranchNumbering>().format, QStringLiteral("A-B-C"));
    // The switch under Separator: off by default, a click asks for the separator at the end.
    ToggleSwitch *trailing = page.numberingTrailingSeparatorField();
    QVERIFY(trailing->isVisibleTo(&page));
    QVERIFY(!trailing->isChecked());
    trailing->click();
    QVERIFY(chosen.back().second.value<BranchNumbering>().trailingSeparator);
    QCOMPARE(chosen.back().second.value<BranchNumbering>().format, QStringLiteral("A.B.C"));
    QVERIFY(document.model()->setStyle({a}, StyleProperty::Numbering, chosen.back().second));
    page.setNodes({a}, {});
    QVERIFY(trailing->isChecked());
    QVERIFY(document.model()->setStyle({a}, StyleProperty::Numbering, value(numbering(QStringLiteral("I.a")))));
    page.setNodes({a}, {});
    QCOMPARE(page.numberingField()->currentText(), QStringLiteral("Custom"));
    // None removes it.
    emit page.numberingField()->activated(page.numberingField()->findText(QStringLiteral("None")));
    QVERIFY(document.model()->setStyle({a}, StyleProperty::Numbering, chosen.back().second));
    QVERIFY(!a->style.numbering);
}

// Bug 2026-10-25 (user's photo): with numbers the children overlapped their parent.
void NumberingTest::layoutMakesRoomForTheNumber() {
    Document document;
    document.startUntitled();
    MindMapModel *model = document.model();
    MindNode *a = document.addChild(nullptr, QStringLiteral("Sotto-argomento 2"));
    MindNode *a1 = document.addChild(a, QStringLiteral("Sotto-argomento 1"));
    MindNode *a11 = document.addChild(a1, QStringLiteral("Sotto-argomento 1"));
    document.flushPendingStep();
    MindMapScene scene(model);
    scene.autoLayout();
    QVERIFY(model->setStyle({model->root()}, StyleProperty::Numbering, value(numbering(QStringLiteral("1.2.3")))));
    QCoreApplication::processEvents();
    const QRectF parent = scene.nodeShapeSceneRect(a), child = scene.nodeShapeSceneRect(a1), grandchild = scene.nodeShapeSceneRect(a11);
    QVERIFY2(child.left() > parent.right() + 10, qPrintable(QStringLiteral("parent %1 child %2").arg(parent.right()).arg(child.left())));
    QVERIFY2(grandchild.left() > child.right() + 10, qPrintable(QStringLiteral("child %1 grandchild %2").arg(child.right()).arg(grandchild.left())));
}

QTEST_MAIN(NumberingTest)
#include "numbering_test.moc"
