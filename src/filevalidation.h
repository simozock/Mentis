#pragma once

#include <QString>

// FEAT-039: controllo dei file prima di inserirli in un nodo (immagini, file allegati di FEAT-049,
// l'incolla e il trascinamento). Si guarda il contenuto reale, non solo
// l'estensione. Un file troppo grande non si rifiuta: chi chiama chiede conferma (decisione
// dell'utente 2026-10-19).
namespace FileValidation {

// Oltre questi valori il file "rallenta il documento" e si chiede conferma (un solo punto, cambiabile).
inline constexpr qint64 kLargeFileBytes = 50LL * 1024 * 1024;  // 50 MB
inline constexpr qint64 kLargeImagePixels = 100'000'000;       // 100 milioni di pixel

struct Check {
    bool ok = false;       // il file si puo' inserire
    QString reason;        // perche' no (gia' tradotto), vuoto se ok
    bool large = false;    // ok ma oltre i limiti: chiedere conferma
    QString largeReason;   // descrizione della dimensione (gia' tradotta), se large
};

// Immagine: Qt deve riconoscerne il formato dal contenuto, leggerne le misure e decodificarla.
Check image(const QString &path);
// File qualunque (FEAT-049): esiste, si legge, non e' vuoto; solo la dimensione puo' essere grande.
Check anyFile(const QString &path);

// Filtro della finestra di scelta: le immagini che Qt sa leggere su questo computer (senza il
// plugin WebP, per esempio, i .webp non compaiono).
QString imageFilter();

}  // namespace FileValidation
