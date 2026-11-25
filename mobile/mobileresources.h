#pragma once

#include "ganttpeoplecharts.h"
#include "ganttplan.h"
#include "ganttresources.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class MobileMap;

// FEAT-122 stage C1: the desktop's Resources modal (FEAT-079, ResourcesDialog) on the phone. It edits
// a draft of the document's people (each with their currency, FEAT-128) and of the active sheet's working week; Save applies
// it through Document::setResources (one Undo step), the X drops it. The rules are the core's
// (GanttResources::problems, parseCost, formatCost, the saved currencies; GanttCalendar::withWorkingWeek).
//
// `people` is rebuilt only when rows are added, removed or moved (not while typing, so a field keeps
// its focus): [{id, firstName, lastName, email, role, cost (the typed text)}]. `problems` follows the
// typing: by row, {firstName, lastName, role, email} true where the field is marked.
class MobileResources : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MobileMap *map READ map WRITE setMap NOTIFY mapChanged)
    Q_PROPERTY(QVariantList people READ people NOTIFY peopleChanged)
    Q_PROPERTY(QVariantList problems READ problems NOTIFY problemsChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY problemsChanged)
    // The cards' currency selects: [{value (the text, "EUR €"), name, saved (an × removes it)}], the
    // four listed, the saved ones, then the people's that are neither.
    Q_PROPERTY(QVariantList currencies READ currencies NOTIFY currencyChanged)
    // FEAT-128: by row, the person's currency ("EUR €") and the symbol after their cost field.
    Q_PROPERTY(QStringList rowCurrencies READ rowCurrencies NOTIFY currencyChanged)
    Q_PROPERTY(QStringList rowSymbols READ rowSymbols NOTIFY currencyChanged)
    Q_PROPERTY(bool saturday READ saturday WRITE setSaturday NOTIFY weekChanged)
    Q_PROPERTY(bool sunday READ sunday WRITE setSunday NOTIFY weekChanged)
    Q_PROPERTY(int dayMinutes READ dayMinutes WRITE setDayMinutes NOTIFY weekChanged)
    Q_PROPERTY(int maxDayMinutes READ maxDayMinutes CONSTANT)
    Q_PROPERTY(int minutesStep READ minutesStep CONSTANT)
    // FEAT-127: the people's charts, the desktop's figures and texts (GanttPeopleCharts). scale: the
    // Gantt's (Days, Weeks, Months); chartRow: the unfolded card's row, -1 for the summary;
    // chartsShown: app setting "gantt/resourceCharts", as the desktop. `charts`: {empty, emptyText,
    // cost (a line per currency), personDays, days [{name, value, fraction}], roleBars [{title (the
    // currency with more than one, else empty), roles [{name, percent, color, fraction}]}], person (or
    // null): {name, line, progress, progressLine, top (the 100 % line's height, 0-1),
    // load [{height 0-1, overloaded, tip}]}}; nothing computed while hidden.
    Q_PROPERTY(int scale READ scale WRITE setScale NOTIFY scaleChanged)
    Q_PROPERTY(int chartRow READ chartRow WRITE setChartRow NOTIFY chartsChanged)
    Q_PROPERTY(bool chartsShown READ chartsShown WRITE setChartsShown NOTIFY chartsShownChanged)
    Q_PROPERTY(QVariantMap charts READ charts NOTIFY chartsChanged)

public:
    explicit MobileResources(QObject *parent = nullptr);

    MobileMap *map() const { return m_map; }
    void setMap(MobileMap *map);
    QVariantList people() const;
    QVariantList problems() const;
    bool valid() const;
    QVariantList currencies() const;
    QStringList rowCurrencies() const;
    QStringList rowSymbols() const;
    bool saturday() const { return m_saturday; }
    void setSaturday(bool on);
    bool sunday() const { return m_sunday; }
    void setSunday(bool on);
    // The length of a working day in minutes: rounded to a quarter hour, between one and 24 h.
    int dayMinutes() const { return m_minutesPerDay; }
    void setDayMinutes(int minutes);
    static int maxDayMinutes() { return GanttCalendar::kMaxMinutesPerDay; }
    static int minutesStep() { return GanttCalendar::kMinutesStep; }
    int scale() const { return int(m_scale); }
    void setScale(int scale);
    int chartRow() const { return m_chartRow; }
    void setChartRow(int row);
    bool chartsShown() const { return m_chartsShown; }
    void setChartsShown(bool shown);
    QVariantMap charts() const { return m_charts; }
    // The figures as computed for the draft (the tests compare them with the desktop's).
    GanttPeopleCharts chartFigures() const;

    // The draft from the document and the active sheet; false when no map is open.
    Q_INVOKABLE bool load();
    Q_INVOKABLE void addPerson();
    Q_INVOKABLE void removePerson(int row);
    Q_INVOKABLE void movePerson(int from, int to);
    // field: firstName, lastName, email, role or cost; the text as typed.
    Q_INVOKABLE void setText(int row, const QString &field, const QString &text);
    Q_INVOKABLE void chooseCurrency(int row, const QString &text);
    // A saved currency removed from this device; the people using it keep it.
    Q_INVOKABLE void removeSavedCurrency(const QString &text);
    // The Add currency modal: 0 nothing typed, 1 already in the list, 2 can be added.
    Q_INVOKABLE static int currencyCheck(const QString &code, const QString &symbol);
    Q_INVOKABLE bool addCurrency(int row, const QString &code, const QString &symbol);  // saved here and chosen for the row
    // The draft applied: one Undo step; false when invalid or nothing changes.
    Q_INVOKABLE bool save();

signals:
    void mapChanged();
    void peopleChanged();
    void problemsChanged();
    void currencyChanged();
    void weekChanged();
    void scaleChanged();
    void chartsChanged();
    void chartsShownChanged();

private:
    struct Row {
        GanttResource person;  // texts as typed and the currency, cost unused
        QString cost;          // as typed
    };
    GanttResource typed(const Row &row) const;
    GanttResources draft() const;
    void updateCharts();

    QPointer<MobileMap> m_map;
    QVector<Row> m_rows;
    QStringList m_openedCurrencies;  // the people's when the page opened: always offered
    GanttCalendar m_calendar;    // the other weekdays and holidays kept
    bool m_saturday = true;
    bool m_sunday = true;
    int m_minutesPerDay = GanttCalendar::kDefaultMinutesPerDay;
    GanttPlan m_plan;  // the sheet's tasks, for the charts
    GanttAssignments m_assignments;
    GanttCanvas::Scale m_scale = GanttCanvas::Scale::Days;
    int m_chartRow = -1;
    bool m_chartsShown = true;
    QVariantMap m_charts;
};
