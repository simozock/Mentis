// FEAT-035 parte A: finestra Impostazioni scura a schede, scelte applicate subito, interruttore.
// Impostazioni separate, piattaforma offscreen.
#include "mainwindow.h"
#include "mcpmaptools.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "performance.h"
#include "settingsdialog.h"
#include "shortcuts.h"
#include "toggleswitch.h"
#include "uitheme.h"
#include "updates.h"

#include <QApplication>
#include <QLabel>
#include <QClipboard>
#include <QTemporaryDir>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QImage>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTest>
#include <QToolButton>
#include <memory>

class SettingsUiTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void tabsWithoutOkOrCancel();
    void toggleAppliesAtOnceWithClickAndSpace();
    void choicesAreSavedAtOnce();
    void agentsAreOffUntilAllowed();
    void updatesCard();
    void editorDefaultsRestoreTheStyle();
    void appearanceAppliesAtOnceAndDefaultsReset();
    void escapeAndCloseButtonClose();
    void snapshotForVisualReview();
    void mapLooksTheSameAtEverySize();
    void windowSnapshotsAtEverySize();

private:
    // The real window on the map page with a small map; the scene drawn into an image.
    struct Window {
        std::unique_ptr<MainWindow> window;
        MindMapScene *scene = nullptr;
    };
    static Window openWindow();
    static QImage drawMap(MindMapScene *scene);
};

SettingsUiTest::Window SettingsUiTest::openWindow() {
    Window result;
    result.window = std::make_unique<MainWindow>();
    result.window->resize(1400, 900);
    result.window->show();
    if (!QTest::qWaitForWindowExposed(result.window.get())) return result;
    qobject_cast<QStackedWidget *>(result.window->centralWidget())->setCurrentIndex(1);
    auto *model = result.window->findChild<MindMapModel *>();
    result.scene = qobject_cast<MindMapScene *>(result.window->findChild<MindMapView *>()->scene());
    model->clear(QStringLiteral("Central topic"));
    for (const QString &title : {QStringLiteral("Main topic 1"), QStringLiteral("Main topic 2")}) {
        MindNode *child = model->addChild(model->root(), title);
        model->addChild(child, QStringLiteral("Subtopic"));
    }
    result.scene->autoLayout();
    QApplication::processEvents();
    return result;
}

QImage SettingsUiTest::drawMap(MindMapScene *scene) {
    QApplication::processEvents();
    const QRectF area = scene->itemsBoundingRect();
    QImage image(area.size().toSize(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    scene->render(&painter, QRectF(QPointF(), area.size()), area);
    return image;
}

void SettingsUiTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisSettingsUiTest"));
}

void SettingsUiTest::init() {
    QSettings().clear();
    NodeItem::selectionStyle() = NodeItem::SelectionStyle{};
    UiTheme::setAppearance({});
}

void SettingsUiTest::tabsWithoutOkOrCancel() {
    SettingsDialog dialog;
    auto *tabs = dialog.findChild<QListWidget *>(QStringLiteral("SettingsTabs"));
    QVERIFY(tabs);
    QStringList names;
    for (int i = 0; i < tabs->count(); ++i) {
        names.append(tabs->item(i)->text());
        QVERIFY(!tabs->item(i)->icon().isNull());
    }
    QCOMPARE(names, (QStringList{QStringLiteral("General"), QStringLiteral("Appearance"), QStringLiteral("Editor")}));  // senza registro niente Scorciatoie
    QVERIFY(!dialog.findChild<QDialogButtonBox *>());
    for (auto *button : dialog.findChildren<QPushButton *>()) {
        QVERIFY(button->text() != QStringLiteral("OK"));
        QVERIFY(button->text() != QStringLiteral("Cancel"));
    }
    QVERIFY(dialog.windowFlags().testFlag(Qt::FramelessWindowHint));
}

void SettingsUiTest::toggleAppliesAtOnceWithClickAndSpace() {
    SettingsDialog dialog;
    QSignalSpy changed(&dialog, &SettingsDialog::selectionStyleChanged);
    auto *toggle = dialog.findChild<ToggleSwitch *>(QStringLiteral("HoverBorderSwitch"));
    QVERIFY(toggle);
    QCOMPARE(toggle->size(), QSize(ToggleSwitch::kTrackWidthPx + 4, ToggleSwitch::kTrackHeightPx + 4));
    const bool before = NodeItem::selectionStyle().hoverEnabled;
    QCOMPARE(toggle->isChecked(), before);
    dialog.show();
    QTest::mouseClick(toggle, Qt::LeftButton);
    QCOMPARE(NodeItem::selectionStyle().hoverEnabled, !before);
    QCOMPARE(QSettings().value(QStringLiteral("selection/hover")).toBool(), !before);
    QCOMPARE(changed.count(), 1);
    toggle->setFocus();
    QTest::keyClick(toggle, Qt::Key_Space);
    QCOMPARE(NodeItem::selectionStyle().hoverEnabled, before);
    QCOMPARE(changed.count(), 2);
}

// FEAT-045: Settings > General > Agents (MCP): off by default, the switch saved at once (mentis-mcp
// reads it at every call), no folders to choose, turning it on adds Mentis to the agents' apps (here
// a stand-in: the real apps' files are never touched), Defaults turns agents off, the configuration
// for other apps goes to the clipboard.
void SettingsUiTest::agentsAreOffUntilAllowed() {
    SettingsDialog dialog;
    int connected = 0;
    dialog.connectAgentApps = [&connected] {
        ++connected;
        return QList<McpClients::Outcome>{{QStringLiteral("Claude Desktop"), true, {}}, {QStringLiteral("Codex"), false, QStringLiteral("disk full")}};
    };
    auto *agents = dialog.findChild<ToggleSwitch *>(QStringLiteral("AgentsSwitch"));
    QVERIFY(agents);
    QVERIFY(!agents->isChecked());
    QVERIFY(!McpMapTools::Access::load().enabled);
    auto *again = dialog.findChild<QPushButton *>(QStringLiteral("AgentsConnectApps"));
    QVERIFY(again && !again->isEnabled());
    agents->setChecked(true);
    QVERIFY(McpMapTools::Access::load().enabled);
    QCOMPARE(connected, 1);
    QVERIFY(again->isEnabled());
    const QString status = again->parentWidget()->parentWidget()->findChild<QLabel *>(QStringLiteral("settingsHint"))->text();
    QVERIFY2(status.contains(QStringLiteral("Claude Desktop")) && status.contains(QStringLiteral("disk full")), qPrintable(status));
    // Codex failed: the configuration is on the clipboard for the user to paste.
    QVERIFY(status.contains(QStringLiteral("paste")));
    QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("mcpServers")));
    // No app found: copied as well, whatever the agent.
    QGuiApplication::clipboard()->clear();
    dialog.connectAgentApps = [&connected] { ++connected; return QList<McpClients::Outcome>{}; };
    again->click();
    QCOMPARE(connected, 2);
    QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("--mcp")));
    QVERIFY(again->parentWidget()->parentWidget()->findChild<QLabel *>(QStringLiteral("settingsHint"))->text().contains(QStringLiteral("No agent app")));
    QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("AgentsAddFolder")));
    dialog.findChild<QPushButton *>(QStringLiteral("AgentsCopyConfiguration"))->click();
    const QString configuration = QGuiApplication::clipboard()->text();
    QVERIFY(configuration.contains(QStringLiteral("mcpServers")));
    QVERIFY(configuration.contains(QStringLiteral("--mcp")));
    // The General page's Defaults: the one on the same page as the switch.
    for (QPushButton *button : dialog.findChildren<QPushButton *>(QStringLiteral("SettingsDefaults")))
        if (button->parentWidget()->isAncestorOf(agents)) { button->click(); break; }
    QVERIFY(!agents->isChecked());
    QVERIFY(!McpMapTools::Access::load().enabled);
}

// FEAT-090: Settings > General > Updates: on by default, saved at once, Defaults turns it back on;
// "Check now" asks the window (which answers over the dialog); the last check under the version.
void SettingsUiTest::updatesCard() {
    Updates::Settings saved = Updates::Settings::load();
    saved.lastCheck = QDateTime();
    saved.save();
    SettingsDialog dialog;
    auto *lastCheck = dialog.findChild<QLabel *>(QStringLiteral("UpdatesLastCheck"));
    QVERIFY(lastCheck);
    QCOMPARE(lastCheck->text(), QStringLiteral("Not checked yet"));
    const QDateTime when(QDate(2026, 11, 6), QTime(11, 0));
    saved.lastCheck = when;
    saved.save();
    dialog.refreshLastUpdateCheck();
    QCOMPARE(lastCheck->text(), QStringLiteral("Last check: ") + QLocale().toString(when, QLocale::ShortFormat));
    auto *updates = dialog.findChild<ToggleSwitch *>(QStringLiteral("UpdatesSwitch"));
    QVERIFY(updates);
    QVERIFY(updates->isChecked());
    updates->setChecked(false);
    QVERIFY(!Updates::Settings::load().checkAtStartup);
    QSignalSpy requested(&dialog, &SettingsDialog::checkForUpdatesRequested);
    dialog.findChild<QPushButton *>(QStringLiteral("UpdatesCheckNow"))->click();
    QCOMPARE(requested.count(), 1);
    for (QPushButton *button : dialog.findChildren<QPushButton *>(QStringLiteral("SettingsDefaults")))
        if (button->parentWidget()->isAncestorOf(updates)) { button->click(); break; }
    QVERIFY(updates->isChecked());
    QVERIFY(Updates::Settings::load().checkAtStartup);
}

void SettingsUiTest::choicesAreSavedAtOnce() {
    SettingsDialog dialog;
    QSignalSpy performance(&dialog, &SettingsDialog::performanceModeChanged);
    auto *profile = dialog.findChild<QComboBox *>(QStringLiteral("PerformanceSelect"));
    QVERIFY(profile);
    profile->setCurrentIndex(profile->findData(static_cast<int>(Performance::Mode::Light)));
    QCOMPARE(Performance::mode(), Performance::Mode::Light);
    QCOMPARE(performance.count(), 1);
    auto *width = dialog.findChild<QComboBox *>(QStringLiteral("SelectionWidth"));
    QVERIFY(width);
    width->setCurrentIndex(width->findData(4));
    QCOMPARE(qRound(NodeItem::selectionStyle().width), 4);
    QCOMPARE(QSettings().value(QStringLiteral("selection/width")).toInt(), 4);
    Performance::setMode(Performance::Mode::Automatic);
}

void SettingsUiTest::editorDefaultsRestoreTheStyle() {
    NodeItem::selectionStyle().gap = 7;
    SettingsDialog dialog;
    QPushButton *defaults = nullptr;
    for (auto *button : dialog.findChildren<QPushButton *>(QStringLiteral("SettingsDefaults")))
        if (button->parentWidget()->isAncestorOf(dialog.findChild<QComboBox *>(QStringLiteral("SelectionGap"))))
            defaults = button;  // quello della scheda Editor (anche Generale ha un interruttore, FEAT-045)
    QVERIFY(defaults);
    defaults->click();
    QCOMPARE(NodeItem::selectionStyle().gap, NodeItem::SelectionStyle{}.gap);
    QCOMPARE(dialog.findChild<QComboBox *>(QStringLiteral("SelectionGap"))->currentData().toInt(), qRound(NodeItem::SelectionStyle{}.gap));
}

// FEAT-035 part B: font, size and density apply without restart and are saved; Defaults resets them.
void SettingsUiTest::appearanceAppliesAtOnceAndDefaultsReset() {
    UiTheme::loadAppearance();  // the filter that scales the style sheets, as in main()
    SettingsDialog dialog;
    auto *family = dialog.findChild<QComboBox *>(QStringLiteral("FontFamilySelect"));
    auto *size = dialog.findChild<QComboBox *>(QStringLiteral("FontSizeSelect"));
    QVERIFY(family && size);
    QCOMPARE(family->count(), UiTheme::bundledFontFamilies().size() + 2);  // default first, system font last
    QCOMPARE(size->currentData().toInt(), 14);
    size->setCurrentIndex(size->findData(18));
    family->setCurrentIndex(family->findData(QStringLiteral("Roboto")));
    QCOMPARE(UiTheme::appearance().fontSize, 18);
    QCOMPARE(dialog.font().family(), QStringLiteral("Roboto"));
    QCOMPARE(QSettings().value(QStringLiteral("ui/fontSize")).toInt(), 18);
    auto *density = dialog.findChild<QWidget *>(QStringLiteral("DensityControl"));
    QVERIFY(density);
    const QList<QToolButton *> segments = density->findChildren<QToolButton *>();
    QCOMPARE(segments.size(), 2);
    segments.at(1)->click();
    QCOMPARE(UiTheme::appearance().density, UiTheme::Density::Compact);
    QCOMPARE(QSettings().value(QStringLiteral("ui/density")).toString(), QStringLiteral("compact"));
    QVERIFY(dialog.styleSheet().contains(QLatin1String("font-size:18px")));  // the dialog's own 14 px
    QPushButton *defaults = nullptr;
    for (auto *button : dialog.findChildren<QPushButton *>(QStringLiteral("SettingsDefaults")))
        if (button->parentWidget()->findChild<QComboBox *>(QStringLiteral("FontSizeSelect"))) defaults = button;
    QVERIFY(defaults);
    // At 18 px the controls grow with their text: none is lower than its text line.
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    auto *tabs = dialog.findChild<QListWidget *>(QStringLiteral("SettingsTabs"));
    QVERIFY2(tabs->font().pointSizeF() > QApplication::font().pointSizeF(), qPrintable(QString::number(tabs->font().pointSizeF())));
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentRow(i);
        QCoreApplication::processEvents();
        for (QWidget *control : dialog.findChildren<QWidget *>()) {
            if (!control->isVisible() || !(qobject_cast<QComboBox *>(control) || qobject_cast<QPushButton *>(control))) continue;
            QVERIFY2(control->height() >= control->fontMetrics().height() + 8, qPrintable(control->objectName()));
        }
    }
    // The segments too, and the density track as tall as the size combo beside it (bug 2026-11-01).
    for (QToolButton *segment : segments) QVERIFY(segment->height() >= segment->fontMetrics().height() + 4);
    QCOMPARE(density->height(), size->height());
    defaults->click();
    QCOMPARE(UiTheme::appearance(), UiTheme::Appearance{});
    QCOMPARE(size->currentData().toInt(), 14);
    QCOMPARE(family->currentIndex(), 0);
}

void SettingsUiTest::escapeAndCloseButtonClose() {
    SettingsDialog dialog;
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QTest::keyClick(&dialog, Qt::Key_Escape);
    QVERIFY(!dialog.isVisible());
    dialog.show();
    auto *close = dialog.findChild<QToolButton *>(QStringLiteral("SettingsClose"));
    QVERIFY(close);
    close->click();
    QVERIFY(!dialog.isVisible());
}

// Foto delle schede per il confronto a vista con il riferimento (FEAT-035, "Prove"): solo con
// MENTIS_SNAPSHOT_DIR, da lanciare con una piattaforma che ha i caratteri (es. windows).
void SettingsUiTest::snapshotForVisualReview() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR non impostata");
    ShortcutRegistry registry;
    using Area = ShortcutRegistry::Area;
    registry.add(QStringLiteral("save"), Area::General, QStringLiteral("Save"), {QKeySequence(QKeySequence::Save)});
    registry.add(QStringLiteral("undo"), Area::General, QStringLiteral("Undo"), {QKeySequence(QKeySequence::Undo)});
    registry.add(QStringLiteral("addChild"), Area::Map, QStringLiteral("Add subtopic"), {QKeySequence(Qt::Key_Tab), QKeySequence(Qt::Key_Insert)});
    registry.add(QStringLiteral("outlineIndent"), Area::Outliner, QStringLiteral("Indent"), {QKeySequence(Qt::Key_Tab)});
    SettingsDialog dialog(nullptr, &registry);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    auto *tabs = dialog.findChild<QListWidget *>(QStringLiteral("SettingsTabs"));
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentRow(i);
        QApplication::processEvents();
        QVERIFY(dialog.grab().save(dir + QStringLiteral("/settings-%1.png").arg(i)));
    }
    // FEAT-035 part B: the same tabs with the largest text, then Compact.
    UiTheme::loadAppearance();
    UiTheme::setAppearance({QString(), UiTheme::kMaxFontSize, UiTheme::Density::Normal});
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentRow(i);
        QApplication::processEvents();
        QVERIFY(dialog.grab().save(dir + QStringLiteral("/settings-%1-18px.png").arg(i)));
    }
    UiTheme::setAppearance({QString(), UiTheme::kDefaultFontSize, UiTheme::Density::Compact});
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentRow(i);
        QApplication::processEvents();
        QVERIFY(dialog.grab().save(dir + QStringLiteral("/settings-%1-compact.png").arg(i)));
    }
    UiTheme::setAppearance({});
}

// FEAT-035 part B: the map (and so its exports, drawn the same way) does not follow the interface.
void SettingsUiTest::mapLooksTheSameAtEverySize() {
    UiTheme::loadAppearance();
    Window window = openWindow();
    QVERIFY(window.scene);
    const QImage today = drawMap(window.scene);
    QVERIFY(!today.isNull());
    for (const UiTheme::Appearance &appearance : {UiTheme::Appearance{QStringLiteral("Roboto"), 18, UiTheme::Density::Normal},
                                                   UiTheme::Appearance{QString(), 12, UiTheme::Density::Compact}}) {
        UiTheme::setAppearance(appearance);
        QVERIFY2(drawMap(window.scene) == today, qPrintable(QString::number(appearance.fontSize)));
    }
    UiTheme::setAppearance({});
}

// Pictures of the whole window for the visual check (only with MENTIS_SNAPSHOT_DIR).
void SettingsUiTest::windowSnapshotsAtEverySize() {
    const QString dir = qEnvironmentVariable("MENTIS_SNAPSHOT_DIR");
    if (dir.isEmpty()) QSKIP("MENTIS_SNAPSHOT_DIR non impostata");
    UiTheme::loadAppearance();
    Window window = openWindow();
    QVERIFY(window.scene);
    const struct {
        const char *name;
        UiTheme::Appearance appearance;
    } shots[] = {{"14px", {}}, {"12px", {QString(), 12, UiTheme::Density::Normal}}, {"18px", {QString(), 18, UiTheme::Density::Normal}},
                 {"14px-compact", {QString(), 14, UiTheme::Density::Compact}}, {"18px-compact", {QString(), 18, UiTheme::Density::Compact}}};
    for (const auto &shot : shots) {
        UiTheme::setAppearance(shot.appearance);
        QApplication::processEvents();
        QTest::qWait(200);
        QVERIFY(window.window->grab().save(dir + QStringLiteral("/window-%1.png").arg(QLatin1String(shot.name))));
    }
    UiTheme::setAppearance({});
}

QTEST_MAIN(SettingsUiTest)
#include "settings_ui_test.moc"
