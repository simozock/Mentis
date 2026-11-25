#pragma once
// FEAT-078 and FEAT-122 stage F: the Gantt table's columns, shared by the desktop's table, the
// phone's and the exports: what each holds, its texts, and the app's choice of shown columns and
// their order (QSettings, the same keys on every platform: gantt/columns, gantt/columnOrder,
// gantt/numberHidden, gantt/columnWidths). Not saved in the file.
#include "ganttplan.h"
#include "ganttresources.h"

#include <QString>
#include <QVector>

struct MindNode;

class GanttColumns {
public:
    enum class Field { Number, Name, Start, End, Duration, Progress, Priority, People };

    // The columns that can be dragged, in the default order (Number and Name stay first).
    static QVector<Field> movable();
    // The setting's key of a movable column ("start", "end", "duration", "progress", "priority",
    // "person"); empty for Number and Name.
    static QString key(Field field);
    // The header's text, the desktop table's (GanttView context, already translated).
    static QString title(Field field);
    // A movable column's default width in px ("Molto bassa" whole in Priority); 0 for Number (as
    // wide as its numbers) and Name (the rest).
    static int defaultWidth(Field field);
    // A movable column's width: dragged by the user (desktop or phone, user's request 2026-11-23),
    // else the default; never under kMinWidth. 0 for Number and Name.
    static int width(Field field);
    static void setWidth(Field field, int width);
    static constexpr int kMinWidth = 48;  // the phone's touch target, so grips never overlap
    // Numbers aligned right (Duration, %).
    static bool alignedRight(Field field) { return field == Field::Duration || field == Field::Progress; }

    // The movable columns in the user's order: keys missing from the setting (or unknown) are
    // ignored, the columns they leave out follow in the default order.
    static QVector<Field> order();
    static void setOrder(const QVector<Field> &order);
    // Name always; Number unless hidden by the user (only meaningful on a numbered map); the
    // movable ones as chosen, Start and Duration by default.
    static bool isShown(Field field);
    static void setShown(Field field, bool shown);
    // Number (when numbered and not hidden), Name, then the shown movable columns in order.
    static QVector<Field> shownFields(bool numbered);

    // Some topic under root has a number (FEAT-024): the No. column applies.
    static bool numbered(const MindNode *root);
    // A cell's text as the desktop's table shows it; people in the Resources list's order.
    static QString cellText(Field field, const GanttPlan &plan, const GanttTask &task, const GanttScheduledTask &scheduled,
                            const MindNode *topic, const QVector<GanttResource> &people);
};
