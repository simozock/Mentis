#pragma once

// FEAT-052 (docs/features/FEAT-052-confini.md, "Summaries"): a summary over a run of sibling
// topics: a square bracket and a summary topic. Data and geometry; no scene items.

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QList>
#include <QSizeF>
#include <QStringList>

#include <memory>

struct MindNode;

struct Summary {
    QString id;
    QStringList topics;  // the covered topics (with their branches), siblings first to last
    // The summary topic: a topic of the sheet whose parent is the covered topics' parent, but not one
    // of its children (outside the layout and the outliner). Shared so the scene can keep its item's
    // node alive until it drops the item.
    std::shared_ptr<MindNode> topic;
};

namespace Summaries {

// Default bracket and summary topic (measured live 2026-10-24 on the reference, px at 100%).
inline constexpr qreal kGapPx = 8;          // farthest covered box to the arms
inline constexpr qreal kOverhangPx = 2;     // arms past the first and the last box
inline constexpr qreal kArmPx = 11;         // arm, to the outer edge of the bracket's line
inline constexpr qreal kTickPx = 9;         // middle tick, beyond the line
inline constexpr qreal kTopicGapPx = 8;     // tick to the summary topic
inline constexpr qreal kLineWidth = 2;
inline constexpr qreal kTopicFontSizePt = 9;  // 12 px
inline constexpr QPointF kTopicPadding{10, 4};
inline constexpr qreal kTopicRadiusPx = 4;

// Where the bracket stands: on the run's outer side, away from the parent (checked live 2026-10-25:
// beside a column of siblings, below or above a row of them, as in org charts, timelines, fishbones).
enum class Side { Right, Left, Down, Up };

// The side for a run: `centres` are its topics' centres (or all the siblings' when it has one topic);
// they form a row when they spread more across than down (or `row` forces it, a fishbone's main
// topics), else a column; one centre alone is a row when it is more below or above its parent.
Side sideFor(const QList<QPointF> &centres, const QRectF &covered, const QPointF &parentCentre, bool row = false);

// Bracket beside `covered` (the union of the covered branches' boxes).
QPainterPath bracket(const QRectF &covered, Side side);
// The summary topic's near edge and its centre along the run (x, y; or y, x below and above).
QPointF topicAnchor(const QRectF &covered, Side side);
// Bracket and summary topic (`topicSize`) together: the room a layout keeps for them.
QRectF extent(const QRectF &covered, Side side, const QSizeF &topicSize);

}  // namespace Summaries
