// FEAT-051 (docs/features/FEAT-051-adesivi-e-marcatori.md): adesivi nel posto dell'immagine e
// marcatori nel posto a sinistra, scelti dal pannello laterale Marcatore/Adesivo (tappa 2: raccolte
// modificabili, marcatori propri, controlli dei formati), con il vero MainWindow.
#include "document.h"
#include "iconlibrary.h"
#include "iconpanel.h"
#include "mainwindow.h"
#include "markers.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "modaldialog.h"
#include "nodeitem.h"
#include "performance.h"
#include "stylecontrols.h"
#include "toggleswitch.h"
#include "stickers.h"
#include "numberfield.h"
#include "segmentedcontrol.h"
#include "slowtests.h"
#include "stickereditor.h"
#include "tour.h"

#include <QAction>
#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDockWidget>
#include <QPushButton>
#include <QTreeView>
#include <QScrollArea>
#include <QScrollBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QToolButton>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QSvgRenderer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

class StickersMarkersTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void markerRules();
    void markerFromThePanelClickAndRemove();
    void panelWithoutSelectionAddsNothing();
    void panelChoiceGoesToEverySelectedNode();
    void markersComeBeforeTheCheckbox();
    void draggingFromAMarkerDoesNotMoveNodes();
    void stickerAddedToTheCollection();
    void builtInSetAndCategory();
    void searchAndNames();
    void severalStickersInOneStep();
    void collectionsAreEditable();
    void stickerEditorSavesACopy();
    void stickerEditorFrameAndText();
    void stickerEditorShapes();
    void stickerEditorLargeFile();
    void stickerEditorDraws();
    void customMarkerFromAFile();
    void fileChecks();
    void gridColumnsFollowThePanel();
    void dialogTextIsNotCut();
    void stickerClicksDoNotMoveTheMap();
    void clicksDoNotMoveTheMap();
    void editingStaysInTheTitlePlace();
    void newImageIsAsWideAsTheNode();
    void outlinerShowsTheContents();
    void deleteKeyRemovesEveryVisual();
    void rotateBy90Degrees();
    void emptyNodeIsASmallSquare();
    void stickerImageAndFormulaShareThePlace();
    void stickerHasTheImageStyle();
    void iconPagesScrollOnTheirOwn();
    void outlineDrawing();
    void fileKeepsStickerAndMarkers();
    void deleteRemovesTheImageNotTheNode();
    void rightClickRemovesTheSticker();
    void deleteOnAMarkerRemovesIt();
    void snapshotForVisualReview();

private:
    QString png(const QString &name, const QColor &color, QSize size = {300, 300});
    QString svg(const QString &name, const QString &color);
    void clickCell(int page, const QString &path);
    MindNode *addNode(const QString &title);
    NodeItem *itemFor(const MindNode *node) const;
    void chooseInNextMenu(const QString &text);
    void flushHistory();
    void undo();

    QTemporaryDir m_dir;
    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    MindMapView *m_view = nullptr;
};

void StickersMarkersTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);  // raccolta degli adesivi in una cartella di prova
    QVERIFY(m_dir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisStickersMarkersTest"));
    // Clean settings (CI) would bring the tour invitation and the views' tips, which take the keys.
    TourSettings::setTourOffered();
    TourSettings::setTipsEnabled(false);
    for (auto kind : {IconLibrary::Kind::Sticker, IconLibrary::Kind::Marker}) QDir(IconLibrary::folder(kind)).removeRecursively();
    QSettings().remove(QStringLiteral("stickers"));
    QSettings().remove(QStringLiteral("markers"));
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
    QVERIFY(m_model && m_view && m_scene);
}

void StickersMarkersTest::cleanupTestCase() {
    m_window.reset();
    for (auto kind : {IconLibrary::Kind::Sticker, IconLibrary::Kind::Marker}) QDir(IconLibrary::folder(kind)).removeRecursively();
}

void StickersMarkersTest::init() {
    m_scene->clearSelection();
    flushHistory();
}

void StickersMarkersTest::flushHistory() {
    // The open history step is closed at once instead of waiting for its 150 ms timer.
    QApplication::processEvents();
    m_window->findChild<Document *>()->flushPendingStep();
    QApplication::processEvents();
}

void StickersMarkersTest::undo() {
    for (auto *action : m_window->actions())
        if (action->shortcuts().contains(QKeySequence(QKeySequence::Undo))) { action->trigger(); break; }
    QApplication::processEvents();
}

QString StickersMarkersTest::png(const QString &name, const QColor &color, QSize size) {
    QImage image(size, QImage::Format_ARGB32);
    image.fill(color);
    const QString path = m_dir.filePath(name);
    return image.save(path) ? path : QString();
}

QString StickersMarkersTest::svg(const QString &name, const QString &color) {
    QFile file(m_dir.filePath(name));
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"%1\"/></svg>")
                   .arg(color).toUtf8());
    return file.fileName();
}

void StickersMarkersTest::clickCell(int page, const QString &path) {
    QAbstractButton *cell = m_window->iconPage(page)->cell(path);
    QVERIFY2(cell, qPrintable(path));
    cell->click();
    QApplication::processEvents();
}

MindNode *StickersMarkersTest::addNode(const QString &title) {
    MindNode *node = m_model->addChild(m_model->root(), title);
    m_scene->autoLayout();
    flushHistory();
    return node;
}

NodeItem *StickersMarkersTest::itemFor(const MindNode *node) const {
    for (auto *item : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

void StickersMarkersTest::chooseInNextMenu(const QString &text) {
    QTimer::singleShot(200, this, [text] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) return;
        for (QAction *action : menu->actions())
            if (action->text() == text) {
                menu->setActiveAction(action);
                QTest::keyClick(menu, Qt::Key_Return);
                return;
            }
        menu->close();
    });
}

void StickersMarkersTest::markerRules() {
    QVERIFY(Markers::isValid(QStringLiteral("priority:3")));
    QVERIFY(!Markers::isValid(QStringLiteral("priority:9")));
    QCOMPARE(Markers::next(QStringLiteral("priority:6")), QStringLiteral("priority:1"));
    QCOMPARE(Markers::next(QStringLiteral("progress:75")), QStringLiteral("progress:100"));
    // Uno per serie, nell'ordine delle serie.
    QStringList markers = Markers::with({}, QStringLiteral("flag"), QStringLiteral("flag:red"));
    markers = Markers::with(markers, QStringLiteral("priority"), QStringLiteral("priority:2"));
    markers = Markers::with(markers, QStringLiteral("priority"), QStringLiteral("priority:5"));
    QCOMPARE(markers, QStringList({QStringLiteral("priority:5"), QStringLiteral("flag:red")}));
    QCOMPARE(Markers::with(markers, QStringLiteral("flag"), QString()), QStringList{QStringLiteral("priority:5")});
}

void StickersMarkersTest::markerFromThePanelClickAndRemove() {
    MindNode *node = addNode(QStringLiteral("Con marcatore"));
    m_scene->selectNode(node);
    IconPage *page = m_window->showIconPanel(0);
    QVERIFY(page && m_window->isIconPanelShown());
    // Faccina accesa, bottone del pannello di stile spento (stessa colonna).
    QVERIFY(m_window->findChild<QToolButton *>(QStringLiteral("IconPanelToggle"))->isChecked());
    QVERIFY(!m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->isChecked());
    // Gruppi di base: le quattro serie disegnate.
    QCOMPARE(page->shownCollections().mid(0, 4), QStringList({QStringLiteral("builtin:priority"), QStringLiteral("builtin:progress"),
                                                                QStringLiteral("builtin:flag"), QStringLiteral("builtin:symbol")}));
    clickCell(0, QStringLiteral("priority:3"));
    QCOMPARE(node->markers, QStringList{QStringLiteral("priority:3")});
    QVERIFY(page->cell(QStringLiteral("priority:3"))->property("active").toBool());
    // Un altro della stessa serie lo sostituisce; lo stesso di nuovo lo toglie.
    clickCell(0, QStringLiteral("priority:5"));
    QCOMPARE(node->markers, QStringList{QStringLiteral("priority:5")});
    clickCell(0, QStringLiteral("priority:5"));
    QVERIFY(node->markers.isEmpty());
    QVERIFY(!page->cell(QStringLiteral("priority:5"))->property("active").toBool());
    clickCell(0, QStringLiteral("priority:3"));
    flushHistory();
    NodeItem *item = itemFor(node);
    QCOMPARE(item->markerRects().size(), 1);
    // Clic: valore seguente della serie, un passo.
    const QPoint point = m_view->mapFromScene(item->mapToScene(item->markerRects().front().center()));
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, point);
    QApplication::processEvents();
    QCOMPARE(node->markers, QStringList{QStringLiteral("priority:4")});
    flushHistory();
    undo();
    QCOMPARE(node->markers, QStringList{QStringLiteral("priority:3")});
    // Tasto destro: Rimuovi marcatore.
    chooseInNextMenu(QStringLiteral("Remove marker"));
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, m_view->viewport()->mapToGlobal(point));
    QApplication::sendEvent(m_view->viewport(), &event);
    QApplication::processEvents();
    QVERIFY(node->markers.isEmpty());
    QVERIFY(item->markerRects().isEmpty());
}

// Decisione 2026-10-20: senza un nodo selezionato il pannello resta aperto ma un clic non mette nulla.
// User's request 2026-10-24: with several selected topics a panel click goes to all of them, one step.
void StickersMarkersTest::panelChoiceGoesToEverySelectedNode() {
    MindNode *a = addNode(QStringLiteral("Uno"));
    MindNode *b = addNode(QStringLiteral("Due"));
    m_scene->selectNode(a);
    itemFor(b)->setSelected(true);
    m_window->showIconPanel(0);
    clickCell(0, QStringLiteral("flag:red"));
    QVERIFY(a->markers.contains(QStringLiteral("flag:red")) && b->markers.contains(QStringLiteral("flag:red")));
    QCOMPARE(m_scene->selectedNodes().size(), qsizetype(2));
    undo();
    QVERIFY(a->markers.isEmpty() && b->markers.isEmpty());
    // Only one has it: the click puts it on both; then both have it and the click removes it.
    m_window->setMarker(a, QStringLiteral("flag"), QStringLiteral("flag:red"));
    m_scene->selectNode(a);
    itemFor(b)->setSelected(true);
    clickCell(0, QStringLiteral("flag:red"));
    QVERIFY(b->markers.contains(QStringLiteral("flag:red")));
    clickCell(0, QStringLiteral("flag:red"));
    QVERIFY(a->markers.isEmpty() && b->markers.isEmpty());
    m_window->showIconPanel(1);
    clickCell(1, QStringLiteral(":/stickers/objects/rocket.svg"));
    QVERIFY(a->imageSticker && b->imageSticker);
    QCOMPARE(m_scene->selectedNodes().size(), qsizetype(2));
    undo();
    QVERIFY(a->imagePath.isEmpty() && b->imagePath.isEmpty());
}

void StickersMarkersTest::panelWithoutSelectionAddsNothing() {
    MindNode *node = addNode(QStringLiteral("Non selezionato"));
    m_window->showIconPanel(1);
    m_scene->clearSelection();
    QApplication::processEvents();
    clickCell(1, QStringLiteral(":/stickers/objects/rocket.svg"));
    m_window->showIconPanel(0);
    clickCell(0, QStringLiteral("flag:red"));
    QVERIFY(m_window->isIconPanelShown());
    QVERIFY(node->imagePath.isEmpty() && node->markers.isEmpty());
    // Il bottone del pannello di stile passa alla sua pagina, la faccina di nuovo la chiude.
    auto *style = m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"));
    auto *icons = m_window->findChild<QToolButton *>(QStringLiteral("IconPanelToggle"));
    style->click();
    QVERIFY(style->isChecked() && !icons->isChecked() && !m_window->isIconPanelShown());
    icons->click();
    QVERIFY(m_window->isIconPanelShown());
    icons->click();
    QVERIFY(!icons->isChecked() && !style->isChecked());
    // Bug 2026-10-20 (foto dell'utente): a pannello chiuso la faccina era quasi bianca sulla barra
    // chiara (icona dei menu scuri); ora scura come quella del pannello.
    const auto darkness = [](const QToolButton *button) {
        const QImage image = button->icon().pixmap(QSize(20, 20), 2.0, QIcon::Normal, QIcon::Off).toImage();
        qint64 sum = 0, count = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (const QColor c = image.pixelColor(x, y); c.alpha() > 128) { sum += c.lightness(); ++count; }
        return count ? int(sum / count) : 255;
    };
    QVERIFY2(darkness(icons) < 128, qPrintable(QString::number(darkness(icons))));
    QVERIFY(std::abs(darkness(icons) - darkness(style)) < 40);
}

void StickersMarkersTest::markersComeBeforeTheCheckbox() {
    MindNode *node = addNode(QStringLiteral("Marcatori e casella"));
    node->task = MindNode::Task::Open;
    m_window->setMarker(node, QStringLiteral("flag"), QStringLiteral("flag:green"));
    m_window->setMarker(node, QStringLiteral("symbol"), QStringLiteral("symbol:star"));
    NodeItem *item = itemFor(node);
    const QList<QRectF> markers = item->markerRects();
    QCOMPARE(markers.size(), 2);
    QVERIFY(markers[0].right() < markers[1].left());
    QVERIFY(markers.back().right() < item->taskRect().left());  // la casella dopo i marcatori
}

// Bug 2026-10-20 (foto dell'utente): trascinando dal marcatore il nodo restava trasparente e lo
// spostamento finiva su un altro nodo.
void StickersMarkersTest::draggingFromAMarkerDoesNotMoveNodes() {
    MindNode *node = addNode(QStringLiteral("Trascinato dal marcatore"));
    MindNode *other = addNode(QStringLiteral("Altro selezionato"));
    m_window->setMarker(node, QStringLiteral("priority"), QStringLiteral("priority:1"));
    m_scene->selectNode(other);
    QApplication::processEvents();
    NodeItem *item = itemFor(node);
    const QPointF nodeBefore = node->position, otherBefore = other->position;
    const QPoint start = m_view->mapFromScene(item->mapToScene(item->markerRects().front().center()));
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, start);
    for (int step = 1; step <= 6; ++step) {
        QMouseEvent move(QEvent::MouseMove, QPointF(start + QPoint(step * 20, step * 10)), m_view->viewport()->mapToGlobal(QPointF(start + QPoint(step * 20, step * 10))),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(m_view->viewport(), &move);
    }
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, start + QPoint(120, 60));
    QApplication::processEvents();
    QCOMPARE(node->position, nodeBefore);
    QCOMPARE(other->position, otherBefore);
    QCOMPARE(item->opacity(), 1.0);
    QCOMPARE(node->markers, QStringList{QStringLiteral("priority:1")});  // rilasciato fuori: nessun clic
}

void StickersMarkersTest::stickerAddedToTheCollection() {
    MindNode *node = addNode(QStringLiteral("Con adesivo"));
    m_scene->selectNode(node);
    const QString source = png(QStringLiteral("gatto.png"), Qt::red);
    IconPage *page = m_window->showIconPanel(1);
    page->pickFiles = [source] { return QStringList{source}; };
    QString asked;
    page->askName = [&asked](const QString &, const QString &current) { asked = current; return QStringLiteral("Red cat"); };
    page->addFiles({});  // nessuna raccolta propria: si crea "My stickers"
    QCOMPARE(asked, QStringLiteral("gatto"));  // the file's own name, prefilled
    QCOMPARE(QFileInfo(node->imagePath).completeBaseName(), QStringLiteral("Red cat"));
    QVERIFY(node->imageSticker);
    QVERIFY(node->imagePath.startsWith(IconLibrary::folder(IconLibrary::Kind::Sticker)));  // copiato nella libreria
    QVERIFY(page->shownPaths().contains(node->imagePath));
    QVERIFY(!itemFor(node)->boundingRect().isEmpty());
    QCOMPARE(node->imageSize, QSizeF(120, 120));  // adesivo piccolo: lato lungo 120 px
    // Per tutte le mappe: si rilegge da library.json.
    const QList<IconLibrary::Collection> collections = IconLibrary::collections(IconLibrary::Kind::Sticker);
    QVERIFY(std::any_of(collections.cbegin(), collections.cend(), [&](const IconLibrary::Collection &c) {
        return !c.builtIn && c.name == QStringLiteral("My stickers") && c.items.size() == 1 && c.items.front().path == node->imagePath;
    }));
}

// Set di base nelle risorse (la mascotte di Mentis, poi 11 raccolte di SVG Repo, CC0, nell'ordine di collections.txt);
// "Categoria" mostra tutte le raccolte o una sola.
void StickersMarkersTest::builtInSetAndCategory() {
    QStringList builtIns;
    int stickers = 0;
    for (const IconLibrary::Collection &c : IconLibrary::collections(IconLibrary::Kind::Sticker))
        if (c.builtIn) { builtIns.append(c.name); stickers += int(c.items.size()); }
    QCOMPARE(builtIns, QStringList({QStringLiteral("Mentis"), QStringLiteral("Education"), QStringLiteral("Business"), QStringLiteral("Thinking"), QStringLiteral("Objects"),
                                    QStringLiteral("Animals"), QStringLiteral("Cute animals"), QStringLiteral("Food"),
                                    QStringLiteral("Health and fitness"), QStringLiteral("Medical"), QStringLiteral("Travel"),
                                    QStringLiteral("Landmarks")}));
    QCOMPARE(stickers, 256);  // 13 PNG della mascotte + 243 SVG
    QCOMPARE(IconLibrary::collection(IconLibrary::Kind::Sticker, QStringLiteral("builtin:mentis")).items.size(), 13);
    QCOMPARE(IconLibrary::collection(IconLibrary::Kind::Sticker, QStringLiteral("builtin:objects")).items.size(), 20);
    MindNode *node = addNode(QStringLiteral("Di base"));
    m_scene->selectNode(node);
    IconPage *page = m_window->showIconPanel(1);
    QComboBox *category = page->categoryFilter();
    QVERIFY(category->count() >= 2);
    category->setCurrentIndex(category->findData(QStringLiteral("builtin:objects")));
    emit category->activated(category->currentIndex());
    QCOMPARE(page->shownCollections(), QStringList{QStringLiteral("builtin:objects")});
    QCOMPARE(page->shownPaths().size(), 20);
    clickCell(1, QStringLiteral(":/stickers/objects/rocket.svg"));
    QVERIFY(node->imageSticker);
    QCOMPARE(node->imageSize, QSizeF(120, 120));  // adesivo piccolo: lato lungo 120 px
    category->setCurrentIndex(0);
    emit category->activated(0);
    QVERIFY(page->shownCollections().size() >= 2);
}

// Richiesta dell'utente 2026-10-20: piu' adesivi nella raccolta in un solo passaggio.
void StickersMarkersTest::severalStickersInOneStep() {
    MindNode *node = addNode(QStringLiteral("Molti"));
    m_scene->selectNode(node);
    const QStringList sources{png(QStringLiteral("uno.png"), Qt::red), png(QStringLiteral("due.png"), Qt::green),
                              png(QStringLiteral("tre.png"), Qt::blue)};
    IconPage *page = m_window->showIconPanel(1);
    const qsizetype before = page->shownPaths().size();
    page->pickFiles = [sources] { return sources; };
    page->askName = [](const QString &, const QString &current) { return current; };
    page->addFiles({});
    QCOMPARE(page->shownPaths().size(), before + 3);
    QVERIFY(node->imagePath.isEmpty());  // con piu' file si sceglie poi quale usare
}

// Raccolte modificabili: nuova, rinomina, spostamento e riordino degli elementi, riordino delle
// raccolte, quelle di base nascoste e ripristinate, quelle proprie eliminate, cartella importata.
// FEAT-070 stage 1: recolour a sticker, save it as a new one, delete the original (own stickers only).
void StickersMarkersTest::stickerEditorSavesACopy() {
    using IconLibrary::Kind;
    IconPage *page = m_window->showIconPanel(1);
    const QString camera = QStringLiteral(":/stickers/objects/camera.svg");
    bool asked = false;
    page->askDeleteOriginal = [&](const QString &) { asked = true; return true; };
    page->runEditor = [](StickerEditorDialog &dialog) {
        const QColor before = dialog.colors().value(0);
        dialog.setColor(0, QColor(QStringLiteral("#123456")));
        if (dialog.colors().value(0) != QColor(QStringLiteral("#123456"))) return false;
        dialog.undo();
        if (dialog.colors().value(0) != before || dialog.isModified()) return false;
        dialog.redo();
        dialog.collectionSelect()->setCurrentIndex(dialog.collectionSelect()->count() - 1);  // New collection…
        if (dialog.acceptButton()->isEnabled()) return false;  // needs the collection's name
        dialog.newCollectionField()->setText(QStringLiteral("Edited"));
        dialog.nameField()->setText(QStringLiteral("Blue camera"));
        return dialog.acceptButton()->isEnabled();
    };
    page->editSticker(camera, QStringLiteral("builtin:objects"));
    QVERIFY(!asked);  // a built-in sticker stays where it is
    QString edited;
    for (const IconLibrary::Collection &c : IconLibrary::collections(Kind::Sticker))
        if (c.name == QStringLiteral("Edited")) edited = c.id;
    QVERIFY(!edited.isEmpty());
    const IconLibrary::Collection copy = IconLibrary::collection(Kind::Sticker, edited);
    QCOMPARE(copy.items.size(), 1);
    QCOMPARE(copy.items[0].name, QStringLiteral("Blue camera"));
    QFile file(copy.items[0].path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("#123456"));
    file.close();
    const QList<IconLibrary::Item> objects = IconLibrary::collection(Kind::Sticker, QStringLiteral("builtin:objects")).items;
    QVERIFY(std::any_of(objects.cbegin(), objects.cend(), [&](const IconLibrary::Item &item) { return item.path == camera; }));

    // An own sticker: the copy goes in the same collection and the original is deleted with its file.
    const QString own = copy.items[0].path;
    page->runEditor = [](StickerEditorDialog &dialog) {
        dialog.setColor(0, QColor(QStringLiteral("#654321")));
        return dialog.acceptButton()->isEnabled();
    };
    page->editSticker(own, edited);
    QVERIFY(asked);
    const IconLibrary::Collection after = IconLibrary::collection(Kind::Sticker, edited);
    QCOMPARE(after.items.size(), 1);
    QVERIFY(after.items[0].path != own);
    IconLibrary::removeDeletedFiles(Kind::Sticker);  // BUG-037: the old file goes at the next start
    QVERIFY(!QFile::exists(own));

    // Closing with changes asks first; "keep editing" leaves the editor open.
    StickerEditorDialog dialog(m_window.get(), camera, IconLibrary::collections(Kind::Sticker), QStringLiteral("builtin:objects"));
    QVERIFY(dialog.isValid());
    int questions = 0;
    dialog.confirmDiscard = [&] { ++questions; return false; };
    dialog.setColor(0, Qt::red);
    dialog.show();
    dialog.reject();
    QCOMPARE(questions, 1);
    QVERIFY(dialog.isVisible());
    dialog.confirmDiscard = [&] { ++questions; return true; };
    dialog.reject();
    QVERIFY(!dialog.isVisible());
}

namespace {
// The file keeps 6 significant digits.
bool almost(const QRectF &a, const QRectF &b) {
    return std::abs(a.left() - b.left()) < 0.01 && std::abs(a.top() - b.top()) < 0.01 && std::abs(a.right() - b.right()) < 0.01 &&
           std::abs(a.bottom() - b.bottom()) < 0.01;
}
bool almost(const QPointF &a, const QPointF &b) { return std::abs(a.x() - b.x()) < 0.01 && std::abs(a.y() - b.y()) < 0.01; }
}  // namespace

// FEAT-070 stage 2: turns, flips, frame, margin and a label, each one step of the editor's history.
void StickersMarkersTest::stickerEditorFrameAndText() {
    StickerEditorDialog dialog(m_window.get(), QStringLiteral(":/stickers/objects/camera.svg"),
                               IconLibrary::collections(IconLibrary::Kind::Sticker), QStringLiteral("builtin:objects"));
    QVERIFY(dialog.isValid());
    const QRectF original = dialog.document().viewBox();
    dialog.rotateBy(90);
    QCOMPARE(dialog.document().transform().rotation, 90.0);
    dialog.rotateBy(-180);
    QCOMPARE(dialog.document().transform().rotation, -90.0);
    dialog.flipHorizontally();
    QVERIFY(dialog.document().transform().flipH);
    dialog.undo();
    dialog.undo();
    QCOMPARE(dialog.document().transform().rotation, 90.0);  // each quarter turn is its own step
    dialog.undo();
    // The angle field: its steps are one change, and the frame widens once for the final angle.
    NumberField *angle = dialog.findChild<NumberField *>(QStringLiteral("StickerAngle"));
    QVERIFY(angle);
    for (int degrees = 1; degrees <= 30; ++degrees) {  // as its + pressed 30 times
        angle->setValue(degrees);
        emit angle->valueChosen(degrees);
    }
    SvgDocument direct;
    QVERIFY(direct.load(dialog.svg()));
    dialog.undo();
    SvgDocument once;
    QVERIFY(once.load(dialog.svg()));
    once.setTransform({30, false, false});
    QVERIFY(almost(direct.viewBox(), once.viewBox()));
    QCOMPARE(dialog.document().transform().rotation, 0.0);
    QCOMPARE(dialog.document().transform(), SvgDocument::Transform());
    QVERIFY(almost(dialog.document().viewBox(), original));

    dialog.fitToDrawing();
    const QRectF fitted = dialog.document().viewBox();
    QVERIFY(original.contains(fitted) && fitted != original);
    dialog.marginField()->setValue(10);
    dialog.addMargin();
    const qreal margin = qMax(fitted.width(), fitted.height()) / 10;
    QVERIFY(almost(dialog.document().viewBox(), fitted.adjusted(-margin, -margin, margin, margin)));

    // Typing is one step; the label takes the controls' style.
    dialog.labelSizeField()->setValue(40);
    dialog.labelBoldButton()->setChecked(true);
    dialog.setLabelText(QStringLiteral("1"));
    dialog.setLabelText(QStringLiteral("12"));
    SvgDocument::Label label = dialog.document().label();
    QCOMPARE(label.text, QStringLiteral("12"));
    QVERIFY(label.bold);
    const QRectF box = dialog.document().viewBox();
    QVERIFY(std::abs(label.size - qMax(box.width(), box.height()) * 40 / 100) < 0.01);
    dialog.setLabelPosition(box.topLeft() + QPointF(box.width() / 4, box.height() / 4));
    QVERIFY(almost(dialog.document().label().position, box.topLeft() + QPointF(box.width() / 4, box.height() / 4)));
    dialog.undo();
    QCOMPARE(dialog.document().label().text, QStringLiteral("12"));
    dialog.undo();
    QVERIFY(dialog.document().label().text.isEmpty());
    QVERIFY(dialog.labelField()->text().isEmpty());
    dialog.redo();
    QCOMPARE(dialog.labelField()->text(), QStringLiteral("12"));

    // Dragging the frame's right edge on the preview (Frame mode): one step on release.
    StickerPreview *preview = dialog.preview();
    preview->setMode(StickerPreview::Mode::Frame);
    const QRectF frame = dialog.document().viewBox();
    const QPoint edge = preview->toWidget(QPointF(frame.right(), frame.center().y())).toPoint();
    const QPoint inside = preview->toWidget(QPointF(frame.right() - frame.width() / 4, frame.center().y())).toPoint();
    QTest::mousePress(preview, Qt::LeftButton, {}, edge);
    QTest::mouseMove(preview, inside);
    QCOMPARE(dialog.document().viewBox(), frame);  // nothing written while dragging
    QTest::mouseRelease(preview, Qt::LeftButton, {}, inside);
    QVERIFY(dialog.document().viewBox().width() < frame.width() * 0.8);
    QVERIFY(std::abs(dialog.document().viewBox().left() - frame.left()) < 0.01);
}

namespace {
// A point inside a large drawn shape that a click picks (the topmost there), in viewBox units.
// away: an area to keep out of (another selection's box and its handles).
bool pickablePoint(const StickerEditorDialog &dialog, QString *id, QPointF *point, const QStringList &except = {}, const QRectF &away = {}) {
    const QList<SvgDocument::Shape> shapes = dialog.document().drawnShapes();
    for (auto i = shapes.size() - 1; i >= 0; --i) {
        const SvgDocument::Shape &shape = shapes[i];
        const QRectF box = shape.outline.boundingRect();
        if (!shape.filled || except.contains(shape.id) || box.width() < dialog.document().viewBox().width() / 8) continue;
        for (int y = 1; y < 8; ++y)
            for (int x = 1; x < 8; ++x) {
                const QPointF at(box.left() + box.width() * x / 8, box.top() + box.height() * y / 8);
                if (!away.contains(at) && SvgDocument::shapeAt(shapes, at, dialog.document().viewBox().width() / 50) == shape.id) {
                    *id = shape.id;
                    *point = at;
                    return true;
                }
            }
    }
    return false;
}
}  // namespace

// FEAT-070 stage 3: select, move, resize, nudge, delete and edit the points, with the mouse and keys.
void StickersMarkersTest::stickerEditorShapes() {
    StickerEditorDialog dialog(m_window.get(), QStringLiteral(":/stickers/objects/camera.svg"),
                               IconLibrary::collections(IconLibrary::Kind::Sticker), QStringLiteral("builtin:objects"));
    QVERIFY(dialog.isValid());
    dialog.show();
    StickerPreview *preview = dialog.preview();
    QCOMPARE(preview->mode(), StickerPreview::Mode::Shapes);
    QString id;
    QPointF inside;
    QVERIFY(pickablePoint(dialog, &id, &inside));
    auto boundsOf = [&](const QString &shape) { return SvgDocument::bounds(dialog.document().drawnShapes(), {shape}); };
    const QRectF before = boundsOf(id);
    const QPoint at = preview->toWidget(inside).toPoint();
    QTest::mouseClick(preview, Qt::LeftButton, {}, at);
    QCOMPARE(preview->selection(), QStringList{id});

    // Drag: one step on release, moved by the drag in viewBox units.
    const QPoint to = at + QPoint(20, 10);
    const QPointF delta = preview->toDocument(to) - preview->toDocument(at);
    QTest::mousePress(preview, Qt::LeftButton, {}, at);
    QTest::mouseMove(preview, at + QPoint(10, 5));
    QTest::mouseMove(preview, to);
    QVERIFY(almost(boundsOf(id), before));  // nothing written while dragging
    QTest::mouseRelease(preview, Qt::LeftButton, {}, to);
    QVERIFY(almost(boundsOf(id), before.translated(delta)));
    dialog.undo();
    QVERIFY(almost(boundsOf(id), before));

    // Arrows: 1% of the frame's longer side each, all of them one step.
    const QRectF box = dialog.document().viewBox();
    const qreal step = qMax(box.width(), box.height()) / 100;
    preview->setFocus();
    QTest::keyClick(preview, Qt::Key_Right);
    QTest::keyClick(preview, Qt::Key_Right);
    QTest::keyClick(preview, Qt::Key_Down, Qt::ShiftModifier);
    QVERIFY(almost(boundsOf(id), before));  // shown at once, written after a pause
    QTRY_VERIFY(almost(boundsOf(id), before.translated(2 * step, 10 * step)));
    QTest::keyClick(preview, Qt::Key_Z, Qt::ControlModifier);  // the editor's Undo, not the map's
    QVERIFY(almost(boundsOf(id), before));

    // Resize from the bottom-right handle with Shift: proportions kept, the top-left stays.
    const QPoint corner = preview->toWidget(before.bottomRight()).toPoint();
    const QPoint further = corner + QPoint(30, 5);
    QTest::mousePress(preview, Qt::LeftButton, Qt::ShiftModifier, corner);
    {  // QTest::mouseMove sends no modifiers
        QMouseEvent move(QEvent::MouseMove, QPointF(further), preview->mapToGlobal(QPointF(further)), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
        QApplication::sendEvent(preview, &move);
    }
    QTest::mouseRelease(preview, Qt::LeftButton, Qt::ShiftModifier, further);
    const QRectF grown = boundsOf(id);
    QVERIFY(grown.width() > before.width() * 1.05);
    QVERIFY(std::abs(grown.width() / grown.height() - before.width() / before.height()) < 0.02);
    QVERIFY2(almost(grown.topLeft(), before.topLeft()), qPrintable(QStringLiteral("%1,%2 %3x%4 -> %5,%6 %7x%8").arg(before.x()).arg(before.y()).arg(before.width()).arg(before.height()).arg(grown.x()).arg(grown.y()).arg(grown.width()).arg(grown.height())));
    dialog.undo();

    // Shift+click adds a second shape; Delete removes both in one step; Esc empties the selection.
    QString other;
    QPointF otherPoint;
    const qreal room = box.width() / 10;
    QVERIFY(pickablePoint(dialog, &other, &otherPoint, {id}, before.adjusted(-room, -room, room, room)));
    QTest::mouseClick(preview, Qt::LeftButton, Qt::ShiftModifier, preview->toWidget(otherPoint).toPoint());
    QCOMPARE(preview->selection().size(), 2);
    const int count = int(dialog.document().drawnShapes().size());
    QTest::keyClick(preview, Qt::Key_Delete);
    QCOMPARE(int(dialog.document().drawnShapes().size()), count - 2);
    QVERIFY(preview->selection().isEmpty());
    dialog.undo();
    QCOMPARE(int(dialog.document().drawnShapes().size()), count);

    // Double click: the shape's points; dragging the first point changes the outline.
    QTest::mouseDClick(preview, Qt::LeftButton, {}, at);
    QCOMPARE(preview->editedShape(), id);
    const QList<SvgDocument::SubPath> path = dialog.document().path(id);
    QVERIFY(!path.isEmpty());
    const QPoint node = preview->toWidget(dialog.document().shapeMatrix(id).map(path.first().start)).toPoint();
    QTest::mousePress(preview, Qt::LeftButton, {}, node);
    QTest::mouseMove(preview, node + QPoint(15, 0));
    QTest::mouseRelease(preview, Qt::LeftButton, {}, node + QPoint(15, 0));
    QVERIFY(dialog.document().path(id).first().start != path.first().start);
    QVERIFY(dialog.svg().contains(QStringLiteral("id=\"%1\"").arg(id).toUtf8()));
    // Esc ends the point editing and keeps the editor open.
    QTest::keyClick(preview, Qt::Key_Escape);
    QVERIFY(preview->editedShape().isEmpty());
    QVERIFY(dialog.isVisible());
    dialog.confirmDiscard = [] { return true; };
    dialog.reject();
}

namespace {
// A sticker near the 2 MB limit: thousands of curved shapes in groups, with gradients.
QByteArray largeSticker() {
    QByteArray data = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1000 1000\"><defs>"
                      "<linearGradient id=\"g\"><stop offset=\"0\" stop-color=\"#ff0000\"/><stop offset=\"1\" stop-color=\"#0000ff\"/></linearGradient></defs>";
    int n = 0;
    while (data.size() < IconLibrary::kMaxFileBytes - 4000) {
        data += "<g transform=\"translate(" + QByteArray::number(n % 40 * 25) + " " + QByteArray::number(n / 40 % 40 * 25) + ")\">";
        for (int i = 0; i < 5; ++i, ++n)
            data += "<path fill=\"" + QByteArray(i == 0 ? "url(#g)" : i % 2 ? "#336699" : "#cc6633") +
                    "\" d=\"M2 2C5 0 10 0 13 2S20 8 18 12 10 20 6 18 0 10 2 2zM4 4l3 1-1 3z\" stroke=\"#000\" stroke-width=\"0.5\"/>";
        data += "</g>";
    }
    return data + "</svg>";
}
}  // namespace

// FEAT-070: how long the editor takes on a sticker near the 2 MB limit (slow: run on request).
void StickersMarkersTest::stickerEditorLargeFile() {
    MENTIS_SLOW_TEST();
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("large.svg"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(largeSticker());
    file.close();
    QElapsedTimer clock;
    clock.start();
    {
        SvgDocument document;
        document.load(largeSticker());
        qInfo() << "  load" << clock.restart() << "ms";
        document.ensureShapeIds();
        qInfo() << "  ids" << clock.restart() << "ms";
        const auto shapes = document.drawnShapes();
        qInfo() << "  drawnShapes" << clock.restart() << "ms";
        document.replaceColor(QColor(QStringLiteral("#336699")), Qt::green);
        qInfo() << "  replaceColor" << clock.restart() << "ms";
        document.transformShapes({shapes.first().id}, QTransform::fromTranslate(1, 0));
        qInfo() << "  transformShapes" << clock.restart() << "ms";
        QSvgRenderer renderer(document.data());
        qInfo() << "  renderer load" << clock.restart() << "ms";
        QImage image(320, 320, QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&image);
        renderer.render(&painter);
        qInfo() << "  render 320" << clock.restart() << "ms";
    }
    StickerEditorDialog dialog(m_window.get(), path, IconLibrary::collections(IconLibrary::Kind::Sticker), QStringLiteral("builtin:objects"));
    qInfo() << "open" << clock.restart() << "ms, bytes" << dialog.svg().size() << "shapes" << dialog.document().drawnShapes().size();
    dialog.show();
    StickerPreview *preview = dialog.preview();
    preview->repaint();
    qInfo() << "first paint" << clock.restart() << "ms";
    preview->repaint();
    qInfo() << "paint" << clock.restart() << "ms";
    dialog.setColor(1, Qt::green);
    qInfo() << "recolour" << clock.restart() << "ms";
    const QString id = dialog.document().drawnShapes().first().id;
    preview->setSelection({id});
    const QPoint at = preview->toWidget(SvgDocument::bounds(dialog.document().drawnShapes(), {id}).center()).toPoint();
    QTest::mousePress(preview, Qt::LeftButton, {}, at);
    qInfo() << "press" << clock.restart() << "ms";
    for (int i = 1; i <= 10; ++i) {
        QTest::mouseMove(preview, at + QPoint(i * 3, 0));
        preview->repaint();
    }
    qInfo() << "10 drag moves" << clock.restart() << "ms";
    QTest::mouseRelease(preview, Qt::LeftButton, {}, at + QPoint(30, 0));
    qInfo() << "release (one step)" << clock.restart() << "ms";
    preview->setFocus();
    for (int i = 0; i < 5; ++i) QTest::keyClick(preview, Qt::Key_Right);
    qInfo() << "5 arrows" << clock.restart() << "ms";
    QTest::keyClicks(dialog.labelField(), QStringLiteral("12345"));
    qInfo() << "5 typed characters (waiting for the pause)" << clock.restart() << "ms" << dialog.labelField()->text();
    QTRY_COMPARE(dialog.document().label().text, QStringLiteral("12345"));
    qInfo() << "the text written, one step" << clock.restart() << "ms";
    dialog.undo();
    qInfo() << "undo" << clock.restart() << "ms";

    // The history keeps at most 64 MB: 30 steps of 2.4 MB do not all stay, the original does.
    const QByteArray original = StickerEditorDialog(m_window.get(), path, {}, QString()).svg();
    for (int i = 0; i < 30; ++i) dialog.setColor(0, i % 2 ? QColor(Qt::red) : QColor(Qt::blue));
    int undos = 0;
    for (QByteArray last = dialog.svg(); undos < 60; ++undos) {
        dialog.undo();
        if (dialog.svg() == last) break;
        last = dialog.svg();
    }
    qInfo() << "undo steps kept" << undos;
    QVERIFY(undos < 30);
    QCOMPARE(dialog.svg(), original);
    QVERIFY(!dialog.isModified());
    dialog.confirmDiscard = [] { return true; };
    dialog.reject();
}

namespace {
// A value typed in an app number field and confirmed.
void choose(NumberField *field, double value) {
    QVERIFY(field);
    field->setValue(value);
    emit field->valueChosen(value);
}

void drag(QWidget *widget, const QPoint &from, const QPoint &to, Qt::KeyboardModifiers modifiers = {}) {
    QTest::mousePress(widget, Qt::LeftButton, modifiers, from);
    QMouseEvent move(QEvent::MouseMove, QPointF(to), widget->mapToGlobal(QPointF(to)), Qt::NoButton, Qt::LeftButton, modifiers);
    QApplication::sendEvent(widget, &move);  // QTest::mouseMove sends no modifiers
    QTest::mouseRelease(widget, Qt::LeftButton, modifiers, to);
}
}  // namespace

// FEAT-070 stage 4: every tool draws one shape (one step), selected, with the style row's style.
void StickersMarkersTest::stickerEditorDraws() {
    StickerEditorDialog dialog(m_window.get(), QStringLiteral(":/stickers/objects/camera.svg"),
                               IconLibrary::collections(IconLibrary::Kind::Sticker), QStringLiteral("builtin:objects"));
    dialog.show();
    StickerPreview *preview = dialog.preview();
    auto tool = [&](const char *name) {
        auto *button = dialog.findChild<QToolButton *>(QString::fromLatin1(name));
        QVERIFY(button);
        button->click();
    };
    const QRectF box = dialog.document().viewBox();
    auto at = [&](qreal x, qreal y) { return preview->toWidget(box.topLeft() + QPointF(box.width() * x, box.height() * y)).toPoint(); };
    auto last = [&] { return dialog.document().drawnShapes().last(); };
    const int before = int(dialog.document().drawnShapes().size());

    // Rectangle: the dragged box; Shift makes a square.
    tool("StickerRectangleTool");
    QCOMPARE(preview->mode(), StickerPreview::Mode::Rectangle);
    drag(preview, at(0.2, 0.2), at(0.5, 0.3));
    QCOMPARE(int(dialog.document().drawnShapes().size()), before + 1);
    QCOMPARE(preview->selection(), QStringList{last().id});
    const QRectF drawn = last().outline.boundingRect();
    QVERIFY(almost(drawn, QRectF(preview->toDocument(at(0.2, 0.2)), preview->toDocument(at(0.5, 0.3)))));
    QCOMPARE(dialog.document().shapeStyle(last().id).fill, QColor(QStringLiteral("#4a90d9")));
    drag(preview, at(0.2, 0.2), at(0.5, 0.3), Qt::ShiftModifier);
    QVERIFY(std::abs(last().outline.boundingRect().width() - last().outline.boundingRect().height()) < box.width() / 100);
    QTest::mouseClick(preview, Qt::LeftButton, {}, at(0.6, 0.6));  // a click alone draws nothing
    QCOMPARE(int(dialog.document().drawnShapes().size()), before + 2);

    // Ellipse with Shift: a circle. Line with Shift: 45°.
    tool("StickerEllipseTool");
    drag(preview, at(0.1, 0.6), at(0.3, 0.7), Qt::ShiftModifier);
    QVERIFY(std::abs(last().outline.boundingRect().width() - last().outline.boundingRect().height()) < box.width() / 100);
    tool("StickerLineTool");
    drag(preview, at(0.5, 0.5), at(0.8, 0.75), Qt::ShiftModifier);  // about 40°: 45°
    const QRectF line = last().outline.boundingRect();
    QVERIFY(std::abs(line.width() - line.height()) < box.width() / 100);  // diagonal

    // Polygon and star: their corners.
    tool("StickerPolygonTool");
    choose(dialog.findChild<NumberField *>(QStringLiteral("StickerSides")), 6);
    drag(preview, at(0.5, 0.5), at(0.6, 0.5));
    QCOMPARE(dialog.document().path(last().id).first().segments.size(), 5);  // 6 corners, the last line by "Z"
    tool("StickerStarTool");
    choose(dialog.findChild<NumberField *>(QStringLiteral("StickerPoints")), 4);
    drag(preview, at(0.5, 0.5), at(0.6, 0.5), Qt::ShiftModifier);
    QCOMPARE(dialog.document().path(last().id).first().segments.size(), 7);  // 8 corners
    QCOMPARE(int(dialog.document().drawnShapes().size()), before + 6);
    dialog.undo();  // each shape is one step
    QCOMPARE(int(dialog.document().drawnShapes().size()), before + 5);

    // Pen: two corners, then a drag for a curve; a double click ends an open path.
    tool("StickerPenTool");
    QTest::mouseClick(preview, Qt::LeftButton, {}, at(0.1, 0.1));
    QTest::mouseClick(preview, Qt::LeftButton, {}, at(0.3, 0.1));
    drag(preview, at(0.3, 0.3), at(0.4, 0.35));
    QVERIFY(preview->penActive());
    QTest::mouseDClick(preview, Qt::LeftButton, {}, at(0.3, 0.3));
    QVERIFY(!preview->penActive());
    QList<SvgDocument::SubPath> pen = dialog.document().path(last().id);
    QCOMPARE(pen.size(), 1);
    QCOMPARE(pen.first().segments.size(), 2);
    QVERIFY(!pen.first().segments[0].curve);
    QVERIFY(pen.first().segments[1].curve);
    QVERIFY(!pen.first().closed);
    // A click on the first point closes; Esc drops a path being drawn.
    QTest::mouseClick(preview, Qt::LeftButton, {}, at(0.6, 0.1));
    QTest::mouseClick(preview, Qt::LeftButton, {}, at(0.8, 0.1));
    QTest::mouseClick(preview, Qt::LeftButton, {}, at(0.7, 0.3));
    QTest::mouseClick(preview, Qt::LeftButton, {}, at(0.6, 0.1));
    pen = dialog.document().path(last().id);
    QVERIFY(pen.first().closed);
    const int count = int(dialog.document().drawnShapes().size());
    QTest::mouseClick(preview, Qt::LeftButton, {}, at(0.1, 0.8));
    QTest::mouseClick(preview, Qt::LeftButton, {}, at(0.2, 0.8));
    preview->setFocus();
    QTest::keyClick(preview, Qt::Key_Escape);
    QVERIFY(!preview->penActive());
    QVERIFY(dialog.isVisible());
    QCOMPARE(int(dialog.document().drawnShapes().size()), count);

    // The style row on the selected shape: no fill, then an outline of 4 px on the 256 px sticker.
    const QString id = last().id;
    QCOMPARE(preview->selection(), QStringList{id});
    emit dialog.findChild<SegmentedControl *>(QStringLiteral("StickerFillKind"))->segmentActivated(1);  // None
    QVERIFY(!dialog.document().shapeStyle(id).fill.isValid());
    choose(dialog.findChild<NumberField *>(QStringLiteral("StickerStrokeWidth")), 4);
    const SvgDocument::ShapeStyle style = dialog.document().shapeStyle(id);
    QVERIFY(style.stroke.isValid());
    QVERIFY(std::abs(style.strokeWidth - 4 * qMax(box.width(), box.height()) / SvgDocument::kStickerPx) < 0.01);
    dialog.confirmDiscard = [] { return true; };
    dialog.reject();
}

void StickersMarkersTest::collectionsAreEditable() {
    IconPage *page = m_window->showIconPanel(1);
    page->askName = [](const QString &, const QString &) { return QStringLiteral("Viaggi"); };
    page->newCollection();
    QString travel;
    for (const IconLibrary::Collection &c : IconLibrary::collections(IconLibrary::Kind::Sticker))
        if (c.name == QStringLiteral("Viaggi")) travel = c.id;
    QVERIFY(!travel.isEmpty());
    QCOMPARE(page->shownCollections().back(), travel);
    page->askName = [](const QString &, const QString &) { return QStringLiteral("Vacanze"); };
    page->renameCollection(travel);
    QCOMPARE(IconLibrary::collection(IconLibrary::Kind::Sticker, travel).name, QStringLiteral("Vacanze"));
    // Elementi di base spostati nella raccolta propria, il secondo prima del primo.
    page->dropItem(QStringLiteral(":/stickers/objects/rocket.svg"), travel, -1);
    page->dropItem(QStringLiteral(":/stickers/objects/camera.svg"), travel, 0);
    IconLibrary::Collection moved = IconLibrary::collection(IconLibrary::Kind::Sticker, travel);
    QCOMPARE(moved.items.size(), 2);
    QCOMPARE(moved.items[0].path, QStringLiteral(":/stickers/objects/camera.svg"));
    QCOMPARE(moved.items[1].path, QStringLiteral(":/stickers/objects/rocket.svg"));
    QCOMPARE(IconLibrary::collection(IconLibrary::Kind::Sticker, QStringLiteral("builtin:objects")).items.size(), 18);
    // Riordino nella stessa raccolta (in fondo).
    page->dropItem(QStringLiteral(":/stickers/objects/camera.svg"), travel, 2);
    QCOMPARE(IconLibrary::collection(IconLibrary::Kind::Sticker, travel).items[1].path, QStringLiteral(":/stickers/objects/camera.svg"));
    // Raccolta in cima.
    page->dropCollection(travel, page->shownCollections().front());
    QCOMPARE(page->shownCollections().front(), travel);
    // Di base: nascosta (non eliminata), poi ripristinata.
    page->removeCollection(QStringLiteral("builtin:objects"));
    QVERIFY(!page->shownCollections().contains(QStringLiteral("builtin:objects")));
    page->restoreBuiltIns();
    QVERIFY(page->shownCollections().contains(QStringLiteral("builtin:objects")));
    // Cartella importata: una raccolta col suo nome, solo i file validi.
    QDir(m_dir.path()).mkpath(QStringLiteral("Animali"));
    const QString folder = m_dir.filePath(QStringLiteral("Animali"));
    QFile::copy(png(QStringLiteral("cane.png"), Qt::yellow), folder + QStringLiteral("/cane.png"));
    QFile::copy(svg(QStringLiteral("gatto.svg"), QStringLiteral("orange")), folder + QStringLiteral("/gatto.svg"));
    QFile broken(folder + QStringLiteral("/rotto.svg"));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("<svg");
    broken.close();
    page->pickFolder = [folder] { return folder; };
    page->importFolder();
    QString animals;
    for (const IconLibrary::Collection &c : IconLibrary::collections(IconLibrary::Kind::Sticker))
        if (c.name == QStringLiteral("Animali")) { animals = c.id; QCOMPARE(c.items.size(), 2); }
    QVERIFY(!animals.isEmpty());
    // Propria eliminata: gli elementi di base tornano nella loro raccolta.
    page->removeCollection(travel);
    QVERIFY(!page->shownCollections().contains(travel));
    QCOMPARE(IconLibrary::collection(IconLibrary::Kind::Sticker, QStringLiteral("builtin:objects")).items.size(), 20);
}

// Tappa 3: the search field filters by the item's and the collection's name; adding a file asks its
// name (Cancel skips it) and own items can be renamed.
void StickersMarkersTest::searchAndNames() {
    IconPage *page = m_window->showIconPanel(1);
    QLineEdit *search = page->searchField();
    QVERIFY(search);
    const qsizetype all = page->shownPaths().size();
    page->setCollapsed(QStringLiteral("builtin:animals"), true);
    QCoreApplication::processEvents();
    search->setText(QStringLiteral("PENGUIN"));
    QTRY_COMPARE(page->shownPaths().size(), 2);  // after the pause in the typing
    QStringList names;
    for (const QString &path : page->shownPaths()) names.append(IconLibrary::displayName(path));
    QCOMPARE(names, QStringList({QStringLiteral("penguin"), QStringLiteral("penguin")}));  // animals (folded) and cute animals
    search->setText(QStringLiteral("thinking"));  // the collection's name: all its items
    QTRY_COMPARE(page->shownPaths().size(), 11);  // and the mascot's "thinking" sticker
    QVERIFY(page->shownCollections().contains(QStringLiteral("builtin:thinking")));
    QCOMPARE(IconLibrary::collection(IconLibrary::Kind::Sticker, QStringLiteral("builtin:thinking")).items.size(), 10);
    QVERIFY(page->shownPaths().contains(QStringLiteral(":/stickers/thinking/shrugging.svg")));
    search->setText(QStringLiteral("zzzz"));
    QTRY_VERIFY(page->shownPaths().isEmpty());
    QVERIFY(page->findChild<QLabel *>(QStringLiteral("IconNoResult")));
    search->clear();
    page->setCollapsed(QStringLiteral("builtin:animals"), false);
    QCoreApplication::processEvents();
    QCOMPARE(page->shownPaths().size(), all);

    // Cancel skips the file; a name given becomes the item's name, then Rename changes it.
    const QString source = png(QStringLiteral("blu.png"), Qt::blue);
    page->pickFiles = [source] { return QStringList{source}; };
    page->askName = [](const QString &, const QString &) { return QString(); };
    page->addFiles({});
    QCOMPARE(page->shownPaths().size(), all);
    page->askName = [](const QString &, const QString &) { return QStringLiteral("Blue square"); };
    page->addFiles({});
    QString added;
    for (const QString &path : page->shownPaths())
        if (IconLibrary::displayName(path) == QLatin1String("Blue square")) added = path;
    QVERIFY(!added.isEmpty());
    page->askName = [](const QString &, const QString &) { return QStringLiteral("Navy square"); };
    page->renameItem(added);
    search->setText(QStringLiteral("navy"));
    QTRY_COMPARE(page->shownPaths().size(), 1);
    QCOMPARE(IconLibrary::displayName(page->shownPaths().front()), QStringLiteral("Navy square"));
    search->clear();
}

// Marcatori propri da un file: gruppo proprio come serie, immagine nel documento.
void StickersMarkersTest::customMarkerFromAFile() {
    MindNode *node = addNode(QStringLiteral("Marcatore proprio"));
    m_scene->selectNode(node);
    IconPage *page = m_window->showIconPanel(0);
    const QString source = svg(QStringLiteral("cuore.svg"), QStringLiteral("crimson"));
    page->pickFiles = [source] { return QStringList{source}; };
    page->askName = [](const QString &, const QString &current) { return current; };
    page->addFiles({});
    QCOMPARE(node->markers.size(), 1);
    const QString marker = node->markers.front();
    QVERIFY(Markers::isCustom(marker));
    QVERIFY(!Markers::image(Markers::customImage(marker)).isEmpty());
    QCOMPARE(itemFor(node)->markerRects().size(), 1);
    // Insieme a uno disegnato; nel file salvato l'immagine viaggia con il documento.
    m_window->setMarker(node, QStringLiteral("flag"), QStringLiteral("flag:red"));
    QCOMPARE(node->markers.size(), 2);
    const QJsonObject json = m_model->toJson();
    QVERIFY(json.value(QStringLiteral("markerImages")).toObject().contains(Markers::customImage(marker)));
    QVERIFY(page->cell(page->shownPaths().back())->property("active").toBool());
    // Lo stesso clic lo toglie.
    clickCell(0, page->shownPaths().back());
    QCOMPARE(node->markers, QStringList{QStringLiteral("flag:red")});
}

// Richiesta dell'utente 2026-10-20: the columns fill the panel's width. Since FEAT-072 B1
// (2026-10-22) the panel has a fixed width and cannot be widened.
void StickersMarkersTest::gridColumnsFollowThePanel() {
    IconPage *page = m_window->showIconPanel(1);
    auto *dock = m_window->findChild<QDockWidget *>(QStringLiteral("InspectorDock"));
    QVERIFY(dock);
    QTest::qWait(100);
    const int width = dock->width();
    const auto fitting = [page] {
        return std::max(IconPage::kStickerColumns, (page->width() + IconPage::kCellSpacingPx) / (IconPage::kStickerCellPx + IconPage::kCellSpacingPx));
    };
    QCOMPARE(page->columns(), fitting());
    const int base = page->columns();
    m_window->resizeDocks({dock}, {width + 3 * (IconPage::kStickerCellPx + IconPage::kCellSpacingPx)}, Qt::Horizontal);
    QTest::qWait(100);
    QCOMPARE(dock->width(), width);
    QCOMPARE(page->columns(), base);
    // Le celle stanno nella pagina: nessuna esce a destra.
    for (const QString &path : page->shownPaths().mid(0, page->columns()))
        QVERIFY(page->cell(path)->mapTo(page, QPoint(page->cell(path)->width(), 0)).x() <= page->width());
}

// Bug 2026-10-20 (foto dell'utente): il testo delle domande che va a capo restava tagliato.
void StickersMarkersTest::dialogTextIsNotCut() {
    ModalDialog dialog(m_window.get(), QStringLiteral("Delete collection"), QStringLiteral("Delete"), 420);
    auto *message = new QLabel(QStringLiteral("Delete \"Prova\" and its 7 added files? Maps that already use them keep their copy."));
    message->setWordWrap(true);
    dialog.contentLayout()->addWidget(message);
    dialog.show();
    QTest::qWait(100);
    QVERIFY(message->height() >= message->heightForWidth(message->width()));
    QVERIFY(message->height() > message->fontMetrics().lineSpacing() * 3 / 2);  // almeno due righe
    dialog.close();
}

// BUG-029: clicking a sticker in the panel moved the map, more and more with repeated clicks; a
// click on the sticker the topic already has changes nothing (no history step).
void StickersMarkersTest::stickerClicksDoNotMoveTheMap() {
    MindNode *child = addNode(QStringLiteral("Adesivo fermo"));
    MindNode *root = m_model->root();
    m_scene->selectNode(child);
    m_view->centerOn(itemFor(root));
    QTest::qWait(100);
    const QString camera = QStringLiteral(":/stickers/objects/camera.svg");
    m_window->showIconPanel(1);
    QTest::qWait(400);  // the panel opening animation
    const QPointF rootFirst = m_view->mapFromScene(itemFor(root)->scenePos());
    clickCell(1, camera);
    flushHistory();
    QTest::qWait(100);
    QCOMPARE(child->imagePath, camera);
    QCOMPARE(QPointF(m_view->mapFromScene(itemFor(root)->scenePos())), rootFirst);  // the central topic stays on screen
    const QPointF rootBefore = root->position, childBefore = child->position;
    const QPointF viewBefore = m_view->mapToScene(QPoint(0, 0));
    const QPointF rootOnScreen = m_view->mapFromScene(itemFor(root)->scenePos());
    const bool couldUndo = m_window->findChild<Document *>()->canUndo();
    for (int i = 0; i < 6; ++i) {
        clickCell(1, camera);
        QTest::qWait(20);
    }
    flushHistory();
    QCOMPARE(root->position, rootBefore);
    QCOMPARE(child->position, childBefore);
    QCOMPARE(m_view->mapToScene(QPoint(0, 0)), viewBefore);
    QCOMPARE(QPointF(m_view->mapFromScene(itemFor(root)->scenePos())), rootOnScreen);
    QCOMPARE(m_window->findChild<Document *>()->canUndo(), couldUndo);
    undo();  // one step for the first click only
    QVERIFY(child->imagePath.isEmpty());
}

// Bug 2026-10-20 (segnalato dall'utente): cliccando un nodo la mappa si spostava di poco. Un clic
// vero non e' fermo: fra pressione e rilascio la mano si muove di 1-2 px, e Qt spostava il nodo
// (dal centrale tutta la mappa). Sotto la soglia di trascinamento il nodo non si muove.
void StickersMarkersTest::clicksDoNotMoveTheMap() {
    MindNode *child = addNode(QStringLiteral("Clic fermi"));
    m_window->setMarker(child, QStringLiteral("priority"), QStringLiteral("priority:2"));
    MindNode *root = m_model->root();
    m_view->centerOn(itemFor(root));
    QTest::qWait(200);
    const auto shakyClick = [this](const QPoint &at) {
        QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, at);
        for (const QPoint step : {QPoint(1, 0), QPoint(1, 1), QPoint(2, 1)}) {
            QMouseEvent move(QEvent::MouseMove, QPointF(at + step), m_view->viewport()->mapToGlobal(QPointF(at + step)),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(m_view->viewport(), &move);
        }
        QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, at + QPoint(2, 1));
        QTest::qWait(40);
    };
    const QPointF rootBefore = root->position, childBefore = child->position;
    const QPointF viewBefore = m_view->mapToScene(QPoint(0, 0));
    for (int i = 0; i < 5; ++i) {
        shakyClick(m_view->mapFromScene(itemFor(root)->mapToScene(itemFor(root)->boundingRect().center())));
        shakyClick(m_view->mapFromScene(itemFor(child)->mapToScene(itemFor(child)->boundingRect().center())));
        shakyClick(m_view->mapFromScene(itemFor(child)->mapToScene(itemFor(child)->markerRects().front().center())));
    }
    QApplication::processEvents();
    QCOMPARE(root->position, rootBefore);
    QCOMPARE(child->position, childBefore);
    QCOMPARE(m_view->mapToScene(QPoint(0, 0)), viewBefore);
    // Oltre la soglia il nodo si trascina ancora, della distanza intera (a mano: con il bilanciamento
    // il trascinamento riordina, FEAT-015).
    m_window->setBalanced(false);
    const QPoint start = m_view->mapFromScene(itemFor(child)->mapToScene(itemFor(child)->boundingRect().center()));
    QTest::mousePress(m_view->viewport(), Qt::LeftButton, {}, start);
    for (int step = 1; step <= 4; ++step) {
        const QPoint at = start + QPoint(step * 10, 0);
        QMouseEvent move(QEvent::MouseMove, QPointF(at), m_view->viewport()->mapToGlobal(QPointF(at)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(m_view->viewport(), &move);
    }
    QTest::mouseRelease(m_view->viewport(), Qt::LeftButton, {}, start + QPoint(40, 0));
    QApplication::processEvents();
    QCOMPARE(child->position, childBefore + QPointF(40, 0));
    undo();
    m_window->setBalanced(true);
}

// Bug 2026-10-20 (foto dell'utente): con il doppio clic il campo del titolo finiva al centro del
// nodo, sopra l'adesivo o l'immagine; deve stare dove si disegna il titolo, in ogni posizione.
void StickersMarkersTest::editingStaysInTheTitlePlace() {
    using Placement = MindNode::ImagePlacement;
    const QString photo = png(QStringLiteral("posto.png"), Qt::darkCyan, {300, 200});
    for (const bool sticker : {true, false}) {
        for (const Placement placement : {Placement::Above, Placement::Below, Placement::Left, Placement::Right}) {
            MindNode *node = addNode(QStringLiteral("Titolo in modifica"));
            if (sticker) QVERIFY(m_window->insertSticker(node, QStringLiteral(":/stickers/objects/rocket.svg")));
            else QVERIFY(m_window->findChild<Document *>()->setImage(node, photo, {150, 100}));
            node->imagePlacement = placement;
            m_scene->updateNode(node);
            m_scene->autoLayout();
            NodeItem *item = itemFor(node);
            const QRectF title = item->titleArea();
            item->beginEditing();
            QApplication::processEvents();
            QVERIFY(item->isEditing());
            const QRectF field = item->titleEditor()->mapRectToParent(item->titleEditor()->boundingRect());
            const QString where = QStringLiteral("%1 %2: campo %3,%4 %5x%6, titolo %7,%8 %9x%10")
                                      .arg(sticker ? QStringLiteral("adesivo") : QStringLiteral("immagine")).arg(int(placement))
                                      .arg(field.x()).arg(field.y()).arg(field.width()).arg(field.height())
                                      .arg(title.x()).arg(title.y()).arg(title.width()).arg(title.height());
            QVERIFY2(!field.intersects(item->pictureRect()), qPrintable(where));
            QVERIFY2(std::abs(field.center().y() - item->titleArea().center().y()) < 1.0, qPrintable(where));
            QTest::keyClick(m_view->viewport(), Qt::Key_Escape);
            QApplication::processEvents();
            if (item->isEditing()) m_scene->clearSelection();
        }
    }
}

// Richiesta dell'utente 2026-10-20 ("una via di mezzo"): un'immagine nuova e' larga quanto il posto
// del nodo ma almeno 200 px, in proporzione; una piccola resta com'e'.
void StickersMarkersTest::newImageIsAsWideAsTheNode() {
    MindNode *node = addNode(QStringLiteral("Foto di oggi"));
    const qreal widthBefore = m_scene->nodeWidth(node);
    const qreal room = std::floor(m_scene->imageRoomWidth(node));
    QVERIFY(room > 40 && room < MainWindow::kMinInsertWidthPx);  // nodo stretto: vale il minimo
    const QString wide = png(QStringLiteral("larga.png"), Qt::darkBlue, {800, 400});
    m_window->setImageFilePicker([wide] { return wide; });
    m_scene->selectNode(node);
    QApplication::processEvents();
    m_window->chooseImage();
    QApplication::processEvents();
    QCOMPARE(node->imageSize, QSizeF(200, 100));
    QVERIFY(m_scene->nodeWidth(node) > widthBefore);  // il nodo si allarga quanto serve ai 200 px
    // Nodo largo (larghezza a mano): l'immagine prende la sua larghezza.
    MindNode *wideNode = addNode(QStringLiteral("Argomento largo"));
    wideNode->customWidth = 500;
    m_scene->updateNode(wideNode);
    const qreal wideRoom = std::floor(m_scene->imageRoomWidth(wideNode));
    QVERIFY(wideRoom > 400);
    m_scene->selectNode(wideNode);
    QApplication::processEvents();
    m_window->chooseImage();
    QApplication::processEvents();
    QCOMPARE(wideNode->imageSize, QSizeF(wideRoom, std::round(wideRoom / 2)));
    QCOMPARE(wideNode->customWidth, 500.0);
    // "Ripristina dimensione" resta la misura del file (si allarga a mano).
    QCOMPARE(NodeItem::insertionImageSize(wide), QSizeF(800, 400));
    MindNode *other = addNode(QStringLiteral("Argomento con un'icona"));
    const QString small = png(QStringLiteral("piccola.png"), Qt::darkRed, {40, 30});
    m_window->setImageFilePicker([small] { return small; });
    m_scene->selectNode(other);
    QApplication::processEvents();
    m_window->chooseImage();
    QApplication::processEvents();
    QCOMPARE(other->imageSize, QSizeF(40, 30));
    m_window->setImageFilePicker(nullptr);
}

// FEAT-065 (decisioni dell'utente 2026-10-19, foto di riferimento 2026-10-20): nell'outliner marcatori a
// sinistra del titolo, immagine o adesivo sotto (misura della mappa), collegamenti e file dopo il
// titolo; con lo stesso clic e lo stesso tasto destro della mappa.
void StickersMarkersTest::outlinerShowsTheContents() {
    MindNode *node = addNode(QStringLiteral("Contenuti nell'outliner"));
    MindNode *plain = addNode(QStringLiteral("Solo testo"));
    m_window->setMarker(node, QStringLiteral("priority"), QStringLiteral("priority:1"));
    QVERIFY(m_window->insertSticker(node, QStringLiteral(":/stickers/food/beer.svg")));
    node->webLink = QStringLiteral("https://example.org");
    QVERIFY(m_window->findChild<Document *>()->addFiles(node, {png(QStringLiteral("allegato.png"), Qt::gray)}));
    m_scene->updateNode(node);
    QPushButton *outliner = nullptr;
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner")) outliner = button;
    QVERIFY(outliner);
    outliner->click();
    QApplication::processEvents();
    QTreeView *tree = nullptr;
    for (auto *candidate : m_window->findChildren<QTreeView *>())
        if (candidate->isVisible() && candidate->model()) tree = candidate;
    QVERIFY(tree);
    tree->doItemsLayout();
    QTest::qWait(50);
    // Immagine sotto il titolo, nella riga (che si allunga), con la stessa misura della mappa
    // (decisione dell'utente 2026-10-20, zoom dell'outliner al 100%).
    const QRect picture = m_window->outlinePictureRect(node);
    QVERIFY(!picture.isEmpty());
    QCOMPARE(QSizeF(picture.size()), node->imageSize);
    const QRect row = tree->visualRect(m_model->indexFromNode(node));
    QVERIFY(row.contains(picture));
    QVERIFY(row.height() > tree->visualRect(m_model->indexFromNode(plain)).height() + picture.height() / 2);
    QVERIFY(m_window->outlinePictureRect(plain).isEmpty());
    // Marcatore a sinistra del titolo; clic = valore seguente, come sulla mappa.
    const QRect marker = m_window->outlineMarkerRect(node, 0);
    QVERIFY(!marker.isEmpty() && marker.right() < picture.left() + 40);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, marker.center());
    QApplication::processEvents();
    QCOMPARE(node->markers, QStringList{QStringLiteral("priority:2")});
    // Collegamento e graffetta dopo il titolo.
    QVERIFY(!m_window->outlineIconRect(node, 0).isEmpty());
    QVERIFY(!m_window->outlineIconRect(node, 1).isEmpty());
    QVERIFY(m_window->outlineIconRect(node, 1).left() > m_window->outlineIconRect(node, 0).left());
    QVERIFY(m_window->outlineIconRect(plain, 0).isEmpty());
    // Clic sull'immagine: scelta con le maniglie; la maniglia in basso a destra la ingrandisce
    // (misura nuova anche sulla mappa, un passo); Canc toglie solo l'adesivo.
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, picture.center());
    QApplication::processEvents();
    QVERIFY(m_window->outlinePictureSelected(node));
    const QSizeF before = node->imageSize;
    const QPoint corner = m_window->outlinePictureRect(node).bottomRight();
    QTest::mousePress(tree->viewport(), Qt::LeftButton, {}, corner);
    for (int step = 1; step <= 4; ++step) {
        const QPoint at = corner - QPoint(step * 10, 0);
        QMouseEvent move(QEvent::MouseMove, QPointF(at), tree->viewport()->mapToGlobal(QPointF(at)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &move);
    }
    QTest::mouseRelease(tree->viewport(), Qt::LeftButton, {}, corner - QPoint(40, 0));
    QApplication::processEvents();
    QVERIFY2(node->imageSize.width() < before.width(), qPrintable(QString::number(node->imageSize.width())));
    QCOMPARE(QSizeF(m_window->outlinePictureRect(node).size()), node->imageSize);  // stessa misura dopo
    QCOMPARE(node->imageSize.width() / node->imageSize.height(), before.width() / before.height());
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, m_window->outlinePictureRect(node).center());
    QApplication::processEvents();
    QTest::keyClick(tree, Qt::Key_Delete);
    QApplication::processEvents();
    QVERIFY(node->imagePath.isEmpty());
    QVERIFY(m_model->indexFromNode(node).isValid());  // il nodo resta
    QVERIFY(m_window->outlinePictureRect(node).isEmpty());
    outliner->click();
    QApplication::processEvents();
}

// Domanda dell'utente 2026-10-20: immagine, adesivo e formula scelti con un clic si tolgono con il
// tasto Canc vero (non solo con l'azione), come con il tasto destro > Rimuovi; il nodo resta.
void StickersMarkersTest::deleteKeyRemovesEveryVisual() {
    // Le scorciatoie arrivano solo alla finestra attiva (una modale chiusa da una prova precedente
    // la lascia senza attivazione).
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    Document *document = m_window->findChild<Document *>();
    for (int kind = 0; kind < 3; ++kind) {
        MindNode *node = addNode(QStringLiteral("Canc vero %1").arg(kind));
        if (kind == 0) QVERIFY(document->setImage(node, png(QStringLiteral("canc.png"), Qt::darkGreen, {300, 150}), {200, 100}));
        if (kind == 1) QVERIFY(m_window->insertSticker(node, QStringLiteral(":/stickers/objects/rocket.svg")));
        if (kind == 2) QVERIFY(document->setFormula(node, QStringLiteral("x^2 + y^2"), Qt::white));
        m_scene->updateNode(node);
        m_scene->autoLayout();
        NodeItem *item = itemFor(node);
        m_view->centerOn(item);
        QApplication::processEvents();
        const QPoint at = m_view->mapFromScene(item->mapToScene(item->pictureRect().center()));
        QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, at);
        QApplication::processEvents();
        QCOMPARE(m_scene->selectedImageNode(), node);
        const qsizetype nodes = m_model->root()->children.size();
        QTest::keyClick(m_view->viewport(), Qt::Key_Delete);
        QApplication::processEvents();
        QVERIFY2(node->imagePath.isEmpty() && node->formula.isEmpty(), qPrintable(QString::number(kind)));
        QCOMPARE(m_model->root()->children.size(), nodes);  // il nodo resta
    }
}

// Richiesta dell'utente 2026-10-20: tasto destro e pagina Stile ruotano di 90 gradi (senso orario)
// immagini, adesivi e formule; la misura si scambia, un passo, salvata nel file.
void StickersMarkersTest::rotateBy90Degrees() {
    MindNode *node = addNode(QStringLiteral("Da ruotare"));
    QImage halves(300, 150, QImage::Format_ARGB32);
    halves.fill(Qt::blue);
    {
        QPainter painter(&halves);
        painter.fillRect(0, 0, 150, 150, Qt::red);  // meta' sinistra rossa
    }
    const QString path = m_dir.filePath(QStringLiteral("meta.png"));
    QVERIFY(halves.save(path));
    QVERIFY(m_window->findChild<Document *>()->setImage(node, path, {200, 100}));
    m_scene->updateNode(node);
    m_scene->autoLayout();
    flushHistory();
    NodeItem *item = itemFor(node);
    item->selectImage();
    m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->click();
    QTest::qWait(300);
    auto *page = m_window->findChild<ImageStylePage *>();
    QVERIFY(page->rotateButton()->isEnabled());
    page->rotateButton()->click();
    QApplication::processEvents();
    QCOMPARE(node->imageStyle.rotation, 90);
    QCOMPARE(node->imageSize, QSizeF(100, 200));
    // In senso orario la meta' sinistra (rossa) va in alto.
    const QImage turned = item->picture().toImage();
    QVERIFY(turned.height() > turned.width());
    QCOMPARE(turned.pixelColor(turned.width() / 2, turned.height() / 4).red(), 255);
    QCOMPARE(turned.pixelColor(turned.width() / 2, turned.height() * 3 / 4).blue(), 255);
    QVERIFY(QJsonDocument(m_model->toJson()).toJson().contains("\"imageRotation\": 90"));
    // "Ripristina dimensione" tiene la rotazione (misura reale scambiata); "Ripristina stile" pure.
    page->resetSizeButton()->click();
    QApplication::processEvents();
    QCOMPARE(node->imageSize, QSizeF(150, 300));
    page->resetStyleButton()->click();
    QApplication::processEvents();
    QCOMPARE(node->imageStyle.rotation, 90);
    flushHistory();
    // Tasto destro > Ruota di 90 gradi, tre volte: si torna diritti e alla misura di prima scambiata due volte.
    for (int i = 0; i < 3; ++i) {
        const QPoint at = m_view->mapFromScene(item->mapToScene(item->pictureRect().center()));
        chooseInNextMenu(QStringLiteral("Rotate 90°"));
        QContextMenuEvent event(QContextMenuEvent::Mouse, at, m_view->viewport()->mapToGlobal(at));
        QApplication::sendEvent(m_view->viewport(), &event);
        QApplication::processEvents();
    }
    QCOMPARE(node->imageStyle.rotation, 0);
    QCOMPARE(node->imageSize, QSizeF(300, 150));
    flushHistory();
    undo();  // un passo per rotazione
    QCOMPARE(node->imageStyle.rotation, 270);
    QCOMPARE(node->imageSize, QSizeF(150, 300));
    // Anche un adesivo; una nuova immagine riparte diritta.
    MindNode *sticker = addNode(QStringLiteral("Adesivo ruotato"));
    QVERIFY(m_window->insertSticker(sticker, QStringLiteral(":/stickers/objects/rocket.svg")));
    m_window->rotateVisual(sticker);
    QCOMPARE(sticker->imageStyle.rotation, 90);
    QVERIFY(m_window->insertSticker(sticker, QStringLiteral(":/stickers/objects/camera.svg")));
    QCOMPARE(sticker->imageStyle.rotation, 0);
}

// BUG-020 (foto di riferimento dell'utente): un nodo puo' restare senza testo; resta un quadrato alto
// quanto un nodo di una riga. Un passo di storico; nell'outliner lo stesso.
void StickersMarkersTest::emptyNodeIsASmallSquare() {
    MindNode *node = addNode(QStringLiteral("Da svuotare"));
    MindNode *oneLine = addNode(QStringLiteral("Una riga"));
    NodeItem *item = itemFor(node);
    const qreal lineHeight = item->mapRectToScene(item->shapeRect()).height();
    m_scene->selectNode(node);
    item->beginEditing();  // tutto il testo selezionato
    QApplication::processEvents();
    QTest::keyClick(m_view->viewport(), Qt::Key_Delete);
    QTest::keyClick(m_view->viewport(), Qt::Key_Return);
    QApplication::processEvents();
    QVERIFY(!item->isEditing());
    QCOMPARE(node->title, QString());
    QVERIFY(m_model->indexFromNode(node).isValid());
    const QRectF shape = item->mapRectToScene(item->shapeRect());
    QVERIFY2(std::abs(shape.height() - lineHeight) < 1.0, qPrintable(QStringLiteral("%1 vs %2").arg(shape.height()).arg(lineHeight)));
    QVERIFY2(std::abs(shape.width() - shape.height()) < 1.0, qPrintable(QStringLiteral("%1x%2").arg(shape.width()).arg(shape.height())));
    QVERIFY(shape.width() < itemFor(oneLine)->mapRectToScene(itemFor(oneLine)->shapeRect()).width());
    flushHistory();
    undo();
    QCOMPARE(node->title, QStringLiteral("Da svuotare"));
    // Si riapre e si riscrive come prima.
    m_window->findChild<Document *>()->setTitle(node, QString());
    m_scene->updateNode(node);
    item->beginEditing();
    QTest::keyClicks(m_view->viewport(), QStringLiteral("Di nuovo"));
    QTest::keyClick(m_view->viewport(), Qt::Key_Return);
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("Di nuovo"));
    // Outliner: titolo cancellato e confermato, il nodo resta vuoto.
    QPushButton *outliner = nullptr;
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner")) outliner = button;
    outliner->click();
    QApplication::processEvents();
    QTreeView *tree = nullptr;
    for (auto *candidate : m_window->findChildren<QTreeView *>())
        if (candidate->isVisible() && candidate->model()) tree = candidate;
    QVERIFY(tree);
    const QModelIndex index = m_model->indexFromNode(node);
    tree->setCurrentIndex(index);
    tree->edit(index);
    QApplication::processEvents();
    QWidget *field = tree->indexWidget(index);
    QVERIFY(field);
    QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(field, Qt::Key_Backspace);
    QTest::keyClick(field, Qt::Key_Return);
    QApplication::processEvents();
    QCOMPARE(node->title, QString());
    QVERIFY(m_model->indexFromNode(node).isValid());
    outliner->click();
    QApplication::processEvents();
}

// Controlli dei formati, uguali per marcatori e adesivi.
void StickersMarkersTest::fileChecks() {
    using IconLibrary::Kind;
    QVERIFY(IconLibrary::checkFile(Kind::Marker, svg(QStringLiteral("ok.svg"), QStringLiteral("green"))).ok);
    QFile broken(m_dir.filePath(QStringLiteral("rotto2.svg")));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("<svg xmlns=\"http://www.w3.org/2000/svg\"><circle");
    broken.close();
    QVERIFY(!IconLibrary::checkFile(Kind::Sticker, broken.fileName()).ok);
    // PNG piccolo: accettato con l'avviso (sotto 64 px per i marcatori, 256 per gli adesivi).
    const QString small = png(QStringLiteral("piccolo.png"), Qt::red, {100, 80});
    const IconLibrary::Check marker = IconLibrary::checkFile(Kind::Marker, small);
    QVERIFY(marker.ok && !marker.small);
    const IconLibrary::Check sticker = IconLibrary::checkFile(Kind::Sticker, small);
    QVERIFY(sticker.ok && sticker.small && !sticker.smallReason.isEmpty());
    // Oltre 2 MB: rifiutato.
    QFile big(m_dir.filePath(QStringLiteral("grande.png")));
    QVERIFY(big.open(QIODevice::WriteOnly));
    big.write(QByteArray(int(IconLibrary::kMaxFileBytes) + 1, 'x'));
    big.close();
    QVERIFY(!IconLibrary::checkFile(Kind::Sticker, big.fileName()).ok);
    QVERIFY(IconLibrary::fileFilter().contains(QStringLiteral("*.svg")));
}

void StickersMarkersTest::stickerImageAndFormulaShareThePlace() {
    MindNode *node = addNode(QStringLiteral("Un posto solo"));
    QVERIFY(m_window->insertSticker(node, png(QStringLiteral("a.png"), Qt::green)));
    QVERIFY(node->imageSticker);
    Document *document = m_window->findChild<Document *>();
    QVERIFY(document->setFormula(node, QStringLiteral("x^2"), Qt::white, QSizeF(80, 40)));
    QVERIFY(node->imagePath.isEmpty());
    QVERIFY(m_window->insertSticker(node, png(QStringLiteral("b.png"), Qt::yellow)));
    QVERIFY(node->formula.isEmpty());
    QVERIFY(document->setImage(node, png(QStringLiteral("c.png"), Qt::gray), QSizeF(80, 60)));
    QVERIFY(!node->imageSticker);  // un'immagine normale non e' un adesivo
}

// Bug 2026-10-21 (segnalato dall'utente): le pagine Marcatore e Adesivo avevano un solo scorrimento,
// quindi i Marcatori erano alti quanto gli Adesivi e scorrevano con loro.
void StickersMarkersTest::iconPagesScrollOnTheirOwn() {
    m_window->showIconPanel(1);
    QApplication::processEvents();
    auto *markers = qobject_cast<QScrollArea *>(m_window->iconPage(0)->parentWidget()->parentWidget());
    auto *stickers = qobject_cast<QScrollArea *>(m_window->iconPage(1)->parentWidget()->parentWidget());
    QVERIFY(markers && stickers && markers != stickers);
    QVERIFY(stickers->verticalScrollBar()->maximum() > 0);
    stickers->verticalScrollBar()->setValue(stickers->verticalScrollBar()->maximum());
    m_window->showIconPanel(0);
    QApplication::processEvents();
    QCOMPARE(markers->verticalScrollBar()->value(), 0);
    QVERIFY(markers->widget()->height() < stickers->widget()->height());
}

// Richiesta dell'utente 2026-10-20: l'adesivo scelto sulla mappa ha nel pannello di Stile le stesse
// impostazioni dell'immagine (misura, bordo, ombra, opacita').
void StickersMarkersTest::stickerHasTheImageStyle() {
    MindNode *node = addNode(QStringLiteral("Adesivo con stile"));
    QVERIFY(m_window->insertSticker(node, QStringLiteral(":/stickers/objects/rocket.svg")));
    m_window->showIconPanel(1);  // dal pannello degli adesivi
    itemFor(node)->selectImage();
    QApplication::processEvents();
    // Richiesta dell'utente 2026-10-21: scegliere l'adesivo porta da solo alla pagina Stile.
    QVERIFY(!m_window->isIconPanelShown());
    QVERIFY(m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->isChecked());
    auto *page = m_window->findChild<ImageStylePage *>();
    QVERIFY(page);
    QVERIFY2(page->isVisibleTo(m_window.get()), "pagina Stile dell'immagine nascosta per l'adesivo");
    QVERIFY(page->isVisible());
    // Contorno bianco e scuro: acceso per i nuovi adesivi, interruttore solo per loro.
    QVERIFY(node->imageStyle.outline);
    QVERIFY(page->outlineSwitch()->isVisibleTo(page));
    QVERIFY(page->outlineSwitch()->isChecked());
    const auto saved = [this] { return QJsonDocument(m_model->toJson()).toJson().count("\"imageOutline\": true"); };
    const qsizetype before = saved();
    QVERIFY(before >= 1);  // salvato nel .mentis
    page->outlineSwitch()->click();
    QApplication::processEvents();
    QVERIFY(!node->imageStyle.outline);
    QCOMPARE(saved(), before - 1);  // spento = predefinito: chiave assente
    page->resetStyleButton()->click();  // "Ripristina stile": l'adesivo torna con il contorno
    QApplication::processEvents();
    QVERIFY(node->imageStyle.outline);
    // Un'immagine qualsiasi non ha la riga.
    MindNode *photo = addNode(QStringLiteral("Foto"));
    QVERIFY(m_window->findChild<Document *>()->setImage(photo, png(QStringLiteral("foto.png"), Qt::darkGreen), {120, 120}));
    m_scene->updateNode(photo);
    itemFor(photo)->selectImage();
    QApplication::processEvents();
    QVERIFY(!page->outlineSwitch()->isVisibleTo(page));
}

// Il contorno: anello bianco attorno al disegno e filo scuro fuori, nella stessa misura.
void StickersMarkersTest::outlineDrawing() {
    QImage dot(200, 200, QImage::Format_ARGB32_Premultiplied);
    dot.fill(Qt::transparent);
    {
        QPainter painter(&dot);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::red);
        painter.drawEllipse(QRectF(40, 40, 120, 120));
    }
    const QImage out = Stickers::outlined(dot);
    QCOMPARE(out.size(), dot.size());
    const QColor centre = out.pixelColor(100, 100);
    QVERIFY(centre.red() > 200 && centre.green() < 60);
    // Lungo il raggio orizzontale verso destra: rosso, poi bianco, poi scuro, poi trasparente.
    bool white = false, dark = false;
    int x = 100;
    while (x < 199 && out.pixelColor(x, 100).red() > 200 && out.pixelColor(x, 100).green() < 60) ++x;
    for (; x < 200; ++x) {
        const QColor c = out.pixelColor(x, 100);
        if (c.alpha() > 200 && c.lightness() > 230) white = true;
        if (white && c.alpha() > 200 && c.lightness() < 60) dark = true;
        if (dark && c.alpha() < 20) break;
    }
    QVERIFY(white);
    QVERIFY(dark);
    QVERIFY(x < 200);  // dopo il filo scuro torna trasparente
}

void StickersMarkersTest::fileKeepsStickerAndMarkers() {
    MindMapModel model;
    MindNode *node = model.addChild(model.root(), QStringLiteral("Voce"));
    node->imagePath = png(QStringLiteral("salvato.png"), Qt::red);
    node->imageSticker = true;
    node->markers = {QStringLiteral("priority:1"), QStringLiteral("progress:50")};
    MindMapModel reopened;
    QVERIFY(reopened.fromJson(model.toJson()));
    const MindNode *back = reopened.root()->children.front().get();
    QVERIFY(back->imageSticker);
    QCOMPARE(back->markers, node->markers);
}

// Richiesta dell'utente 2026-10-20: togliere immagine, adesivo, formula e marcatore.
// Il comando Rimuovi della mappa (scorciatoia Canc).
static void pressDelete(MainWindow *window) {
    for (QAction *action : window->findChildren<QAction *>())
        if (action->text() == QStringLiteral("Remove") && action->shortcuts().contains(QKeySequence(QKeySequence::Delete))) {
            action->trigger();
            break;
        }
    QApplication::processEvents();
}

void StickersMarkersTest::deleteRemovesTheImageNotTheNode() {
    MindNode *node = addNode(QStringLiteral("Con immagine da togliere"));
    QVERIFY(m_window->insertSticker(node, png(QStringLiteral("via.png"), Qt::red)));
    NodeItem *item = itemFor(node);
    // Clic sull'immagine: scelta (maniglie), poi Canc.
    QVERIFY(m_scene->selectedImageNode() == nullptr);
    // L'adesivo sta sopra il testo: il suo centro e' nella meta' alta del nodo.
    const QRectF shape = item->mapRectToScene(item->shapeRect());
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, m_view->mapFromScene(QPointF(shape.center().x(), shape.top() + 40)));
    QApplication::processEvents();
    QCOMPARE(m_scene->selectedImageNode(), node);
    const qsizetype nodes = m_model->root()->children.size();
    pressDelete(m_window.get());
    QVERIFY(node->imagePath.isEmpty());
    QVERIFY(!node->imageSticker);
    QCOMPARE(m_model->root()->children.size(), nodes);  // il nodo resta
    flushHistory();
    undo();  // un passo
    QVERIFY(!node->imagePath.isEmpty());
}

void StickersMarkersTest::rightClickRemovesTheSticker() {
    MindNode *node = addNode(QStringLiteral("Adesivo dal menu"));
    QVERIFY(m_window->insertSticker(node, png(QStringLiteral("menu.png"), Qt::blue)));
    NodeItem *item = itemFor(node);
    const QRectF shape = item->mapRectToScene(item->shapeRect());
    const QPoint point = m_view->mapFromScene(QPointF(shape.center().x(), shape.top() + 40));
    chooseInNextMenu(QStringLiteral("Remove sticker"));
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, m_view->viewport()->mapToGlobal(point));
    QApplication::sendEvent(m_view->viewport(), &event);
    QApplication::processEvents();
    QVERIFY(node->imagePath.isEmpty());
}

void StickersMarkersTest::deleteOnAMarkerRemovesIt() {
    MindNode *node = addNode(QStringLiteral("Marcatore da togliere"));
    m_window->setMarker(node, QStringLiteral("flag"), QStringLiteral("flag:blue"));
    m_window->setMarker(node, QStringLiteral("symbol"), QStringLiteral("symbol:idea"));
    m_scene->selectNode(node);
    NodeItem *item = itemFor(node);
    m_view->centerOn(item);  // con tanti nodi delle prove prima il nodo puo' stare fuori dalla vista
    QApplication::processEvents();
    QTest::mouseMove(m_view->viewport(), QPoint(5, 5));  // prima fuori, cosi' il passaggio sul marcatore e' un movimento
    QTest::qWait(50);
    QTest::mouseMove(m_view->viewport(), m_view->mapFromScene(item->mapToScene(item->markerRects().front().center())));
    QTest::qWait(50);
    QApplication::processEvents();
    QCOMPARE(item->hoveredMarker(), 0);
    const qsizetype nodes = m_model->root()->children.size();
    pressDelete(m_window.get());
    QCOMPARE(node->markers, QStringList{QStringLiteral("symbol:idea")});
    QCOMPARE(m_model->root()->children.size(), nodes);
}

// Foto per il confronto a vista (solo con MENTIS_SNAPSHOT_DIR, piattaforma con i caratteri).
void StickersMarkersTest::snapshotForVisualReview() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR non impostata");
    m_model->clear(QStringLiteral("Progetto"));
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("Ricerca"));
    MindNode *b = m_model->addChild(m_model->root(), QStringLiteral("Scrittura"));
    MindNode *c = m_model->addChild(m_model->root(), QStringLiteral("Revisione"));
    a->markers = {QStringLiteral("priority:1"), QStringLiteral("progress:75")};
    b->markers = {QStringLiteral("priority:2"), QStringLiteral("progress:25"), QStringLiteral("flag:red")};
    c->markers = {QStringLiteral("progress:100"), QStringLiteral("symbol:star")};
    c->task = MindNode::Task::Done;
    for (MindNode *node : {a, b, c}) m_scene->updateNode(node);
    m_scene->autoLayout();
    QImage sticker(120, 120, QImage::Format_ARGB32);
    sticker.fill(Qt::transparent);
    {
        QPainter painter(&sticker);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor("#f5b400"));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(10, 10, 100, 100);
        painter.setBrush(QColor("#1f2937"));
        painter.drawEllipse(38, 42, 12, 16);
        painter.drawEllipse(70, 42, 12, 16);
    }
    const QString path = m_dir.filePath(QStringLiteral("faccina.png"));
    sticker.save(path);
    m_window->insertSticker(b, path);
    m_view->centerOn(itemFor(m_model->root()));
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/stickers-markers-map.png")));
    m_scene->selectNode(a);
    m_window->showIconPanel(0);
    QTest::qWait(500);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/markers-panel.png")));
    m_window->showIconPanel(1);
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/stickers-panel.png")));
    itemFor(b)->selectImage();
    m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->click();
    QTest::qWait(500);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/sticker-style.png")));
    // BUG-020 e rotazione: due nodi vuoti come nella foto di riferimento, un adesivo ruotato.
    for (int i = 0; i < 2; ++i) m_model->addChild(m_model->root(), QString());
    m_window->rotateVisual(b);
    m_scene->autoLayout();
    m_scene->clearSelection();
    m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"))->click();
    m_view->centerOn(itemFor(m_model->root()));
    QTest::qWait(400);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/empty-and-rotated.png")));
        // FEAT-065: l'outliner con marcatori, adesivo e immagine.
    MindNode *photo = m_model->addChild(m_model->root(), QStringLiteral("Circuito"));
    m_window->findChild<Document *>()->setImage(photo, png(QStringLiteral("circuito.png"), QColor("#dbeafe"), {400, 220}), {200, 110});
    photo->markers = {QStringLiteral("flag:green")};
    photo->webLink = QStringLiteral("https://example.org");
    m_scene->updateNode(photo);
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner")) { button->click(); break; }
    QTest::qWait(400);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/outliner-contents.png")));
}

QTEST_MAIN(StickersMarkersTest)
#include "stickers_markers_test.moc"
