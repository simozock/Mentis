#pragma once

// FEAT-124: a Gantt task's name (its topic's title as GanttTask::shown shows it) with the title's
// bold, italic, strikethrough, underline and highlight (OutlineFormats, as the outliner: font, size
// and colour stay on the map). Shared by the desktop's Name column and name fields and the phone's.
// Qt Gui only.

#include "outlineformats.h"

#include <QList>
#include <QString>
#include <QTextLayout>

class QPainter;
class QRectF;
class QTextDocument;
struct MindNode;

namespace GanttNames {

// The weights of a row: in a summary's row (already bold) bold is Black, as in the outliner's first level.
OutlineFormats::BoldWeights rowWeights(bool boldRow);

// The formats of the shown name, in its positions; none for a title without formats or with a formula.
QList<QTextLayout::FormatRange> ranges(const MindNode *topic, bool boldRow);

// False for a title holding a formula: its name is shown without it, so it takes no format (a
// format would write the shown name, the formula lost).
bool formattable(const MindNode *topic);

// The shown name in a name field's document, with its formats; the field is left unmodified.
void load(QTextDocument *field, const MindNode *topic, bool boldRow);

// The name typed in a field loaded by load: false when it is empty or the shown name with the same
// formats (nothing to write: line breaks and the map's formats stay); otherwise the new title (spaces
// simplified, as GanttTask::shown) and its formatted form for MindNode::titleHtml (empty: plain),
// the formats of the characters not typed kept (OutlineFormats::mergeOutlineFormats). A title with
// a formula: only a changed text counts, written plain (as before FEAT-124).
bool edited(const MindNode *topic, const QTextDocument &field, bool boldRow, QString *title, QString *html);

// Draws text in the painter's font and pen with ranges, at the left of rect and centred in its
// height, elided at the right ("…") when wider; the width drawn.
qreal paint(QPainter *painter, const QRectF &rect, const QString &text, const QList<QTextLayout::FormatRange> &ranges);

}  // namespace GanttNames
