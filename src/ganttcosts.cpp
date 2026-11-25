#include "ganttcosts.h"

#include <QSet>

namespace {

// a * b / c rounded to the nearest, for non-negative values.
qint64 scaled(qint64 a, qint64 b, qint64 c) { return c <= 0 ? 0 : (a * b + c / 2) / c; }

}  // namespace

GanttMoney &GanttMoney::operator+=(const GanttMoney &other) {
    for (auto it = other.cents.cbegin(); it != other.cents.cend(); ++it) cents[it.key()] += it.value();
    return *this;
}

QVector<std::pair<GanttCurrency, qint64>> GanttMoney::shown(const GanttCurrency &fallback) const {
    QVector<std::pair<GanttCurrency, qint64>> result;
    for (auto it = cents.cbegin(); it != cents.cend(); ++it) result.append({GanttCurrency::fromText(it.key()).normalized(), it.value()});
    if (result.isEmpty()) result.append({fallback.normalized(), 0});
    return result;
}

QString GanttMoney::text(const GanttCurrency &fallback) const {
    QStringList parts;
    for (const auto &[currency, value] : shown(fallback)) parts.append(GanttResources::formatCost(value, currency.displaySymbol()));
    return parts.join(QStringLiteral(" + "));
}

GanttCosts GanttCosts::compute(const GanttPlan &plan, const GanttSchedule &schedule, const GanttAssignments &assignments,
                               const QVector<GanttResource> &people) {
    GanttCosts costs;
    struct Rate {
        qint64 cents = 0;  // per hour
        QString currency;  // normalized text, built once: the days below reuse it
    };
    QHash<QString, Rate> rates;  // person -> rate
    for (const GanttResource &person : people) rates.insert(person.id, {person.hourlyCostCents, person.currency.normalized().text()});
    const GanttCalendar &calendar = plan.calendar();
    QHash<QString, QString> parentOf;
    for (const GanttTask &task : plan.tasks()) parentOf.insert(task.id, task.parentId);
    for (const GanttTask &task : plan.tasks()) costs.tasks.insert(task.id, {});

    for (const GanttTask &task : plan.tasks()) {
        if (plan.hasChildren(task.id)) continue;  // summaries sum their tasks below
        const int days = task.length();
        const GanttScheduledTask scheduled = schedule.tasks.value(task.id);
        // The working days of a dated task.
        QVector<QDate> workingDays;
        if (scheduled.dated && days > 0)
            for (QDate day = scheduled.start; day <= scheduled.end; day = day.addDays(1))
                if (calendar.isWorkingDay(day)) workingDays.append(day);
        for (const GanttAssignment &assignment : assignments.value(task.id)) {
            const auto rate = rates.constFind(assignment.person);
            if (rate == rates.cend()) continue;
            const qint64 minutes = scaled(qint64(days) * calendar.minutesPerDay, assignment.percent, 100);
            const qint64 cents = scaled(minutes, rate->cents, 60);
            const Amount amount{minutes, GanttMoney(rate->currency, cents)};
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
                const qint64 dayMinutes = scaled(minutes, i + 1, count) - scaled(minutes, i, count);
                const qint64 dayCents = scaled(cents, i + 1, count) - scaled(cents, i, count);
                costs.days[workingDays.at(i)][assignment.person] += Amount{dayMinutes, GanttMoney(rate->currency, dayCents)};
            }
        }
    }
    for (auto day = costs.days.cbegin(); day != costs.days.cend(); ++day) {
        QHash<QString, Amount> &week = costs.weeks[day.key().addDays(1 - day.key().dayOfWeek())];
        for (auto person = day->cbegin(); person != day->cend(); ++person) week[person.key()] += person.value();
    }
    return costs;
}
