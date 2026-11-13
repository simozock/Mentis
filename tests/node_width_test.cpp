// FEAT-018 (docs/features/FEAT-018-larghezza-automatica-nodi.md): larghezza automatica e manuale dei
// nodi, ritorno all'automatica con il doppio clic sul bordo. BUG-003 (misura automatica = contenuto
// + padding), BUG-004 (la vista inquadra il nodo) e FEAT-033 (scorrimento automatico, Esc).
// Vero MainWindow, piattaforma offscreen e impostazioni separate da quelle dell'applicazione.
#include "slowtests.h"
#include "bmapstorage.h"
#include "document.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "nodestyle.h"
#include "performance.h"
#include "stylecontrols.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QScrollBar>
#include <QJsonArray>
#include <QJsonObject>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTest>

#include <limits>
#include <memory>

namespace {
// Limiti approvati (AGENTS.md, "Mappa").
constexpr qreal kMaxAutoNodeWidthPx = 280;
// Margini del testo per ruolo (AGENTS.md, "Mappa"; NodeStyle::textPadding).
qreal padX(const MindNode *node) { return NodeStyle::textPadding(node).x(); }
qreal padY(const MindNode *node) { return NodeStyle::textPadding(node).y(); }
}

class NodeWidthTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void automaticNodeShrinksAfterTitleCommit();
    void automaticSizeIsContentPlusPadding();
    void emptyTitleKeepsOneCharacterWidth();
    void automaticNodeGrowsUntilMaximumThenWraps();
    void dragMakesWidthManualAndKeepsItAfterTitleChanges();
    void dragStopsAtMaximumManualWidth();
    void doubleClickOnEitherBorderRestoresAutomaticWidth();
    void doubleClickOnRootBorderKeepsCentre();
    void doubleClickOnTextOpensEditing();
    void doubleClickOnAutomaticNodeAddsNoHistoryStep();
    void undoRedoOfResizeAndReset();
    void resetKeepsFormattedAndFormulaTitles();
    void saveAndReloadKeepBothModes();
    void invalidWidthsFromFileAreCorrected();
    void viewFramesNodeAfterReset();
    void handMovementDuringDoubleClickDoesNotMoveNode();
    void viewScrollsWhileDraggingBorder();
    void viewScrollsWhileMovingNode();
    void escapeCancelsBorderDrag();
    void resetTimingOnLargeMaps();
    void panelWidthFollowsDragAndDoubleClick();

private:
    void flushHistory();
    MindNode *addNode(const QString &title);
    NodeItem *itemFor(const MindNode *node) const;
    qreal boxWidth(const MindNode *node) const;
    QPoint viewPoint(const MindNode *node, qreal localX) const;
    void dragBorder(MindNode *node, int side, qreal sceneDx);
    void doubleClickAt(const MindNode *node, qreal localX);
    void setTitle(MindNode *node, const QString &title, const QString &html = {});
    QAction *action(const QString &shortcut) const;
    void useSmallWindow();

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    MindMapView *m_view = nullptr;
};

void NodeWidthTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisNodeWidthTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    // A short double-click interval: the waits that keep two clicks apart last 150 ms, not 550.
    QGuiApplication::styleHints()->setMouseDoubleClickInterval(100);
    Performance::setMode(Performance::Mode::Light);
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    auto *pages = qobject_cast<QStackedWidget *>(m_window->centralWidget());
    QVERIFY(pages);
    pages->setCurrentIndex(1);  // editor
    m_model = m_window->findChild<MindMapModel *>();
    m_view = m_window->findChild<MindMapView *>();
    QVERIFY(m_model && m_view);
    m_scene = qobject_cast<MindMapScene *>(m_view->scene());
    QVERIFY(m_scene);
}

void NodeWidthTest::cleanupTestCase() { m_window.reset(); }

// The open history step is closed at once (Document::flushPendingStep) instead of waiting for its
// 150 ms timer: the same step, without the sleep.
void NodeWidthTest::flushHistory() {
    QApplication::processEvents();
    m_window->findChild<Document *>()->flushPendingStep();
    QApplication::processEvents();
}

void NodeWidthTest::init() {
    // FEAT-012 (passo 4a): qui si provano misure e distanze verticali fra rami vicini, non i due lati
    // (tests/balanced_layout_test.cpp): tutti i rami a destra, la direzione "destra" della Mappa mentale.
    if (m_model->root()->structureDirection != QLatin1String("right")) {
        m_model->root()->structure = QStringLiteral("mindmap");
        m_model->root()->structureDirection = QStringLiteral("right");
        m_scene->autoLayout();
        // Passo a se': un Ctrl+Z della prova non deve toglierlo insieme alla sua modifica.
        m_window->findChild<Document *>()->flushPendingStep();
    }
    // Ogni caso aggiunge i propri nodi alla stessa mappa: svuotarla con MindMapModel::clear()
    // salterebbe lo storico di MainWindow e Ctrl+Z tornerebbe a stati di casi precedenti.
    m_scene->clearSelection();
    m_window->resize(1400, 900);
    m_view->setZoomPercent(100);
    flushHistory();
}

MindNode *NodeWidthTest::addNode(const QString &title) {
    MindNode *node = m_model->addChild(m_model->root(), title);
    m_scene->autoLayout();
    flushHistory();
    return node;
}

NodeItem *NodeWidthTest::itemFor(const MindNode *node) const {
    for (auto *item : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

qreal NodeWidthTest::boxWidth(const MindNode *node) const {
    const NodeItem *item = itemFor(node);
    return item ? 2 * item->anchorHalfWidth() : -1;
}

QPoint NodeWidthTest::viewPoint(const MindNode *node, qreal localX) const {
    NodeItem *item = itemFor(node);
    m_view->ensureVisible(item, 0, 0);
    return m_view->mapFromScene(item->mapToScene(QPointF(localX, 0)));
}

void NodeWidthTest::dragBorder(MindNode *node, int side, qreal sceneDx) {
    // Zona del bordo: 4 px fuori e 5 dentro (NodeItem::resizeSideAt); si preme 2 px dentro.
    const qreal half = boxWidth(node) / 2;
    const QPoint start = viewPoint(node, side * (half - 2));
    const QPoint end = start + QPoint(qRound(sceneDx * m_view->transform().m11()), 0);
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, start);
    QTest::mouseMove(m_view->viewport(), start + (end - start) / 2);
    QTest::mouseMove(m_view->viewport(), end);
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, end);
    QApplication::processEvents();
}

void NodeWidthTest::doubleClickAt(const MindNode *node, qreal localX) {
    const QPoint point = viewPoint(node, localX);
    // Come un doppio clic reale: pressione e rilascio, poi doppio clic e rilascio.
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, point);
    QTest::mouseDClick(m_view->viewport(), Qt::LeftButton, {}, point);
    // QTest::mouseDClick di Qt 6 non invia il rilascio finale: senza, il nodo resterebbe il
    // destinatario di tutti i clic successivi (mouse grabber).
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, point);
    QApplication::processEvents();
    // Il clic successivo non deve essere contato da Qt come parte di un altro doppio clic.
    QTest::qWait(QApplication::doubleClickInterval() + 50);
}

void NodeWidthTest::setTitle(MindNode *node, const QString &title, const QString &html) {
    QVERIFY(m_window->findChild<Document *>()->editTitle(node, title, html));
    flushHistory();
}

void NodeWidthTest::useSmallWindow() {
    m_window->resize(1000, 650);
    QApplication::processEvents();
}

QAction *NodeWidthTest::action(const QString &shortcut) const {
    for (auto *candidate : m_window->actions())
        if (candidate->shortcuts().contains(QKeySequence(shortcut))) return candidate;
    return nullptr;
}

void NodeWidthTest::automaticNodeShrinksAfterTitleCommit() {
    MindNode *node = addNode(QStringLiteral("Un titolo di media lunghezza"));
    const qreal before = boxWidth(node);
    setTitle(node, QStringLiteral("Idea"));
    QCOMPARE(node->customWidth, 0.0);
    QVERIFY(boxWidth(node) < before);
    QCOMPARE(boxWidth(node), QFontMetricsF(NodeItem::titleFont(node)).horizontalAdvance(QStringLiteral("Idea")) + 2 * padX(node));
}

void NodeWidthTest::automaticSizeIsContentPlusPadding() {
    // BUG-003: nessuna misura minima fissa (prima 96x40, nodo centrale 128x48).
    for (const QString &title : {QStringLiteral("a"), QStringLiteral("Idea 1"), QStringLiteral("aaaaaaaaaaaaaaa")}) {
        MindNode *node = addNode(title);
        const QFontMetricsF metrics(NodeItem::titleFont(node));
        // Larghezze frazionarie del font predefinito: il riquadro le arrotonda al mezzo pixel.
        QVERIFY(std::abs(boxWidth(node) - (metrics.horizontalAdvance(title) + 2 * padX(node))) < 1.0);
        // layoutHeight comprende i 4 px di margine per lato di normalBoundingRect().
        QCOMPARE(itemFor(node)->layoutHeight(), metrics.height() + 2 * padY(node) + 8);
    }
    // Il nodo centrale non ha un indice nel modello: si cambia il titolo come fa la scena.
    MindNode *root = m_model->root();
    const QString rootTitle = root->title;
    root->title = QStringLiteral("Centro");
    itemFor(root)->syncFromNode();
    const QFontMetricsF rootMetrics(NodeItem::titleFont(root));
    QVERIFY(std::abs(boxWidth(root) - (rootMetrics.horizontalAdvance(QStringLiteral("Centro")) + 2 * padX(root))) < 1.0);
    QCOMPARE(itemFor(root)->layoutHeight(), rootMetrics.height() + 2 * padY(root) + 8);
    root->title = rootTitle;
    itemFor(root)->syncFromNode();
}

// BUG-020 (2026-10-20, foto di riferimento dell'utente): senza testo il nodo e' un quadrato alto quanto un
// nodo di una riga (prima largo un carattere piu' i margini).
void NodeWidthTest::emptyTitleKeepsOneCharacterWidth() {
    MindNode *node = addNode(QStringLiteral("Da svuotare"));
    setTitle(node, QString());
    QCOMPARE(boxWidth(node), itemFor(node)->shapeRect().height());
}

void NodeWidthTest::automaticNodeGrowsUntilMaximumThenWraps() {
    MindNode *node = addNode(QStringLiteral("Breve"));
    const qreal singleLineHeight = itemFor(node)->layoutHeight();
    setTitle(node, QStringLiteral("Un titolo molto lungo che non puo' stare su una sola riga del nodo automatico"));
    // A capo: il riquadro e' la riga piu' larga + padding, quindi al massimo 280 px (le parole non
    // si spezzano a meta', percio' la riga piu' larga puo' restare qualche pixel sotto il limite).
    QVERIFY(boxWidth(node) <= kMaxAutoNodeWidthPx);
    QVERIFY(boxWidth(node) > kMaxAutoNodeWidthPx - 80);
    QVERIFY(itemFor(node)->layoutHeight() > singleLineHeight);
}

void NodeWidthTest::panelWidthFollowsDragAndDoubleClick() {
    // FEAT-021: il campo Larghezza della pagina Stile segue il trascinamento del bordo (numero
    // chiaro) e il doppio clic sul bordo (di nuovo grigio).
    auto *page = m_window->findChild<StylePage *>();
    QVERIFY(page);
    MindNode *node = addNode(QStringLiteral("Nodo del pannello"));
    m_scene->selectNode(node);
    QApplication::processEvents();
    QLineEdit *width = page->widthField();
    const QString automatic = QString::number(std::round(m_scene->nodeWidth(node)));
    QCOMPARE(width->text(), automatic);
    QCOMPARE(width->property("manual").toBool(), false);
    dragBorder(node, 1, 80);
    QVERIFY(node->customWidth > 0);
    QCOMPARE(width->text(), QString::number(node->customWidth));
    QCOMPARE(width->property("manual").toBool(), true);
    doubleClickAt(node, boxWidth(node) / 2 - 2);
    QCOMPARE(node->customWidth, 0.0);
    QCOMPARE(width->text(), automatic);
    QCOMPARE(width->property("manual").toBool(), false);
}

void NodeWidthTest::dragMakesWidthManualAndKeepsItAfterTitleChanges() {
    MindNode *node = addNode(QStringLiteral("Nodo da allargare"));
    const qreal before = boxWidth(node);
    dragBorder(node, 1, 120);
    QVERIFY(node->customWidth > 0);
    const qreal manual = boxWidth(node);
    QVERIFY(qAbs(manual - (before + 120)) <= 2);
    setTitle(node, QStringLiteral("Corto"));
    QCOMPARE(boxWidth(node), manual);
    setTitle(node, QStringLiteral("Un titolo decisamente piu' lungo della larghezza scelta a mano dall'utente"));
    QCOMPARE(boxWidth(node), manual);
    setTitle(node, QStringLiteral("Prima riga\nSeconda riga"));
    QCOMPARE(boxWidth(node), manual);
}

void NodeWidthTest::dragStopsAtMaximumManualWidth() {
    MindNode *node = addNode(QStringLiteral("Nodo"));
    m_view->setZoomPercent(25);
    QApplication::processEvents();
    dragBorder(node, 1, 5000);
    QCOMPARE(node->customWidth, kMaxManualNodeWidthPx);
    QCOMPARE(boxWidth(node), kMaxManualNodeWidthPx);
}

void NodeWidthTest::doubleClickOnEitherBorderRestoresAutomaticWidth() {
    MindNode *node = addNode(QStringLiteral("Larghezza da ripristinare"));
    const qreal automatic = boxWidth(node);
    for (const int side : {-1, 1}) {
        dragBorder(node, side, 150);
        QVERIFY(node->customWidth > 0);
        doubleClickAt(node, side * (boxWidth(node) / 2 - 2));
        QCOMPARE(node->customWidth, 0.0);
        QCOMPARE(boxWidth(node), automatic);
        QVERIFY(!itemFor(node)->isEditing());
    }
}

void NodeWidthTest::doubleClickOnRootBorderKeepsCentre() {
    MindNode *root = m_model->root();
    dragBorder(root, 1, 100);
    QVERIFY(root->customWidth > 0);
    const QPointF afterDrag = itemFor(root)->pos();
    doubleClickAt(root, boxWidth(root) / 2 - 2);
    QCOMPARE(root->customWidth, 0.0);
    QCOMPARE(itemFor(root)->pos(), afterDrag);
}

void NodeWidthTest::doubleClickOnTextOpensEditing() {
    MindNode *node = addNode(QStringLiteral("Testo da modificare"));
    dragBorder(node, 1, 80);
    const qreal manual = node->customWidth;
    QVERIFY(manual > 0);
    doubleClickAt(node, 0);
    QVERIFY(itemFor(node)->isEditing());
    QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : m_view->viewport(), Qt::Key_Escape);
    QApplication::processEvents();
    QVERIFY(!itemFor(node)->isEditing());
    QCOMPARE(node->customWidth, manual);
}

void NodeWidthTest::doubleClickOnAutomaticNodeAddsNoHistoryStep() {
    MindNode *node = addNode(QStringLiteral("Nodo gia' automatico"));
    const QString id = node->id;
    dragBorder(node, 1, 70);
    const qreal manual = node->customWidth;
    QVERIFY(manual > 0);
    doubleClickAt(node, boxWidth(node) / 2 - 2);
    QCOMPARE(node->customWidth, 0.0);
    flushHistory();
    // Secondo doppio clic sul bordo, ora che il nodo e' automatico: nessun cambiamento.
    doubleClickAt(node, boxWidth(node) / 2 - 2);
    QCOMPARE(node->customWidth, 0.0);
    flushHistory();
    // Se il secondo doppio clic avesse creato un passo, Ctrl+Z annullerebbe quello e la larghezza
    // resterebbe automatica; invece annulla il ritorno all'automatica.
    QAction *undo = action(QStringLiteral("Ctrl+Z"));
    QVERIFY(undo);
    undo->trigger();
    QApplication::processEvents();
    MindNode *restored = nullptr;
    for (const auto &child : m_model->root()->children) if (child->id == id) restored = child.get();
    QVERIFY(restored);
    QCOMPARE(restored->customWidth, manual);
}

void NodeWidthTest::undoRedoOfResizeAndReset() {
    MindNode *node = addNode(QStringLiteral("Storico della larghezza"));
    const QString id = node->id;
    auto current = [this, &id]() -> MindNode * {
        for (const auto &child : m_model->root()->children) if (child->id == id) return child.get();
        return nullptr;
    };
    const qreal automatic = boxWidth(node);
    dragBorder(node, 1, 90);
    const qreal manual = node->customWidth;
    QVERIFY(manual > 0);
    doubleClickAt(node, boxWidth(node) / 2 - 2);
    QCOMPARE(node->customWidth, 0.0);
    flushHistory();
    QAction *undo = action(QStringLiteral("Ctrl+Z"));
    QAction *redo = action(QStringLiteral("Ctrl+Y"));
    QVERIFY(undo && redo);
    undo->trigger();
    QApplication::processEvents();
    QCOMPARE(current()->customWidth, manual);
    QCOMPARE(boxWidth(current()), manual);
    undo->trigger();
    QApplication::processEvents();
    QCOMPARE(current()->customWidth, 0.0);
    QCOMPARE(boxWidth(current()), automatic);
    redo->trigger();
    QApplication::processEvents();
    QCOMPARE(current()->customWidth, manual);
    redo->trigger();
    QApplication::processEvents();
    QCOMPARE(current()->customWidth, 0.0);
    QCOMPARE(boxWidth(current()), automatic);
}

void NodeWidthTest::resetKeepsFormattedAndFormulaTitles() {
    const QString html = QStringLiteral("<p>Testo <b>formattato</b> nel nodo</p>");
    MindNode *formatted = addNode(QStringLiteral("Testo formattato nel nodo"));
    setTitle(formatted, QStringLiteral("Testo formattato nel nodo"), html);
    MindNode *formula = addNode(QStringLiteral("Area $x^2 + y^2$ del cerchio"));
    for (MindNode *node : {formatted, formula}) {
        const QString title = node->title;
        const QString titleHtml = node->titleHtml;
        // La larghezza automatica si confronta fra due ritorni all'automatica, non con quella del
        // nodo appena creato: sulla piattaforma offscreen, senza font installati, QTextDocument
        // misura in modo diverso un documento appena creato (verificato: 375 px contro 189 per lo
        // stesso titolo; con i font di Windows entrambe le misure sono 179 px).
        qreal automatic = -1;
        for (int cycle = 0; cycle < 2; ++cycle) {
            dragBorder(node, 1, 100);
            QVERIFY(node->customWidth > 0);
            doubleClickAt(node, boxWidth(node) / 2 - 2);
            QCOMPARE(node->customWidth, 0.0);
            QCOMPARE(node->title, title);
            QCOMPARE(node->titleHtml, titleHtml);
            QVERIFY(boxWidth(node) > 2 * padX(node));
            if (cycle == 0) automatic = boxWidth(node);
            else QCOMPARE(boxWidth(node), automatic);
        }
    }
}

void NodeWidthTest::saveAndReloadKeepBothModes() {
    MindNode *automatic = addNode(QStringLiteral("Automatico"));
    MindNode *manual = addNode(QStringLiteral("Manuale"));
    dragBorder(manual, 1, 110);
    const qreal manualWidth = manual->customWidth;
    QVERIFY(manualWidth > 0);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("larghezze.mentis"));
    QString error;
    QVERIFY2(BmapStorage::save(path, *m_model, BmapStorage::SaveReason::Explicit, &error), qPrintable(error));
    MindMapModel restored;
    QVERIFY2(BmapStorage::load(path, restored, &error, false), qPrintable(error));
    const MindNode *savedAutomatic = nullptr, *savedManual = nullptr;
    for (const auto &child : restored.root()->children) {
        if (child->id == automatic->id) savedAutomatic = child.get();
        if (child->id == manual->id) savedManual = child.get();
    }
    QVERIFY(savedAutomatic && savedManual);
    QCOMPARE(savedAutomatic->customWidth, 0.0);
    QCOMPARE(savedManual->customWidth, manualWidth);
}

void NodeWidthTest::invalidWidthsFromFileAreCorrected() {
    QCOMPARE(sanitizedManualNodeWidth(-5), 0.0);
    QCOMPARE(sanitizedManualNodeWidth(0), 0.0);
    QCOMPARE(sanitizedManualNodeWidth(std::numeric_limits<double>::quiet_NaN()), 0.0);
    QCOMPARE(sanitizedManualNodeWidth(250), 250.0);
    QCOMPARE(sanitizedManualNodeWidth(5000), kMaxManualNodeWidthPx);
    // Anche attraverso il JSON del file: un testo al posto del numero vale "automatica".
    QJsonObject root{{QStringLiteral("id"), QStringLiteral("r")}, {QStringLiteral("title"), QStringLiteral("Centro")}};
    QJsonArray children;
    children.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("a")}, {QStringLiteral("title"), QStringLiteral("A")}, {QStringLiteral("width"), -40}});
    children.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("b")}, {QStringLiteral("title"), QStringLiteral("B")}, {QStringLiteral("width"), 5000}});
    children.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("c")}, {QStringLiteral("title"), QStringLiteral("C")}, {QStringLiteral("width"), QStringLiteral("largo")}});
    root.insert(QStringLiteral("children"), children);
    MindMapModel model;
    QString error;
    QVERIFY2(model.fromJson(QJsonObject{{QStringLiteral("format"), QStringLiteral("mentis-1")}, {QStringLiteral("root"), root}}, &error), qPrintable(error));
    QCOMPARE(model.root()->children.size(), size_t(3));
    QCOMPARE(model.root()->children[0]->customWidth, 0.0);
    QCOMPARE(model.root()->children[1]->customWidth, kMaxManualNodeWidthPx);
    QCOMPARE(model.root()->children[2]->customWidth, 0.0);
}

void NodeWidthTest::viewFramesNodeAfterReset() {
    // BUG-004: con la finestra piccola il bordo destro del nodo allargato e' fuori dalla vista;
    // dopo il doppio clic il nodo, tornato corto, deve essere tutto visibile.
    useSmallWindow();
    MindNode *node = addNode(QStringLiteral("Nodo da inquadrare"));
    dragBorder(node, 1, 1500);
    QVERIFY(node->customWidth > 0);
    doubleClickAt(node, boxWidth(node) / 2 - 2);  // viewPoint() fa scorrere fino al bordo
    QCOMPARE(node->customWidth, 0.0);
    const QRect onScreen = m_view->mapFromScene(itemFor(node)->sceneBoundingRect()).boundingRect();
    QVERIFY2(m_view->viewport()->rect().contains(onScreen), "il nodo non e' tutto visibile dopo il ritorno all'automatica");
}

void NodeWidthTest::handMovementDuringDoubleClickDoesNotMoveNode() {
    // BUG-004 (seconda segnalazione, 2026-10-15, foto dell'utente): dopo il doppio clic sul bordo
    // il nodo finiva qualche pixel piu' in alto e a sinistra dei fratelli. Durante il secondo clic
    // la mano sposta il mouse con il tasto premuto: il nodo veniva trascinato e lo spostamento
    // salvato come spostamento manuale.
    Performance::setMode(Performance::Mode::Standard);
    MindNode *node = addNode(QStringLiteral("Idea con mano mossa"));
    dragBorder(node, 1, 300);
    QVERIFY(node->customWidth > 0);
    flushHistory();
    const QPoint point = viewPoint(node, boxWidth(node) / 2 - 2);
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, point);
    QTest::mouseDClick(m_view->viewport(), Qt::LeftButton, {}, point);
    QTest::mouseMove(m_view->viewport(), point + QPoint(-4, -3));
    QTest::mouseMove(m_view->viewport(), point + QPoint(-8, -6));
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, point + QPoint(-8, -6));
    QApplication::processEvents();
    QTest::qWait(QApplication::doubleClickInterval() + 50);
    Performance::setMode(Performance::Mode::Light);
    QCOMPARE(node->customWidth, 0.0);
    QCOMPARE(node->manualOffset, QPointF());
    // Stesso bordo sinistro dei fratelli: posizione automatica del layout.
    const MindNode *sibling = m_model->root()->children.front().get();
    QCOMPARE(itemFor(node)->pos().x() - boxWidth(node) / 2, itemFor(sibling)->pos().x() - boxWidth(sibling) / 2);
}

void NodeWidthTest::viewScrollsWhileDraggingBorder() {
    useSmallWindow();
    MindNode *node = addNode(QStringLiteral("Bordo verso il margine"));
    const qreal half = boxWidth(node) / 2;
    const QPoint start = viewPoint(node, half - 2);
    const QPoint edge(m_view->viewport()->width() - 2, start.y());
    const int scrollBefore = m_view->horizontalScrollBar()->value();
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, start);
    QTest::mouseMove(m_view->viewport(), edge);
    const qreal widthAtEdge = boxWidth(node);
    QTest::qWait(400);  // mouse fermo sul margine: la vista scorre e il bordo la segue
    QTest::mouseMove(m_view->viewport(), edge);
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, edge);
    QApplication::processEvents();
    QVERIFY(m_view->horizontalScrollBar()->value() > scrollBefore);
    QVERIFY(node->customWidth > widthAtEdge + 50);
    QVERIFY(node->customWidth <= kMaxManualNodeWidthPx);
    flushHistory();
}

void NodeWidthTest::viewScrollsWhileMovingNode() {
    useSmallWindow();
    MindNode *node = addNode(QStringLiteral("Nodo da spostare"));
    const QPoint start = viewPoint(node, 0);
    const QPoint edge(m_view->viewport()->width() - 2, start.y());
    const int scrollBefore = m_view->horizontalScrollBar()->value();
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, start);
    QTest::mouseMove(m_view->viewport(), start + QPoint(20, 0));
    QTest::mouseMove(m_view->viewport(), edge);
    const qreal xAtEdge = itemFor(node)->pos().x();
    QTest::qWait(400);
    QTest::mouseMove(m_view->viewport(), edge);
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, edge);
    QApplication::processEvents();
    QVERIFY(m_view->horizontalScrollBar()->value() > scrollBefore);
    QVERIFY(itemFor(node)->pos().x() > xAtEdge + 50);
    flushHistory();
}

void NodeWidthTest::escapeCancelsBorderDrag() {
    MindNode *node = addNode(QStringLiteral("Allargamento annullato"));
    const qreal widthBefore = boxWidth(node);
    const QPointF posBefore = itemFor(node)->pos();
    const QPoint start = viewPoint(node, widthBefore / 2 - 2);
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, start);
    QTest::mouseMove(m_view->viewport(), start + QPoint(60, 0));
    QTest::mouseMove(m_view->viewport(), start + QPoint(120, 0));
    QVERIFY(node->customWidth > 0);
    QTest::keyClick(m_view->viewport(), Qt::Key_Escape);
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, start + QPoint(120, 0));
    QApplication::processEvents();
    QCOMPARE(node->customWidth, 0.0);
    QCOMPARE(boxWidth(node), widthBefore);
    QCOMPARE(itemFor(node)->pos(), posBefore);
    QCOMPARE(node->manualOffset, QPointF());
}

void NodeWidthTest::resetTimingOnLargeMaps() {
    MENTIS_SLOW_TEST();
    // Misura riportata in FEAT-018: ritorno all'automatica (ridisposizione compresa) su mappe con
    // testi lunghi. Non e' un limite del test: serve a confrontare le misure fra versioni.
    for (const int count : {1000, 10000}) {
        QJsonObject root{{QStringLiteral("id"), QStringLiteral("root")}, {QStringLiteral("title"), QStringLiteral("Centro")}};
        QJsonArray branches;
        for (int b = 0; b < 10; ++b) {
            QJsonArray leaves;
            for (int i = 0; i < count / 10 - 1; ++i)
                leaves.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("n%1-%2").arg(b).arg(i)},
                                          {QStringLiteral("title"), QStringLiteral("Nodo %1 con un testo abbastanza lungo da andare a capo nel riquadro").arg(i)}});
            branches.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("b%1").arg(b)}, {QStringLiteral("title"), QStringLiteral("Ramo %1").arg(b)},
                                        {QStringLiteral("width"), 600}, {QStringLiteral("children"), leaves}});
        }
        root.insert(QStringLiteral("children"), branches);
        QString error;
        QVERIFY2(m_model->fromJson(QJsonObject{{QStringLiteral("format"), QStringLiteral("mentis-1")}, {QStringLiteral("root"), root}}, &error), qPrintable(error));
        QApplication::processEvents();
        // Si usa un ramo il cui bordo destro non e' coperto da altri nodi.
        MindNode *branch = nullptr;
        QPoint point;
        for (const auto &child : m_model->root()->children) {
            point = viewPoint(child.get(), boxWidth(child.get()) / 2 - 2);
            auto *hit = qgraphicsitem_cast<NodeItem *>(m_view->itemAt(point));
            if (hit && hit->node() == child.get()) { branch = child.get(); break; }
        }
        QVERIFY(branch);
        QCOMPARE(branch->customWidth, 600.0);
        // Due misure: la prima subito dopo il caricamento (lo storico registra per la prima volta
        // la mappa intera), la seconda in uso normale, con la larghezza rimessa a 600 px.
        for (const char *phase : {"dopo il caricamento", "in uso normale"}) {
            if (branch->customWidth == 0) {
                branch->customWidth = 600;
                itemFor(branch)->syncFromNode();
                m_scene->autoLayout();
                flushHistory();
                point = viewPoint(branch, boxWidth(branch) / 2 - 2);
            }
            QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, point);
            QElapsedTimer timer;
            timer.start();
            QTest::mouseDClick(m_view->viewport(), Qt::LeftButton, {}, point);
            const qint64 elapsedMs = timer.elapsed();
            QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, point);
            QTest::qWait(QApplication::doubleClickInterval() + 50);
            QCOMPARE(branch->customWidth, 0.0);
            qInfo("FEAT-018 ritorno all'automatica con %d nodi, %s: %lld ms", count, phase, elapsedMs);
        }
    }
}

QTEST_MAIN(NodeWidthTest)
#include "node_width_test.moc"
