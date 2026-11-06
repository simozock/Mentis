// Storico veloce su mappe grandi (2026-10-16): istantanea con cache per nodo
// (MindMapModel::toFlatSnapshot) e confronto senza copie delle chiavi (MapHistory::commit).
// Le prove confrontano ogni istantanea con una costruita da capo da toJson() (stessi campi del file
// .mentis, senza cache) e ogni stato annullato o ripristinato con quello registrato al momento della
// modifica, anche attraverso il giornale SQLite dello storico.
#include "maphistory.h"
#include "mindmapmodel.h"
#include "nodestyle.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTest>

#include <functional>

class HistorySnapshotTest final : public QObject {
    Q_OBJECT

private slots:
    void snapshotMatchesFullBuildAfterEveryEdit();
    void undoAndRedoRestoreTheRecordedStates();
    void fieldsThatAppearOrDisappearAreRecorded();
    void unchangedMapAddsNoStep();
    void removedSheetLeavesTheHistory();
    void undoneBranchListedBeforeItsChildren();

private:
    // Istantanea attesa, costruita senza cache: l'albero di toJson() reso piatto come lo storico.
    static MapHistory::Snapshot referenceSnapshot(const MindMapModel &model);
    // Modifica casuale di un tipo scelto da `random` (testo, posizione, stile, struttura, palette).
    static void randomEdit(MindMapModel &model, QRandomGenerator &random);
    static QList<MindNode *> allNodes(const MindMapModel &model);
    // Prima differenza fra due istantanee (chiave e campo), vuota se sono uguali.
    static QString firstDifference(const MapHistory::Snapshot &actual, const MapHistory::Snapshot &expected);
};

QString HistorySnapshotTest::firstDifference(const MapHistory::Snapshot &actual, const MapHistory::Snapshot &expected) {
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        if (!actual.contains(it.key())) return QStringLiteral("manca %1").arg(it.key());
        const QJsonObject a = actual.value(it.key()), e = it.value();
        if (a == e) continue;
        QStringList keys = e.keys() + a.keys();
        keys.removeDuplicates();
        for (const QString &field : std::as_const(keys))
            if (a.value(field) != e.value(field))
                return QStringLiteral("%1.%2: %3 invece di %4").arg(it.key(), field, a.value(field).toVariant().toString(), e.value(field).toVariant().toString());
    }
    for (auto it = actual.cbegin(); it != actual.cend(); ++it)
        if (!expected.contains(it.key())) return QStringLiteral("in piu' %1").arg(it.key());
    return {};
}

MapHistory::Snapshot HistorySnapshotTest::referenceSnapshot(const MindMapModel &model) {
    MapHistory::Snapshot snapshot;
    const QJsonObject json = model.toJson();
    std::function<void(QJsonObject, const QString &, int)> visit = [&](QJsonObject node, const QString &parentId, int row) {
        const QJsonArray children = node.take(QStringLiteral("children")).toArray();
        const QString id = node.value(QStringLiteral("id")).toString();
        node.insert(QStringLiteral("parent"), parentId);
        node.insert(QStringLiteral("row"), row);
        snapshot.insert(id, node);
        for (int i = 0; i < children.size(); ++i) visit(children.at(i).toObject(), id, i);
    };
    visit(json.value(QStringLiteral("root")).toObject(), QString(), 0);
    // "@map" non usa la cache: si prende dall'istantanea del modello.
    snapshot.insert(QStringLiteral("@map"), model.toFlatSnapshot().value(QStringLiteral("@map")));
    return snapshot;
}

QList<MindNode *> HistorySnapshotTest::allNodes(const MindMapModel &model) {
    QList<MindNode *> nodes;
    std::function<void(MindNode *)> visit = [&](MindNode *node) {
        nodes.append(node);
        for (const auto &child : node->children) visit(child.get());
    };
    visit(model.root());
    return nodes;
}

void HistorySnapshotTest::randomEdit(MindMapModel &model, QRandomGenerator &random) {
    const QList<MindNode *> nodes = allNodes(model);
    MindNode *node = nodes.at(random.bounded(nodes.size()));
    MindNode *other = nodes.at(random.bounded(nodes.size()));
    const int value = random.bounded(1000);
    switch (random.bounded(18)) {
    case 0: node->title = QStringLiteral("Titolo %1").arg(value); break;
    case 1: node->titleHtml = value % 2 ? QStringLiteral("<b>%1</b>").arg(value) : QString(); break;  // compare e sparisce
    case 2: node->notes = QStringLiteral("Note %1").arg(value); break;
    case 3: node->position = {value * 0.5, -value * 0.25}; break;
    case 4: node->manualOffset = {value % 3 ? 0.0 : value * 1.0, 0}; break;
    case 5: node->customWidth = value % 2 ? 0 : 100 + value; break;
    case 6: node->imagePath = value % 2 ? QStringLiteral("immagine-%1.png").arg(value) : QString(); node->imageSize = {180.0 + value, 120}; break;
    case 7: model.setStyle({node, other}, StyleProperty::FillColor, QColor::fromRgb(value, 255 - value % 256, 40)); break;
    case 8: model.setStyle({node}, StyleProperty::FontSize, 8 + value % 60); break;
    case 9: model.setStyle({node}, StyleProperty::Italic, value % 2 == 0); break;
    case 10: model.clearStyle({node, other}, value % 2 ? StyleProperty::FillColor : StyleProperty::FontSize); break;
    case 11: model.addChild(node, QStringLiteral("Nuovo %1").arg(value)); break;
    case 12: if (node->parent && nodes.size() > 4) model.removeNode(node); break;
    case 13: if (node->parent) model.indentNode(node); break;
    case 14: if (node->parent) model.outdentNode(node); break;
    case 16: model.setLinks({{node, value % 2 ? other->id : QString(), {}}}); break;  // FEAT-027
    case 17: model.setLinks({{node, {}, value % 2 ? QStringLiteral("https://example.com/%1").arg(value) : QString()}}); break;
    case 15: {
        const auto &palettes = NodeStyle::basePalettes();
        const NodeStyle::Palette &palette = palettes.at(value % palettes.size());
        model.setPalette(palette.id, palette.colors);
        break;
    }
    }
}

void HistorySnapshotTest::snapshotMatchesFullBuildAfterEveryEdit() {
    MindMapModel model;
    // Due livelli: un nodo ogni tre va sotto il primo ramo.
    for (int i = 0; i < 30; ++i) model.addChild(i % 3 == 2 ? model.root()->children.front().get() : model.root(), QStringLiteral("Nodo %1").arg(i));
    QRandomGenerator random(20261016);
    QCOMPARE(model.toFlatSnapshot(), referenceSnapshot(model));
    for (int i = 0; i < 600; ++i) {
        // Piu' modifiche fra due istantanee, come un passo di storico che ne raccoglie diverse.
        for (int edits = random.bounded(1, 4); edits > 0; --edits) randomEdit(model, random);
        QCOMPARE(model.toFlatSnapshot(), referenceSnapshot(model));
    }
    // Ricaricata la mappa, gli id restano gli stessi ma i nodi sono nuovi.
    QString error;
    QVERIFY2(model.fromJson(model.toJson(), &error), qPrintable(error));
    QCOMPARE(model.toFlatSnapshot(), referenceSnapshot(model));
}

void HistorySnapshotTest::undoAndRedoRestoreTheRecordedStates() {
    MindMapModel model;
    for (int i = 0; i < 20; ++i) model.addChild(model.root(), QStringLiteral("Nodo %1").arg(i));
    // Blocchi di 4 passi in RAM: annulla e ripristina passano molte volte dal giornale SQLite.
    MapHistory history(4);
    history.reset(model.toFlatSnapshot());
    QList<MapHistory::Snapshot> states{referenceSnapshot(model)};  // states[i] = stato dopo i passi
    QRandomGenerator random(1016);
    auto checkModel = [&](const MapHistory::Snapshot &expected) {
        QVERIFY2(history.current() == expected, qPrintable(firstDifference(history.current(), expected)));
        // Il modello portato allo stato dello storico (come MainWindow::restoreSnapshot).
        if (!model.applyFlatChanges(history.current(), history.lastChangedKeys())) {
            QString error;
            QVERIFY2(model.fromFlatSnapshot(history.current(), &error), qPrintable(error));
        }
        const MapHistory::Snapshot restored = referenceSnapshot(model);
        QVERIFY2(restored == expected, qPrintable(firstDifference(restored, expected)));
    };
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 40; ++i) {
            for (int edits = random.bounded(1, 4); edits > 0; --edits) randomEdit(model, random);
            if (history.commit(model.toFlatSnapshot())) states.append(referenceSnapshot(model));
            QCOMPARE(history.current(), states.last());
        }
        // Tutto indietro e tutto avanti, controllando ogni stato.
        const int steps = history.index();
        QCOMPARE(steps, static_cast<int>(states.size()) - 1);
        for (int i = steps - 1; i >= 0; --i) {
            QVERIFY(history.canUndo());
            history.undo();
            checkModel(states.at(i));
        }
        for (int i = 1; i <= steps; ++i) {
            QVERIFY(history.canRedo());
            history.redo();
            checkModel(states.at(i));
        }
        // Una modifica dopo qualche annulla cancella i passi successivi; le istantanee restano giuste.
        const int back = random.bounded(1, 6);
        for (int i = 1; i <= back; ++i) {
            history.undo();
            checkModel(states.at(steps - i));
        }
        states.resize(steps - back + 1);
        model.root()->title = QStringLiteral("Giro %1").arg(round);
        QVERIFY(history.commit(model.toFlatSnapshot()));
        states.append(referenceSnapshot(model));
        QVERIFY(!history.canRedo());
    }
}

void HistorySnapshotTest::fieldsThatAppearOrDisappearAreRecorded() {
    // B12: un campo che compare (titolo formattato, stile scelto a mano) o sparisce si annulla e si
    // ripristina; qui le chiavi dei due stati non coincidono e si usa il confronto per chiave.
    MindMapModel model;
    MindNode *node = model.addChild(model.root(), QStringLiteral("Nodo"));
    MapHistory history;
    history.reset(model.toFlatSnapshot());
    const MapHistory::Snapshot plain = referenceSnapshot(model);
    node->titleHtml = QStringLiteral("<i>Nodo</i>");
    QVERIFY(model.setStyle({node}, StyleProperty::FontSize, 20));
    QVERIFY(history.commit(model.toFlatSnapshot()));
    const MapHistory::Snapshot formatted = referenceSnapshot(model);
    QVERIFY(formatted.value(node->id).contains(QStringLiteral("titleHtml")));
    node->titleHtml.clear();
    QVERIFY(model.clearStyle({node}, StyleProperty::FontSize));
    node->position = {12, 34};
    QVERIFY(history.commit(model.toFlatSnapshot()));
    const MapHistory::Snapshot cleared = referenceSnapshot(model);
    QVERIFY(!cleared.value(node->id).contains(QStringLiteral("fontSize")));
    QCOMPARE(history.undo(), formatted);
    QCOMPARE(history.undo(), plain);
    QCOMPARE(history.redo(), formatted);
    QCOMPARE(history.redo(), cleared);
}

void HistorySnapshotTest::unchangedMapAddsNoStep() {
    MindMapModel model;
    for (int i = 0; i < 10; ++i) model.addChild(model.root(), QStringLiteral("Nodo %1").arg(i));
    MapHistory history;
    history.reset(model.toFlatSnapshot());
    QVERIFY(!history.commit(model.toFlatSnapshot()));
    // Un valore cambiato e poi rimesso com'era: nessun passo.
    MindNode *node = model.root()->children.front().get();
    const QString title = node->title;
    node->title = QStringLiteral("Altro");
    node->title = title;
    QVERIFY(!history.commit(model.toFlatSnapshot()));
    QCOMPARE(history.stepCount(), 0);
}

// FEAT-004: deleting a sheet drops its steps (also from the SQLite journal, 4 steps per block) and
// its part of a two-sheet step; the other sheet keeps Undo/Redo, saved state and new steps.
// Undoing a pasted branch removes it with its children; the changed keys can list the branch before
// them, and the children were then read after being freed (crash on Linux CI, 2026-11-03).
void HistorySnapshotTest::undoneBranchListedBeforeItsChildren() {
    MindMapModel model;
    model.addChild(model.root(), QStringLiteral("B"));
    const MapHistory::Snapshot before = model.toFlatSnapshot();
    MindNode *branch = model.addChild(model.root(), QStringLiteral("A"));
    const QStringList keys{branch->id, model.addChild(branch, QStringLiteral("A1"))->id,
                           model.addChild(branch, QStringLiteral("A2"))->id};
    QVERIFY(model.applyFlatChanges(before, keys));
    QCOMPARE(model.root()->children.size(), size_t(1));
    QCOMPARE(firstDifference(model.toFlatSnapshot(), before), QString());
}

void HistorySnapshotTest::removedSheetLeavesTheHistory() {
    const QString sheetA = QStringLiteral("A"), sheetB = QStringLiteral("B");
    MindMapModel a, b;
    MapHistory history(4);
    history.setSheet(sheetA, a.toFlatSnapshot());
    history.setSheet(sheetB, b.toFlatSnapshot());
    QList<MapHistory::Snapshot> statesA{a.toFlatSnapshot()};
    for (int i = 0; i < 15; ++i) {
        if (i % 3 != 1) {
            a.addChild(a.root(), QStringLiteral("A%1").arg(i));
            QVERIFY(history.commit(sheetA, a.toFlatSnapshot()));
            statesA.append(a.toFlatSnapshot());
        } else {
            b.addChild(b.root(), QStringLiteral("B%1").arg(i));
            QVERIFY(history.commit(sheetB, b.toFlatSnapshot()));
        }
        if (i == 6) history.markSaved();  // after 5 steps of A
    }
    a.root()->title = QStringLiteral("Linked");
    b.root()->title = QStringLiteral("Linked");
    QList<std::pair<QString, MapHistory::Snapshot>> both{{sheetB, b.toFlatSnapshot()}, {sheetA, a.toFlatSnapshot()}};
    QVERIFY(history.commit(both));  // the removed sheet is the step's own sheet
    statesA.append(a.toFlatSnapshot());
    history.undo();
    history.removeSheet(sheetB);
    QVERIFY(!history.hasSheet(sheetB));
    QCOMPARE(history.stepCount(), int(statesA.size()) - 1);
    QCOMPARE(history.index(), int(statesA.size()) - 2);
    QCOMPARE(history.redoSheet(), sheetA);
    history.redo();
    QCOMPARE(history.current(sheetA), statesA.last());
    for (int i = int(statesA.size()) - 2; i >= 0; --i) {
        QCOMPARE(history.undoSheet(), sheetA);
        history.undo();
        QCOMPARE(history.current(sheetA), statesA.at(i));
    }
    QVERIFY(!history.canUndo());
    for (int i = 0; i < 5; ++i) history.redo();
    QVERIFY(history.isAtSavedState());
    history.redo();
    QVERIFY(!history.isAtSavedState());
    QCOMPARE(history.current(sheetA), statesA.at(6));
    MindMapModel next;
    QVERIFY(next.fromFlatSnapshot(history.current(sheetA)));
    next.addChild(next.root(), QStringLiteral("After"));
    QVERIFY(history.commit(sheetA, next.toFlatSnapshot()));
    QVERIFY(!history.canRedo());
    history.undo();
    QCOMPARE(history.current(sheetA), statesA.at(6));
}

QTEST_MAIN(HistorySnapshotTest)
#include "history_snapshot_test.moc"
