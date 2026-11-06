// BUG-014: "Importa Markdown…" sostituiva il documento senza chiedere di salvare le modifiche.
// Ora chiede come "Apri": Annulla lascia il documento com'era, "Non salvare" importa il file.
// FEAT-055: il Markdown incollato nell'outliner diventa un ramo con i livelli, in un passo di storico;
// incollato nel titolo principale diventa tutta la mappa.
// Vero MainWindow, piattaforma offscreen e impostazioni separate.
#include "document.h"
#include "markdownimport.h"
#include "mainwindow.h"
#include "modaldialog.h"
#include "mindmapmodel.h"
#include "performance.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QTreeView>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>


class MarkdownImportTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cancelKeepsUnsavedDocument();
    void emptyListItemIsAnEmptyNode();
    void discardImportsTheFile();
    void pastedMarkdownBecomesABranch();
    void markdownPastedInRootTitleReplacesTheMap();
    // FEAT-066 tappa 4 (regressione): esportazione e testo del ramo copiato.
    void exportWritesTheMapAsMarkdown();
    void copiedBranchHasDataAndIndentedText();
    // BUG-017: Importa ed Esporta Markdown nel menu principale.
    void mainMenuOffersImportAndExport();

private:
    QAction *importAction() const;
    QAction *undoAction() const;
    // Aggiunge un nodo non salvato e avvia l'importazione; answer e' il bottone premuto nella
    // domanda sulle modifiche. Restituisce true se la domanda e' comparsa.
    bool importWithAnswer(ModalDialog::Choice answer);
    bool hasTopLevelTitle(const QString &title) const;

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    QTemporaryDir m_dir;
    QString m_markdownPath;
};

void MarkdownImportTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisMarkdownImportTest"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);
    QVERIFY(m_dir.isValid());
    m_markdownPath = m_dir.filePath(QStringLiteral("appunti.md"));
    QFile file(m_markdownPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("- Biologia\n  - Cellula\n  - Tessuti\n");
    file.close();

    m_window = std::make_unique<MainWindow>();
    m_window->setMarkdownFilePicker([this] { return m_markdownPath; });
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    auto *pages = qobject_cast<QStackedWidget *>(m_window->centralWidget());
    QVERIFY(pages);
    pages->setCurrentIndex(1);  // editor
    m_model = m_window->findChild<MindMapModel *>();
    QVERIFY(m_model);
    QVERIFY(importAction());
}

void MarkdownImportTest::cleanupTestCase() { m_window.reset(); }

// BUG-020: un nodo senza testo si esporta come "- " e torna con l'importazione, con i suoi figli
// al loro livello.
void MarkdownImportTest::emptyListItemIsAnEmptyNode() {
    const MarkdownImport::Result result = MarkdownImport::parse(QStringLiteral("- Primo\n- \n  - Figlio del vuoto\n- Ultimo\n"));
    QCOMPARE(result.items.size(), size_t(3));
    QCOMPARE(result.items[1].title, QString());
    QCOMPARE(result.items[1].children.size(), size_t(1));
    QCOMPARE(result.items[1].children[0].title, QStringLiteral("Figlio del vuoto"));
}

QAction *MarkdownImportTest::importAction() const {
    for (auto *candidate : m_window->findChildren<QAction *>())
        if (candidate->text() == QStringLiteral("Import Markdown…")) return candidate;
    return nullptr;
}

bool MarkdownImportTest::importWithAnswer(ModalDialog::Choice answer) {
    m_model->addChild(m_model->root(), QStringLiteral("Non salvato"));
    QApplication::processEvents();
    m_window->findChild<Document *>()->flushPendingStep();  // closes the history step at once
    bool asked = false;
    QTimer::singleShot(0, this, [&] {
        auto *box = qobject_cast<ModalDialog *>(QApplication::activeModalWidget());
        if (!box) return;
        // 2026-11-05: "Unsaved changes" has no "Cancel" but an X; "Don't save" left, "Save" right.
        auto *secondary = box->findChild<QPushButton *>(QStringLiteral("ModalSecondary"));
        auto *close = box->findChild<QToolButton *>(QStringLiteral("ModalClose"));
        if (box->cancelButton()->isVisible() || !close || !close->isVisible() || !secondary
            || secondary->x() >= box->acceptButton()->x() - 100) return;
        asked = true;
        if (answer == ModalDialog::Choice::Cancel) close->click();
        else if (answer == ModalDialog::Choice::Primary) box->acceptButton()->click();
        else box->findChild<QPushButton *>(QStringLiteral("ModalSecondary"))->click();
    });
    // Rete di sicurezza: una finestra rimasta aperta non blocca la prova.
    QTimer::singleShot(5000, this, [] { if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
    importAction()->trigger();
    QApplication::processEvents();
    return asked;
}

bool MarkdownImportTest::hasTopLevelTitle(const QString &title) const {
    for (const auto &child : m_model->root()->children)
        if (child->title == title) return true;
    return false;
}

void MarkdownImportTest::cancelKeepsUnsavedDocument() {
    QVERIFY(importWithAnswer(ModalDialog::Choice::Cancel));
    QVERIFY(hasTopLevelTitle(QStringLiteral("Non salvato")));
    QVERIFY(m_model->root()->title != QStringLiteral("Biologia"));
}

void MarkdownImportTest::discardImportsTheFile() {
    QVERIFY(importWithAnswer(ModalDialog::Choice::Secondary));
    QCOMPARE(m_model->root()->title, QStringLiteral("Biologia"));
    QVERIFY(hasTopLevelTitle(QStringLiteral("Cellula")));
    QVERIFY(!hasTopLevelTitle(QStringLiteral("Non salvato")));
}

namespace {
QString childTitles(const MindNode *node) {
    QStringList titles;
    for (const auto &child : node->children) titles.append(child->title);
    return titles.join(QLatin1Char('|'));
}
}  // namespace

void MarkdownImportTest::pastedMarkdownBecomesABranch() {
    QPushButton *outlinerButton = nullptr;
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner")) outlinerButton = button;
    QVERIFY(outlinerButton);
    outlinerButton->click();
    QTreeView *tree = nullptr;
    for (auto *candidate : m_window->findChildren<QTreeView *>())
        if (candidate->isVisible() && candidate->model() == m_model) tree = candidate;
    QVERIFY(tree);
    QApplication::processEvents();
    m_window->findChild<Document *>()->flushPendingStep();  // closes the history step at once
    // Dopo l'importazione: Biologia > Cellula, Tessuti. Si incolla sotto "Cellula".
    QCOMPARE(childTitles(m_model->root()), QStringLiteral("Cellula|Tessuti"));
    tree->setFocus();
    tree->setCurrentIndex(m_model->indexFromNode(m_model->root()->children.front().get()));
    QApplication::clipboard()->setText(QStringLiteral("# Organelli\nTesto di nota.\n- **Mitocondri**\n  - Creste\n- Ribosomi\n"));
    QTest::keyClick(tree, Qt::Key_V, Qt::ControlModifier);
    QApplication::processEvents();
    // FEAT-042 (2026-11-05): as the item's last child (before, after it at its level).
    QCOMPARE(childTitles(m_model->root()), QStringLiteral("Cellula|Tessuti"));
    MindNode *cellula = m_model->root()->children.front().get();
    MindNode *organelli = cellula->children.back().get();
    QCOMPARE(organelli->title, QStringLiteral("Organelli"));
    QCOMPARE(organelli->notes, QStringLiteral("Testo di nota."));
    QCOMPARE(childTitles(organelli), QStringLiteral("Mitocondri|Ribosomi"));
    QCOMPARE(childTitles(organelli->children.front().get()), QStringLiteral("Creste"));
    QVERIFY(!organelli->children.front()->titleHtml.isEmpty());
    QCOMPARE(tree->currentIndex(), m_model->indexFromNode(organelli));
    // Bug 2026-11-02 (manual pictures): the pasted branch stayed closed in the outliner, its items
    // hidden, while the map showed them.
    QVERIFY(tree->isExpanded(m_model->indexFromNode(organelli)));
    QVERIFY(tree->isExpanded(m_model->indexFromNode(organelli->children.front().get())));
    // Un solo passo di storico per tutto l'incolla.
    m_window->findChild<Document *>()->flushPendingStep();  // closes the history step at once
    QAction *undo = undoAction();
    QVERIFY(undo);
    const size_t pastedRow = cellula->children.size() - 1;
    undo->trigger();
    QApplication::processEvents();
    QCOMPARE(childTitles(m_model->root()), QStringLiteral("Cellula|Tessuti"));
    QCOMPARE(m_model->root()->children.front()->children.size(), pastedRow);
}

QAction *MarkdownImportTest::undoAction() const {
    for (auto *candidate : m_window->findChildren<QAction *>())
        if (candidate->shortcut() == QKeySequence(QKeySequence::Undo)) return candidate;
    return nullptr;
}

void MarkdownImportTest::markdownPastedInRootTitleReplacesTheMap() {
    // Dopo la prova precedente l'outliner e' aperto: Biologia > Cellula, Tessuti.
    QPlainTextEdit *rootEdit = nullptr;
    for (auto *edit : m_window->findChildren<QPlainTextEdit *>())
        if (edit->isVisible() && edit->toPlainText() == m_model->root()->title) rootEdit = edit;
    QVERIFY(rootEdit);
    rootEdit->setFocus();
    QApplication::processEvents();

    // Una sola voce: testo normale nel titolo, mappa invariata.
    rootEdit->selectAll();
    QApplication::clipboard()->setText(QStringLiteral("Chimica"));
    QTest::keyClick(rootEdit, Qt::Key_V, Qt::ControlModifier);
    QApplication::processEvents();
    QCOMPARE(rootEdit->toPlainText(), QStringLiteral("Chimica"));
    QCOMPARE(m_model->root()->title, QStringLiteral("Biologia"));
    QCOMPARE(childTitles(m_model->root()), QStringLiteral("Cellula|Tessuti"));

    // Piu' voci: la mappa diventa il Markdown incollato.
    rootEdit->selectAll();
    QApplication::clipboard()->setText(QStringLiteral("# Fisica\n## Meccanica\n- Moto\n## Ottica\n"));
    QTest::keyClick(rootEdit, Qt::Key_V, Qt::ControlModifier);
    QApplication::processEvents();
    QCOMPARE(m_model->root()->title, QStringLiteral("Fisica"));
    QCOMPARE(childTitles(m_model->root()), QStringLiteral("Meccanica|Ottica"));
    QCOMPARE(childTitles(m_model->root()->children.front().get()), QStringLiteral("Moto"));
    QCOMPARE(rootEdit->toPlainText(), QStringLiteral("Fisica"));

    // Uscendo dal campo il titolo resta quello nuovo; Ctrl+Z riporta la mappa di prima.
    m_window->findChild<Document *>()->flushPendingStep();  // closes the history step at once
    m_window->setFocus();
    QApplication::processEvents();
    QCOMPARE(m_model->root()->title, QStringLiteral("Fisica"));
    m_window->findChild<Document *>()->flushPendingStep();  // closes the history step at once
    QAction *undo = undoAction();
    QVERIFY(undo);
    undo->trigger();
    QApplication::processEvents();
    QCOMPARE(m_model->root()->title, QStringLiteral("Biologia"));
    QCOMPARE(childTitles(m_model->root()), QStringLiteral("Cellula|Tessuti"));
}

void MarkdownImportTest::exportWritesTheMapAsMarkdown() {
    // Dopo la prova precedente: Biologia > Cellula, Tessuti.
    const QString exported = m_dir.filePath(QStringLiteral("esportata.md"));
    m_window->setExportFilePicker([exported] { return exported; });
    m_window->setOfficeExportChooser([](OfficeExportDialog &) { return true; });  // FEAT-083: the Mind map tab
    QAction *exportAction = nullptr;
    for (auto *candidate : m_window->findChildren<QAction *>())
        if (candidate->text() == QStringLiteral("Export Markdown…")) exportAction = candidate;
    QVERIFY(exportAction);
    // FEAT-054 (2026-10-18): solo testo, senza righe di stile; le note restano rientrate sotto la voce.
    m_model->root()->children.front()->notes = QStringLiteral("Nota della cellula\nseconda riga");
    exportAction->trigger();
    // FEAT-054: il nome scelto diventa una cartella con dentro il .md.
    QFile file(Document::exportMarkdownPath(exported));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(file.readAll());
    QCOMPARE(text, QStringLiteral("- Biologia\n"
                                  "  - Cellula\n"
                                  "    Nota della cellula\n"
                                  "    seconda riga\n"
                                  "  - Tessuti\n"));
}

void MarkdownImportTest::copiedBranchHasDataAndIndentedText() {
    MindNode *cellula = m_model->root()->children.front().get();
    m_model->addChild(cellula, QStringLiteral("Membrana"));
    QApplication::processEvents();
    QTreeView *tree = nullptr;
    for (auto *candidate : m_window->findChildren<QTreeView *>())
        if (candidate->isVisible() && candidate->model() == m_model) tree = candidate;
    QVERIFY(tree);
    tree->setFocus();
    tree->selectionModel()->setCurrentIndex(m_model->indexFromNode(cellula), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QTest::keyClick(tree, Qt::Key_C, Qt::ControlModifier);
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    QVERIFY(mime);
    QVERIFY(mime->hasFormat(QStringLiteral("application/x-mentis-subtree")));
    QCOMPARE(mime->text(), QStringLiteral("Cellula\n\tMembrana"));
}

void MarkdownImportTest::mainMenuOffersImportAndExport() {
    QToolButton *mainMenu = nullptr;
    for (auto *button : m_window->findChildren<QToolButton *>())
        if (button->toolTip() == QStringLiteral("Main menu")) mainMenu = button;
    QVERIFY(mainMenu && mainMenu->menu());
    QMenu *importMenu = nullptr;
    QMenu *exportMenu = nullptr;
    QMenu *fileMenu = nullptr;
    for (QAction *action : mainMenu->menu()->actions()) {
        if (action->text() == QStringLiteral("Import")) importMenu = action->menu();
        if (action->text() == QStringLiteral("Export")) exportMenu = action->menu();
        if (action->text() == QStringLiteral("File")) fileMenu = action->menu();
    }
    // Import on the first level, above Export (user's request 2026-10-27; before in File > Import).
    QVERIFY(fileMenu);
    QVERIFY(importMenu && importMenu->actions().size() == 1);
    QCOMPARE(importMenu->actions().front()->text(), QStringLiteral("Markdown…"));
    // FEAT-054 (2026-10-18): "Esporta" e' un sottomenu dei formati; stage 2 (2026-10-26) adds images and PDF,
    // FEAT-063 stage C (2026-10-27) the Pitch presentation (.pptx), stage 4 (2026-10-27) Word and Excel.
    QVERIFY(exportMenu);
    QStringList formats;
    for (QAction *action : exportMenu->actions()) formats.append(action->text());
    QCOMPARE(formats, QStringList({QStringLiteral("Image…"), QStringLiteral("PDF…"), QStringLiteral("Word…"),
                                      QStringLiteral("Excel…"), QStringLiteral("Pitch…"), QStringLiteral("Markdown…")}));
    // Only there: File has no Export of its own (user's request 2026-10-27).
    for (QAction *action : fileMenu->actions())
        QVERIFY(action->text() != QStringLiteral("Export") && action->text() != QStringLiteral("Import"));
    // La voce del sottomenu esporta davvero.
    const QString exported = m_dir.filePath(QStringLiteral("dal-sottomenu.md"));
    m_window->setExportFilePicker([exported] { return exported; });
    m_window->setOfficeExportChooser([](OfficeExportDialog &) { return true; });  // FEAT-083: the Mind map tab
    exportMenu->actions().back()->trigger();
    QVERIFY(QFile::exists(Document::exportMarkdownPath(exported)));
}

QTEST_MAIN(MarkdownImportTest)
#include "markdown_import_test.moc"
