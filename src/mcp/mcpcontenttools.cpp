// FEAT-045 phase 3, stage A: the agents' tools on the style and the contents of topics. Values use
// the keys of the .mentis file (NodeStyle::*Key), listed by list_options. Each tool goes through the
// same Document and MindMapModel operations as the app, so a change is one Undo step in a window.
#include "mcptoolsupport.h"

#include "document.h"
#include "filevalidation.h"
#include "markers.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "nodeitem.h"
#include "nodelinks.h"
#include "stickers.h"

#include <QColor>
#include <QDirIterator>
#include <QJsonArray>

namespace McpMapTools::Support {
namespace {

// The keys of an enum, from its key function: 0, 1, 2... until one has no key (at most 64).
template <typename Enum, typename KeyOf>
QStringList keysOf(KeyOf keyOf) {
    QStringList keys;
    for (int value = 0; value < 64; ++value) {
        const QString key = keyOf(static_cast<Enum>(value));
        if (key.isEmpty() || keys.contains(key)) break;
        keys.append(key);
    }
    return keys;
}

QJsonObject enumProperty(const QString &description, const QStringList &keys) {
    QJsonArray values;
    for (const QString &key : keys) values.append(key);
    values.append(QJsonValue::Null);  // null = back to automatic
    return {{"enum", values}, {"description", description}};
}

QJsonObject colorProperty(const QString &description) {
    return {{"type", QJsonArray{"string", "null"}}, {"description", description + QStringLiteral(" (#rrggbb; null = automatic)")}};
}

QJsonObject numberProperty(const QString &description, double minimum, double maximum) {
    return {{"type", QJsonArray{"number", "null"}}, {"minimum", minimum}, {"maximum", maximum},
            {"description", description + QStringLiteral(" (null = automatic)")}};
}

QJsonObject boolProperty(const QString &description) {
    return {{"type", QJsonArray{"boolean", "null"}}, {"description", description + QStringLiteral(" (null = automatic)")}};
}

// The built-in stickers, as their resource paths.
QStringList builtInStickers() {
    QStringList stickers;
    QDirIterator files(QStringLiteral(":/stickers"), {QStringLiteral("*.svg"), QStringLiteral("*.png")}, QDir::Files,
                       QDirIterator::Subdirectories);
    while (files.hasNext()) stickers.append(files.next());
    stickers.sort();
    return stickers;
}

struct StyleField {
    const char *argument;
    StyleProperty property;
    enum class Kind { Color, Number, Bool, Key, Text, Bold } kind;
    double minimum = 0, maximum = 0;
    std::function<std::optional<int>(const QString &)> fromKey = {};
};

const QList<StyleField> &styleFields() {
    using Kind = StyleField::Kind;
    static const QList<StyleField> fields{
        {"fill", StyleProperty::FillColor, Kind::Color},
        {"text_color", StyleProperty::TextColor, Kind::Color},
        {"border_color", StyleProperty::BorderColor, Kind::Color},
        {"branch_color", StyleProperty::BranchColor, Kind::Color},
        {"border_width", StyleProperty::BorderWidth, Kind::Number, NodeStyle::kMinBorderWidthPx, NodeStyle::kMaxBorderWidthPx},
        {"branch_width", StyleProperty::BranchWidth, Kind::Number, NodeStyle::kMinBranchWidthPx, NodeStyle::kMaxBranchWidthPx},
        {"font_size", StyleProperty::FontSize, Kind::Number, double(NodeStyle::kMinFontSizePt), double(NodeStyle::kMaxFontSizePt)},
        {"font_family", StyleProperty::FontFamily, Kind::Text},
        {"bold", StyleProperty::FontWeight, Kind::Bold},
        {"italic", StyleProperty::Italic, Kind::Bool},
        {"underline", StyleProperty::Underline, Kind::Bool},
        {"strikethrough", StyleProperty::StrikeOut, Kind::Bool},
        {"shape", StyleProperty::Shape, Kind::Key, 0, 0,
         [](const QString &key) { const auto v = NodeStyle::shapeFromKey(key); return v ? std::optional(int(*v)) : std::nullopt; }},
        {"fill_style", StyleProperty::FillStyle, Kind::Key, 0, 0,
         [](const QString &key) { const auto v = NodeStyle::fillStyleFromKey(key); return v ? std::optional(int(*v)) : std::nullopt; }},
        {"border_style", StyleProperty::BorderStyle, Kind::Key, 0, 0,
         [](const QString &key) { const auto v = NodeStyle::borderStyleFromKey(key); return v ? std::optional(int(*v)) : std::nullopt; }},
        {"text_case", StyleProperty::TextCase, Kind::Key, 0, 0,
         [](const QString &key) { const auto v = NodeStyle::textCaseFromKey(key); return v ? std::optional(int(*v)) : std::nullopt; }},
        {"text_align", StyleProperty::TextAlign, Kind::Key, 0, 0,
         [](const QString &key) { const auto v = NodeStyle::textAlignFromKey(key); return v ? std::optional(int(*v)) : std::nullopt; }},
        {"branch_shape", StyleProperty::BranchShape, Kind::Key, 0, 0,
         [](const QString &key) { const auto v = NodeStyle::branchShapeFromKey(key); return v ? std::optional(int(*v)) : std::nullopt; }},
        {"preset", StyleProperty::Preset, Kind::Key, 0, 0,
         [](const QString &key) { const auto v = NodeStyle::presetFromKey(key); return v ? std::optional(int(*v)) : std::nullopt; }},
    };
    return fields;
}

QJsonObject styleSchema() {
    using Kind = StyleField::Kind;
    QJsonObject properties{{"ids", idsProperty(QStringLiteral("Topics to change"))}, {"sheet", kSheet}};
    const QHash<QString, QStringList> keys{
        {"shape", keysOf<NodeShape>(&NodeStyle::shapeKey)},
        {"fill_style", keysOf<NodeFillStyle>(&NodeStyle::fillStyleKey)},
        {"border_style", keysOf<NodeBorderStyle>(&NodeStyle::borderStyleKey)},
        {"text_case", keysOf<TextCase>(&NodeStyle::textCaseKey)},
        {"text_align", keysOf<TextAlign>(&NodeStyle::textAlignKey)},
        {"branch_shape", keysOf<BranchShape>(&NodeStyle::branchShapeKey)},
        {"preset", keysOf<NodePreset>(&NodeStyle::presetKey)},
    };
    for (const StyleField &field : styleFields()) {
        const QString name = QString::fromLatin1(field.argument);
        switch (field.kind) {
        case Kind::Color: properties.insert(name, colorProperty(name)); break;
        case Kind::Number: properties.insert(name, numberProperty(name, field.minimum, field.maximum)); break;
        case Kind::Bool:
        case Kind::Bold: properties.insert(name, boolProperty(name)); break;
        case Kind::Text: properties.insert(name, QJsonObject{{"type", QJsonArray{"string", "null"}}, {"description", "Font family (null = the map's)"}}); break;
        case Kind::Key: properties.insert(name, enumProperty(name, keys.value(name))); break;
        }
    }
    return schema(properties, {QStringLiteral("ids")});
}

ToolResult setStyle(const QJsonObject &args, Document &document, MindMapModel &model) {
    using Kind = StyleField::Kind;
    QString error;
    const QList<MindNode *> nodes = topics(model, args, QStringLiteral("ids"), &error);
    if (nodes.isEmpty()) return ToolResult::error(error);
    // Every value is checked before anything changes: all or nothing.
    QList<std::pair<StyleProperty, std::optional<QVariant>>> changes;
    for (const StyleField &field : styleFields()) {
        const QString name = QString::fromLatin1(field.argument);
        if (!args.contains(name)) continue;
        const QJsonValue value = args.value(name);
        if (value.isNull()) { changes.append({field.property, std::nullopt}); continue; }
        switch (field.kind) {
        case Kind::Color: {
            const QColor color(value.toString());
            if (!value.isString() || !color.isValid()) return ToolResult::error(QStringLiteral("%1 must be a colour like #2563eb").arg(name));
            changes.append({field.property, QVariant(color)});
            break;
        }
        case Kind::Number:
            if (!value.isDouble() || value.toDouble() < field.minimum || value.toDouble() > field.maximum)
                return ToolResult::error(QStringLiteral("%1 must be a number from %2 to %3").arg(name).arg(field.minimum).arg(field.maximum));
            changes.append({field.property, QVariant(value.toDouble())});
            break;
        case Kind::Bool:
            if (!value.isBool()) return ToolResult::error(QStringLiteral("%1 must be true or false").arg(name));
            changes.append({field.property, QVariant(value.toBool())});
            break;
        case Kind::Bold:
            if (!value.isBool()) return ToolResult::error(QStringLiteral("bold must be true or false"));
            changes.append({field.property, QVariant(value.toBool() ? 700 : 400)});
            break;
        case Kind::Text:
            if (!value.isString() || value.toString().size() > 200) return ToolResult::error(QStringLiteral("%1 must be a font name").arg(name));
            changes.append({field.property, QVariant(value.toString())});
            break;
        case Kind::Key: {
            const std::optional<int> key = value.isString() ? field.fromKey(value.toString()) : std::nullopt;
            if (!key) return ToolResult::error(QStringLiteral("%1: unknown value %2 (list_options lists them)").arg(name, value.toString()));
            changes.append({field.property, QVariant(*key)});
            break;
        }
        }
    }
    if (changes.isEmpty()) return ToolResult::error(QStringLiteral("Nothing to change: give at least one style property"));
    for (const auto &[property, value] : std::as_const(changes)) {
        if (value) model.setStyle(nodes, property, *value);
        else model.clearStyle(nodes, property);
    }
    document.markChanged();  // on a file nothing else records the change; in a window it joins the step
    QJsonArray ids;
    for (const MindNode *node : nodes) ids.append(node->id);
    return ToolResult::object({{"ids", ids}, {"changed", int(changes.size())}});
}

}  // namespace

namespace {

const QStringList kPlacements{QStringLiteral("above"), QStringLiteral("below"), QStringLiteral("left"), QStringLiteral("right")};  // by ImagePlacement

// The picture's place and width arguments of set_image, set_sticker, set_formula and place_visual.
void addLayoutProperties(QJsonObject &properties) {
    properties.insert(QStringLiteral("placement"), QJsonObject{{"enum", QJsonArray::fromStringList(kPlacements)},
                                                               {"description", "Where the picture sits by the title"}});
    properties.insert(QStringLiteral("width"), QJsonObject{{"type", "number"}, {"minimum", NodeItem::kMinImageWidthPx},
                                                           {"maximum", NodeItem::kMaxImageWidthPx},
                                                           {"description", "Pixels; the height keeps the proportions"}});
}

struct Layout {
    std::optional<MindNode::ImagePlacement> placement;
    std::optional<qreal> width;
};

// The layout arguments checked; nullopt with error set for a wrong value.
std::optional<Layout> layoutArguments(const QJsonObject &args, QString *error) {
    Layout layout;
    if (args.contains(QStringLiteral("placement"))) {
        const int placement = int(kPlacements.indexOf(args.value(QStringLiteral("placement")).toString()));
        if (placement < 0) { *error = QStringLiteral("placement must be one of %1").arg(kPlacements.join(QStringLiteral(", "))); return std::nullopt; }
        layout.placement = MindNode::ImagePlacement(placement);
    }
    if (args.contains(QStringLiteral("width"))) {
        const QJsonValue width = args.value(QStringLiteral("width"));
        if (!width.isDouble() || width.toDouble() < NodeItem::kMinImageWidthPx || width.toDouble() > NodeItem::kMaxImageWidthPx) {
            *error = QStringLiteral("width must be %1 to %2 pixels").arg(NodeItem::kMinImageWidthPx).arg(NodeItem::kMaxImageWidthPx);
            return std::nullopt;
        }
        layout.width = std::round(width.toDouble());
    }
    return layout;
}

// Applies the layout to the topic's picture: the width as the resize handles (proportions kept).
void applyLayout(Document &document, MindNode *node, const Layout &layout) {
    if (layout.placement) document.setImagePlacement(node, *layout.placement);
    if (layout.width && node->imageSize.width() > 0)
        document.setImageSize(node, QSizeF(*layout.width, std::max<qreal>(1, std::round(node->imageSize.height() * *layout.width / node->imageSize.width()))));
}

QJsonObject layoutJson(const MindNode *node) {
    return {{"id", node->id}, {"placement", kPlacements.value(int(node->imagePlacement))},
            {"width", node->imageSize.width()}, {"height", node->imageSize.height()}};
}

}  // namespace

void appendContentEdits(QList<MapEdit> &edits) {
    const QJsonObject idProperty = stringProperty(QStringLiteral("Id of the topic"));

    edits.append({{QStringLiteral("set_style"), QStringLiteral("Style topics"),
                   QStringLiteral("Sets the look of topics: colours, shape, border, fill, font, branch, preset (very important, "
                                  "important, crossed out). Only the given properties change; null brings one back to automatic."),
                   styleSchema(), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      return setStyle(args, document, model);
                  }});

    edits.append({{QStringLiteral("reset_style"), QStringLiteral("Reset the style"),
                   QStringLiteral("Removes the style chosen for the topics: they look as their level in the theme again."),
                   schema({{"ids", idsProperty(QStringLiteral("Topics"))}, {"sheet", kSheet}}, {QStringLiteral("ids")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      const QList<MindNode *> nodes = topics(model, args, QStringLiteral("ids"), &error);
                      if (nodes.isEmpty()) return ToolResult::error(error);
                      model.resetStyle(nodes);
                      document.markChanged();
                      return ToolResult::object({{"reset", int(nodes.size())}});
                  }});

    edits.append({{QStringLiteral("set_marker"), QStringLiteral("Set a marker"),
                   QStringLiteral("Sets a marker on topics: a built-in one (priority:1, flag:red...: list_options lists them) or "
                                  "an own one of the library (its path from list_library, kind marker). An empty marker with "
                                  "series removes that series."),
                   schema({{"ids", idsProperty(QStringLiteral("Topics"))}, {"sheet", kSheet},
                           {"series", stringProperty(QStringLiteral("Series id, e.g. priority"))},
                           {"marker", stringProperty(QStringLiteral("Marker id (priority:1) or library marker path; empty = none"))}},
                          {QStringLiteral("ids"), QStringLiteral("marker")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      const QList<MindNode *> nodes = topics(model, args, QStringLiteral("ids"), &error);
                      std::optional<QString> series = nodes.isEmpty() ? std::nullopt : text(args, QStringLiteral("series"), false, &error, 100);
                      std::optional<QString> marker = error.isEmpty() && !nodes.isEmpty() ? text(args, QStringLiteral("marker"), true, &error, 4096) : std::nullopt;
                      if (!marker) return ToolResult::error(error);
                      // A marker of the library (its path from list_library): its own series, its image kept in the map.
                      if (const auto own = libraryMarker(*marker)) {
                          model.addMarkerImage(Markers::customImage(own->first), own->second);
                          for (MindNode *node : nodes) document.setMarker(node, Markers::seriesOf(own->first), own->first);
                          return ToolResult::object({{"marker", own->first}});
                      }
                      if (!series && !marker->isEmpty()) series = Markers::seriesOf(*marker);
                      if (!series) return ToolResult::error(QStringLiteral("Missing argument: series"));
                      const bool known = std::any_of(Markers::series().cbegin(), Markers::series().cend(),
                                                     [&series](const Markers::Series &s) { return s.id == *series; });
                      if (!known) return ToolResult::error(QStringLiteral("Unknown series %1 (list_options lists them)").arg(*series));
                      if (!marker->isEmpty() && (!Markers::isValid(*marker) || Markers::seriesOf(*marker) != *series))
                          return ToolResult::error(QStringLiteral("%1 is not a marker of %2").arg(*marker, *series));
                      for (MindNode *node : nodes) document.setMarker(node, *series, *marker);
                      return ToolResult::object({{"marker", *marker}});
                  }});

    edits.append({{QStringLiteral("set_sticker"), QStringLiteral("Put a sticker"),
                   QStringLiteral("Puts a sticker of the library (built-in or own: list_library lists them) in the topic's "
                                  "picture place, instead of an image or a formula; placement and width as place_visual."),
                   [&] {
                       QJsonObject properties{{"id", idProperty}, {"sheet", kSheet}, {"sticker", stringProperty(QStringLiteral("Sticker path from list_options"))}};
                       addLayoutProperties(properties);
                       return schema(properties, {QStringLiteral("id"), QStringLiteral("sticker")});
                   }(), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      MindNode *node = topic(model, args, QStringLiteral("id"), &error);
                      const std::optional<QString> sticker = node ? text(args, QStringLiteral("sticker"), true, &error, 300) : std::nullopt;
                      const std::optional<Layout> layout = sticker ? layoutArguments(args, &error) : std::nullopt;
                      if (!layout) return ToolResult::error(error);
                      const bool builtIn = sticker->startsWith(QStringLiteral(":/stickers/")) && QFile::exists(*sticker);
                      if (!builtIn && !inStickerLibrary(*sticker))
                          return ToolResult::error(QStringLiteral("Unknown sticker %1 (list_library lists them)").arg(*sticker));
                      document.setSticker(node, *sticker, Stickers::insertionSize(*sticker));
                      applyLayout(document, node, *layout);
                      return ToolResult::object(layoutJson(node));
                  }});

    edits.append({{QStringLiteral("set_image"), QStringLiteral("Put an image"),
                   QStringLiteral("Puts an image file in the topic; it is embedded in the map when saved. "
                                  "placement and width as place_visual (omitted width = the natural size, within the topic's room)."),
                   [&] {
                       QJsonObject properties{{"id", idProperty}, {"sheet", kSheet}, {"file", stringProperty(QStringLiteral("Absolute path of a PNG, JPEG, SVG... file"))}};
                       addLayoutProperties(properties);
                       return schema(properties, {QStringLiteral("id"), QStringLiteral("file")});
                   }(), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &scene, MindMapModel &model) {
                      QString error;
                      MindNode *node = topic(model, args, QStringLiteral("id"), &error);
                      const std::optional<QString> raw = node ? text(args, QStringLiteral("file"), true, &error, 4096) : std::nullopt;
                      const std::optional<QString> file = raw ? existingFile(*raw, &error) : std::nullopt;
                      const std::optional<Layout> layout = file ? layoutArguments(args, &error) : std::nullopt;
                      if (!layout) return ToolResult::error(error);
                      if (const FileValidation::Check check = FileValidation::image(*file); !check.ok)
                          return ToolResult::error(QStringLiteral("Not a usable image: %1").arg(check.reason));
                      // As the app: the natural size, no wider than the topic's room (at least 200 px).
                      const QSizeF natural = NodeItem::insertionImageSize(*file);
                      const qreal room = std::max<qreal>(std::floor(scene.imageRoomWidth(node)), 200);
                      const QSizeF size = natural.width() <= room ? natural
                                                                  : QSizeF(room, std::max<qreal>(1, std::round(natural.height() * room / natural.width())));
                      document.setImage(node, *file, size);
                      applyLayout(document, node, *layout);
                      return ToolResult::object(layoutJson(node));
                  }});

    edits.append({{QStringLiteral("set_formula"), QStringLiteral("Put a formula"),
                   QStringLiteral("Puts a LaTeX formula in the topic's picture place (empty = removed); placement and width as "
                                  "place_visual."),
                   [&] {
                       QJsonObject properties{{"id", idProperty}, {"sheet", kSheet}, {"latex", stringProperty(QStringLiteral("LaTeX code, e.g. E = mc^2"))}};
                       addLayoutProperties(properties);
                       return schema(properties, {QStringLiteral("id"), QStringLiteral("latex")});
                   }(), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      MindNode *node = topic(model, args, QStringLiteral("id"), &error);
                      const std::optional<QString> latex = node ? text(args, QStringLiteral("latex"), true, &error) : std::nullopt;
                      const std::optional<Layout> layout = latex ? layoutArguments(args, &error) : std::nullopt;
                      if (!layout) return ToolResult::error(error);
                      MindNode probe;  // the natural size, as the formula dialog (MainWindow::editFormula)
                      probe.formula = *latex;
                      probe.fontFamily = node->fontFamily;
                      probe.fontSize = node->fontSize;
                      probe.fontWeight = node->fontWeight;
                      probe.parent = node->parent;
                      document.setFormula(node, *latex, Qt::white, latex->isEmpty() ? QSizeF() : NodeItem::formulaInsertionSize(&probe));
                      if (latex->isEmpty()) return ToolResult::object({{"id", node->id}});
                      applyLayout(document, node, *layout);
                      return ToolResult::object(layoutJson(node));
                  }});

    edits.append({{QStringLiteral("place_visual"), QStringLiteral("Place the picture"),
                   QStringLiteral("Moves the image, sticker or formula of topics above, below, left or right of the title and/or "
                                  "gives it a width in pixels (the height keeps the proportions), as dragging it or its handles."),
                   [] {
                       QJsonObject properties{{"ids", idsProperty(QStringLiteral("Topics with a picture"))}, {"sheet", kSheet}};
                       addLayoutProperties(properties);
                       return schema(properties, {QStringLiteral("ids")});
                   }(), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      const QList<MindNode *> nodes = topics(model, args, QStringLiteral("ids"), &error);
                      if (nodes.isEmpty()) return ToolResult::error(error);
                      const std::optional<Layout> layout = layoutArguments(args, &error);
                      if (!layout) return ToolResult::error(error);
                      if (!layout->placement && !layout->width) return ToolResult::error(QStringLiteral("Nothing to change: give placement or width"));
                      for (const MindNode *node : nodes)
                          if (node->imagePath.isEmpty() && node->formula.isEmpty())
                              return ToolResult::error(QStringLiteral("%1 has no image, sticker or formula").arg(node->id));
                      QJsonArray placed;
                      for (MindNode *node : nodes) {
                          applyLayout(document, node, *layout);
                          placed.append(layoutJson(node));
                      }
                      return ToolResult::object({{"topics", placed}});
                  }});

    edits.append({{QStringLiteral("remove_visual"), QStringLiteral("Remove the picture"),
                   QStringLiteral("Removes the image, sticker or formula of the topic."),
                   schema({{"id", idProperty}, {"sheet", kSheet}}, {QStringLiteral("id")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      MindNode *node = topic(model, args, QStringLiteral("id"), &error);
                      if (!node) return ToolResult::error(error);
                      if (!document.removeVisual(node)) return ToolResult::error(QStringLiteral("The topic has no image, sticker or formula"));
                      return ToolResult::object({{"id", node->id}});
                  }});

    edits.append({{QStringLiteral("set_links"), QStringLiteral("Set links"),
                   QStringLiteral("Sets the web link (http, https, mailto) and/or the link to another topic of the same sheet; "
                                  "an empty value removes it, an omitted one stays."),
                   schema({{"id", idProperty}, {"sheet", kSheet}, {"web", stringProperty(QStringLiteral("Web address"))},
                           {"topic", stringProperty(QStringLiteral("Id of the linked topic"))}},
                          {QStringLiteral("id")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      MindNode *node = topic(model, args, QStringLiteral("id"), &error);
                      const std::optional<QString> web = node ? text(args, QStringLiteral("web"), false, &error, 4096) : std::nullopt;
                      const std::optional<QString> target = error.isEmpty() ? text(args, QStringLiteral("topic"), false, &error, 200) : std::nullopt;
                      if (!node || !error.isEmpty()) return ToolResult::error(error);
                      if (!web && !target) return ToolResult::error(QStringLiteral("Nothing to change: give web or topic"));
                      MindMapModel::LinkChange change{node, std::nullopt, std::nullopt};
                      if (web) {
                          change.webLink = web->trimmed().isEmpty() ? QString() : NodeLinks::webAddress(*web);
                          if (!web->trimmed().isEmpty() && change.webLink->isEmpty())
                              return ToolResult::error(QStringLiteral("%1 is not an http, https or mailto address").arg(*web));
                      }
                      if (target) {
                          const MindNode *linked = target->isEmpty() ? nullptr : model.nodeById(*target);
                          if (!target->isEmpty() && (!linked || linked == node))
                              return ToolResult::error(QStringLiteral("No other topic with id %1 in this sheet").arg(*target));
                          change.topicLink = *target;
                      }
                      model.setLinks({change});
                      document.markChanged();
                      return ToolResult::object({{"id", node->id}, {"web", node->webLink}, {"topic", node->topicLink}});
                  }});

    edits.append({{QStringLiteral("set_task"), QStringLiteral("Set the checkbox"),
                   QStringLiteral("Gives topics a checkbox to do, ticks it (done), or removes it (none)."),
                   schema({{"ids", idsProperty(QStringLiteral("Topics"))}, {"sheet", kSheet},
                           {"state", QJsonObject{{"enum", QJsonArray{"none", "todo", "done"}}}}},
                          {QStringLiteral("ids"), QStringLiteral("state")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      const QList<MindNode *> nodes = topics(model, args, QStringLiteral("ids"), &error);
                      if (nodes.isEmpty()) return ToolResult::error(error);
                      const QString state = args.value(QStringLiteral("state")).toString();
                      const std::optional<MindNode::Task> task = state == QLatin1String("none") ? std::optional(MindNode::Task::None)
                                                               : state == QLatin1String("todo") ? std::optional(MindNode::Task::Open)
                                                               : state == QLatin1String("done") ? std::optional(MindNode::Task::Done)
                                                                                                : std::nullopt;
                      if (!task) return ToolResult::error(QStringLiteral("state must be none, todo or done"));
                      document.setTask(nodes, *task);
                      return ToolResult::object({{"state", state}});
                  }});

    edits.append({{QStringLiteral("attach_files"), QStringLiteral("Attach files"),
                   QStringLiteral("Attaches files to a topic; they are embedded in the map when saved."),
                   schema({{"id", idProperty}, {"sheet", kSheet},
                           {"files", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"minItems", 1}}}},
                          {QStringLiteral("id"), QStringLiteral("files")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      MindNode *node = topic(model, args, QStringLiteral("id"), &error);
                      if (!node) return ToolResult::error(error);
                      const QJsonArray list = args.value(QStringLiteral("files")).toArray();
                      if (list.isEmpty() || list.size() > 50) return ToolResult::error(QStringLiteral("files must list 1 to 50 paths"));
                      QStringList files;
                      for (const QJsonValue &value : list) {
                          const std::optional<QString> file = existingFile(value.toString(), &error);
                          if (!file) return ToolResult::error(error);
                          if (const FileValidation::Check check = FileValidation::anyFile(*file); !check.ok)
                              return ToolResult::error(QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(*file), check.reason));
                          files.append(*file);
                      }
                      document.addFiles(node, files);
                      return ToolResult::object({{"id", node->id}, {"files", int(node->files.size())}});
                  }});

    edits.append({{QStringLiteral("rename_file"), QStringLiteral("Rename an attached file"),
                   QStringLiteral("Gives the attached file at index (0 = the first; read_map lists them) a new shown name; the "
                                  "extension stays when the name has none."),
                   schema({{"id", idProperty}, {"sheet", kSheet}, {"index", QJsonObject{{"type", "integer"}, {"minimum", 0}}},
                           {"name", stringProperty(QStringLiteral("New name"))}},
                          {QStringLiteral("id"), QStringLiteral("index"), QStringLiteral("name")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      MindNode *node = topic(model, args, QStringLiteral("id"), &error);
                      const std::optional<int> at = node ? index(args, &error) : std::nullopt;
                      if (!at) return ToolResult::error(error.isEmpty() ? QStringLiteral("Missing argument: index") : error);
                      if (*at >= int(node->files.size())) return ToolResult::error(QStringLiteral("The topic has no file at %1").arg(*at));
                      if (!document.renameFile(node, *at, args.value(QStringLiteral("name")).toString()))
                          return ToolResult::error(QStringLiteral("Not renamed: an empty or unchanged name, or one with / or \\"));
                      return ToolResult::object({{"id", node->id}, {"name", node->files[*at].name}});
                  }});

    edits.append({{QStringLiteral("remove_file"), QStringLiteral("Remove an attached file"),
                   QStringLiteral("Removes the attached file at index (0 = the first) from the topic."),
                   schema({{"id", idProperty}, {"sheet", kSheet}, {"index", QJsonObject{{"type", "integer"}, {"minimum", 0}}}},
                          {QStringLiteral("id"), QStringLiteral("index")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      MindNode *node = topic(model, args, QStringLiteral("id"), &error);
                      const std::optional<int> at = node ? index(args, &error) : std::nullopt;
                      if (!at) return ToolResult::error(error.isEmpty() ? QStringLiteral("Missing argument: index") : error);
                      if (*at >= int(node->files.size()) || !document.removeFile(node, *at))
                          return ToolResult::error(QStringLiteral("The topic has no file at %1").arg(*at));
                      return ToolResult::object({{"id", node->id}, {"files", int(node->files.size())}});
                  }});
}

McpServer::Tool optionsTool(std::function<Access()> access) {
    return {QStringLiteral("list_options"), QStringLiteral("List style options"),
            QStringLiteral("Lists the values the style and content tools accept: shapes, fill and border styles, text case and "
                           "alignment, branch shapes, presets, marker series and markers, built-in stickers."),
            schema({}, {}), true,
            [access](const QJsonObject &) {
                if (!access().enabled) return ToolResult::error(offMessage());
                QJsonArray markers;
                for (const Markers::Series &series : Markers::series()) {
                    QJsonArray values;  // whole markers, as set_marker takes them ("priority:1")
                    for (const QString &value : series.values) values.append(series.id + QLatin1Char(':') + value);
                    markers.append(QJsonObject{{"series", series.id}, {"markers", values}});
                }
                QJsonObject options{{"shape", QJsonArray::fromStringList(keysOf<NodeShape>(&NodeStyle::shapeKey))},
                                           {"fill_style", QJsonArray::fromStringList(keysOf<NodeFillStyle>(&NodeStyle::fillStyleKey))},
                                           {"border_style", QJsonArray::fromStringList(keysOf<NodeBorderStyle>(&NodeStyle::borderStyleKey))},
                                           {"text_case", QJsonArray::fromStringList(keysOf<TextCase>(&NodeStyle::textCaseKey))},
                                           {"text_align", QJsonArray::fromStringList(keysOf<TextAlign>(&NodeStyle::textAlignKey))},
                                           {"branch_shape", QJsonArray::fromStringList(keysOf<BranchShape>(&NodeStyle::branchShapeKey))},
                                           {"preset", QJsonArray::fromStringList(keysOf<NodePreset>(&NodeStyle::presetKey))},
                                           {"markers", markers},
                                           {"stickers", QJsonArray::fromStringList(builtInStickers())}};
                for (const QJsonObject &more : {structureOptions(), pitchOptions()})  // stages C and E
                    for (auto it = more.constBegin(); it != more.constEnd(); ++it) options.insert(it.key(), it.value());
                return ToolResult::object(options);
            }};
}

}  // namespace McpMapTools::Support
