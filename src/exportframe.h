#pragma once
// FEAT-084 (docs/features/FEAT-084-cornice-esportazione.md): a frame around the exported content
// (images and PDF pages of the map, the outline, the Gantt and Pitch). Core value type, no widgets:
// the band of `width()` px inside an outer rectangle, in vector paths.
#include <QColor>
#include <QCoreApplication>
#include <QRectF>
#include <QString>

class QPainter;

struct ExportFrame {
    Q_DECLARE_TR_FUNCTIONS(ExportFrame)
public:
    // The nodes' line styles, double, then the ornaments of our own (user, 2026-10-31; the hand-drawn
    // ones taken out the same day: at a page's size they looked as the plain ones).
    enum class Style { None, Solid, Dashed, Dotted, Double, GreekKey, Waves, Chain, Dentils };
    static constexpr int kStyles = 9;
    static constexpr double kMinThickness = 1;
    static constexpr double kMaxThickness = 40;
    static constexpr double kMinOrnament = 8;  // an ornament thinner than this does not show its motif
    static constexpr double kDefaultThickness = 4;
    static const QColor &defaultColor();  // the accent, #7047eb

    Style style = Style::None;
    double thickness = kDefaultThickness;
    QColor color = defaultColor();
    bool rounded = false;

    bool enabled() const { return style != Style::None; }
    static bool isOrnament(Style style) { return style >= Style::GreekKey; }
    // The band's width: 0 without a frame; ornaments at least kMinOrnament; clamped to the range.
    double width() const;
    double radius() const;  // the outer corners' radius when rounded: 4 x width, 8-32 px
    static QString name(Style style);
    // The band inside outer (its outer edge on outer's edges).
    void paint(QPainter *painter, const QRectF &outer) const;
    // A straight piece of the style filling band (its height the band's width): the pictures of the
    // Style select.
    void paintStrip(QPainter *painter, const QRectF &band) const;
    bool operator==(const ExportFrame &) const = default;
};
