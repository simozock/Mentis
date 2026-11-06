#include "localization.h"

#include "offeredtranslations.h"

#include <QCoreApplication>
#include <QHash>
#include <QLibraryInfo>
#include <QSettings>
#include <QTranslator>

#include <algorithm>
#include <utility>
#include <vector>

namespace Localization {
namespace {
const QString kSettingsKey = QStringLiteral("ui/language");
const QString kSourceLanguage = QStringLiteral("en");  // lingua dei testi nel codice (ADR-003)

// The name most locales of a language and script share: "español", "Deutsch", "português".
// Qt has no territory-neutral endonym: QLocale("es").nativeLanguageName() is the default
// territory's "español de España". Ties go to the shorter name.
QString genericName(const QLocale &locale) {
    QHash<QString, int> counts;
    QString best;
    for (const QLocale &variant : QLocale::matchingLocales(locale.language(), locale.script(), QLocale::AnyTerritory)) {
        const QString name = variant.nativeLanguageName();
        if (name.isEmpty()) continue;
        const int count = ++counts[name];
        const int bestCount = counts.value(best);
        if (best.isEmpty() || count > bestCount || (count == bestCount && name.size() < best.size())) best = name;
    }
    return best.isEmpty() ? locale.nativeLanguageName() : best;
}

// The code the system gives ("pt-BR", "zh-Hans-CN") matched without case against the offered ones.
QString findCode(const QStringList &available, const QString &code) {
    for (const QString &candidate : available)
        if (candidate.compare(code, Qt::CaseInsensitive) == 0) return candidate;
    return {};
}
}  // namespace

// FEAT-046, FEAT-091: the translations come from translations/<code>.json; the build lists the ones at
// least 80% finished (the user, 2026-11-02). English is the source, always there.
QStringList availableLanguages() { return orderedLanguages(QStringList{MENTIS_OFFERED_TRANSLATIONS}); }

// English first, then by native name (the user, 2026-11-02: a rule that needs no list in the code).
QStringList orderedLanguages(const QStringList &translations) {
    std::vector<std::pair<QString, QString>> named;  // (name for sorting, code)
    for (const QString &code : translations) {
        if (code == kSourceLanguage || std::any_of(named.cbegin(), named.cend(), [&](const auto &entry) { return entry.second == code; }))
            continue;
        named.emplace_back(nativeName(code).toCaseFolded(), code);
    }
    std::sort(named.begin(), named.end());
    QStringList languages{kSourceLanguage};
    for (const auto &entry : named) languages.append(entry.second);
    return languages;
}

QString nativeName(const QString &code) {
    if (code == kSourceLanguage) return QStringLiteral("English");  // QLocale says "American English"
    const QLocale locale(code);
    if (locale.language() == QLocale::C) return code;
    // Language codes: "it", "pt_BR" (territory), "zh_Hans" (script, whose name says it: 简体中文).
    const QStringList parts = code.split(QLatin1Char('_'));
    const bool territory = parts.size() > 1 && (parts.last().size() == 2 || parts.last().size() == 3);
    const bool script = std::any_of(parts.cbegin() + 1, parts.cend(), [](const QString &part) { return part.size() == 4; });
    QString name;
    if (territory) {
        name = locale.nativeLanguageName();  // "British English", but plain "português" for pt_BR
        if (name == genericName(locale)) name += QStringLiteral(" (%1)").arg(locale.nativeTerritoryName());
    } else {
        name = script ? locale.nativeLanguageName() : genericName(locale);
    }
    if (name.isEmpty()) return code;
    return locale.toUpper(name.left(1)) + name.mid(1);
}

QString preference() {
    const QString code = QSettings().value(kSettingsKey).toString();
    return availableLanguages().contains(code) ? code : QString();
}

void setPreference(const QString &code) {
    QSettings settings;
    if (availableLanguages().contains(code)) settings.setValue(kSettingsKey, code);
    else settings.remove(kSettingsKey);
}

QString effectiveLanguage(const QString &preference, const QStringList &systemLanguages, const QStringList &available) {
    if (available.contains(preference)) return preference;
    // uiLanguages() da' codici come "it-CH" o "en-US", in ordine di preferenza del sistema.
    for (const QString &language : systemLanguages) {
        QString code = language;
        code.replace(QLatin1Char('-'), QLatin1Char('_'));
        for (;;) {  // zh_Hans_CN, zh_Hans, zh
            const QString found = findCode(available, code);
            if (!found.isEmpty()) return found;
            const qsizetype cut = code.lastIndexOf(QLatin1Char('_'));
            if (cut < 0) break;
            code.truncate(cut);
        }
        const QLocale system(language);
        if (system.language() == QLocale::C) continue;
        for (const QString &candidate : available) {  // pt-PT -> pt_BR, but zh-TW is not zh_Hans
            const QLocale offered(candidate);
            if (offered.language() == system.language() && offered.script() == system.script()) return candidate;
        }
    }
    return kSourceLanguage;
}

QString installTranslators() {
    const QString language = effectiveLanguage(preference());
    if (language == kSourceLanguage) return language;
    // Statici: devono restare installati per tutta la vita dell'applicazione.
    static QTranslator appTranslator;
    static QTranslator qtTranslator;
    if (appTranslator.load(QStringLiteral("mentis_") + language, QStringLiteral(":/i18n")))
        QCoreApplication::installTranslator(&appTranslator);
    // By locale: Qt ships qtbase_pt_BR and qtbase_zh_CN, found for pt_BR and zh_Hans.
    if (qtTranslator.load(QLocale(language), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(&qtTranslator);
    return language;
}

}  // namespace Localization
