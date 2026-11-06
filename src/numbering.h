#pragma once
// FEAT-024 (docs/features/FEAT-024-numerazione-rami.md): numbering of a topic's branches, chosen on
// the topic in the Style page. Numbers are computed (MindNode::number), never stored in the titles.
#include <QChar>
#include <QJsonValue>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

struct BranchNumbering {
    QString format;            // one symbol per level, joined by the separator: "1.2.3", "I-A-1"
    QChar separator = u'.';
    bool trailingSeparator = false;  // the separator also after the last symbol: "1.2.3."
    bool hierarchical = true;  // false = by level: only the topic's own number
    int levels = 0;            // levels numbered below the topic, 0 = all
    int start = 1;             // first number of the first level
    bool operator==(const BranchNumbering &) const = default;
};
Q_DECLARE_METATYPE(BranchNumbering)

namespace Numbering {

enum class Symbol { Arabic, UpperLetter, LowerLetter, UpperRoman, LowerRoman };

inline constexpr int kMaxLevels = 10;    // choices of the Levels select
inline constexpr int kMaxStart = 9999;   // First number

const QList<QChar> &separators();        // '.', ')', '-'
// The presets of the select (1.2.3, A.B.C, a.b.c, I.II.III) written with the separator.
QStringList presetFormats(QChar separator);
// Index of the preset the numbering uses (it numbers every level), -1 = custom.
int presetIndex(const BranchNumbering &numbering);
// Symbols of a format: digits = 1, 2, 3; a token starting with I or i made of Roman digits = Roman;
// other upper or lower letters = A, B, C or a, b, c. Nullopt if a token is none of them.
std::optional<QList<Symbol>> parse(const QString &format, QChar separator);
bool isValid(const BranchNumbering &numbering);
// value >= 1: 1, A (after Z: AA, AB... like spreadsheet columns), I (Roman up to 3999, then digits).
QString symbolText(int value, Symbol symbol);
// Number of a topic `path.size()` levels below the numbering topic; path holds the value at each level
// (the first already from `start`). Empty when the level is not numbered.
QString label(const BranchNumbering &numbering, const QList<int> &path);

// Text shown for a topic: the number, a space and the title ("1.2 Topic"); the number alone without a title.
inline QString withNumber(const QString &number, const QString &title) {
    if (number.isEmpty()) return title;
    return title.isEmpty() ? number : number + u' ' + title;
}

// File and history form ("numbering" key); defaults left out.
QJsonValue toJson(const BranchNumbering &numbering);
std::optional<BranchNumbering> fromJson(const QJsonValue &value);

// Gives every topic of a tree its number (assignTo(node, number), empty = none). forEachChild(node, f)
// calls f on the children that are numbered, in order (no list built per topic); setting(node): the
// topic's own numbering or nullptr. A topic with its own numbering keeps the number given by its
// ancestor's and restarts it for its branch.
template <class Node, class ForEachChildFn, class SettingFn, class AssignFn>
void assign(Node root, const ForEachChildFn &forEachChild, const SettingFn &setting, const AssignFn &assignTo) {
    const std::function<void(Node, const BranchNumbering *, const QList<int> &)> visit =
        [&](Node node, const BranchNumbering *active, const QList<int> &path) {
            assignTo(node, active && !path.isEmpty() ? label(*active, path) : QString());
            const BranchNumbering *own = setting(node);
            const BranchNumbering *next = own ? own : active;
            const QList<int> base = own ? QList<int>() : path;
            int rank = 0;
            forEachChild(node, [&](Node child) {
                if (!next) { visit(child, nullptr, {}); return; }
                QList<int> childPath = base;
                childPath.append((base.isEmpty() ? next->start : 1) + rank++);
                visit(child, next, childPath);
            });
        };
    visit(root, nullptr, {});
}

}  // namespace Numbering
