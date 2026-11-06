// FEAT-027 (docs/features/FEAT-027-collegamenti-argomenti-web.md): collegamenti con il vero
// MainWindow: menu +, modali, icone nel nodo, salti fra fogli, cronologia, Undo/Redo, misure.
#include "slowtests.h"
#include "floatingui.h"
#include "linkdialogs.h"
#include "document.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "nodestyle.h"
#include "performance.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTest>
#include <QDialog>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <functional>
#include <memory>

class NodeLinksUiTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void iconSitsInsideTheNodeOnTheRight();
    void topicLinkWithBackLinkIsOneStep();
    void searchShowsMatchesWithAncestorsAndExcludesSource();
    void clickingTheIconJumpsAndAltArrowsGoBack();
    void deletedTargetTurnsGreyAndUndoRestoresIt();
    void backLinkInAnotherSheetIsOneStep();
    void webLinkIsCheckedAndOpensTheBrowser();
    void menuIsDisabledWithSeveralNodes();
    void topicDialogOnTenThousandNodes();

private:
    MindNode *addNode(const QString &title, MindNode *parent = nullptr);
    void select(const QList<MindNode *> &nodes);
    void flushHistory();
    QAction *action(const QString &shortcut) const;
    QMenu *linkMenu() const;
    // Apre il modale con l'azione e, quando e' aperto, esegue interact (poi deve chiuderlo).
    template <typename Dialog>
    void runDialog(QAction *opener, const std::function<void(Dialog *)> &interact);
    void clickIcon(const MindNode *node, NodeItem::LinkKind kind);
    QComboBox *sheetSelect() const;

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    MindMapView *m_view = nullptr;
    QList<QUrl> m_opened;
};

void NodeLinksUiTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisNodeLinksUiTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    auto *pages = qobject_cast<QStackedWidget *>(m_window->centralWidget());
    QVERIFY(pages);
    pages->setCurrentIndex(1);
    m_model = m_window->findChild<MindMapModel *>();
    m_view = m_window->findChild<MindMapView *>();
    QVERIFY(m_model && m_view);
    m_scene = qobject_cast<MindMapScene *>(m_view->scene());
    QVERIFY(m_scene);
    m_window->setUrlOpener([this](const QUrl &url) { m_opened.append(url); return true; });
}

void NodeLinksUiTest::cleanupTestCase() { m_window.reset(); }

void NodeLinksUiTest::init() {
    m_scene->clearSelection();
    m_opened.clear();
    flushHistory();
}

void NodeLinksUiTest::flushHistory() {
    QApplication::processEvents();
    // The open history step is closed at once instead of waiting for its 150 ms timer.
    m_window->findChild<Document *>()->flushPendingStep();
    QApplication::processEvents();
}

MindNode *NodeLinksUiTest::addNode(const QString &title, MindNode *parent) {
    MindNode *node = m_model->addChild(parent ? parent : m_model->root(), title);
    m_scene->autoLayout();
    flushHistory();
    return node;
}

void NodeLinksUiTest::select(const QList<MindNode *> &nodes) {
    m_scene->clearSelection();
    for (auto *item : m_scene->items())
        if (auto *node = qgraphicsitem_cast<NodeItem *>(item); node && nodes.contains(node->node())) node->setSelected(true);
    QApplication::processEvents();
}

QAction *NodeLinksUiTest::action(const QString &shortcut) const {
    for (auto *candidate : m_window->actions())
        if (candidate->shortcuts().contains(QKeySequence(shortcut))) return candidate;
    return nullptr;
}

QMenu *NodeLinksUiTest::linkMenu() const { return m_window->findChild<QMenu *>(QStringLiteral("LinkMenu")); }

template <typename Dialog>
void NodeLinksUiTest::runDialog(QAction *opener, const std::function<void(Dialog *)> &interact) {
    bool seen = false;
    QTimer::singleShot(0, this, [&] {
        auto *dialog = qobject_cast<Dialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        seen = true;
        interact(dialog);
    });
    // Rete di sicurezza: un modale rimasto aperto non blocca la prova.
    QTimer::singleShot(5000, this, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
    opener->trigger();
    QApplication::processEvents();
    QVERIFY(seen);
}

void NodeLinksUiTest::clickIcon(const MindNode *node, NodeItem::LinkKind kind) {
    const QRectF icon = m_scene->linkIconSceneRect(node, kind);
    QVERIFY(!icon.isEmpty());
    m_view->ensureVisible(icon, 40, 40);
    const QPoint point = m_view->mapFromScene(icon.center());
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, point);
    QApplication::processEvents();
}

QComboBox *NodeLinksUiTest::sheetSelect() const {
    return m_window->findChild<QComboBox *>(QStringLiteral("sheetSelect"));
}

void NodeLinksUiTest::iconSitsInsideTheNodeOnTheRight() {
    MindNode *node = addNode(QStringLiteral("Spesa settimanale"));
    MindNode *target = addNode(QStringLiteral("To-do List"));
    const QRectF before = m_scene->nodeSceneRect(node);
    QVERIFY(m_model->setLinks({{node, target->id, QStringLiteral("https://example.com")}}));
    QApplication::processEvents();
    const QRectF box = m_scene->nodeSceneRect(node);
    const QRectF topic = m_scene->linkIconSceneRect(node, NodeItem::LinkKind::Topic);
    const QRectF web = m_scene->linkIconSceneRect(node, NodeItem::LinkKind::Web);
    QVERIFY(box.width() > before.width());  // le icone allargano il riquadro
    QCOMPARE(topic.size(), QSizeF(NodeItem::kLinkButtonPx, NodeItem::kLinkButtonPx));  // bottone quadrato
    QVERIFY(box.contains(topic) && box.contains(web));
    QVERIFY(web.left() > topic.right());  // web a destra di quella dell'argomento, 4 px fra loro
    QCOMPARE(web.left() - topic.right(), NodeItem::kLinkIconGapPx);
    QVERIFY(qAbs(topic.center().y() - box.center().y()) < 1.0);
    // Larghezza scelta a mano: il riquadro resta quello, il testo va a capo prima delle icone.
    node->customWidth = 140;
    m_scene->updateNode(node);
    QApplication::processEvents();
    QVERIFY(qAbs(m_scene->linkIconSceneRect(node, NodeItem::LinkKind::Web).right() + NodeStyle::kMainTextPadding.x() - m_scene->nodeSceneRect(node).right()) < 6);
    QVERIFY(m_model->setLinks({{node, QString(), QString()}}));
    QApplication::processEvents();
    QVERIFY(m_scene->linkIconSceneRect(node, NodeItem::LinkKind::Topic).isEmpty());
}

void NodeLinksUiTest::topicLinkWithBackLinkIsOneStep() {
    MindNode *source = addNode(QStringLiteral("Spesa settimanale"));
    MindNode *target = addNode(QStringLiteral("To-do List"));
    select({source});
    runDialog<TopicLinkDialog>(linkMenu()->actions().at(0), [&](TopicLinkDialog *dialog) {
        QVERIFY(!dialog->acceptButton()->isEnabled());  // nessun argomento scelto
        QCOMPARE(dialog->focusWidget(), dialog->searchField());  // la ricerca ha il focus
        QVERIFY(dialog->selectNode(target->id));
        QVERIFY(dialog->acceptButton()->isEnabled());
        QVERIFY(dialog->backCheck()->isEnabled());
        dialog->backCheck()->setChecked(true);
        dialog->acceptButton()->click();
    });
    QCOMPARE(source->topicLink, target->id);
    QCOMPARE(target->topicLink, source->id);
    flushHistory();
    action(QStringLiteral("Ctrl+Z"))->trigger();  // un solo passo: tolti entrambi
    QApplication::processEvents();
    QVERIFY(source->topicLink.isEmpty());
    QVERIFY(target->topicLink.isEmpty());
    action(QStringLiteral("Ctrl+Y"))->trigger();
    QApplication::processEvents();
    QCOMPARE(source->topicLink, target->id);
    QCOMPARE(target->topicLink, source->id);
    // Esc chiude senza modifiche; un arrivo gia' collegato spegne la casella dell'inverso.
    MindNode *third = addNode(QStringLiteral("Terzo"));
    select({third});
    runDialog<TopicLinkDialog>(linkMenu()->actions().at(0), [&](TopicLinkDialog *dialog) {
        QVERIFY(dialog->selectNode(target->id));
        QVERIFY(!dialog->backCheck()->isEnabled());
        QTest::keyClick(dialog, Qt::Key_Escape);
    });
    QVERIFY(third->topicLink.isEmpty());
}

void NodeLinksUiTest::searchShowsMatchesWithAncestorsAndExcludesSource() {
    MindNode *parent = addNode(QStringLiteral("Lavoro"));
    MindNode *child = addNode(QStringLiteral("Lista della spesa"), parent);
    MindNode *other = addNode(QStringLiteral("Altro"));
    select({other});
    runDialog<TopicLinkDialog>(linkMenu()->actions().at(0), [&](TopicLinkDialog *dialog) {
        QVERIFY(!dialog->selectNode(other->id));  // il nodo di partenza non si sceglie
        QTest::keyClicks(dialog->searchField(), QStringLiteral("LISTA"));
        auto visible = [dialog](const QString &title) {
            for (QTreeWidgetItem *item : dialog->tree()->findItems(title, Qt::MatchExactly | Qt::MatchRecursive))
                if (!item->isHidden()) return true;
            return false;
        };
        QVERIFY(visible(child->title));
        QVERIFY(visible(parent->title));  // antenato del risultato
        QVERIFY(!visible(QStringLiteral("Altro")));
        dialog->searchField()->clear();
        QVERIFY(visible(QStringLiteral("Altro")));
        dialog->reject();
    });
}

void NodeLinksUiTest::clickingTheIconJumpsAndAltArrowsGoBack() {
    MindNode *source = addNode(QStringLiteral("Partenza"));
    MindNode *target = addNode(QStringLiteral("Arrivo"));
    QVERIFY(m_model->setLinks({{source, target->id, {}}}));
    flushHistory();
    select({source});
    clickIcon(source, NodeItem::LinkKind::Topic);
    QCOMPARE(m_scene->selectedNode(), target);
    action(QStringLiteral("Alt+Left"))->trigger();
    QApplication::processEvents();
    QCOMPARE(m_scene->selectedNode(), source);
    action(QStringLiteral("Alt+Right"))->trigger();
    QApplication::processEvents();
    QCOMPARE(m_scene->selectedNode(), target);
}

void NodeLinksUiTest::deletedTargetTurnsGreyAndUndoRestoresIt() {
    MindNode *source = addNode(QStringLiteral("Partenza"));
    MindNode *target = addNode(QStringLiteral("Da eliminare"));
    const QString targetId = target->id;
    QVERIFY(m_model->setLinks({{source, targetId, {}}}));
    flushHistory();
    QVERIFY(!m_scene->isTopicLinkBroken(source));
    m_model->removeNode(target);
    flushHistory();
    QVERIFY(m_scene->isTopicLinkBroken(source));
    select({source});
    clickIcon(source, NodeItem::LinkKind::Topic);  // nessun salto
    QCOMPARE(m_scene->selectedNode(), source);
    action(QStringLiteral("Ctrl+Z"))->trigger();
    QApplication::processEvents();
    QVERIFY(!m_scene->isTopicLinkBroken(source));
}

void NodeLinksUiTest::backLinkInAnotherSheetIsOneStep() {
    MindNode *source = addNode(QStringLiteral("Nel primo foglio"));
    const QString sourceId = source->id;
    QToolButton *addSheet = nullptr;
    for (auto *button : m_window->findChildren<QToolButton *>())
        if (button->toolTip() == QStringLiteral("Add sheet")) addSheet = button;
    QVERIFY(addSheet);
    QComboBox *sheets = sheetSelect();
    QVERIFY(sheets);
    const int firstSheet = sheets->currentIndex();
    // FEAT-026 fase D: "+ foglio" apre la galleria; si conferma la prima variante.
    QTimer::singleShot(0, [] { if (auto *gallery = qobject_cast<QDialog *>(QApplication::activeModalWidget())) gallery->accept(); });
    addSheet->click();
    flushHistory();
    const int secondSheet = sheets->currentIndex();
    QVERIFY(secondSheet != firstSheet);
    MindNode *target = addNode(QStringLiteral("Nel secondo foglio"));
    const QString targetId = target->id;
    emit sheets->activated(firstSheet);
    flushHistory();
    source = m_model->root()->children.back().get();
    QCOMPARE(source->id, sourceId);
    select({source});
    runDialog<TopicLinkDialog>(linkMenu()->actions().at(0), [&](TopicLinkDialog *dialog) {
        QVERIFY(dialog->selectNode(targetId));
        QCOMPARE(dialog->targetSheetId().isEmpty(), false);
        dialog->backCheck()->setChecked(true);
        dialog->acceptButton()->click();
    });
    QCOMPARE(source->topicLink, targetId);
    // Il clic porta al secondo foglio, con il nodo selezionato e il collegamento inverso.
    flushHistory();
    clickIcon(source, NodeItem::LinkKind::Topic);
    QCOMPARE(sheets->currentIndex(), secondSheet);
    QVERIFY(m_scene->selectedNode() && m_scene->selectedNode()->id == targetId);
    QCOMPARE(m_scene->selectedNode()->topicLink, sourceId);
    // Un Ctrl+Z (qui, nel secondo foglio) toglie entrambi i collegamenti.
    action(QStringLiteral("Ctrl+Z"))->trigger();
    QApplication::processEvents();
    QVERIFY(m_scene->selectedNode() == nullptr || m_scene->selectedNode()->topicLink.isEmpty());
    for (const auto &child : m_model->root()->children) if (child->id == targetId) QVERIFY(child->topicLink.isEmpty());
    emit sheets->activated(firstSheet);
    flushHistory();
    for (const auto &child : m_model->root()->children) if (child->id == sourceId) QVERIFY(child->topicLink.isEmpty());
    action(QStringLiteral("Ctrl+Y"))->trigger();
    QApplication::processEvents();
    for (const auto &child : m_model->root()->children) if (child->id == sourceId) QCOMPARE(child->topicLink, targetId);
}

void NodeLinksUiTest::webLinkIsCheckedAndOpensTheBrowser() {
    MindNode *node = addNode(QStringLiteral("Sito"));
    select({node});
    runDialog<WebLinkDialog>(linkMenu()->actions().at(1), [&](WebLinkDialog *dialog) {
        QVERIFY(!dialog->acceptButton()->isEnabled());  // vuoto
        QTest::keyClicks(dialog->addressField(), QStringLiteral("ftp://example.com"));
        QVERIFY(!dialog->acceptButton()->isEnabled());
        dialog->addressField()->clear();
        QTest::keyClicks(dialog->addressField(), QStringLiteral("example.com"));
        QVERIFY(dialog->acceptButton()->isEnabled());
        QTest::keyClick(dialog->addressField(), Qt::Key_Return);
    });
    QCOMPARE(node->webLink, QStringLiteral("https://example.com"));
    clickIcon(node, NodeItem::LinkKind::Web);
    QCOMPARE(m_opened, QList<QUrl>{QUrl(QStringLiteral("https://example.com"))});
}

void NodeLinksUiTest::menuIsDisabledWithSeveralNodes() {
    MindNode *a = addNode(QStringLiteral("A"));
    MindNode *b = addNode(QStringLiteral("B"));
    QMenu *menu = linkMenu();
    auto *insert = qobject_cast<QMenu *>(menu->parent());
    QVERIFY(insert);
    select({a, b});
    emit insert->aboutToShow();
    QVERIFY(!menu->menuAction()->isEnabled());
    select({a});
    emit insert->aboutToShow();
    QVERIFY(menu->menuAction()->isEnabled());
}

void NodeLinksUiTest::topicDialogOnTenThousandNodes() {
    MENTIS_SLOW_TEST();
    // Misura per FEAT-027 (obiettivo: apertura sotto 300 ms, ogni lettera sotto 50 ms in Standard).
    Performance::setMode(Performance::Mode::Standard);
    MindMapModel big;
    for (int i = 0; i < 100; ++i) {
        MindNode *branch = big.addChild(big.root(), QStringLiteral("Ramo %1").arg(i));
        for (int j = 0; j < 99; ++j) big.addChild(branch, QStringLiteral("Argomento %1-%2").arg(i).arg(j));
    }
    const QList<TopicLinkDialog::SheetContent> sheets{{QStringLiteral("s"), QStringLiteral("Foglio"), big.toFlatSnapshot()}};
    QElapsedTimer timer;
    timer.start();
    TopicLinkDialog dialog(m_window.get(), sheets, QString(), QString());
    const qint64 openMs = timer.elapsed();
    qint64 worstMs = 0;
    for (const QString &text : {QStringLiteral("a"), QStringLiteral("ar"), QStringLiteral("arg"), QStringLiteral("argomento 5"),
                                QStringLiteral("argomento 57-")}) {
        timer.restart();
        dialog.filter(text);
        worstMs = std::max(worstMs, timer.elapsed());
    }
    Performance::setMode(Performance::Mode::Light);
    qInfo("FEAT-027 modale con 10.000 nodi: apertura %lld ms, lettera della ricerca al massimo %lld ms", openMs, worstMs);
}

QTEST_MAIN(NodeLinksUiTest)
#include "node_links_ui_test.moc"
