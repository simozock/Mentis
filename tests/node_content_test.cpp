// FEAT-037 (docs/features/FEAT-037-contenuto-del-nodo.md): immagine, testo e bottoni dei
// collegamenti dentro il nodo, con il vero MainWindow: impaginazione, misura iniziale, maniglie,
// zone di rilascio, originale per il disegno nitido, misura su 1.000 nodi.
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

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QImageReader>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

class NodeContentTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void imageAndTextSitInsideInFourLayouts();
    void imageOnlyNodeAndLinksOnTheRight();
    void insertedImageFitsTheNode();
    void handlesResizeWithLockedProportions();
    void escapeCancelsImageResize();
    void draggingTheImageChangesTheLayout();
    void draggingTheImageLeavesTheNodeOpaque();
    void originalIsReadForSharpDrawing();
    void layoutOfThousandNodesWithImages();

private:
    QString imageFile(int width, int height);
    MindNode *addNodeWithImage(const QString &title, QSize size, MindNode::ImagePlacement placement = MindNode::ImagePlacement::Above);
    NodeItem *itemFor(const MindNode *node) const;
    QPoint viewPoint(const MindNode *node, const QPointF &local) const;
    void flushHistory();
    QAction *action(const QString &shortcut) const;

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    MindMapView *m_view = nullptr;
    QTemporaryDir m_dir;
};

void NodeContentTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisNodeContentTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);
    QVERIFY(m_dir.isValid());
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
}

void NodeContentTest::cleanupTestCase() { m_window.reset(); }

void NodeContentTest::init() {
    m_scene->clearSelection();
    m_view->setZoomPercent(100);
    flushHistory();
}

void NodeContentTest::flushHistory() {
    QApplication::processEvents();
    // The open history step is closed at once instead of waiting for its 150 ms timer.
    m_window->findChild<Document *>()->flushPendingStep();
    QApplication::processEvents();
}

QAction *NodeContentTest::action(const QString &shortcut) const {
    for (auto *candidate : m_window->actions())
        if (candidate->shortcuts().contains(QKeySequence(shortcut))) return candidate;
    return nullptr;
}

QString NodeContentTest::imageFile(int width, int height) {
    const QString path = m_dir.filePath(QStringLiteral("immagine-%1x%2.png").arg(width).arg(height));
    if (!QFileInfo::exists(path)) {
        QImage image(width, height, QImage::Format_RGB32);
        image.fill(QColor("#50c3f7"));
        if (!image.save(path)) return {};
    }
    return path;
}

MindNode *NodeContentTest::addNodeWithImage(const QString &title, QSize size, MindNode::ImagePlacement placement) {
    MindNode *node = m_model->addChild(m_model->root(), title);
    node->imagePath = imageFile(size.width(), size.height());
    node->imageSize = size;
    node->imagePlacement = placement;
    m_scene->updateNode(node);
    m_scene->autoLayout();
    flushHistory();
    return node;
}

NodeItem *NodeContentTest::itemFor(const MindNode *node) const {
    for (auto *item : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

QPoint NodeContentTest::viewPoint(const MindNode *node, const QPointF &local) const {
    NodeItem *item = itemFor(node);
    m_view->ensureVisible(item, 40, 40);
    return m_view->mapFromScene(item->mapToScene(local));
}

void NodeContentTest::imageAndTextSitInsideInFourLayouts() {
    using P = MindNode::ImagePlacement;
    for (const P placement : {P::Above, P::Below, P::Left, P::Right}) {
        MindNode *node = addNodeWithImage(QStringLiteral("Viaggio"), {200, 100}, placement);
        NodeItem *item = itemFor(node);
        QVERIFY(item);
        const QRectF image = item->imageBounds();
        const QRectF shape = item->boundingRect();
        QVERIFY2(shape.contains(image), "l'immagine sta dentro il nodo");
        const QRectF contentBox = QRectF(-item->textBoxWidth() / 2, image.center().y(), item->textBoxWidth(), 0);
        switch (placement) {
        case P::Above:
            QCOMPARE(image.left() - contentBox.left(), contentBox.right() - image.right());  // centrata
            QVERIFY(image.top() < 0);
            break;
        case P::Below:
            QVERIFY(image.bottom() > 0);
            break;
        case P::Left:
            QCOMPARE(image.left() - contentBox.left(), NodeStyle::kMainTextPadding.x());
            QCOMPARE(image.center().y(), 0.0);
            break;
        case P::Right:
            QCOMPARE(contentBox.right() - image.right(), NodeStyle::kMainTextPadding.x());
            break;
        }
        QVERIFY(item->textBoxWidth() >= 200 + 2 * NodeStyle::kMainTextPadding.x());  // immagine e margini
    }
}

void NodeContentTest::imageOnlyNodeAndLinksOnTheRight() {
    MindNode *node = addNodeWithImage(QString(), {120, 80});
    NodeItem *item = itemFor(node);
    QCOMPARE(item->textBoxWidth(), 120.0 + 2 * NodeStyle::kMainTextPadding.x());  // solo l'immagine con i margini
    MindNode *other = addNodeWithImage(QStringLiteral("Con collegamento"), {160, 90});
    QVERIFY(m_model->setLinks({{other, node->id, QStringLiteral("https://example.com")}}));
    QApplication::processEvents();
    const QRectF image = itemFor(other)->mapRectToScene(itemFor(other)->imageBounds());
    const QRectF topic = m_scene->linkIconSceneRect(other, NodeItem::LinkKind::Topic);
    const QRectF box = m_scene->nodeSceneRect(other);
    QVERIFY(topic.left() > image.right());  // bottoni a destra di tutto
    QVERIFY(box.contains(topic));
}

// Decisione dell'utente 2026-10-20 (al posto della misura reale fino a 3.000 px del 2026-10-16):
// un'immagine nuova e' larga quanto il posto del nodo ma almeno 200 px, in proporzione;
// "Ripristina dimensione" resta la misura reale, ridotta oltre 3.000 px.
void NodeContentTest::insertedImageFitsTheNode() {
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("Foto"));
    m_scene->autoLayout();
    m_scene->selectNode(node);
    flushHistory();
    const qreal room = std::max<qreal>(std::floor(m_scene->imageRoomWidth(node)), MainWindow::kMinInsertWidthPx);
    const QString big = imageFile(4000, 1000);
    m_window->setImageFilePicker([big] { return big; });
    m_window->chooseImage();
    QCOMPARE(node->imageSize, QSizeF(room, std::round(room / 4)));
    QCOMPARE(NodeItem::insertionImageSize(big), QSizeF(3000, 750));  // misura di "Ripristina dimensione"
    const QString small = imageFile(300, 200);
    m_window->setImageFilePicker([small] { return small; });
    node->customWidth = 300;  // larghezza a mano: l'immagine prende il posto del nodo (300 - 2 x 20 = 260 px)
    m_scene->updateNode(node);
    m_window->chooseImage();
    QCOMPARE(node->imageSize, QSizeF(260, std::round(260.0 * 200 / 300)));
    QCOMPARE(node->customWidth, 300.0);
    m_window->setImageFilePicker({});
}

void NodeContentTest::handlesResizeWithLockedProportions() {
    MindNode *node = addNodeWithImage(QStringLiteral("Ridimensiona"), {200, 100});
    NodeItem *item = itemFor(node);
    const QRectF image = item->imageBounds();
    // Clic sull'immagine: scelta, con le maniglie.
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, viewPoint(node, image.center()));
    QApplication::processEvents();
    QVERIFY(item->isImageSelected());
    QTest::qWait(QApplication::doubleClickInterval() + 50);
    // Maniglia in basso a destra trascinata di 40 px a destra: larghezza + 80 (il nodo e' centrato).
    const QPoint start = viewPoint(node, item->imageBounds().bottomRight());
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, start);
    QTest::mouseMove(m_view->viewport(), start + QPoint(20, 0));
    QTest::mouseMove(m_view->viewport(), start + QPoint(40, 0));
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, start + QPoint(40, 0));
    QApplication::processEvents();
    QCOMPARE(node->imageSize, QSizeF(280, 140));  // proporzioni bloccate
    flushHistory();
    action(QStringLiteral("Ctrl+Z"))->trigger();  // un passo
    QApplication::processEvents();
    QCOMPARE(node->imageSize, QSizeF(200, 100));
    // Clic fuori dall'immagine (sul testo): maniglie nascoste.
    action(QStringLiteral("Ctrl+Y"))->trigger();
    QApplication::processEvents();
}

void NodeContentTest::escapeCancelsImageResize() {
    MindNode *node = addNodeWithImage(QStringLiteral("Esc"), {200, 100});
    NodeItem *item = itemFor(node);
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, viewPoint(node, item->imageBounds().center()));
    QApplication::processEvents();
    QTest::qWait(QApplication::doubleClickInterval() + 50);
    const QPoint start = viewPoint(node, item->imageBounds().topLeft());
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, start);
    QTest::mouseMove(m_view->viewport(), start + QPoint(-30, 0));
    QVERIFY(node->imageSize.width() > 200);
    QVERIFY(item->cancelResize());  // come Esc nella vista
    QCOMPARE(node->imageSize, QSizeF(200, 100));
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, start + QPoint(-30, 0));
    QApplication::processEvents();
    QCOMPARE(node->imageSize, QSizeF(200, 100));
}

void NodeContentTest::draggingTheImageChangesTheLayout() {
    MindNode *node = addNodeWithImage(QStringLiteral("Sposta"), {120, 80});
    NodeItem *item = itemFor(node);
    const QPoint start = viewPoint(node, item->imageBounds().center());
    // Zona Destra: la striscia destra dentro il nodo.
    const QRectF box(-item->textBoxWidth() / 2, item->imageBounds().top() - 8, item->textBoxWidth(), 10);
    const QPoint right = viewPoint(node, QPointF(box.right() - 6, 0));
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, start);
    QTest::mouseMove(m_view->viewport(), start + QPoint(10, 0));
    QTest::mouseMove(m_view->viewport(), right);
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, right);
    QApplication::processEvents();
    QCOMPARE(node->imagePlacement, MindNode::ImagePlacement::Right);
    QVERIFY(item->imageBounds().center().x() > 0);
}

void NodeContentTest::draggingTheImageLeavesTheNodeOpaque() {
    // Bug 2026-10-17: dopo aver trascinato l'immagine il nodo restava trasparente (e poteva
    // spostarsi) finche' non si faceva doppio clic.
    MindNode *node = addNodeWithImage(QStringLiteral("Opaco"), {120, 80});
    NodeItem *item = itemFor(node);
    const QPointF before = node->position;
    const QPoint start = viewPoint(node, item->imageBounds().center());
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, start);
    QTest::mouseMove(m_view->viewport(), start + QPoint(1, 0));  // sotto la soglia
    QTest::mouseMove(m_view->viewport(), start + QPoint(2, 1));
    QCOMPARE(item->opacity(), 1.0);
    QTest::mouseMove(m_view->viewport(), start + QPoint(30, 0));  // trascinamento dell'immagine
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, start + QPoint(30, 0));
    QApplication::processEvents();
    QCOMPARE(item->opacity(), 1.0);
    QCOMPARE(node->position, before);
}

void NodeContentTest::originalIsReadForSharpDrawing() {
    // Il file tiene l'originale; la cache ne estrae una copia a piena risoluzione.
    MindMapModel model;
    MindNode *node = model.addChild(model.root(), QStringLiteral("Grande"));
    node->imagePath = imageFile(1600, 900);
    node->imageSize = {1600, 900};
    const QString path = m_dir.filePath(QStringLiteral("immagini.mentis"));
    QString error;
    QVERIFY2(BmapStorage::save(path, model, BmapStorage::SaveReason::Explicit, &error), qPrintable(error));
    MindMapModel reopened;
    QVERIFY2(BmapStorage::load(path, reopened, &error), qPrintable(error));
    const MindNode *loaded = reopened.root()->children.front().get();
    QVERIFY(!loaded->imageAssetId.isEmpty());
    QVERIFY(QImageReader(loaded->imagePath).size().width() <= 512);  // miniatura per il disegno
    const QString original = BmapStorage::originalAssetPath(path, loaded->imageAssetId, loaded->imageOriginalName, &error);
    QVERIFY2(!original.isEmpty(), qPrintable(error));
    QCOMPARE(QImageReader(original).size(), QSize(1600, 900));
}

void NodeContentTest::layoutOfThousandNodesWithImages() {
    MENTIS_SLOW_TEST();
    // Misura per FEAT-037: ridisposizione di 1.000 nodi con immagine piccola (obiettivo: non oltre
    // il 10% rispetto a prima, confronto fatto con la stessa prova sul codice precedente).
    Performance::setMode(Performance::Mode::Standard);
    const QString path = imageFile(180, 120);
    for (int i = 0; i < 1000; ++i) {
        MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("Nodo %1").arg(i));
        node->imagePath = path;
        node->imageSize = {180, 120};
    }
    // Scena ricostruita (misura di tutti i nodi) e prima ridisposizione: qui pesa l'impaginazione.
    QList<qint64> times;
    for (int pass = 0; pass < 5; ++pass) {
        QElapsedTimer timer;
        timer.start();
        m_scene->rebuild();
        m_scene->autoLayout();
        times.append(timer.elapsed());
    }
    std::sort(times.begin(), times.end());
    const qint64 best = times.at(times.size() / 2);  // mediana
    Performance::setMode(Performance::Mode::Light);
    qInfo("FEAT-037 ridisposizione di 1.000 nodi con immagine, profilo Standard: %lld ms (mediana di 5, ricostruzione e disposizione)", best);
}

QTEST_MAIN(NodeContentTest)
#include "node_content_test.moc"
