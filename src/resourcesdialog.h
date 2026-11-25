#pragma once

// FEAT-079: the Resources modal of the Gantt page. The people of the document (first name, last
// name, email, role, hourly cost and its currency, FEAT-128), numbered and moved by their number, and the sheet's working week (Saturday,
// Sunday, hours in a day). It only edits a copy: the caller applies resources() and calendar()
// after Save (Document::setResources, one history step).
// FEAT-127: the people's charts beside People and Working week when the window holds a
// kWideSurfaceWidthPx modal, under the people otherwise; read-only, recomputed from the modal's copy
// at every change. A toggle in the title hides them (app setting kChartsSetting).

#include "ganttcanvas.h"
#include "ganttcosts.h"
#include "ganttplan.h"
#include "ganttresources.h"
#include "modaldialog.h"

#include <QComboBox>

class NumberField;
class PeopleChartsPanel;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class QPushButton;
class QTableWidget;
class ToggleSwitch;

class ResourcesDialog final : public ModalDialog {
    Q_OBJECT
public:
    enum Column { FirstName, LastName, Email, Role, HourlyCost, Currency, Remove, ColumnCount };
    static constexpr int kHeaderHeightPx = 28;
    static constexpr int kRowHeightPx = 32;
    static constexpr int kMinVisibleRows = 3;  // the table's height with fewer rows (user, 2026-10-30)
    static constexpr int kVisibleRows = 10;    // more rows scroll

    static constexpr int kWideSurfaceWidthPx = 1060;  // with the charts beside (FEAT-127)
    static constexpr char kChartsSetting[] = "gantt/resourceCharts";

    // plan, assignments: the sheet's, for the charts; scale: the Gantt's (Days, Weeks, Months).
    ResourcesDialog(QWidget *parent, const GanttResources &resources, const GanttCalendar &calendar,
                    const GanttPlan &plan = GanttPlan(), const GanttAssignments &assignments = {},
                    GanttCanvas::Scale scale = GanttCanvas::Scale::Days);

    GanttResources resources() const;  // normalized: empty rows dropped
    GanttCalendar calendar() const;

    QTableWidget *table() const { return m_table; }
    QPushButton *addButton() const { return m_add; }
    // FEAT-128: each row's currency select (listed, saved on this computer with an ×, the ones of
    // the modal's people, then "+ Add currency..."), nullptr for a row that does not exist.
    QComboBox *currencySelect(int row) const;
    // The last item of a row's select, "+ Add currency...": opens addCurrency(row) (user, 2026-10-30).
    int addCurrencyItem(int row) const { return currencySelect(row)->count() - 1; }
    void addCurrency(int row);                    // the "Add currency" modal; Add saves on this computer and chooses it for the row
    void removeSavedCurrency(int row, int item);  // a saved currency of a row's select from this computer
    ToggleSwitch *saturdaySwitch() const { return m_saturday; }
    ToggleSwitch *sundaySwitch() const { return m_sunday; }
    NumberField *dayLengthField() const { return m_length; }  // minutes, shown as hours.minutes
    QLabel *emptyLabel() const { return m_empty; }

    void addPerson();  // an empty row, its first name being edited
    // The person of row `from` moved to row `to` (user, 2026-10-30: dragged by its number); the
    // numbers stay 1, 2, 3 in order.
    void movePerson(int from, int to);

    // FEAT-127.
    QToolButton *chartsButton() const { return m_chartsButton; }
    PeopleChartsPanel *charts() const { return m_charts; }
    bool chartsShown() const { return m_chartsShown; }
    bool chartsBeside() const { return m_beside; }
    int chartRow() const { return m_chartRow; }  // the person shown, -1 for the summary
    void setChartsShown(bool shown);             // kept in kChartsSetting
    void showChartRow(int row);                  // -1: the summary

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updateCharts();       // recomputed from the modal's copy, if shown
    void updateChartsPlace();  // beside or under, the modal's width

    void insertRow(int row, const GanttResource &person);
    GanttResource rowPerson(int row) const;  // as typed, not normalized
    int rowOfCell(const QWidget *child, int column) const;  // the row whose cell widget holds child, -1
    void removeRow(QWidget *button);
    void updateRows();      // table height, empty text
    void updateValidity();  // email cells marked, Save enabled
    void setRowCurrency(int row, const QString &text);  // the person's currency, its select and cost shown
    void fillCurrencySelect(QComboBox *select, const QString &current) const;
    void refreshCurrencySelects();  // after a currency is saved or removed on this computer
    bool eventFilter(QObject *watched, QEvent *event) override;  // the × of the saved currencies; chart rows

    GanttCalendar m_calendar;  // other weekdays and holidays are kept as they are
    QTableWidget *m_table;
    QLabel *m_empty;
    QPushButton *m_add;
    QStringList m_openedCurrencies;  // the people's currencies the modal was opened with: always offered
    ToggleSwitch *m_saturday;
    ToggleSwitch *m_sunday;
    NumberField *m_length;
    // FEAT-127.
    GanttPlan m_plan;
    GanttAssignments m_assignments;
    GanttCanvas::Scale m_scale;
    QScrollArea *m_scroll;
    QHBoxLayout *m_body;
    QVBoxLayout *m_under;  // in the left column, after the people
    QWidget *m_chartsLine;
    PeopleChartsPanel *m_charts;
    QToolButton *m_chartsButton;
    int m_chartRow = -1;
    bool m_beside = false;
    bool m_chartsShown = true;
    bool m_placed = false;  // updateChartsPlace has run
    bool m_settingCurrent = false;  // the current cell set by code: not the user's choice of a person
};
