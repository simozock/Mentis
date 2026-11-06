#pragma once

// Palette dell'interfaccia scura (FEAT-035 parte A, AGENTS.md "Interfaccia scura"): un solo punto
// per i colori di modali, Impostazioni, menu, pannello laterale e Home. La mappa, l'outliner e i
// gruppi flottanti sopra la mappa restano chiari e non usano questi token.
// FEAT-035 part B: the interface's font, text size and density (Settings > Appearance), for the
// whole interface except the map, the Gantt chart and the exports.

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>
#include <QStringList>

namespace UiTheme {

enum class Token { Surface, SurfaceSunken, SurfaceRaised, Divider, Text, TextMuted, Accent, Scrim };

QColor color(Token token);

// Foglio di stile con i nomi dei token al posto dei colori: "@surface", "@surfaceSunken",
// "@surfaceRaised", "@divider", "@text", "@textMuted", "@accent", "@scrim".
QString qss(const QString &styleSheet);

enum class Density { Normal, Compact };

inline constexpr int kDefaultFontSize = 14;  // today's look: every size is scaled by size / 14
inline constexpr int kMinFontSize = 12;
inline constexpr int kMaxFontSize = 18;
inline const QString kDefaultFontFamily = QStringLiteral("Atkinson Hyperlegible");
inline const QString kSystemFont = QStringLiteral("system");

struct Appearance {
    QString fontFamily;  // empty = kDefaultFontFamily, kSystemFont = the operating system's font
    int fontSize = kDefaultFontSize;
    Density density = Density::Normal;
    bool operator==(const Appearance &) const = default;
};

Appearance appearance();
// Reads the settings (a missing or unknown value is the default) and applies them; called once at
// startup, before the first window. Installs the filter that scales every style sheet.
void loadAppearance();
// Saves, applies to the open windows and emits AppearanceNotifier::changed.
void setAppearance(const Appearance &value);
// The bundled text fonts offered in Settings, after the default one (no code or maths fonts).
QStringList bundledFontFamilies();
// The family actually used by the interface.
QString fontFamily();

// Today's application font, which the map, the Gantt chart and the exports keep whatever the
// interface: for the widgets that draw the document (map view, Gantt chart, map thumbnails).
QFont documentFont();
// The widgets' font at the chosen family and size (for painting outside a widget's own font).
QFont interfaceFont();
// An interface text size at the chosen size: base * size / 14, rounded to whole pixels.
int fontPx(int base);
// A control's height at the chosen size: larger text makes it taller (rounded up to the 4 px grid).
int heightPx(int base);
// A gap, padding or height at the chosen density: Compact goes one step down (48 -> 40, 32 -> 28,
// 30 -> 26, 28 -> 24, 16 -> 12, 12 -> 8, 8 -> 4); other values stay.
int space(int normal);
// A widget with this property set to true computes its own sizes with fontPx / space / heightPx
// (style sheets built from zoomed values): the appearance filter leaves its style sheet alone.
inline constexpr char kOwnSizes[] = "mentisOwnSizes";
// Every "font-size", "min-height", "max-height" and "padding" of a style sheet at the chosen size and density.
QString scaleStyleSheet(const QString &styleSheet);

class AppearanceNotifier final : public QObject {
    Q_OBJECT
public:
    static AppearanceNotifier *instance();
signals:
    void changed();
};

}  // namespace UiTheme
