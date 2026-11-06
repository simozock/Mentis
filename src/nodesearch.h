#pragma once

// FEAT-061 / FEAT-072 C: search for the left navigation panel. No widgets: it reads
// the flat form of a sheet (MapHistory::Snapshot, the same as history and saving), so other sheets can
// be searched without loading them into the model.

#include "maphistory.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace NodeSearch {

// Fields searched (filters Topic, Note, Label, Link; relationships and boundaries do not exist yet).
enum Field { Title = 1, Note = 2, Label = 4, Link = 8, AllFields = Title | Note | Label | Link };

struct Options {
    bool matchCase = false;
    bool wholeWords = false;
    int fields = AllFields;
};

// A topic of a sheet in tree order (depth-first, children by row).
struct Entry {
    QString sheetId;
    QString nodeId;
    QString title;
    QString number;  // FEAT-024: shown and searched with the title ("1.2 Topic")
    QString notes;
    QStringList labels;
    QString webLink;
    int depth = 0;  // 0 = central topic
};

QList<Entry> entries(const QString &sheetId, const MapHistory::Snapshot &snapshot);

// FEAT-024: the topic as listed: number and title.
QString shownTitle(const Entry &entry);

// Start and length of each occurrence of query in text; empty query = none.
QList<std::pair<int, int>> occurrences(const QString &text, const QString &query, const Options &options);

// Fields of the entry that contain the query (Field bits), only among options.fields.
int matchingFields(const Entry &entry, const QString &query, const Options &options);

// Text with every occurrence of query replaced (same rules as occurrences); unchanged without any.
QString replaced(const QString &text, const QString &query, const QString &replacement, const Options &options);

}  // namespace NodeSearch
