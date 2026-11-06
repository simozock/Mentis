#include "uitheme.h"

#include "nodeitem.h"

#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QLayout>
#include <QRegularExpression>
#include <QSettings>
#include <QWidget>
#include <algorithm>
#include <functional>
#include <cmath>
#include <utility>

namespace UiTheme {
namespace {
// Valori misurati sulle foto di riferimento (docs/features/attachments/FEAT-035, 2026-10-18).
// Valori costanti (QRgb), inizializzati prima di ogni altro codice: altri file li leggono anche
// nelle proprie costanti globali.
struct Entry {
    Token token;
    const char *name;
    QRgb value;
};
constexpr Entry kEntries[] = {
    // I nomi piu' lunghi prima: "@surfaceSunken" non deve diventare "<surface>Sunken".
    {Token::SurfaceSunken, "@surfaceSunken", qRgb(0x1f, 0x23, 0x26)},
    {Token::SurfaceRaised, "@surfaceRaised", qRgb(0x3f, 0x43, 0x48)},
    {Token::Surface, "@surface", qRgb(0x2b, 0x2f, 0x33)},
    {Token::Divider, "@divider", qRgb(0x38, 0x3c, 0x40)},
    {Token::TextMuted, "@textMuted", qRgb(0x74, 0x7a, 0x80)},
    {Token::Text, "@text", qRgb(0xff, 0xff, 0xff)},
    {Token::Accent, "@accent", qRgb(0x70, 0x47, 0xeb)},
    {Token::Scrim, "@scrim", qRgba(15, 23, 42, 61)},  // rgba(15,23,42,0.24)
};

QString cssValue(const QColor &color) {
    if (color.alpha() == 255) return color.name();
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}
}  // namespace

QColor color(Token token) {
    for (const Entry &entry : kEntries)
        if (entry.token == token) return QColor::fromRgba(entry.value);
    return {};
}

QString qss(const QString &styleSheet) {
    QString result = styleSheet;
    for (const Entry &entry : kEntries) result.replace(QLatin1String(entry.name), cssValue(QColor::fromRgba(entry.value)));
    return result;
}

namespace {
Appearance g_appearance;

QFont g_interfaceFont;  // today's application font at the chosen family and size

void rescale(QWidget *widget) {
    const QString current = widget->styleSheet();
    if (current.isEmpty() || widget->property(kOwnSizes).toBool()) return;
    const QString scaled = scaleStyleSheet(current);
    if (scaled != current) widget->setStyleSheet(scaled);  // its StyleChange finds nothing to change
}

// The interface font reaches every widget two ways (checked with Qt 6.12, 2026-11-01): the class font
// of "QWidget" reaches the widgets inside a window (also under a style sheet, which stops a parent's
// font) but not the windows, which get it with setFont. QApplication::font(), the base of QFont() and
// of every painter on an image (map, exports), stays today's font, so the document never follows the
// interface. A window whose code chose a font keeps it.
constexpr char kOwnFont[] = "mentisInterfaceFont";

void applyWindowFont(QWidget *window) {
    const bool own = window->property(kOwnFont).toBool();
    if (window->testAttribute(Qt::WA_SetFont) && !own) return;
    if (g_appearance == Appearance{}) {
        if (!own) return;
        window->setFont(QFont());  // inherits the application font again
        window->setProperty(kOwnFont, false);
        return;
    }
    window->setFont(g_interfaceFont);
    window->setProperty(kOwnFont, true);
}

// Density of the layouts and of the minimum heights written in the code: the value the code set is
// the base, kept with the value written in its place; a value the code changes later is a new base.
constexpr char kBaseValues[] = "mentisDensityBase";
constexpr char kAppliedValues[] = "mentisDensityApplied";

QList<int> densified(QObject *object, const QList<int> &current, const std::function<int(int)> &map) {
    QList<int> base = current;
    if (object->property(kAppliedValues).value<QList<int>>() == current) base = object->property(kBaseValues).value<QList<int>>();
    QList<int> result;
    for (const int value : std::as_const(base)) result.append(value < 0 ? value : map(value));
    object->setProperty(kBaseValues, QVariant::fromValue(base));
    object->setProperty(kAppliedValues, QVariant::fromValue(result));
    return result;
}

void densifyLayout(QLayout *layout) {
    const QMargins margins = layout->contentsMargins();
    const QList<int> values = densified(layout, {layout->spacing(), margins.left(), margins.top(), margins.right(), margins.bottom()}, space);
    if (values[0] >= 0 && values[0] != layout->spacing()) layout->setSpacing(values[0]);
    const QMargins wanted(values[1], values[2], values[3], values[4]);
    if (wanted != margins) layout->setContentsMargins(wanted);
    for (int i = 0; i < layout->count(); ++i)
        if (QLayout *child = layout->itemAt(i)->layout()) densifyLayout(child);
}

void densify(QWidget *widget) {
    if (QLayout *layout = widget->layout()) densifyLayout(layout);
    // A minimum height of a widget that can grow (rows, panels); fixed sizes (icon buttons) stay. A
    // minimum height from the widget's style sheet (Qt marks it "_q_stylesheet_minh") is already at
    // the chosen density: changing it again would shrink it twice (review 2026-11-01).
    if (widget->property("_q_stylesheet_minh").toBool()) return;
    if (widget->maximumHeight() != QWIDGETSIZE_MAX || (widget->minimumHeight() <= 0 && !widget->property(kAppliedValues).isValid())) return;
    const int height = densified(widget, {widget->minimumHeight()}, space).constFirst();
    if (height != widget->minimumHeight()) widget->setMinimumHeight(height);
}

// Every new or changed style sheet (setStyleSheet sends QEvent::StyleChange) is scaled once, and
// every new widget gets the density and, for a window, the interface font before it is shown: the
// code keeps writing today's sizes and no component needs to know the setting.
class AppearanceFilter final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (!watched->isWidgetType()) return false;
        auto *widget = static_cast<QWidget *>(watched);
        if (event->type() == QEvent::StyleChange) rescale(widget);
        else if (event->type() == QEvent::Polish) {
            if (widget->isWindow()) applyWindowFont(widget);
            densify(widget);
        }
        return false;
    }
};

void apply() {
    g_interfaceFont = documentFont();
    const QString family = fontFamily();
    if (family != kDefaultFontFamily) NodeItem::loadBundledFonts();  // registered only when chosen
    g_interfaceFont.setFamilies({family});
    const qreal scale = qreal(g_appearance.fontSize) / kDefaultFontSize;
    if (g_interfaceFont.pointSizeF() > 0) g_interfaceFont.setPointSizeF(g_interfaceFont.pointSizeF() * scale);
    else g_interfaceFont.setPixelSize(qRound(g_interfaceFont.pixelSize() * scale));
    QApplication::setFont(documentFont());  // also clears the class fonts: today's look
    if (!(g_appearance == Appearance{})) QApplication::setFont(g_interfaceFont, "QWidget");
    for (QWidget *window : QApplication::topLevelWidgets()) applyWindowFont(window);
    for (QWidget *widget : QApplication::allWidgets()) {
        rescale(widget);
        densify(widget);
    }
    emit AppearanceNotifier::instance()->changed();
}
}  // namespace

Appearance appearance() { return g_appearance; }

void loadAppearance() {
    const QSettings settings;
    Appearance value;
    const QString family = settings.value("ui/fontFamily").toString();
    if (family == kSystemFont || bundledFontFamilies().contains(family)) value.fontFamily = family;
    const int size = settings.value("ui/fontSize", kDefaultFontSize).toInt();
    if (size >= kMinFontSize && size <= kMaxFontSize) value.fontSize = size;
    if (settings.value("ui/density").toString() == QLatin1String("compact")) value.density = Density::Compact;
    g_appearance = value;
    static const bool installed = [] {
        qApp->installEventFilter(new AppearanceFilter(qApp));
        return true;
    }();
    Q_UNUSED(installed);
    apply();
}

void setAppearance(const Appearance &value) {
    if (value == g_appearance) return;
    g_appearance = value;
    g_appearance.fontSize = std::clamp(value.fontSize, kMinFontSize, kMaxFontSize);
    QSettings settings;
    settings.setValue("ui/fontFamily", g_appearance.fontFamily);
    settings.setValue("ui/fontSize", g_appearance.fontSize);
    settings.setValue("ui/density", g_appearance.density == Density::Compact ? QStringLiteral("compact") : QStringLiteral("normal"));
    apply();
}

QStringList bundledFontFamilies() {
    return {QStringLiteral("Atkinson Hyperlegible Next"), QStringLiteral("Arvo"), QStringLiteral("Montserrat"),
            QStringLiteral("OpenDyslexic"), QStringLiteral("Roboto")};
}

QString fontFamily() {
    if (g_appearance.fontFamily.isEmpty()) return kDefaultFontFamily;
    if (g_appearance.fontFamily == kSystemFont) return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    return g_appearance.fontFamily;
}

QFont documentFont() {
    static const QFont font = QApplication::font();  // Atkinson at the system's size (main.cpp)
    return font;
}

QFont interfaceFont() { return g_appearance == Appearance{} ? documentFont() : g_interfaceFont; }

int fontPx(int base) { return qRound(qreal(base) * g_appearance.fontSize / kDefaultFontSize); }

int heightPx(int base) {
    // Heights follow the text only upwards, on the 4 px grid: smaller text keeps today's heights
    // (Compact density makes them smaller).
    if (g_appearance.fontSize <= kDefaultFontSize) return base;
    return int(std::ceil(qreal(base) * g_appearance.fontSize / kDefaultFontSize / 4)) * 4;
}

int space(int normal) {
    if (g_appearance.density == Density::Normal) return normal;
    // One step down on the 4 px grid (FEAT-035 part B); 2, 4, separators and other values stay.
    switch (normal) {
    case 48: return 40;
    case 32: return 28;
    case 30: return 26;  // a 32 px control's min-height inside its 1 px border
    case 28: return 24;
    case 16: return 12;
    case 12: return 8;
    case 8: return 4;
    default: return normal;
    }
}

// A changed value keeps its base in a comment, "font-size:15px/*12*/": a sheet copied to another
// widget, extended or changed again still starts from today's value, never from a changed one.
QString scaleStyleSheet(const QString &styleSheet) {
    const bool changed = !(g_appearance == Appearance{}) && (g_appearance.fontSize != kDefaultFontSize || g_appearance.density != Density::Normal);
    if (!styleSheet.contains(QLatin1String("font-size")) && !styleSheet.contains(QLatin1String("-height")) &&
        !styleSheet.contains(QLatin1String("padding")))
        return styleSheet;
    if (!changed && !styleSheet.contains(QLatin1String("px/*"))) return styleSheet;
    static const QRegularExpression declaration(QStringLiteral("(font-size|min-height|max-height|padding(?:-left|-right|-top|-bottom)?)\\s*:([^;}]*)"));
    static const QRegularExpression length(QStringLiteral("(\\d+)px(?:/\\*(\\d+)\\*/)?"));
    QString result;
    qsizetype last = 0;
    for (auto it = declaration.globalMatch(styleSheet); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QString property = match.captured(1);
        const QString value = match.captured(2);
        QString written;
        qsizetype from = 0;
        for (auto lengths = length.globalMatch(value); lengths.hasNext();) {
            const QRegularExpressionMatch number = lengths.next();
            const int base = (number.hasCaptured(2) ? number.captured(2) : number.captured(1)).toInt();
            const int px = property == QLatin1String("font-size")    ? fontPx(base)
                           : property.endsWith(QLatin1String("-height")) ? heightPx(space(base))
                                                                      : space(base);
            written += QStringView(value).mid(from, number.capturedStart(0) - from);
            written += changed ? QStringLiteral("%1px/*%2*/").arg(px).arg(base) : QStringLiteral("%1px").arg(base);
            from = number.capturedEnd(0);
        }
        written += QStringView(value).mid(from);
        result += QStringView(styleSheet).mid(last, match.capturedStart(2) - last);
        result += written;
        last = match.capturedEnd(0);
    }
    result += QStringView(styleSheet).mid(last);
    return result;
}

AppearanceNotifier *AppearanceNotifier::instance() {
    static AppearanceNotifier notifier;
    return &notifier;
}

}  // namespace UiTheme
