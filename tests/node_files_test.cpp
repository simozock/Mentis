// FEAT-049 (docs/features/FEAT-049-file-locali-nel-nodo.md): file del computer allegati al nodo,
// sempre incorporati nel .mentis, con il vero MainWindow: graffetta con il numero, elenco Apri / Salva
// con nome / Rimuovi, copia temporanea osservata e aggiornamento chiesto, tipi a rischio,
// trascinamento, salvataggio e riapertura, esportazione Markdown.
#include "document.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "modaldialog.h"
#include "nodeitem.h"
#include "performance.h"
#include "clipboardpaste.h"
#include "filevalidation.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QTreeView>
#include <QPlainTextEdit>
#include <QGraphicsTextItem>
#include <QContextMenuEvent>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

#include <memory>

class NodeFilesTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();
    void attachShowsTheClipWithTheCount();
    void openCopiesAndAsksToUpdate();
    void riskyFilesAskFirst();
    void menuSavesAndRemoves();
    void dropAttachesOrSetsTheImage();
    void clipboardContentKinds();
    void pasteOnTheMapTopic();
    void pasteOnTheOutlinerItem();
    void pasteWhileTypingTheTitle();
    void savedInsideTheDocument();
    void documentNamesAndIdsStayInTheirFolder();
    void depthLimitShowsTheCryingMascot();
    void markdownExportCopiesTheFiles();
    void snapshotForVisualReview();

private:
    QString makeFile(const QString &name, const QByteArray &content);
    MindNode *addNode(const QString &title);
    NodeItem *itemFor(const MindNode *node) const;
    void onNextModal(const std::function<void(ModalDialog *)> &handle);
    void chooseInNextMenu(const QString &text);
    void flushHistory();
    void undo();

    QTemporaryDir m_dir;
    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
    MindMapView *m_view = nullptr;
    Document *m_document = nullptr;
    QList<QUrl> m_opened;
    QStringList m_nextFiles;
};

void NodeFilesTest::initTestCase() {
    QVERIFY(m_dir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisNodeFilesTest"));
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
    m_document = m_window->findChild<Document *>();
    QVERIFY(m_model && m_view && m_scene && m_document);
    m_window->setUrlOpener([this](const QUrl &url) { m_opened.append(url); return true; });
    m_window->setAttachFilesPicker([this] { return std::exchange(m_nextFiles, {}); });
}

void NodeFilesTest::cleanupTestCase() { m_window.reset(); }

void NodeFilesTest::init() {
    m_opened.clear();
    m_scene->clearSelection();
    flushHistory();
}

// A wait for a window that never came (openCopiesAndAsksToUpdate: "not asked") ends with its test:
// it caught the next test's window and wrote into a variable gone with the test before (Linux CI).
void NodeFilesTest::cleanup() {
    for (QTimer *timer : findChildren<QTimer *>(QStringLiteral("nextModal"), Qt::FindDirectChildrenOnly)) delete timer;
}

void NodeFilesTest::flushHistory() {
    QApplication::processEvents();
    // The open history step is closed at once instead of waiting for its 150 ms timer.
    m_window->findChild<Document *>()->flushPendingStep();
    QApplication::processEvents();
}

void NodeFilesTest::undo() {
    for (auto *action : m_window->actions())
        if (action->shortcuts().contains(QKeySequence(QKeySequence::Undo))) { action->trigger(); break; }
    QApplication::processEvents();
}

QString NodeFilesTest::makeFile(const QString &name, const QByteArray &content) {
    const QString path = m_dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(content);
    return path;
}

MindNode *NodeFilesTest::addNode(const QString &title) {
    MindNode *node = m_model->addChild(m_model->root(), title);
    m_scene->autoLayout();
    flushHistory();
    return node;
}

NodeItem *NodeFilesTest::itemFor(const MindNode *node) const {
    for (auto *item : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->node() == node) return nodeItem;
    return nullptr;
}

void NodeFilesTest::onNextModal(const std::function<void(ModalDialog *)> &handle) {
    auto *timer = new QTimer(this);
    timer->setObjectName(QStringLiteral("nextModal"));
    auto attempts = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this, [timer, attempts, handle] {
        if (auto *modal = qobject_cast<ModalDialog *>(QApplication::activeModalWidget())) {
            timer->stop();
            timer->deleteLater();
            handle(modal);
        } else if (++*attempts > 1000) {  // 10 s: a slow CI runner
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start(10);
}

// Il menu della graffetta e' modale: si sceglie la voce appena compare.
void NodeFilesTest::chooseInNextMenu(const QString &text) {
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

void NodeFilesTest::attachShowsTheClipWithTheCount() {
    MindNode *node = addNode(QStringLiteral("Con file"));
    NodeItem *item = itemFor(node);
    const qreal widthBefore = item->boundingRect().width();
    QVERIFY(item->filesRect().isEmpty());
    m_scene->selectNode(node);
    QApplication::processEvents();
    m_nextFiles = {makeFile(QStringLiteral("relazione.pdf"), "%PDF-1.4 uno")};
    for (auto *menu : m_window->findChildren<QMenu *>())
        for (auto *action : menu->actions())
            if (action->text() == QStringLiteral("File…")) { action->trigger(); goto attached; }
attached:
    QApplication::processEvents();
    QCOMPARE(node->files.size(), 1);
    QCOMPARE(node->files.front().name, QStringLiteral("relazione.pdf"));
    QVERIFY(!node->files.front().assetId.isEmpty());  // contenuto riconosciuto subito (SHA-256)
    QVERIFY(!item->filesRect().isEmpty());
    QVERIFY(item->boundingRect().width() > widthBefore);  // la graffetta ha il suo posto
    // Ultimo a destra, dopo il collegamento web.
    node->webLink = QStringLiteral("https://example.com");
    m_scene->updateNode(node);
    QCOMPARE(item->linkIconRect(NodeItem::LinkKind::Web).right() + NodeItem::kLinkIconGapPx, item->filesRect().left());
    QVERIFY(m_window->attachFiles(node, {makeFile(QStringLiteral("dati.csv"), "a;b\n1;2\n")}));
    QCOMPARE(node->files.size(), 2);
    flushHistory();
    undo();  // un passo per aggiunta
    QCOMPARE(node->files.size(), 1);
}

void NodeFilesTest::openCopiesAndAsksToUpdate() {
    MindNode *node = addNode(QStringLiteral("Da aprire"));
    QVERIFY(m_window->attachFiles(node, {makeFile(QStringLiteral("appunti.txt"), "prima versione")}));
    const QString originalId = node->files.front().assetId;
    const QString copy = m_window->openNodeFile(node, 0);
    QVERIFY(!copy.isEmpty());
    QCOMPARE(m_opened, QList<QUrl>{QUrl::fromLocalFile(copy)});
    QFile opened(copy);
    QVERIFY(opened.open(QIODevice::ReadOnly));
    QCOMPARE(opened.readAll(), QByteArray("prima versione"));
    opened.close();
    // Modificato fuori dall'app: si chiede e si aggiorna.
    QFile changed(copy);
    QVERIFY(changed.open(QIODevice::WriteOnly | QIODevice::Truncate));
    changed.write("seconda versione, piu' lunga");
    changed.close();
    QString message;
    onNextModal([&message](ModalDialog *modal) {
        for (auto *label : modal->findChildren<QLabel *>()) message += label->text();
        modal->acceptButton()->click();
    });
    m_window->checkOpenedFiles();
    QApplication::processEvents();
    QVERIFY2(message.contains(QStringLiteral("appunti.txt")), qPrintable(message));
    QVERIFY(node->files.front().assetId != originalId);
    QCOMPARE(node->files.front().size, qint64(QByteArray("seconda versione, piu' lunga").size()));
    // La stessa versione non si richiede.
    bool asked = false;
    onNextModal([&asked](ModalDialog *modal) { asked = true; modal->reject(); });
    m_window->checkOpenedFiles();
    QTest::qWait(100);
    QVERIFY(!asked);
}

void NodeFilesTest::riskyFilesAskFirst() {
    QVERIFY(MainWindow::isRiskyFile(QStringLiteral("setup.EXE")));
    QVERIFY(MainWindow::isRiskyFile(QStringLiteral("script.ps1")));
    QVERIFY(!MainWindow::isRiskyFile(QStringLiteral("tesi.pdf")));
    QVERIFY(MainWindow::isRiskyFile(QStringLiteral("libreria.dll")));  // BUG-043
    QVERIFY(MainWindow::isRiskyFile(QStringLiteral("sito.url")));
    MindNode *node = addNode(QStringLiteral("Programma"));
    QVERIFY(m_window->attachFiles(node, {makeFile(QStringLiteral("strumento.bat"), "echo ciao")}));
    onNextModal([](ModalDialog *modal) { modal->reject(); });  // Annulla
    QVERIFY(m_window->openNodeFile(node, 0).isEmpty());
    QVERIFY(m_opened.isEmpty());
}

void NodeFilesTest::menuSavesAndRemoves() {
    MindNode *node = addNode(QStringLiteral("Menu"));
    QVERIFY(m_window->attachFiles(node, {makeFile(QStringLiteral("tabella.xlsx"), "PK contenuto")}));
    NodeItem *item = itemFor(node);
    const QPoint clip = m_view->mapFromScene(item->mapToScene(item->filesRect().center()));
    const QString target = m_dir.filePath(QStringLiteral("salvato.xlsx"));
    m_window->setAttachmentSavePicker([target](const QString &) { return target; });
    chooseInNextMenu(QStringLiteral("Save as…"));
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, clip);
    QApplication::processEvents();
    QFile saved(target);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), QByteArray("PK contenuto"));
    // Tasto destro: lo stesso elenco; Rimuovi.
    chooseInNextMenu(QStringLiteral("Remove"));
    QContextMenuEvent event(QContextMenuEvent::Mouse, clip, m_view->viewport()->mapToGlobal(clip));
    QApplication::sendEvent(m_view->viewport(), &event);
    QApplication::processEvents();
    QVERIFY(node->files.isEmpty());
    QVERIFY(item->filesRect().isEmpty());
}

void NodeFilesTest::dropAttachesOrSetsTheImage() {
    MindNode *node = addNode(QStringLiteral("Bersaglio"));
    const QPointF center = m_scene->nodeSceneRect(node).center();
    emit m_view->filesDropped({makeFile(QStringLiteral("a.txt"), "a"), makeFile(QStringLiteral("b.txt"), "b")}, center);
    QApplication::processEvents();
    QCOMPARE(node->files.size(), 2);
    // Un'immagine sola su un nodo senza immagine: diventa l'immagine.
    QImage image(40, 20, QImage::Format_RGB32);
    image.fill(Qt::red);
    const QString png = m_dir.filePath(QStringLiteral("foto.png"));
    QVERIFY(image.save(png));
    emit m_view->filesDropped({png}, m_scene->nodeSceneRect(node).center());
    QApplication::processEvents();
    QVERIFY(!node->imagePath.isEmpty());
    QCOMPARE(node->files.size(), 2);
    // Sullo sfondo: niente.
    emit m_view->filesDropped({makeFile(QStringLiteral("c.txt"), "c")}, QPointF(-5000, -5000));
    QApplication::processEvents();
    QCOMPARE(node->files.size(), 2);
}

// FEAT-094: what the clipboard holds decides what Ctrl+V does on a topic.
void NodeFilesTest::clipboardContentKinds() {
    using ClipboardPaste::Kind;
    const QString branchType = QStringLiteral("application/x-mentis-test-branch");
    const auto kindOf = [&](QMimeData &mime) { return ClipboardPaste::read(&mime, branchType).kind; };
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::blue);
    {   QMimeData mime; mime.setData(branchType, "{}"); mime.setText(QStringLiteral("ramo")); QCOMPARE(kindOf(mime), Kind::Branch); }
    {   QMimeData mime; mime.setImageData(image); QCOMPARE(kindOf(mime), Kind::Image); }
    {   // An Office table copies a picture with its text: it stays text.
        QMimeData mime; mime.setImageData(image); mime.setText(QStringLiteral("a\tb\n1\t2")); QCOMPARE(kindOf(mime), Kind::Text); }
    {   QMimeData mime; mime.setImageData(image); mime.setText(QStringLiteral("https://example.com/a.png")); QCOMPARE(kindOf(mime), Kind::Image); }
    {   QMimeData mime; mime.setUrls({QUrl::fromLocalFile(makeFile(QStringLiteral("k.pdf"), "%PDF"))});
        const ClipboardPaste::Content content = ClipboardPaste::read(&mime, branchType);
        QCOMPARE(content.kind, Kind::Files); QCOMPARE(content.files.size(), 1); }
    {   QMimeData mime; mime.setText(QStringLiteral("  https://example.com/pagina?x=1 ")); QCOMPARE(kindOf(mime), Kind::WebLink);
        QCOMPARE(ClipboardPaste::read(&mime, branchType).address, QStringLiteral("https://example.com/pagina?x=1")); }
    {   QMimeData mime; mime.setText(QStringLiteral("www.example.com"));
        QCOMPARE(ClipboardPaste::read(&mime, branchType).address, QStringLiteral("https://www.example.com")); }
    {   QMimeData mime; mime.setUrls({QUrl(QStringLiteral("https://example.org"))}); QCOMPARE(kindOf(mime), Kind::WebLink); }
    for (const QString &text : {QStringLiteral("example"), QStringLiteral("vedi https://example.com"), QStringLiteral("ftp://example.com"),
                                QStringLiteral("javascript:alert(1)")}) {
        QMimeData mime; mime.setText(text); QCOMPARE(kindOf(mime), Kind::Text);
    }
    { QMimeData mime; QCOMPARE(kindOf(mime), Kind::None); }
    const QString saved = ClipboardPaste::saveImage(image);
    QVERIFY(QFileInfo::exists(saved));
    QCOMPARE(QFileInfo(saved).fileName(), QStringLiteral("pasted-image.png"));
    QCOMPARE(ClipboardPaste::saveImage(image), saved);  // the same image, the same file
}

void NodeFilesTest::pasteOnTheMapTopic() {
    MindNode *node = addNode(QStringLiteral("Incolla qui"));
    m_scene->selectNode(node);
    m_window->activateWindow();  // the modals of the tests before leave the window without focus
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    m_view->setFocus();
    QApplication::processEvents();
    QClipboard *clipboard = QApplication::clipboard();
    const auto paste = [&] { QTest::keyClick(m_view->viewport(), Qt::Key_V, Qt::ControlModifier); QApplication::processEvents(); };
    QImage image(30, 20, QImage::Format_RGB32);
    image.fill(Qt::green);
    clipboard->setImage(image);
    paste();
    QVERIFY(!node->imagePath.isEmpty());
    QCOMPARE(QImage(node->imagePath).size(), QSize(30, 20));
    QCOMPARE(node->imageOriginalName, QStringLiteral("pasted-image.png"));
    flushHistory();
    undo();  // one step
    QVERIFY(node->imagePath.isEmpty());
    paste();
    QVERIFY(!node->imagePath.isEmpty());
    flushHistory();
    auto *files = new QMimeData;
    files->setUrls({QUrl::fromLocalFile(makeFile(QStringLiteral("incollato.pdf"), "%PDF-1.4"))});
    clipboard->setMimeData(files);
    paste();
    QCOMPARE(node->files.size(), 1);
    QCOMPARE(node->files.front().name, QStringLiteral("incollato.pdf"));
    clipboard->setText(QStringLiteral("https://example.com/incollato"));
    paste();
    QCOMPARE(node->webLink, QStringLiteral("https://example.com/incollato"));
    flushHistory();
    const qsizetype children = node->children.size();
    clipboard->setText(QStringLiteral("Figlio incollato"));
    paste();  // plain text: a child topic, as Edit > Paste did
    QCOMPARE(node->children.size(), children + 1);
    QCOMPARE(node->children.back()->title, QStringLiteral("Figlio incollato"));
    flushHistory();
    undo();
    QCOMPARE(node->children.size(), children);
}

void NodeFilesTest::pasteOnTheOutlinerItem() {
    MindNode *node = addNode(QStringLiteral("Voce dell'outliner"));
    QTreeView *tree = nullptr;
    for (auto *candidate : m_window->findChildren<QTreeView *>())
        if (candidate->model() == m_model) tree = candidate;
    QVERIFY(tree);
    const auto toggleOutliner = [this] {
        for (auto *action : m_window->actions())
            if (action->shortcuts().contains(QKeySequence(QStringLiteral("Ctrl+Shift+M")))) { action->trigger(); break; }
        QApplication::processEvents();
    };
    toggleOutliner();
    QVERIFY(tree->isVisible());
    tree->setFocus();
    tree->setCurrentIndex(m_model->indexFromNode(node));
    QImage image(12, 12, QImage::Format_RGB32);
    image.fill(Qt::yellow);
    QApplication::clipboard()->setImage(image);
    QTest::keyClick(tree, Qt::Key_V, Qt::ControlModifier);
    QApplication::processEvents();
    QVERIFY(!node->imagePath.isEmpty());
    QApplication::clipboard()->setText(QStringLiteral("mailto:a@example.com"));
    tree->setCurrentIndex(m_model->indexFromNode(node));
    QTest::keyClick(tree, Qt::Key_V, Qt::ControlModifier);
    QApplication::processEvents();
    QCOMPARE(node->webLink, QStringLiteral("mailto:a@example.com"));
    toggleOutliner();  // back to the map for the tests after
}

// FEAT-094: while the title is typed, an image or files go onto the topic (the text is kept and
// confirmed); a web address goes into the text.
void NodeFilesTest::pasteWhileTypingTheTitle() {
    MindNode *node = addNode(QStringLiteral("Prima"));
    NodeItem *item = itemFor(node);
    m_scene->selectNode(node);
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    m_view->setFocus();
    item->beginEditing();
    QApplication::processEvents();
    QVERIFY(item->isEditing());
    QTest::keyClicks(m_view->viewport(), QStringLiteral("Scritto"));
    QApplication::clipboard()->setText(QStringLiteral("https://example.com/nel-testo"));
    QTest::keyClick(m_view->viewport(), Qt::Key_V, Qt::ControlModifier);
    QApplication::processEvents();
    QVERIFY(item->isEditing());
    QCOMPARE(item->titleEditor()->toPlainText(), QStringLiteral("Scrittohttps://example.com/nel-testo"));
    QVERIFY(node->webLink.isEmpty());
    QTest::keyClick(m_view->viewport(), Qt::Key_Backspace);  // back to "Scritto" + part of the address
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::magenta);
    QApplication::clipboard()->setImage(image);
    QTest::keyClick(m_view->viewport(), Qt::Key_V, Qt::ControlModifier);
    QApplication::processEvents();
    QApplication::processEvents();
    QVERIFY(!item->isEditing());  // the text is confirmed, as from the + menu
    QVERIFY(node->title.startsWith(QStringLiteral("Scrittohttps://")));
    QVERIFY(!node->imagePath.isEmpty());
    flushHistory();

    // Outliner: files pasted in the open field are attached to the item.
    QTreeView *tree = nullptr;
    for (auto *candidate : m_window->findChildren<QTreeView *>())
        if (candidate->model() == m_model) tree = candidate;
    QVERIFY(tree);
    for (auto *action : m_window->actions())
        if (action->shortcuts().contains(QKeySequence(QStringLiteral("Ctrl+Shift+M")))) { action->trigger(); break; }
    QApplication::processEvents();
    const QModelIndex index = m_model->indexFromNode(node);
    tree->setFocus();
    tree->setCurrentIndex(index);
    tree->edit(index);
    QApplication::processEvents();
    auto *field = qobject_cast<QPlainTextEdit *>(QApplication::focusWidget());
    QVERIFY(field);
    auto *files = new QMimeData;
    files->setUrls({QUrl::fromLocalFile(makeFile(QStringLiteral("scrivendo.pdf"), "%PDF-1.4 b"))});
    QApplication::clipboard()->setMimeData(files);
    const qsizetype before = node->files.size();
    QTest::keyClick(field, Qt::Key_V, Qt::ControlModifier);
    QApplication::processEvents();
    QApplication::processEvents();
    QCOMPARE(node->files.size(), before + 1);
    QVERIFY(node->title.startsWith(QStringLiteral("Scrittohttps://")));  // the title did not take the path
    for (auto *action : m_window->actions())
        if (action->shortcuts().contains(QKeySequence(QStringLiteral("Ctrl+Shift+M")))) { action->trigger(); break; }
    QApplication::processEvents();
}

void NodeFilesTest::savedInsideTheDocument() {
    m_model->clear(QStringLiteral("Documento con file"));
    MindNode *node = addNode(QStringLiteral("Allegati"));
    const QString source = makeFile(QStringLiteral("dentro.txt"), "contenuto incorporato");
    QVERIFY(m_window->attachFiles(node, {source}));
    const QString bmap = m_dir.filePath(QStringLiteral("con-file.mentis"));
    QVERIFY(m_document->save(bmap, BmapStorage::SaveReason::Explicit).ok);
    QVERIFY(QFile::remove(source));  // l'originale non serve piu'
    QVERIFY(m_document->open(bmap, false).ok);
    QApplication::processEvents();
    const MindNode *back = nullptr;
    for (const auto &child : m_model->root()->children)
        if (child->title == QStringLiteral("Allegati")) back = child.get();
    QVERIFY(back);
    QCOMPARE(back->files.size(), 1);
    QVERIFY(back->files.front().path.isEmpty());
    QCOMPARE(back->files.front().name, QStringLiteral("dentro.txt"));
    QFile extracted(m_document->fileSource(back, 0));
    QVERIFY(extracted.open(QIODevice::ReadOnly));
    QCOMPARE(extracted.readAll(), QByteArray("contenuto incorporato"));
}

// BUG-043: a document from someone else can hold any attachment name and asset id. The name stays
// a plain file name and the id cannot lead outside the cache.
void NodeFilesTest::documentNamesAndIdsStayInTheirFolder() {
    QCOMPARE(FileValidation::diskFileName(QStringLiteral("tesi finale.pdf"), 1), QStringLiteral("attach01.pdf"));
    QCOMPARE(FileValidation::diskFileName(QStringLiteral("../../Startup/evil.BAT"), 2), QStringLiteral("attach02.BAT"));
    QCOMPARE(FileValidation::diskFileName(QStringLiteral("evil.exe. "), 3), QStringLiteral("attach03"));
    QCOMPARE(FileValidation::diskFileName(QStringLiteral("nota.txt:flusso"), 4), QStringLiteral("attach04"));
    QCOMPARE(FileValidation::diskFileName(QStringLiteral("senza estensione"), 123), QStringLiteral("attach123"));
    QVERIFY(MainWindow::isRiskyFile(FileValidation::diskFileName(QStringLiteral("x.EXE"), 1)));

    m_model->clear(QStringLiteral("Documento ricevuto"));
    MindNode *node = addNode(QStringLiteral("Ricevuto"));
    QVERIFY(m_window->attachFiles(node, {makeFile(QStringLiteral("ricevuto.txt"), "dal file")}));
    const QString bmap = m_dir.filePath(QStringLiteral("ricevuto.mentis"));
    QVERIFY(m_document->save(bmap, BmapStorage::SaveReason::Explicit).ok);
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString escaped = cache + QStringLiteral("/mentis-escape-thumb.jpg");
    QFile::remove(escaped);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("bug043"));
        db.setDatabaseName(bmap);
        QVERIFY(db.open());
        QSqlQuery asset(db);
        asset.prepare(QStringLiteral("INSERT INTO assets(id,sha256,mime_type,file_name,byte_size,data,thumbnail,thumbnail_mime) "
                                     "VALUES(?,?,?,?,?,?,?,?)"));
        const QString badId = QStringLiteral("../../mentis-escape");
        for (const QVariant &value : QVariantList{badId, QByteArray(32, 'x'), QStringLiteral("image/jpeg"), QStringLiteral("a.jpg"),
                                                  4, QByteArray("JPEG"), QByteArray("JPEG"), QStringLiteral("image/jpeg")})
            asset.addBindValue(value);
        QVERIFY2(asset.exec(), qPrintable(asset.lastError().text()));
        QSqlQuery payload(db);
        payload.prepare(QStringLiteral("UPDATE nodes SET payload=json_set(CAST(payload AS TEXT), '$.files[0].name', ?, "
                                       "'$.imageAsset', ?, '$.imageName', 'a.jpg') WHERE CAST(payload AS TEXT) LIKE '%ricevuto.txt%'"));
        payload.addBindValue(QStringLiteral("..\\..\\mentis-escape.txt"));
        payload.addBindValue(badId);
        QVERIFY2(payload.exec(), qPrintable(payload.lastError().text()));
        QCOMPARE(payload.numRowsAffected(), 1);
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("bug043"));
    QVERIFY(m_document->open(bmap, false).ok);
    QApplication::processEvents();
    QVERIFY2(!QFileInfo::exists(escaped), qPrintable(escaped));
    MindNode *back = nullptr;
    for (const auto &child : m_model->root()->children)
        if (child->title == QStringLiteral("Ricevuto")) back = child.get();
    QVERIFY(back);
    QCOMPARE(back->files.size(), 1);
    // The name in the map stays as the document has it; the copy on disk is numbered.
    QCOMPARE(back->files.front().name, QStringLiteral("..\\..\\mentis-escape.txt"));
    const QString copy = m_window->openNodeFile(back, 0);
    QVERIFY(!copy.isEmpty());
    QCOMPARE(QFileInfo(copy).fileName(), QStringLiteral("attach01.txt"));
    QCOMPARE(QFileInfo(QFileInfo(copy).absolutePath()).dir().dirName(), QStringLiteral("Mentis-files"));
}

// BUG-046: adding a child past the limit opens one message with the crying mascot; nothing is added.
void NodeFilesTest::depthLimitShowsTheCryingMascot() {
    m_model->clear(QStringLiteral("Profonda"));
    MindNode *deepest = m_model->root();
    for (int level = 1; level < kMaxTreeDepth; ++level) deepest = m_model->addChild(deepest, QStringLiteral("L%1").arg(level));
    m_scene->autoLayout();
    m_scene->selectNode(deepest);
    QString message;
    bool mascot = false;
    onNextModal([&](ModalDialog *modal) {
        for (auto *label : modal->findChildren<QLabel *>()) {
            message += label->text();
            if (label->objectName() == QStringLiteral("ModalMascot")) mascot = !label->pixmap().isNull();
        }
        modal->acceptButton()->click();
    });
    QAction *addChild = nullptr;
    for (auto *action : m_window->findChildren<QAction *>())
        if (action->text() == QStringLiteral("Add child")) addChild = action;
    QVERIFY(addChild);
    addChild->trigger();
    QTRY_VERIFY(!message.isEmpty());
    QVERIFY(mascot);
    QVERIFY2(message.contains(QString::number(kMaxTreeDepth)), qPrintable(message));
    QVERIFY(deepest->children.empty());
}

void NodeFilesTest::markdownExportCopiesTheFiles() {
    m_model->clear(QStringLiteral("Esportazione"));
    MindNode *node = addNode(QStringLiteral("Con allegato"));
    QVERIFY(m_window->attachFiles(node, {makeFile(QStringLiteral("scheda.docx"), "PK docx")}));
    const QString markdown = m_dir.filePath(QStringLiteral("export/mappa.md"));
    const Document::ExportResult result = m_document->exportMarkdown(markdown);
    QVERIFY2(result.ok, qPrintable(result.error));
    QFile text(markdown);
    QVERIFY(text.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(text.readAll()).contains(QStringLiteral("[scheda.docx](assets/attach01.docx)")));
    QVERIFY(QFile::exists(m_dir.filePath(QStringLiteral("export/assets/attach01.docx"))));
}

// Foto per il confronto a vista (solo con MENTIS_SNAPSHOT_DIR, piattaforma con i caratteri).
void NodeFilesTest::snapshotForVisualReview() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR non impostata");
    m_model->clear(QStringLiteral("Tesi"));
    MindNode *a = m_model->addChild(m_model->root(), QStringLiteral("Bibliografia"));
    MindNode *b = m_model->addChild(m_model->root(), QStringLiteral("Dati"));
    m_scene->autoLayout();
    m_window->attachFiles(a, {makeFile(QStringLiteral("articolo.pdf"), "%PDF")});
    m_window->attachFiles(b, {makeFile(QStringLiteral("misure.csv"), "1;2"), makeFile(QStringLiteral("grafico.xlsx"), "PK"),
                              makeFile(QStringLiteral("note.txt"), "ok")});
    b->webLink = QStringLiteral("https://example.com");
    m_scene->updateNode(b);
    m_scene->autoLayout();
    m_view->centerOn(itemFor(m_model->root()));
    QTest::qWait(300);
    QVERIFY(m_window->grab().save(dir + QStringLiteral("/files-map.png")));
    NodeItem *item = itemFor(b);
    const QPoint clip = m_view->mapFromScene(item->mapToScene(item->filesRect().center()));
    QTimer::singleShot(400, this, [this, dir] {
        m_window->grab().save(dir + QStringLiteral("/files-menu.png"));
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            menu->grab().save(dir + QStringLiteral("/files-menu-only.png"));
            menu->close();
        }
    });
    QTest::mouseClick(m_view->viewport(), Qt::LeftButton, {}, clip);
}

QTEST_MAIN(NodeFilesTest)
#include "node_files_test.moc"
