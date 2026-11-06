// FEAT-041: registro delle scorciatoie senza widget (predefinite, scelte salvate, conflitti per area,
// ripristino, tasti premuti). Impostazioni di prova separate.
#include "shortcuts.h"

#include <QKeyEvent>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

class ShortcutsTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void defaultsAndSavedChoices();
    void atMostTwoWithoutDuplicates();
    void conflictsArePerArea();
    void resetRestoresDefaults();
    void pressedKeysMatch();

private:
    void registerCommands(ShortcutRegistry &registry) const;
};

void ShortcutsTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisShortcutsTest"));
}

void ShortcutsTest::init() { QSettings().remove(QStringLiteral("shortcuts")); }

void ShortcutsTest::registerCommands(ShortcutRegistry &registry) const {
    using Area = ShortcutRegistry::Area;
    registry.add(QStringLiteral("save"), Area::General, QStringLiteral("Save"), {QKeySequence(QStringLiteral("Ctrl+S"))});
    registry.add(QStringLiteral("redo"), Area::General, QStringLiteral("Redo"),
                 {QKeySequence(QStringLiteral("Ctrl+Shift+Z")), QKeySequence(QStringLiteral("Ctrl+Y"))});
    registry.add(QStringLiteral("addChild"), Area::Map, QStringLiteral("Add child"), {QKeySequence(Qt::Key_Tab)});
    registry.add(QStringLiteral("outlineIndent"), Area::Outliner, QStringLiteral("Indent"), {QKeySequence(Qt::Key_Tab)});
    registry.add(QStringLiteral("outlineOutdent"), Area::Outliner, QStringLiteral("Outdent"),
                 {QKeySequence(QStringLiteral("Shift+Tab")), QKeySequence(Qt::Key_Backspace)});
    registry.add(QStringLiteral("outlineCopy"), Area::Outliner, QStringLiteral("Copy"), {QKeySequence(QStringLiteral("Ctrl+C"))});
}

void ShortcutsTest::defaultsAndSavedChoices() {
    ShortcutRegistry registry;
    registerCommands(registry);
    QCOMPARE(registry.shortcuts(QStringLiteral("redo")).size(), 2);
    QVERIFY(registry.isDefault(QStringLiteral("save")));
    QSignalSpy changed(&registry, &ShortcutRegistry::changed);
    registry.setShortcuts(QStringLiteral("save"), {QKeySequence(QStringLiteral("Ctrl+K"))});
    QCOMPARE(changed.size(), 1);
    QCOMPARE(registry.shortcuts(QStringLiteral("save")), QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+K"))});
    QVERIFY(!registry.isDefault(QStringLiteral("save")));
    // Un altro registro (nuovo avvio) rilegge la scelta.
    ShortcutRegistry again;
    registerCommands(again);
    QCOMPARE(again.shortcuts(QStringLiteral("save")), QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+K"))});
    // Nessun tasto: stringa vuota, diversa dalle predefinite.
    registry.setShortcuts(QStringLiteral("save"), {});
    QVERIFY(registry.shortcuts(QStringLiteral("save")).isEmpty());
    // Formato di prima: un solo tasto salvato come testo.
    QSettings().setValue(QStringLiteral("shortcuts/redo"), QStringLiteral("Ctrl+R"));
    QCOMPARE(registry.shortcuts(QStringLiteral("redo")), QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+R"))});
}

void ShortcutsTest::atMostTwoWithoutDuplicates() {
    ShortcutRegistry registry;
    registerCommands(registry);
    registry.setShortcuts(QStringLiteral("save"), {QKeySequence(), QKeySequence(QStringLiteral("Ctrl+1")),
                                                   QKeySequence(QStringLiteral("Ctrl+1")), QKeySequence(QStringLiteral("Ctrl+2")),
                                                   QKeySequence(QStringLiteral("Ctrl+3"))});
    QCOMPARE(registry.shortcuts(QStringLiteral("save")),
             (QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+1")), QKeySequence(QStringLiteral("Ctrl+2"))}));
}

void ShortcutsTest::conflictsArePerArea() {
    ShortcutRegistry registry;
    registerCommands(registry);
    // Tab sulla mappa e nell'outliner: aree diverse, nessun conflitto.
    QVERIFY(registry.conflict(QStringLiteral("addChild"), QKeySequence(Qt::Key_Tab)).isEmpty());
    // Stessa area.
    QCOMPARE(registry.conflict(QStringLiteral("outlineCopy"), QKeySequence(Qt::Key_Tab)), QStringLiteral("outlineIndent"));
    // Un comando Generale vale ovunque.
    QCOMPARE(registry.conflict(QStringLiteral("outlineCopy"), QKeySequence(QStringLiteral("Ctrl+S"))), QStringLiteral("save"));
    QCOMPARE(registry.conflict(QStringLiteral("save"), QKeySequence(QStringLiteral("Ctrl+C"))), QStringLiteral("outlineCopy"));
    QVERIFY(registry.conflict(QStringLiteral("save"), QKeySequence(QStringLiteral("Ctrl+S"))).isEmpty());  // se' stesso
}

void ShortcutsTest::resetRestoresDefaults() {
    ShortcutRegistry registry;
    registerCommands(registry);
    registry.setShortcuts(QStringLiteral("save"), {QKeySequence(QStringLiteral("Ctrl+K"))});
    registry.setShortcuts(QStringLiteral("outlineCopy"), {});
    registry.reset(QStringLiteral("save"));
    QVERIFY(registry.isDefault(QStringLiteral("save")));
    QVERIFY(!registry.isDefault(QStringLiteral("outlineCopy")));
    QSignalSpy changed(&registry, &ShortcutRegistry::changed);
    registry.resetAll();
    QCOMPARE(changed.size(), 1);  // solo il comando che non era predefinito
    QVERIFY(registry.isDefault(QStringLiteral("outlineCopy")));
}

void ShortcutsTest::pressedKeysMatch() {
    ShortcutRegistry registry;
    registerCommands(registry);
    const QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
    QVERIFY(registry.matches(QStringLiteral("outlineOutdent"), &backtab));
    const QKeyEvent backspace(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    QVERIFY(registry.matches(QStringLiteral("outlineOutdent"), &backspace));
    const QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QVERIFY(!registry.matches(QStringLiteral("outlineOutdent"), &tab));
    QVERIFY(registry.matches(QStringLiteral("outlineIndent"), &tab));
    const QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QVERIFY(registry.matches(QStringLiteral("outlineCopy"), &copy));
    registry.setShortcuts(QStringLiteral("outlineIndent"), {QKeySequence(QStringLiteral("Ctrl+Right"))});
    QVERIFY(!registry.matches(QStringLiteral("outlineIndent"), &tab));
}

QTEST_MAIN(ShortcutsTest)
#include "shortcuts_test.moc"
