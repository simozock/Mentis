#include "nodesearch.h"

#include "numbering.h"

#include <QHash>
#include <QJsonArray>

#include <algorithm>
#include <functional>

namespace NodeSearch {

namespace {

// Word characters as \b with Unicode properties: letters, digits, marks and '_', so accents count.
bool isWordAt(const QString &text, qsizetype at, bool before) {
    if (before ? at <= 0 : at >= text.size()) return false;
    char32_t code = before ? text.at(at - 1).unicode() : text.at(at).unicode();
    if (before && QChar::isLowSurrogate(code) && at >= 2 && text.at(at - 2).isHighSurrogate())
        code = QChar::surrogateToUcs4(text.at(at - 2), text.at(at - 1));
    else if (!before && QChar::isHighSurrogate(code) && at + 1 < text.size() && text.at(at + 1).isLowSurrogate())
        code = QChar::surrogateToUcs4(text.at(at), text.at(at + 1));
    return code == U'_' || QChar::isLetterOrNumber(code) || QChar::isMark(code);
}

// \b: a word character on exactly one side of the position.
bool boundary(const QString &text, qsizetype at) { return isWordAt(text, at, true) != isWordAt(text, at, false); }

}  // namespace

QList<Entry> entries(const QString &sheetId, const MapHistory::Snapshot &snapshot) {
    // Children of each topic, ordered by row; the central topic has no parent.
    QHash<QString, QList<std::pair<int, QString>>> children;
    QString rootId;
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
        if (it.key().startsWith(QLatin1Char('@'))) continue;  // sheet data ("@map"), not a topic
        const QString parent = it.value().value(QStringLiteral("parent")).toString();
        if (parent.isEmpty()) rootId = it.key();
        else children[parent].append({it.value().value(QStringLiteral("row")).toInt(), it.key()});
    }
    QList<Entry> result;
    if (rootId.isEmpty()) return result;
    result.reserve(snapshot.size());
    for (auto &kids : children) std::sort(kids.begin(), kids.end());
    // FEAT-024: numbers as in the map (MindMapModel::refreshStyles), from the flat form.
    QHash<QString, BranchNumbering> settings;
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it)
        if (const auto numbering = Numbering::fromJson(it.value().value(QStringLiteral("numbering")))) settings.insert(it.key(), *numbering);
    QHash<QString, QString> numbers;
    if (!settings.isEmpty()) {
        const auto floating = [&snapshot](const QString &id) { return snapshot.value(id).value(QStringLiteral("floating")).toBool(); };
        const auto childrenOf = [&](const QString &id, const auto &visit) {
            for (const auto &[row, child] : children.value(id)) if (id != rootId || !floating(child)) visit(child);
        };
        const auto setting = [&settings](const QString &id) {
            const auto it = settings.constFind(id);
            return it != settings.cend() ? &it.value() : nullptr;
        };
        const auto assignTo = [&numbers](const QString &id, const QString &number) { if (!number.isEmpty()) numbers.insert(id, number); };
        Numbering::assign(rootId, childrenOf, setting, assignTo);
        for (const auto &[row, child] : children.value(rootId))
            if (floating(child)) Numbering::assign(child, childrenOf, setting, assignTo);
    }
    const std::function<void(const QString &, int)> visit = [&](const QString &id, int depth) {
        const QJsonObject json = snapshot.value(id);
        Entry entry;
        entry.sheetId = sheetId;
        entry.nodeId = id;
        entry.title = json.value(QStringLiteral("title")).toString();
        entry.number = numbers.value(id);
        entry.notes = json.value(QStringLiteral("notes")).toString();
        for (const auto label : json.value(QStringLiteral("labels")).toArray()) entry.labels.append(label.toString());
        entry.webLink = json.value(QStringLiteral("webLink")).toString();
        entry.depth = depth;
        result.append(entry);
        for (const auto &[row, child] : children.value(id)) visit(child, depth + 1);
    };
    visit(rootId, 0);
    return result;
}

QList<std::pair<int, int>> occurrences(const QString &text, const QString &query, const Options &options) {
    QList<std::pair<int, int>> found;
    const QString needle = query.trimmed();
    if (needle.isEmpty() || text.isEmpty()) return found;
    const Qt::CaseSensitivity sensitivity = options.matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const qsizetype end = needle.size();
    for (qsizetype at = text.indexOf(needle, 0, sensitivity); at >= 0;) {
        // Whole words: a word boundary (\b) at both ends of the match. Checked here rather than with a
        // regular expression, which made 5,000 topics take 346 ms instead of about the same as without.
        if (options.wholeWords && !(boundary(text, at) && boundary(text, at + end))) {
            at = text.indexOf(needle, at + 1, sensitivity);
            continue;
        }
        found.append({int(at), int(end)});
        at = text.indexOf(needle, at + end, sensitivity);
    }
    return found;
}

QString shownTitle(const Entry &entry) { return Numbering::withNumber(entry.number, entry.title); }

int matchingFields(const Entry &entry, const QString &query, const Options &options) {
    const auto contains = [&](const QString &text) { return !occurrences(text, query, options).isEmpty(); };
    int fields = 0;
    if ((options.fields & Title) && contains(shownTitle(entry))) fields |= Title;
    if ((options.fields & Note) && contains(entry.notes)) fields |= Note;
    if ((options.fields & Label) && std::any_of(entry.labels.cbegin(), entry.labels.cend(), contains)) fields |= Label;
    if ((options.fields & Link) && contains(entry.webLink)) fields |= Link;
    return fields;
}

QString replaced(const QString &text, const QString &query, const QString &replacement, const Options &options) {
    QString result = text;
    const QList<std::pair<int, int>> found = occurrences(text, query, options);
    for (auto it = found.crbegin(); it != found.crend(); ++it) result.replace(it->first, it->second, replacement);
    return result;
}

}  // namespace NodeSearch
