// FEAT-068 tappa 1: motore delle scorciatoie Markdown mentre si scrive, senza widget. Si scrive
// un carattere alla volta come farebbe l'utente e dopo ognuno si chiama afterTyping.
#include "markdownshortcuts.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>

using namespace MarkdownShortcuts;

class MarkdownShortcutsTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void boldItalicStrikeHighlightInTitles();
    void typingAfterTheShortcutIsPlain();
    void falsePositivesStayAsTyped();
    void undoBringsBackTheSymbols();
    void checkboxAtTheStartOfTheTitle();
    void notesHaveListsHeadingLinksAndCode();
    void codeAndLinksAreNotesOnly();
    void switchOffKeepsTheSymbols();
    void wholeDocumentForImportedNotes();
    void buttonsTurnHighlightAndCodeOff();

private:
    struct Typed {
        std::unique_ptr<QTextDocument> document = std::make_unique<QTextDocument>();
        Result last;
        int task = 0;
    };
    static Typed type(const QString &text, Context context = Context::Title);
    static QTextCharFormat formatAt(const QTextDocument &document, int position);
};

void MarkdownShortcutsTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisMarkdownShortcutsTest"));
    setEnabled(true);
}

MarkdownShortcutsTest::Typed MarkdownShortcutsTest::type(const QString &text, Context context) {
    Typed typed;
    QTextCursor cursor(typed.document.get());
    Options options;
    options.context = context;
    for (const QChar c : text) {
        cursor.insertText(QString(c));
        typed.last = afterTyping(cursor, c, options);
        if (typed.last.task) typed.task = typed.last.task;
    }
    return typed;
}

QTextCharFormat MarkdownShortcutsTest::formatAt(const QTextDocument &document, int position) {
    QTextCursor cursor(const_cast<QTextDocument *>(&document));
    cursor.setPosition(position + 1);  // formato del carattere in position
    return cursor.charFormat();
}

void MarkdownShortcutsTest::boldItalicStrikeHighlightInTitles() {
    Typed bold = type(QStringLiteral("un **ciao**"));
    QCOMPARE(bold.document->toPlainText(), QStringLiteral("un ciao"));
    QCOMPARE(formatAt(*bold.document, 3).fontWeight(), 700);
    QVERIFY(formatAt(*bold.document, 1).fontWeight() < 700);
    Typed italic = type(QStringLiteral("*a*"));
    QCOMPARE(italic.document->toPlainText(), QStringLiteral("a"));
    QVERIFY(formatAt(*italic.document, 0).fontItalic());
    Typed strike = type(QStringLiteral("~~via~~"));
    QCOMPARE(strike.document->toPlainText(), QStringLiteral("via"));
    QVERIFY(formatAt(*strike.document, 0).fontStrikeOut());
    Typed highlight = type(QStringLiteral("==nota bene=="));
    QCOMPARE(highlight.document->toPlainText(), QStringLiteral("nota bene"));
    QVERIFY(isHighlight(formatAt(*highlight.document, 2)));
}

void MarkdownShortcutsTest::typingAfterTheShortcutIsPlain() {
    Typed typed = type(QStringLiteral("**ciao** mondo"));
    QCOMPARE(typed.document->toPlainText(), QStringLiteral("ciao mondo"));
    QVERIFY(formatAt(*typed.document, 7).fontWeight() < 700);
    Typed mark = type(QStringLiteral("==a== b"));
    QVERIFY(!isHighlight(formatAt(*mark.document, 2)));
}

void MarkdownShortcutsTest::falsePositivesStayAsTyped() {
    for (const QString &text : {QStringLiteral("2 * 3 * 4"), QStringLiteral("file_nome_lungo"), QStringLiteral("a*b*"),
                                QStringLiteral("$a*b*$"), QStringLiteral("** ciao**"), QStringLiteral("x == y == z"),
                                QStringLiteral("****"), QStringLiteral("c++ e c**")}) {
        Typed typed = type(text);
        QCOMPARE(typed.document->toPlainText(), text);
    }
    // In un titolo "- " a inizio riga resta testo (gli elenchi sono solo nelle note).
    QCOMPARE(type(QStringLiteral("- uno")).document->toPlainText(), QStringLiteral("- uno"));
}

void MarkdownShortcutsTest::undoBringsBackTheSymbols() {
    Typed typed = type(QStringLiteral("**ciao**"));
    QCOMPARE(typed.document->toPlainText(), QStringLiteral("ciao"));
    typed.document->undo();
    QCOMPARE(typed.document->toPlainText(), QStringLiteral("**ciao**"));
}

void MarkdownShortcutsTest::checkboxAtTheStartOfTheTitle() {
    Typed open = type(QStringLiteral("- [ ] Ripassare"));
    QCOMPARE(open.document->toPlainText(), QStringLiteral("Ripassare"));
    QCOMPARE(open.task, 1);
    Typed done = type(QStringLiteral("[x] Fatto"));
    QCOMPARE(done.document->toPlainText(), QStringLiteral("Fatto"));
    QCOMPARE(done.task, 2);
    Typed middle = type(QStringLiteral("ok - [ ] no"));  // non a inizio titolo
    QCOMPARE(middle.task, 0);
    QCOMPARE(middle.document->toPlainText(), QStringLiteral("ok - [ ] no"));
}

void MarkdownShortcutsTest::notesHaveListsHeadingLinksAndCode() {
    Typed bullets = type(QStringLiteral("- primo"), Context::Note);
    QCOMPARE(bullets.document->toPlainText(), QStringLiteral("primo"));
    QVERIFY(bullets.document->firstBlock().textList());
    QCOMPARE(bullets.document->firstBlock().textList()->format().style(), QTextListFormat::ListDisc);
    Typed numbers = type(QStringLiteral("1. uno"), Context::Note);
    QCOMPARE(numbers.document->firstBlock().textList()->format().style(), QTextListFormat::ListDecimal);
    Typed heading = type(QStringLiteral("## Date"), Context::Note);
    QCOMPARE(heading.document->toPlainText(), QStringLiteral("Date"));
    QCOMPARE(heading.document->firstBlock().blockFormat().headingLevel(), 2);
    QCOMPARE(formatAt(*heading.document, 1).fontWeight(), int(QFont::Bold));
    Typed link = type(QStringLiteral("vedi [sito](example.com) qui"), Context::Note);
    QCOMPARE(link.document->toPlainText(), QStringLiteral("vedi sito qui"));
    QCOMPARE(formatAt(*link.document, 6).anchorHref(), QStringLiteral("https://example.com"));
    QVERIFY(!formatAt(*link.document, 11).isAnchor());  // il testo dopo non e' collegato
    Typed code = type(QStringLiteral("usa `ls -la` ora"), Context::Note);
    QCOMPARE(code.document->toPlainText(), QStringLiteral("usa ls -la ora"));
    QVERIFY(isCode(formatAt(*code.document, 5)));
    QVERIFY(!isCode(formatAt(*code.document, 12)));
    // Dentro il codice niente grassetto.
    Typed inside = type(QStringLiteral("`a **b** c`"), Context::Note);
    QVERIFY(inside.document->toPlainText().contains(QStringLiteral("**b**")) || isCode(formatAt(*inside.document, 0)));
}

void MarkdownShortcutsTest::codeAndLinksAreNotesOnly() {
    QCOMPARE(type(QStringLiteral("`x`")).document->toPlainText(), QStringLiteral("`x`"));
    QCOMPARE(type(QStringLiteral("[a](b.com)")).document->toPlainText(), QStringLiteral("[a](b.com)"));
    QCOMPARE(type(QStringLiteral("## Titolo")).document->toPlainText(), QStringLiteral("## Titolo"));
}

void MarkdownShortcutsTest::switchOffKeepsTheSymbols() {
    setEnabled(false);
    QCOMPARE(type(QStringLiteral("**ciao**")).document->toPlainText(), QStringLiteral("**ciao**"));
    setEnabled(true);
}

void MarkdownShortcutsTest::wholeDocumentForImportedNotes() {
    QTextDocument document;
    document.setPlainText(QStringLiteral("prima ==importante== e ==anche questo==\nfine"));
    Options options;
    options.context = Context::Note;
    QVERIFY(applyToDocument(&document, options));
    QCOMPARE(document.toPlainText(), QStringLiteral("prima importante e anche questo\nfine"));
    QVERIFY(isHighlight(formatAt(document, 7)));
    QVERIFY(isHighlight(formatAt(document, 20)));
}

// Il Markdown mette il formato, i bottoni lo tolgono (2026-10-19).
void MarkdownShortcutsTest::buttonsTurnHighlightAndCodeOff() {
    Typed typed = type(QStringLiteral("a ==bene== e `cmd` fine"), Context::Note);
    QCOMPARE(typed.document->toPlainText(), QStringLiteral("a bene e cmd fine"));
    QTextCursor cursor(typed.document.get());
    cursor.setPosition(0);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);  // anche testo senza formato
    setHighlight(cursor, false);
    setCode(cursor, false);
    for (int i = 0; i < int(typed.document->toPlainText().size()); ++i) {
        QVERIFY(!isHighlight(formatAt(*typed.document, i)));
        QVERIFY(!isCode(formatAt(*typed.document, i)));
        QCOMPARE(formatAt(*typed.document, i).background().style(), Qt::NoBrush);
        QCOMPARE(formatAt(*typed.document, i).foreground().style(), Qt::NoBrush);
    }
    typed.document->undo();  // un passo solo
    QVERIFY(isCode(formatAt(*typed.document, 10)));
    typed.document->undo();
    QVERIFY(isHighlight(formatAt(*typed.document, 3)));
    // Acceso dal bottone su una selezione.
    QTextCursor word(typed.document.get());
    word.setPosition(13);
    word.setPosition(17, QTextCursor::KeepAnchor);
    setHighlight(word, true);
    QVERIFY(isHighlight(formatAt(*typed.document, 14)));
}

QTEST_MAIN(MarkdownShortcutsTest)
#include "markdown_shortcuts_test.moc"
