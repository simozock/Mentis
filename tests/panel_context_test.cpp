// FEAT-017: scelta della pagina del pannello laterale, senza widget.
#include "panelcontext.h"

#include <QTest>

using Page = PanelContext::Page;

class PanelContextTest final : public QObject {
    Q_OBJECT

private slots:
    void startsOnMap() {
        PanelContext context;
        QCOMPARE(context.current(), Page::Map);
        QVERIFY(!context.styleControlsEnabled());
    }

    void selectionDecidesContextualPage() {
        PanelContext context;
        QCOMPARE(context.selectionChanged({}), Page::Map);
        QCOMPARE(context.selectionChanged({QStringLiteral("a")}), Page::Style);
        QVERIFY(context.styleControlsEnabled());
        QCOMPARE(context.selectionChanged({}), Page::Map);
        QVERIFY(!context.styleControlsEnabled());
    }

    void rootNodeIsAnOrdinaryNode() {
        PanelContext context;
        QCOMPARE(context.selectionChanged({QStringLiteral("root")}), Page::Style);
    }

    void manualChoiceLastsUntilSelectionChanges() {
        PanelContext context;
        context.selectionChanged({QStringLiteral("a")});
        QCOMPARE(context.manualChoice(Page::Map), Page::Map);
        // Stessa selezione notificata di nuovo (es. segnale ripetuto): la scelta resta.
        QCOMPARE(context.selectionChanged({QStringLiteral("a")}), Page::Map);
        QCOMPARE(context.selection(), QStringList{QStringLiteral("a")});
        // Nuovo nodo selezionato: torna Stile.
        QCOMPARE(context.selectionChanged({QStringLiteral("b")}), Page::Style);
        // Scelta manuale e poi deselezione completa: torna Mappa.
        QCOMPARE(context.manualChoice(Page::Pitch), Page::Pitch);
        QCOMPARE(context.selectionChanged({}), Page::Map);
    }

    void multipleSelectionOpensStyleWithControlsDisabled() {
        PanelContext context;
        QCOMPARE(context.selectionChanged({QStringLiteral("a"), QStringLiteral("b")}), Page::Style);
        QVERIFY(!context.styleControlsEnabled());  // fino a FEAT-019
        QCOMPARE(context.selectionChanged({QStringLiteral("a")}), Page::Style);
        QVERIFY(context.styleControlsEnabled());
    }

    void reopeningForgetsManualChoice() {
        PanelContext context;
        context.selectionChanged({QStringLiteral("a")});
        context.manualChoice(Page::Pitch);
        QCOMPARE(context.reopened({QStringLiteral("a")}), Page::Style);
        context.manualChoice(Page::Style);
        QCOMPARE(context.reopened({}), Page::Map);
    }

    void activeSheetDoesNotChangePageOrSelection() {
        PanelContext context;
        context.selectionChanged({QStringLiteral("a")});
        context.manualChoice(Page::Pitch);
        context.setActiveSheet(QStringLiteral("sheet-2"));
        QCOMPARE(context.activeSheet(), QStringLiteral("sheet-2"));
        QCOMPARE(context.current(), Page::Pitch);
        QCOMPARE(context.selection(), QStringList{QStringLiteral("a")});
    }
};

QTEST_GUILESS_MAIN(PanelContextTest)
#include "panel_context_test.moc"
