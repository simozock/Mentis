#pragma once

#include "stylechoices.h"

#include <QColor>
#include <QList>
#include <QRectF>

class QPainter;

// FEAT-080: the choices of the Gantt's style selects (the side panel's Gantt page and the Task page's
// Style section), in the desktop's menu order, with their names (the desktop panel's, context
// "GanttTaskPanel", so both apps share the translations) and their pictures, drawn as the chart
// draws the bars (GanttCanvas, NodeSketch, BranchPath). Shared by the desktop's GanttTaskPanel and
// the phone's side panel (FEAT-122 stage C2); no widget.
namespace GanttStyleChoices {

enum class List { BarShape, SummaryShape, MilestoneShape, Fill, Border, LinkLine, LinkEnd, LinkWidth, LinkEndSize };

QList<StyleChoices::Choice> choices(List list);
int columns(List list);    // the desktop's grid menu columns
int noneValue(List list);  // the choice shown as a wide tile at the bottom (Border's None), -1 = none
// A choice's picture in the text colour, inside box (the selects' 36 x 22 icons).
void drawIcon(List list, int value, QPainter &painter, const QRectF &box, const QColor &text);

}  // namespace GanttStyleChoices
