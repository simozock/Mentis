#pragma once

// FEAT-053 (docs/features/FEAT-053-relazioni.md): relationships between two map elements.
// Data, JSON form and geometry; no widgets or scene items.

#include "nodestyle.h"

#include <QColor>
#include <QFont>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <optional>

struct Relationship {
    enum class Shape { Curved, Straight, Elbow };
    QString id;
    // Ids of the end elements: topics (summary topics included) or boundaries (FEAT-052).
    QString from, to;
    Shape shape = Shape::Curved;
    // Control points as offsets from the centre of the element at that end; unset = automatic,
    // so a relationship whose handles were never moved follows its elements.
    std::optional<QPointF> fromControl, toControl;
    QString label;  // empty = none (a placeholder is shown only while selected)
    // Style chosen by hand; unset = default (see Relationships::k* below).
    std::optional<QColor> lineColor;
    std::optional<qreal> lineWidth;
    std::optional<int> linePattern;  // Qt::PenStyle: SolidLine, DashLine or DotLine
    // Marks at the source and target ends, the same as the branches' (FEAT-023; user 2026-10-24).
    std::optional<BranchEnd> startMark, endMark;
    QString fontFamily;              // empty = the sheet's font
    std::optional<qreal> fontSize;
    std::optional<QColor> textColor;
    bool operator==(const Relationship &) const = default;
};

namespace Relationships {

// Defaults (measured 2026-10-24 on the reference): dashed Thin line, label 13 px Demibold. The line color is
// the one used before FEAT-053 (NodeStyle::annotationColor adapts it to dark backdrops).
inline constexpr qreal kDefaultLineWidth = 2;
inline constexpr int kDefaultLinePattern = 2;  // Qt::DashLine
inline constexpr qreal kDefaultFontSize = 13;
inline constexpr BranchEnd kDefaultStartMark = BranchEnd::None;
inline constexpr BranchEnd kDefaultEndMark = BranchEnd::FilledArrow;
inline const QColor kDefaultLineColor{0x7c, 0x87, 0x96};
// Automatic control points: each at this share of the distance between the ends, turned this many
// degrees from the line joining them, both the same way (an S).
inline constexpr qreal kControlShare = 0.36;
inline constexpr qreal kControlAngleDeg = 28;

QString shapeKey(Relationship::Shape shape);
std::optional<Relationship::Shape> shapeFromKey(const QString &key);

QJsonObject toJson(const Relationship &relationship);
// nullopt when the object has no id or no ends (unknown values fall back to the defaults).
std::optional<Relationship> fromJson(const QJsonObject &object);
QJsonArray toJson(const QList<Relationship> &relationships);
// Entries that are not valid objects are skipped; old "[from, to]" pairs are not read (no
// backward compatibility during development, user rule).
QList<Relationship> listFromJson(const QJsonArray &array);

// Outline and centre of an end element, in scene coordinates.
struct End {
    QPainterPath outline;
    QPointF centre;
};

struct Geometry {
    QPainterPath path;        // the line, from the start end to the target end
    QPointF fromCentre, toCentre;  // centres of the end elements (control offsets start there)
    QPointF start, end;       // where the line meets each outline
    QPointF fromControl, toControl;  // absolute control points (handles)
    QPointF labelPoint;       // middle of the line
    QPointF startDirection;   // unit vector pointing out of the line at `start` (for its mark)
    QPointF endDirection;     // unit vector of the line arriving at `end` (for the arrow)
};

// Line of the relationship between the two ends. Each end sits on its element's outline, on the
// ray from the element's centre towards the other centre, or towards its handle once moved.
Geometry geometry(const Relationship &relationship, const End &from, const End &to);

// Point where the ray from `centre` towards `towards` leaves `outline` (centre if it is not inside).
QPointF outlineExit(const QPainterPath &outline, const QPointF &centre, const QPointF &towards);

// How a relationship is drawn, its unset values filled with the defaults above for the sheet's
// backdrop and font; shared by the desktop's scene and the phone's (FEAT-110).
struct Look {
    QColor lineColor;
    qreal lineWidth = kDefaultLineWidth;
    int linePattern = kDefaultLinePattern;  // Qt::PenStyle
    BranchEnd startMark = kDefaultStartMark, endMark = kDefaultEndMark;
    QFont font;
    QColor textColor;
    QColor backdrop;  // behind the label, so the line does not cross the text
};
Look look(const Relationship &relationship, const NodeStyle::Backdrop &backdrop, const QString &sheetFontFamily);

// The label's box: the text's size with kLabelPaddingPx around it, centred on `labelPoint`.
inline constexpr qreal kLabelPaddingPx = 3;
inline constexpr qreal kLabelRadiusPx = 2;
// A selected relationship: a band of the selection colour at this alpha, kSelectionExtraPx wider
// than the line; a curved or elbow one adds its two control handles of kHandleRadiusPx
// (RelationshipItem::paint).
inline constexpr int kSelectionAlpha = 110;
inline constexpr qreal kSelectionExtraPx = 4;
inline constexpr qreal kHandleRadiusPx = 4;
inline constexpr qreal kPlaceholderAlpha = 0.45;  // the empty label's placeholder text
QSizeF labelSize(const QFont &font, const QString &text);
inline QRectF labelRect(const QPointF &labelPoint, const QSizeF &size) {
    return QRectF(labelPoint - QPointF(size.width() / 2, size.height() / 2), size);
}

}  // namespace Relationships
