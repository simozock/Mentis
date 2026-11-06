#pragma once

// FEAT-044 (docs/features/FEAT-044-controllo-ortografico.md): the system spell checker underlines
// misspelled words in the text being edited (map topic, Outliner items and title) and gives the
// spelling items of the text menu. Stage A: Windows (ISpellChecker); elsewhere there is no backend
// and nothing is underlined.

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

class QTextDocument;

namespace SpellCheck {

struct Range {
    int start = 0;
    int length = 0;
};

// A spell checker for one language. All calls happen on the UI thread.
class Backend {
public:
    virtual ~Backend() = default;
    virtual QList<Range> misspelled(const QString &text) = 0;
    virtual QStringList suggestions(const QString &word, int max) = 0;
    virtual void learn(const QString &word) = 0;   // into the user's dictionary
    virtual void ignore(const QString &word) = 0;  // until the app closes
};

// Settings > Editor > Writing > "Check spelling" (off by default, Windows only); applies to the next edit.
bool enabled();
void setEnabled(bool on);

// System checker for languageTag ("it-IT"; it falls back to another region of the same
// language), or nullptr when the system has none.
std::unique_ptr<Backend> createSystemBackend(const QString &languageTag);
// Checker of the system language, created at the first call (not at startup); nullptr if none.
Backend *backend();
// Tests: replaces the checker (nullptr = no dictionary), so they never touch the user's dictionary.
void setBackendForTesting(std::unique_ptr<Backend> backend);

// Misspelled words of text, formulas ($...$, \(...\)) excluded.
QList<Range> check(const QString &text);

// Underlines the misspelled words of document while it is edited: nothing happens when the
// setting is off or there is no checker. detach removes the underline.
void attach(QTextDocument *document);
void detach(QTextDocument *document);
bool isAttached(const QTextDocument *document);

// Misspelled word at the cursor position of an attached document; empty text if there is none.
struct Word {
    int position = 0;  // in the document
    int length = 0;
    QString text;
};
Word misspelledAt(const QTextDocument *document, int position);

// "Learn spelling" and "Ignore": the word stops being underlined in every field being edited.
void learn(const QString &word);
void ignore(const QString &word);

}  // namespace SpellCheck
