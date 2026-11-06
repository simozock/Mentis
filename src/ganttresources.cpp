#include "ganttresources.h"

#include <QHash>
#include <QJsonArray>
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
        GanttResources entry;
        entry.setCurrencyText(text);
        const QString clean = entry.normalized().currencyText();
        if (!currencies().contains(entry.normalized().currency) && !result.contains(clean))
            result.append(clean);
    }
    return result.mid(std::max<qsizetype>(0, result.size() - kMaxSavedCurrencies));
}

bool GanttResources::canSaveCurrency() const {
    const GanttResources clean = normalized();
    return !currencies().contains(clean.currency) && !savedCurrencies().contains(clean.currencyText());
}

void GanttResources::saveCurrency() const {
    if (!canSaveCurrency()) return;
    QStringList saved = savedCurrencies();
    saved.append(normalized().currencyText());
    QSettings().setValue(kSavedCurrenciesKey, saved.mid(std::max<qsizetype>(0, saved.size() - kMaxSavedCurrencies)));
}

void GanttResources::removeSavedCurrency(const QString &text) {
    QStringList saved = savedCurrencies();
    if (saved.removeAll(text) == 0) return;
    if (saved.isEmpty()) QSettings().remove(kSavedCurrenciesKey);
    else QSettings().setValue(kSavedCurrenciesKey, saved);
}

void GanttResources::setCurrencyText(const QString &text) {
    const QString value = text.simplified();
    const qsizetype space = value.lastIndexOf(QLatin1Char(' '));
    currency = space < 0 ? value : value.left(space);
    currencySymbol = space < 0 ? QString() : value.mid(space + 1);
}

QString GanttResources::currencyText() const {
    return currencySymbol.isEmpty() || currencySymbol == currency ? currency : currency + QLatin1Char(' ') + currencySymbol;
}

QString GanttResources::displaySymbol() const { return currencySymbol.isEmpty() ? currency : currencySymbol; }

GanttResources GanttResources::normalized() const {
    const auto text = [](const QString &value) { return value.trimmed().left(kMaxTextLength); };
    GanttResources result;
    result.currency = currency.simplified().left(kMaxCurrencyLength).trimmed();
    if (result.currency.isEmpty()) result.currency = currencies().front();
    result.currencySymbol = currencySymbol.simplified().remove(QLatin1Char(' ')).left(kMaxSymbolLength);
    if (result.currencySymbol.isEmpty() || result.currencySymbol == result.currency) result.currencySymbol = knownSymbol(result.currency);
    for (const GanttResource &person : people) {
        GanttResource clean{person.id, text(person.firstName), text(person.lastName), text(person.email), text(person.role),
                            std::clamp<qint64>(person.hourlyCostCents, 0, kMaxHourlyCostCents)};
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

QJsonObject GanttResources::toJson() const {
    QJsonObject json;
    if (currency != currencies().front()) json.insert(QStringLiteral("currency"), currency);
    if (currencySymbol != knownSymbol(currency)) json.insert(QStringLiteral("currencySymbol"), currencySymbol);
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
        array.append(object);
    }
    if (!array.isEmpty()) json.insert(QStringLiteral("people"), array);
    return json;
}

GanttResources GanttResources::fromJson(const QJsonObject &json) {
    GanttResources resources;
    resources.currency = json.value(QStringLiteral("currency")).toString(resources.currency);
    resources.currencySymbol = json.value(QStringLiteral("currencySymbol")).toString();
    for (const QJsonValue &value : json.value(QStringLiteral("people")).toArray()) {
        const QJsonObject object = value.toObject();
        const double cost = object.value(QStringLiteral("hourlyCost")).toDouble();
        resources.people.append({object.value(QStringLiteral("id")).toString(),
                                 object.value(QStringLiteral("firstName")).toString(),
                                 object.value(QStringLiteral("lastName")).toString(),
                                 object.value(QStringLiteral("email")).toString(),
                                 object.value(QStringLiteral("role")).toString(),
                                 std::isfinite(cost) ? qint64(std::llround(std::clamp(cost, 0.0, 100000.0) * 100.0)) : 0});
    }
    // Values from the file are repaired as a dialog's (FEAT-074 rule: invalid entries are repaired).
    return resources.normalized();
}
