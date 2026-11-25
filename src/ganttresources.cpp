#include "ganttresources.h"

#include <QHash>
#include <QJsonArray>
#include <QLocale>
#include <QSettings>
#include <QUuid>

#include <algorithm>
#include <cmath>

bool GanttResource::isEmpty() const {
    return firstName.isEmpty() && lastName.isEmpty() && email.isEmpty() && role.isEmpty() && hourlyCostCents == 0;
}

QString GanttResource::displayName() const {
    return QStringList{firstName, lastName}.join(QLatin1Char(' ')).simplified();
}

const QStringList &GanttResources::currencies() {
    static const QStringList list{QStringLiteral("EUR"), QStringLiteral("USD"), QStringLiteral("GBP"), QStringLiteral("CHF")};
    return list;
}

QString GanttResources::knownSymbol(const QString &currency) {
    static const QHash<QString, QString> symbols{
        {QStringLiteral("EUR"), QStringLiteral("€")}, {QStringLiteral("USD"), QStringLiteral("$")}, {QStringLiteral("GBP"), QStringLiteral("£")}};
    return symbols.value(currency);
}

namespace {
const QString kSavedCurrenciesKey = QStringLiteral("gantt/currencies");
}

QStringList GanttResources::savedCurrencies() {
    QStringList result;
    // Entries edited by hand are read as typed ones: normalized, duplicates skipped.
    for (const QString &text : QSettings().value(kSavedCurrenciesKey).toStringList()) {
        const GanttCurrency entry = GanttCurrency::fromText(text).normalized();
        if (!currencies().contains(entry.code) && !result.contains(entry.text())) result.append(entry.text());
    }
    return result.mid(std::max<qsizetype>(0, result.size() - kMaxSavedCurrencies));
}

void GanttResources::removeSavedCurrency(const QString &text) {
    QStringList saved = savedCurrencies();
    if (saved.removeAll(text) == 0) return;
    if (saved.isEmpty()) QSettings().remove(kSavedCurrenciesKey);
    else QSettings().setValue(kSavedCurrenciesKey, saved);
}

GanttCurrency GanttCurrency::fromText(const QString &text) {
    const QString value = text.simplified();
    const qsizetype space = value.lastIndexOf(QLatin1Char(' '));
    return {space < 0 ? value : value.left(space), space < 0 ? QString() : value.mid(space + 1)};
}

QString GanttCurrency::text() const { return symbol.isEmpty() || symbol == code ? code : code + QLatin1Char(' ') + symbol; }

QString GanttCurrency::displaySymbol() const { return symbol.isEmpty() ? code : symbol; }

GanttCurrency GanttCurrency::normalized() const {
    GanttCurrency result;
    // No spaces in the code either: "code symbol" is read back at the last space.
    result.code = code.simplified().remove(QLatin1Char(' ')).left(kMaxCodeLength);
    if (result.code.isEmpty()) result.code = GanttResources::currencies().front();
    result.symbol = symbol.simplified().remove(QLatin1Char(' ')).left(kMaxSymbolLength);
    if (result.symbol.isEmpty() || result.symbol == result.code) result.symbol = GanttResources::knownSymbol(result.code);
    return result;
}

bool GanttCurrency::canSave() const {
    const GanttCurrency clean = normalized();
    return !GanttResources::currencies().contains(clean.code) && !GanttResources::savedCurrencies().contains(clean.text());
}

void GanttCurrency::save() const {
    if (!canSave()) return;
    QStringList saved = GanttResources::savedCurrencies();
    saved.append(normalized().text());
    QSettings().setValue(kSavedCurrenciesKey,
                         saved.mid(std::max<qsizetype>(0, saved.size() - GanttResources::kMaxSavedCurrencies)));
}

GanttCurrency GanttResources::newPersonCurrency() const {
    return people.isEmpty() ? GanttCurrency() : people.constLast().currency.normalized();
}

QVector<GanttCurrency> GanttResources::usedCurrencies() const {
    QVector<GanttCurrency> result;
    for (const GanttResource &person : people)
        if (const GanttCurrency clean = person.currency.normalized(); !result.contains(clean)) result.append(clean);
    return result;
}

GanttResources GanttResources::normalized() const {
    const auto text = [](const QString &value) { return value.trimmed().left(kMaxTextLength); };
    GanttResources result;
    for (const GanttResource &person : people) {
        GanttResource clean{person.id, text(person.firstName), text(person.lastName), text(person.email), text(person.role),
                            std::clamp<qint64>(person.hourlyCostCents, 0, kMaxHourlyCostCents), person.currency.normalized()};
        if (clean.isEmpty()) continue;
        if (clean.id.isEmpty()) clean.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        result.people.append(clean);
    }
    return result;
}

bool GanttResources::isValidEmail(const QString &email) {
    const QString value = email.trimmed();
    if (value.isEmpty()) return true;
    const qsizetype at = value.indexOf(QLatin1Char('@'));
    return at > 0 && at == value.lastIndexOf(QLatin1Char('@')) && at < value.size() - 1;
}

GanttResourceProblems GanttResources::problems(const GanttResource &typed) {
    GanttResource clean = typed;
    for (QString *text : {&clean.firstName, &clean.lastName, &clean.email, &clean.role}) *text = text->trimmed();
    GanttResourceProblems result;
    result.email = !isValidEmail(clean.email);
    if (clean.isEmpty()) return result;
    // A person has first name, last name and role (user, 2026-10-30).
    result.firstName = clean.firstName.isEmpty();
    result.lastName = clean.lastName.isEmpty();
    result.role = clean.role.isEmpty();
    return result;
}

qint64 GanttResources::parseCost(const QString &text) {
    bool ok = false;
    const double value = QLocale().toDouble(text.trimmed(), &ok);
    return ok && std::isfinite(value) ? std::clamp<qint64>(std::llround(value * 100.0), 0, kMaxHourlyCostCents) : 0;
}

QString GanttResources::formatCost(qint64 cents, const QString &symbol) {
    return QLocale().toCurrencyString(double(cents) / 100.0, symbol, 2);
}

QJsonObject GanttResources::toJson() const {
    QJsonObject json;
    QJsonArray array;
    for (const GanttResource &person : people) {
        QJsonObject object{{QStringLiteral("id"), person.id}};
        const auto put = [&object](const char *key, const QString &value) {
            if (!value.isEmpty()) object.insert(QLatin1String(key), value);
        };
        put("firstName", person.firstName);
        put("lastName", person.lastName);
        put("email", person.email);
        put("role", person.role);
        if (person.hourlyCostCents != 0) object.insert(QStringLiteral("hourlyCost"), double(person.hourlyCostCents) / 100.0);
        if (person.currency.code != currencies().front()) object.insert(QStringLiteral("currency"), person.currency.code);
        if (person.currency.symbol != knownSymbol(person.currency.code))
            object.insert(QStringLiteral("currencySymbol"), person.currency.symbol);
        array.append(object);
    }
    if (!array.isEmpty()) json.insert(QStringLiteral("people"), array);
    return json;
}

GanttResources GanttResources::fromJson(const QJsonObject &json) {
    GanttResources resources;
    // Before FEAT-128 the document had one currency: the default of its people.
    const GanttCurrency documentCurrency{json.value(QStringLiteral("currency")).toString(currencies().front()),
                                         json.value(QStringLiteral("currencySymbol")).toString()};
    for (const QJsonValue &value : json.value(QStringLiteral("people")).toArray()) {
        const QJsonObject object = value.toObject();
        const double cost = object.value(QStringLiteral("hourlyCost")).toDouble();
        // EUR is not written, its symbol may be ("EUR x"): either key makes the currency the person's.
        const bool own = object.contains(QStringLiteral("currency")) || object.contains(QStringLiteral("currencySymbol"));
        const GanttCurrency currency = own ? GanttCurrency{object.value(QStringLiteral("currency")).toString(currencies().front()),
                                                           object.value(QStringLiteral("currencySymbol")).toString()}
                                           : documentCurrency;
        resources.people.append({object.value(QStringLiteral("id")).toString(),
                                 object.value(QStringLiteral("firstName")).toString(),
                                 object.value(QStringLiteral("lastName")).toString(),
                                 object.value(QStringLiteral("email")).toString(),
                                 object.value(QStringLiteral("role")).toString(),
                                 std::isfinite(cost) ? qint64(std::llround(std::clamp(cost, 0.0, 100000.0) * 100.0)) : 0,
                                 currency});
    }
    // Values from the file are repaired as a dialog's (FEAT-074 rule: invalid entries are repaired).
    return resources.normalized();
}
