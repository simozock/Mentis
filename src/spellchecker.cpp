#include "spellchecker.h"

#include "formulatext.h"

#include <QColor>
#include <QLocale>
#include <QSettings>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextDocument>

namespace SpellCheck {

namespace {

// docs/design/ui-foundations.md, "Colori".
const QColor kUnderlineColor(0xe5, 0x48, 0x4d);

struct State {
    std::unique_ptr<Backend> backend;
    bool created = false;
};
State &state() {
    static State instance;
    return instance;
}

// The underline is a layout format (QSyntaxHighlighter), not a document format: it never reaches
// the saved title, its HTML or the history. Only the changed paragraph is checked again.
class Highlighter;
QList<Highlighter *> g_highlighters;

class Highlighter final : public QSyntaxHighlighter {
public:
    explicit Highlighter(QTextDocument *document) : QSyntaxHighlighter(document) {
        m_format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        m_format.setUnderlineColor(kUnderlineColor);
        g_highlighters.append(this);
    }
    ~Highlighter() override { g_highlighters.removeOne(this); }

protected:
    void highlightBlock(const QString &text) override {
        for (const Range &range : check(text)) setFormat(range.start, range.length, m_format);
    }

private:
    QTextCharFormat m_format;
};

Highlighter *highlighterOf(const QTextDocument *document) {
    for (Highlighter *highlighter : g_highlighters)
        if (highlighter->document() == document) return highlighter;
    return nullptr;
}

void rehighlightAll() {
    for (Highlighter *highlighter : g_highlighters) highlighter->rehighlight();
}

}  // namespace

// Off by default (user's decision 2026-10-26). Only Windows has a system checker so far: elsewhere
// backend() is null and attach() does nothing, while the tests' checker works on every system.
bool enabled() { return QSettings().value(QStringLiteral("editor/spellCheck"), false).toBool(); }
void setEnabled(bool on) { QSettings().setValue(QStringLiteral("editor/spellCheck"), on); }

#ifndef Q_OS_WIN
// Stages B (macOS) and C (Linux) of FEAT-044 are not implemented yet.
std::unique_ptr<Backend> createSystemBackend(const QString &) { return nullptr; }
#endif

Backend *backend() {
    State &s = state();
    if (!s.created) {
        s.created = true;
        s.backend = createSystemBackend(QLocale::system().name().replace(QLatin1Char('_'), QLatin1Char('-')));
    }
    return s.backend.get();
}

void setBackendForTesting(std::unique_ptr<Backend> backend) {
    State &s = state();
    s.backend = std::move(backend);
    s.created = true;
    rehighlightAll();
}

QList<Range> check(const QString &text) {
    Backend *checker = backend();
    if (!checker || text.trimmed().isEmpty()) return {};
    if (!FormulaText::containsMath(text)) return checker->misspelled(text);
    QList<Range> ranges;
    for (const FormulaText::Segment &segment : FormulaText::split(text)) {
        if (segment.math) continue;
        for (Range range : checker->misspelled(text.mid(segment.from, segment.to - segment.from))) {
            range.start += segment.from;
            ranges.append(range);
        }
    }
    return ranges;
}

void attach(QTextDocument *document) {
    if (!document || highlighterOf(document) || !enabled() || !backend()) return;
    new Highlighter(document);  // owned by the document
}

void detach(QTextDocument *document) { delete highlighterOf(document); }

bool isAttached(const QTextDocument *document) { return highlighterOf(document) != nullptr; }

Word misspelledAt(const QTextDocument *document, int position) {
    if (!isAttached(document)) return {};
    const QTextBlock block = document->findBlock(position);
    if (!block.isValid()) return {};
    const int offset = position - block.position();
    const QString text = block.text();
    for (const Range &range : check(text)) {
        // The click may land just after the last letter (the cursor is placed between characters).
        if (offset >= range.start && offset <= range.start + range.length)
            return {block.position() + range.start, range.length, text.mid(range.start, range.length)};
    }
    return {};
}

void learn(const QString &word) {
    if (Backend *checker = backend()) checker->learn(word);
    rehighlightAll();
}

void ignore(const QString &word) {
    if (Backend *checker = backend()) checker->ignore(word);
    rehighlightAll();
}

}  // namespace SpellCheck
