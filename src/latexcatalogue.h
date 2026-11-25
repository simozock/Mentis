#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// FEAT-132: the LaTeX commands offered by the formula modal's list, ported from MentisNote's
// cheatsheet (FEAT-046 there) and kept to what JKQtMathText draws (tests/formula_test.cpp checks
// every entry). Static data; names are translated with the "LatexCatalogue" context.
namespace LatexCatalogue {

enum class Category { Greek, Operators, Relations, Arrows, Structures, BigOperators, Accents, Brackets, Matrices,
                      Functions, Sets, Fonts, Symbols };

struct Entry {
    Category category;
    const char *code;      // inserted text; the first "{}" takes the selection and the caret
    const char *name;      // English, translated in the "LatexCatalogue" context
    const char *keywords;  // extra search words, English and Italian, never shown
    const char *preview;   // drawn instead of the code; nullptr = the code with "{}" filled by \square
};

const QVector<Entry> &entries();
QVector<Category> categories();
QString categoryName(Category category);  // translated
QString name(const Entry &entry);         // translated
QString previewCode(const Entry &entry);

// Lower case without diacritics, so "piu" finds "più".
QString fold(const QString &text);
// Folded search text per entry (names in English and in the interface's language, code,
// keywords): built once by the list, not per keystroke.
QStringList searchTexts();
// Indexes into entries() whose text contains every word of the query, in the given category (or
// any with allCategories); an empty query keeps them all.
QVector<int> search(const QStringList &texts, const QString &query, Category category, bool allCategories);

struct Insertion { QString text; int caret = 0; };
// Replaces [start, end) of value with code: the selected text goes into the first "{}" and the
// caret after it; with no "{}" the caret goes after the inserted text.
Insertion insert(const QString &value, int start, int end, const QString &code);

}  // namespace LatexCatalogue
