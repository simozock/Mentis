#pragma once

// FEAT-079: the Resources modal of the Gantt page. The people of the document (first name, last
// name, email, role, hourly cost), numbered and moved by their number, with their currency, and the sheet's working week (Saturday,
// Sunday, hours in a day). It only edits a copy: the caller applies resources() and calendar()
// after Save (Document::setResources, one history step).

#include "ganttplan.h"
#include "ganttresources.h"
#include "modaldialog.h"

#include <QComboBox>

class NumberField;
class QLabel;
class QPushButton;
class QTableWidget;
class ToggleSwitch;

class ResourcesDialog final : public ModalDialog {
    Q_OBJECT
public:
    enum Column { FirstName, LastName, Email, Role, HourlyCost, Remove, ColumnCount };
    static constexpr int kHeaderHeightPx = 28;
    static constexpr int kRowHeightPx = 32;
    static constexpr int kMinVisibleRows = 3;  // the table's height with fewer rows (user, 2026-10-30)
    static constexpr int kVisibleRows = 10;    // more rows scroll

    ResourcesDialog(QWidget *parent, const GanttResources &resources, const GanttCalendar &calendar);

    GanttResources resources() const;  // normalized: empty rows dropped
    GanttCalendar calendar() const;
    // "€45.00" / "45,00 €" in the interface's locale, with the currency's symbol or name.
    static QString formatCost(qint64 cents, const QString &symbol);

    QTableWidget *table() const { return m_table; }
    QPushButton *addButton() const { return m_add; }
    QComboBox *currencySelect() const { return m_currency; }
    // The last item of the currency select, "+ Add currency...": opens addCurrency() (user, 2026-10-30).
    int addCurrencyItem() const { return m_currency->count() - 1; }
    void addCurrency();                  // the "Add currency" modal; Add saves on this computer and chooses it
    void removeSavedCurrency(int item);  // a saved currency of the select from this computer
    ToggleSwitch *saturdaySwitch() const { return m_saturday; }
    ToggleSwitch *sundaySwitch() const { return m_sunday; }
    NumberField *hoursField() const { return m_hours; }
    QComboBox *minutesSelect() const { return m_minutes; }
    QLabel *emptyLabel() const { return m_empty; }

    void addPerson();  // an empty row, its first name being edited
    // The person of row `from` moved to row `to` (user, 2026-10-30: dragged by its number); the
    // numbers stay 1, 2, 3 in order.
    void movePerson(int from, int to);

private:
    void insertRow(int row, const GanttResource &person);
    GanttResource rowPerson(int row) const;  // as typed, not normalized
    void removeRow(QWidget *button);
    void updateRows();      // table height, empty text
    void updateValidity();  // email cells marked, Save enabled
    GanttResources currency() const;  // the select's currency, normalized, without people
    void insertCurrencyItem(const QString &text, bool saved);  // before "+ Add currency..."
    void setCurrencyItemSaved(int item, bool saved);           // the × and its tooltip
    int findCurrencyItem(const QString &text) const;           // -1: not in the select
    bool eventFilter(QObject *watched, QEvent *event) override;  // the × of the saved currencies

    GanttCalendar m_calendar;  // other weekdays and holidays are kept as they are
    QTableWidget *m_table;
    QLabel *m_empty;
    QPushButton *m_add;
    QComboBox *m_currency;
    QString m_documentCurrency;  // the currency the modal was opened with: always offered
    QString m_shownCurrency;     // the chosen item's text, kept while "+ Add currency..." is picked
    ToggleSwitch *m_saturday;
    ToggleSwitch *m_sunday;
    NumberField *m_hours;
    QComboBox *m_minutes;
};
