// FEAT-045: the agents' tools on colours and text: own palettes in the app's library, the theme and
// the style of a sheet, formatted titles and notes (Markdown, as pasting in Mentis), labels renamed
// in the whole map, find and replace in every sheet.
#include "mcptoolsupport.h"

#include "custompalettes.h"
#include "document.h"
#include "markdownimport.h"
#include "markdownshortcuts.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "nodestyle.h"
#include "richtitle.h"

#include <QColor>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextDocument>

namespace McpMapTools::Support {

// A title written with inline Markdown (**bold**, *italic*, ~~crossed out~~): the plain text and the
// formatted title, as an imported or pasted outline gets them (MarkdownImport::toSubtreeJson).
bool setMarkdownTitle(Document &document, MindNode *node, const QString &markdown) {
    const MarkdownImport::Result parsed = MarkdownImport::parse(QStringLiteral("- ") + QString(markdown).replace(QLatin1Char('\n'), QLatin1Char(' ')));
    if (parsed.items.empty()) return false;
    const QJsonObject json = MarkdownImport::toSubtreeJson(parsed.items.front(), !node->parent);
    const QString html = json.value(QStringLiteral("titleHtml")).toString();
    return html.isEmpty() ? document.setTitle(node, json.value(QStringLiteral("title")).toString())
                          : document.setFormattedTitle(node, json.value(QStringLiteral("title")).toString(), html);
}

// Notes in Markdown (lists, bold, ==highlight==...), as a note typed with the Markdown shortcuts.
bool setMarkdownNotes(Document &document, MindNode *node, const QString &markdown) {
    if (markdown.trimmed().isEmpty()) return document.setNote(node, QString(), QString());
    QTextDocument note;
    note.setMarkdown(markdown);
    MarkdownShortcuts::Options options;
    options.context = MarkdownShortcuts::Context::Note;
    MarkdownShortcuts::applyToDocument(&note, options);
    return document.setNote(node, note.toPlainText(), RichTitle::withoutDocumentFont(note.toHtml()));
}

namespace {

QJsonObject paletteJson(const NodeStyle::Palette &palette, bool own) {
    return {{"id", palette.id}, {"name", NodeStyle::displayName(palette)}, {"colors", QJsonArray::fromStringList(palette.colors)}, {"own", own}};
}

std::optional<NodeStyle::Palette> anyPalette(const QString &id) {
    if (const NodeStyle::Palette *base = NodeStyle::findPalette(id)) return *base;
    return CustomPalettes::find(id);
}

std::optional<QColor> colorArgument(const QJsonValue &value) {
    const QColor color(value.toString());
    if (!value.isString() || !color.isValid()) return std::nullopt;
    return color;
}

}  // namespace

QList<McpServer::Tool> paletteTools(std::function<Access()> access) {
    QList<McpServer::Tool> list;
    const auto guarded = [access](std::function<ToolResult(const Access &, const QJsonObject &)> run) {
        return [access, run](const QJsonObject &args) {
            const Access allowed = access();
            if (!allowed.enabled) return ToolResult::error(offMessage());
            return run(allowed, args);
        };
    };
    list.append({QStringLiteral("list_palettes"), QStringLiteral("List colour themes"),
                 QStringLiteral("Lists the colour palettes (built-in and own) with their six branch colours; set_theme applies one to a sheet."),
                 schema({}, {}), true, guarded([](const Access &, const QJsonObject &) {
                     QJsonArray palettes;
                     for (const NodeStyle::Palette &palette : NodeStyle::basePalettes()) palettes.append(paletteJson(palette, false));
                     for (const NodeStyle::Palette &palette : CustomPalettes::all()) palettes.append(paletteJson(palette, true));
                     return ToolResult::object({{"palettes", palettes}, {"variants", NodeStyle::kVariantCount}});
                 })});
    list.append({QStringLiteral("create_palette"), QStringLiteral("Create a colour theme"),
                 QStringLiteral("Creates an own palette of six branch colours (#rrggbb) in the app's library, for every map; returns its id."),
                 schema({{"name", stringProperty(QStringLiteral("Name of the palette"))},
                         {"colors", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"minItems", 6}, {"maxItems", 6}}}},
                        {QStringLiteral("name"), QStringLiteral("colors")}), false,
                 guarded([](const Access &, const QJsonObject &args) {
                     const QString name = args.value(QStringLiteral("name")).toString().simplified();
                     if (name.isEmpty() || name.size() > 100) return ToolResult::error(QStringLiteral("name must be 1 to 100 characters"));
                     if (CustomPalettes::isNameTaken(name)) return ToolResult::error(QStringLiteral("A palette is already called %1").arg(name));
                     QStringList colors;
                     for (const QJsonValue &value : args.value(QStringLiteral("colors")).toArray()) {
                         const std::optional<QColor> color = colorArgument(value);
                         if (!color) return ToolResult::error(QStringLiteral("colors must be six colours like #2563eb"));
                         colors.append(color->name());
                     }
                     if (colors.size() != 6) return ToolResult::error(QStringLiteral("colors must be six colours"));
                     const QString id = CustomPalettes::add(name, colors);
                     if (id.isEmpty()) return ToolResult::error(QStringLiteral("The palette could not be created"));
                     return ToolResult::object({{"id", id}, {"name", name}});
                 })});
    list.append({QStringLiteral("update_palette"), QStringLiteral("Change a colour theme"),
                 QStringLiteral("Changes an own palette's name and/or its six colours. Sheets that use it take the new colours when "
                                "the palette is applied again (set_theme)."),
                 schema({{"id", stringProperty(QStringLiteral("Own palette id"))}, {"name", stringProperty(QStringLiteral("New name"))},
                         {"colors", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"minItems", 6}, {"maxItems", 6}}}},
                        {QStringLiteral("id")}), false,
                 guarded([](const Access &, const QJsonObject &args) {
                     const QString id = args.value(QStringLiteral("id")).toString();
                     if (!CustomPalettes::find(id)) return ToolResult::error(QStringLiteral("No own palette %1 (built-in ones cannot change)").arg(id));
                     std::optional<QString> name;
                     if (args.contains(QStringLiteral("name"))) name = args.value(QStringLiteral("name")).toString().simplified();
                     std::optional<QStringList> colors;
                     if (args.contains(QStringLiteral("colors"))) {
                         colors = QStringList();
                         for (const QJsonValue &value : args.value(QStringLiteral("colors")).toArray()) {
                             const std::optional<QColor> color = colorArgument(value);
                             if (!color) return ToolResult::error(QStringLiteral("colors must be six colours like #2563eb"));
                             colors->append(color->name());
                         }
                     }
                     if (!name && !colors) return ToolResult::error(QStringLiteral("Nothing to change: give name or colors"));
                     if (!CustomPalettes::update(id, name, colors)) return ToolResult::error(QStringLiteral("Refused: an empty or taken name, or not six colours"));
                     return ToolResult::object({{"id", id}});
                 })});
    list.append({QStringLiteral("delete_palette"), QStringLiteral("Delete a colour theme"),
                 QStringLiteral("Deletes an own palette from the library, after the user's explicit confirmation (Mentis asks when "
                                "open; otherwise ask in the chat and pass confirm: true). Maps that use it keep their colours."),
                 schema({{"id", stringProperty(QStringLiteral("Own palette id from list_palettes"))}, {"confirm", kConfirm}},
                        {QStringLiteral("id")}), false,
                 guarded([](const Access &allowed, const QJsonObject &args) {
                     const QString id = args.value(QStringLiteral("id")).toString();
                     const std::optional<NodeStyle::Palette> palette = CustomPalettes::find(id);
                     if (!palette) return ToolResult::error(QStringLiteral("No own palette %1 (built-in ones cannot be deleted)").arg(id));
                     QString error;
                     if (!confirmed(allowed, args, QStringLiteral("delete_palette"), palette->name, &error)) return ToolResult::error(error);
                     if (!CustomPalettes::remove(id)) return ToolResult::error(QStringLiteral("The palette could not be deleted"));
                     return ToolResult::object({{"deleted", id}});
                 })});
    return list;
}

void appendThemeEdits(QList<MapEdit> &edits) {
    edits.append({{QStringLiteral("set_theme"), QStringLiteral("Set the sheet's colours"),
                   QStringLiteral("Applies a palette (list_palettes) to the sheet, and/or one of its variants (0-5: backgrounds, also "
                                  "dark ones; -1 = none), and/or a free background colour (null = none)."),
                   schema({{"sheet", kSheet}, {"palette", stringProperty(QStringLiteral("Palette id"))},
                           {"variant", QJsonObject{{"type", "integer"}, {"minimum", -1}, {"maximum", NodeStyle::kVariantCount - 1}}},
                           {"background", QJsonObject{{"type", QJsonArray{"string", "null"}}, {"description", "#rrggbb, null = none"}}}},
                          {}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      if (!args.contains(QStringLiteral("palette")) && !args.contains(QStringLiteral("variant")) && !args.contains(QStringLiteral("background")))
                          return ToolResult::error(QStringLiteral("Nothing to change: give palette, variant or background"));
                      std::optional<NodeStyle::Palette> palette;
                      if (args.contains(QStringLiteral("palette"))) {
                          palette = anyPalette(args.value(QStringLiteral("palette")).toString());
                          if (!palette) return ToolResult::error(QStringLiteral("No palette %1 (list_palettes lists them)").arg(args.value(QStringLiteral("palette")).toString()));
                      }
                      const QJsonValue variant = args.value(QStringLiteral("variant"));
                      if (args.contains(QStringLiteral("variant")) && (!variant.isDouble() || variant.toInt() < -1 || variant.toInt() >= NodeStyle::kVariantCount))
                          return ToolResult::error(QStringLiteral("variant must be -1 to %1").arg(NodeStyle::kVariantCount - 1));
                      std::optional<std::optional<QColor>> background;
                      if (args.contains(QStringLiteral("background"))) {
                          if (args.value(QStringLiteral("background")).isNull()) background = std::optional<QColor>();
                          else if (const std::optional<QColor> color = colorArgument(args.value(QStringLiteral("background")))) background = color;
                          else return ToolResult::error(QStringLiteral("background must be a colour like #1e3a2a or null"));
                      }
                      // As the Map page: the palette first (it drops variant and background), then those.
                      if (palette) model.setPalette(palette->id, palette->colors, CustomPalettes::find(palette->id) ? palette->name : QString());
                      if (args.contains(QStringLiteral("variant"))) model.setBackdrop(variant.toInt());
                      if (background) model.setBackground(*background);
                      document.markChanged();
                      return ToolResult::object({{"palette", model.paletteId}, {"variant", model.paletteVariant},
                                                 {"background", model.background ? model.background->name() : QString()}});
                  }});

    edits.append({{QStringLiteral("set_sheet_style"), QStringLiteral("Set the sheet's style"),
                   QStringLiteral("The sheet's map style: font, branch width (px, 0 = default), tapered or single-colour branches and "
                                  "their colour, notes shown, compact map, free branches, justified topics. Omitted values stay."),
                   schema({{"sheet", kSheet}, {"font", stringProperty(QStringLiteral("Font family; empty = the system's"))},
                           {"branch_width", QJsonObject{{"type", "number"}, {"minimum", 0}, {"maximum", NodeStyle::kMaxBranchWidthPx}}},
                           {"tapered", QJsonObject{{"type", "boolean"}}}, {"colored_branches", QJsonObject{{"type", "boolean"}}},
                           {"branch_color", stringProperty(QStringLiteral("#rrggbb, the lines without the palette's colours"))},
                           {"show_notes", QJsonObject{{"type", "boolean"}}}, {"compact", QJsonObject{{"type", "boolean"}}},
                           {"free_branches", QJsonObject{{"type", "boolean"}}}, {"justify", QJsonObject{{"type", "boolean"}}}},
                          {}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      NodeStyle::SheetStyle style = model.sheetStyle;
                      if (args.contains(QStringLiteral("font"))) style.fontFamily = args.value(QStringLiteral("font")).toString().trimmed().left(200);
                      if (args.contains(QStringLiteral("branch_width"))) {
                          const double width = args.value(QStringLiteral("branch_width")).toDouble(-1);
                          if (width != 0 && (width < NodeStyle::kMinBranchWidthPx || width > NodeStyle::kMaxBranchWidthPx))
                              return ToolResult::error(QStringLiteral("branch_width must be 0 or %1 to %2").arg(NodeStyle::kMinBranchWidthPx).arg(NodeStyle::kMaxBranchWidthPx));
                          style.branchWidth = width;
                      }
                      if (args.contains(QStringLiteral("branch_color"))) {
                          const std::optional<QColor> color = colorArgument(args.value(QStringLiteral("branch_color")));
                          if (!color) return ToolResult::error(QStringLiteral("branch_color must be a colour like #475569"));
                          style.lineColor = *color;
                      }
                      for (const auto &[key, field] : {std::pair{"tapered", &NodeStyle::SheetStyle::tapered},
                                                       std::pair{"colored_branches", &NodeStyle::SheetStyle::coloredBranches},
                                                       std::pair{"show_notes", &NodeStyle::SheetStyle::showNotes},
                                                       std::pair{"compact", &NodeStyle::SheetStyle::compact},
                                                       std::pair{"free_branches", &NodeStyle::SheetStyle::freeBranches},
                                                       std::pair{"justify", &NodeStyle::SheetStyle::justify}}) {
                          const QJsonValue value = args.value(QLatin1String(key));
                          if (value.isUndefined()) continue;
                          if (!value.isBool()) return ToolResult::error(QStringLiteral("%1 must be true or false").arg(QLatin1String(key)));
                          style.*field = value.toBool();
                      }
                      if (style == model.sheetStyle) return ToolResult::error(QStringLiteral("Nothing changes"));
                      model.setSheetStyle(style);
                      document.markChanged();
                      return ToolResult::object({{"sheet", document.currentSheetId()}});
                  }});

    edits.append({{QStringLiteral("rename_label"), QStringLiteral("Rename a label everywhere"),
                   QStringLiteral("Renames a label on every topic of every sheet (empty to = removes it); topics that have both keep one."),
                   schema({{"from", stringProperty(QStringLiteral("The label now"))}, {"to", stringProperty(QStringLiteral("The new label; empty = removed"))}},
                          {QStringLiteral("from"), QStringLiteral("to")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &scene, MindMapModel &) {
                      const QString from = args.value(QStringLiteral("from")).toString(), to = args.value(QStringLiteral("to")).toString();
                      if (from.trimmed().isEmpty() || from.size() > kMaxTextLength || to.size() > kMaxTextLength)
                          return ToolResult::error(QStringLiteral("from must be a label"));
                      const int changed = document.renameLabel(from, to, [&scene] { scene.autoLayout(); });
                      if (changed == 0) return ToolResult::error(QStringLiteral("No topic has the label %1").arg(from));
                      return ToolResult::object({{"topics", changed}});
                  }});

    edits.append({{QStringLiteral("find_replace"), QStringLiteral("Find and replace"),
                   QStringLiteral("Replaces text in every sheet: in titles, notes and/or labels (in, omitted = titles), whole words and "
                                  "case optional; formatted titles and notes keep their formatting. One Undo step."),
                   schema({{"find", stringProperty(QStringLiteral("Text to find"))}, {"replace", stringProperty(QStringLiteral("New text"))},
                           {"in", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"enum", QJsonArray{"titles", "notes", "labels"}}}}}},
                           {"match_case", QJsonObject{{"type", "boolean"}}}, {"whole_words", QJsonObject{{"type", "boolean"}}}},
                          {QStringLiteral("find"), QStringLiteral("replace")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &scene, MindMapModel &) {
                      const QString find = args.value(QStringLiteral("find")).toString(), replace = args.value(QStringLiteral("replace")).toString();
                      if (find.isEmpty() || find.size() > kMaxTextLength || replace.size() > kMaxTextLength)
                          return ToolResult::error(QStringLiteral("find must be some text"));
                      QStringList in{QStringLiteral("titles")};
                      if (args.contains(QStringLiteral("in"))) {
                          in.clear();
                          for (const QJsonValue &value : args.value(QStringLiteral("in")).toArray()) in.append(value.toString());
                          for (const QString &part : std::as_const(in))
                              if (part != QLatin1String("titles") && part != QLatin1String("notes") && part != QLatin1String("labels"))
                                  return ToolResult::error(QStringLiteral("in lists titles, notes, labels"));
                          if (in.isEmpty()) return ToolResult::error(QStringLiteral("in is empty"));
                      }
                      QRegularExpression pattern(args.value(QStringLiteral("whole_words")).toBool()
                                                     ? QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(find))
                                                     : QRegularExpression::escape(find),
                                                 (args.value(QStringLiteral("match_case")).toBool() ? QRegularExpression::NoPatternOption
                                                                                                    : QRegularExpression::CaseInsensitiveOption)
                                                     | QRegularExpression::UseUnicodePropertiesOption);
                      // In formatted text each match is replaced in place: the new text takes the format of the old one.
                      const auto replaceRich = [&pattern, &replace](const QString &html, QString *plain) {
                          QTextDocument rich;
                          rich.setHtml(html);
                          QTextCursor at(&rich);
                          bool changed = false;
                          while (true) {
                              const QTextCursor found = rich.find(pattern, at);
                              if (found.isNull()) break;
                              QTextCursor edit = found;
                              QTextCharFormat format = edit.charFormat();
                              edit.insertText(replace, format);
                              at = edit;
                              changed = true;
                          }
                          *plain = rich.toPlainText();
                          return changed ? RichTitle::withoutDocumentFont(rich.toHtml()) : html;
                      };
                      const int count = document.replaceText([&](MindNode &node) {
                          bool changed = false;
                          if (in.contains(QLatin1String("titles")) && node.title.contains(pattern)) {
                              if (node.titleHtml.isEmpty()) node.title.replace(pattern, replace);
                              else node.titleHtml = replaceRich(node.titleHtml, &node.title);
                              changed = true;
                          }
                          if (in.contains(QLatin1String("notes")) && node.notes.contains(pattern)) {
                              if (node.notesHtml.isEmpty()) node.notes.replace(pattern, replace);
                              else node.notesHtml = replaceRich(node.notesHtml, &node.notes);
                              changed = true;
                          }
                          if (in.contains(QLatin1String("labels"))) {
                              QStringList labels = node.labels;
                              for (QString &label : labels) label.replace(pattern, replace);
                              labels = Labels::normalized(labels, Labels::autoSort());
                              if (labels != node.labels) { node.labels = labels; changed = true; }
                          }
                          return changed;
                      }, [&scene] { scene.autoLayout(); });
                      if (count == 0) return ToolResult::error(QStringLiteral("%1 was not found").arg(find));
                      return ToolResult::object({{"topics", count}});
                  }});
}

}  // namespace McpMapTools::Support
