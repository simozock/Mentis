// FEAT-055: conversione del Markdown in rami (incolla nell'outliner e "Importa Markdown…"), senza
// widget. Gli esempi sono quelli della scheda docs/features/FEAT-055-importazione.md.
#include "markdownimport.h"
#include "mindmapmodel.h"

#include <QJsonArray>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>

using MarkdownImport::Item;

namespace {
// Albero come testo rientrato di 2 spazi per livello, con le note fra parentesi quadre.
QString outline(const std::vector<Item> &items, int depth = 0) {
    QString out;
    for (const Item &item : items) {
        out += QString(depth * 2, QLatin1Char(' ')) + item.title;
        if (!item.notes.isEmpty()) out += QStringLiteral(" [") + item.notes + QLatin1Char(']');
        out += QLatin1Char('\n') + outline(item.children, depth + 1);
    }
    return out;
}

QString outline(const MindNode *node, int depth = 0) {
    QString out = QString(depth * 2, QLatin1Char(' ')) + node->title;
    if (!node->notes.isEmpty()) out += QStringLiteral(" [") + node->notes + QLatin1Char(']');
    out += QLatin1Char('\n');
    for (const auto &child : node->children) out += outline(child.get(), depth + 1);
    return out;
}

QString parsed(const QString &markdown) { return outline(MarkdownImport::parse(markdown).items); }

// Peso, corsivo e barrato del carattere at del titolo formattato.
QTextCharFormat formatAt(const QString &html, int at) {
    QTextDocument document;
    document.setHtml(html);
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (at >= fragment.position() && at < fragment.position() + fragment.length()) return fragment.charFormat();
        }
    return {};
}
}  // namespace

class MarkdownParseTest final : public QObject {
    Q_OBJECT

private slots:
    void indentationIsDetected();
    void irregularIndentGoesOneLevelDown();
    void headingsBecomeLevelsAndTextBecomesNotes();
    void inlineFormatsBecomeSpans();
    void minorRules();
    void plainTextUsesIndentation();
    void textBeforeFirstItemIsAnItem();
    void pasteLimit();
    void subtreeJsonCarriesFormats();
    void fileImportUsesTheSameRules();
    void exportedMarkdownRoundTrips();
};

void MarkdownParseTest::indentationIsDetected() {
    const QString expected = QStringLiteral("Biologia\n  Cellula\n    Membrana\n  Tessuti\n");
    QCOMPARE(parsed(QStringLiteral("- Biologia\n  - Cellula\n    - Membrana\n  - Tessuti\n")), expected);
    QCOMPARE(parsed(QStringLiteral("- Biologia\n    - Cellula\n        - Membrana\n    - Tessuti\n")), expected);
    QCOMPARE(parsed(QStringLiteral("- Biologia\n\t- Cellula\n\t\t- Membrana\n\t- Tessuti\n")), expected);
    QCOMPARE(parsed(QStringLiteral("* Biologia\r\n  + Cellula\r\n    - Membrana\r\n  * Tessuti\r\n")), expected);
}

void MarkdownParseTest::irregularIndentGoesOneLevelDown() {
    QCOMPARE(parsed(QStringLiteral("- A\n  - B\n        - C\n  - D\n")), QStringLiteral("A\n  B\n    C\n  D\n"));
    // 3 spazi con unita' 2: livello piu' vicino, mai oltre uno sotto la voce precedente.
    QCOMPARE(parsed(QStringLiteral("- A\n  - B\n   - C\n")), QStringLiteral("A\n  B\n    C\n"));
}

void MarkdownParseTest::headingsBecomeLevelsAndTextBecomesNotes() {
    const auto result = MarkdownImport::parse(QStringLiteral(
        "# Biologia\nStudio degli esseri viventi.\n## Cellula\n- **Membrana** plasmatica\n"
        "- Nucleo e *cromatina*\n## Tessuti\n"));
    QCOMPARE(outline(result.items), QStringLiteral(
        "Biologia [Studio degli esseri viventi.]\n  Cellula\n    Membrana plasmatica\n"
        "    Nucleo e cromatina\n  Tessuti\n"));
    QCOMPARE(result.itemCount, 5);
    // Titoli che partono da ##: il piu' alto e' il primo livello.
    QCOMPARE(parsed(QStringLiteral("## A\n### B\n## C\n")), QStringLiteral("A\n  B\nC\n"));
    // Due paragrafi di nota separati da una riga vuota; righe dello stesso paragrafo a capo.
    QCOMPARE(parsed(QStringLiteral("- A\n  prima riga\n  seconda\n\n  altro paragrafo\n")),
             QStringLiteral("A [prima riga\nseconda\n\naltro paragrafo]\n"));
}

void MarkdownParseTest::inlineFormatsBecomeSpans() {
    const auto result = MarkdownImport::parse(QStringLiteral("- **a** *b* _c_ ~~d~~ __e__ snake_case_word 2*3\n"));
    QCOMPARE(result.items.size(), size_t(1));
    const Item &item = result.items.front();
    QCOMPARE(item.title, QStringLiteral("a b c d e snake_case_word 2*3"));
    QCOMPARE(item.spans.size(), size_t(5));
    QVERIFY(item.spans[0].bold && item.spans[0].start == 0 && item.spans[0].length == 1);
    QVERIFY(item.spans[1].italic && item.spans[1].start == 2);
    QVERIFY(item.spans[2].italic && item.spans[2].start == 4);
    QVERIFY(item.spans[3].strikeOut && item.spans[3].start == 6);
    QVERIFY(item.spans[4].bold && item.spans[4].start == 8);
    // Formati annidati e simboli senza chiusura.
    const Item nested = MarkdownImport::parse(QStringLiteral("- **grassetto *e corsivo***\n- a ** b\n")).items.front();
    QCOMPARE(nested.title, QStringLiteral("grassetto e corsivo"));
    QCOMPARE(MarkdownImport::parse(QStringLiteral("- a ** b\n")).items.front().title, QStringLiteral("a ** b"));
    QCOMPARE(MarkdownImport::parse(QStringLiteral("- a \\*b\\*\n")).items.front().title, QStringLiteral("a *b*"));
}

void MarkdownParseTest::minorRules() {
    QCOMPARE(parsed(QStringLiteral("1. Primo\n2) Secondo\n   1. Dentro\n")), QStringLiteral("Primo\nSecondo\n  Dentro\n"));
    QCOMPARE(parsed(QStringLiteral("- [ ] Da fare\n- [x] Fatto\n")), QStringLiteral("Da fare\nFatto\n"));
    QCOMPARE(parsed(QStringLiteral("- Vedi [Wikipedia](https://it.wikipedia.org) e ![foto](a.png)\n")),
             QStringLiteral("Vedi Wikipedia e foto\n"));
    QCOMPARE(parsed(QStringLiteral("- A\n  ![foto](a.png)\n")), QStringLiteral("A\n"));
    QCOMPARE(parsed(QStringLiteral("- Area $\\pi r^2$ **qui**\n")), QStringLiteral("Area $\\pi r^2$ qui\n"));
    QVERIFY(MarkdownImport::toSubtreeJson(MarkdownImport::parse(QStringLiteral("- $x$ **b**\n")).items.front())
                .value(QStringLiteral("titleHtml")).toString().isEmpty());
    QCOMPARE(parsed(QStringLiteral("- Riga uno<br>riga due\n")), QStringLiteral("Riga uno\nriga due\n"));
    QCOMPARE(parsed(QStringLiteral("- A\n---\n***\n- B\n")), QStringLiteral("A\nB\n"));
    QCOMPARE(parsed(QStringLiteral("- A\n> citata **forte**\n```\nint x = *p;\n```\n")),
             QStringLiteral("A [citata forte\nint x = *p;]\n"));
    QCOMPARE(parsed(QStringLiteral("- A\n<!-- mindmap: fill=#ffffff -->\n")), QStringLiteral("A\n"));
}

void MarkdownParseTest::plainTextUsesIndentation() {
    QCOMPARE(parsed(QStringLiteral("Uno\n    Due\n        Tre\nQuattro\n")), QStringLiteral("Uno\n  Due\n    Tre\nQuattro\n"));
    QCOMPARE(parsed(QStringLiteral("solo una riga")), QStringLiteral("solo una riga\n"));
    QCOMPARE(parsed(QStringLiteral("\n  \n")), QString());
}

void MarkdownParseTest::textBeforeFirstItemIsAnItem() {
    QCOMPARE(parsed(QStringLiteral("Introduzione al corso\n\n- A\n  - B\n")), QStringLiteral("Introduzione al corso\nA\n  B\n"));
}

void MarkdownParseTest::pasteLimit() {
    QString markdown;
    for (int i = 0; i <= MarkdownImport::kMaxPastedItems; ++i) markdown += QStringLiteral("- voce %1\n").arg(i);
    const auto limited = MarkdownImport::parse(markdown, MarkdownImport::kMaxPastedItems);
    QVERIFY(limited.tooLarge);
    QVERIFY(limited.items.empty());
    const auto unlimited = MarkdownImport::parse(markdown);
    QVERIFY(!unlimited.tooLarge);
    QCOMPARE(unlimited.itemCount, MarkdownImport::kMaxPastedItems + 1);
}

void MarkdownParseTest::subtreeJsonCarriesFormats() {
    const auto result = MarkdownImport::parse(QStringLiteral("- **Membrana** e *nucleo*\n  Nota\n  - Figlio\n- Semplice\n"));
    const QJsonObject json = MarkdownImport::toSubtreeJson(result.items.front());
    QCOMPARE(json.value(QStringLiteral("title")).toString(), QStringLiteral("Membrana e nucleo"));
    QCOMPARE(json.value(QStringLiteral("notes")).toString(), QStringLiteral("Nota"));
    QCOMPARE(json.value(QStringLiteral("children")).toArray().size(), 1);
    const QString html = json.value(QStringLiteral("titleHtml")).toString();
    QVERIFY(!html.isEmpty());
    QCOMPARE(formatAt(html, 0).fontWeight(), int(QFont::Bold));
    QCOMPARE(formatAt(html, 9).fontWeight(), int(QFont::Normal));
    QVERIFY(formatAt(html, 12).fontItalic());
    // Il nodo centrale e' gia' in grassetto: grassetto 900, resto 700.
    const QString rootHtml = MarkdownImport::toSubtreeJson(result.items.front(), true).value(QStringLiteral("titleHtml")).toString();
    QCOMPARE(formatAt(rootHtml, 0).fontWeight(), int(QFont::Black));
    QCOMPARE(formatAt(rootHtml, 9).fontWeight(), int(QFont::Bold));
    QVERIFY(MarkdownImport::toSubtreeJson(result.items.back()).value(QStringLiteral("titleHtml")).toString().isEmpty());
}

void MarkdownParseTest::fileImportUsesTheSameRules() {
    MindMapModel model;
    QVERIFY(model.fromMarkdown(QStringLiteral(
        "# Biologia\nStudio degli esseri viventi.\n## Cellula\n- **Membrana** plasmatica\n## Tessuti\n")));
    QCOMPARE(outline(model.root()), QStringLiteral(
        "Biologia [Studio degli esseri viventi.]\n  Cellula\n    Membrana plasmatica\n  Tessuti\n"));
    QVERIFY(!model.root()->children.front()->children.front()->titleHtml.isEmpty());
    // Piu' voci di primo livello: la prima e' il nodo centrale, le altre diventano suoi figli.
    QVERIFY(model.fromMarkdown(QStringLiteral("- A\n  - A1\n- B\n- C\n")));
    QCOMPARE(outline(model.root()), QStringLiteral("A\n  A1\n  B\n  C\n"));
    QString error;
    QVERIFY(!model.fromMarkdown(QStringLiteral("\n<!-- solo un commento -->\n"), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(outline(model.root()), QStringLiteral("A\n  A1\n  B\n  C\n"));  // invariato dopo l'errore
}

void MarkdownParseTest::exportedMarkdownRoundTrips() {
    MindMapModel model;
    QVERIFY(model.fromMarkdown(QStringLiteral("- Centro\n  - Ramo\n    - Foglia\n  - Altro\n")));
    model.root()->children.front()->notes = QStringLiteral("Prima riga\nseconda riga");
    model.root()->children.back()->title = QStringLiteral("Due\nrighe");
    const QString before = outline(model.root());
    const QString exported = model.toMarkdown(true);
    QVERIFY(exported.contains(QStringLiteral("<!-- mindmap:")));
    MindMapModel reopened;
    QVERIFY(reopened.fromMarkdown(exported));
    QCOMPARE(outline(reopened.root()), before);
}

QTEST_MAIN(MarkdownParseTest)
#include "markdown_parse_test.moc"
