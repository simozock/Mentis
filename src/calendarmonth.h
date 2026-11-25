#pragma once
// The app's calendar rules without widgets (FEAT-122 stage B2), shared by the desktop's DatePicker and
// the phone's date picker: a month shown as six weeks from the locale's first day of the week, its
// title and the weekdays' two letters in the interface language (BUG-035).
#include <QDate>
#include <QLocale>
#include <QString>

namespace CalendarMonth {

constexpr int kColumns = 7;
constexpr int kRows = 6;  // every month fits in six weeks

// The locale of the interface language (FEAT-046) for month and day names, which follow the
// language chosen in Settings rather than the system's (BUG-035). The first day of the week stays
// the system's: it belongs to the region, not to the language.
QLocale namesLocale();
QString title(const QDate &anyDay, const QLocale &names);     // "January 2027"
QString weekdayName(int day, const QLocale &names);            // day: 1 = Monday; "Mo"
QDate firstShown(const QDate &anyDay);                         // the grid's first day

}  // namespace CalendarMonth
