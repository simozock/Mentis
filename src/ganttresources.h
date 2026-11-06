#pragma once

// FEAT-079: the people of the document's projects (one list for every sheet) and the currency of
// their hourly cost. No widgets; the document keeps it, the file stores it as JSON.

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct GanttResource {
    QString id;  // stable (UUID), for assigning people to tasks later
    QString firstName;
    QString lastName;
    QString email;
    QString role;
    qint64 hourlyCostCents = 0;  // cents: two decimals without rounding errors
    bool operator==(const GanttResource &) const = default;
    bool isEmpty() const;  // every field empty and no cost
    QString displayName() const;  // FEAT-081: "First Last", a missing part left out
};

struct GanttResources {
    static constexpr int kMaxTextLength = 200;
    static constexpr qint64 kMaxHourlyCostCents = 10000000;  // 100000.00
    static constexpr int kMaxCurrencyLength = 12;            // the currency's name, e.g. "EUR"
    static constexpr int kMaxSymbolLength = 5;               // its symbol, e.g. "€"
    static const QStringList &currencies();                  // EUR (default), USD, GBP, CHF
    // The symbol of a listed currency (€, $, £), empty for CHF and the others.
    static QString knownSymbol(const QString &currency);
    // Currencies the user saved on this computer ("RON lei"), offered after the listed ones in
    // every document: app settings "gantt/currencies", oldest first, at most kMaxSavedCurrencies.
    static constexpr int kMaxSavedCurrencies = 20;
    static QStringList savedCurrencies();
    bool canSaveCurrency() const;  // neither listed nor saved yet
    // Adds this currency if canSaveCurrency(); the oldest is dropped past the limit.
    void saveCurrency() const;
    // Removes a saved currency ("RON lei") from this computer; documents using it keep it.
    static void removeSavedCurrency(const QString &text);

    QString currency = QStringLiteral("EUR");  // a listed one or typed by the user
    QString currencySymbol = QStringLiteral("€");  // empty: the name is shown with the amounts
    QVector<GanttResource> people;
    bool operator==(const GanttResources &) const = default;

    // "EUR €": the name, then the symbol after the last space if any (FEAT-079).
    void setCurrencyText(const QString &text);
    QString currencyText() const;  // "EUR €", "CHF"
    QString displaySymbol() const;  // the symbol, or the name when there is none

    // Texts trimmed and cut to kMaxTextLength, cost clamped, empty rows dropped, a missing id given;
    // no currency name means EUR, a listed currency without a symbol takes its own.
    GanttResources normalized() const;
    // Empty, or one "@" with text on both sides (FEAT-079).
    static bool isValidEmail(const QString &email);
    // Defaults and empty fields are not written: an empty list in EUR is an empty object.
    QJsonObject toJson() const;
    static GanttResources fromJson(const QJsonObject &json);
};
