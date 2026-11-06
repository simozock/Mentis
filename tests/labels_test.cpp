// FEAT-060 (docs/features/FEAT-060-etichette.md): etichette dei nodi con il vero MainWindow: riquadro
// "Etichette" da + > Etichetta, pillole sotto il nodo con il loro spazio, suggerimenti dal documento,
// ordinamento automatico, menu della pillola (Rimuovi, Evidenzia argomenti collegati), outliner,
// file e Markdown.
#include "labeleditor.h"
#include "document.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "performance.h"
#include "toggleswitch.h"

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTest>
#include <QTimer>
#include <QTreeView>

#include <memory>

class LabelsTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void rulesIgnoreCaseAndSpaces();
    void menuOpensTheEditorAndSavesInOneStep();
    void autoSortOffKeepsTheTypedOrder();
    void pillsSitBelowTheNodeAndMakeRoom();
    void suggestionsComeFromTheDocument();
    void menuRemovesTheLabel();
    void highlightFadesTheOtherTopics();
    void outlinerShowsThePillsBelowTheRow();
    void fileAndMarkdownKeepTheLabels();
    void newItemShowsTheLabelsAtOnce();
    void snapshotForVisualReview();

private:
    MindNode *addNode(const QString &title);
    NodeItem *itemFor(const MindNode *node) const;
    QAction *actionWithText(const QString &text) const;
    QPushButton *outlinerButton() const;
    void flushHistory();
    void undo();

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    MindMapView *m_view = nullptr;
    LabelEditor *m_editor = nullptr;
};

void LabelsTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisLabelsTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    qobject_cast<QStackedWidget *>(m_window->centralWidget())->setCurrentIndex(1);
    m_model = m_window->findChild<MindMapModel *>();
    m_view = m_window->findChild<MindMapView *>();
    m_scene = qobject_cast<MindMapScene *>(m_view->scene());
    m_editor = m_window->labelEditor();
    QVERIFY(m_model && m_view && m_scene && m_editor);
}

void LabelsTest::cleanupTestCase() { m_window.reset(); }

void LabelsTest::init() {
    // FEAT-012 (passo 4a): qui si provano misure e distanze verticali fra rami vicini, non i due lati
    // (tests/balanced_layout_test.cpp): tutti i rami a destra, la direzione "destra" della Mappa mentale.
    if (m_model->root()->structureDirection != QLatin1String("right")) {
        m_model->root()->structure = QStringLiteral("mindmap");
        m_model->root()->structureDirection = QStringLiteral("right");
        m_scene->autoLayout();
        // Passo a se': un Ctrl+Z della prova non deve toglierlo insieme alla sua modifica.
        m_window->findChild<Document *>()->flushPendingStep();
    }
    if (m_editor->isOpen()) m_editor->commitAndClose();
    Labels::setAutoSort(true);
    m_scene->setLabelHighlight({});
    m_scene->clearSelection();
    m_view->setZoomPercent(100);
    flushHistory();
}

void LabelsTest::flushHistory() {
    QApplication::processEvents();
    // The open history step is closed at once instead of waiting for its 150 ms timer.
    m_window->findChild<Document *>()->flushPendingStep();
    QApplication::processEvents();
}

void LabelsTest::undo() {
    for (auto *action : m_window->actions())
        if (action->shortcuts().contains(QKeySequence(QKeySequence::Undo))) { action->trigger(); break; }
    QApplication::processEvents();
}

MindNode *LabelsTest::addNode(const QString &title) {
    MindNode *node = m_model->addChild(m_model->root(), title);
    m_scene->autoLayout();
    flushHistory();
    return node;
}

NodeItem *LabelsTest::itemFor(const MindNode *node) const {
    for (auto *item : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

QAction *LabelsTest::actionWithText(const QString &text) const {
    for (auto *menu : m_window->findChildren<QMenu *>())
        for (auto *action : menu->actions())
            if (action->text().section(QLatin1Char('\t'), 0, 0) == text) return action;  // FEAT-041 stage B: the key after a tab
    return nullptr;
}

QPushButton *LabelsTest::outlinerButton() const {
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner")) return button;
    return nullptr;
}

void LabelsTest::rulesIgnoreCaseAndSpaces() {
    QCOMPARE(Labels::normalized({QStringLiteral(" Esame "), QStringLiteral("esame"), QString(), QStringLiteral("da  fare")}, false),
             QStringList({QStringLiteral("Esame"), QStringLiteral("da fare")}));
    QCOMPARE(Labels::normalized({QStringLiteral("zeta"), QStringLiteral("Alfa")}, true), QStringList({QStringLiteral("Alfa"), QStringLiteral("zeta")}));
    QVERIFY(Labels::same(QStringLiteral("Urgente"), QStringLiteral(" URGENTE")));
}

void LabelsTest::menuOpensTheEditorAndSavesInOneStep() {
    MindNode *node = addNode(QStringLiteral("Con etichette"));
    m_scene->selectNode(node);
    QApplication::processEvents();
    QAction *label = actionWithText(QStringLiteral("Label"));
    QVERIFY(label);
    label->trigger();
    QApplication::processEvents();
    QVERIFY(m_editor->isOpen());
    QCOMPARE(m_editor->nodeId(), node->id);
    QVERIFY(m_editor->autoSortSwitch()->isChecked());
    // Sotto il nodo, allineato a sinistra, senza coprirlo (2026-10-19).
    const QRect nodeRect = m_view->mapFromScene(m_scene->nodeSceneRect(node)).boundingRect();
    const QRect inWindow(m_view->viewport()->mapTo(m_window.get(), nodeRect.topLeft()), nodeRect.size());
    QVERIFY(!m_editor->geometry().intersects(inWindow.adjusted(4, 4, -4, -4)));
    QVERIFY(m_editor->geometry().top() > inWindow.bottom() - 4);
    QVERIFY(qAbs(m_editor->geometry().left() + 4 - inWindow.left()) <= 1);
    // Senza spazio sotto: sopra.
    const QRect nearBottom(m_window->mapToGlobal(QPoint(300, m_window->height() - 40)), QSize(120, 30));
    m_editor->moveNextTo(nearBottom);
    QVERIFY(m_editor->geometry().bottom() < m_window->height() - 40);
    QTest::keyClicks(m_editor->field(), QStringLiteral("Zeta, alfa, ALFA"));
    QTest::keyClick(m_editor->field(), Qt::Key_Return);  // chiude e salva
    QApplication::processEvents();
    QVERIFY(!m_editor->isOpen());
    QCOMPARE(node->labels, QStringList({QStringLiteral("alfa"), QStringLiteral("Zeta")}));  // doppione tolto, in ordine
    QCOMPARE(itemFor(node)->labelRects().size(), 2);
    flushHistory();
    undo();  // una sessione = un passo (etichette e ridisposizione)
    QVERIFY(node->labels.isEmpty());
    QVERIFY(itemFor(node)->labelRects().isEmpty());
}

void LabelsTest::autoSortOffKeepsTheTypedOrder() {
    MindNode *node = addNode(QStringLiteral("Ordine"));
    emit m_scene->labelActivated(node);
    QApplication::processEvents();
    m_editor->autoSortSwitch()->click();
    QVERIFY(!Labels::autoSort());
    QTest::keyClicks(m_editor->field(), QStringLiteral("zeta, beta"));
    m_editor->commitAndClose();
    QApplication::processEvents();
    QCOMPARE(node->labels, QStringList({QStringLiteral("zeta"), QStringLiteral("beta")}));
}

void LabelsTest::pillsSitBelowTheNodeAndMakeRoom() {
    MindNode *node = addNode(QStringLiteral("Pillole"));
    MindNode *next = addNode(QStringLiteral("Vicino"));
    NodeItem *item = itemFor(node);
    const qreal heightBefore = item->layoutHeight();
    emit m_scene->labelActivated(node);
    QApplication::processEvents();
    QTest::keyClicks(m_editor->field(), QStringLiteral("Esame, da ripassare"));
    m_editor->commitAndClose();
    QApplication::processEvents();
    const QList<QRectF> pills = item->labelRects();
    QCOMPARE(pills.size(), 2);
    // Fuori dalla forma, sotto, e il layout le conta: il nodo seguente non le copre.
    QVERIFY(pills.front().top() > item->shapeRect().bottom());
    QVERIFY(item->boundingRect().contains(pills.back()));
    QVERIFY(item->layoutHeight() > heightBefore);
    const QRectF lastInScene = item->mapRectToScene(pills.back());
    QVERIFY(m_scene->nodeSceneRect(next).top() > lastInScene.bottom());
    // Mouse sulla pillola: si accende la pillola, non il nodo (2026-10-19).
    QTest::mouseMove(m_view->viewport(), m_view->mapFromScene(item->mapToScene(pills.front().center())));
    QApplication::processEvents();
    QCOMPARE(item->hoveredLabel(), 0);
    QVERIFY(!item->isHovered());
    QTest::mouseMove(m_view->viewport(), m_view->mapFromScene(item->mapToScene(item->shapeRect().center())));
    QApplication::processEvents();
    QCOMPARE(item->hoveredLabel(), -1);
    QVERIFY(item->isHovered());
    // Un clic sulla pillola riapre il riquadro.
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, m_view->mapFromScene(item->mapToScene(pills.front().center())));
    QApplication::processEvents();
    QVERIFY(m_editor->isOpen());
    QCOMPARE(m_editor->typedLabels(), node->labels);
    QVERIFY(!item->isEditing());
    // Clic fuori: si chiude senza cambiare nulla.
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, QPoint(10, 300));
    QApplication::processEvents();
    QVERIFY(!m_editor->isOpen());
    QCOMPARE(node->labels.size(), 2);
}

void LabelsTest::suggestionsComeFromTheDocument() {
    MindNode *tagged = addNode(QStringLiteral("Gia' etichettato"));
    tagged->labels = {QStringLiteral("Storia"), QStringLiteral("Urgente")};
    MindNode *node = addNode(QStringLiteral("Nuovo"));
    emit m_scene->labelActivated(node);
    QApplication::processEvents();
    QVERIFY(m_editor->visibleSuggestions().contains(QStringLiteral("Storia")));
    QVERIFY(m_editor->visibleSuggestions().contains(QStringLiteral("Urgente")));
    QTest::keyClicks(m_editor->field(), QStringLiteral("urg"));  // filtro mentre si scrive
    QVERIFY(!m_editor->visibleSuggestions().contains(QStringLiteral("Storia")));
    m_editor->field()->clear();
    m_editor->addSuggestion(QStringLiteral("Storia"));
    QVERIFY(!m_editor->visibleSuggestions().contains(QStringLiteral("Storia")));  // gia' scelta
    m_editor->commitAndClose();
    QApplication::processEvents();
    QCOMPARE(node->labels, QStringList({QStringLiteral("Storia")}));
}

void LabelsTest::menuRemovesTheLabel() {
    MindNode *node = addNode(QStringLiteral("Da togliere"));
    emit m_scene->labelActivated(node);
    QApplication::processEvents();
    QTest::keyClicks(m_editor->field(), QStringLiteral("uno, due"));
    m_editor->commitAndClose();
    QApplication::processEvents();
    NodeItem *item = itemFor(node);
    const QPoint pill = m_view->mapFromScene(item->mapToScene(item->labelRects().front().center()));
    // Il menu e' modale: si sceglie "Rimuovi" appena compare.
    QTimer::singleShot(200, this, [] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        QVERIFY(menu);
        QCOMPARE(menu->actions().size(), 2);
        menu->setActiveAction(menu->actions().front());
        QTest::keyClick(menu, Qt::Key_Return);
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, pill, m_view->viewport()->mapToGlobal(pill));
    QApplication::sendEvent(m_view->viewport(), &event);
    QApplication::processEvents();
    QCOMPARE(node->labels, QStringList({QStringLiteral("uno")}));  // "due" era la prima (in ordine)
}

void LabelsTest::highlightFadesTheOtherTopics() {
    MindNode *a = addNode(QStringLiteral("Con Esame"));
    MindNode *b = addNode(QStringLiteral("Anche"));
    MindNode *c = addNode(QStringLiteral("Senza"));
    a->labels = {QStringLiteral("Esame")};
    b->labels = {QStringLiteral("esame"), QStringLiteral("Altro")};
    for (MindNode *node : {a, b}) m_scene->updateNode(node);
    m_scene->setLabelHighlight(QStringLiteral("Esame"));
    QCOMPARE(itemFor(a)->opacity(), 1.0);
    QCOMPARE(itemFor(b)->opacity(), 1.0);  // maiuscole a parte, stessa etichetta
    QVERIFY(itemFor(c)->opacity() < 0.5);
    QVERIFY(itemFor(m_model->root())->opacity() < 0.5);
    // Esc sulla mappa: tutto torna normale.
    m_view->setFocus();
    QTest::keyClick(m_view, Qt::Key_Escape);
    QApplication::processEvents();
    QVERIFY(m_scene->labelHighlight().isEmpty());
    QCOMPARE(itemFor(c)->opacity(), 1.0);
    // Anche un clic sulla mappa la chiude.
    m_scene->setLabelHighlight(QStringLiteral("Esame"));
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, QPoint(10, 300));
    QApplication::processEvents();
    QCOMPARE(itemFor(c)->opacity(), 1.0);
}

void LabelsTest::outlinerShowsThePillsBelowTheRow() {
    MindNode *plain = addNode(QStringLiteral("Senza etichette"));
    MindNode *tagged = addNode(QStringLiteral("Con etichette sotto"));
    tagged->labels = {QStringLiteral("Esame"), QStringLiteral("Urgente")};
    m_scene->updateNode(tagged);
    QPushButton *outliner = outlinerButton();
    QVERIFY(outliner);
    outliner->click();
    QApplication::processEvents();
    QTreeView *tree = nullptr;
    for (auto *candidate : m_window->findChildren<QTreeView *>())
        if (candidate->isVisible() && candidate->model()) tree = candidate;
    QVERIFY(tree);
    tree->doItemsLayout();
    QApplication::processEvents();
    const QRect plainRow = tree->visualRect(m_model->indexFromNode(plain));
    const QRect taggedRow = tree->visualRect(m_model->indexFromNode(tagged));
    QVERIFY(taggedRow.height() > plainRow.height());
    // Clic sulla prima pillola (sotto il titolo, all'inizio del testo): riquadro delle etichette.
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, QPoint(taggedRow.left() + 61 + 10, taggedRow.bottom() - 8 - 10));
    QApplication::processEvents();
    QVERIFY(m_editor->isOpen());
    QCOMPARE(m_editor->nodeId(), tagged->id);
    // Sotto le pillole, allineato all'inizio del testo (2026-10-19).
    const QRect rowInWindow(tree->viewport()->mapTo(m_window.get(), taggedRow.topLeft()), taggedRow.size());
    // (Qui la riga puo' essere in fondo alla finestra: allora il riquadro sta sopra il titolo.)
    const QRect box = m_editor->geometry().adjusted(4, 4, -4, -4);
    QVERIFY2(box.top() > rowInWindow.bottom() - 8 - 20 || box.bottom() < rowInWindow.top() + 4,
             qPrintable(QStringLiteral("%1 %2 %3").arg(box.top()).arg(box.bottom()).arg(rowInWindow.bottom())));
    QVERIFY(m_editor->geometry().left() < rowInWindow.left() + 61 + 4);
    m_editor->commitAndClose();
    // Titolo in modifica: il campo resta sulla riga del titolo, sopra le pillole (2026-10-19).
    const QModelIndex index = m_model->indexFromNode(tagged);
    tree->setCurrentIndex(index);
    tree->edit(index);
    QApplication::processEvents();
    QWidget *field = tree->indexWidget(index);
    QVERIFY(field);
    QVERIFY2(field->geometry().bottom() < taggedRow.bottom() - 8 - 20, qPrintable(QStringLiteral("%1").arg(field->geometry().bottom())));
    QTest::keyClick(field, Qt::Key_Escape);
    QApplication::processEvents();
    outliner->click();
    QApplication::processEvents();
}

void LabelsTest::fileAndMarkdownKeepTheLabels() {
    MindMapModel model;
    MindNode *node = model.addChild(model.root(), QStringLiteral("Voce"));
    node->labels = {QStringLiteral("da ripassare"), QStringLiteral("Esame")};
    MindMapModel reopened;
    QVERIFY(reopened.fromJson(model.toJson()));
    QCOMPARE(reopened.root()->children.front()->labels, node->labels);
    const QString markdown = model.toMarkdown(false);
    QVERIFY2(markdown.contains(QStringLiteral("- Voce #da_ripassare #Esame")), qPrintable(markdown));
    MindMapModel imported;
    QVERIFY(imported.fromMarkdown(markdown + QStringLiteral("    - Capitolo #2\n")));  // figlio di Voce
    const MindNode *back = imported.root()->children.front().get();
    QCOMPARE(back->title, QStringLiteral("Voce"));
    QCOMPARE(back->labels, node->labels);
    QCOMPARE(back->children.size(), size_t(1));
    QCOMPARE(back->children.front()->title, QStringLiteral("Capitolo #2"));  // un numero non e' un'etichetta
    QVERIFY(back->children.front()->labels.isEmpty());
}

// Bug 2026-11-02: a map just opened showed no pills until the topic changed (the item laid its
// labels out only when synced, not when created).
void LabelsTest::newItemShowsTheLabelsAtOnce() {
    MindMapModel model;
    MindNode *node = model.addChild(model.root(), QStringLiteral("Voce"));
    node->labels = {QStringLiteral("Esame"), QStringLiteral("da ripassare")};
    NodeItem item(node);
    QCOMPARE(item.labelRects().size(), 2);
    QVERIFY(item.layoutHeight() > NodeItem(model.addChild(model.root(), QStringLiteral("Senza"))).layoutHeight());
}

// Foto per il confronto a vista (solo con MENTIS_SNAPSHOT_DIR, piattaforma con i caratteri).
void LabelsTest::snapshotForVisualReview() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR non impostata");
    m_model->clear(QStringLiteral("Esame di storia"));
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("Rivoluzione francese"));
    MindNode *b = m_model->addChild(m_model->root(), QStringLiteral("Napoleone"));
    MindNode *c = m_model->addChild(m_model->root(), QStringLiteral("Restaurazione"));
    a->labels = {QStringLiteral("Esame"), QStringLiteral("da ripassare")};
    b->labels = {QStringLiteral("Esame")};
    c->labels = {QStringLiteral("Approfondimento"), QStringLiteral("Capitolo 4"), QStringLiteral("Mappe")};
    for (MindNode *node : {a, b, c}) m_scene->updateNode(node);
    m_scene->autoLayout();
    m_view->centerOn(itemFor(m_model->root()));
    QApplication::processEvents();
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/labels-map.png")));
    // Primo piano della pillola in hover (testo centrato, solo il bordo acceso).
    NodeItem *item = itemFor(a);
    const QPoint pill = m_view->mapFromScene(item->mapToScene(item->labelRects().front().center()));
    QTest::mouseMove(m_view->viewport(), pill);
    QTest::qWait(200);
    const QRect around = QRect(m_view->viewport()->mapTo(m_window.get(), m_view->mapFromScene(item->sceneBoundingRect()).boundingRect().topLeft()),
                               m_view->mapFromScene(item->sceneBoundingRect()).boundingRect().size()).adjusted(-10, -10, 10, 10);
    QVERIFY(m_window->grab(around).scaled(around.size() * 3).save(dir + QStringLiteral("/labels-hover.png")));
    QTest::mouseMove(m_view->viewport(), QPoint(10, 300));
    emit m_scene->labelActivated(a);
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/labels-editor.png")));
    m_editor->commitAndClose();
    m_scene->setLabelHighlight(QStringLiteral("Esame"));
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/labels-highlight.png")));
    m_scene->setLabelHighlight({});
    outlinerButton()->click();
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/labels-outliner.png")));
    outlinerButton()->click();
}

QTEST_MAIN(LabelsTest)
#include "labels_test.moc"
