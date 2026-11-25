#include "mobileresources.h"

#include "document.h"
#include "mindmapmodel.h"
#include "mobilemap.h"

#include <QLocale>
#include <QSettings>

#include <algorithm>

namespace {

GanttCurrency typedCurrency(const QString &code, const QString &symbol) { return GanttCurrency{code, symbol}.normalized(); }

}  // namespace

MobileResources::MobileResources(QObject *parent)
    : QObject(parent), m_chartsShown(QSettings().value(QStringLiteral("gantt/resourceCharts"), true).toBool()) {}

void MobileResources::setMap(MobileMap *map) {
    if (m_map == map) return;
    m_map = map;
    emit mapChanged();
}

bool MobileResources::load() {
    Document *document = m_map && m_map->isOpen() ? m_map->document() : nullptr;
    if (!document) return false;
    const GanttResources resources = document->resources();
    m_rows.clear();
    for (const GanttResource &person : resources.people)
        m_rows.append({person, person.hourlyCostCents ? QLocale().toString(double(person.hourlyCostCents) / 100.0, 'f', 2) : QString()});
    m_openedCurrencies.clear();
    for (const GanttCurrency &used : resources.usedCurrencies()) m_openedCurrencies.append(used.text());
    m_calendar = document->model()->gantt().calendar();
    m_saturday = m_calendar.workingDays.contains(Qt::Saturday);
    m_sunday = m_calendar.workingDays.contains(Qt::Sunday);
    m_minutesPerDay = m_calendar.minutesPerDay;
    m_plan = document->model()->gantt();
    m_assignments = document->model()->ganttAssignments();
    m_chartRow = -1;
    updateCharts();
    emit peopleChanged();
    emit problemsChanged();
    emit currencyChanged();
    emit weekChanged();
    return true;
}

QVariantList MobileResources::people() const {
    QVariantList result;
    for (const Row &row : m_rows)
        result.append(QVariantMap{{QStringLiteral("id"), row.person.id},
                                  {QStringLiteral("firstName"), row.person.firstName},
                                  {QStringLiteral("lastName"), row.person.lastName},
                                  {QStringLiteral("email"), row.person.email},
                                  {QStringLiteral("role"), row.person.role},
                                  {QStringLiteral("cost"), row.cost}});
    return result;
}

GanttResource MobileResources::typed(const Row &row) const {
    GanttResource person = row.person;
    person.hourlyCostCents = GanttResources::parseCost(row.cost);
    return person;
}

QVariantList MobileResources::problems() const {
    QVariantList result;
    for (const Row &row : m_rows) {
        const GanttResourceProblems wrong = GanttResources::problems(typed(row));
        result.append(QVariantMap{{QStringLiteral("firstName"), wrong.firstName},
                                  {QStringLiteral("lastName"), wrong.lastName},
                                  {QStringLiteral("role"), wrong.role},
                                  {QStringLiteral("email"), wrong.email}});
    }
    return result;
}

bool MobileResources::valid() const {
    return std::none_of(m_rows.cbegin(), m_rows.cend(), [this](const Row &row) { return GanttResources::problems(typed(row)).any(); });
}

QVariantList MobileResources::currencies() const {
    QVariantList result;
    QStringList shown;
    const auto add = [&](const QString &text, bool saved) {
        if (shown.contains(text)) return;
        shown.append(text);
        result.append(QVariantMap{{QStringLiteral("value"), text}, {QStringLiteral("name"), text},
                                  {QStringLiteral("group"), QString()}, {QStringLiteral("saved"), saved}});
    };
    for (const QString &code : GanttResources::currencies()) add(GanttCurrency{code, GanttResources::knownSymbol(code)}.text(), false);
    for (const QString &text : GanttResources::savedCurrencies()) add(text, true);
    // Neither listed nor saved here (another device's file, or a saved one removed): offered
    // without ×, as long as the page is open.
    for (const QString &text : m_openedCurrencies) add(text, false);
    for (const QString &text : rowCurrencies()) add(text, false);
    return result;
}

QStringList MobileResources::rowCurrencies() const {
    QStringList result;
    for (const Row &row : m_rows) result.append(row.person.currency.normalized().text());
    return result;
}

QStringList MobileResources::rowSymbols() const {
    QStringList result;
    for (const Row &row : m_rows) result.append(row.person.currency.normalized().displaySymbol());
    return result;
}

void MobileResources::setSaturday(bool on) {
    if (m_saturday == on) return;
    m_saturday = on;
    emit weekChanged();
    updateCharts();
}

void MobileResources::setSunday(bool on) {
    if (m_sunday == on) return;
    m_sunday = on;
    emit weekChanged();
    updateCharts();
}

void MobileResources::setDayMinutes(int minutes) {
    constexpr int step = GanttCalendar::kMinutesStep;
    const int rounded = std::clamp((minutes + step / 2) / step * step, step, GanttCalendar::kMaxMinutesPerDay);
    if (rounded == m_minutesPerDay) return;
    m_minutesPerDay = rounded;
    emit weekChanged();
    updateCharts();
}

void MobileResources::addPerson() {
    Row row;
    // FEAT-128: the last person's currency, EUR for the first.
    if (!m_rows.isEmpty()) row.person.currency = m_rows.constLast().person.currency.normalized();
    m_rows.append(row);
    emit peopleChanged();
    emit problemsChanged();
    emit currencyChanged();
}

void MobileResources::removePerson(int row) {
    if (row < 0 || row >= m_rows.size()) return;
    m_rows.removeAt(row);
    if (m_chartRow == row) m_chartRow = -1;
    else if (m_chartRow > row) --m_chartRow;
    emit peopleChanged();
    emit problemsChanged();
    emit currencyChanged();
    updateCharts();
}

void MobileResources::movePerson(int from, int to) {
    if (from == to || from < 0 || to < 0 || from >= m_rows.size() || to >= m_rows.size()) return;
    m_rows.move(from, to);
    if (m_chartRow == from) m_chartRow = to;
    else if (from < m_chartRow && m_chartRow <= to) --m_chartRow;
    else if (to <= m_chartRow && m_chartRow < from) ++m_chartRow;
    emit peopleChanged();
    emit problemsChanged();
    emit currencyChanged();
    updateCharts();
}

void MobileResources::setText(int row, const QString &field, const QString &text) {
    if (row < 0 || row >= m_rows.size()) return;
    Row &edited = m_rows[row];
    const QString value = text.left(GanttResources::kMaxTextLength);
    if (field == QLatin1String("firstName")) edited.person.firstName = value;
    else if (field == QLatin1String("lastName")) edited.person.lastName = value;
    else if (field == QLatin1String("email")) edited.person.email = value;
    else if (field == QLatin1String("role")) edited.person.role = value;
    else if (field == QLatin1String("cost")) edited.cost = value;
    else {
        return;
    }
    emit problemsChanged();
    updateCharts();
}

void MobileResources::chooseCurrency(int row, const QString &text) {
    if (row < 0 || row >= m_rows.size()) return;
    const GanttCurrency clean = GanttCurrency::fromText(text).normalized();
    if (clean == m_rows.at(row).person.currency.normalized()) return;
    m_rows[row].person.currency = clean;
    emit currencyChanged();
    updateCharts();
}

void MobileResources::removeSavedCurrency(const QString &text) {
    if (!GanttResources::savedCurrencies().contains(text)) return;
    GanttResources::removeSavedCurrency(text);
    emit currencyChanged();  // the people using it keep it, offered without ×
}

int MobileResources::currencyCheck(const QString &code, const QString &symbol) {
    if (code.trimmed().isEmpty()) return 0;
    return typedCurrency(code, symbol).canSave() ? 2 : 1;
}

bool MobileResources::addCurrency(int row, const QString &code, const QString &symbol) {
    if (row < 0 || row >= m_rows.size() || currencyCheck(code, symbol) != 2) return false;
    const GanttCurrency added = typedCurrency(code, symbol);
    added.save();  // at once, also if the modal is then cancelled
    m_rows[row].person.currency = added;
    emit currencyChanged();
    updateCharts();
    return true;
}

GanttResources MobileResources::draft() const {
    GanttResources result;
    for (const Row &row : m_rows) result.people.append(typed(row));
    return result.normalized();
}

bool MobileResources::save() {
    Document *document = m_map && m_map->isOpen() ? m_map->document() : nullptr;
    if (!document || !valid()) return false;
    const GanttCalendar calendar = m_calendar.withWorkingWeek(m_saturday, m_sunday, m_minutesPerDay);
    if (!document->setResources(draft(), calendar)) return false;
    m_map->finishEdit(m_map->selectedId());
    return true;
}

void MobileResources::setScale(int scale) {
    const auto chosen = GanttCanvas::Scale(std::clamp(scale, int(GanttCanvas::Scale::Days), int(GanttCanvas::Scale::Months)));
    if (chosen == m_scale) return;
    m_scale = chosen;
    emit scaleChanged();
    updateCharts();
}

void MobileResources::setChartRow(int row) {
    const int shown = row >= 0 && row < m_rows.size() ? row : -1;
    if (shown == m_chartRow) return;
    m_chartRow = shown;
    if (m_chartsShown) updateCharts();
    else emit chartsChanged();  // chartRow's notification, nothing computed
}

void MobileResources::setChartsShown(bool shown) {
    if (shown == m_chartsShown) return;
    m_chartsShown = shown;
    QSettings().setValue(QStringLiteral("gantt/resourceCharts"), shown);
    emit chartsShownChanged();
    updateCharts();
}

GanttPeopleCharts MobileResources::chartFigures() const {
    GanttPlan plan = m_plan;
    plan.setCalendar(m_calendar.withWorkingWeek(m_saturday, m_sunday, m_minutesPerDay));
    // The rows as typed, blank ones left out (Save drops them); a new person gets an id of its row.
    QVector<GanttResource> people;
    for (int row = 0; row < m_rows.size(); ++row) {
        GanttResource person = typed(m_rows.at(row));
        if (person.isEmpty()) continue;
        if (person.id.isEmpty()) person.id = QStringLiteral("row:%1").arg(row);
        people.append(person);
    }
    return GanttPeopleCharts::compute(plan, m_assignments, people, m_scale);
}

void MobileResources::updateCharts() {
    if (!m_chartsShown) return;  // hidden charts compute nothing
    const GanttPeopleCharts figures = chartFigures();
    int person = -1;  // the chart row's index among the people that are not blank
    for (int row = 0, index = 0; row < m_rows.size(); ++row) {
        if (typed(m_rows.at(row)).isEmpty()) continue;
        if (row == m_chartRow) person = index;
        ++index;
    }
    QVariantList days;
    qint64 most = 1;
    for (const GanttPeopleCharts::Bar &bar : figures.days) most = std::max(most, bar.minutes);
    for (const GanttPeopleCharts::Bar &bar : figures.days)
        days.append(QVariantMap{{QStringLiteral("name"), bar.name}, {QStringLiteral("value"), figures.dayCount(bar.minutes)},
                                {QStringLiteral("fraction"), double(bar.minutes) / double(most)}});
    QVariantList roleBars;
    for (const GanttPeopleCharts::RoleBar &bar : figures.roleBars) {
        QVariantList roles;
        for (const GanttPeopleCharts::Role &role : bar.roles)
            roles.append(QVariantMap{{QStringLiteral("name"), role.name}, {QStringLiteral("percent"), QStringLiteral("%1%").arg(role.percent)},
                                     {QStringLiteral("color"), role.color.name()},
                                     {QStringLiteral("fraction"), bar.cents > 0 ? double(role.cents) / double(bar.cents) : 0.0}});
        roleBars.append(QVariantMap{{QStringLiteral("title"), figures.roleBarTitle(bar)}, {QStringLiteral("roles"), roles}});
    }
    QVariant shownPerson;
    if (person >= 0 && person < figures.people.size()) {  // repeated ids are dropped by compute
        const GanttPeopleCharts::Person &chosen = figures.people.at(person);
        int top = 100;
        for (const GanttPeopleCharts::Period &period : chosen.load) top = std::max(top, period.percent());
        QVariantList load;
        for (const GanttPeopleCharts::Period &period : chosen.load)
            load.append(QVariantMap{{QStringLiteral("height"), double(period.percent()) / top},
                                    {QStringLiteral("overloaded"), period.overloaded()},
                                    {QStringLiteral("tip"), figures.periodTip(period)}});
        shownPerson = QVariantMap{{QStringLiteral("name"), chosen.name},
                                  {QStringLiteral("line"), figures.personLine(chosen)},
                                  {QStringLiteral("progress"), double(std::clamp(chosen.progressPercent(), 0, 100)) / 100.0},
                                  {QStringLiteral("progressLine"), figures.progressLine(chosen)},
                                  {QStringLiteral("top"), 100.0 / top},
                                  {QStringLiteral("load"), load}};
    }
    m_charts = QVariantMap{{QStringLiteral("empty"), figures.empty()},
                           {QStringLiteral("emptyText"), GanttPeopleCharts::emptyText()},
                           {QStringLiteral("cost"), figures.costText()},
                           {QStringLiteral("personDays"), figures.personDays()},
                           {QStringLiteral("days"), days},
                           {QStringLiteral("roleBars"), roleBars},
                           {QStringLiteral("person"), shownPerson}};
    emit chartsChanged();
}
