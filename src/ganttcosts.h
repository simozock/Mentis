#pragma once
// FEAT-083 (docs/features/FEAT-083-gantt-esportazione.md): the people's hours and cost on a sheet's
// tasks, summed in the summaries, per person, per working day and per ISO week. Core value type, no
// widgets; the exports and the people's charts (FEAT-127) read it. Minutes and cents are integers: no rounding errors in the sums.
#include "ganttplan.h"
#include "ganttresources.h"

#include <QDate>
#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>

#include <utility>

struct GanttAssignment {
    QString person;     // a resource id (FEAT-079)
    int percent = 100;  // the person's share of the task, 1-100 (FEAT-083)
    bool operator==(const GanttAssignment &) const = default;
};
using GanttAssignments = QHash<QString, QVector<GanttAssignment>>;  // task id -> its people, in order

// FEAT-128: an amount in one or more currencies, never added across them (no exchange rates).
struct GanttMoney {
    // Currency text ("EUR €") -> cents, by the text's order; no entry is 0, so equal sums compare
    // equal whatever their parts.
    QMap<QString, qint64> cents;
    GanttMoney() = default;
    GanttMoney(const GanttCurrency &currency, qint64 value) : GanttMoney(currency.normalized().text(), value) {}
    GanttMoney(const QString &currencyText, qint64 value) {  // a normalized text
        if (value != 0) cents.insert(currencyText, value);
    }
    GanttMoney &operator+=(const GanttMoney &other);
    bool operator==(const GanttMoney &) const = default;
    bool isZero() const { return cents.isEmpty(); }
    // The currencies with an amount; none: 0 in `fallback`.
    QVector<std::pair<GanttCurrency, qint64>> shown(const GanttCurrency &fallback = GanttCurrency()) const;
    // "€1,200.00 + $300.00": shown(), each with GanttResources::formatCost's text, joined by " + ".
    QString text(const GanttCurrency &fallback = GanttCurrency()) const;
};

struct GanttCosts {
    struct Amount {
        qint64 minutes = 0;
        GanttMoney cost;  // in the people's currencies (FEAT-128)
        Amount &operator+=(const Amount &other) {
            minutes += other.minutes;
            cost += other.cost;
            return *this;
        }
        bool operator==(const Amount &) const = default;
    };

    // A task's hours per person = its working days (undated: its duration; a milestone: none) x the
    // sheet's working day x the person's share; cost = hours x the person's hourly cost, in their currency. Rounded once
    // per task and person (to the minute, to the cent). People not in the resources are left out.
    static GanttCosts compute(const GanttPlan &plan, const GanttSchedule &schedule, const GanttAssignments &assignments,
                              const QVector<GanttResource> &people);

    QHash<QString, Amount> tasks;                       // every task; a summary = the sum of its tasks
    QHash<QString, QHash<QString, Amount>> taskPeople;  // task -> person -> amount (summaries summed too)
    QHash<QString, Amount> people;                      // person -> every task
    Amount total;
    // Working day -> person -> amount; dated tasks only, their minutes spread evenly on their working
    // days by running totals, so the days add up to the task exactly (FEAT-127).
    QMap<QDate, QHash<QString, Amount>> days;
    // Monday of the ISO week -> person -> amount: the days summed.
    QMap<QDate, QHash<QString, Amount>> weeks;
};
