#pragma once

#include <QString>

// FEAT-027: regole dei collegamenti dei nodi, senza widget (testabili da sole).
namespace NodeLinks {

// Indirizzo web completo da quello scritto dall'utente: spazi ai lati tolti, "https://" aggiunto se
// manca lo schema. Ammessi solo http, https e mailto (decisione 2026-10-15); vuoto se l'indirizzo
// non e' valido o usa un altro schema (es. ftp://, javascript:).
QString webAddress(const QString &typed);

}  // namespace NodeLinks
