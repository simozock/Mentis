#pragma once

// FEAT-052 (docs/features/FEAT-052-confini.md): boundaries around a branch or a run of sibling
// topics. Data, JSON form and geometry; no widgets or scene items.

#include "nodestyle.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <optional>

struct Boundary {
    enum class Shape { RoundedRectangle, Rectangle };
    QString id;
    QStringList topics;  // the covered topics (with their branches), siblings first to last
    QString title;       // empty = no title tab
    Shape shape = Shape::RoundedRectangle;
    // Style chosen by hand; unset = default (see Boundaries::k* below).
    std::optional<QColor> lineColor;
    std::optional<qreal> lineWidth;
    std::optional<int> linePattern;  // Qt::PenStyle: SolidLine, DashLine or DotLine
    std::optional<QColor> fillColor;
    bool operator==(const Boundary &) const = default;
};

namespace Boundaries {

// Defaults (measured live 2026-10-24 on the reference, px at 100%): stroke black at 40% alpha, dashed
// 7 on / 2 off, 8 px outside the covered topics, corner radius 6, fill black at 8% alpha. The reference
// names the width "Thin": the same named width as the relationships.
inline constexpr qreal kMarginPx = 8;
inline constexpr qreal kCornerRadiusPx = 6;
inline constexpr qreal kDefaultLineWidth = 2;
inline constexpr int kDefaultLinePattern = 2;  // Qt::DashLine
inline constexpr qreal kDashOnPx = 7;
inline constexpr qreal kDashOffPx = 2;
inline constexpr qreal kLineAlpha = 0.4;
inline constexpr qreal kFillAlpha = 0.08;
// Title tab: 12 px from the left side, 18 px high, 10 px text padding, top corners radius 2,
// filled with the line color; text 14 px.
inline constexpr qreal kTitleInsetPx = 12;
inline constexpr qreal kTitleHeightPx = 18;
inline constexpr qreal kTitlePaddingPx = 10;
inline constexpr qreal kTitleRadiusPx = 2;
inline constexpr qreal kTitleFontPx = 14;

QString shapeKey(Boundary::Shape shape);
std::optional<Boundary::Shape> shapeFromKey(const QString &key);

QJsonObject toJson(const Boundary &boundary);
// nullopt when the object has no id or no topics (unknown values fall back to the defaults).
std::optional<Boundary> fromJson(const QJsonObject &object);
QJsonArray toJson(const QList<Boundary> &boundaries);
// Entries that are not valid objects are skipped; old id lists are not read (no backward
// compatibility during development, user rule).
QList<Boundary> listFromJson(const QJsonArray &array);

// Outline of the stroke around `covered` (the union of the covered topics' boxes).
QRectF strokeRect(const QRectF &covered);
QPainterPath outline(Boundary::Shape shape, const QRectF &stroke);
// Title tab standing on the top edge of `stroke`, wide enough for a text `textWidth` wide.
QRectF titleRect(const QRectF &stroke, qreal textWidth);
QPainterPath titlePath(const QRectF &tab);

// Room the layout leaves above and below the covered run (margin, half the stroke; the tab).
qreal padAbove(const Boundary &boundary);
qreal padBelow(const Boundary &boundary);

}  // namespace Boundaries
