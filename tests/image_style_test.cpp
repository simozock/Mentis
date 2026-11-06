// FEAT-040 (docs/features/FEAT-040-stile-immagine.md): pagina Stile dell'immagine scelta, con il
// vero MainWindow: pagina al posto di quella del nodo, catena delle proporzioni, ripristino della
// dimensione, bordo, ombra, opacita', ripristino dello stile, storico e file.
#include "document.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "performance.h"
#include "stylecontrols.h"
#include "toggleswitch.h"

#include <QDockWidget>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QSlider>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <memory>

class ImageStyleTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void pageReplacesTheNodePageWhileTheImageIsChosen();
    void chainKeepsOrFreesTheProportions();
    void resetSizeGoesBackToTheInsertionSize();
    void borderShadowAndOpacityAreUndoableSteps();
    void resetStyleRestoresTheDefaults();
    void fileKeepsOnlyChangedValues();
    void snapshotForVisualReview();

private:
    QString imageFile(int width, int height);
    MindNode *addNodeWithImage(const QString &title, QSize fileSize, QSizeF shownSize);
    NodeItem *itemFor(const MindNode *node) const;
    void flushHistory();
    void undo();
    static void type(QLineEdit *field, const QString &text);

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    MindMapView *m_view = nullptr;
    ImageStylePage *m_page = nullptr;
    StylePage *m_nodePage = nullptr;
    QTemporaryDir m_dir;
};

void ImageStyleTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisImageStyleTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);
    QVERIFY(m_dir.isValid());
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    // The side panel is built once the window has drawn its first frame.
    QTRY_VERIFY(m_window->findChild<QDockWidget *>(QStringLiteral("InspectorDock")));
    auto *pages = qobject_cast<QStackedWidget *>(m_window->centralWidget());
    QVERIFY(pages);
    pages->setCurrentIndex(1);
    m_model = m_window->findChild<MindMapModel *>();
    m_view = m_window->findChild<MindMapView *>();
    m_page = m_window->findChild<ImageStylePage *>();
    m_nodePage = m_window->findChild<StylePage *>();
    QVERIFY(m_model && m_view && m_page && m_nodePage);
    m_scene = qobject_cast<MindMapScene *>(m_view->scene());
    QVERIFY(m_scene);
}

void ImageStyleTest::cleanupTestCase() { m_window.reset(); }

void ImageStyleTest::init() {
    m_scene->clearSelection();
    flushHistory();
}

void ImageStyleTest::flushHistory() {
    QApplication::processEvents();
    // The open history step is closed at once instead of waiting for its 150 ms timer.
    m_window->findChild<Document *>()->flushPendingStep();
    QApplication::processEvents();
}

void ImageStyleTest::undo() {
    for (auto *action : m_window->actions())
        if (action->shortcuts().contains(QKeySequence(QKeySequence::Undo))) { action->trigger(); break; }
    QApplication::processEvents();
}

void ImageStyleTest::type(QLineEdit *field, const QString &text) {
    field->setText(text);
    emit field->editingFinished();
    QApplication::processEvents();
}

QString ImageStyleTest::imageFile(int width, int height) {
    const QString path = m_dir.filePath(QStringLiteral("foto-%1x%2.png").arg(width).arg(height));
    if (!QFileInfo::exists(path)) {
        QImage image(width, height, QImage::Format_RGB32);
        image.fill(QColor("#50c3f7"));
        if (!image.save(path)) return {};
    }
    return path;
}

MindNode *ImageStyleTest::addNodeWithImage(const QString &title, QSize fileSize, QSizeF shownSize) {
    MindNode *node = m_model->addChild(m_model->root(), title);
    node->imagePath = imageFile(fileSize.width(), fileSize.height());
    node->imageSize = shownSize;
    m_scene->updateNode(node);
    m_scene->autoLayout();
    flushHistory();
    return node;
}

NodeItem *ImageStyleTest::itemFor(const MindNode *node) const {
    for (auto *item : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

void ImageStyleTest::pageReplacesTheNodePageWhileTheImageIsChosen() {
    MindNode *node = addNodeWithImage(QStringLiteral("Pagina"), {200, 100}, {200, 100});
    NodeItem *item = itemFor(node);
    QVERIFY(item);
    // Clic sull'immagine nella vista: pagina dell'immagine al posto di quella del nodo.
    const QPoint center = m_view->mapFromScene(item->mapToScene(item->imageBounds().center()));
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, center);
    QApplication::processEvents();
    QVERIFY(item->isImageSelected());
    QCOMPARE(m_scene->selectedImageNode(), node);
    QVERIFY(!m_page->isHidden());
    QVERIFY(m_nodePage->isHidden());
    QCOMPARE(m_page->widthField()->text().remove(QStringLiteral("px")).trimmed(), QStringLiteral("200"));
    QVERIFY(m_page->chainButton()->isChecked());
    // Un'altra selezione torna alla pagina del nodo.
    m_scene->clearSelection();
    m_scene->selectNode(node);
    QApplication::processEvents();
    QVERIFY(!item->isImageSelected());
    QVERIFY(m_page->isHidden());
    QVERIFY(!m_nodePage->isHidden());
}

void ImageStyleTest::chainKeepsOrFreesTheProportions() {
    MindNode *node = addNodeWithImage(QStringLiteral("Catena"), {200, 100}, {200, 100});
    itemFor(node)->selectImage();
    QApplication::processEvents();
    type(m_page->widthField(), QStringLiteral("100"));
    QCOMPARE(node->imageSize, QSizeF(100, 50));  // proporzioni mantenute
    type(m_page->heightField(), QStringLiteral("80"));
    QCOMPARE(node->imageSize, QSizeF(160, 80));
    m_page->chainButton()->click();  // catena spenta
    QVERIFY(!m_page->chainButton()->isChecked());
    type(m_page->widthField(), QStringLiteral("120"));
    QCOMPARE(node->imageSize, QSizeF(120, 80));  // l'altezza resta
    flushHistory();
    undo();
    QCOMPARE(node->imageSize, QSizeF(160, 80));  // un passo per scelta
    m_page->chainButton()->click();
}

void ImageStyleTest::resetSizeGoesBackToTheInsertionSize() {
    MindNode *node = addNodeWithImage(QStringLiteral("Ripristino"), {300, 200}, {90, 60});
    itemFor(node)->selectImage();
    QApplication::processEvents();
    m_page->resetSizeButton()->click();
    QApplication::processEvents();
    QCOMPARE(node->imageSize, QSizeF(300, 200));  // pixel reali del file
    QCOMPARE(m_page->heightField()->text().remove(QStringLiteral("px")).trimmed(), QStringLiteral("200"));
}

void ImageStyleTest::borderShadowAndOpacityAreUndoableSteps() {
    MindNode *node = addNodeWithImage(QStringLiteral("Stile"), {200, 100}, {200, 100});
    itemFor(node)->selectImage();
    QApplication::processEvents();
    // Senza bordo spessore e colore sono disattivati.
    QVERIFY(!m_page->borderWidthField()->isEnabled());
    QComboBox *border = m_page->borderField();
    const int dashed = border->findData(static_cast<int>(ImageStyle::Border::Dashed));
    QVERIFY(dashed >= 0);
    border->setCurrentIndex(dashed);
    emit border->activated(dashed);
    QApplication::processEvents();
    QCOMPARE(node->imageStyle.border, ImageStyle::Border::Dashed);
    QVERIFY(m_page->borderWidthField()->isEnabled());
    type(m_page->borderWidthField(), QStringLiteral("4"));
    QCOMPARE(node->imageStyle.borderWidth, 4.0);
    m_page->shadowSwitch()->click();
    QApplication::processEvents();
    QVERIFY(node->imageStyle.shadow);
    type(m_page->opacityField(), QStringLiteral("40%"));
    QCOMPARE(node->imageStyle.opacity, 40);
    QCOMPARE(m_page->opacitySlider()->value(), 40);
    type(m_page->opacityField(), QStringLiteral("250"));  // oltre il massimo: 100
    QCOMPARE(node->imageStyle.opacity, 100);
    // Ogni scelta e' un passo: tornano indietro una alla volta.
    undo();
    QCOMPARE(node->imageStyle.opacity, 40);
    undo();
    QCOMPARE(node->imageStyle.opacity, 100);
    QVERIFY(node->imageStyle.shadow);
    undo();
    QVERIFY(!node->imageStyle.shadow);
    undo();
    QCOMPARE(node->imageStyle.borderWidth, ImageStyle::kDefaultBorderWidthPx);
    undo();
    QCOMPARE(node->imageStyle.border, ImageStyle::Border::None);
}

void ImageStyleTest::resetStyleRestoresTheDefaults() {
    MindNode *node = addNodeWithImage(QStringLiteral("Predefiniti"), {200, 100}, {120, 60});
    itemFor(node)->selectImage();
    QApplication::processEvents();
    m_page->shadowSwitch()->click();
    type(m_page->opacityField(), QStringLiteral("0"));
    QCOMPARE(node->imageStyle.opacity, 0);
    // A 0 % l'immagine resta cliccabile: il clic la sceglie ancora.
    m_scene->clearSelection();
    QApplication::processEvents();
    NodeItem *item = itemFor(node);
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, m_view->mapFromScene(item->mapToScene(item->imageBounds().center())));
    QApplication::processEvents();
    QVERIFY(item->isImageSelected());
    m_page->resetStyleButton()->click();
    QApplication::processEvents();
    QCOMPARE(node->imageStyle, ImageStyle());
    QCOMPARE(node->imageSize, QSizeF(120, 60));  // la dimensione non cambia
}

void ImageStyleTest::fileKeepsOnlyChangedValues() {
    MindMapModel model;
    MindNode *plain = model.addChild(model.root(), QStringLiteral("Senza stile"));
    MindNode *styled = model.addChild(model.root(), QStringLiteral("Con stile"));
    for (MindNode *node : {plain, styled}) node->imagePath = imageFile(40, 20);
    styled->imageStyle.border = ImageStyle::Border::Dotted;
    styled->imageStyle.borderWidth = 3;
    styled->imageStyle.borderColor = QColor("#ff0000");
    styled->imageStyle.shadow = true;
    styled->imageStyle.opacity = 55;
    const QJsonObject json = model.toJson();
    const QString text = QString::fromUtf8(QJsonDocument(json).toJson());
    QCOMPARE(text.count(QStringLiteral("\"imageShadow\"")), 1);  // solo il nodo con stile
    QCOMPARE(text.count(QStringLiteral("\"imageOpacity\"")), 1);
    MindMapModel reopened;
    QVERIFY(reopened.fromJson(json));
    const auto &children = reopened.root()->children;
    QCOMPARE(children.size(), size_t(2));
    QCOMPARE(children[0]->imageStyle, ImageStyle());
    QCOMPARE(children[1]->imageStyle, styled->imageStyle);
}

// Foto della finestra per il confronto a vista con il riferimento: solo con
// MENTIS_SNAPSHOT_DIR, da lanciare con una piattaforma che ha i caratteri (es. windows).
void ImageStyleTest::snapshotForVisualReview() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR non impostata");
    MindNode *node = addNodeWithImage(QStringLiteral("Foto"), {240, 160}, {240, 160});
    node->imageStyle.border = ImageStyle::Border::Solid;
    node->imageStyle.borderWidth = 3;
    node->imageStyle.shadow = true;
    node->imageStyle.opacity = 70;
    m_scene->updateNode(node);
    m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->click();  // the map's, not the Gantt's
    QTest::qWait(400);  // apertura del pannello
    itemFor(node)->selectImage();
    m_view->centerOn(itemFor(node));
    QApplication::processEvents();
    QTest::qWait(200);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/image-style.png")));
}

QTEST_MAIN(ImageStyleTest)
#include "image_style_test.moc"
