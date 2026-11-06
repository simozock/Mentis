#pragma once

// FEAT-019 (docs/features/FEAT-019-fondamenta-stile-nodo.md): proprieta' di stile del nodo e
// cascata dei valori (docs/architecture/node-style-contract.md). Nessuna dipendenza da widget:
// si usa dal modello, dal pannello e dai test.

#include <QColor>
#include <QList>
#include <QPointF>
#include <QStringList>
#include <QVariant>

#include "numbering.h"

#include <optional>

struct MindNode;

enum class StyleProperty {
    FillColor, TextColor, BorderColor, BranchColor, BranchWidth, BranchStyle,
    Preset, FillStyle, BorderStyle, BorderWidth,  // FEAT-020 parte A
    Shape,                                        // FEAT-020 parte B
    FontFamily, FontSize, FontWeight, Italic, Underline, StrikeOut, TextCase, TextAlign,  // FEAT-022
    BranchShape,  // FEAT-072: shape of the lines towards the children
    BranchStart, BranchEnd,  // FEAT-023: marks at the parent's and the child's end of those lines
    Numbering  // FEAT-024: BranchNumbering of the topic's branches (no cascade: own value or none)
};

// FEAT-020: stile predefinito del nodo (decisione 2026-10-16). Predefinito = aspetto del
// livello (FEAT-031); gli altri tre valgono a ogni livello. I valori interi sono usati solo in
// memoria (QVariant); nel file si salvano come testo (presetKey e simili).
enum class NodePreset { Predefinito, MoltoImportante, Importante, Depenna };
// FEAT-020: tipo di riempimento e di bordo (parte A); motivi e tratti a mano libera (parte C) in
// coda, cosi' i valori della parte A non cambiano. Nel file si salvano i nomi.
enum class NodeFillStyle {
    Pieno, Nessuno, PienoManoLibera, PennellateDiagonali, TratteggioDiagonale, PennellateOrizzontali, TratteggioOrizzontale
};

// FEAT-040: aspetto dell'immagine del nodo (pagina Stile dell'immagine). I valori predefiniti non
// si scrivono nel file: un file senza queste chiavi apre l'immagine come prima.
struct ImageStyle {
    enum class Border { None, Solid, Dashed, Dotted };
    static constexpr qreal kDefaultBorderWidthPx = 2;
    static constexpr int kMinOpacity = 0;
    static constexpr int kMaxOpacity = 100;
    Border border = Border::None;
    qreal borderWidth = kDefaultBorderWidthPx;  // NodeStyle::kMinBorderWidthPx..kMaxBorderWidthPx
    QColor borderColor{0x20, 0x26, 0x30};       // testo principale della mappa (#202630)
    bool shadow = false;
    int opacity = kMaxOpacity;                  // percento, 0 = invisibile ma ancora cliccabile
    // FEAT-051: contorno bianco e scuro dell'adesivo (Stickers::outlined), acceso per i nuovi
    // adesivi; vale solo con MindNode::imageSticker.
    bool outline = false;
    // Rotazione in gradi in senso orario (0, 90, 180, 270; richiesta dell'utente 2026-10-20). Con 90 e
    // 270 la misura del nodo (imageSize) e' gia' quella ruotata: Document::rotateImage la scambia.
    int rotation = 0;
    bool operator==(const ImageStyle &) const = default;
};

enum class NodeBorderStyle { Continuo, Tratteggiato, Nessuno, ContinuoManoLibera, TratteggiatoManoLibera };
// FEAT-020 parte B: forme del gruppo Base, nell'ordine del menu (la prima e' quella attuale).
enum class NodeShape {
    RettangoloArrotondato, Rettangolo, LineaSotto, DoppiaLineaSotto, Cerchio, Ellisse,
    Rombo, Parallelogramma, Pillola, Esagono, EsagonoVerticale, Ottagono,
    ParentesiQuadre,  // FEAT-026 phase E: the fishbone's central topic (variant 1)
    // FEAT-030 stage A: the Advanced group, in the menu's order.
    Cuore, Nuvola, Stella, Goccia, Gemma, Scudo, FrecciaSinistra, FrecciaDestra, Etichetta, Bandiera, Nota,
    PilaDiSchede, SchedaConBordoLaterale,
    // FEAT-030 stage B: the Puntuale group (with ParentesiQuadre above), in the menu's order.
    ParentesiAngolari, DoppieParentesiAngolari, ParentesiTonde, ParentesiGraffe, Angoli, Virgolette,
    // FEAT-030 stage C: the flowchart group, in the menu's order.
    Decisione, InputManuale, ProcessoPredefinito, OperazioneManuale, Database, Documento, DocumentiMultipli, Ritardo,
    Dati, Connettore, ConnettoreFuoriPagina, CerchioDoppio, Visualizzazione, Trapezio, TrapezioRovesciato
};
// FEAT-022: maiuscole/minuscole (solo visualizzazione nella mappa) e allineamento del titolo.
enum class TextCase { Nessuna, Maiuscolo, Minuscolo };
enum class TextAlign { Sinistra, Centro, Destra, Giustificato };
// FEAT-072 ("Branch" select, measured live 2026-10-22): shape of the lines from a topic to its
// own children (not inherited). Auto = the map's default (curve from the central topic, rounded bracket below, or the
// variant's line). Brackets: 12 px stub, vertical bar, 16 px into the child; fans: from a point 12 px
// out, children 54 px from the parent instead of 28.
enum class BranchShape { Auto, RoundedBracket, Bracket, Straight, Curve, SCurve, Angled, RoundedAngled };
// FEAT-023 (user's approval 2026-10-24): mark at either end of a branch line.
enum class BranchEnd { None, OpenArrow, FilledArrow, Dot, Square, Diamond };

// Valori scelti a mano. Un campo vuoto segue la cascata: palette del foglio, poi predefinito
// dell'app. Solo questi si salvano nel file e nello storico.
struct NodeStyleOverrides {
    std::optional<QColor> fill;
    std::optional<QColor> text;
    std::optional<QColor> border;
    std::optional<QColor> branchColor;  // BUG-005: colore del ramo, separato dal bordo
    std::optional<qreal> branchWidth;
    std::optional<Qt::PenStyle> branchStyle;
    std::optional<NodePreset> preset;            // FEAT-020: vale solo per il nodo
    std::optional<NodeFillStyle> fillStyle;
    std::optional<NodeBorderStyle> borderStyle;
    std::optional<qreal> borderWidth;
    std::optional<NodeShape> shape;
    // FEAT-022: testo del nodo. Senza font scelto vale quello di sistema (scegliere un font vuoto
    // toglie la scelta, come Predefinito per lo stile).
    std::optional<QString> fontFamily;
    std::optional<qreal> fontSize;     // punti, interi kMinFontSizePt-kMaxFontSizePt
    std::optional<int> fontWeight;     // 100-900 a passi di 100
    std::optional<bool> italic;
    std::optional<bool> underline;
    std::optional<bool> strikeOut;     // senza scelta: barrato dello stile Depenna
    std::optional<TextCase> textCase;
    std::optional<TextAlign> textAlign;
    std::optional<BranchShape> branchShape;  // FEAT-072
    std::optional<BranchEnd> branchStart, branchEnd;  // FEAT-023
    std::optional<BranchNumbering> numbering;         // FEAT-024
    bool operator==(const NodeStyleOverrides &) const = default;
};

namespace NodeStyle {

// FEAT-031: palette di base (colori misurati sulle immagini di riferimento,
// docs/features/FEAT-031-palette-colori.md). Ognuna ha 6 colori per i rami principali.
enum class PaletteGroup { Colorato, Classico, Personalizzata };  // Personalizzata: FEAT-032
struct Palette {
    QString id;      // salvato nel file del foglio (paletteId)
    QString name;    // base: English source text (FEAT-046), shown through displayName()
    PaletteGroup group;
    QStringList colors;
};
const QList<Palette> &basePalettes();
// Name to show: a base palette's in the interface language, a custom one's as the user wrote it.
QString displayName(const Palette &palette);
const Palette *findPalette(const QString &id);
// Palette di base con gli stessi colori, se c'e' (file salvati prima che si salvasse l'id).
const Palette *findPaletteByColors(const QStringList &colors);
inline const QString kDefaultPaletteId = QStringLiteral("alba");

// Luminanza relativa WCAG (https://www.w3.org/TR/WCAG21/#dfn-relative-luminance).
qreal relativeLuminance(const QColor &color);
// Colori "quasi bianchi" che non si assegnano ai rami principali (luminanza >= 0,85, FEAT-031).
bool isNearWhite(const QColor &color);
// Colore del ramo "scurito" per il testo dal secondo livello in giu': stessa tinta e saturazione,
// luminosita' HSL al 20% (FEAT-031).
QColor darkenedText(const QColor &branchColor);

// Intervallo dello spessore del ramo, px (come il controllo del pannello di FEAT-017).
inline constexpr qreal kMinBranchWidthPx = 1;
inline constexpr qreal kMaxBranchWidthPx = 12;
// FEAT-023 (richiesta dell'utente 2026-10-21, foto di riferimento): linee a mano libera del ramo, salvate
// come stile della penna fuori dagli stili di Qt (0-6) ma dentro il loro intervallo (0-15).
inline constexpr Qt::PenStyle kFreehandSolidLine = static_cast<Qt::PenStyle>(8);
inline constexpr Qt::PenStyle kFreehandDashLine = static_cast<Qt::PenStyle>(9);
// Spessori con i nomi di riferimento, misurati sulle foto dell'utente (2026-10-21): Extra sottile, Sottile,
// Medio, Grassetto, Extra spesso. Affusolato: spessore alla partenza del ramo (si assottiglia a 1 px).
inline const QList<qreal> kNamedBranchWidths{1, 2, 3, 5, 8};
inline const QList<qreal> kTaperedStartWidths{3, 5, 8, 11, 14};
qreal taperedStartWidth(qreal width);  // dalla tabella, altrimenti il doppio piu' 1
// Spessore del bordo del nodo, px (FEAT-020).
inline constexpr qreal kMinBorderWidthPx = 1;
inline constexpr qreal kMaxBorderWidthPx = 6;
inline constexpr qreal kDefaultBorderWidthPx = 2;
// FEAT-030 stage B: the border a Puntuale shape gets when it had none ("Thin" in the reference, 2 px).
inline constexpr qreal kPunctualBorderWidthPx = 2;
// Dimensione del testo, punti (FEAT-022): valori ammessi e predefiniti (nodo centrale e altri).
inline constexpr int kMinFontSizePt = 8;
inline constexpr int kMaxFontSizePt = 72;
// Sizes offered in px (FEAT-072 Q5); the list is ours and includes the reference's
// defaults 14, 18 and 30. The model keeps points: pt = px * 3 / 4.
inline const QList<int> kFontSizesPx{10, 12, 14, 16, 18, 20, 24, 30, 36, 48, 60, 72};
// Testo predefinito (richiesta dell'utente 2026-10-21; misure sulle foto a zoom 100%: centrale
// 30 px, rami principali 18 px Medium come dice il pannello di riferimento, sotto-argomenti 14 px). In punti
// (1 pt = 4/3 px). Prima 13 pt grassetto per il centrale e 11 pt normale per tutti gli altri.
inline constexpr qreal kRootFontSizePt = 22.5;
inline constexpr qreal kLogicRootFontSizePt = 21;  // FEAT-072: central topic of a logic chart, 28 px
inline constexpr int kLogicRootFontWeight = 600;   // and DemiBold (logic template)
inline constexpr qreal kMainFontSizePt = 13.5;    // rami principali e argomenti mobili
inline constexpr qreal kDefaultFontSizePt = 10.5;
inline constexpr int kRootFontWeight = 800;  // ExtraBold, as the reference panel says (FEAT-072 A4; was 700)
inline constexpr int kMainFontWeight = 500;
inline constexpr int kDefaultFontWeight = 400;
// Margini interni del testo per ruolo, px (misure dal vivo a zoom 100%, 2026-10-21; prima
// 16x8 per tutti): centrale 32x14, rami principali e argomenti mobili 20x11 (alti 42), gli altri 6x7
// (alti 28; 6x6 gave 26).
inline constexpr QPointF kRootTextPadding{32, 14};
// FEAT-026 phase E: brace map central topic is 65 px high with its 24 px text (live 2026-10-24).
inline constexpr QPointF kBraceRootTextPadding{32, 18};
inline constexpr QPointF kMainTextPadding{20, 11};
inline constexpr QPointF kSubTextPadding{6, 7};
QPointF textPadding(const MindNode *node);
// Default map font (empty font = this one): Atkinson Hyperlegible, bundled with the app
// (assets/fonts/atkinson-hyperlegible, SIL OFL 1.1; user's choice 2026-10-26, before Atkinson Hyperlegible
// Next). It has Regular and Bold only: weight 800 draws Bold, 500 draws Regular.
inline const QString kDefaultMapFontFamily = QStringLiteral("Atkinson Hyperlegible");

// Nomi salvati nel file per stile predefinito, riempimento e bordo; testo sconosciuto = nullopt.
QString presetKey(NodePreset preset);
std::optional<NodePreset> presetFromKey(const QString &key);
QString fillStyleKey(NodeFillStyle style);
std::optional<NodeFillStyle> fillStyleFromKey(const QString &key);
QString borderStyleKey(NodeBorderStyle style);
std::optional<NodeBorderStyle> borderStyleFromKey(const QString &key);
QString shapeKey(NodeShape shape);
std::optional<NodeShape> shapeFromKey(const QString &key);
QString textCaseKey(TextCase value);
std::optional<TextCase> textCaseFromKey(const QString &key);
QString textAlignKey(TextAlign value);
std::optional<TextAlign> textAlignFromKey(const QString &key);
QString branchShapeKey(BranchShape value);
std::optional<BranchShape> branchShapeFromKey(const QString &key);
QString branchEndKey(BranchEnd value);
std::optional<BranchEnd> branchEndFromKey(const QString &key);
bool isFanShape(BranchShape shape);  // straight, curve, S-curve and the two angled shapes
// Riempimento fatto di linee (pennellate e tratteggi): fra le linee si vede lo sfondo della mappa
// (FEAT-020 parte C).
bool isPatternFill(NodeFillStyle style);
// Tratto della penna per un tipo di bordo (Nessuno = Qt::NoPen).
Qt::PenStyle borderPenStyle(NodeBorderStyle style);
// Colori di "Molto importante" e "Importante" del tema (FEAT-020, regola approvata il 2026-10-16):
// tinta dell'ultimo colore della palette con saturazione almeno 15% (non quasi bianco) ruotata di
// 60 gradi e poi di altri 45; saturazione di quel colore fra 25% e 65%; luminosita' 60%, 47% se il
// colore e' spento (saturazione sotto 30%). Palette senza colori adatti: tinta 200, saturazione 50%.
struct ImportantColors { QColor veryImportant, important; };
ImportantColors importantColors(const QStringList &palette);
// Sfondo della mappa: bianco (FEAT-036 lo rendera' variabile). Riempimento dello stile Depenna.
inline const QColor kMapBackground{"#ffffff"};

// Contrasto percettivo APCA (versione 0.0.98G-4g, https://github.com/Myndex/apca-w3) del testo
// sullo sfondo, in valore assoluto (0 = illeggibile, circa 106 = massimo). I colori
// semitrasparenti si compongono prima sul bianco della mappa.
qreal apcaContrast(const QColor &text, const QColor &background);
// Colore del testo leggibile su un riempimento (BUG-009, rivisto il 2026-10-21): nero se
// il nero raggiunge Lc 45 APCA, altrimenti bianco. Prima #172033 o bianco, quello con piu' contrasto.
inline constexpr QRgb kReadableDarkText = 0xff000000;
inline constexpr qreal kReadableDarkMinContrast = 45;
QColor readableTextOn(const QColor &fill);

// Vero se il valore e' accettabile per la proprieta' (colore valido, spessore nell'intervallo,
// stile di linea fra quelli offerti).
bool isValidValue(StyleProperty property, const QVariant &value);
// FEAT-041 stage B: the next size of kFontSizesPx above (larger) or below the size in points, in
// points; nullopt past the ends of the list or of the allowed sizes.
std::optional<qreal> steppedFontSizePt(qreal sizePt, bool larger);

// Valore effettivo (gia' risolto dalla cascata) e valore scelto a mano, se c'e'.
QVariant effectiveValue(const MindNode *node, StyleProperty property);
bool hasManualValue(const MindNode *node, StyleProperty property);

// Imposta o toglie il valore scelto a mano di un nodo; vero se qualcosa e' cambiato.
// Il valore deve essere gia' validato con isValidValue().
bool setManualValue(MindNode *node, StyleProperty property, const QVariant &value);
bool setManualValue(NodeStyleOverrides &style, StyleProperty property, const QVariant &value);
// FEAT-085: the whole look of the topic, every property set to its effective value (a value the
// panel could not choose by hand, such as a branch width out of range, stays unset).
NodeStyleOverrides effectiveStyle(const MindNode *node);
bool clearManualValue(MindNode *node, StyleProperty property);

// Lettura aggregata per il pannello: valore comune o misto ("—"), e se il valore e' scelto a
// mano su tutti i nodi (testo chiaro) oppure no (testo grigio).
struct Reading {
    bool empty = true;      // nessun nodo
    bool mixed = false;     // valori effettivi diversi
    QVariant value;         // valore comune, valido solo se !empty && !mixed
    bool allManual = false; // ogni nodo ha un valore scelto a mano
};
Reading read(const QList<const MindNode *> &nodes, StyleProperty property);

// Ricalcola i valori effettivi di tutto l'albero a partire da root (regole di BUG-005, BUG-006 e
// BUG-007, decise dall'utente il 2026-10-16, e di FEAT-031):
// - colore del ramo: scelto a mano, altrimenti quello del genitore (a cascata), altrimenti, per i
//   rami principali, il colore della palette secondo la posizione (saltando i colori quasi
//   bianchi); i nodi liberi proseguono la sequenza dopo l'ultimo ramo principale;
// - stile predefinito (FEAT-020): scelto a mano solo per quel nodo, altrimenti Predefinito. Con il
//   colore del ramo c: Predefinito = aspetto del livello (FEAT-031: principali e liberi pieni di c,
//   testo leggibile; secondo livello c al 20%, testo c scurito; centrale e dal terzo livello solo
//   testo, scurito, #172033 per il centrale); Molto importante e Importante = pieni del colore del
//   tema (importantColors), testo leggibile; Depenna = sfondo della mappa, bordo c, testo scurito e
//   barrato. Nessun bordo salvo Depenna;
// - forma (FEAT-020 parte B): scelta a mano solo per quel nodo, altrimenti rettangolo arrotondato.
//   Con "Linea sotto" e "Doppia linea sotto" il riempimento non si vede, la linea (il bordo) e'
//   visibile salvo "Nessun bordo" scelto a mano, e il testo non scelto a mano e' il colore del
//   ramo scurito (su riempimento invisibile il testo chiaro non si leggerebbe);
// - riempimento, bordo e testo scelti a mano valgono solo per quel nodo e vincono sullo stile. Un
//   colore di riempimento (o di bordo) scelto a mano rende visibile il riempimento (o il bordo),
//   salvo un tipo "Nessuno" scelto a mano; un tipo scelto a mano vince sullo stile. Riempimento e
//   bordo non visibili hanno colore effettivo trasparente; spessore del bordo scelto a mano o 2 px;
// - stile della linea (vale per le linee verso i figli): scelto a mano, altrimenti quello del
//   genitore, a cascata; altrimenti continua;
// - spessore della linea (vale solo per le linee verso i figli diretti): scelto a mano, altrimenti
//   2 px; non si eredita.
// La linea fra un genitore e un figlio usa il colore del ramo del figlio e stile e spessore del
// genitore. Restituisce i nodi i cui valori effettivi sono cambiati.
// FEAT-036: sfondo del foglio e colori dei rami della variante scelta. Su sfondo bianco il testo
// segue le regole di FEAT-031 senza cambiamenti.
inline constexpr int kVariantCount = 6;
inline constexpr qreal kMinBranchContrast = 30;  // APCA minimo fra colore del ramo e sfondo
inline constexpr qreal kVividChroma = 0.6;       // croma (max-min dei canali) di un colore acceso
inline constexpr qreal kMinMonoContrast = 35;    // APCA minimo del colore unico su sfondo chiaro (Kimono: blu su rosa)
struct Backdrop {
    QColor color = kMapBackground;
    int variant = -1;           // 0-5, -1 = nessuna
    QList<QColor> branches;     // colori dei rami decisi dalla variante; vuoto = regola generale
    bool isDefault() const { return color == kMapBackground && branches.isEmpty(); }
    bool operator==(const Backdrop &) const = default;
};
// Palette "tonale" (ha un colore quasi bianco, luminanza >= 0,85, o quasi nero, <= 0,035) o
// "colorata" (decisione dell'utente 2026-10-19, schema ricavato dalle foto di riferimento).
bool isTonalPalette(const QStringList &palette);
// Aspetto della variante (0-5) di una palette; senza variante lo sfondo libero o il bianco:
// - palette tonale: sfondo ci; su sfondo scuro rami multicolori (regola del contrasto), su sfondo
//   chiaro un colore solo, il piu' saturo con APCA almeno kMinMonoContrast (quasi bianco: multicolori); un colore acceso
//   (croma >= kVividChroma) non fa da sfondo: sfondo bianco e rami di quel colore;
// - palette colorata: sfondo bianco; variante 1 multicolore come FEAT-031, varianti 2-6 tutti i
//   rami del colore c1-c5;
// - eccezioni dalle foto di riferimento: Magnifico (sfondo ci, rami bianchi o scuri secondo il contrasto)
//   e Corallo (sfondo quasi nero tinto di ci, rami del colore ci).
// Uno sfondo libero sostituisce solo lo sfondo: i rami restano quelli della variante (o della
// palette senza variante).
Backdrop backdropFor(const QString &paletteId, const QStringList &palette, int variant, const std::optional<QColor> &background);
// Sfondo scuro: il testo leggibile sopra e' il bianco (regola di BUG-009).
bool isDarkBackground(const QColor &background);
// Colori dei rami principali in ordine. Sfondo predefinito: la palette senza i colori quasi bianchi
// (FEAT-031). Altrimenti i colori tranne quello della variante, a partire dal successivo, con
// contrasto APCA almeno kMinBranchContrast con lo sfondo; se nessuno basta, il solo colore piu'
// contrastante (bianco o #172033 se anche quello non basta).
QList<QColor> branchColors(const QStringList &palette, const Backdrop &backdrop);
// Colore di relazioni, sommari e confini senza colore scelto: quello indicato sullo sfondo
// predefinito, altrimenti chiaro o scuro secondo il contrasto (decisione dell'utente 2026-10-18).
QColor annotationColor(const QColor &onDefault, const Backdrop &backdrop);

// FEAT-043 parte stile: valori del foglio dalla pagina Mappa. Font e spessore sono la base dei nodi
// senza un valore scelto a mano; con coloredBranches spento tutte le linee (tranne quelle dei rami
// con un colore scelto a mano) hanno lineColor; non valido (predefinito) = il colore del testo del
// nodo centrale (foto dell'utente 2026-10-21; prima grigio #a0a7b1).
inline const QColor kDefaultBranchLineColor{};
inline const QList<qreal> kSheetBranchWidths = kNamedBranchWidths;
struct SheetStyle {
    QString fontFamily;      // vuoto = carattere del sistema
    qreal branchWidth = 0;   // 0 = predefinito (kDefault, 2 px)
    bool coloredBranches = true;
    QColor lineColor = kDefaultBranchLineColor;
    bool showNotes = false;  // FEAT-059: "Mostra tutte le note" (riquadro sotto i nodi con nota)
    bool tapered = false;    // FEAT-023: rami affusolati (Ampiezza linea ramo "... - Affusolato")
    // FEAT-043 parte layout (richiesta dell'utente 2026-10-21): distanze un po' ridotte, e rami che si
    // spostano a mano anche con il bilanciamento (niente disposizione ne' bilanciamento automatici).
    bool compact = false;
    bool freeBranches = false;
    // Justify: nodes of the same level are as wide as the widest (FEAT-043, user 2026-10-24).
    bool justify = false;
    // FEAT-026 fase D: variante della galleria (MapVariants), vuota = l'aspetto di sempre.
    QString variant;
    bool operator==(const SheetStyle &) const = default;
};

// summaryTopics (FEAT-052): resolved after the tree, under their parents (MindNode::summaryTopic).
QList<MindNode *> resolveTree(MindNode *root, const QStringList &palette, const Backdrop &backdrop = Backdrop(),
                              const SheetStyle &sheet = SheetStyle(), const QList<MindNode *> &summaryTopics = {});

}  // namespace NodeStyle
