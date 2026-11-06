// FEAT-041: scorciatoie modificabili nella finestra vera. Le azioni cambiano subito, l'outliner e il
// suo menu leggono i tasti dal registro, la pagina Scorciatoie (gruppi, ricerca, conflitto con il
// modale, Sostituisci) e "Scorciatoie…" che apre le Impostazioni su quella pagina. Stage B: i comandi
// standard con i loro tasti, nessun conflitto fra le predefinite, solo sulla mappa, tasti nei menu.
// Piattaforma offscreen, impostazioni di prova separate.
#include "document.h"
#include "gantttaskpanel.h"
#include "ganttview.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "noteeditor.h"
#include "modaldialog.h"
#include "performance.h"
#include "richtitle.h"
#include "settingsdialog.h"
#include "shortcuts.h"
#include "sidepanel.h"
#include "tour.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QGraphicsTextItem>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextDocument>
#include <QTreeView>

#include <functional>
#include <memory>

class ShortcutsUiTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void actionsChangeImmediately();
    void outlinerKeysComeFromTheRegistry();
    void pageShowsGroupsAndFilters();
    void conflictAsksAndReplaceMovesTheKey();
    void shortcutsMenuOpensTheSettingsPage();
    void spaceOnTheMapStartsEditingAndTypesInside();
    void standardRowsHaveTheirDefaults();
    void defaultKeysHaveNoConflicts();
    void standardKeysAct();
    void panelKeysToggle();
    void topicKeysAreMapOnly();
    void menusShowTheKeys();
    void standardTopicKeysAct();
    void everyAssignedKeyActs();
    void helpKeyIsRecordedInSettings();
    void charKeysFormatTheWholeTopic();
    void charKeysFormatTheSelectionOnTheMap();
    void textTypedAfterOpeningIsNormal();
    void charKeysInTheOutliner();

private:
    QAction *action(const QString &text) const;
    QWidget *row(QWidget *page, const QString &id) const;
    QKeySequenceEdit *edit(QWidget *page, const QString &id, int index) const;
    // Il modale del conflitto si apre in coda (dopo la registrazione del tasto): si controlla ogni
    // 10 ms per un secondo e si chiama handle sul primo modale trovato.
    void onNextModal(const std::function<void(ModalDialog *)> &handle);

    std::unique_ptr<MainWindow> m_window;
    MindMapModel *m_model = nullptr;
    ShortcutRegistry *m_registry = nullptr;
};

void ShortcutsUiTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisShortcutsUiTest"));
    // Clean settings (CI) would bring the tour invitation and the views' tips, which take the keys.
    TourSettings::setTourOffered();
    TourSettings::setTipsEnabled(false);
    QSettings().remove(QStringLiteral("shortcuts"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Performance::setMode(Performance::Mode::Light);
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1400, 900);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    qobject_cast<QStackedWidget *>(m_window->centralWidget())->setCurrentIndex(1);
    m_model = m_window->findChild<MindMapModel *>();
    m_registry = m_window->findChild<ShortcutRegistry *>();
    QVERIFY(m_model && m_registry);
}

void ShortcutsUiTest::cleanupTestCase() {
    m_window.reset();
    QSettings().remove(QStringLiteral("shortcuts"));
}

void ShortcutsUiTest::init() { m_registry->resetAll(); }

void ShortcutsUiTest::onNextModal(const std::function<void(ModalDialog *)> &handle) {
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

QAction *ShortcutsUiTest::action(const QString &text) const {
    for (auto *candidate : m_window->findChildren<QAction *>())
        if (candidate->text() == text) return candidate;
    return nullptr;
}

QWidget *ShortcutsUiTest::row(QWidget *page, const QString &id) const {
    return page->findChild<QWidget *>(QStringLiteral("ShortcutRow_") + id);
}

QKeySequenceEdit *ShortcutsUiTest::edit(QWidget *page, const QString &id, int index) const {
    QWidget *widget = row(page, id);
    if (!widget) return nullptr;
    const auto edits = widget->findChildren<QKeySequenceEdit *>();
    return edits.value(index);
}

void ShortcutsUiTest::actionsChangeImmediately() {
    QAction *save = action(QStringLiteral("Save"));
    QVERIFY(save);
    QCOMPARE(save->shortcut(), QKeySequence(QKeySequence::Save));
    m_registry->setShortcuts(QStringLiteral("save"), {QKeySequence(QStringLiteral("Ctrl+K"))});
    QCOMPARE(save->shortcuts(), QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+K"))});
    QAction *redo = action(QStringLiteral("Redo"));
    QCOMPARE(redo->shortcuts().size(), 2);  // Ctrl+Shift+Z e Ctrl+Y predefinite
    QAction *editText = action(QStringLiteral("Edit text"));
    QCOMPARE(editText->shortcuts(), QList<QKeySequence>{QKeySequence(Qt::Key_F2)});
    QAction *editAtEnd = action(QStringLiteral("Edit text at the end"));
    QCOMPARE(editAtEnd->shortcuts(), QList<QKeySequence>{QKeySequence(Qt::Key_Space)});
}

void ShortcutsUiTest::outlinerKeysComeFromTheRegistry() {
    while (!m_model->root()->children.empty()) m_model->removeNode(m_model->root()->children.front().get());
    m_model->addChild(m_model->root(), QStringLiteral("A"));
    m_model->addChild(m_model->root(), QStringLiteral("B"));
    QApplication::processEvents();
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner")) { button->click(); break; }
    QTreeView *tree = nullptr;
    for (auto *candidate : m_window->findChildren<QTreeView *>())
        if (candidate->isVisible() && candidate->model() == m_model) tree = candidate;
    QVERIFY(tree);
    m_registry->setShortcuts(QStringLiteral("outlineIndent"), {QKeySequence(QStringLiteral("Ctrl+Right"))});
    tree->setFocus();
    const auto selectB = [&] {
        MindNode *b = m_model->root()->children.size() > 1 ? m_model->root()->children.at(1).get() : nullptr;
        QVERIFY(b);
        tree->selectionModel()->setCurrentIndex(m_model->indexFromNode(b), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    };
    selectB();
    QTest::keyClick(tree, Qt::Key_Tab);  // non e' piu' il rientro
    QApplication::processEvents();
    QCOMPARE(m_model->root()->children.size(), size_t(2));
    QTest::keyClick(tree, Qt::Key_Right, Qt::ControlModifier);
    QApplication::processEvents();
    QCOMPARE(m_model->root()->children.size(), size_t(1));
    QCOMPARE(m_model->root()->children.front()->children.size(), size_t(1));
    // Il menu contestuale mostra il tasto scelto.
    QString shown;
    QTimer::singleShot(0, this, [&] {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            for (QAction *item : menu->actions())
                if (item->text() == QStringLiteral("Indent")) shown = item->shortcut().toString(QKeySequence::PortableText);
            menu->close();
        }
    });
    const QModelIndex first = m_model->indexFromNode(m_model->root()->children.front().get());
    emit tree->customContextMenuRequested(tree->visualRect(first).center());
    QApplication::processEvents();
    QCOMPARE(shown, QStringLiteral("Ctrl+Right"));
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Mind map")) { button->click(); break; }
}

void ShortcutsUiTest::pageShowsGroupsAndFilters() {
    SettingsDialog dialog(m_window.get(), m_registry);
    dialog.showShortcutsPage();
    QWidget *page = dialog.findChild<QWidget *>(QStringLiteral("ShortcutsPage"));
    QVERIFY(page);
    QStringList groups;
    for (auto *label : page->findChildren<QLabel *>(QStringLiteral("shortcutGroup"))) groups.append(label->text());
    QCOMPARE(groups, (QStringList{QStringLiteral("General"), QStringLiteral("Map"), QStringLiteral("Outliner"), QStringLiteral("Gantt")}));
    QVERIFY(row(page, QStringLiteral("save")) && row(page, QStringLiteral("addChild")) && row(page, QStringLiteral("outlineIndent")));
    QCOMPARE(edit(page, QStringLiteral("redo"), 1)->keySequence(), QKeySequence(QStringLiteral("Ctrl+Y")));
    dialog.show();
    auto *search = page->findChild<QLineEdit *>(QStringLiteral("ShortcutSearch"));
    QVERIFY(search);
    search->setText(QStringLiteral("indent"));
    QVERIFY(row(page, QStringLiteral("outlineIndent"))->isVisible());
    QVERIFY(!row(page, QStringLiteral("save"))->isVisible());
    QStringList visibleGroups;
    for (auto *label : page->findChildren<QLabel *>(QStringLiteral("shortcutGroup")))
        if (label->isVisible()) visibleGroups.append(label->text());
    QCOMPARE(visibleGroups, QStringList{QStringLiteral("Outliner")});
    search->setText(QStringLiteral("Ctrl+L"));  // anche per tasto
    QVERIFY(row(page, QStringLiteral("layout"))->isVisible());
    search->clear();
    QVERIFY(row(page, QStringLiteral("save"))->isVisible());
}

void ShortcutsUiTest::conflictAsksAndReplaceMovesTheKey() {
    SettingsDialog dialog(m_window.get(), m_registry);
    dialog.showShortcutsPage();
    QWidget *page = dialog.findChild<QWidget *>(QStringLiteral("ShortcutsPage"));
    // Ctrl+S (Salva, Generale) su Duplica (Outliner): conflitto; prima Annulla, poi Sostituisci.
    QString message;
    onNextModal([&](ModalDialog *modal) {
        for (auto *label : modal->findChildren<QLabel *>()) if (label->text().contains(QStringLiteral("already used"))) message = label->text();
        modal->reject();
    });
    edit(page, QStringLiteral("outlineDuplicate"), 0)->setKeySequence(QKeySequence(QKeySequence::Save));
    QTRY_VERIFY_WITH_TIMEOUT(!message.isEmpty(), 2000);
    QVERIFY(message.contains(QStringLiteral("\"Save\"")));
    QCOMPARE(m_registry->shortcuts(QStringLiteral("outlineDuplicate")), QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+D"))});
    QCOMPARE(edit(page, QStringLiteral("outlineDuplicate"), 0)->keySequence(), QKeySequence(QStringLiteral("Ctrl+D")));  // campo ripristinato
    onNextModal([](ModalDialog *modal) { modal->acceptButton()->click(); });
    edit(page, QStringLiteral("outlineDuplicate"), 0)->setKeySequence(QKeySequence(QKeySequence::Save));
    QTRY_VERIFY_WITH_TIMEOUT(!m_registry->shortcuts(QStringLiteral("outlineDuplicate")).isEmpty() &&
                             m_registry->shortcuts(QStringLiteral("outlineDuplicate")).front() == QKeySequence(QKeySequence::Save), 2000);
    QCOMPARE(m_registry->shortcuts(QStringLiteral("outlineDuplicate")).front(), QKeySequence(QKeySequence::Save));
    QVERIFY(m_registry->shortcuts(QStringLiteral("save")).isEmpty());  // tolto a Salva
    QVERIFY(edit(page, QStringLiteral("save"), 0)->keySequence().isEmpty());
    // Tab in Mappa e nell'outliner convivono: nessuna domanda.
    bool asked = false;
    onNextModal([&](ModalDialog *modal) { asked = true; modal->reject(); });
    edit(page, QStringLiteral("addChild"), 1)->setKeySequence(QKeySequence(Qt::Key_Backspace));  // usato solo nell'Outliner (Riduci rientro)
    QTRY_COMPARE_WITH_TIMEOUT(m_registry->shortcuts(QStringLiteral("addChild")).size(), 2, 2000);
    QVERIFY(!asked);
    // Ripristina tutte.
    for (auto *button : page->findChildren<QPushButton *>(QStringLiteral("shortcutRestoreAll"))) button->click();
    QVERIFY(m_registry->isDefault(QStringLiteral("save")));
    QCOMPARE(edit(page, QStringLiteral("save"), 0)->keySequence(), QKeySequence(QKeySequence::Save));
}

void ShortcutsUiTest::shortcutsMenuOpensTheSettingsPage() {
    QString page;
    QTimer::singleShot(0, this, [&] {
        if (auto *dialog = qobject_cast<SettingsDialog *>(QApplication::activeModalWidget())) {
            if (auto *list = dialog->findChild<QListWidget *>(); list && list->currentItem()) page = list->currentItem()->text();
            dialog->reject();
        }
    });
    action(QStringLiteral("Shortcuts…"))->trigger();
    QCOMPARE(page, QStringLiteral("Shortcuts"));
}

// Space ("Edit text at the end", FEAT-041) opens the title with the cursor after the last character.
void ShortcutsUiTest::spaceOnTheMapStartsEditingAndTypesInside() {
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("Uno"));
    auto *view = m_window->findChild<MindMapView *>();
    auto *scene = qobject_cast<MindMapScene *>(view->scene());
    scene->autoLayout();
    QApplication::processEvents();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    scene->selectNode(node);
    view->setFocus();
    NodeItem *item = nullptr;
    for (auto *candidate : scene->items())
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(candidate); nodeItem && nodeItem->node() == node) item = nodeItem;
    QVERIFY(item);
    QTest::keyClick(view->viewport(), Qt::Key_Space);
    QApplication::processEvents();
    QVERIFY(item->isEditing());
    // Inside the field Space is a space; the old title stays and the text goes after it.
    QTest::keyClicks(view->viewport(), QStringLiteral("a b"));
    QTest::keyClick(view->viewport(), Qt::Key_Return);
    QApplication::processEvents();
    QVERIFY(!item->isEditing());
    QCOMPARE(node->title, QStringLiteral("Unoa b"));
    // Bug 2026-11-04: after Enter, Space opened the title again only the first time.
    for (int round = 0; round < 3; ++round) {
        QTest::keyClick(view->viewport(), Qt::Key_Space);
        QApplication::processEvents();
        QVERIFY2(item->isEditing(), qPrintable(QString::number(round)));
        QTest::keyClick(view->viewport(), Qt::Key_Return);
        QApplication::processEvents();
        QVERIFY(!item->isEditing());
    }
    // Typing on the topic after Enter replaces the title again (FEAT-002).
    QTest::keyClicks(view->viewport(), QStringLiteral("x"));
    QTest::keyClick(view->viewport(), Qt::Key_Return);
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("x"));
    // BUG-042: while the title is typed, Ctrl+C copies its text, not the branch.
    QApplication::clipboard()->clear();
    QTest::keyClick(view->viewport(), Qt::Key_Space);
    QApplication::processEvents();
    QVERIFY(item->isEditing());
    QTest::keyClick(view->viewport(), Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(view->viewport(), Qt::Key_C, Qt::ControlModifier);
    QApplication::processEvents();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("x"));
    QVERIFY(!QApplication::clipboard()->mimeData()->hasFormat(QStringLiteral("application/x-mentis-subtree")));
    QTest::keyClick(view->viewport(), Qt::Key_Escape);
    QApplication::processEvents();
}

// FEAT-041 stage B: standard commands that Mentis has, with the keys of the table in the card.
void ShortcutsUiTest::standardRowsHaveTheirDefaults() {
    using Area = ShortcutRegistry::Area;
    const auto keys = [](std::initializer_list<const char *> texts) {
        QList<QKeySequence> list;
        for (const char *text : texts) list.append(QKeySequence(QString::fromLatin1(text)));
        return list;
    };
    const QList<std::tuple<const char *, Area, QList<QKeySequence>>> rows{
        {"home", Area::General, keys({"Ctrl+Shift+H", "Ctrl+H"})},
        {"bold", Area::General, keys({"Ctrl+B"})},  // FEAT-095
        {"italic", Area::General, keys({"Ctrl+I"})},
        {"underline", Area::General, keys({"Ctrl+U"})},
        {"strikeOut", Area::General, keys({"Ctrl+Shift+X"})},
        {"newSheet", Area::General, keys({"Ctrl+Alt+N"})},
        {"prevSheet", Area::General, keys({"Ctrl+Shift+["})},
        {"nextSheet", Area::General, keys({"Ctrl+Shift+]"})},
        {"toggleOutliner", Area::General, keys({"Ctrl+Shift+M"})},
        {"ganttView", Area::General, keys({"Ctrl+Alt+G"})},
        {"zoomIn", Area::General, keys({"Ctrl++"})},
        {"zoomOut", Area::General, keys({"Ctrl+-"})},
        {"actualSize", Area::General, {}},
        {"fit", Area::General, keys({"Ctrl+0"})},
        {"panelStyle", Area::General, keys({"Ctrl+]"})},
        {"panelPitch", Area::General, {}},
        {"panelMap", Area::General, {}},
        {"toggleSidePanel", Area::General, keys({"Ctrl+Alt+S"})},
        {"toggleIconPanel", Area::General, keys({"Ctrl+Alt+M"})},
        {"present", Area::General, keys({"F5", "Ctrl+Alt+P"})},
        {"settings", Area::General, keys({"Ctrl+,", "Ctrl+Shift+P"})},
        {"branchToSheet", Area::Map, {}},
        {"resetStyle", Area::Map, keys({"Ctrl+Alt+0"})},
        {"relation", Area::Map, keys({"Ctrl+Shift+R"})},
        {"boundary", Area::Map, keys({"Ctrl+Shift+B"})},
        {"summary", Area::Map, {}},
        {"note", Area::Map, keys({"Ctrl+Shift+N"})},
        {"labels", Area::Map, keys({"Ctrl+Shift+L"})},
        {"webLink", Area::Map, keys({"Ctrl+K"})},
        {"topicLink", Area::Map, {}},
        {"attachFiles", Area::Map, {}},
        {"markers", Area::Map, {}},
        {"stickers", Area::Map, {}},
        {"formula", Area::Map, {}},
        {"removeMarkers", Area::Map, {}},
        {"removeVisual", Area::Map, {}},
        {"removeLinks", Area::Map, {}},
        {"addTask", Area::Map, keys({"Ctrl+Alt+T"})},
        {"removeTask", Area::Map, {}},
        {"taskDone", Area::Map, {}},
        {"taskNotDone", Area::Map, {}},
        {"upperCase", Area::Map, {}},
        {"lowerCase", Area::Map, {}},
        {"alignLeft", Area::Map, {}},
        {"alignCenter", Area::Map, {}},
        {"alignRight", Area::Map, {}},
        {"fontLarger", Area::Map, keys({"Ctrl+Shift+."})},
        {"fontSmaller", Area::Map, keys({"Ctrl+Shift+,"})},
        {"numberingNone", Area::Map, {}},
        {"numbering123", Area::Map, {}},
        {"numberingABC", Area::Map, {}},
        {"numberingabc", Area::Map, {}},
        {"numberingRoman", Area::Map, {}},
        {"goToCentral", Area::Map, keys({"Ctrl+R"})},
        {"selectSubtopics", Area::Map, {}},
        {"selectSiblings", Area::Map, {}},
        {"selectSameLevel", Area::Map, {}},
        {"selectFloating", Area::Map, {}},
        {"selectRelationships", Area::Map, {}},
        {"selectSummaries", Area::Map, {}},
        {"selectBoundaries", Area::Map, {}},
        {"selectImages", Area::Map, {}},
        {"selectEquations", Area::Map, {}},
        {"toggleCollapse", Area::Map, keys({"Ctrl+/"})},
        {"toggleSubBranches", Area::Map, keys({"Ctrl+Alt+/"})},
        {"moveUp", Area::Map, keys({"Alt+Up"})},  // FEAT-086
        {"moveDown", Area::Map, keys({"Alt+Down"})},
        {"topicBefore", Area::Map, keys({"Shift+Return"})},
        {"parentTopic", Area::Map, keys({"Ctrl+Return"})},
        {"presetVeryImportant", Area::Map, keys({"Shift+Alt+1"})},
        {"presetImportant", Area::Map, keys({"Shift+Alt+2"})},
        {"presetCrossedOut", Area::Map, keys({"Shift+Alt+3"})},
        {"mapCopy", Area::Map, keys({"Ctrl+C"})},  // BUG-042
        {"mapCut", Area::Map, keys({"Ctrl+X"})},
        {"mapPaste", Area::Map, keys({"Ctrl+V"})},
        {"bringToFront", Area::Map, {}},
        {"bringForward", Area::Map, {}},
        {"sendBackward", Area::Map, {}},
        {"sendToBack", Area::Map, {}},
    };
    for (const auto &[id, area, defaults] : rows) {
        const QString name = QString::fromLatin1(id);
        QVERIFY2(m_registry->contains(name), id);
        QVERIFY2(m_registry->command(name).area == area, id);
        QVERIFY2(m_registry->command(name).defaults == defaults, id);
    }
}

void ShortcutsUiTest::defaultKeysHaveNoConflicts() {
    for (const QString &id : m_registry->ids())
        for (const QKeySequence &key : m_registry->command(id).defaults)
            QVERIFY2(m_registry->conflict(id, key).isEmpty(),
                     qPrintable(id + QLatin1Char(' ') + key.toString() + QStringLiteral(" used by ") + m_registry->conflict(id, key)));
}

void ShortcutsUiTest::standardKeysAct() {
    while (!m_model->root()->children.empty()) m_model->removeNode(m_model->root()->children.front().get());
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("Uno"));
    auto *view = m_window->findChild<MindMapView *>();
    auto *scene = qobject_cast<MindMapScene *>(view->scene());
    scene->autoLayout();
    QApplication::processEvents();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    const auto press = [&](Qt::Key key, Qt::KeyboardModifiers modifiers) {
        QTest::keyClick(view->viewport(), key, modifiers);
        QApplication::processEvents();
    };
    scene->selectNode(node);
    view->setFocus();

    const qreal size = node->fontSize;
    press(Qt::Key_Period, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(node->fontSize > size);
    press(Qt::Key_R, Qt::ControlModifier);
    QCOMPARE(scene->selectedNodes(), QList<MindNode *>{m_model->root()});
    // A row without a default runs once a key is given to it.
    scene->selectNode(node);
    m_registry->setShortcuts(QStringLiteral("alignRight"), {QKeySequence(QStringLiteral("Ctrl+Shift+F9"))});
    press(Qt::Key_F9, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(node->textAlign, TextAlign::Destra);
    press(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(m_window->noteEditor()->isVisible());
    m_window->noteEditor()->hide();

    // Sheets: Ctrl+Alt+N adds one and shows it, Ctrl+Shift+[ and ] move between them.
    auto *document = m_window->findChild<Document *>();
    QVERIFY(document);
    const qsizetype sheets = document->sheets().size();
    view->setFocus();
    press(Qt::Key_N, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(document->sheets().size(), sheets + 1);
    const int added = document->currentSheet();
    press(Qt::Key_BracketLeft, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(document->currentSheet(), added - 1);
    press(Qt::Key_BracketRight, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(document->currentSheet(), added);

    press(Qt::Key_BracketRight, Qt::ControlModifier);
    auto *dock = m_window->findChild<QWidget *>(QStringLiteral("InspectorDock"));
    QVERIFY(dock && dock->isVisible());
    QCOMPARE(m_window->findChild<SidePanel *>(QStringLiteral("InspectorSidePanel"))->currentPage(), 0);  // Style
    press(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    QVERIFY(m_window->findChild<GanttView *>()->isVisible());
}

// 2026-11-05: Ctrl+Alt+S opens and closes the side panel of the view shown (Format panel on the map,
// task panel in the Gantt), Ctrl+Alt+M the markers and stickers panel of the map.
void ShortcutsUiTest::panelKeysToggle() {
    auto *view = m_window->findChild<MindMapView *>();
    const auto showMap = [this] {
        for (auto *button : m_window->findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("Mind map") && button->isVisibleTo(m_window.get())) { button->click(); break; }
        QApplication::processEvents();
    };
    showMap();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    view->setFocus();
    const auto press = [](QWidget *target, Qt::Key key) {
        QTest::keyClick(target, key, Qt::ControlModifier | Qt::AltModifier);
        QApplication::processEvents();
    };
    auto *dock = m_window->findChild<QWidget *>(QStringLiteral("InspectorDock"));
    if (dock && dock->isVisible()) press(view->viewport(), Qt::Key_S);  // start closed
    dock = m_window->findChild<QWidget *>(QStringLiteral("InspectorDock"));
    QVERIFY(!dock || !dock->isVisible());

    press(view->viewport(), Qt::Key_S);
    dock = m_window->findChild<QWidget *>(QStringLiteral("InspectorDock"));
    QVERIFY(dock && dock->isVisible());
    auto *style = m_window->findChild<QToolButton *>(QStringLiteral("PanelToggle"));
    auto *icons = m_window->findChild<QToolButton *>(QStringLiteral("IconPanelToggle"));
    QVERIFY(style->isChecked());
    press(view->viewport(), Qt::Key_S);
    QVERIFY(!dock->isVisible());

    press(view->viewport(), Qt::Key_M);
    QVERIFY(dock->isVisible());
    QVERIFY(icons->isChecked());
    QVERIFY(!style->isChecked());
    press(view->viewport(), Qt::Key_M);
    QVERIFY(!dock->isVisible());

    press(view->viewport(), Qt::Key_G);  // Gantt chart
    auto *gantt = m_window->findChild<GanttView *>();
    QVERIFY(gantt->isVisible());
    const bool shown = gantt->panel()->isVisible();
    press(gantt, Qt::Key_S);
    QCOMPARE(gantt->panel()->isVisible(), !shown);
    press(gantt, Qt::Key_S);
    QCOMPARE(gantt->panel()->isVisible(), shown);
    press(gantt, Qt::Key_M);  // the Gantt has no markers panel
    QVERIFY(!dock->isVisible());
    showMap();
}

// Q3: the topic commands are the map's; with the focus in the outliner Ctrl+Shift+N does nothing.
void ShortcutsUiTest::topicKeysAreMapOnly() {
    while (!m_model->root()->children.empty()) m_model->removeNode(m_model->root()->children.front().get());
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("A"));
    QApplication::processEvents();
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner") && button->isVisibleTo(m_window.get())) { button->click(); break; }
    QTreeView *tree = nullptr;
    for (auto *candidate : m_window->findChildren<QTreeView *>())
        if (candidate->isVisible() && candidate->model() == m_model) tree = candidate;
    QVERIFY(tree);
    m_window->findChild<MindMapScene *>()->selectNode(node);
    tree->setCurrentIndex(m_model->indexFromNode(node));
    tree->setFocus();
    QTest::keyClick(tree, Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::processEvents();
    QVERIFY(!m_window->noteEditor()->isVisible());
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Mind map") && button->isVisibleTo(m_window.get())) { button->click(); break; }
}

// Q4: the menus show the keys, and follow the Shortcuts page.
void ShortcutsUiTest::menusShowTheKeys() {
    QAction *note = nullptr;
    for (auto *menu : m_window->findChildren<QMenu *>())
        if (menu->title() == QStringLiteral("Insert"))
            for (QAction *entry : menu->actions())
                if (entry->text().startsWith(QStringLiteral("Note\t"))) note = entry;
    QVERIFY(note);
    QCOMPARE(note->text(), QStringLiteral("Note\t") + QKeySequence(QStringLiteral("Ctrl+Shift+N")).toString(QKeySequence::NativeText));
    m_registry->setShortcuts(QStringLiteral("note"), {});
    QCOMPARE(note->text(), QStringLiteral("Note"));
    m_registry->reset(QStringLiteral("note"));
    bool relation = false;
    for (auto *candidate : m_window->findChildren<QAction *>())
        relation |= candidate->text() == QStringLiteral("Relationship") && candidate->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+R"));
    QVERIFY(relation);
    QVERIFY(action(QStringLiteral("Bring forward"))->shortcuts().isEmpty());  // Ctrl+] is the Style panel's
}

// FEAT-086: Alt+Down, Shift+Enter, Ctrl+Enter and Shift+Alt+1 on the map.
void ShortcutsUiTest::standardTopicKeysAct() {
    while (!m_model->root()->children.empty()) m_model->removeNode(m_model->root()->children.front().get());
    MindNode *child = m_model->addChild(m_model->root(), QStringLiteral("A"));
    MindNode *a1 = m_model->addChild(child, QStringLiteral("A1"));
    m_model->addChild(child, QStringLiteral("A2"));
    auto *view = m_window->findChild<MindMapView *>();
    auto *scene = qobject_cast<MindMapScene *>(view->scene());
    scene->autoLayout();
    QApplication::processEvents();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    const auto press = [&](Qt::Key key, Qt::KeyboardModifiers modifiers) {
        QTest::keyClick(view->viewport(), key, modifiers);
        QApplication::processEvents();
    };
    scene->selectNode(a1);
    view->setFocus();
    press(Qt::Key_Down, Qt::AltModifier);
    QCOMPARE(child->children.back().get(), a1);
    press(Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(child->children.size(), size_t(3));
    QCOMPARE(child->children[2].get(), a1);
    QCOMPARE(scene->selectedNodes(), QList<MindNode *>{child->children[1].get()});
    scene->selectNode(a1);
    press(Qt::Key_Return, Qt::ControlModifier);
    MindNode *parent = child->children[2].get();
    QVERIFY(parent != a1);
    QCOMPARE(parent->children.front().get(), a1);
    press(Qt::Key_1, Qt::ShiftModifier | Qt::AltModifier);
    QCOMPARE(parent->preset, NodePreset::MoltoImportante);

    // BUG-042: Ctrl+C then Ctrl+V on another topic pastes the branch under it; Ctrl+X removes it.
    MindNode *a2 = child->children[1].get();
    scene->selectNode(parent);
    press(Qt::Key_C, Qt::ControlModifier);
    scene->selectNode(a2);
    press(Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(a2->children.size(), size_t(1));
    QCOMPARE(a2->children.front()->children.size(), size_t(1));
    QCOMPARE(a2->children.front()->children.front()->title, QStringLiteral("A1"));
    scene->selectNode(a2->children.front().get());
    press(Qt::Key_X, Qt::ControlModifier);
    QVERIFY(a2->children.empty());
    scene->selectNode(child);
    press(Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(child->children.back()->children.front()->title, QStringLiteral("A1"));

    // FEAT-042 (2026-11-05): Ctrl+C with several topics copies all of them; Ctrl+V pastes copies
    // with their descendants under the selected topic. Ctrl+X cuts them all.
    MindNode *b = m_model->addChild(m_model->root(), QStringLiteral("B"));
    scene->autoLayout();
    QApplication::processEvents();
    scene->selectNode(child);
    for (QGraphicsItem *item : scene->items())
        if (auto *node = qgraphicsitem_cast<NodeItem *>(item); node && node->node() == b) node->setSelected(true);
    QCOMPARE(scene->selectedNodes().size(), 2);
    press(Qt::Key_C, Qt::ControlModifier);
    MindNode *target = child->children.front().get();
    const size_t before = target->children.size();
    scene->selectNode(target);
    press(Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(target->children.size(), before + 2);
    QCOMPARE(target->children[before]->title, QStringLiteral("A"));
    QCOMPARE(target->children[before]->children.size(), child->children.size());  // a copy of A as it was
    QCOMPARE(target->children[before + 1]->title, QStringLiteral("B"));
}

namespace {
// Counts the shortcut events an action receives and keeps them, so its command does not run.
class ShortcutCatcher final : public QObject {
public:
    using QObject::QObject;
    int count = 0;
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() != QEvent::Shortcut) return false;
        ++count;
        return true;
    }
};
}  // namespace

// 2026-11-04: a key given in Settings to Import/Export or Add image did nothing (the action was only
// behind a proxy menu entry or in the hidden menu bar). Every General and Map command acts on its key.
void ShortcutsUiTest::everyAssignedKeyActs() {
    auto *view = m_window->findChild<MindMapView *>();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    const QKeySequence key(QStringLiteral("Ctrl+Alt+Shift+F12"));
    for (const ShortcutRegistry::Area area : {ShortcutRegistry::Area::General, ShortcutRegistry::Area::Map}) {
        for (const ShortcutRegistry::Command &command : m_registry->commands(area)) {
            QAction *found = nullptr;
            for (auto *candidate : m_window->findChildren<QAction *>(Qt::FindDirectChildrenOnly))
                if (candidate->text() == command.name) found = candidate;
            if (!found) continue;  // keys handled by the map's event filter (arrows)
            const bool enabled = found->isEnabled();
            found->setEnabled(true);  // Undo and Redo with an empty history
            ShortcutCatcher catcher;
            found->installEventFilter(&catcher);
            m_registry->setShortcuts(command.id, {key});
            view->setFocus();
            QTest::keyClick(view->viewport(), Qt::Key_F12, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
            found->removeEventFilter(&catcher);
            found->setEnabled(enabled);
            m_registry->reset(command.id);
            QVERIFY2(catcher.count == 1, qPrintable(command.id));
        }
    }
}

// F1 opens the manual from a dialog, but a shortcut field being recorded takes it.
void ShortcutsUiTest::helpKeyIsRecordedInSettings() {
    SettingsDialog dialog(m_window.get(), m_registry);
    dialog.showShortcutsPage();
    dialog.setModal(true);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QWidget *page = dialog.findChild<QWidget *>(QStringLiteral("ShortcutsPage"));
    m_registry->setShortcuts(QStringLiteral("help"), {QKeySequence(Qt::Key_F1)});
    QKeySequenceEdit *field = edit(page, QStringLiteral("help"), 1);
    QVERIFY(field);
    field->setFocus();
    QTest::keyClick(field, Qt::Key_F1);
    QCOMPARE(field->keySequence(), QKeySequence(Qt::Key_F1));
    QVERIFY(!m_window->manualWindow());
    dialog.reject();
}

namespace {
using RichTitle::CharStyle;
RichTitle::BaseStyle baseOf(const MindNode *node) { return {node->fontWeight, node->italic, node->underline, node->strikeOut}; }
bool everywhere(const MindNode *node, CharStyle style) {
    return RichTitle::hasEverywhere(node->title, node->titleHtml, style, baseOf(node), NodeItem::boldWeightFor(node));
}
// The style of the character at position in the saved title.
bool styledAt(const MindNode *node, int position, CharStyle style) {
    QTextDocument document;
    document.setHtml(RichTitle::withoutDocumentFont(node->titleHtml));
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (position < fragment.position() || position >= fragment.position() + fragment.length()) continue;
            const QTextCharFormat format = fragment.charFormat();
            switch (style) {
            case CharStyle::Bold: return format.fontWeight() >= NodeItem::boldWeightFor(node);
            case CharStyle::Italic: return format.fontItalic();
            case CharStyle::Underline: return format.fontUnderline();
            case CharStyle::StrikeOut: return format.fontStrikeOut();
            }
        }
    return false;
}
void select(MindMapScene *scene, const QList<MindNode *> &nodes) {
    scene->clearSelection();
    for (QGraphicsItem *item : scene->items())
        if (auto *node = dynamic_cast<NodeItem *>(item); node && nodes.contains(node->node())) node->setSelected(true);
}
}  // namespace

// FEAT-095: Ctrl+B on selected topics formats every character they have, in one undo step.
void ShortcutsUiTest::charKeysFormatTheWholeTopic() {
    while (!m_model->root()->children.empty()) m_model->removeNode(m_model->root()->children.front().get());
    MindNode *one = m_model->addChild(m_model->root(), QStringLiteral("Uno"));
    MindNode *two = m_model->addChild(m_model->root(), QStringLiteral("Due tre"));
    auto *view = m_window->findChild<MindMapView *>();
    auto *scene = qobject_cast<MindMapScene *>(view->scene());
    auto *document = m_window->findChild<Document *>();
    scene->autoLayout();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    const auto press = [&](Qt::Key key, Qt::KeyboardModifiers modifiers) {
        QTest::keyClick(view->viewport(), key, modifiers);
        QApplication::processEvents();
    };
    // One of the two bold already: the key makes both bold, then both normal.
    scene->selectNode(one);
    view->setFocus();
    press(Qt::Key_B, Qt::ControlModifier);
    QVERIFY(everywhere(one, CharStyle::Bold));
    select(scene, {one, two});
    QCOMPARE(scene->selectedNodes().size(), 2);
    const qreal width = scene->nodeSceneRect(two).width();
    press(Qt::Key_B, Qt::ControlModifier);
    QVERIFY(everywhere(one, CharStyle::Bold) && everywhere(two, CharStyle::Bold));
    QVERIFY(two->fontWeight < QFont::Bold);  // the topic's style is untouched
    QVERIFY(scene->nodeSceneRect(two).width() > width);  // measured again: bold is wider
    // Undo rebuilds the sheet's topics: they are found again after it.
    const auto topics = [this](MindNode *&first, MindNode *&second) {
        first = m_model->root()->children[0].get();
        second = m_model->root()->children[1].get();
    };
    document->undo();
    topics(one, two);
    QVERIFY(everywhere(one, CharStyle::Bold) && !everywhere(two, CharStyle::Bold));  // one step for both
    document->redo();
    topics(one, two);
    QApplication::processEvents();
    select(scene, {one, two});
    press(Qt::Key_B, Qt::ControlModifier);
    QVERIFY(!everywhere(one, CharStyle::Bold) && !everywhere(two, CharStyle::Bold));
    const std::tuple<Qt::Key, Qt::KeyboardModifiers, CharStyle> keys[] = {
        {Qt::Key_I, Qt::ControlModifier, CharStyle::Italic},
        {Qt::Key_U, Qt::ControlModifier, CharStyle::Underline},
        {Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier, CharStyle::StrikeOut}};
    for (const auto &[key, modifiers, style] : keys) {
        press(key, modifiers);
        QVERIFY2(everywhere(two, style), qPrintable(QString::number(int(style))));
    }
    QVERIFY(!two->underline && !two->italic && !two->strikeOut);
}

// FEAT-095: with text selected in the map's title only that text changes.
void ShortcutsUiTest::charKeysFormatTheSelectionOnTheMap() {
    while (!m_model->root()->children.empty()) m_model->removeNode(m_model->root()->children.front().get());
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("uno due"));
    auto *view = m_window->findChild<MindMapView *>();
    auto *scene = qobject_cast<MindMapScene *>(view->scene());
    scene->autoLayout();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    scene->selectNode(node);
    view->setFocus();
    QTest::keyClick(view->viewport(), Qt::Key_F2);
    auto *editor = dynamic_cast<QGraphicsTextItem *>(scene->focusItem());
    QVERIFY(editor);
    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(0);
    cursor.setPosition(3, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    QTest::keyClick(view->viewport(), Qt::Key_I, Qt::ControlModifier);
    QTest::keyClick(view->viewport(), Qt::Key_Return);
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("uno due"));
    QVERIFY(styledAt(node, 0, CharStyle::Italic));
    QVERIFY(!styledAt(node, 5, CharStyle::Italic));
}

// FEAT-095: a bold topic opened with Space: the text typed at the end is normal.
void ShortcutsUiTest::textTypedAfterOpeningIsNormal() {
    while (!m_model->root()->children.empty()) m_model->removeNode(m_model->root()->children.front().get());
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("uno"));
    auto *view = m_window->findChild<MindMapView *>();
    auto *scene = qobject_cast<MindMapScene *>(view->scene());
    scene->autoLayout();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    scene->selectNode(node);
    view->setFocus();
    QTest::keyClick(view->viewport(), Qt::Key_B, Qt::ControlModifier);
    QVERIFY(everywhere(node, CharStyle::Bold));
    QTest::keyClick(view->viewport(), Qt::Key_Space);
    QTest::keyClicks(view->viewport(), QStringLiteral(" due"));
    QTest::keyClick(view->viewport(), Qt::Key_Return);
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("uno due"));
    QVERIFY(styledAt(node, 0, CharStyle::Bold));
    QVERIFY(!styledAt(node, 4, CharStyle::Bold));
    QVERIFY(!styledAt(node, 6, CharStyle::Bold));
    // The normal format joins the typing's step: one Ctrl+Z goes back to the bold "uno".
    m_window->findChild<Document *>()->undo();
    node = m_model->root()->children.front().get();  // undo rebuilds the sheet's topics
    QCOMPARE(node->title, QStringLiteral("uno"));
    QVERIFY(everywhere(node, CharStyle::Bold));
    // Typing on the selected topic replaces its title (FEAT-002): the new title is normal, one step.
    QApplication::processEvents();
    scene->selectNode(node);
    view->setFocus();
    QTest::keyClicks(view->viewport(), QStringLiteral("tre"));
    QTest::keyClick(view->viewport(), Qt::Key_Return);
    QApplication::processEvents();
    QCOMPARE(node->title, QStringLiteral("tre"));
    QVERIFY(!styledAt(node, 0, CharStyle::Bold) && !styledAt(node, 2, CharStyle::Bold));
    m_window->findChild<Document *>()->undo();
    node = m_model->root()->children.front().get();
    QCOMPARE(node->title, QStringLiteral("uno"));
}

// FEAT-095: in the outliner on the selected item, and on the text selected in its field.
void ShortcutsUiTest::charKeysInTheOutliner() {
    while (!m_model->root()->children.empty()) m_model->removeNode(m_model->root()->children.front().get());
    MindNode *node = m_model->addChild(m_model->root(), QStringLiteral("uno due"));
    QApplication::processEvents();
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Outliner") && button->isVisibleTo(m_window.get())) { button->click(); break; }
    QTreeView *tree = nullptr;
    for (auto *candidate : m_window->findChildren<QTreeView *>())
        if (candidate->isVisible() && candidate->model() == m_model) tree = candidate;
    QVERIFY(tree);
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
    const QModelIndex index = m_model->indexFromNode(node);
    tree->setCurrentIndex(index);
    tree->setFocus();
    QTest::keyClick(tree, Qt::Key_U, Qt::ControlModifier);
    QApplication::processEvents();
    QVERIFY(everywhere(node, CharStyle::Underline));
    tree->edit(index);
    QApplication::processEvents();
    auto *field = qobject_cast<QPlainTextEdit *>(QApplication::focusWidget());
    QVERIFY(field);
    QTextCursor cursor = field->textCursor();
    cursor.setPosition(4);
    cursor.setPosition(7, QTextCursor::KeepAnchor);
    field->setTextCursor(cursor);
    QApplication::processEvents();
    QTest::keyClick(field, Qt::Key_B, Qt::ControlModifier);
    QTest::keyClick(field, Qt::Key_Return);
    QApplication::processEvents();
    QVERIFY(styledAt(node, 5, CharStyle::Bold));
    QVERIFY(!styledAt(node, 1, CharStyle::Bold));
    QVERIFY(everywhere(node, CharStyle::Underline));
    for (auto *button : m_window->findChildren<QPushButton *>())
        if (button->text() == QStringLiteral("Mind map") && button->isVisibleTo(m_window.get())) { button->click(); break; }
}

QTEST_MAIN(ShortcutsUiTest)
#include "shortcuts_ui_test.moc"
