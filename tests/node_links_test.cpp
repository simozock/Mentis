// FEAT-027 (docs/features/FEAT-027-collegamenti-argomenti-web.md): regole dei collegamenti senza
// widget: indirizzi web, campi del nodo, salvataggio e passo di storico di piu' fogli.
#include "maphistory.h"
#include "mindmapmodel.h"
#include "nodelinks.h"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

class NodeLinksTest final : public QObject {
    Q_OBJECT

private slots:
    void webAddressesAreCompletedOrRejected_data();
    void webAddressesAreCompletedOrRejected();
    void linksAreSavedCopiedAndOptional();
    void setLinksIsAllOrNothingAndSignalsOnce();
    void oneStepCanChangeTwoSheets();
};

void NodeLinksTest::webAddressesAreCompletedOrRejected_data() {
    QTest::addColumn<QString>("typed");
    QTest::addColumn<QString>("address");
    QTest::newRow("senza schema") << QStringLiteral("example.com") << QStringLiteral("https://example.com");
    QTest::newRow("spazi") << QStringLiteral("  example.com/pagina  ") << QStringLiteral("https://example.com/pagina");
    QTest::newRow("porta senza schema") << QStringLiteral("localhost:3000") << QStringLiteral("https://localhost:3000");
    QTest::newRow("http") << QStringLiteral("http://example.com") << QStringLiteral("http://example.com");
    QTest::newRow("https maiuscolo") << QStringLiteral("HTTPS://Example.com") << QStringLiteral("https://example.com");
    QTest::newRow("mailto") << QStringLiteral("mailto:ada@example.com") << QStringLiteral("mailto:ada@example.com");
    QTest::newRow("vuoto") << QStringLiteral("   ") << QString();
    QTest::newRow("ftp") << QStringLiteral("ftp://example.com") << QString();
    QTest::newRow("javascript") << QStringLiteral("javascript:alert(1)") << QString();
    QTest::newRow("file") << QStringLiteral("file:///C:/segreto.txt") << QString();
    QTest::newRow("mailto vuoto") << QStringLiteral("mailto:") << QString();
    QTest::newRow("spazio nell'host") << QStringLiteral("exa mple.com") << QString();
}

void NodeLinksTest::webAddressesAreCompletedOrRejected() {
    QFETCH(QString, typed);
    QFETCH(QString, address);
    QCOMPARE(NodeLinks::webAddress(typed), address);
}

void NodeLinksTest::linksAreSavedCopiedAndOptional() {
    MindMapModel model;
    MindNode *source = model.addChild(model.root(), QStringLiteral("Spesa settimanale"));
    MindNode *target = model.addChild(model.root(), QStringLiteral("To-do List"));
    // Nessun collegamento: nessuna chiave nel file (documenti precedenti identici).
    QVERIFY(!model.toJson().value(QStringLiteral("root")).toObject().value(QStringLiteral("children")).toArray().at(0)
                 .toObject().contains(QStringLiteral("topicLink")));
    QVERIFY(model.setLinks({{source, target->id, QStringLiteral("https://example.com")}}));
    MindMapModel reopened;
    QString error;
    QVERIFY2(reopened.fromJson(model.toJson(), &error), qPrintable(error));
    const MindNode *copy = reopened.root()->children.front().get();
    QCOMPARE(copy->topicLink, target->id);
    QCOMPARE(copy->webLink, QStringLiteral("https://example.com"));
    QVERIFY2(reopened.fromFlatSnapshot(model.toFlatSnapshot(), &error), qPrintable(error));
    QCOMPARE(reopened.root()->children.front()->topicLink, target->id);
    // Copia/incolla e duplica: il collegamento viene copiato (id nuovi solo per i nodi copiati).
    MindNode *pasted = model.insertSubtree(model.root(), 2, model.subtreeToJson(source));
    QVERIFY(pasted && pasted->id != source->id);
    QCOMPARE(pasted->topicLink, target->id);
    QCOMPARE(pasted->webLink, QStringLiteral("https://example.com"));
}

void NodeLinksTest::setLinksIsAllOrNothingAndSignalsOnce() {
    MindMapModel model;
    MindNode *a = model.addChild(model.root(), QStringLiteral("A"));
    MindNode *b = model.addChild(model.root(), QStringLiteral("B"));
    QSignalSpy edited(&model, &MindMapModel::linksEdited);
    QVERIFY(!model.setLinks({{a, b->id, {}}, {nullptr, a->id, {}}}));  // un nodo nullo: niente
    QVERIFY(a->topicLink.isEmpty());
    QVERIFY(model.setLinks({{a, b->id, {}}, {b, a->id, {}}}));  // collegamento e inverso insieme
    QCOMPARE(edited.count(), 1);
    QCOMPARE(edited.at(0).at(0).value<QList<MindNode *>>().size(), 2);
    QVERIFY(!model.setLinks({{a, b->id, {}}}));  // nulla cambia
    QCOMPARE(edited.count(), 1);
    QVERIFY(model.setLinks({{a, QString(), {}}}));  // rimosso; il web resta invariato
    QVERIFY(a->topicLink.isEmpty());
    QCOMPARE(b->topicLink, a->id);
}

void NodeLinksTest::oneStepCanChangeTwoSheets() {
    // Collegamento inverso in un altro foglio: un solo passo cambia due fogli e Ctrl+Z li rimette
    // entrambi, anche quando il passo torna dal giornale SQLite (blocchi di 2 passi in RAM).
    MindMapModel first, second;
    MindNode *source = first.addChild(first.root(), QStringLiteral("Partenza"));
    MindNode *target = second.addChild(second.root(), QStringLiteral("Arrivo"));
    MapHistory history(2);
    history.setSheet(QStringLiteral("A"), first.toFlatSnapshot());
    history.setSheet(QStringLiteral("B"), second.toFlatSnapshot());
    const MapHistory::Snapshot firstBefore = first.toFlatSnapshot(), secondBefore = second.toFlatSnapshot();
    QVERIFY(first.setLinks({{source, target->id, {}}}));
    QVERIFY(second.setLinks({{target, source->id, {}}}));
    QList<std::pair<QString, MapHistory::Snapshot>> both{{QStringLiteral("A"), first.toFlatSnapshot()}, {QStringLiteral("B"), second.toFlatSnapshot()}};
    QVERIFY(history.commit(both));
    const MapHistory::Snapshot firstLinked = first.toFlatSnapshot(), secondLinked = second.toFlatSnapshot();
    QCOMPARE(history.undoSheet(), QStringLiteral("A"));
    QCOMPARE(history.undoSheets(), QStringList({QStringLiteral("A"), QStringLiteral("B")}));
    // Altri passi, cosi' il passo di due fogli finisce nel giornale.
    for (int i = 0; i < 5; ++i) {
        first.root()->title = QStringLiteral("Titolo %1").arg(i);
        QVERIFY(history.commit(QStringLiteral("A"), first.toFlatSnapshot()));
    }
    for (int i = 0; i < 5; ++i) history.undo();
    QCOMPARE(history.current(QStringLiteral("A")), firstLinked);
    QCOMPARE(history.undoSheets(), QStringList({QStringLiteral("A"), QStringLiteral("B")}));
    history.undo();
    QCOMPARE(history.current(QStringLiteral("A")), firstBefore);
    QCOMPARE(history.current(QStringLiteral("B")), secondBefore);
    QCOMPARE(history.lastChangedKeys(QStringLiteral("B")), QStringList{target->id});
    QCOMPARE(history.lastChangedKeys(), QStringList{source->id});  // foglio del passo
    QCOMPARE(history.redoSheets(), QStringList({QStringLiteral("A"), QStringLiteral("B")}));
    history.redo();
    QCOMPARE(history.current(QStringLiteral("A")), firstLinked);
    QCOMPARE(history.current(QStringLiteral("B")), secondLinked);
    // Un passo senza differenze in nessun foglio non si registra.
    const int steps = history.stepCount();
    for (int i = 0; i < 5; ++i) history.redo();
    QVERIFY(!history.commit({{QStringLiteral("A"), history.current(QStringLiteral("A"))}, {QStringLiteral("B"), history.current(QStringLiteral("B"))}}));
    QCOMPARE(history.stepCount(), steps);
}

QTEST_MAIN(NodeLinksTest)
#include "node_links_test.moc"
