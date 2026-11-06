#pragma once

// FEAT-026 fase D (docs/features/FEAT-026-strutture-mappa.md): varianti della galleria. Una variante
// e' un insieme di predefiniti del foglio (SheetStyle::variant): aspetto dei nodi per ruolo (centrale,
// rami principali, gli altri), forma delle linee e rami affusolati o a mano libera. I valori scelti a
// mano sul nodo vincono sempre. Ricavate dalle foto della galleria di riferimento dell'utente (2026-10-21,
// attachments/FEAT-026/galleria/); le PRO dalle miniature grigie.

#include "mapstructure.h"
#include "nodestyle.h"

#include <QString>

class MindMapModel;

namespace MapVariants {

// Aspetto di un ruolo: Default = quello di sempre (FEAT-031); Filled = pieno nel colore del ramo (il
// centrale scuro); Light = pieno al 20%; Outline = solo il bordo; Underline = linea sotto; Plain = solo
// il testo; Dark = pieno scuro con il testo chiaro.
enum class Look { Default, Filled, Light, Outline, Underline, Plain, Dark };
struct Role {
    Look look = Look::Default;
    std::optional<NodeShape> shape;              // vuoto = la forma predefinita
    std::optional<NodeFillStyle> fillStyle;      // per esempio pennellate a mano libera
    std::optional<NodeBorderStyle> borderStyle;
    std::optional<qreal> borderWidth;            // anche lo spessore della linea sotto
};
// Linee della mappa mentale e del grafico logico (organigramma e albero hanno le loro): Curve = curve
// (BranchPath::fromRoot e between), Angled = tratto obliquo poi orizzontale, Square = ad angolo retto,
// Straight = dritte.
enum class Line { Curve, Angled, Square, Straight };

struct Variant {
    QString id;                    // salvato nel file del foglio (sheetVariant)
    MapStructure::Type type;
    Role root, main, sub;
    Line line = Line::Curve;
    // Copiati nello stile del foglio quando si sceglie la variante (si cambiano poi dalla pagina Mappa).
    bool tapered = false;
    qreal branchWidth = 0;         // 0 = predefinito
    qreal rootBranchWidth = 0;     // lines from the central topic only (brace map: 3 px); 0 = like the others
    // Text of the central and main topics (reference panel, brace map 2026-10-24); 0 = the app's defaults.
    qreal rootFontSizePt = 0;
    int rootFontWeight = 0;
    int mainFontWeight = 0;
    qreal mainFontSizePt = 0;      // timeline 2: 16 px (reference panel 2026-10-24)
    int subFontWeight = 0;         // timeline: Medium below the main topics too
    std::optional<MapStructure::Direction> direction;  // empty = the type's default (timeline 3: off-axis)
    bool freehandLines = false;    // linee a mano libera (predefinito dello stile del ramo)
    QString fontFamily;            // FEAT-072: global font of the sheet (empty = NodeStyle::kDefaultMapFontFamily)
};

const QList<Variant> &all();
const Variant *find(const QString &id);          // vuoto o sconosciuto = nullptr (aspetto di sempre)
QList<const Variant *> ofType(MapStructure::Type type);
// Tipi della galleria, nell'ordine in cui compaiono (la parentesi graffa dalla fase E; le altre strutture speciali seguiranno).
QList<MapStructure::Type> galleryTypes();
// Il foglio prende tipo (con la direzione predefinita) e variante: stile del foglio (variante, affusolato,
// spessore) e, con withTopics su un foglio con il solo centrale, i quattro rami principali.
void apply(MindMapModel &model, const QString &id, bool withTopics);

}  // namespace MapVariants
