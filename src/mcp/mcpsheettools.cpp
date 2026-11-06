// FEAT-045 phase 3, stage C: the agents' tools on sheets (delete with the user's confirmation,
// duplicate, move) and on the map's structure (type and direction, balance, automatic layout,
// folded branches).
#include "mcptoolsupport.h"

#include "document.h"
#include "mapstructure.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"

#include <QJsonArray>

namespace McpMapTools::Support {
namespace {

QStringList structureKeys() {
    using MapStructure::Type;
    QStringList keys;
    for (Type type : {Type::MindMap, Type::LogicChart, Type::OrgChart, Type::TreeChart, Type::Brace, Type::Timeline,
                      Type::Fishbone, Type::TreeTable})
        keys.append(MapStructure::typeKey(type));
    return keys;
}

QStringList directionKeys() {
    using MapStructure::Direction;
    QStringList keys;
    for (Direction direction : {Direction::BothSides, Direction::Right, Direction::Left, Direction::Down, Direction::Up, Direction::OffAxis})
        keys.append(MapStructure::directionKey(direction));
    return keys;
}

const QJsonObject kSheetRequired = stringProperty(QStringLiteral("Sheet id from read_map"));

}  // namespace

QJsonObject structureOptions() {
    return {{"structure", QJsonArray::fromStringList(structureKeys())}, {"direction", QJsonArray::fromStringList(directionKeys())}};
}

void appendSheetEdits(QList<MapEdit> &edits) {
    edits.append({{QStringLiteral("delete_sheet"), QStringLiteral("Delete a sheet"),
                   QStringLiteral("Deletes a sheet with all its topics, after the user's explicit confirmation: Mentis asks when it is "
                                  "open; otherwise ask the user in the chat and pass confirm: true. The last sheet stays."),
                   schema({{"sheet", kSheetRequired}, {"confirm", kConfirm}}, {QStringLiteral("sheet")}), false, {}},
                  true,
                  [](const Access &access, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &) {
                      if (document.sheets().size() < 2) return ToolResult::error(QStringLiteral("The last sheet cannot be deleted"));
                      const int index = document.currentSheet();  // the requested sheet, shown by the caller
                      const QString id = document.sheets()[index].id;
                      const QString name = document.sheets()[index].name;
                      QString error;
                      if (!confirmed(access, args, QStringLiteral("delete_sheet"), name, &error)) return ToolResult::error(error);
                      if (const Document::Result removed = document.removeSheet(index); !removed.ok) return ToolResult::error(removed.error);
                      return ToolResult::object({{"deleted", id}, {"sheet", document.currentSheetId()}});
                  }});

    edits.append({{QStringLiteral("duplicate_sheet"), QStringLiteral("Duplicate a sheet"),
                   QStringLiteral("Copies a sheet right after it, named \"<name> (copy)\", with the same content and new ids."),
                   schema({{"sheet", kSheet}}, {}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &, Document &document, MindMapScene &, MindMapModel &) {
                      if (const Document::Result copied = document.duplicateSheet(document.currentSheet()); !copied.ok)
                          return ToolResult::error(copied.error);
                      return ToolResult::object({{"sheet", document.currentSheetId()}});
                  }});

    edits.append({{QStringLiteral("move_sheet"), QStringLiteral("Move a sheet"),
                   QStringLiteral("Moves a sheet to position index among the sheets (0 = first)."),
                   schema({{"sheet", kSheet}, {"index", QJsonObject{{"type", "integer"}, {"minimum", 0}}}}, {QStringLiteral("index")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &) {
                      QString error;
                      const std::optional<int> to = index(args, &error);
                      if (!to) return ToolResult::error(error.isEmpty() ? QStringLiteral("Missing argument: index") : error);
                      const int target = std::min(*to, int(document.sheets().size()) - 1);
                      document.moveSheet(document.currentSheet(), target);
                      return ToolResult::object({{"sheet", document.currentSheetId()}, {"index", document.currentSheet()}});
                  }});

    edits.append({{QStringLiteral("set_structure"), QStringLiteral("Set the map type"),
                   QStringLiteral("Sets the type and direction of the whole sheet (id = the central topic) or of one branch "
                                  "(empty structure = as its parent). list_options lists the values. Moves done by hand in it are lost."),
                   schema({{"sheet", kSheet}, {"id", stringProperty(QStringLiteral("The central topic or a branch"))},
                           {"structure", QJsonObject{{"type", "string"}, {"description", "A structure from list_options; empty = as the parent"}}},
                           {"direction", stringProperty(QStringLiteral("A direction from list_options; omitted = the first of the type"))}},
                          {QStringLiteral("id"), QStringLiteral("structure")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      MindNode *node = topic(model, args, QStringLiteral("id"), &error);
                      const std::optional<QString> type = node ? text(args, QStringLiteral("structure"), true, &error, 50) : std::nullopt;
                      const std::optional<QString> direction = type ? text(args, QStringLiteral("direction"), false, &error, 50) : std::nullopt;
                      if (!type || !error.isEmpty()) return ToolResult::error(error);
                      if (!type->isEmpty() && !structureKeys().contains(*type))
                          return ToolResult::error(QStringLiteral("Unknown structure %1 (list_options lists them)").arg(*type));
                      if (direction && !direction->isEmpty() && !directionKeys().contains(*direction))
                          return ToolResult::error(QStringLiteral("Unknown direction %1 (list_options lists them)").arg(*direction));
                      if (!node->parent && type->isEmpty()) return ToolResult::error(QStringLiteral("The central topic needs a structure"));
                      document.setStructure(node, *type, direction.value_or(QString()));
                      return ToolResult::object({{"id", node->id}});
                  }});

    edits.append({{QStringLiteral("set_balanced"), QStringLiteral("Balance the map"),
                   QStringLiteral("Turns the automatic balance of the main branches on or off (on: moves done by hand are dropped)."),
                   schema({{"sheet", kSheet}, {"balanced", QJsonObject{{"type", "boolean"}}}}, {QStringLiteral("balanced")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &scene, MindMapModel &model) {
                      if (!args.value(QStringLiteral("balanced")).isBool()) return ToolResult::error(QStringLiteral("balanced must be true or false"));
                      const bool on = args.value(QStringLiteral("balanced")).toBool();
                      // Turning it off keeps each main branch on the side it has now (MainWindow::setBalanced).
                      QHash<const MindNode *, int> sides;
                      for (const auto &child : model.root()->children) sides.insert(child.get(), scene.sideOf(child.get()));
                      document.setBalanced(on, sides);
                      return ToolResult::object({{"balanced", model.balanced}});
                  }});

    edits.append({{QStringLiteral("auto_layout"), QStringLiteral("Lay the map out again"),
                   QStringLiteral("Forgets the moves done by hand and lays the sheet out automatically."),
                   schema({{"sheet", kSheet}}, {}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &, Document &document, MindMapScene &, MindMapModel &model) {
                      model.clearManualOffsets();
                      document.markChanged();
                      return ToolResult::object({{"sheet", document.currentSheetId()}});
                  }});

    edits.append({{QStringLiteral("set_collapsed"), QStringLiteral("Fold or unfold branches"),
                   QStringLiteral("Folds (collapsed: true) or unfolds the children of topics on the map."),
                   schema({{"sheet", kSheet}, {"ids", idsProperty(QStringLiteral("Topics"))}, {"collapsed", QJsonObject{{"type", "boolean"}}}},
                          {QStringLiteral("ids"), QStringLiteral("collapsed")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      const QList<MindNode *> nodes = topics(model, args, QStringLiteral("ids"), &error);
                      if (nodes.isEmpty()) return ToolResult::error(error);
                      if (!args.value(QStringLiteral("collapsed")).isBool()) return ToolResult::error(QStringLiteral("collapsed must be true or false"));
                      int changed = 0;
                      for (MindNode *node : nodes) changed += document.setCollapsed(node, args.value(QStringLiteral("collapsed")).toBool()) ? 1 : 0;
                      return ToolResult::object({{"changed", changed}});
                  }});
}

}  // namespace McpMapTools::Support
