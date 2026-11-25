#include "ganttpeoplecharts.h"

#include <QHash>
#include <QLocale>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {

// The first day of the day's period in the Gantt's scale.
QDate periodStart(const QDate &day, GanttCanvas::Scale scale) {
    switch (scale) {
    case GanttCanvas::Scale::Days: return day;
    case GanttCanvas::Scale::Weeks: return day.addDays(1 - day.dayOfWeek());
    case GanttCanvas::Scale::Months: return QDate(day.year(), day.month(), 1);
    }
    return day;
}

QDate periodEnd(const QDate &first, GanttCanvas::Scale scale) {
    switch (scale) {
    case GanttCanvas::Scale::Days: return first;
    case GanttCanvas::Scale::Weeks: return first.addDays(6);
    case GanttCanvas::Scale::Months: return first.addMonths(1).addDays(-1);
    }
    return first;
}

int rounded(qint64 part, qint64 whole) { return whole <= 0 ? 0 : int((part * 100 + whole / 2) / whole); }

// One decimal, without ".0" when whole (the interface's locale).
QString decimal(double value) {
    const double tenths = std::round(value * 10.0) / 10.0;
    return QLocale().toString(tenths, 'f', std::fmod(tenths, 1.0) == 0.0 ? 0 : 1);
}

}  // namespace

int GanttPeopleCharts::Period::percent() const { return rounded(minutes, capacityMinutes); }

int GanttPeopleCharts::Person::progressPercent() const { return rounded(doneMinutes, minutes); }

GanttPeopleCharts GanttPeopleCharts::compute(const GanttPlan &plan, const GanttAssignments &assignments,
                                             const QVector<GanttResource> &people, GanttCanvas::Scale scale) {
    GanttPeopleCharts charts;
    const GanttCalendar &calendar = plan.calendar();
    charts.minutesPerDay = calendar.minutesPerDay;
    charts.scale = scale;
    const GanttSchedule schedule = plan.schedule();
    const GanttCosts costs = GanttCosts::compute(plan, schedule, assignments, people);
    charts.cost = costs.total.cost;
    charts.minutes = costs.total.minutes;

    // The sheet's periods, the same for every person: from the first to the last dated task.
    QVector<Period> periods;
    if (schedule.start.isValid() && schedule.end.isValid()) {
        for (QDate first = periodStart(schedule.start, scale); first <= schedule.end;
             first = periodEnd(first, scale).addDays(1)) {
            Period period{first, periodEnd(first, scale)};
            int working = 0;
            for (QDate day = period.first; day <= period.last; day = day.addDays(1))
                if (calendar.isWorkingDay(day)) ++working;
            if (working == 0) continue;  // a weekend off has no column
            period.capacityMinutes = qint64(working) * calendar.minutesPerDay;
            periods.append(period);
        }
    }
    QHash<QDate, int> periodOf;  // period's first day -> index
    for (int i = 0; i < periods.size(); ++i) periodOf.insert(periods.at(i).first, i);

    QHash<QString, int> personIndex;
    for (const GanttResource &resource : people) {
        if (personIndex.contains(resource.id)) continue;
        personIndex.insert(resource.id, charts.people.size());
        Person person;
        person.id = resource.id;
        person.name = resource.displayName();
        const GanttCosts::Amount amount = costs.people.value(resource.id);
        person.minutes = amount.minutes;
        person.currency = resource.currency.normalized();
        person.cents = amount.cost.cents.value(person.currency.text());
        person.load = periods;
        charts.people.append(person);
        charts.days.append({person.name, person.minutes});
    }
    for (auto day = costs.days.cbegin(); day != costs.days.cend(); ++day) {
        const int index = periodOf.value(periodStart(day.key(), scale), -1);
        if (index < 0) continue;
        for (auto amount = day->cbegin(); amount != day->cend(); ++amount)
            if (const int person = personIndex.value(amount.key(), -1); person >= 0)
                charts.people[person].load[index].minutes += amount->minutes;
    }
    for (const GanttTask &task : plan.tasks()) {
        if (task.milestone || plan.hasChildren(task.id)) continue;
        const QHash<QString, GanttCosts::Amount> shares = costs.taskPeople.value(task.id);
        QSet<QString> counted;
        for (const GanttAssignment &assignment : assignments.value(task.id)) {
            const int person = personIndex.value(assignment.person, -1);
            if (person < 0 || counted.contains(assignment.person)) continue;
            counted.insert(assignment.person);
            Person &shown = charts.people[person];
            ++shown.tasks;
            shown.doneMinutes += (shares.value(assignment.person).minutes * std::clamp(task.progress, 0, 100) + 50) / 100;
        }
    }

    // Per currency: roles, most expensive first; a person without a role and the roles past
    // kMaxRoles in "Other".
    struct Group {
        QVector<Role> roles;
        QHash<QString, int> index;
        qint64 otherCents = 0;
    };
    QMap<QString, Group> groups;  // currency text -> its roles, by the text's order as GanttMoney
    for (const Person &person : std::as_const(charts.people)) groups[person.currency.text()];
    for (const GanttResource &resource : people) {
        const int shown = personIndex.value(resource.id, -1);
        if (shown < 0) continue;
        const Person &person = charts.people.at(shown);
        Group &group = groups[person.currency.text()];
        const QString role = resource.role.trimmed();
        if (role.isEmpty()) {
            group.otherCents += person.cents;
            continue;
        }
        const auto found = group.index.constFind(role);
        if (found == group.index.cend()) {
            group.index.insert(role, group.roles.size());
            group.roles.append({role, person.cents});
        } else {
            group.roles[*found].cents += person.cents;
        }
    }
    for (auto group = groups.begin(); group != groups.end(); ++group) {
        QVector<Role> &roles = group->roles;
        std::stable_sort(roles.begin(), roles.end(), [](const Role &a, const Role &b) { return a.cents > b.cents; });
        qint64 otherCents = group->otherCents;
        for (int i = kMaxRoles; i < roles.size(); ++i) otherCents += roles.at(i).cents;
        if (roles.size() > kMaxRoles) roles.resize(kMaxRoles);
        RoleBar bar{GanttCurrency::fromText(group.key()).normalized(), 0, {}};
        for (const Role &kept : std::as_const(roles)) {
            if (kept.cents <= 0) continue;
            Role role = kept;
            role.color = roleColor(int(bar.roles.size()));
            bar.roles.append(role);
        }
        if (otherCents > 0) bar.roles.append({tr("Other"), otherCents, 0, roleColor(kMaxRoles)});
        for (const Role &role : std::as_const(bar.roles)) bar.cents += role.cents;
        for (Role &role : bar.roles) role.percent = rounded(role.cents, bar.cents);
        if (bar.cents > 0) charts.roleBars.append(bar);
    }
    return charts;
}

QString GanttPeopleCharts::money(qint64 cents, const QString &symbol) {
    return QLocale().toCurrencyString(std::round(double(cents) / 100.0), symbol, 0);
}

QString GanttPeopleCharts::costText() const {
    QStringList lines;
    const GanttCurrency fallback = people.isEmpty() ? GanttCurrency() : people.constFirst().currency;
    for (const auto &[currency, cents] : cost.shown(fallback)) lines.append(money(cents, currency.displaySymbol()));
    return lines.join(QLatin1Char('\n'));
}

QString GanttPeopleCharts::roleBarTitle(const RoleBar &bar) const { return roleBars.size() > 1 ? bar.currency.text() : QString(); }

QString GanttPeopleCharts::personDays() const {
    return QLocale().toString(minutesPerDay > 0 ? double(minutes) / minutesPerDay : 0.0, 'f', 1);
}

QString GanttPeopleCharts::dayCount(qint64 value) const { return decimal(minutesPerDay > 0 ? double(value) / minutesPerDay : 0.0); }

QString GanttPeopleCharts::personLine(const Person &person) const {
    // Singular and plural spelled out: English has no plural forms of its own here (a %n text shows "(s)").
    const QString tasks = person.tasks == 1 ? tr("1 task") : tr("%1 tasks").arg(QLocale().toString(person.tasks));
    const QString days = dayCount(person.minutes);
    return tasks + QStringLiteral(" · ") + (days == QLocale().toString(1) ? tr("1 day") : tr("%1 days").arg(days))
           + QStringLiteral(" · ") + money(person.cents, person.currency.displaySymbol());
}

QString GanttPeopleCharts::progressLine(const Person &person) const {
    const QString left = dayCount(person.minutes - person.doneMinutes);
    return left == QLocale().toString(1) ? tr("%1% · 1 day left").arg(person.progressPercent())
                                         : tr("%1% · %2 days left").arg(person.progressPercent()).arg(left);
}

QString GanttPeopleCharts::periodTip(const Period &period) const {
    const QLocale locale;
    QString when;
    switch (scale) {
    case GanttCanvas::Scale::Days: when = locale.toString(period.first, QLocale::ShortFormat); break;
    case GanttCanvas::Scale::Weeks:
        when = tr("Week %1, %2").arg(period.first.weekNumber()).arg(locale.toString(period.first, QLocale::ShortFormat));
        break;
    case GanttCanvas::Scale::Months:
        when = locale.standaloneMonthName(period.first.month()) + QLatin1Char(' ') + QString::number(period.first.year());
        break;
    }
    return tr("%1\n%2 h of %3 h · %4%").arg(when, decimal(double(period.minutes) / 60.0), decimal(double(period.capacityMinutes) / 60.0))
        .arg(period.percent());
}

QColor GanttPeopleCharts::roleColor(int index) {
    // Eight of the colour picker's grid, readable on the dark modal, the accent first; grey for
    // "Other" (docs/design/ui-foundations.md, "Colori").
    static const QColor colors[] = {QColor(0x70, 0x47, 0xeb), QColor(0x05, 0xa8, 0xf4), QColor(0x2c, 0xd5, 0x51),
                                    QColor(0xff, 0xc0, 0x09), QColor(0xff, 0x6f, 0x00), QColor(0xba, 0x69, 0xc8),
                                    QColor(0x00, 0xa9, 0x9d), QColor(0xea, 0x8a, 0xba), QColor(0x99, 0x99, 0x99)};
    return colors[std::clamp(index, 0, kMaxRoles)];
}

QColor GanttPeopleCharts::barColor() { return roleColor(0); }

QColor GanttPeopleCharts::overloadColor() { return QColor(0xe5, 0x48, 0x4d); }

QString GanttPeopleCharts::emptyText() { return tr("Assign people to tasks to see their charts"); }
