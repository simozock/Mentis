// FEAT-045 phase 3, stage B: the agents' tools on the elements of a map besides the tree:
// relationships, summaries, boundaries and floating topics. read_map lists them with their ids.
#include "mcptoolsupport.h"

#include "boundary.h"
#include "document.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "nodestyle.h"
#include "relationship.h"
#include "summary.h"

#include <QJsonArray>

namespace McpMapTools::Support {
namespace {

std::optional<Relationship::Shape> relationShape(const QString &key) {
    if (key == QLatin1String("curved")) return Relationship::Shape::Curved;
    if (key == QLatin1String("straight")) return Relationship::Shape::Straight;
    if (key == QLatin1String("elbow")) return Relationship::Shape::Elbow;
    return std::nullopt;
}

QString relationShapeKey(Relationship::Shape shape) {
    switch (shape) {
    case Relationship::Shape::Straight: return QStringLiteral("straight");
    case Relationship::Shape::Elbow: return QStringLiteral("elbow");
    case Relationship::Shape::Curved: break;
    }
    return QStringLiteral("curved");
}

// A nullable [x, y] offset; false with error set for a wrong value.
bool readControl(const QJsonObject &args, const QString &key, std::optional<QPointF> *control, QString *error) {
    if (!args.contains(key)) return true;
    const QJsonValue value = args.value(key);
    if (value.isNull()) { control->reset(); return true; }
    const QJsonArray point = value.toArray();
    if (point.size() != 2 || !point[0].isDouble() || !point[1].isDouble() || qAbs(point[0].toDouble()) > 100000 || qAbs(point[1].toDouble()) > 100000) {
        *error = QStringLiteral("%1 must be [x, y] (pixels from the centre of that end) or null").arg(key);
        return false;
    }
    *control = QPointF(point[0].toDouble(), point[1].toDouble());
    return true;
}

// A nullable colour.
bool readColor(const QJsonObject &args, const QString &key, std::optional<QColor> *color, QString *error) {
    if (!args.contains(key)) return true;
    const QJsonValue value = args.value(key);
    if (value.isNull()) { color->reset(); return true; }
    const QColor parsed(value.toString());
    if (!value.isString() || !parsed.isValid()) { *error = QStringLiteral("%1 must be a colour like #2563eb or null").arg(key); return false; }
    *color = parsed;
    return true;
}

// A nullable number in [low, high].
bool readNumber(const QJsonObject &args, const QString &key, qreal low, qreal high, std::optional<qreal> *number, QString *error) {
    if (!args.contains(key)) return true;
    const QJsonValue value = args.value(key);
    if (value.isNull()) { number->reset(); return true; }
    if (!value.isDouble() || value.toDouble() < low || value.toDouble() > high) {
        *error = QStringLiteral("%1 must be %2 to %3 or null").arg(key).arg(low).arg(high);
        return false;
    }
    *number = value.toDouble();
    return true;
}

// A nullable end mark.
bool readMark(const QJsonObject &args, const QString &key, std::optional<BranchEnd> *mark, QString *error) {
    if (!args.contains(key)) return true;
    const QJsonValue value = args.value(key);
    if (value.isNull()) { mark->reset(); return true; }
    const std::optional<BranchEnd> parsed = NodeStyle::branchEndFromKey(value.toString());
    if (!parsed) { *error = QStringLiteral("%1 must be none, openArrow, filledArrow, dot, square, diamond or null").arg(key); return false; }
    *mark = parsed;
    return true;
}

QJsonObject nullable(const QString &type, const QString &description) {
    return {{"type", QJsonArray{type, "null"}}, {"description", description}};
}

const Summary *summaryWithId(const MindMapModel &model, const QString &id) {
    for (const Summary &summary : model.summaries)
        if (summary.id == id) return &summary;
    return nullptr;
}

}  // namespace

QJsonObject elementsJson(const MindMapModel &model) {
    QJsonArray relations, summaries, boundaries;
    for (const Relationship &relation : model.relations) {
        // Every value set by hand (curve offsets, style), as in the file; the shape always.
        QJsonObject json = Relationships::toJson(relation);
        json.insert(QStringLiteral("shape"), relationShapeKey(relation.shape));
        relations.append(json);
    }
    for (const Summary &summary : model.summaries)
        summaries.append(QJsonObject{{"id", summary.id}, {"topics", QJsonArray::fromStringList(summary.topics)},
                                     {"topicId", summary.topic ? summary.topic->id : QString()},
                                     {"title", summary.topic ? summary.topic->title : QString()}});
    for (const Boundary &boundary : model.boundaries) {
        QJsonObject json{{"id", boundary.id}, {"topics", QJsonArray::fromStringList(boundary.topics)}};
        if (!boundary.title.isEmpty()) json.insert(QStringLiteral("title"), boundary.title);
        boundaries.append(json);
    }
    return {{"relationships", relations}, {"summaries", summaries}, {"boundaries", boundaries}};
}

void appendElementEdits(QList<MapEdit> &edits) {
    edits.append({{QStringLiteral("add_relationship"), QStringLiteral("Add a relationship"),
                   QStringLiteral("Draws a relationship line between two topics (or boundaries) of the sheet, with an optional label."),
                   schema({{"sheet", kSheet}, {"from", stringProperty(QStringLiteral("Id of the first topic"))},
                           {"to", stringProperty(QStringLiteral("Id of the second topic"))},
                           {"label", stringProperty(QStringLiteral("Text on the line"))},
                           {"shape", QJsonObject{{"enum", QJsonArray{"curved", "straight", "elbow"}}}}},
                          {QStringLiteral("from"), QStringLiteral("to")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &) {
                      QString error;
                      const std::optional<QString> from = text(args, QStringLiteral("from"), true, &error, 200);
                      const std::optional<QString> to = from ? text(args, QStringLiteral("to"), true, &error, 200) : std::nullopt;
                      const std::optional<QString> label = to ? text(args, QStringLiteral("label"), false, &error, 1000) : std::nullopt;
                      const std::optional<QString> shapeKey = error.isEmpty() ? text(args, QStringLiteral("shape"), false, &error, 20) : std::nullopt;
                      if (!to || !error.isEmpty()) return ToolResult::error(error);
                      const std::optional<Relationship::Shape> shape = shapeKey ? relationShape(*shapeKey) : std::optional(Relationship::Shape::Curved);
                      if (!shape) return ToolResult::error(QStringLiteral("shape must be curved, straight or elbow"));
                      const QString id = document.addRelation(*from, *to);
                      if (id.isEmpty()) return ToolResult::error(QStringLiteral("No relationship: the ends must be two different topics or boundaries of this sheet"));
                      if ((label && !label->isEmpty()) || *shape != Relationship::Shape::Curved) {
                          Relationship changed = *document.relation(id);
                          if (label) changed.label = *label;
                          changed.shape = *shape;
                          document.updateRelations({changed});
                      }
                      return ToolResult::object({{"id", id}});
                  }});

    edits.append({{QStringLiteral("update_relationship"), QStringLiteral("Change a relationship"),
                   QStringLiteral("Changes a relationship by its id from read_map: its ends (from, to: topics or boundaries of "
                                  "the sheet), label, shape, curve and style. Omitted fields stay; null puts back the automatic "
                                  "curve or the default style. from_control and to_control are the curve's handles: [x, y] "
                                  "pixels from the centre of that end (y grows downwards). Moving an end makes its handle "
                                  "automatic unless given."),
                   schema({{"sheet", kSheet}, {"id", stringProperty(QStringLiteral("Id of the relationship"))},
                           {"from", stringProperty(QStringLiteral("Id of the new first end"))},
                           {"to", stringProperty(QStringLiteral("Id of the new second end"))},
                           {"label", stringProperty(QStringLiteral("Text on the line; empty = none"))},
                           {"shape", QJsonObject{{"enum", QJsonArray{"curved", "straight", "elbow"}}}},
                           {"from_control", nullable(QStringLiteral("array"), QStringLiteral("[x, y] handle near the first end"))},
                           {"to_control", nullable(QStringLiteral("array"), QStringLiteral("[x, y] handle near the second end"))},
                           {"line_color", nullable(QStringLiteral("string"), QStringLiteral("e.g. #2563eb"))},
                           {"line_width", nullable(QStringLiteral("number"), QStringLiteral("0.5 to 20 pixels"))},
                           {"line_pattern", QJsonObject{{"enum", QJsonArray{"solid", "dashed", "dotted", QJsonValue()}}}},
                           {"start_mark", QJsonObject{{"enum", QJsonArray{"none", "openArrow", "filledArrow", "dot", "square", "diamond", QJsonValue()}}}},
                           {"end_mark", QJsonObject{{"enum", QJsonArray{"none", "openArrow", "filledArrow", "dot", "square", "diamond", QJsonValue()}}}},
                           {"font", stringProperty(QStringLiteral("Font family of the label; empty = the sheet's"))},
                           {"font_size", nullable(QStringLiteral("number"), QStringLiteral("6 to 200 pixels"))},
                           {"text_color", nullable(QStringLiteral("string"), QStringLiteral("Colour of the label"))}},
                          {QStringLiteral("id")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &) {
                      const QString id = args.value(QStringLiteral("id")).toString();
                      const Relationship *relation = document.relation(id);
                      if (!relation) return ToolResult::error(QStringLiteral("No relationship %1 (read_map lists them)").arg(id));
                      Relationship changed = *relation;
                      QString error;
                      const std::optional<QString> from = text(args, QStringLiteral("from"), false, &error, 200);
                      const std::optional<QString> to = error.isEmpty() ? text(args, QStringLiteral("to"), false, &error, 200) : std::nullopt;
                      const std::optional<QString> label = error.isEmpty() ? text(args, QStringLiteral("label"), false, &error, 1000) : std::nullopt;
                      const std::optional<QString> font = error.isEmpty() ? text(args, QStringLiteral("font"), false, &error, 200) : std::nullopt;
                      if (!error.isEmpty()) return ToolResult::error(error);
                      if (from && *from != changed.from) { changed.from = *from; changed.fromControl.reset(); }
                      if (to && *to != changed.to) { changed.to = *to; changed.toControl.reset(); }
                      if (label) changed.label = *label;
                      if (font) changed.fontFamily = font->trimmed();
                      if (args.contains(QStringLiteral("shape"))) {
                          const std::optional<Relationship::Shape> shape = relationShape(args.value(QStringLiteral("shape")).toString());
                          if (!shape) return ToolResult::error(QStringLiteral("shape must be curved, straight or elbow"));
                          changed.shape = *shape;
                      }
                      if (args.contains(QStringLiteral("line_pattern"))) {
                          const QString pattern = args.value(QStringLiteral("line_pattern")).toString();
                          if (args.value(QStringLiteral("line_pattern")).isNull()) changed.linePattern.reset();
                          else if (pattern == QLatin1String("solid")) changed.linePattern = int(Qt::SolidLine);
                          else if (pattern == QLatin1String("dashed")) changed.linePattern = int(Qt::DashLine);
                          else if (pattern == QLatin1String("dotted")) changed.linePattern = int(Qt::DotLine);
                          else return ToolResult::error(QStringLiteral("line_pattern must be solid, dashed, dotted or null"));
                      }
                      if (!readControl(args, QStringLiteral("from_control"), &changed.fromControl, &error)
                          || !readControl(args, QStringLiteral("to_control"), &changed.toControl, &error)
                          || !readColor(args, QStringLiteral("line_color"), &changed.lineColor, &error)
                          || !readColor(args, QStringLiteral("text_color"), &changed.textColor, &error)
                          || !readNumber(args, QStringLiteral("line_width"), 0.5, 20, &changed.lineWidth, &error)
                          || !readNumber(args, QStringLiteral("font_size"), 6, 200, &changed.fontSize, &error)
                          || !readMark(args, QStringLiteral("start_mark"), &changed.startMark, &error)
                          || !readMark(args, QStringLiteral("end_mark"), &changed.endMark, &error))
                          return ToolResult::error(error);
                      if (changed == *relation) return ToolResult::error(QStringLiteral("Nothing changed"));
                      if (!document.updateRelations({changed}))
                          return ToolResult::error(QStringLiteral("Refused: the ends must be two different topics or boundaries of this sheet"));
                      return ToolResult::object(Relationships::toJson(*document.relation(id)));
                  }});

    edits.append({{QStringLiteral("add_summary"), QStringLiteral("Add a summary"),
                   QStringLiteral("Adds a summary (a brace with its own topic) over sibling topics, with an optional title. "
                                  "Topics of different parents get one summary each."),
                   schema({{"sheet", kSheet}, {"ids", idsProperty(QStringLiteral("Sibling topics to summarise"))},
                           {"title", stringProperty(QStringLiteral("Text of the summary topic"))}},
                          {QStringLiteral("ids")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &scene, MindMapModel &model) {
                      QString error;
                      const QList<MindNode *> nodes = topics(model, args, QStringLiteral("ids"), &error);
                      const std::optional<QString> title = nodes.isEmpty() ? std::nullopt : text(args, QStringLiteral("title"), false, &error);
                      if (nodes.isEmpty() || !error.isEmpty()) return ToolResult::error(error);
                      const QList<MindNode *> created = document.addSummaries(nodes, [&scene](const MindNode *node) { return scene.sideOf(node); });
                      if (created.isEmpty()) return ToolResult::error(QStringLiteral("No summary: the topics already have one, or the map type has none"));
                      QJsonArray ids, topicIds;
                      for (MindNode *topic : created) {
                          if (title && !title->isEmpty()) document.setTitle(topic, *title);
                          topicIds.append(topic->id);
                      }
                      for (const Summary &summary : model.summaries)
                          for (const MindNode *topic : created)
                              if (summary.topic.get() == topic) ids.append(summary.id);
                      return ToolResult::object({{"ids", ids}, {"topicIds", topicIds}});
                  }});

    edits.append({{QStringLiteral("add_boundary"), QStringLiteral("Add a boundary"),
                   QStringLiteral("Draws a boundary around sibling topics and their branches, with an optional title."),
                   schema({{"sheet", kSheet}, {"ids", idsProperty(QStringLiteral("Sibling topics to enclose"))},
                           {"title", stringProperty(QStringLiteral("Title of the boundary"))}},
                          {QStringLiteral("ids")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &scene, MindMapModel &model) {
                      QString error;
                      const QList<MindNode *> nodes = topics(model, args, QStringLiteral("ids"), &error);
                      const std::optional<QString> title = nodes.isEmpty() ? std::nullopt : text(args, QStringLiteral("title"), false, &error, 1000);
                      if (nodes.isEmpty() || !error.isEmpty()) return ToolResult::error(error);
                      const QStringList ids = document.addBoundaries(nodes, [&scene](const MindNode *node) { return scene.sideOf(node); });
                      if (ids.isEmpty()) return ToolResult::error(QStringLiteral("No boundary: not the central topic, and not where one already is"));
                      if (title && !title->isEmpty()) {
                          QList<Boundary> titled;
                          for (const QString &id : ids) {
                              Boundary changed = *document.boundary(id);
                              changed.title = *title;
                              titled.append(changed);
                          }
                          document.updateBoundaries(titled);
                      }
                      return ToolResult::object({{"ids", QJsonArray::fromStringList(ids)}});
                  }});

    edits.append({{QStringLiteral("update_element"), QStringLiteral("Change an element"),
                   QStringLiteral("Changes a relationship (label, shape), a summary (title) or a boundary (title) by its id from read_map. "
                                  "update_relationship changes everything else of a relationship."),
                   schema({{"sheet", kSheet}, {"id", stringProperty(QStringLiteral("Id of the relationship, summary or boundary"))},
                           {"title", stringProperty(QStringLiteral("Label or title; empty = none"))},
                           {"shape", QJsonObject{{"enum", QJsonArray{"curved", "straight", "elbow"}}, {"description", "Relationships only"}}}},
                          {QStringLiteral("id")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      const std::optional<QString> id = text(args, QStringLiteral("id"), true, &error, 200);
                      const std::optional<QString> title = id ? text(args, QStringLiteral("title"), false, &error) : std::nullopt;
                      const std::optional<QString> shapeKey = error.isEmpty() ? text(args, QStringLiteral("shape"), false, &error, 20) : std::nullopt;
                      if (!id || !error.isEmpty()) return ToolResult::error(error);
                      if (!title && !shapeKey) return ToolResult::error(QStringLiteral("Nothing to change: give title or shape"));
                      if (const Relationship *relation = document.relation(*id)) {
                          Relationship changed = *relation;
                          if (title) changed.label = *title;
                          if (shapeKey) {
                              const std::optional<Relationship::Shape> shape = relationShape(*shapeKey);
                              if (!shape) return ToolResult::error(QStringLiteral("shape must be curved, straight or elbow"));
                              changed.shape = *shape;
                          }
                          document.updateRelations({changed});
                          return ToolResult::object({{"id", *id}});
                      }
                      if (shapeKey) return ToolResult::error(QStringLiteral("shape is for relationships only"));
                      if (const Boundary *boundary = document.boundary(*id)) {
                          Boundary changed = *boundary;
                          changed.title = *title;
                          document.updateBoundaries({changed});
                          return ToolResult::object({{"id", *id}});
                      }
                      if (const Summary *summary = summaryWithId(model, *id); summary && summary->topic) {
                          document.setTitle(summary->topic.get(), *title);
                          return ToolResult::object({{"id", *id}});
                      }
                      return ToolResult::error(QStringLiteral("No relationship, summary or boundary with id %1 (read_map lists them)").arg(*id));
                  }});

    edits.append({{QStringLiteral("delete_elements"), QStringLiteral("Delete elements"),
                   QStringLiteral("Deletes relationships, summaries and boundaries by their ids (the topics stay)."),
                   schema({{"sheet", kSheet}, {"ids", idsProperty(QStringLiteral("Ids of relationships, summaries or boundaries"))}},
                          {QStringLiteral("ids")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      const QJsonArray list = args.value(QStringLiteral("ids")).toArray();
                      if (list.isEmpty()) return ToolResult::error(QStringLiteral("ids must be a non-empty array"));
                      QStringList relations, summaries, boundaries;
                      for (const QJsonValue &value : list) {
                          const QString id = value.toString();
                          if (document.relation(id)) relations.append(id);
                          else if (summaryWithId(model, id)) summaries.append(id);
                          else if (document.boundary(id)) boundaries.append(id);
                          else return ToolResult::error(QStringLiteral("No relationship, summary or boundary with id %1").arg(id));
                      }
                      if (!relations.isEmpty()) document.removeRelations(relations);
                      if (!summaries.isEmpty()) document.removeSummaries(summaries);
                      if (!boundaries.isEmpty()) document.removeBoundaries(boundaries);
                      return ToolResult::object({{"deleted", int(relations.size() + summaries.size() + boundaries.size())}});
                  }});

    edits.append({{QStringLiteral("add_floating_topic"), QStringLiteral("Add a floating topic"),
                   QStringLiteral("Adds a topic that is not attached to the tree, placed below the map; it can have children."),
                   schema({{"sheet", kSheet}, {"title", stringProperty(QStringLiteral("Text of the topic"))},
                           {"notes", stringProperty(QStringLiteral("Plain text notes"))}},
                          {QStringLiteral("title")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &scene, MindMapModel &) {
                      QString error;
                      const std::optional<QString> title = text(args, QStringLiteral("title"), true, &error);
                      const std::optional<QString> notes = title ? text(args, QStringLiteral("notes"), false, &error) : std::nullopt;
                      if (!title || !error.isEmpty()) return ToolResult::error(error);
                      // Below everything there is, so it covers nothing (the central topic is at 0, 0).
                      const QRectF map = scene.itemsBoundingRect();
                      MindNode *node = document.addFloatingTopic(QPointF(0, map.bottom() + 80), *title);
                      if (!node) return ToolResult::error(QStringLiteral("The topic could not be added"));
                      if (notes) document.setNotes(node, *notes);
                      return ToolResult::object({{"id", node->id}});
                  }});
}

}  // namespace McpMapTools::Support
