// FEAT-039 (docs/features/FEAT-039-validazione-file-importati.md): controllo del contenuto reale di
// immagini e file prima dell'inserimento, conferma per i file grandi, modale di rifiuto con il
// vero MainWindow e segnaposto per le immagini che non si leggono.
#include "filevalidation.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "modaldialog.h"
#include "nodeitem.h"
#include "performance.h"

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QStyleOptionGraphicsItem>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <functional>
#include <memory>

class FileValidationTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void imagesAreCheckedByContent();
    void filterListsOnlyReadableFormats();
    void rejectedImageShowsTheReasonAndChangesNothing();
    void missingImageKeepsItsPlaceWithAPlaceholder();

private:
    QString write(const QString &name, const QByteArray &bytes);
    QString png(const QString &name, QSize size);
    void onNextModal(const std::function<void(ModalDialog *)> &handle);

    QTemporaryDir m_dir;
    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    MindMapScene *m_scene = nullptr;
};

void FileValidationTest::initTestCase() {
    QVERIFY(m_dir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisFileValidationTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1200, 800);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    qobject_cast<QStackedWidget *>(m_window->centralWidget())->setCurrentIndex(1);
    m_model = m_window->findChild<MindMapModel *>();
    m_scene = qobject_cast<MindMapScene *>(m_window->findChild<MindMapView *>()->scene());
    QVERIFY(m_model && m_scene);
}

void FileValidationTest::cleanupTestCase() { m_window.reset(); }

QString FileValidationTest::write(const QString &name, const QByteArray &bytes) {
    const QString path = m_dir.filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) file.write(bytes);
    return path;
}

QString FileValidationTest::png(const QString &name, QSize size) {
    QImage image(size, QImage::Format_RGB32);
    image.fill(QColor("#50c3f7"));
    const QString path = m_dir.filePath(name);
    image.save(path, "PNG");
    return path;
}

void FileValidationTest::onNextModal(const std::function<void(ModalDialog *)> &handle) {
    auto *timer = new QTimer(this);
    auto attempts = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this, [timer, attempts, handle] {
        if (auto *modal = qobject_cast<ModalDialog *>(QApplication::activeModalWidget())) {
            timer->stop();
            timer->deleteLater();
            handle(modal);
        } else if (++*attempts > 100) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start(10);
}

void FileValidationTest::imagesAreCheckedByContent() {
    QVERIFY(FileValidation::image(png(QStringLiteral("vera.png"), {40, 20})).ok);
    // Un'immagine PNG con l'estensione .jpg: conta il contenuto.
    QFile::copy(m_dir.filePath(QStringLiteral("vera.png")), m_dir.filePath(QStringLiteral("rinominata.jpg")));
    QVERIFY(FileValidation::image(m_dir.filePath(QStringLiteral("rinominata.jpg"))).ok);
    const FileValidation::Check text = FileValidation::image(write(QStringLiteral("finta.png"), "solo testo, non un'immagine"));
    QVERIFY(!text.ok);
    QVERIFY(!text.reason.isEmpty());
    // PNG troncato a meta': intestazione giusta, contenuto rovinato.
    QFile real(png(QStringLiteral("intera.png"), {300, 300}));
    QVERIFY(real.open(QIODevice::ReadOnly));
    const QByteArray bytes = real.readAll();
    QVERIFY(!FileValidation::image(write(QStringLiteral("troncata.png"), bytes.left(bytes.size() / 3))).ok);
    QVERIFY(!FileValidation::image(m_dir.filePath(QStringLiteral("non-esiste.png"))).ok);
    QVERIFY(!FileValidation::image(write(QStringLiteral("vuota.png"), QByteArray())).ok);
    // Grande: ok ma con la conferma richiesta.
    const FileValidation::Check big = FileValidation::anyFile(write(QStringLiteral("grande.bin"),
                                                                    QByteArray(FileValidation::kLargeFileBytes + 1, 'x')));
    QVERIFY(big.ok);
    QVERIFY(big.large);
    QVERIFY(!big.largeReason.isEmpty());
}

void FileValidationTest::filterListsOnlyReadableFormats() {
    const QString filter = FileValidation::imageFilter();
    QVERIFY(filter.contains(QStringLiteral("*.png")));
    QCOMPARE(filter.contains(QStringLiteral("*.webp")), QImageReader::supportedImageFormats().contains("webp"));
}

void FileValidationTest::rejectedImageShowsTheReasonAndChangesNothing() {
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("Foto"));
    m_scene->autoLayout();
    m_scene->selectNode(node);
    QApplication::processEvents();
    const QString fake = write(QStringLiteral("rovinata.png"), "testo");
    m_window->setImageFilePicker([fake] { return fake; });
    QString message;
    onNextModal([&message](ModalDialog *modal) {
        for (auto *label : modal->findChildren<QLabel *>()) message += label->text();
        modal->acceptButton()->click();
    });
    m_window->chooseImage();
    QApplication::processEvents();
    QVERIFY(message.contains(QStringLiteral("rovinata.png")));
    QVERIFY(node->imagePath.isEmpty());  // nessuna immagine inserita
    m_window->setImageFilePicker({});
}

void FileValidationTest::missingImageKeepsItsPlaceWithAPlaceholder() {
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("Persa"));
    node->imagePath = m_dir.filePath(QStringLiteral("sparita.png"));  // come un asset non leggibile
    node->imageSize = {120, 80};
    m_scene->updateNode(node);
    m_scene->autoLayout();
    NodeItem *item = nullptr;
    for (auto *candidate : m_scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(candidate); nodeItem && nodeItem->node() == node) item = nodeItem;
    QVERIFY(item);
    QImage canvas(400, 300, QImage::Format_ARGB32);
    QPainter painter(&canvas);
    QStyleOptionGraphicsItem option;
    option.exposedRect = item->boundingRect();
    item->paint(&painter, &option, nullptr);  // primo disegno: caricamento fallito
    painter.end();
    QApplication::processEvents();
    QCOMPARE(item->imageBounds().size(), QSizeF(120, 80));  // il posto resta
}

QTEST_MAIN(FileValidationTest)
#include "file_validation_test.moc"
