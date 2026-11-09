#pragma once

#include <QString>

class QUrl;

// FEAT-027: regole dei collegamenti dei nodi, senza widget (testabili da sole).
namespace NodeLinks {

// Indirizzo web completo da quello scritto dall'utente: spazi ai lati tolti, "https://" aggiunto se
// manca lo schema. Ammessi solo http, https e mailto (decisione 2026-10-15); vuoto se l'indirizzo
// non e' valido o usa un altro schema (es. ftp://, javascript:).
QString webAddress(const QString &typed);

// BUG-043: a link may be opened (web link of a topic, link in a note): http or https with a host,
// or mailto. A document from someone else can hold any text, such as file:///.../tool.exe or
// \\server\share (on Windows that sends the user's credentials to the server).
bool canOpen(const QUrl &url);

}  // namespace NodeLinks
