#pragma once
// FEAT-056 (docs/features/FEAT-056-comandi-di-selezione.md): what each "Select" command of the topic
// menu selects. Model only; the scene keeps the visible ones (MindMapScene::selectTargets).
#include <QList>
#include <QStringList>

class MindMapModel;
struct MindNode;

namespace SelectionCommands {

enum class Kind { Subtopics, Siblings, SameLevel, Floating, Relationships, Summaries, Boundaries, Images, Equations };

struct Targets {
    QList<MindNode *> nodes;
    QStringList relations;
    QStringList boundaries;
    bool isEmpty() const { return nodes.isEmpty() && relations.isEmpty() && boundaries.isEmpty(); }
};

// `selected`: the selected topics (Subtopics, Siblings and Same level start from them, union over
// all). Topics hidden in collapsed branches are left out.
Targets targets(Kind kind, const MindMapModel &model, const QList<MindNode *> &selected);

}  // namespace SelectionCommands
