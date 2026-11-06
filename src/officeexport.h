#pragma once

// FEAT-054 stage 4 (docs/features/FEAT-054-esportazione.md, "Stage 4"): the sheets of a document as
// a Word (.docx) or Excel (.xlsx) file, with our own OOXML writer. No widgets.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QXmlStreamWriter>

#include <functional>

class MindMapModel;
struct MindNode;

namespace OfficeExport {

struct Sheet {
    QString name;
    const MindMapModel *model = nullptr;
};
// The original file of a topic's image (embedded asset); empty or missing = the node's own file.
using ImageSource = std::function<QString(const MindNode *)>;

struct Package {
    QByteArray bytes;
    QStringList skipped;  // topics whose image was not found (left out)
};

// A Title per central topic (a new page per sheet), Heading 1-9 per level, a bulleted list deeper;
// labels, note lines and the picture below each title.
Package docx(const QList<Sheet> &sheets, const ImageSource &imageSource, const QString &title);
// A worksheet per sheet: a column per level, then the Notes, Labels, Link, Task and Formula columns
// the sheet uses.
QByteArray xlsx(const QList<Sheet> &sheets, const QString &title);

// Excel's worksheet names for the sheets: no []:*?/\, at most 31 characters, unique.
QStringList worksheetNames(const QStringList &sheetNames);

// Write the file; empty = written, else the error (no half file is left).
QString writeDocx(const QString &file, const QList<Sheet> &sheets, const ImageSource &imageSource, const QString &title,
                  QStringList *skipped = nullptr);
QString writeXlsx(const QString &file, const QList<Sheet> &sheets, const QString &title);

// Shared with the Gantt's Word and Excel files (FEAT-083, ganttoffice.h).
QByteArray wordStyles();
void inlinePicture(QXmlStreamWriter &xml, int number, const QString &name, const QString &relationId, qint64 cx, qint64 cy);
QString columnName(int index);  // 0 = A
QString writeFile(const QString &file, const QByteArray &bytes);  // empty = written

}  // namespace OfficeExport
