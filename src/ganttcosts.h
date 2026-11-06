#pragma once
// FEAT-083 (docs/features/FEAT-083-gantt-esportazione.md): the people's hours and cost on a sheet's
// tasks, summed in the summaries, per person and per ISO week. Core value type, no widgets; the
// exports read it. Minutes and cents are integers: no rounding errors in the sums.
#include "ganttplan.h"
#include "ganttresources.h"

#include <QDate>
#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>

struct GanttAssignment {
    QString person;     // a resource id (FEAT-079)
    int percent = 100;  // the person's share of the task, 1-100 (FEAT-083)
    bool operator==(const GanttAssignment &) const = default;
};
using GanttAssignments = QHash<QString, QVector<GanttAssignment>>;  // task id -> its people, in order

struct GanttCosts {
    struct Amount {
        qint64 minutes = 0;
        qint64 cents = 0;
        Amount &operator+=(const Amount &other) {
            minutes += other.minutes;
            cents += other.cents;
            return *this;
        }
        bool operator==(const Amount &) const = default;
    };

    // A task's hours per person = its working days (undated: its duration; a milestone: none) x the
    // sheet's working day x the person's share; cost = hours x the person's hourly cost. Rounded once
    // per task and person (to the minute, to the cent). People not in the resources are left out.
    static GanttCosts compute(const GanttPlan &plan, const GanttSchedule &schedule, const GanttAssignments &assignments,
                              const QVector<GanttResource> &people);

    QHash<QString, Amount> tasks;                       // every task; a summary = the sum of its tasks
    QHash<QString, QHash<QString, Amount>> taskPeople;  // task -> person -> amount (summaries summed too)
    QHash<QString, Amount> people;                      // person -> every task
    Amount total;
    // Monday of the ISO week -> person -> amount; dated tasks only, their minutes spread evenly on
    // their working days (the first days take the remainder), so the weeks add up to the task.
    QMap<QDate, QHash<QString, Amount>> weeks;
};
