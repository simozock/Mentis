#include "nodestyle.h"
#include "summary.h"

#include <QSet>
#include "mindmapmodel.h"
#include "mapstructure.h"
#include "mapvariants.h"
#include "nodeshape.h"

#include <QCoreApplication>
#include <QHash>

#include <cmath>
#include <functional>

namespace NodeStyle {

namespace {
// Valori effettivi senza nulla di scelto a mano ne' di palette: quelli di MindNode.
struct Effective {
    QColor fill, text, border, branchColor, lineColor;
    qreal branchWidth;
    Qt::PenStyle branchStyle;
    NodePreset preset;
    NodeFillStyle fillStyle;
    NodeBorderStyle borderStyle;
    qreal borderWidth;
    bool strikeOut;
    NodeShape shape;
    QString fontFamily;
    qreal fontSize;
    int fontWeight;
    bool italic, underline;
    TextCase textCase;
    TextAlign textAlign;
    BranchShape branchShape = BranchShape::Auto;
    BranchEnd branchStart = BranchEnd::None, branchEnd = BranchEnd::None;
    bool operator==(const Effective &) const = default;
};

Effective appDefaults() {
    const MindNode defaults;
    return {QColor("#fff4cf"), QColor("#172033"), QColor("#d69727"), QColor("#d69727"), QColor("#d69727"), defaults.branchWidth, defaults.branchStyle,
            defaults.preset, defaults.fillStyle, defaults.borderStyle, defaults.borderWidth, defaults.strikeOut, defaults.shape,
            defaults.fontFamily, defaults.fontSize, defaults.fontWeight, defaults.italic, defaults.underline,
            defaults.textCase, defaults.textAlign, defaults.branchShape, defaults.branchStart, defaults.branchEnd};
}

Effective effectiveOf(const MindNode *node) {
    return {node->fill, node->text, node->border, node->branchColor, node->lineColor, node->branchWidth, node->branchStyle,
            node->preset, node->fillStyle, node->borderStyle, node->borderWidth, node->strikeOut, node->shape,
            node->fontFamily, node->fontSize, node->fontWeight, node->italic, node->underline, node->textCase, node->textAlign, node->branchShape,
            node->branchStart, node->branchEnd};
}

const QStringList &textCaseKeys() {
    static const QStringList keys{QStringLiteral("nessuna"), QStringLiteral("maiuscolo"), QStringLiteral("minuscolo")};
    return keys;
}
const QStringList &branchEndKeys() {
    static const QStringList keys{QStringLiteral("none"), QStringLiteral("openArrow"), QStringLiteral("filledArrow"),
                                  QStringLiteral("dot"), QStringLiteral("square"), QStringLiteral("diamond")};
    return keys;
}
const QStringList &branchShapeKeys() {
    static const QStringList keys{QStringLiteral("auto"), QStringLiteral("roundedBracket"), QStringLiteral("bracket"),
                                  QStringLiteral("straight"), QStringLiteral("curve"), QStringLiteral("sCurve"),
                                  QStringLiteral("angled"), QStringLiteral("roundedAngled")};
    return keys;
}
const QStringList &textAlignKeys() {
    static const QStringList keys{QStringLiteral("sinistra"), QStringLiteral("centro"), QStringLiteral("destra"),
                                  QStringLiteral("giustificato")};
    return keys;
}

bool isBool(const QVariant &value) { return value.metaType().id() == QMetaType::Bool; }

// Nomi salvati nel file, nello stesso ordine degli enum.
const QStringList &presetKeys() {
    static const QStringList keys{QStringLiteral("predefinito"), QStringLiteral("moltoImportante"),
                                  QStringLiteral("importante"), QStringLiteral("depenna")};
    return keys;
}
const QStringList &fillStyleKeys() {
    static const QStringList keys{QStringLiteral("pieno"), QStringLiteral("nessuno"), QStringLiteral("pienoManoLibera"),
                                  QStringLiteral("pennellateDiagonali"), QStringLiteral("tratteggioDiagonale"),
                                  QStringLiteral("pennellateOrizzontali"), QStringLiteral("tratteggioOrizzontale")};
    return keys;
}
const QStringList &borderStyleKeys() {
    static const QStringList keys{QStringLiteral("continuo"), QStringLiteral("tratteggiato"), QStringLiteral("nessuno"),
                                  QStringLiteral("continuoManoLibera"), QStringLiteral("tratteggiatoManoLibera")};
    return keys;
}

const QStringList &shapeKeys() {
    static const QStringList keys{QStringLiteral("rettangoloArrotondato"), QStringLiteral("rettangolo"),
                                  QStringLiteral("lineaSotto"), QStringLiteral("doppiaLineaSotto"), QStringLiteral("cerchio"),
                                  QStringLiteral("ellisse"), QStringLiteral("rombo"), QStringLiteral("parallelogramma"),
                                  QStringLiteral("pillola"), QStringLiteral("esagono"), QStringLiteral("esagonoVerticale"),
                                  QStringLiteral("ottagono"), QStringLiteral("parentesiQuadre"), QStringLiteral("cuore"),
                                  QStringLiteral("nuvola"), QStringLiteral("stella"), QStringLiteral("goccia"), QStringLiteral("gemma"),
                                  QStringLiteral("scudo"), QStringLiteral("frecciaSinistra"), QStringLiteral("frecciaDestra"),
                                  QStringLiteral("etichetta"), QStringLiteral("bandiera"), QStringLiteral("nota"),
                                  QStringLiteral("pilaDiSchede"), QStringLiteral("schedaConBordoLaterale"),
                                  QStringLiteral("parentesiAngolari"), QStringLiteral("doppieParentesiAngolari"),
                                  QStringLiteral("parentesiTonde"), QStringLiteral("parentesiGraffe"), QStringLiteral("angoli"),
                                  QStringLiteral("virgolette"), QStringLiteral("decisione"), QStringLiteral("inputManuale"),
                                  QStringLiteral("processoPredefinito"), QStringLiteral("operazioneManuale"),
                                  QStringLiteral("database"), QStringLiteral("documento"), QStringLiteral("documentiMultipli"),
                                  QStringLiteral("ritardo"), QStringLiteral("dati"), QStringLiteral("connettore"),
                                  QStringLiteral("connettoreFuoriPagina"), QStringLiteral("cerchioDoppio"),
                                  QStringLiteral("visualizzazione"), QStringLiteral("trapezio"), QStringLiteral("trapezioRovesciato")};
    return keys;
}

bool isEnumIndex(const QVariant &value, int count) {
    bool ok = false;
    const int index = value.toInt(&ok);
    return ok && index >= 0 && index < count;
}

bool isOfferedPenStyle(int style) {
    return style == Qt::NoPen || style == Qt::SolidLine || style == Qt::DashLine || style == Qt::DotLine ||
           style == kFreehandSolidLine || style == kFreehandDashLine;
}
}  // namespace

qreal taperedStartWidth(qreal width) {
    const qsizetype index = kNamedBranchWidths.indexOf(width);
    return index >= 0 ? kTaperedStartWidths[index] : 2 * width + 1;
}

const QList<Palette> &basePalettes() {
    static const QList<Palette> palettes{
        {QStringLiteral("alba"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Dawn")), PaletteGroup::Colorato, {"#ff6b6b", "#ff9f69", "#97d3b6", "#88e2d7", "#6fd0f9", "#e18bee"}},
        {QStringLiteral("arcobaleno"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Rainbow")), PaletteGroup::Colorato, {"#f3d321", "#b7c82b", "#d8dcaf", "#76b18a", "#fa8155", "#7b4083"}},
        {QStringLiteral("energia"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Energy")), PaletteGroup::Colorato, {"#ffffff", "#f2f2f2", "#f22816", "#f2b807", "#233ed9", "#0d0d0d"}},
        {QStringLiteral("danza"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Dance")), PaletteGroup::Colorato, {"#4e60ef", "#eb4758", "#ffffff", "#fff8e0", "#aa0e1d", "#363026"}},
        {QStringLiteral("codice"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Code")), PaletteGroup::Colorato, {"#fff0b8", "#cbffb8", "#ffffff", "#db8fff", "#8abeff", "#2c2d30"}},
        {QStringLiteral("kimono"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Kimono")), PaletteGroup::Colorato, {"#ffffff", "#ffabaa", "#ff7b31", "#8cb5ff", "#4a51d9", "#191959"}},
        {QStringLiteral("isole"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Islands")), PaletteGroup::Colorato, {"#ffe8d6", "#ddbea9", "#cb997e", "#b7b7a4", "#a5a58d", "#6b705c"}},
        {QStringLiteral("rose"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Roses")), PaletteGroup::Colorato, {"#fff0f3", "#ffccd5", "#ffb3c1", "#ff758f", "#c9184a", "#a4133c"}},
        {QStringLiteral("menta"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Mint")), PaletteGroup::Colorato, {"#ffffff", "#c4fff9", "#9ceaef", "#68d8d6", "#06afa9", "#046562"}},
        {QStringLiteral("te-verde"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Green tea")), PaletteGroup::Colorato, {"#d6d9c3", "#b6ad90", "#579360", "#656d4a", "#265834", "#1f2b1d"}},
        {QStringLiteral("spazio"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Space")), PaletteGroup::Colorato, {"#d9dcd6", "#81c3d7", "#3a7ca5", "#2f6690", "#16425b", "#0d2f42"}},
        {QStringLiteral("sofisticato"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Sophisticated")), PaletteGroup::Colorato, {"#7d5a2c", "#fdfbf7", "#dfcaa4", "#c49c64", "#d3381d", "#1e1d1a"}},
        {QStringLiteral("innocenza"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Innocence")), PaletteGroup::Colorato, {"#fdc9d1", "#ea618a", "#a4d0f9", "#4f73ba", "#fdf8e7", "#3c4244"}},
        {QStringLiteral("macaron"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Macaron")), PaletteGroup::Colorato, {"#cab08f", "#feb58c", "#afd4c4", "#ecf6f6", "#f9e088", "#3c4244"}},
        {QStringLiteral("bosco"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Forest")), PaletteGroup::Colorato, {"#e1c356", "#5b805c", "#86964f", "#b3c785", "#f9ffeb", "#1f2513"}},
        {QStringLiteral("crema"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Cream")), PaletteGroup::Colorato, {"#d8ead2", "#d4d0de", "#ffffff", "#c9dbec", "#dcc4c0", "#7d6e83"}},
        {QStringLiteral("hawaii"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Hawaii")), PaletteGroup::Colorato, {"#b7d6e8", "#4a94c3", "#254b85", "#4b9383", "#d29f55", "#f3e6cf"}},
        {QStringLiteral("pigna"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Pine cone")), PaletteGroup::Colorato, {"#64625c", "#978477", "#1d414b", "#c8c6cb", "#aa9fa3", "#d1bfaf"}},
        {QStringLiteral("distopia"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Dystopia")), PaletteGroup::Colorato, {"#bd2828", "#f4f5f6", "#dfe4e7", "#a5acb1", "#606466", "#2a2c2c"}},
        {QStringLiteral("freschezza"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Freshness")), PaletteGroup::Colorato, {"#f0f0f0", "#f2bdc7", "#f2dc6b", "#5ba683", "#b796d9", "#3c74a6"}},
        {QStringLiteral("florido"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Blooming")), PaletteGroup::Colorato, {"#edf3ff", "#c1e554", "#ffaa39", "#d389d5", "#1692d2", "#0a052e"}},
        {QStringLiteral("tranquillo"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Tranquil")), PaletteGroup::Colorato, {"#f9f5de", "#dfddce", "#4b9d9d", "#7884a4", "#aa79aa", "#153e5d"}},
        {QStringLiteral("colorato"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Colorful")), PaletteGroup::Colorato, {"#f6f5f5", "#9bffed", "#ffc947", "#e46d57", "#1f3c88", "#070d59"}},
        {QStringLiteral("luccicante"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Sparkling")), PaletteGroup::Colorato, {"#ffffff", "#efd7e6", "#ff7dc1", "#a239ea", "#5c37e5", "#092933"}},
        {QStringLiteral("vintage"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Vintage")), PaletteGroup::Colorato, {"#e9c46a", "#f4a261", "#dc856f", "#a4705e", "#2a9d8f", "#264653"}},
        {QStringLiteral("dessert"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Dessert")), PaletteGroup::Colorato, {"#f9f8ed", "#ffedd2", "#ffbc9f", "#d8ac8f", "#83c5be", "#006d77"}},
        {QStringLiteral("vaniglia"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Vanilla")), PaletteGroup::Colorato, {"#ffffff", "#e4f9f5", "#30e3ca", "#11999e", "#40514e", "#0d4040"}},
        {QStringLiteral("caramella"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Candy")), PaletteGroup::Colorato, {"#ffffff", "#ff9c72", "#f5cd6c", "#f09e3a", "#9cc3e4", "#54a6d6"}},
        {QStringLiteral("cyberpunk"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Cyberpunk")), PaletteGroup::Colorato, {"#ffffff", "#72efdd", "#56cfe1", "#4ea8de", "#5e60ce", "#7400b8"}},
        {QStringLiteral("ciliegio"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Cherry blossom")), PaletteGroup::Colorato, {"#ffe3e8", "#ffdcc8", "#ffb4b6", "#ffa9c6", "#d1c3bd", "#c1cfde"}},
        {QStringLiteral("camino"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Fireplace")), PaletteGroup::Colorato, {"#fdd29a", "#f9a655", "#fc901a", "#e04b51", "#a4564c", "#6d3b37"}},
        {QStringLiteral("vacanze"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Holidays")), PaletteGroup::Colorato, {"#d5f2e3", "#f0a346", "#e12a37", "#bc191e", "#2d6c65", "#101f23"}},
        {QStringLiteral("oceano"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Ocean")), PaletteGroup::Colorato, {"#b4f2fd", "#6ee2fd", "#3bb6e3", "#135cae", "#01206a", "#000d2d"}},
        {QStringLiteral("viola"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Violet")), PaletteGroup::Colorato, {"#fffbef", "#fbd58a", "#dcbef4", "#b67be6", "#9d4edd", "#72369d"}},
        {QStringLiteral("costanza"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Constancy")), PaletteGroup::Classico, {"#3949ab", "#e53935", "#c0ca33", "#00897b", "#1e88e5", "#8e24aa"}},
        {QStringLiteral("crema-classica"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Classic cream")), PaletteGroup::Classico, {"#2196f3", "#3f51b5", "#f44336", "#ff9800", "#cddc39", "#009688"}},
        {QStringLiteral("fiori"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Flowers")), PaletteGroup::Classico, {"#982b3c", "#ae942f", "#307047", "#225373", "#19417c", "#5a2866"}},
        {QStringLiteral("corallo"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Coral")), PaletteGroup::Classico, {"#df7474", "#f4d956", "#bac74c", "#4396b5", "#7574b7", "#c36d8d"}},
        {QStringLiteral("magnifico"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Magnificent")), PaletteGroup::Classico, {"#bf3e4c", "#e8ca43", "#acba3a", "#255aa0", "#514a9b", "#783b86"}},
        {QStringLiteral("champagne"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Champagne")), PaletteGroup::Classico, {"#bdaca6", "#c5bea4", "#a2b5a6", "#a2b3b8", "#a2a2ae", "#baadb0"}},
        {QStringLiteral("profumo"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Perfume")), PaletteGroup::Classico, {"#a79552", "#5c8668", "#38697b", "#545279", "#674b6f", "#835264"}},
        {QStringLiteral("zen"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Zen")), PaletteGroup::Classico, {"#ffffff", "#ededed", "#d3d3d3", "#393939", "#202020", "#080808"}},
        {QStringLiteral("serie"), QStringLiteral(QT_TRANSLATE_NOOP("Palette", "Series")), PaletteGroup::Classico, {"#e15241", "#f19d38", "#67ad5b", "#4994ec", "#4350af", "#d63864"}},
    };
    return palettes;
}

QString displayName(const Palette &palette) {
    if (palette.group == PaletteGroup::Personalizzata) return palette.name;
    return QCoreApplication::translate("Palette", palette.name.toUtf8().constData());
}

const Palette *findPalette(const QString &id) {
    for (const Palette &palette : basePalettes())
        if (palette.id == id) return &palette;
    return nullptr;
}

const Palette *findPaletteByColors(const QStringList &colors) {
    for (const Palette &palette : basePalettes()) {
        if (palette.colors.size() != colors.size()) continue;
        bool same = true;
        for (int i = 0; i < colors.size() && same; ++i) same = QColor(palette.colors[i]) == QColor(colors[i]);
        if (same) return &palette;
    }
    return nullptr;
}

qreal relativeLuminance(const QColor &color) {
    auto channel = [](qreal value) { return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}

bool isNearWhite(const QColor &color) { return relativeLuminance(color) >= 0.85; }

QColor darkenedText(const QColor &branchColor) {
    // I colori neutri hanno tinta -1: diventano grigio scuro.
    return QColor::fromHslF(std::max(0.0f, branchColor.hslHueF()), branchColor.hslSaturationF(), 0.2f);
}

namespace {
// Luminanza APCA ("screen luminance"): curva 2,4 senza tratto lineare, coefficienti sRGB.
qreal apcaLuminance(const QColor &color) {
    // Composizione sul bianco della mappa per i colori semitrasparenti.
    const qreal alpha = color.alphaF();
    auto channel = [alpha](qreal value) { return std::pow(value * alpha + (1.0 - alpha), 2.4); };
    return 0.2126729 * channel(color.redF()) + 0.7151522 * channel(color.greenF()) + 0.0721750 * channel(color.blueF());
}
}  // namespace

qreal apcaContrast(const QColor &text, const QColor &background) {
    // Costanti di APCA 0.0.98G-4g.
    constexpr qreal kBlackThreshold = 0.022, kBlackClamp = 1.414, kScale = 1.14, kOffset = 0.027;
    constexpr qreal kMinDelta = 0.0005, kLowClip = 0.1;
    auto clamp = [](qreal y) { return y > kBlackThreshold ? y : y + std::pow(kBlackThreshold - y, kBlackClamp); };
    const qreal textY = clamp(apcaLuminance(text));
    const qreal backgroundY = clamp(apcaLuminance(background));
    if (std::abs(backgroundY - textY) < kMinDelta) return 0;
    if (backgroundY > textY) {  // testo scuro su sfondo chiaro
        const qreal contrast = (std::pow(backgroundY, 0.56) - std::pow(textY, 0.57)) * kScale;
        return contrast < kLowClip ? 0 : (contrast - kOffset) * 100;
    }
    const qreal contrast = (std::pow(backgroundY, 0.65) - std::pow(textY, 0.62)) * kScale;  // testo chiaro su scuro
    return contrast > -kLowClip ? 0 : -(contrast + kOffset) * 100;
}

QColor readableTextOn(const QColor &fill) {
    // Come nel riferimento (dal vivo 2026-10-21: nero anche sul rosso #ff6b6b, Lc 51, dove il bianco ha piu'
    // contrasto): nero da Lc 45, il livello APCA dei titoli grandi
    // (https://git.apcacontrast.com/documentation/APCA_in_a_Nutshell); soglia scelta per
    // riprodurre il riferimento, non una regola nota. Altrimenti bianco.
    const QColor dark(kReadableDarkText);
    return apcaContrast(dark, fill) >= kReadableDarkMinContrast ? dark : QColor("#ffffff");
}

QString presetKey(NodePreset preset) { return presetKeys().value(static_cast<int>(preset)); }
std::optional<NodePreset> presetFromKey(const QString &key) {
    const int index = presetKeys().indexOf(key);
    return index < 0 ? std::nullopt : std::optional<NodePreset>(static_cast<NodePreset>(index));
}
QString fillStyleKey(NodeFillStyle style) { return fillStyleKeys().value(static_cast<int>(style)); }
std::optional<NodeFillStyle> fillStyleFromKey(const QString &key) {
    const int index = fillStyleKeys().indexOf(key);
    return index < 0 ? std::nullopt : std::optional<NodeFillStyle>(static_cast<NodeFillStyle>(index));
}
QString borderStyleKey(NodeBorderStyle style) { return borderStyleKeys().value(static_cast<int>(style)); }
std::optional<NodeBorderStyle> borderStyleFromKey(const QString &key) {
    const int index = borderStyleKeys().indexOf(key);
    return index < 0 ? std::nullopt : std::optional<NodeBorderStyle>(static_cast<NodeBorderStyle>(index));
}

QString shapeKey(NodeShape shape) { return shapeKeys().value(static_cast<int>(shape)); }
std::optional<NodeShape> shapeFromKey(const QString &key) {
    const int index = shapeKeys().indexOf(key);
    return index < 0 ? std::nullopt : std::optional<NodeShape>(static_cast<NodeShape>(index));
}

QString textCaseKey(TextCase value) { return textCaseKeys().value(static_cast<int>(value)); }
std::optional<TextCase> textCaseFromKey(const QString &key) {
    const int index = textCaseKeys().indexOf(key);
    return index < 0 ? std::nullopt : std::optional<TextCase>(static_cast<TextCase>(index));
}
QString textAlignKey(TextAlign value) { return textAlignKeys().value(static_cast<int>(value)); }
QString branchShapeKey(BranchShape value) { return branchShapeKeys().value(static_cast<int>(value)); }
std::optional<BranchShape> branchShapeFromKey(const QString &key) {
    const int index = branchShapeKeys().indexOf(key);
    return index <= 0 ? std::nullopt : std::optional<BranchShape>(static_cast<BranchShape>(index));  // "auto" is not stored
}
QString branchEndKey(BranchEnd value) { return branchEndKeys().value(static_cast<int>(value)); }
std::optional<BranchEnd> branchEndFromKey(const QString &key) {
    // "none" is stored too: a child can turn off a mark it would inherit.
    const int index = branchEndKeys().indexOf(key);
    return index < 0 ? std::nullopt : std::optional<BranchEnd>(static_cast<BranchEnd>(index));
}
bool isFanShape(BranchShape shape) {
    return shape == BranchShape::Straight || shape == BranchShape::Curve || shape == BranchShape::SCurve
           || shape == BranchShape::Angled || shape == BranchShape::RoundedAngled;
}
std::optional<TextAlign> textAlignFromKey(const QString &key) {
    const int index = textAlignKeys().indexOf(key);
    return index < 0 ? std::nullopt : std::optional<TextAlign>(static_cast<TextAlign>(index));
}

bool isPatternFill(NodeFillStyle style) {
    return style == NodeFillStyle::PennellateDiagonali || style == NodeFillStyle::TratteggioDiagonale
           || style == NodeFillStyle::PennellateOrizzontali || style == NodeFillStyle::TratteggioOrizzontale;
}

Qt::PenStyle borderPenStyle(NodeBorderStyle style) {
    switch (style) {
    case NodeBorderStyle::Continuo:
    case NodeBorderStyle::ContinuoManoLibera: return Qt::SolidLine;
    case NodeBorderStyle::Tratteggiato:
    case NodeBorderStyle::TratteggiatoManoLibera: return Qt::DashLine;
    case NodeBorderStyle::Nessuno: return Qt::NoPen;
    }
    return Qt::SolidLine;
}

ImportantColors importantColors(const QStringList &palette) {
    QColor base;
    for (const QString &name : palette) {
        const QColor color(name);
        if (color.isValid() && !isNearWhite(color) && color.hslSaturationF() >= 0.15f && color.lightnessF() > 0.08f) base = color;
    }
    const float hue = base.isValid() ? std::max(0.0f, base.hslHueF()) * 360.0f : 200.0f;
    const float saturation = base.isValid() ? std::clamp(base.hslSaturationF(), 0.25f, 0.65f) : 0.5f;
    const float lightness = base.isValid() && base.hslSaturationF() < 0.3f ? 0.47f : 0.6f;
    auto at = [saturation, lightness](float degrees) {
        return QColor::fromHslF(std::fmod(degrees, 360.0f) / 360.0f, saturation, lightness);
    };
    return {at(hue + 60), at(hue + 105)};
}

bool isValidValue(StyleProperty property, const QVariant &value) {
    switch (property) {
    case StyleProperty::FillColor:
    case StyleProperty::TextColor:
    case StyleProperty::BorderColor:
    case StyleProperty::BranchColor:
        return value.canConvert<QColor>() && value.value<QColor>().isValid();
    case StyleProperty::BranchWidth: {
        bool ok = false;
        const qreal width = value.toDouble(&ok);
        return ok && width >= kMinBranchWidthPx && width <= kMaxBranchWidthPx;
    }
    case StyleProperty::BranchStyle: {
        bool ok = false;
        const int style = value.toInt(&ok);
        return ok && isOfferedPenStyle(style);
    }
    case StyleProperty::Preset: return isEnumIndex(value, presetKeys().size());
    case StyleProperty::FillStyle: return isEnumIndex(value, fillStyleKeys().size());
    case StyleProperty::BorderStyle: return isEnumIndex(value, borderStyleKeys().size());
    case StyleProperty::Shape: return isEnumIndex(value, shapeKeys().size());
    case StyleProperty::BorderWidth: {
        bool ok = false;
        const qreal width = value.toDouble(&ok);
        return ok && width >= kMinBorderWidthPx && width <= kMaxBorderWidthPx;
    }
    case StyleProperty::FontFamily:  // vuoto = torna al font di sistema (toglie la scelta)
        return value.metaType().id() == QMetaType::QString && value.toString().size() <= 200;
    case StyleProperty::FontSize: {
        bool ok = false;
        const qreal size = value.toDouble(&ok);
        // Quarter points: whole px sizes from the panel (FEAT-072 Q5, 18 px = 13.5 pt) and the whole
        // points of older files.
        return ok && size * 4 == std::floor(size * 4) && size >= kMinFontSizePt && size <= kMaxFontSizePt;
    }
    case StyleProperty::FontWeight: {
        bool ok = false;
        const int weight = value.toInt(&ok);
        return ok && value.toDouble() == weight && weight >= 100 && weight <= 900 && weight % 100 == 0;
    }
    case StyleProperty::Italic:
    case StyleProperty::Underline:
    case StyleProperty::StrikeOut:
        return isBool(value);
    case StyleProperty::TextCase: return isEnumIndex(value, textCaseKeys().size());
    case StyleProperty::TextAlign: return isEnumIndex(value, textAlignKeys().size());
    case StyleProperty::BranchShape: return isEnumIndex(value, branchShapeKeys().size());
    case StyleProperty::BranchStart:
    case StyleProperty::BranchEnd: return isEnumIndex(value, branchEndKeys().size());
    case StyleProperty::Numbering: {  // an empty format (None) removes the numbering
        if (!value.canConvert<BranchNumbering>()) return false;
        const BranchNumbering numbering = value.value<BranchNumbering>();
        return numbering.format.isEmpty() || Numbering::isValid(numbering);
    }
    }
    return false;
}

std::optional<qreal> steppedFontSizePt(qreal sizePt, bool larger) {
    const qreal px = sizePt * 4 / 3;
    std::optional<qreal> next;
    for (const int candidate : kFontSizesPx) {
        const qreal pt = candidate * 3.0 / 4;
        if (!isValidValue(StyleProperty::FontSize, pt)) continue;
        if (larger && candidate > px + 0.01) return pt;
        if (!larger && candidate < px - 0.01) next = pt;  // the last one below
    }
    return next;
}

QVariant effectiveValue(const MindNode *node, StyleProperty property) {
    switch (property) {
    case StyleProperty::FillColor: return node->fill;
    case StyleProperty::TextColor: return node->text;
    case StyleProperty::BorderColor: return node->border;
    case StyleProperty::BranchColor: return node->branchColor;
    case StyleProperty::BranchWidth: return node->branchWidth;
    case StyleProperty::BranchStyle: return static_cast<int>(node->branchStyle);
    case StyleProperty::Preset: return static_cast<int>(node->preset);
    case StyleProperty::FillStyle: return static_cast<int>(node->fillStyle);
    case StyleProperty::BorderStyle: return static_cast<int>(node->borderStyle);
    case StyleProperty::BorderWidth: return node->borderWidth;
    case StyleProperty::Shape: return static_cast<int>(node->shape);
    // Font vuoto = quello di sistema: il pannello lo mostra con il suo nome.
    case StyleProperty::FontFamily: return node->fontFamily;
    case StyleProperty::FontSize: return node->fontSize;
    case StyleProperty::FontWeight: return node->fontWeight;
    case StyleProperty::Italic: return node->italic;
    case StyleProperty::Underline: return node->underline;
    case StyleProperty::StrikeOut: return node->strikeOut;
    case StyleProperty::TextCase: return static_cast<int>(node->textCase);
    case StyleProperty::TextAlign: return static_cast<int>(node->textAlign);
    case StyleProperty::BranchShape: return static_cast<int>(node->branchShape);
    case StyleProperty::BranchStart: return static_cast<int>(node->branchStart);
    case StyleProperty::BranchEnd: return static_cast<int>(node->branchEnd);
    case StyleProperty::Numbering: return QVariant::fromValue(node->style.numbering.value_or(BranchNumbering()));  // empty format = none
    }
    return {};
}

bool hasManualValue(const MindNode *node, StyleProperty property) {
    const NodeStyleOverrides &style = node->style;
    switch (property) {
    case StyleProperty::FillColor: return style.fill.has_value();
    case StyleProperty::TextColor: return style.text.has_value();
    case StyleProperty::BorderColor: return style.border.has_value();
    case StyleProperty::BranchColor: return style.branchColor.has_value();
    case StyleProperty::BranchWidth: return style.branchWidth.has_value();
    case StyleProperty::BranchStyle: return style.branchStyle.has_value();
    case StyleProperty::Preset: return style.preset.has_value();
    case StyleProperty::FillStyle: return style.fillStyle.has_value();
    case StyleProperty::BorderStyle: return style.borderStyle.has_value();
    case StyleProperty::BorderWidth: return style.borderWidth.has_value();
    case StyleProperty::Shape: return style.shape.has_value();
    case StyleProperty::FontFamily: return style.fontFamily.has_value();
    case StyleProperty::FontSize: return style.fontSize.has_value();
    case StyleProperty::FontWeight: return style.fontWeight.has_value();
    case StyleProperty::Italic: return style.italic.has_value();
    case StyleProperty::Underline: return style.underline.has_value();
    case StyleProperty::StrikeOut: return style.strikeOut.has_value();
    case StyleProperty::TextCase: return style.textCase.has_value();
    case StyleProperty::TextAlign: return style.textAlign.has_value();
    case StyleProperty::BranchShape: return style.branchShape.has_value();
    case StyleProperty::BranchStart: return style.branchStart.has_value();
    case StyleProperty::BranchEnd: return style.branchEnd.has_value();
    case StyleProperty::Numbering: return style.numbering.has_value();
    }
    return false;
}

bool setManualValue(MindNode *node, StyleProperty property, const QVariant &value) {
    return setManualValue(node->style, property, value);
}

NodeStyleOverrides effectiveStyle(const MindNode *node) {
    NodeStyleOverrides style;
    for (int index = 0; index <= static_cast<int>(StyleProperty::Numbering); ++index) {
        const auto property = static_cast<StyleProperty>(index);
        const QVariant value = effectiveValue(node, property);
        if (isValidValue(property, value)) setManualValue(style, property, value);
    }
    return style;
}

bool setManualValue(NodeStyleOverrides &style, StyleProperty property, const QVariant &value) {
    const NodeStyleOverrides before = style;
    switch (property) {
    case StyleProperty::FillColor: style.fill = value.value<QColor>(); break;
    case StyleProperty::TextColor: style.text = value.value<QColor>(); break;
    case StyleProperty::BorderColor: style.border = value.value<QColor>(); break;
    case StyleProperty::BranchColor: style.branchColor = value.value<QColor>(); break;
    case StyleProperty::BranchWidth: style.branchWidth = value.toDouble(); break;
    case StyleProperty::BranchStyle: style.branchStyle = static_cast<Qt::PenStyle>(value.toInt()); break;
    case StyleProperty::Preset:
        // Scegliere Predefinito toglie lo stile scelto: il nodo torna all'aspetto del livello.
        if (static_cast<NodePreset>(value.toInt()) == NodePreset::Predefinito) style.preset.reset();
        else style.preset = static_cast<NodePreset>(value.toInt());
        break;
    case StyleProperty::FillStyle: style.fillStyle = static_cast<NodeFillStyle>(value.toInt()); break;
    case StyleProperty::BorderStyle: style.borderStyle = static_cast<NodeBorderStyle>(value.toInt()); break;
    case StyleProperty::BorderWidth: style.borderWidth = value.toDouble(); break;
    case StyleProperty::Shape: style.shape = static_cast<NodeShape>(value.toInt()); break;
    case StyleProperty::FontFamily:
        if (value.toString().trimmed().isEmpty()) style.fontFamily.reset();  // font di sistema
        else style.fontFamily = value.toString().trimmed();
        break;
    case StyleProperty::FontSize: style.fontSize = value.toDouble(); break;
    case StyleProperty::FontWeight: style.fontWeight = value.toInt(); break;
    case StyleProperty::Italic: style.italic = value.toBool(); break;
    case StyleProperty::Underline: style.underline = value.toBool(); break;
    case StyleProperty::StrikeOut: style.strikeOut = value.toBool(); break;
    case StyleProperty::TextCase: style.textCase = static_cast<TextCase>(value.toInt()); break;
    case StyleProperty::TextAlign: style.textAlign = static_cast<TextAlign>(value.toInt()); break;
    case StyleProperty::BranchShape:
        // Auto chosen from the panel = no choice of its own (follows the parent again).
        if (static_cast<BranchShape>(value.toInt()) == BranchShape::Auto) style.branchShape.reset();
        else style.branchShape = static_cast<BranchShape>(value.toInt());
        break;
    case StyleProperty::BranchStart: style.branchStart = static_cast<BranchEnd>(value.toInt()); break;
    case StyleProperty::BranchEnd: style.branchEnd = static_cast<BranchEnd>(value.toInt()); break;
    case StyleProperty::Numbering:
        if (value.value<BranchNumbering>().format.isEmpty()) style.numbering.reset();
        else style.numbering = value.value<BranchNumbering>();
        break;
    }
    return !(style == before);
}

bool clearManualValue(MindNode *node, StyleProperty property) {
    if (!hasManualValue(node, property)) return false;
    NodeStyleOverrides &style = node->style;
    switch (property) {
    case StyleProperty::FillColor: style.fill.reset(); break;
    case StyleProperty::TextColor: style.text.reset(); break;
    case StyleProperty::BorderColor: style.border.reset(); break;
    case StyleProperty::BranchColor: style.branchColor.reset(); break;
    case StyleProperty::BranchWidth: style.branchWidth.reset(); break;
    case StyleProperty::BranchStyle: style.branchStyle.reset(); break;
    case StyleProperty::Preset: style.preset.reset(); break;
    case StyleProperty::FillStyle: style.fillStyle.reset(); break;
    case StyleProperty::BorderStyle: style.borderStyle.reset(); break;
    case StyleProperty::BorderWidth: style.borderWidth.reset(); break;
    case StyleProperty::Shape: style.shape.reset(); break;
    case StyleProperty::FontFamily: style.fontFamily.reset(); break;
    case StyleProperty::FontSize: style.fontSize.reset(); break;
    case StyleProperty::FontWeight: style.fontWeight.reset(); break;
    case StyleProperty::Italic: style.italic.reset(); break;
    case StyleProperty::Underline: style.underline.reset(); break;
    case StyleProperty::StrikeOut: style.strikeOut.reset(); break;
    case StyleProperty::TextCase: style.textCase.reset(); break;
    case StyleProperty::TextAlign: style.textAlign.reset(); break;
    case StyleProperty::BranchShape: style.branchShape.reset(); break;
    case StyleProperty::BranchStart: style.branchStart.reset(); break;
    case StyleProperty::BranchEnd: style.branchEnd.reset(); break;
    case StyleProperty::Numbering: style.numbering.reset(); break;
    }
    return true;
}

Reading read(const QList<const MindNode *> &nodes, StyleProperty property) {
    Reading reading;
    reading.allManual = true;
    for (const MindNode *node : nodes) {
        if (!node) continue;
        const QVariant value = effectiveValue(node, property);
        if (reading.empty) {
            reading.empty = false;
            reading.value = value;
        } else if (value != reading.value) {
            reading.mixed = true;
        }
        reading.allManual = reading.allManual && hasManualValue(node, property);
    }
    if (reading.empty) reading.allManual = false;
    if (reading.mixed) reading.value = QVariant();
    return reading;
}

bool isTonalPalette(const QStringList &palette) {
    for (const QString &name : palette)
        if (const QColor color(name); color.isValid() && (relativeLuminance(color) >= 0.85 || relativeLuminance(color) <= 0.035))
            return true;
    return false;
}

namespace {
qreal chroma(const QColor &color) {
    const QColor rgb = color.toRgb();
    return std::max({rgb.redF(), rgb.greenF(), rgb.blueF()}) - std::min({rgb.redF(), rgb.greenF(), rgb.blueF()});
}
}  // namespace

namespace {
Backdrop variantLook(const QString &paletteId, const QStringList &palette, int variant);
}  // namespace

Backdrop backdropFor(const QString &paletteId, const QStringList &palette, int variant, const std::optional<QColor> &background) {
    Backdrop look = variantLook(paletteId, palette, variant);
    if (!background || !background->isValid()) return look;
    // Richiesta dell'utente 2026-10-19: lo sfondo libero cambia solo lo sfondo (e il testo dei nodi
    // senza riempimento); i rami tengono i colori della palette e della variante.
    if (look.branches.isEmpty()) look.branches = branchColors(palette, look);
    look.color = background->toRgb();
    return look;
}

namespace {
Backdrop variantLook(const QString &paletteId, const QStringList &palette, int variant) {
    if (variant < 0 || variant >= kVariantCount || variant >= palette.size() || !QColor(palette[variant]).isValid()) return {};
    const QColor own(palette[variant]);
    if (paletteId == QLatin1String("magnifico")) return {own, variant, {readableTextOn(own)}};
    if (paletteId == QLatin1String("corallo"))
        return {QColor::fromHslF(std::max<float>(0, own.hslHueF()), 0.45f, 0.08f).toRgb(), variant, {own}};
    if (!isTonalPalette(palette)) {
        if (variant == 0) return {kMapBackground, 0, {}};  // multicolore come FEAT-031
        return {kMapBackground, variant, {QColor(palette[variant - 1])}};
    }
    // Colore acceso (croma almeno kVividChroma): il riferimento non lo usa mai come sfondo; sfondo bianco e
    // tutti i rami di quel colore, come nelle palette colorate.
    if (chroma(own) >= kVividChroma) return {kMapBackground, variant, {own}};
    Backdrop backdrop{own, variant, {}};
    // Sfondo scuro o quasi bianco (prima variante di Kimono, Menta, Energia): rami multicolori.
    if (isDarkBackground(own) || isNearWhite(own)) return backdrop;  // multicolore con il contrasto (branchColors)
    // Sfondo chiaro: un solo colore, il piu' saturo che si legge; altrimenti il piu' contrastante.
    QColor best, mostContrasting;
    qreal bestChroma = -1, bestContrast = -1;
    for (int i = 0; i < palette.size(); ++i) {
        const QColor color(palette[i]);
        if (i == variant || !color.isValid()) continue;
        const qreal contrast = apcaContrast(color, own);
        if (contrast >= kMinMonoContrast && chroma(color) > bestChroma) { bestChroma = chroma(color); best = color; }
        if (contrast > bestContrast) { bestContrast = contrast; mostContrasting = color; }
    }
    const QColor mono = best.isValid() ? best : (bestContrast >= kMinBranchContrast ? mostContrasting : readableTextOn(own));
    backdrop.branches = {mono};
    return backdrop;
}
}  // namespace

bool isDarkBackground(const QColor &background) { return readableTextOn(background) == QColor(Qt::white); }

QList<QColor> branchColors(const QStringList &palette, const Backdrop &backdrop) {
    if (!backdrop.branches.isEmpty()) return backdrop.branches;  // decisi dalla variante
    QList<QColor> colors;
    if (backdrop.isDefault()) {
        // Una sola conversione per colore di palette, non per nodo; i colori quasi bianchi non si
        // assegnano ai rami (FEAT-031), a meno che la palette non abbia solo quelli.
        for (const QString &name : palette)
            if (const QColor color(name); color.isValid() && !isNearWhite(color)) colors.append(color);
        if (colors.isEmpty())
            for (const QString &name : palette)
                if (const QColor color(name); color.isValid()) colors.append(color);
        return colors;
    }
    // FEAT-036: dal colore dopo quello dello sfondo, girando la palette, senza lo sfondo stesso.
    const int count = static_cast<int>(palette.size());
    QColor best;
    qreal bestContrast = -1;
    for (int step = 1; step <= count; ++step) {
        const int index = ((backdrop.variant < 0 ? -1 : backdrop.variant) + step) % count;
        if (index == backdrop.variant) continue;
        const QColor color(palette[index]);
        if (!color.isValid()) continue;
        const qreal contrast = apcaContrast(color, backdrop.color);
        if (contrast >= kMinBranchContrast) colors.append(color);
        if (contrast > bestContrast) { bestContrast = contrast; best = color; }
    }
    if (colors.isEmpty()) colors.append(bestContrast >= kMinBranchContrast ? best : readableTextOn(backdrop.color));
    return colors;
}

QColor annotationColor(const QColor &onDefault, const Backdrop &backdrop) {
    return backdrop.color == kMapBackground ? onDefault : readableTextOn(backdrop.color);
}

QList<MindNode *> resolveTree(MindNode *root, const QStringList &palette, const Backdrop &backdrop, const SheetStyle &sheet,
                              const QList<MindNode *> &summaryTopics) {
    QList<MindNode *> changed;
    if (!root) return changed;
    const Effective defaults = appDefaults();
    const QList<QColor> paletteColors = branchColors(palette, backdrop);
    // FEAT-036: testo dei nodi che stanno sullo sfondo (centrale, dal terzo livello, senza
    // riempimento): sullo sfondo predefinito il colore del ramo scurito, altrimenti chiaro o scuro.
    const bool plainBackdrop = backdrop.color == kMapBackground;
    const QColor textOnBackdrop = readableTextOn(backdrop.color);
    const auto over = [&backdrop](const QColor &top) {
        const qreal a = top.alphaF();
        const QColor &bottom = backdrop.color;
        return QColor::fromRgbF(top.redF() * a + bottom.redF() * (1 - a), top.greenF() * a + bottom.greenF() * (1 - a),
                                top.blueF() * a + bottom.blueF() * (1 - a));
    };
    // Posizione nella sequenza dei colori: i rami principali in ordine, poi i nodi liberi.
    QHash<const MindNode *, int> sequence;
    int position = 0;
    for (const auto &child : root->children) if (!child->floating) sequence.insert(child.get(), position++);
    for (const auto &child : root->children) if (child->floating) sequence.insert(child.get(), position++);
    const QColor transparent(0, 0, 0, 0);
    const ImportantColors important = importantColors(palette);  // una volta per palette
    // Testo scurito calcolato una volta per colore del ramo, non per nodo.
    QHash<QRgb, QColor> darkened;
    auto darkenedFor = [&darkened, plainBackdrop, textOnBackdrop](const QColor &branch) {
        if (!plainBackdrop) return textOnBackdrop;
        auto it = darkened.constFind(branch.rgba());
        if (it == darkened.cend()) it = darkened.insert(branch.rgba(), darkenedText(branch));
        return *it;
    };
    const QColor rootText = plainBackdrop ? QColor(kReadableDarkText) : textOnBackdrop;
    // FEAT-043: rami con un colore scelto a mano (sul nodo o su un antenato, compreso il centrale per
    // i principali): con "Ramo colorato" spento la loro linea tiene quel colore.
    QSet<const MindNode *> manualBranch;
    QColor rootLineColor;  // FEAT-043: testo del centrale, per le linee con "Ramo colorato" spento
    const MapVariants::Variant *variant = MapVariants::find(sheet.variant);  // FEAT-026 fase D
    // FEAT-052: effective values and depth of the summary topics' parents, kept during the tree visit.
    QHash<const MindNode *, std::pair<Effective, int>> summaryParents;
    for (const MindNode *topic : summaryTopics) if (topic && topic->parent) summaryParents.insert(topic->parent, {});

    std::function<void(MindNode *, const Effective *, int)> visit = [&](MindNode *node, const Effective *parent, int depth) {
        const NodeStyleOverrides &style = node->style;
        Effective next = defaults;
        // Colore del ramo: scelto a mano, poi quello del genitore (cascata, BUG-005); i rami
        // principali senza un colore scelto sul nodo centrale prendono la palette per posizione.
        if (style.branchColor || (node->parent && manualBranch.contains(node->parent))
            || (depth == 1 && node->parent->style.branchColor))
            manualBranch.insert(node);
        if (style.branchColor) {
            next.branchColor = *style.branchColor;
        } else if (parent && depth == 1) {
            if (node->parent->style.branchColor) next.branchColor = parent->branchColor;
            else if (!paletteColors.isEmpty()) next.branchColor = paletteColors[sequence.value(node) % paletteColors.size()];
        } else if (parent) {
            next.branchColor = parent->branchColor;
        }
        // Colori del nodo dallo stile predefinito (FEAT-020) e dal colore del ramo, mai dai valori
        // scelti a mano degli antenati (BUG-007). I nodi liberi hanno l'aspetto dei principali.
        const QColor &color = next.branchColor;
        next.preset = style.preset.value_or(NodePreset::Predefinito);
        // The central topic's fill and border, once turned on, take its text colour (black on
        // Alba, live 2026-10-22; before: the branch colour, orange).
        QColor fill = depth == 0 ? rootText : color;
        QColor borderColor = depth == 0 ? rootText : color;
        NodeFillStyle fillStyle = NodeFillStyle::Nessuno;
        NodeBorderStyle borderStyle = NodeBorderStyle::Nessuno;
        next.strikeOut = false;
        // Tre ruoli: centrale, rami principali (anche gli argomenti mobili), il resto; e i
        // sommari (FEAT-052): filled with the central topic's text colour, small text (measured live 2026-10-24).
        const bool summary = node->summaryTopic;
        const bool mainTopic = !summary && (depth == 1 || node->floating);
        // FEAT-026 fase D: aspetto del ruolo nella variante del foglio (solo per lo stile Predefinito).
        const MapVariants::Role *role = !variant || summary || next.preset != NodePreset::Predefinito ? nullptr
                                      : depth == 0 ? &variant->root : mainTopic ? &variant->main : &variant->sub;
        switch (next.preset) {
        case NodePreset::Predefinito:  // aspetto del livello (FEAT-031)
            if (summary) {
                fill = rootText;
                borderColor = rootText;
                fillStyle = NodeFillStyle::Pieno;
                next.text = readableTextOn(fill);
            } else if (depth == 1) {
                fillStyle = NodeFillStyle::Pieno;
                next.text = readableTextOn(color);
            } else if (depth == 2) {
                fillStyle = NodeFillStyle::Pieno;
                fill.setAlphaF(0.2f);
                // FEAT-036: su uno sfondo colorato il testo deve leggersi sul 20% sopra lo sfondo.
                next.text = plainBackdrop ? darkenedFor(color) : readableTextOn(over(fill));
            } else {
                next.text = depth == 0 ? rootText : darkenedFor(color);
            }
            if (role) {
                // Il centrale non ha il colore di un ramo: pieno e bordo nel colore del suo testo.
                const QColor own = depth == 0 ? rootText : color;
                borderColor = own;
                switch (role->look) {
                case MapVariants::Look::Default: break;
                case MapVariants::Look::Filled:
                case MapVariants::Look::Dark:
                    fill = role->look == MapVariants::Look::Dark ? rootText : own;
                    fillStyle = NodeFillStyle::Pieno;
                    next.text = readableTextOn(fill);
                    break;
                case MapVariants::Look::Light:
                    fill = own;
                    fill.setAlphaF(0.2f);
                    fillStyle = NodeFillStyle::Pieno;
                    next.text = plainBackdrop ? (depth == 0 ? rootText : darkenedFor(color)) : readableTextOn(over(fill));
                    break;
                case MapVariants::Look::Outline:
                    fillStyle = NodeFillStyle::Nessuno;
                    borderStyle = NodeBorderStyle::Continuo;
                    break;
                case MapVariants::Look::Underline:
                case MapVariants::Look::Plain:
                    fillStyle = NodeFillStyle::Nessuno;
                    borderStyle = NodeBorderStyle::Nessuno;
                    break;
                }
                if (role->fillStyle) fillStyle = *role->fillStyle;
                if (role->borderStyle) borderStyle = *role->borderStyle;
            }
            break;
        case NodePreset::MoltoImportante:
        case NodePreset::Importante:
            fill = next.preset == NodePreset::MoltoImportante ? important.veryImportant : important.important;
            fillStyle = NodeFillStyle::Pieno;
            next.text = readableTextOn(fill);
            break;
        case NodePreset::Depenna:
            fill = backdrop.color;  // FEAT-036: lo sfondo del foglio
            fillStyle = NodeFillStyle::Pieno;
            borderStyle = NodeBorderStyle::Continuo;
            next.text = darkenedFor(color);
            next.strikeOut = true;
            break;
        }
        // Forma (parte B): con le linee sotto il riempimento non si vede, la linea si'.
        const NodeShape roleShape = !role ? NodeShape::RettangoloArrotondato
                                  : role->shape ? *role->shape
                                  : role->look == MapVariants::Look::Underline ? NodeShape::LineaSotto : NodeShape::RettangoloArrotondato;
        next.shape = style.shape.value_or(roleShape);
        const bool underline = next.shape == NodeShape::LineaSotto || next.shape == NodeShape::DoppiaLineaSotto;
        if (underline) {
            borderStyle = NodeBorderStyle::Continuo;
            next.text = depth == 0 ? rootText : darkenedFor(color);
        }
        if (style.fill) { fill = *style.fill; fillStyle = NodeFillStyle::Pieno; }
        if (style.border) borderStyle = NodeBorderStyle::Continuo;
        next.fillStyle = style.fillStyle.value_or(fillStyle);
        next.borderStyle = style.borderStyle.value_or(borderStyle);
        next.fill = next.fillStyle == NodeFillStyle::Nessuno || underline ? transparent : fill;
        next.border = next.borderStyle == NodeBorderStyle::Nessuno ? transparent : style.border.value_or(borderColor);
        // Senza riempimento o con un motivo il testo sta sullo sfondo della mappa: come dal terzo
        // livello e con le linee sotto, colore del nodo scurito (prima un principale con "Nessun
        // riempimento" scelto a mano teneva il testo per il pieno, anche bianco su bianco).
        // FEAT-030: the connectors' text is below their mark, on the map's background too.
        if (next.fillStyle == NodeFillStyle::Nessuno || isPatternFill(next.fillStyle) || NodeShapes::hasTextBelow(next.shape))
            next.text = depth == 0 ? rootText : darkenedFor(color);
        // A solid fill chosen by hand: the text that reads on it (white on a black central topic).
        else if ((style.fill || style.fillStyle) && !underline)
            next.text = readableTextOn(over(next.fill));
        next.borderWidth = style.borderWidth.value_or(role && role->borderWidth ? *role->borderWidth : kDefaultBorderWidthPx);
        // FEAT-072 (user's comparison 2026-10-23): an underline is never thinner than the line
        // that reaches it, so line and underline read as one stroke (before: lines 3 px, underlines 2
        // and 1); a thicker bar of the variant stays (variant 5).
        if (underline && !style.borderWidth && parent) next.borderWidth = std::max(next.borderWidth, parent->branchWidth);
        // FEAT-072: in the underlined variants the main topics' text is black, not the branch colour.
        if (underline && mainTopic && role && role->look == MapVariants::Look::Underline) next.text = rootText;
        if (style.text) next.text = *style.text;
        // FEAT-022: testo del nodo, solo per quel nodo; predefiniti per ruolo (centrale 13 pt
        // grassetto, gli altri 11 pt normale). Il barrato senza scelta e' quello di Depenna.
        next.fontFamily = style.fontFamily.value_or(sheet.fontFamily);  // FEAT-043: font del foglio
        // FEAT-072: in the logic chart the central topic is 28 px (21 pt), so a usual title stays on one line.
        const bool logicRoot = MapStructure::typeFromKey(node->structure) == MapStructure::Type::LogicChart;
        const qreal rootSize = variant && variant->rootFontSizePt > 0 ? variant->rootFontSizePt : logicRoot ? kLogicRootFontSizePt : kRootFontSizePt;
        const qreal mainSize = variant && variant->mainFontSizePt > 0 ? variant->mainFontSizePt : kMainFontSizePt;
        next.fontSize = style.fontSize.value_or(depth == 0 ? rootSize : summary ? Summaries::kTopicFontSizePt : mainTopic ? mainSize : kDefaultFontSizePt);
        const int rootWeight = variant && variant->rootFontWeight > 0 ? variant->rootFontWeight : logicRoot ? kLogicRootFontWeight : kRootFontWeight;
        const int mainWeight = variant && variant->mainFontWeight > 0 ? variant->mainFontWeight : kMainFontWeight;
        const int subWeight = variant && variant->subFontWeight > 0 ? variant->subFontWeight : kDefaultFontWeight;
        next.fontWeight = style.fontWeight.value_or(depth == 0 ? rootWeight : mainTopic ? mainWeight : subWeight);
        next.italic = style.italic.value_or(false);
        next.underline = style.underline.value_or(false);
        next.strikeOut = style.strikeOut.value_or(next.strikeOut);
        next.textCase = style.textCase.value_or(TextCase::Nessuna);
        // Il centrale centrato (foto dell'utente 2026-10-21), gli altri a sinistra.
        next.textAlign = style.textAlign.value_or(depth == 0 ? TextAlign::Centro : TextAlign::Sinistra);
        // FEAT-072: the shape applies to the lines towards the topic's own children only;
        // deeper levels keep the map default (live check 2026-10-22, before: inherited).
        next.branchShape = style.branchShape.value_or(BranchShape::Auto);
        // Linee verso i figli (BUG-006): lo stile si eredita, lo spessore no.
        if (style.branchStyle) next.branchStyle = *style.branchStyle;
        else if (parent) next.branchStyle = parent->branchStyle;
        else if (variant && variant->freehandLines) next.branchStyle = kFreehandSolidLine;  // FEAT-026 fase D
        // FEAT-023: line ends are inherited like the line style.
        next.branchStart = style.branchStart.value_or(parent ? parent->branchStart : BranchEnd::None);
        next.branchEnd = style.branchEnd.value_or(parent ? parent->branchEnd : BranchEnd::None);
        if (style.branchWidth) next.branchWidth = *style.branchWidth;
        else if (sheet.branchWidth > 0) next.branchWidth = sheet.branchWidth;  // FEAT-043
        else if (depth == 0 && variant && variant->rootBranchWidth > 0) next.branchWidth = variant->rootBranchWidth;
        if (depth == 0) rootLineColor = next.text;
        next.lineColor = sheet.coloredBranches || manualBranch.contains(node) ? next.branchColor
                       : sheet.lineColor.isValid() ? sheet.lineColor : rootLineColor;
        if (!(effectiveOf(node) == next)) {
            node->fill = next.fill;
            node->text = next.text;
            node->border = next.border;
            node->branchColor = next.branchColor;
            node->lineColor = next.lineColor;
            node->branchWidth = next.branchWidth;
            node->branchStyle = next.branchStyle;
            node->preset = next.preset;
            node->fillStyle = next.fillStyle;
            node->borderStyle = next.borderStyle;
            node->borderWidth = next.borderWidth;
            node->strikeOut = next.strikeOut;
            node->shape = next.shape;
            node->fontFamily = next.fontFamily;
            node->fontSize = next.fontSize;
            node->fontWeight = next.fontWeight;
            node->italic = next.italic;
            node->underline = next.underline;
            node->textCase = next.textCase;
            node->textAlign = next.textAlign;
            node->branchShape = next.branchShape;
            node->branchStart = next.branchStart;
            node->branchEnd = next.branchEnd;
            changed.append(node);
        }
        if (const auto it = summaryParents.find(node); it != summaryParents.end()) *it = {next, depth};
        for (const auto &child : node->children) visit(child.get(), &next, depth + 1);
    };
    visit(root, nullptr, 0);
    for (MindNode *topic : summaryTopics) {
        const auto it = topic && topic->parent ? summaryParents.constFind(topic->parent) : summaryParents.cend();
        if (it != summaryParents.cend()) visit(topic, &it->first, it->second + 1);
    }
    return changed;
}

QPointF textPadding(const MindNode *node) {
    if (node->summaryTopic) return Summaries::kTopicPadding;  // FEAT-052
    if (!node->parent)
        return MapStructure::typeFromKey(node->structure) == MapStructure::Type::Brace ? kBraceRootTextPadding : kRootTextPadding;
    return node->parent->parent ? kSubTextPadding : kMainTextPadding;  // figli del centrale e argomenti mobili
}

}  // namespace NodeStyle
