// FEAT-133: LaTeX braces closed while typing (BraceCompletion, without widgets). Each character is
// typed as a phone field does, then afterTyping (or afterDeleting for a Backspace) is called; the
// desktop's step over a "}" (stepOver) is tried on its own.
#include "bracecompletion.h"

#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

using namespace BraceCompletion;

class BraceCompletionTest final : public QObject {
    Q_OBJECT

private slots:
    void codeClosesAndStepsOver();
    void desktopStepsOverWithTheCursor();
    void backspaceRemovesTheEmptyPair();
    void textOnlyInFormulas();
    void escapedAndBeforeWordsStayAlone();
    void oneUndoTakesThePairBack();

private:
    struct Field {
        QTextDocument document;
        QTextCursor cursor{&document};
        Scope scope = Scope::Code;
        void type(const QString &text) {
            for (const QChar c : text) {
                cursor.insertText(QString(c));
                afterTyping(cursor, c, scope);
            }
        }
        void backspace() {
            const QChar removed = document.characterAt(cursor.position() - 1);
            cursor.deletePreviousChar();
            afterDeleting(cursor, removed, scope);
        }
        QString text() const { return document.toPlainText(); }
        int at() const { return cursor.position(); }
    };
};

void BraceCompletionTest::codeClosesAndStepsOver() {
    Field field;
    field.type(QStringLiteral("\\frac{"));
    QCOMPARE(field.text(), QStringLiteral("\\frac{}"));
    QCOMPARE(field.at(), 6);
    field.type(QStringLiteral("a}{b}"));
    QCOMPARE(field.text(), QStringLiteral("\\frac{a}{b}"));
    QCOMPARE(field.at(), 11);
    // "{{" then "}" steps over the inner one, a second "}" over the outer one.
    Field nested;
    nested.type(QStringLiteral("x^{{"));
    QCOMPARE(nested.text(), QStringLiteral("x^{{}}"));
    nested.type(QStringLiteral("}"));
    QCOMPARE(nested.text(), QStringLiteral("x^{{}}"));
    QCOMPARE(nested.at(), 5);
    nested.type(QStringLiteral("}"));
    QCOMPARE(nested.text(), QStringLiteral("x^{{}}"));
    QCOMPARE(nested.at(), 6);
    // A "}" an open "{" still needs is typed.
    Field open;
    open.cursor.insertText(QStringLiteral("{{a}"));
    open.cursor.setPosition(3);
    open.type(QStringLiteral("}"));
    QCOMPARE(open.text(), QStringLiteral("{{a}}"));
}

void BraceCompletionTest::desktopStepsOverWithTheCursor() {
    QTextDocument document(QStringLiteral("$x^{a}$"));
    QTextCursor cursor(&document);
    cursor.setPosition(5);
    QVERIFY(stepOver(cursor, Scope::Text));
    QCOMPARE(cursor.position(), 6);
    QCOMPARE(document.toPlainText(), QStringLiteral("$x^{a}$"));
    // Not when the "}" is still needed, nor outside a formula, nor after a backslash.
    document.setPlainText(QStringLiteral("{{a}"));
    cursor.setPosition(3);
    QVERIFY(!stepOver(cursor, Scope::Code));
    document.setPlainText(QStringLiteral("x^{a}"));
    cursor.setPosition(4);
    QVERIFY(!stepOver(cursor, Scope::Text));
    document.setPlainText(QStringLiteral("\\{a\\}"));
    cursor.setPosition(4);
    QVERIFY(!stepOver(cursor, Scope::Code));
}

void BraceCompletionTest::backspaceRemovesTheEmptyPair() {
    Field field;
    field.type(QStringLiteral("\\sqrt{"));
    field.backspace();
    QCOMPARE(field.text(), QStringLiteral("\\sqrt"));
    // Not when the brace holds something, nor when the "}" belongs to another "{".
    Field full;
    full.type(QStringLiteral("x^{a"));
    full.cursor.setPosition(3);
    full.backspace();
    QCOMPARE(full.text(), QStringLiteral("x^a}"));
    Field kept;
    kept.cursor.insertText(QStringLiteral("{{}"));
    kept.cursor.setPosition(2);
    kept.backspace();
    QCOMPARE(kept.text(), QStringLiteral("{}"));
}

void BraceCompletionTest::textOnlyInFormulas() {
    Field plain;
    plain.scope = Scope::Text;
    plain.type(QStringLiteral("set {"));
    QCOMPARE(plain.text(), QStringLiteral("set {"));
    // The formula being typed, not closed yet.
    Field typing;
    typing.scope = Scope::Text;
    typing.type(QStringLiteral("Area $x^{"));
    QCOMPARE(typing.text(), QStringLiteral("Area $x^{}"));
    typing.type(QStringLiteral("2}$ and \\(\\sqrt{"));
    QCOMPARE(typing.text(), QStringLiteral("Area $x^{2}$ and \\(\\sqrt{}"));
    // Inside a closed formula, also $$...$$; not in the text after it.
    Field closed;
    closed.scope = Scope::Text;
    closed.cursor.insertText(QStringLiteral("a $$x^$$ b {"));
    closed.cursor.setPosition(6);
    closed.type(QStringLiteral("{"));
    QCOMPARE(closed.text(), QStringLiteral("a $$x^{}$$ b {"));
    closed.cursor.movePosition(QTextCursor::End);
    closed.type(QStringLiteral("{"));
    QCOMPARE(closed.text(), QStringLiteral("a $$x^{}$$ b {{"));
    // A "$ " (a price) opens no formula; a formula on another line does not count.
    Field price;
    price.scope = Scope::Text;
    price.type(QStringLiteral("5 $ each {"));
    QCOMPARE(price.text(), QStringLiteral("5 $ each {"));
    Field lines;
    lines.scope = Scope::Text;
    lines.cursor.insertText(QStringLiteral("$x\n"));
    lines.type(QStringLiteral("{"));
    QCOMPARE(lines.text(), QStringLiteral("$x\n{"));
}

void BraceCompletionTest::escapedAndBeforeWordsStayAlone() {
    Field field;
    field.type(QStringLiteral("\\{"));
    QCOMPARE(field.text(), QStringLiteral("\\{"));
    field.type(QStringLiteral("\\\\{"));
    QCOMPARE(field.text(), QStringLiteral("\\{\\\\{}"));
    Field word;
    word.cursor.insertText(QStringLiteral("x^ab"));
    word.cursor.setPosition(2);
    word.type(QStringLiteral("{"));
    QCOMPARE(word.text(), QStringLiteral("x^{ab"));
    // "\{" removed by Backspace before a "}": the "}" stays.
    Field escaped;
    escaped.cursor.insertText(QStringLiteral("\\{}"));
    escaped.cursor.setPosition(2);
    escaped.backspace();
    QCOMPARE(escaped.text(), QStringLiteral("\\}"));
}

void BraceCompletionTest::oneUndoTakesThePairBack() {
    Field field;
    field.type(QStringLiteral("x^{"));
    QCOMPARE(field.text(), QStringLiteral("x^{}"));
    // The typed text may merge into one step with the letters before it: never "x^{" left alone.
    field.document.undo(&field.cursor);
    QVERIFY2(!field.text().contains(QLatin1Char('{')) && !field.text().contains(QLatin1Char('}')), qPrintable(field.text()));
    field.document.redo(&field.cursor);
    QCOMPARE(field.text(), QStringLiteral("x^{}"));
}

QTEST_GUILESS_MAIN(BraceCompletionTest)
#include "brace_completion_test.moc"
