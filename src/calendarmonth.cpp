#include "calendarmonth.h"

#include "localization.h"

namespace CalendarMonth {

namespace {
QString capitalized(const QString &text) { return text.isEmpty() ? text : text.left(1).toUpper() + text.mid(1); }
}  // namespace

QLocale namesLocale() { return QLocale(Localization::effectiveLanguage(Localization::preference())); }

QString title(const QDate &anyDay, const QLocale &names) {
    return capitalized(names.standaloneMonthName(anyDay.month())) + u' ' + QString::number(anyDay.year());
}

QString weekdayName(int day, const QLocale &names) { return capitalized(names.standaloneDayName(day, QLocale::ShortFormat).left(2)); }

QDate firstShown(const QDate &anyDay) {
    // The week starts on the locale's first day (Monday in Italy, Sunday in the US).
    const QDate month(anyDay.year(), anyDay.month(), 1);
    const int offset = (month.dayOfWeek() - int(QLocale().firstDayOfWeek()) + kColumns) % kColumns;
    return month.addDays(-offset);
}

}  // namespace CalendarMonth
