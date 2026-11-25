#pragma once

// FEAT-079: the people of the document's projects (one list for every sheet); FEAT-128: each with
// the currency of their hourly cost. No widgets; the document keeps it, the file stores it as JSON.

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

// FEAT-079, FEAT-128: a currency, its code and its symbol, written "EUR €" (the symbol after the
// last space, if any).
struct GanttCurrency {
    static constexpr int kMaxCodeLength = 12;   // e.g. "EUR"
    static constexpr int kMaxSymbolLength = 5;  // e.g. "€"
    QString code = QStringLiteral("EUR");
    QString symbol = QStringLiteral("€");  // empty: the code is shown with the amounts
    bool operator==(const GanttCurrency &) const = default;

    static GanttCurrency fromText(const QString &text);  // "EUR €", "CHF"; not normalized
    QString text() const;                                // "EUR €", "CHF"
    QString displaySymbol() const;                       // the symbol, or the code when there is none
    // Trimmed and cut; no code means EUR, a listed currency without a symbol takes its own.
    GanttCurrency normalized() const;
    bool canSave() const;  // neither listed nor saved yet
    // Adds this currency on this computer if canSave(); the oldest is dropped past the limit.
    void save() const;
};

struct GanttResource {
    QString id;  // stable (UUID), for assigning people to tasks later
    QString firstName;
    QString lastName;
    QString email;
    QString role;
    qint64 hourlyCostCents = 0;  // cents: two decimals without rounding errors
    GanttCurrency currency;      // of the hourly cost (FEAT-128)
    bool operator==(const GanttResource &) const = default;
    bool isEmpty() const;  // every field empty and no cost, whatever the currency
    QString displayName() const;  // FEAT-081: "First Last", a missing part left out
};

// FEAT-079: the fields of a typed person the Resources modal marks (and that keep Save disabled): a
// person not blank lacks first name, last name or role; an email, if any, is invalid.
struct GanttResourceProblems {
    bool firstName = false;
    bool lastName = false;
    bool role = false;
    bool email = false;
    bool any() const { return firstName || lastName || role || email; }
};

struct GanttResources {
    static constexpr int kMaxTextLength = 200;
    static constexpr qint64 kMaxHourlyCostCents = 10000000;  // 100000.00
    static const QStringList &currencies();                  // EUR (default), USD, GBP, CHF
    // The symbol of a listed currency (€, $, £), empty for CHF and the others.
    static QString knownSymbol(const QString &currency);
    // Currencies the user saved on this computer ("RON lei"), offered after the listed ones in
    // every document: app settings "gantt/currencies", oldest first, at most kMaxSavedCurrencies.
    static constexpr int kMaxSavedCurrencies = 20;
    static QStringList savedCurrencies();
    // Removes a saved currency ("RON lei") from this computer; people using it keep it.
    static void removeSavedCurrency(const QString &text);

    QVector<GanttResource> people;
    bool operator==(const GanttResources &) const = default;

    // The currency of a new person: the last person's, EUR for the first (FEAT-128).
    GanttCurrency newPersonCurrency() const;
    // The people's currencies, normalized, each once, in the people's order.
    QVector<GanttCurrency> usedCurrencies() const;

    // Texts trimmed and cut to kMaxTextLength, cost clamped, currencies normalized, empty rows
    // dropped, a missing id given.
    GanttResources normalized() const;
    // Empty, or one "@" with text on both sides (FEAT-079).
    static bool isValidEmail(const QString &email);
    // Texts trimmed; a row with every field empty is not checked (it is dropped on Save).
    static GanttResourceProblems problems(const GanttResource &typed);
    // A typed hourly cost in the interface's locale: cents, clamped; 0 when it is not a number.
    static qint64 parseCost(const QString &text);
    // "€45.00" / "45,00 €" in the interface's locale, with the currency's symbol or name.
    static QString formatCost(qint64 cents, const QString &symbol);
    // Defaults and empty fields are not written: an empty list is an empty object. A person's
    // currency: "currency" left out for EUR, "currencySymbol" when the listed one's own. A file
    // written before FEAT-128 has the currency once for the document: its people take it.
    QJsonObject toJson() const;
    static GanttResources fromJson(const QJsonObject &json);
};
