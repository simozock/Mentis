#pragma once

// Lingua dell'interfaccia (FEAT-046, ADR-003): i testi del codice sono in inglese e le altre
// lingue sono traduzioni incorporate (:/i18n/mentis_<codice>.qm). Nessuna dipendenza da
// widget. La lingua scelta vale dal prossimo avvio (decisione dell'utente 2026-10-18).

#include <QLocale>
#include <QString>
#include <QStringList>

namespace Localization {

// Lingua scelta nelle Impostazioni (QSettings "ui/language"): vuota = lingua del sistema.
// Un codice non disponibile si legge come vuoto.
QString preference();
void setPreference(const QString &code);

// Codes of the languages offered, in the order of the select: English (the source) and the
// translations at least 80% finished, listed at build time from translations/mentis_*.ts.
QStringList availableLanguages();
// The select's order for the given translation codes ("it", "pt_BR"...): English first, then the
// others by native name. Duplicates and "en" in the input are ignored.
QStringList orderedLanguages(const QStringList &translations);
// Language name written in the language itself ("Italiano", "English", "Português (Brasil)").
// An unknown code is returned as it is.
QString nativeName(const QString &code);

// Lingua da usare: la preferenza, oppure la prima lingua del sistema disponibile, oppure "en".
// A system language matches a code with the same language, script and territory first
// ("pt-BR" -> pt_BR), then a shorter one ("pt-BR" -> pt), then another territory with the same
// script ("pt-PT" -> pt_BR).
QString effectiveLanguage(const QString &preference, const QStringList &systemLanguages = QLocale().uiLanguages(),
                          const QStringList &available = availableLanguages());

// Carica le traduzioni dell'app e di Qt (bottoni dei dialoghi standard) per la lingua effettiva.
// Da chiamare una volta, dopo aver creato QApplication; restituisce la lingua usata.
QString installTranslators();

}  // namespace Localization
