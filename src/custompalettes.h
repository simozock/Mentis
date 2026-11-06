#pragma once

#include "nodestyle.h"

#include <optional>

// FEAT-032: palette create dall'utente con 6 colori. Stanno nelle impostazioni dell'app
// ("palettes/custom"), quindi valgono per tutti i file di questo computer; il foglio salva comunque
// id, nome e colori della palette che usa (MindMapModel), cosi' si apre uguale altrove.
namespace CustomPalettes {

inline constexpr int kColorCount = 6;

// Palette personalizzate in ordine di creazione (gruppo NodeStyle::PaletteGroup::Personalizzata).
QList<NodeStyle::Palette> all();
std::optional<NodeStyle::Palette> find(const QString &id);
// Palette di base o personalizzata con questo id.
std::optional<NodeStyle::Palette> lookup(const QString &id);
// Nome gia' usato da una palette di base o personalizzata: il confronto ignora maiuscole e spazi
// iniziali e finali (decisione dell'utente 2026-10-17).
bool isNameTaken(const QString &name);
// Nome per un campo lasciato vuoto: "Personalizzata 1", "Personalizzata 2"... (primo numero libero).
QString nextDefaultName();
// Aggiunge la palette (nome vuoto = nextDefaultName(); id vuoto = nuovo) e ne restituisce l'id.
// Vuoto se i colori non sono kColorCount colori validi o l'id c'e' gia'.
QString add(const QString &name, const QStringList &colors, const QString &id = QString());
// Toglie la palette dall'elenco; i fogli che la usano conservano i loro colori.
bool remove(const QString &id);
// FEAT-045: an own palette's name and/or colours (nullopt = unchanged); false for an unknown id, a
// name already taken by another, or colours that are not six valid ones.
bool update(const QString &id, const std::optional<QString> &name, const std::optional<QStringList> &colors);

// FEAT-036: tema delle mappe nuove (Impostazioni > Editor): palette e variante (-1 = sfondo
// bianco). Predefinito Alba con sfondo bianco; una palette che non c'e' piu' torna ad Alba.
struct DefaultTheme {
    NodeStyle::Palette palette;
    int variant = -1;
};
DefaultTheme defaultTheme();
void setDefaultTheme(const QString &paletteId, int variant);

}  // namespace CustomPalettes
