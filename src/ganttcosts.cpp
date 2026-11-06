#include "ganttcosts.h"

#include <QSet>

namespace {

// a * b / c rounded to the nearest, for non-negative values.
qint64 scaled(qint64 a, qint64 b, qint64 c) { return c <= 0 ? 0 : (a * b + c / 2) / c; }

}  // namespace

GanttCosts GanttCosts::compute(const GanttPlan &plan, const GanttSchedule &schedule, const GanttAssignments &assignments,
                               const QVector<GanttResource> &people) {
    GanttCosts costs;
    QHash<QString, qint64> rates;  // person -> cents per hour
    for (const GanttResource &person : people) rates.insert(person.id, person.hourlyCostCents);
    const GanttCalendar &calendar = plan.calendar();
    QHash<QString, QString> parentOf;
    for (const GanttTask &task : plan.tasks()) parentOf.insert(task.id, task.parentId);
    for (const GanttTask &task : plan.tasks()) costs.tasks.insert(task.id, {});

    for (const GanttTask &task : plan.tasks()) {
        if (plan.hasChildren(task.id)) continue;  // summaries sum their tasks below
        const int days = task.length();
        const GanttScheduledTask scheduled = schedule.tasks.value(task.id);
        // The working days of a dated task, for the weeks.
        QVector<QDate> workingDays;
        if (scheduled.dated && days > 0)
            for (QDate day = scheduled.start; day <= scheduled.end; day = day.addDays(1))
                if (calendar.isWorkingDay(day)) workingDays.append(day);
        for (const GanttAssignment &assignment : assignments.value(task.id)) {
            if (!rates.contains(assignment.person)) continue;
            Amount amount;
            amount.minutes = scaled(qint64(days) * calendar.minutesPerDay, assignment.percent, 100);
            amount.cents = scaled(amount.minutes, rates.value(assignment.person), 60);
            // The task, then every summary above it.
            for (QString id = task.id; !id.isEmpty(); id = parentOf.value(id)) {
                costs.tasks[id] += amount;
                costs.taskPeople[id][assignment.person] += amount;
            }
            costs.people[assignment.person] += amount;
            costs.total += amount;
            // Spread on the working days by running totals: the days add up to the amount exactly.
            const qint64 count = workingDays.size();
            for (qint64 i = 0; i < count; ++i) {
                const Amount before{scaled(amount.minutes, i, count), scaled(amount.cents, i, count)};
                const Amount after{scaled(amount.minutes, i + 1, count), scaled(amount.cents, i + 1, count)};
                const QDate monday = workingDays.at(i).addDays(1 - workingDays.at(i).dayOfWeek());
                costs.weeks[monday][assignment.person] += Amount{after.minutes - before.minutes, after.cents - before.cents};
            }
        }
    }
    return costs;
}
