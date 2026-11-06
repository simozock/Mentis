#include "selectioncommands.h"

#include "mindmapmodel.h"

#include <QSet>
#include <functional>

namespace SelectionCommands {

namespace {

// Hidden in the map: below a collapsed topic (a summary topic follows the topics it covers' parent).
bool hidden(const MindNode *node) {
    for (const MindNode *up = node->parent; up; up = up->parent)
        if (up->collapsed) return true;
    return false;
}

// Distance from the root of the node's hierarchy (central topic, or its floating topic), and which
// hierarchy: the map's (false) or the floating branches' (true).
std::pair<int, bool> level(const MindNode *node) {
    int depth = 0;
    for (const MindNode *at = node; at->parent; at = at->parent) {
        if (at->floating) return {depth, true};
        ++depth;
    }
    return {depth, false};
}

// Every topic of the sheet: the tree, then the summary topics.
void forEachTopic(const MindMapModel &model, const std::function<void(MindNode *)> &visit) {
    const std::function<void(MindNode *)> walk = [&](MindNode *node) {
        visit(node);
        for (const auto &child : node->children) walk(child.get());
    };
    walk(model.root());
    for (const Summary &summary : model.summaries)
        if (summary.topic) visit(summary.topic.get());
}

}  // namespace

Targets targets(Kind kind, const MindMapModel &model, const QList<MindNode *> &selected) {
    Targets result;
    QSet<const MindNode *> added;
    const auto add = [&](MindNode *node) {
        if (node && !hidden(node) && !added.contains(node)) {
            added.insert(node);
            result.nodes.append(node);
        }
    };
    switch (kind) {
    case Kind::Subtopics: {
        const std::function<void(MindNode *)> below = [&](MindNode *node) {
            for (const auto &child : node->children) {
                // A floating topic is a child of the central one but not one of its subtopics.
                if (child->floating && !node->parent) continue;
                add(child.get());
                below(child.get());
            }
        };
        for (MindNode *node : selected) below(node);
        break;
    }
    case Kind::Siblings:
        for (MindNode *node : selected) {
            // The central and summary topics have no siblings: the topic itself.
            if (!node->parent || node->summaryTopic) { add(node); continue; }
            for (const auto &child : node->parent->children)
                if (child->floating == node->floating || node->parent->parent) add(child.get());
        }
        break;
    case Kind::SameLevel: {
        QSet<std::pair<int, bool>> levels;
        for (const MindNode *node : selected) levels.insert(level(node));
        forEachTopic(model, [&](MindNode *node) { if (levels.contains(level(node))) add(node); });
        break;
    }
    case Kind::Floating:
        for (const auto &child : model.root()->children)
            if (child->floating) add(child.get());
        break;
    case Kind::Relationships:
        for (const Relationship &relation : model.relations) result.relations.append(relation.id);
        break;
    case Kind::Summaries:
        for (const Summary &summary : model.summaries) add(summary.topic.get());
        break;
    case Kind::Boundaries:
        for (const Boundary &boundary : model.boundaries) result.boundaries.append(boundary.id);
        break;
    case Kind::Images:
        forEachTopic(model, [&](MindNode *node) { if (!node->imagePath.isEmpty()) add(node); });
        break;
    case Kind::Equations:
        forEachTopic(model, [&](MindNode *node) { if (!node->formula.isEmpty()) add(node); });
        break;
    }
    return result;
}

}  // namespace SelectionCommands
