#pragma once

// FEAT-127 (docs/features/FEAT-127-gantt-grafici-del-personale.md): the figures of the people's charts
// in the Resources modal, desktop and phone, from GanttCosts (FEAT-083) so they equal the cost export.
// Core value type, no widgets: the desktop paints it, MobileResources hands it to QML. The texts are
// built here too, so both front ends show the same ones.

#include "ganttcanvas.h"
#include "ganttcosts.h"

#include <QColor>
#include <QCoreApplication>
#include <QDate>
#include <QString>
#include <QVector>

class GanttPeopleCharts {
    Q_DECLARE_TR_FUNCTIONS(GanttPeopleCharts)
public:
    static constexpr int kMaxRoles = 8;  // the rest in "Other"

    struct Bar {  // Days per person
        QString name;
        qint64 minutes = 0;
    };
    struct Role {  // Cost by role, most expensive first
        QString name;  // "Other" for the roles past kMaxRoles and the people without one
        qint64 cents = 0;
        int percent = 0;  // of the bar's currency's planned cost, rounded
        QColor color;
    };
    // FEAT-128: one Cost by role bar per currency, by the currency's text; each its own %.
    struct RoleBar {
        GanttCurrency currency;
        qint64 cents = 0;
        QVector<Role> roles;
    };
    // A day, ISO week or month of the sheet with at least one working day (the Gantt's scale).
    struct Period {
        QDate first;
        QDate last;
        qint64 minutes = 0;          // the person's
        qint64 capacityMinutes = 0;  // its working days x the working day
        int percent() const;
        bool overloaded() const { return minutes > capacityMinutes; }
    };
    struct Person {
        QString id;
        QString name;
        int tasks = 0;  // leaf tasks with this person, milestones left out
        qint64 minutes = 0;
        qint64 cents = 0;
        GanttCurrency currency;  // the person's (FEAT-128)
        qint64 doneMinutes = 0;  // the minutes weighted by their tasks' progress
        QVector<Period> load;
        int progressPercent() const;
    };

    // plan: the sheet's tasks with the calendar to use (the modal's copy); people: the modal's, in
    // the table's order. Undated tasks count in the totals, not in the load; milestones count nothing.
    static GanttPeopleCharts compute(const GanttPlan &plan, const GanttAssignments &assignments,
                                     const QVector<GanttResource> &people, GanttCanvas::Scale scale);

    int minutesPerDay = GanttCalendar::kDefaultMinutesPerDay;
    GanttCanvas::Scale scale = GanttCanvas::Scale::Days;
    GanttMoney cost;     // planned cost, per currency (FEAT-128)
    qint64 minutes = 0;  // the people's hours
    QVector<Bar> days;   // one per person, the table's order
    QVector<RoleBar> roleBars;  // the currencies with a cost; empty: no cost
    QVector<Person> people;  // the table's order
    bool empty() const { return minutes == 0; }  // no people, or no task with people

    // The texts, the same on the desktop and the phone (values in the interface's locale).
    static QString money(qint64 cents, const QString &symbol);  // whole units: "€14,080"
    QString costText() const;                  // Planned cost: money() per currency, one a line
    QString roleBarTitle(const RoleBar &bar) const;  // "EUR €" with more than one bar, else empty
    QString personDays() const;                // "40.5", one decimal
    QString dayCount(qint64 minutes) const;    // "4.5", "32": one decimal at most
    QString personLine(const Person &person) const;   // "5 tasks · 32 days · €14,080", their currency
    QString progressLine(const Person &person) const;                        // "62% · 12 days left"
    QString periodTip(const Period &period) const;  // the column's date or period, hours, %

    static QColor roleColor(int index);  // index kMaxRoles: "Other"
    static QColor barColor();            // the accent
    static QColor overloadColor();       // danger
    static QString emptyText();
};
