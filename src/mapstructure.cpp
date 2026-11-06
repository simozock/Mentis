#include "mapstructure.h"

#include "mindmapmodel.h"

#include <cmath>

namespace MapStructure {

namespace {
struct TypeInfo { Type type; const char *key; QList<Direction> directions; };
const QList<TypeInfo> &types() {
    static const QList<TypeInfo> list{
        {Type::MindMap, "mindmap", {Direction::BothSides, Direction::Right, Direction::Left}},
        {Type::LogicChart, "logic", {Direction::Right, Direction::Left, Direction::BothSides}},
        {Type::OrgChart, "org", {Direction::Down, Direction::Up, Direction::BothSides}},
        {Type::TreeChart, "tree", {Direction::Right, Direction::Left, Direction::BothSides}},
        {Type::Brace, "brace", {Direction::Right, Direction::Left}},
        {Type::Timeline, "timeline", {Direction::Right, Direction::OffAxis}},
        {Type::Fishbone, "fishbone", {Direction::Right, Direction::Left}},
        {Type::TreeTable, "treetable", {Direction::Down}},
    };
    return list;
}
const TypeInfo &info(Type type) {
    for (const TypeInfo &item : types())
        if (item.type == type) return item;
    return types().front();
}
}  // namespace

QString typeKey(Type type) { return QString::fromLatin1(info(type).key); }

Type typeFromKey(const QString &key) {
    for (const TypeInfo &item : types())
        if (key == QLatin1String(item.key)) return item.type;
    return Type::MindMap;
}

QString directionKey(Direction direction) {
    switch (direction) {
    case Direction::BothSides: return QStringLiteral("both");
    case Direction::Right: return QStringLiteral("right");
    case Direction::Left: return QStringLiteral("left");
    case Direction::Down: return QStringLiteral("down");
    case Direction::Up: return QStringLiteral("up");
    case Direction::OffAxis: return QStringLiteral("offaxis");
    }
    return QStringLiteral("both");
}

Direction directionFromKey(const QString &key, Type type) {
    for (const Direction direction : info(type).directions)
        if (directionKey(direction) == key) return direction;
    return info(type).directions.front();
}

QList<Direction> directions(Type type) { return info(type).directions; }

bool isTwoSided(Direction direction) { return direction == Direction::BothSides; }

bool isBlockType(Type type) { return type == Type::OrgChart || type == Type::TreeChart; }

bool laysOutAsBlock(Type type) {
    return isBlockType(type) || type == Type::Timeline || type == Type::Fishbone || type == Type::TreeTable;
}

Type typeOf(const MindNode *node) {
    for (const MindNode *at = node; at; at = at->parent) {
        if (!at->structure.isEmpty()) return typeFromKey(at->structure);
        if (at->summaryTopic) return Type::LogicChart;  // FEAT-052: its branch grows away from the bracket
    }
    return Type::MindMap;
}

bool inTreeTable(const MindNode *node) {
    for (const MindNode *at = node ? node->parent : nullptr; at; at = at->parent) {
        if (at->structure.isEmpty()) continue;
        const Type type = typeFromKey(at->structure);
        if (type != Type::MindMap) return type == Type::TreeTable;
    }
    return false;
}

bool insideTreeTable(const MindNode *node) {
    for (const MindNode *at = node ? node->parent : nullptr; at; at = at->parent)
        if (typeFromKey(at->structure) == Type::TreeTable) return true;
    return false;
}

Direction directionOf(const MindNode *node) {
    for (const MindNode *at = node; at; at = at->parent) {
        if (!at->structure.isEmpty()) return directionFromKey(at->structureDirection, typeFromKey(at->structure));
        if (at->summaryTopic) return Direction::Right;  // FEAT-052: see typeOf
    }
    return Direction::BothSides;
}

QList<int> balancedSides(const QList<double> &weights, const QList<int> &fixed) {
    QList<int> sides(weights.size(), 0);
    double first = 0, second = 0;
    int firstCount = 0, secondCount = 0;
    QList<int> automatic;
    for (int i = 0; i < weights.size(); ++i) {
        const int side = i < fixed.size() ? fixed[i] : 0;
        if (side > 0) { sides[i] = 1; first += weights[i]; ++firstCount; }
        else if (side < 0) { sides[i] = -1; second += weights[i]; ++secondCount; }
        else automatic.append(i);
    }
    // k = quanti rami automatici vanno sul primo lato (i primi k nell'ordine). prefixes[k] = loro peso.
    QList<double> prefixes{0};
    for (const int i : std::as_const(automatic)) prefixes.append(prefixes.back() + weights[i]);
    const int count = int(automatic.size());
    const double total = prefixes.back();
    const auto difference = [&](int k) { return std::abs((first + prefixes[k]) - (second + total - prefixes[k])); };
    // Divisione per numero: meta' dei rami sul primo lato (dispari: uno in piu'), ma due rami stanno
    // entrambi sul primo (foto di riferimento 2026-10-21: con due rami principali sono tutti e due a destra).
    const int all = firstCount + secondCount + count;
    const int wanted = std::max((all + 1) / 2, std::min(all, 2));
    int best = 0;
    for (int k = 1; k <= count; ++k)
        if (std::abs(firstCount + k - wanted) <= std::abs(firstCount + best - wanted)) best = k;
    // FEAT-072 (reference photos 2026-10-21/23): while the sides differ by kRebalanceRows rows or more, the
    // topic at the split moves to the lighter side, one at a time, as long as that narrows the gap.
    // 12+1+1+1 stays 2/2 (13 against 2), 13+1+1+1 becomes 1/3, 13+1+1+1+1+1 becomes 2/4 (before: a
    // ratio of 7 between the sides, which kept 3/3 there with 15 rows against 3).
    const bool bothSides = firstCount + best > 0 && secondCount + count - best > 0;
    while (bothSides && difference(best) >= kRebalanceRows) {
        const double side = (first + prefixes[best]) - (second + total - prefixes[best]);
        const int next = side > 0 ? best - 1 : best + 1;
        if (next < 0 || next > count || difference(next) >= difference(best)) break;
        best = next;
    }
    for (int j = 0; j < count; ++j) sides[automatic[j]] = j < best ? 1 : -1;
    return sides;
}

}  // namespace MapStructure
