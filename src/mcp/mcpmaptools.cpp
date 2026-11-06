#include "mcpmaptools.h"

#include "custompalettes.h"
#include "mcpbridge.h"
#include "mcptoolsupport.h"
#include "document.h"
#include "mapvariants.h"
#include "markdownimport.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QSettings>
#include <QSysInfo>
#include <QUrl>

namespace McpMapTools {

using ToolResult = McpServer::ToolResult;

Access Access::load() {
    const QSettings settings;
    return {settings.value(QStringLiteral("mcp/enabled"), false).toBool(), {}};
}

void Access::save() const {
    QSettings settings;
    settings.setValue(QStringLiteral("mcp/enabled"), enabled);
}

ToolResult routeMap(const Access &access, const MapEdit &edit, const QJsonObject &args);

QString instructions() {
    QFile guide(QStringLiteral(":/mcp/mentis-mcp-guide.md"));
    return guide.open(QIODevice::ReadOnly) ? QString::fromUtf8(guide.readAll()) : QString();
}

namespace Support {

const QString kOff = QStringLiteral(
    "Agents are turned off in Mentis. The user can turn them on in Settings > General > Agents (MCP).");

// --- Arguments ---

// A string argument; nullopt with error set when it is missing (required), not a string or too long.
std::optional<QString> text(const QJsonObject &args, const QString &key, bool required, QString *error, int maxLength) {
    const QJsonValue value = args.value(key);
    if (value.isUndefined() || value.isNull()) {
        if (required) *error = QStringLiteral("Missing argument: %1").arg(key);
        return std::nullopt;
    }
    if (!value.isString()) { *error = QStringLiteral("%1 must be a string").arg(key); return std::nullopt; }
    const QString result = value.toString();
    if (result.size() > maxLength) { *error = QStringLiteral("%1 is longer than %2 characters").arg(key).arg(maxLength); return std::nullopt; }
    return result;
}

std::optional<int> index(const QJsonObject &args, QString *error) {
    const QJsonValue value = args.value(QStringLiteral("index"));
    if (value.isUndefined() || value.isNull()) return std::nullopt;
    if (!value.isDouble() || value.toDouble() < 0 || value.toDouble() != std::floor(value.toDouble())) {
        *error = QStringLiteral("index must be a whole number from 0");
        return std::nullopt;
    }
    return value.toInt();
}

std::optional<QStringList> labels(const QJsonObject &args, QString *error) {
    const QJsonValue value = args.value(QStringLiteral("labels"));
    if (value.isUndefined() || value.isNull()) return std::nullopt;
    if (!value.isArray()) { *error = QStringLiteral("labels must be an array of strings"); return std::nullopt; }
    QStringList result;
    for (const QJsonValue &label : value.toArray()) {
        if (!label.isString() || label.toString().size() > kMaxTextLength) { *error = QStringLiteral("labels must be an array of strings"); return std::nullopt; }
        result.append(label.toString());
    }
    return result;
}

// --- Paths ---

// The .mentis file at path, resolved (links included). existing: the file must exist; otherwise it
// must not (a new map) and its folder must.
std::optional<QString> mapPath(const QJsonObject &args, bool existing, QString *error) {
    const std::optional<QString> raw = text(args, QStringLiteral("path"), true, error, 4096);
    if (!raw) return std::nullopt;
    if (!QDir::isAbsolutePath(*raw)) { *error = QStringLiteral("path must be absolute"); return std::nullopt; }
    const QFileInfo info(*raw);
    if (info.suffix().compare(QStringLiteral("mentis"), Qt::CaseInsensitive) != 0) {
        *error = QStringLiteral("Only .mentis files");
        return std::nullopt;
    }
    QString canonical;
    if (existing) {
        if (!info.isFile()) { *error = QStringLiteral("No map at %1").arg(QDir::toNativeSeparators(*raw)); return std::nullopt; }
        canonical = info.canonicalFilePath();
    } else {
        if (info.exists()) { *error = QStringLiteral("%1 already exists").arg(QDir::toNativeSeparators(*raw)); return std::nullopt; }
        const QString folder = QFileInfo(info.absolutePath()).canonicalFilePath();
        if (folder.isEmpty()) { *error = QStringLiteral("The folder of %1 does not exist").arg(QDir::toNativeSeparators(*raw)); return std::nullopt; }
        canonical = folder + QLatin1Char('/') + info.fileName();
    }
    return canonical;
}

// --- Maps ---

// The Home shows the sheet the agent changed, as the app shows the one it closed on.
void storePreview(Document &document) {
    document.setPreviewProvider([&document] { return MindMapScene::homePreviewJpeg(document.sheetContent(document.currentSheet())); });
    document.storePreview();
}

using Edit = std::function<ToolResult(const Access &, const QJsonObject &, Document &, MindMapScene &, MindMapModel &)>;

// Opens the map (and the sheet in args "sheet", else the first), runs edit and, when it changes the
// map and succeeds, lays it out and saves. The file is closed (and unlocked) before returning.
ToolResult withMap(const Access &access, const QJsonObject &args, bool write, const Edit &edit) {
    if (!access.enabled) return ToolResult::error(kOff);
    QString error;
    const std::optional<QString> path = mapPath(args, true, &error);
    if (!path) return ToolResult::error(error);
    const std::optional<QString> sheet = text(args, QStringLiteral("sheet"), false, &error, 200);
    if (!error.isEmpty()) return ToolResult::error(error);
    Document document;
    const Document::Result opened = document.open(*path, false);
    if (!opened.ok)
        return ToolResult::error(opened.fileLocked
            ? QStringLiteral("The map is open in Mentis: ask the user to close it there first.")
            : QStringLiteral("Cannot open the map: %1").arg(opened.error));
    if (sheet && !sheet->isEmpty()) {
        const int found = document.sheetIndex(*sheet);
        if (found < 0) { document.close(); return ToolResult::error(QStringLiteral("No sheet with id %1 (read_map lists them)").arg(*sheet)); }
        if (const Document::Result shown = document.switchSheet(found); !shown.ok) {
            document.close();
            return ToolResult::error(QStringLiteral("Cannot open the sheet: %1").arg(shown.error));
        }
    }
    MindMapScene scene(document.model());
    ToolResult result = edit(access, args, document, scene, *document.model());
    if (write && !result.isError) {
        scene.autoLayout();  // the positions the app would give, so the map opens laid out
        document.flushPendingStep();
        if (const Document::Result saved = document.save(*path, BmapStorage::SaveReason::Automatic); !saved.ok)
            result = ToolResult::error(QStringLiteral("The map was not saved: %1").arg(saved.error));
        else
            storePreview(document);
    }
    document.close();
    return result;
}

MindNode *topic(MindMapModel &model, const QJsonObject &args, const QString &key, QString *error) {
    const std::optional<QString> id = text(args, key, true, error, 200);
    if (!id) return nullptr;
    MindNode *node = model.nodeById(*id);
    if (!node) *error = QStringLiteral("No topic with id %1 in this sheet (read_map lists them)").arg(*id);
    else if (node->summaryTopic) { *error = QStringLiteral("%1 is a summary topic: not available to agents yet").arg(*id); node = nullptr; }
    return node;
}

QList<MindNode *> topics(MindMapModel &model, const QJsonObject &args, const QString &key, QString *error) {
    const QJsonValue value = args.value(key);
    if (!value.isArray() || value.toArray().isEmpty()) { *error = QStringLiteral("%1 must be a non-empty array of topic ids").arg(key); return {}; }
    QList<MindNode *> nodes;
    for (const QJsonValue &id : value.toArray()) {
        MindNode *node = id.isString() ? model.nodeById(id.toString()) : nullptr;
        if (!node || node->summaryTopic) { *error = QStringLiteral("No topic with id %1 in this sheet").arg(id.toString()); return {}; }
        if (!nodes.contains(node)) nodes.append(node);
    }
    return nodes;
}

std::optional<QString> existingFile(const QString &raw, QString *error) {
    const QFileInfo info(raw);
    if (!QDir::isAbsolutePath(raw) || !info.isFile()) { *error = QStringLiteral("No file at %1").arg(QDir::toNativeSeparators(raw)); return std::nullopt; }
    return info.canonicalFilePath();
}

QJsonObject topicJson(const MindNode *node, int &budget, bool &truncated) {
    QJsonObject json{{"id", node->id}, {"title", node->title}};
    if (!node->notes.isEmpty()) json.insert(QStringLiteral("notes"), node->notes);
    if (!node->labels.isEmpty()) json.insert(QStringLiteral("labels"), QJsonArray::fromStringList(node->labels));
    if (node->floating) json.insert(QStringLiteral("floating"), true);
    if (!node->imagePath.isEmpty() || !node->formula.isEmpty()) {  // the picture place: image, sticker or formula
        static const QStringList placements{QStringLiteral("above"), QStringLiteral("below"), QStringLiteral("left"), QStringLiteral("right")};
        const QString kind = !node->formula.isEmpty() ? QStringLiteral("formula") : node->imageSticker ? QStringLiteral("sticker") : QStringLiteral("image");
        json.insert(QStringLiteral("picture"), QJsonObject{{"kind", kind}, {"placement", placements.value(int(node->imagePlacement))},
                                                           {"width", node->imageSize.width()}, {"height", node->imageSize.height()}});
    }
    if (!node->files.isEmpty()) {
        QJsonArray files;
        for (const NodeFile &file : node->files) files.append(file.name);
        json.insert(QStringLiteral("files"), files);
    }
    QJsonArray children;
    for (const auto &child : node->children) {
        if (budget <= 0) { truncated = true; break; }
        --budget;
        children.append(topicJson(child.get(), budget, truncated));
    }
    if (!children.isEmpty()) json.insert(QStringLiteral("children"), children);
    return json;
}

QJsonArray sheetsJson(const Document &document) {
    QJsonArray sheets;
    for (int i = 0; i < document.sheets().size(); ++i)
        sheets.append(QJsonObject{{"id", document.sheets()[i].id}, {"title", document.sheets()[i].name},
                                  {"active", i == document.currentSheet()}});
    return sheets;
}

// Markdown becomes the sheet's structure: a list of two or more items replaces the map (the first is
// the central topic, as importing a file), a single item goes under the central topic.
ToolResult applyMarkdown(Document &document, const QString &markdown) {
    if (markdown.toUtf8().size() > kMaxMarkdownBytes) return ToolResult::error(QStringLiteral("markdown is larger than 1 MB"));
    Document::PasteResult pasted = document.replaceMapWithMarkdown(markdown);
    if (!pasted.first && !pasted.tooLarge)
        pasted = document.pasteMarkdown(document.model()->root(), int(document.model()->root()->children.size()), markdown);
    if (pasted.tooLarge) return ToolResult::error(QStringLiteral("markdown has more than %1 items").arg(MarkdownImport::kMaxPastedItems));
    if (!pasted.first) return ToolResult::error(QStringLiteral("markdown has no headings or list items"));
    return {};
}

bool inBranch(const MindNode *node, const MindNode *branch) {
    for (const MindNode *at = node; at; at = at->parent)
        if (at == branch) return true;
    return false;
}

// --- Where a tool on a map runs ---

ToolResult forwarded(const std::optional<McpBridge::Instance> &window, const MapEdit &edit, const QJsonObject &args) {
    if (!window)
        return ToolResult::error(QStringLiteral("No map is open in Mentis: give the path of a .mentis file, or ask the user to open the map"));
    return McpBridge::forward(*window, edit.tool.name, args);
}

}  // namespace Support

using namespace Support;

// Phase 2: a map open in a window is changed there (one Undo step, the user sees it); without path,
// the map of the window used last, even without a name (user's decision 2026-11-02).
ToolResult routeMap(const Access &access, const MapEdit &edit, const QJsonObject &args) {
    if (!access.enabled) return ToolResult::error(kOff);
    if (!edit.edit || !args.contains(QStringLiteral("path"))) return forwarded(McpBridge::lastActive(), edit, args);
    QString error;
    const std::optional<QString> path = mapPath(args, true, &error);
    if (!path) return ToolResult::error(error);
    if (const std::optional<McpBridge::Instance> window = McpBridge::instanceWith(*path)) return McpBridge::forward(*window, edit.tool.name, args);
    return withMap(access, args, edit.write, edit.edit);
}

QString offMessage() { return kOff; }

namespace Support {

// path becomes optional for the tools on a map: without it, the map open in Mentis.
QJsonObject openMapSchema(QJsonObject schema, const QString &name) {
    if (name == QLatin1String("save_open_map")) return schema;
    QJsonArray required;
    for (const QJsonValue &key : schema.value(QStringLiteral("required")).toArray())
        if (key.toString() != QLatin1String("path")) required.append(key);
    if (required.isEmpty()) schema.remove(QStringLiteral("required"));
    else schema.insert(QStringLiteral("required"), required);
    QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
    properties.insert(QStringLiteral("path"),
                      QJsonObject{{"type", "string"},
                                  {"description", "Absolute path of the .mentis file; omitted = the map open "
                                                  "in Mentis (the window used last)"}});
    schema.insert(QStringLiteral("properties"), properties);
    return schema;
}

// --- Schemas ---

QJsonObject stringProperty(const QString &description) { return {{"type", "string"}, {"description", description}}; }

QJsonObject schema(const QJsonObject &properties, const QStringList &required) {
    QJsonObject result{{"type", "object"}, {"properties", properties}, {"additionalProperties", false}};
    if (!required.isEmpty()) result.insert(QStringLiteral("required"), QJsonArray::fromStringList(required));
    return result;
}

const QJsonObject kPath = stringProperty(QStringLiteral("Absolute path of the .mentis file"));
bool confirmed(const Access &access, const QJsonObject &args, const QString &kind, const QString &subject, QString *error) {
    if (access.ask) {
        if (const std::optional<bool> answer = access.ask(kind, subject)) {
            if (!*answer) *error = QStringLiteral("The user did not confirm in Mentis: nothing was changed");
            return *answer;
        }
    }
    if (args.value(QStringLiteral("confirm")).toBool()) return true;
    *error = QStringLiteral("This needs the user's explicit confirmation: Mentis is not open to ask, so ask the user in the chat "
                            "and call again with confirm: true only if they agree");
    return false;
}

const QJsonObject kConfirm{{"type", "boolean"},
                           {"description", "true only after the user agreed in the chat; when Mentis is open it asks the user itself"}};

QJsonObject idsProperty(const QString &description) {
    return {{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"minItems", 1}, {"description", description}};
}

const QJsonObject kTitleMarkdown = stringProperty(QStringLiteral("The title with **bold**, *italic*, ~~crossed out~~ (instead of title)"));
const QJsonObject kNotesMarkdown = stringProperty(QStringLiteral("Notes in Markdown: lists, **bold**, ==highlight== (instead of notes)"));

const QJsonObject kSheet = stringProperty(QStringLiteral("Sheet id from read_map; omitted = the first sheet"));
const QJsonObject kIndex{{"type", "integer"}, {"minimum", 0}, {"description", "Position among the siblings; omitted = last"}};
const QJsonObject kLabels{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"description", "Labels (tags) of the topic"}};
const QJsonObject kMarkdown = stringProperty(QStringLiteral("Markdown outline: headings and nested lists become topics"));

}  // namespace Support

using namespace Support;

const QList<MapEdit> &mapEdits() {
    static const QList<MapEdit> edits = [] {
        QList<MapEdit> edits;
    edits.append({{QStringLiteral("read_map"), QStringLiteral("Read a map"),
                 QStringLiteral("Returns the sheets of the map and the topic tree of one sheet: id, title, notes, labels, children. "
                                "Read before changing a map; topics are addressed by id."),
                 schema({{"path", kPath}, {"sheet", kSheet}}, {QStringLiteral("path")}), true, {}},
                  false,
                  [](const Access &, const QJsonObject &, Document &document, MindMapScene &, MindMapModel &model) {
        int budget = kMaxReadTopics - 1;
        bool truncated = false;
        const QJsonObject root = topicJson(model.root(), budget, truncated);
        QJsonObject result{{"path", QDir::toNativeSeparators(document.filePath())}, {"sheets", sheetsJson(document)},
                           {"sheet", document.currentSheetId()}, {"root", root}, {"truncated", truncated}};
        const QJsonObject elements = elementsJson(model);  // relationships, summaries, boundaries
        for (auto it = elements.constBegin(); it != elements.constEnd(); ++it) result.insert(it.key(), it.value());
        return ToolResult::object(result);
    }});
    edits.append({{QStringLiteral("add_sheet"), QStringLiteral("Add a sheet"),
                 QStringLiteral("Adds a sheet at the end of the map, with title as its name and central topic, or the structure of markdown."),
                 schema({{"path", kPath}, {"title", stringProperty(QStringLiteral("Name of the sheet"))}, {"markdown", kMarkdown}},
                        {QStringLiteral("path"), QStringLiteral("title")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &scene, MindMapModel &model) {
        QString error;
        const std::optional<QString> title = text(args, QStringLiteral("title"), true, &error, 200);
        const std::optional<QString> markdown = error.isEmpty() ? text(args, QStringLiteral("markdown"), false, &error, kMaxMarkdownBytes) : std::nullopt;
        if (!error.isEmpty()) return ToolResult::error(error);
        const QString name = title->simplified();
        if (name.isEmpty()) return ToolResult::error(QStringLiteral("title is empty"));
        const QString variant = MapVariants::all().first().id;
        document.addSheet([&variant](MindMapModel &sheet) { MapVariants::apply(sheet, variant, true); },
                          [&scene] { scene.autoLayout(); });
        document.renameSheet(document.currentSheet(), name);
        if (markdown && !markdown->trimmed().isEmpty()) {
            if (const ToolResult applied = applyMarkdown(document, *markdown); applied.isError) return applied;
        } else {
            document.setTitle(model.root(), name);
        }
        return ToolResult::object({{"sheet", document.currentSheetId()}, {"rootId", model.root()->id}});
    }});
    edits.append({{QStringLiteral("rename_sheet"), QStringLiteral("Rename a sheet"), QStringLiteral("Gives a sheet a new name."),
                 schema({{"path", kPath}, {"sheet", stringProperty(QStringLiteral("Sheet id from read_map"))},
                         {"title", stringProperty(QStringLiteral("New name"))}},
                        {QStringLiteral("path"), QStringLiteral("sheet"), QStringLiteral("title")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &) {
        QString error;
        const std::optional<QString> title = text(args, QStringLiteral("title"), true, &error, 200);
        if (!title) return ToolResult::error(error);
        if (title->simplified().isEmpty()) return ToolResult::error(QStringLiteral("title is empty"));
        document.renameSheet(document.currentSheet(), *title);
        return ToolResult::object({{"sheet", document.currentSheetId()}, {"title", document.sheets()[document.currentSheet()].name}});
    }});
    edits.append({{QStringLiteral("add_topic"), QStringLiteral("Add a topic"),
                   QStringLiteral("Adds a topic under parent_id (the central topic's id for a main branch). Returns its id. "
                                  "title_markdown and notes_markdown give formatted text instead of title and notes."),
                   schema({{"path", kPath}, {"sheet", kSheet}, {"parent_id", stringProperty(QStringLiteral("Id of the parent topic"))},
                           {"title", stringProperty(QStringLiteral("Text of the topic"))}, {"index", kIndex},
                           {"notes", stringProperty(QStringLiteral("Plain text notes"))}, {"labels", kLabels},
                           {"title_markdown", kTitleMarkdown}, {"notes_markdown", kNotesMarkdown}},
                          {QStringLiteral("path"), QStringLiteral("parent_id")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      MindNode *parent = topic(model, args, QStringLiteral("parent_id"), &error);
                      const std::optional<QString> titleMarkdown = parent ? text(args, QStringLiteral("title_markdown"), false, &error) : std::nullopt;
                      const std::optional<QString> title = parent && error.isEmpty() ? text(args, QStringLiteral("title"), !titleMarkdown, &error) : std::nullopt;
                      const std::optional<int> at = error.isEmpty() ? index(args, &error) : std::nullopt;
                      const std::optional<QString> notes = error.isEmpty() ? text(args, QStringLiteral("notes"), false, &error) : std::nullopt;
                      const std::optional<QString> notesMarkdown = error.isEmpty() ? text(args, QStringLiteral("notes_markdown"), false, &error) : std::nullopt;
                      const std::optional<QStringList> tags = error.isEmpty() ? labels(args, &error) : std::nullopt;
                      if (!parent || (!title && !titleMarkdown) || !error.isEmpty()) return ToolResult::error(error);
                      MindNode *node = document.addChild(parent, title.value_or(QString()));
                      if (!node) return ToolResult::error(QStringLiteral("The topic could not be added"));
                      if (at && *at < int(parent->children.size()) - 1) document.moveNode(node, parent, *at, 0);
                      if (titleMarkdown) setMarkdownTitle(document, node, *titleMarkdown);
                      if (notes) document.setNotes(node, *notes);
                      if (notesMarkdown) setMarkdownNotes(document, node, *notesMarkdown);
                      if (tags) document.setLabels(node, *tags);
                      return ToolResult::object({{"id", node->id}});
                  }});
    edits.append({{QStringLiteral("update_topic"), QStringLiteral("Change a topic"),
                   QStringLiteral("Changes the title, the notes (empty = none) or the labels of a topic; omitted fields stay. "
                                  "title and notes are plain text; title_markdown and notes_markdown keep formatting."),
                   schema({{"path", kPath}, {"sheet", kSheet}, {"id", stringProperty(QStringLiteral("Id of the topic"))},
                           {"title", stringProperty(QStringLiteral("New text"))}, {"notes", stringProperty(QStringLiteral("New notes"))},
                           {"labels", kLabels}, {"title_markdown", kTitleMarkdown}, {"notes_markdown", kNotesMarkdown}},
                          {QStringLiteral("path"), QStringLiteral("id")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      MindNode *node = topic(model, args, QStringLiteral("id"), &error);
                      const std::optional<QString> title = node ? text(args, QStringLiteral("title"), false, &error) : std::nullopt;
                      const std::optional<QString> notes = error.isEmpty() ? text(args, QStringLiteral("notes"), false, &error) : std::nullopt;
                      const std::optional<QString> titleMarkdown = error.isEmpty() ? text(args, QStringLiteral("title_markdown"), false, &error) : std::nullopt;
                      const std::optional<QString> notesMarkdown = error.isEmpty() ? text(args, QStringLiteral("notes_markdown"), false, &error) : std::nullopt;
                      const std::optional<QStringList> tags = error.isEmpty() ? labels(args, &error) : std::nullopt;
                      if (!node || !error.isEmpty()) return ToolResult::error(error);
                      if (!title && !notes && !tags && !titleMarkdown && !notesMarkdown)
                          return ToolResult::error(QStringLiteral("Nothing to change: give title, notes or labels"));
                      if (title) document.setTitle(node, *title);
                      if (titleMarkdown) setMarkdownTitle(document, node, *titleMarkdown);
                      if (notes) document.setNotes(node, *notes);
                      if (notesMarkdown) setMarkdownNotes(document, node, *notesMarkdown);
                      if (tags) document.setLabels(node, *tags);
                      return ToolResult::object({{"id", node->id}});
                  }});
    edits.append({{QStringLiteral("move_topic"), QStringLiteral("Move a topic"),
                 QStringLiteral("Moves a topic with its branch under parent_id (omitted = the same parent) at index. "
                                "Not the central topic, not into its own branch."),
                 schema({{"path", kPath}, {"sheet", kSheet}, {"id", stringProperty(QStringLiteral("Id of the topic"))},
                         {"parent_id", stringProperty(QStringLiteral("Id of the new parent"))}, {"index", kIndex}},
                        {QStringLiteral("path"), QStringLiteral("id")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
        QString error;
        MindNode *node = topic(model, args, QStringLiteral("id"), &error);
        if (!node) return ToolResult::error(error);
        if (!node->parent) return ToolResult::error(QStringLiteral("The central topic cannot be moved"));
        MindNode *parent = args.contains(QStringLiteral("parent_id")) ? topic(model, args, QStringLiteral("parent_id"), &error) : node->parent;
        const std::optional<int> at = parent ? index(args, &error) : std::nullopt;
        if (!parent || !error.isEmpty()) return ToolResult::error(error);
        if (inBranch(parent, node)) return ToolResult::error(QStringLiteral("A topic cannot go into its own branch"));
        // index is the final place among the siblings; moveNode counts rows in the list that still
        // holds the topic, so moving down in the same parent is one row further.
        const bool sameParent = parent == node->parent;
        const int count = int(parent->children.size()) - (sameParent ? 1 : 0);
        const int target = std::min(at.value_or(count), count);
        const auto &siblings = node->parent->children;
        const int oldRow = int(std::find_if(siblings.cbegin(), siblings.cend(), [node](const auto &child) { return child.get() == node; })
                               - siblings.cbegin());
        document.moveNode(node, parent, sameParent && target > oldRow ? target + 1 : target, 0);
        return ToolResult::object({{"id", node->id}, {"parent_id", node->parent->id}});
    }});
    edits.append({{QStringLiteral("delete_topic"), QStringLiteral("Delete a topic"),
                 QStringLiteral("Deletes a topic with its whole branch (not the central topic). The user can restore it in Mentis."),
                 schema({{"path", kPath}, {"sheet", kSheet}, {"id", stringProperty(QStringLiteral("Id of the topic"))}},
                        {QStringLiteral("path"), QStringLiteral("id")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
        QString error;
        MindNode *node = topic(model, args, QStringLiteral("id"), &error);
        if (!node) return ToolResult::error(error);
        if (!node->parent) return ToolResult::error(QStringLiteral("The central topic cannot be deleted"));
        const QString id = node->id;
        if (!document.removeNodes({node})) return ToolResult::error(QStringLiteral("The topic could not be deleted"));
        return ToolResult::object({{"deleted", id}});
    }});
    edits.append({{QStringLiteral("import_markdown"), QStringLiteral("Add a Markdown outline"),
                 QStringLiteral("Adds the headings and list items of markdown as branches under parent_id, at index."),
                 schema({{"path", kPath}, {"sheet", kSheet}, {"parent_id", stringProperty(QStringLiteral("Id of the parent topic"))},
                         {"markdown", kMarkdown}, {"index", kIndex}},
                        {QStringLiteral("path"), QStringLiteral("parent_id"), QStringLiteral("markdown")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
        QString error;
        MindNode *parent = topic(model, args, QStringLiteral("parent_id"), &error);
        const std::optional<QString> markdown = parent ? text(args, QStringLiteral("markdown"), true, &error, kMaxMarkdownBytes) : std::nullopt;
        const std::optional<int> at = markdown ? index(args, &error) : std::nullopt;
        if (!parent || !markdown || !error.isEmpty()) return ToolResult::error(error);
        if (markdown->toUtf8().size() > kMaxMarkdownBytes) return ToolResult::error(QStringLiteral("markdown is larger than 1 MB"));
        const int row = std::min(at.value_or(int(parent->children.size())), int(parent->children.size()));
        const Document::PasteResult pasted = document.pasteMarkdown(parent, row, *markdown);
        if (pasted.tooLarge) return ToolResult::error(QStringLiteral("markdown has more than %1 items").arg(MarkdownImport::kMaxPastedItems));
        if (!pasted.first) return ToolResult::error(QStringLiteral("markdown has no headings or list items"));
        return ToolResult::object({{"firstId", pasted.first->id}});
    }});
    edits.append({{QStringLiteral("export_markdown"), QStringLiteral("Export a sheet as Markdown"),
                 QStringLiteral("Returns one sheet as a Markdown outline (titles, levels, notes, web links)."),
                 schema({{"path", kPath}, {"sheet", kSheet}}, {QStringLiteral("path")}), true, {}},
                  false,
                  [](const Access &, const QJsonObject &, Document &, MindMapScene &, MindMapModel &model) {
        return ToolResult{model.toMarkdown(), {}, false};
    }});

        appendContentEdits(edits);  // phase 3, stage A
        appendElementEdits(edits);  // stage B
        appendSheetEdits(edits);    // stage C
        appendGanttEdits(edits);    // stage D
        appendExportEdits(edits);   // stage E
        appendThemeEdits(edits);    // the sheet's theme and style, labels, find and replace
        edits.append({{QStringLiteral("open_dialog"), QStringLiteral("Open a window of Mentis"),
                       QStringLiteral("Opens a window of the Mentis app for the user, who completes it: export_image (with scope, "
                                      "gantt, background, scale, frame preset), export_pdf (scope, kind, background, frame), print, "
                                      "export_word, export_excel, export_pitch, gantt_resources, sticker_editor (sticker path), "
                                      "new_palette, formula / topic_link / web_link / labels / note (topic id), settings (page), "
                                      "present. The call returns once the window is open; nothing is confirmed for the user."),
                       schema({{"dialog", QJsonObject{{"enum", QJsonArray{"export_image", "export_pdf", "print", "export_word",
                                                                     "export_excel", "export_pitch", "gantt_resources",
                                                                     "sticker_editor", "new_palette", "formula", "topic_link",
                                                                     "web_link", "labels", "note", "settings", "present"}}}},
                               {"sheet", kSheet}, {"id", stringProperty(QStringLiteral("Topic, for formula, links, labels and note"))},
                               {"sticker", stringProperty(QStringLiteral("Sticker path from list_library, for sticker_editor"))},
                               {"page", QJsonObject{{"enum", QJsonArray{"general", "appearance", "editor", "shortcuts"}}}},
                               {"scope", QJsonObject{{"enum", QJsonArray{"sheet", "all", "branch"}}}},
                               {"kind", QJsonObject{{"enum", QJsonArray{"map", "outliner", "gantt", "pitch"}}, {"description", "export_pdf"}}},
                               {"gantt", QJsonObject{{"type", "boolean"}, {"description", "export_image: the Gantt instead of the map"}}},
                               {"background", QJsonObject{{"type", "boolean"}}},
                               {"scale", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 3}}},
                               {"frame", QJsonObject{{"type", "object"},
                                                     {"description", "style (none, solid, dashed, dotted, double, greek-key, waves, chain, "
                                                                     "dentils), thickness (px), color (#rrggbb), rounded"}}}},
                              {QStringLiteral("dialog")}), false, {}},
                      false, {}});
        edits.append({{QStringLiteral("save_open_map"), QStringLiteral("Save the open map"),
                       QStringLiteral("Saves the map open in Mentis: with path as a .mentis file (also a "
                                      "map without a name; an existing file only after the user's confirmation), without path "
                                      "where it already is."),
                       schema({{"path", stringProperty(QStringLiteral(".mentis file; omitted = the map's own file"))}, {"confirm", kConfirm}}, {}), false, {}},
                      false, {}});
        return edits;
    }();
    return edits;
}

const MapEdit *mapEdit(const QString &name) {
    for (const MapEdit &edit : mapEdits())
        if (edit.tool.name == name) return &edit;
    return nullptr;
}

QList<McpServer::Tool> tools(std::function<Access()> accessProvider) {
    // A confirmation is asked in the Mentis window used last, when there is one.
    const auto access = [accessProvider] {
        Access allowed = accessProvider ? accessProvider() : Access();
        if (!allowed.ask)
            allowed.ask = [](const QString &kind, const QString &subject) -> std::optional<bool> {
                const std::optional<McpBridge::Instance> window = McpBridge::lastActive();
                if (!window) return std::nullopt;
                const ToolResult answer = McpBridge::forward(*window, QStringLiteral("@confirm"), {{"kind", kind}, {"subject", subject}});
                return !answer.isError && answer.structured.value(QStringLiteral("confirmed")).toBool();
            };
        return allowed;
    };
    QList<McpServer::Tool> list;

    list.append({QStringLiteral("list_maps"), QStringLiteral("List maps"),
                 QStringLiteral("Lists .mentis maps: those in folder and its subfolders, or without folder the maps the user "
                                "opened recently in Mentis."),
                 schema({{"folder", stringProperty(QStringLiteral("Absolute path of a folder; omitted = the recent maps"))}}, {}), true,
                 [access](const QJsonObject &args) {
        if (!access().enabled) return ToolResult::error(kOff);
        QString error;
        const std::optional<QString> only = text(args, QStringLiteral("folder"), false, &error, 4096);
        if (!error.isEmpty()) return ToolResult::error(error);
        QJsonArray maps;
        bool truncated = false;
        const auto add = [&maps](const QFileInfo &info) {
            maps.append(QJsonObject{{"path", QDir::toNativeSeparators(info.filePath())},
                                    {"modified", info.lastModified().toString(Qt::ISODate)},
                                    {"bytes", double(info.size())}});
        };
        if (only) {
            const QFileInfo folder(*only);
            if (!QDir::isAbsolutePath(*only) || !folder.isDir()) return ToolResult::error(QStringLiteral("No folder at %1").arg(QDir::toNativeSeparators(*only)));
            // Any folder can be given, even a whole disk: the walk stops after kMaxListSeconds. No name
            // filter, so the time is checked at every file, not only at every map found.
            QElapsedTimer elapsed;
            elapsed.start();
            QDirIterator files(folder.canonicalFilePath(), QDir::Files, QDirIterator::Subdirectories);
            while (files.hasNext()) {
                if (maps.size() >= kMaxListedMaps || elapsed.elapsed() > kMaxListSeconds * 1000) { truncated = true; break; }
                const QFileInfo info(files.next());
                if (info.suffix().compare(QStringLiteral("mentis"), Qt::CaseInsensitive) == 0) add(info);
            }
        } else {
            // The Home's recent maps (MainWindow keeps "recentFiles"), those still on disk.
            for (const QString &path : QSettings().value(QStringLiteral("recentFiles")).toStringList())
                if (const QFileInfo info(path); info.isFile() && maps.size() < kMaxListedMaps) add(info);
        }
        return ToolResult::object({{"maps", maps}, {"truncated", truncated}});
    }});

    list.append({QStringLiteral("create_map"), QStringLiteral("Create a map"),
                 QStringLiteral("Creates a new .mentis map. The central topic is title, or the first item of markdown when it "
                                "has two or more items. An existing map is replaced only after the user's confirmation, never "
                                "while it is open in Mentis."),
                 schema({{"path", kPath}, {"title", stringProperty(QStringLiteral("Central topic"))}, {"markdown", kMarkdown},
                         {"confirm", kConfirm}},
                        {QStringLiteral("path")}), false,
                 [access](const QJsonObject &args) {
        const Access allowed = access();
        if (!allowed.enabled) return ToolResult::error(kOff);
        QString error;
        bool exists = false;
        const std::optional<QString> path = outputPath(args, QStringLiteral("path"), {QStringLiteral("mentis")}, &exists, &error);
        if (path && exists) {
            if (fileInUse(*path)) return ToolResult::error(QStringLiteral("The map is open in Mentis: it cannot be replaced"));
            if (!confirmed(allowed, args, QStringLiteral("overwrite"), *path, &error)) return ToolResult::error(error);
        }
        const std::optional<QString> title = path ? text(args, QStringLiteral("title"), false, &error) : std::nullopt;
        const std::optional<QString> markdown = error.isEmpty() ? text(args, QStringLiteral("markdown"), false, &error, kMaxMarkdownBytes) : std::nullopt;
        if (!path || !error.isEmpty()) return ToolResult::error(error);
        Document document;
        MindMapModel &model = *document.model();
        // As a new map in the app (MainWindow::applyTemplate): the default theme and the Mind Map type.
        model.clear(title && !title->trimmed().isEmpty() ? title->trimmed() : MindMapModel::tr("Central Topic"));
        const CustomPalettes::DefaultTheme theme = CustomPalettes::defaultTheme();
        const bool custom = theme.palette.group == NodeStyle::PaletteGroup::Personalizzata;
        model.setTheme(theme.palette.id, theme.palette.colors, custom ? theme.palette.name : QString(), theme.variant);
        MapVariants::apply(model, MapVariants::all().first().id, false);
        document.startUntitled();
        MindMapScene scene(&model);
        if (markdown && !markdown->trimmed().isEmpty())
            if (const ToolResult applied = applyMarkdown(document, *markdown); applied.isError) { document.close(); return applied; }
        scene.autoLayout();
        document.flushPendingStep();
        const bool saved = replaceWith(*path, [&document](const QString &target, QString *failure) {
            const Document::Result result = document.save(target, BmapStorage::SaveReason::Explicit);
            if (!result.ok) *failure = result.error;
            return result.ok;
        }, &error);
        if (saved) storePreview(document);
        const QString root = model.root()->id;
        const QJsonArray sheets = sheetsJson(document);
        document.close();
        if (!saved) return ToolResult::error(QStringLiteral("The map was not saved: %1").arg(error));
        return ToolResult::object({{"path", QDir::toNativeSeparators(*path)}, {"sheets", sheets}, {"rootId", root}});
    }});

    // The user's request (2026-11-06): after creating a map the agent may open it, with the user's yes.
    list.append({QStringLiteral("open_map"), QStringLiteral("Open a map in Mentis"),
                 QStringLiteral("Opens a .mentis map in Mentis so the user sees it (for example one made with create_map). "
                                "Mentis asks the user in its window; with no Mentis window open, ask the user in the chat "
                                "and pass confirm: true only if they agree: Mentis then starts with the map."),
                 schema({{"path", kPath}, {"confirm", kConfirm}}, {QStringLiteral("path")}), false,
                 [access](const QJsonObject &args) {
        if (!access().enabled) return ToolResult::error(kOff);
        QString error;
        const std::optional<QString> path = mapPath(args, true, &error);
        if (!path) return ToolResult::error(error);
        // Open in a window already: that window comes to the front. Otherwise the window used last asks
        // and opens it (after asking to save its own changes).
        std::optional<McpBridge::Instance> window = McpBridge::instanceWith(*path);
        if (!window) window = McpBridge::lastActive();
        if (window) return McpBridge::forward(*window, QStringLiteral("open_map"), args);
        if (!confirmed(Access{true, {}}, args, QStringLiteral("open_map"), *path, &error)) return ToolResult::error(error);
        // The new Mentis is a normal window: not the server's offscreen platform (McpStdio::prepareProcess),
        // and no stdio of its own, which belongs to the agent's client.
        QProcess mentis;
        mentis.setProgram(QCoreApplication::applicationFilePath());
        mentis.setArguments({QDir::toNativeSeparators(*path)});
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        if (environment.value(QStringLiteral("QT_QPA_PLATFORM")) == QLatin1String("offscreen")) environment.remove(QStringLiteral("QT_QPA_PLATFORM"));
        mentis.setProcessEnvironment(environment);
        mentis.setStandardInputFile(QProcess::nullDevice());
        mentis.setStandardOutputFile(QProcess::nullDevice());
        mentis.setStandardErrorFile(QProcess::nullDevice());
        if (!mentis.startDetached()) return ToolResult::error(QStringLiteral("Mentis could not be started"));
        return ToolResult::object({{"path", QDir::toNativeSeparators(*path)}, {"started", true}});
    }});

    list.append({QStringLiteral("report_issue"), QStringLiteral("Prepare a bug report"),
                 QStringLiteral("Prepares a bug report about Mentis: the text and a link that opens a prefilled issue. Nothing is sent: "
                                "show both to the user, who decides. Do not put map content in it unless the user agrees."),
                 schema({{"title", stringProperty(QStringLiteral("Short summary"))},
                         {"description", stringProperty(QStringLiteral("What happened"))},
                         {"steps", stringProperty(QStringLiteral("Steps to reproduce"))},
                         {"expected", stringProperty(QStringLiteral("What should have happened"))}},
                        {QStringLiteral("title"), QStringLiteral("description")}), true,
                 [access](const QJsonObject &args) {
        if (!access().enabled) return ToolResult::error(kOff);
        QString error;
        const std::optional<QString> title = text(args, QStringLiteral("title"), true, &error, 200);
        const std::optional<QString> description = title ? text(args, QStringLiteral("description"), true, &error) : std::nullopt;
        const std::optional<QString> steps = description ? text(args, QStringLiteral("steps"), false, &error) : std::nullopt;
        const std::optional<QString> expected = error.isEmpty() ? text(args, QStringLiteral("expected"), false, &error) : std::nullopt;
        if (!title || !description || !error.isEmpty()) return ToolResult::error(error);
        QString body = QStringLiteral("## Description\n\n%1\n").arg(*description);
        if (steps && !steps->isEmpty()) body += QStringLiteral("\n## Steps to reproduce\n\n%1\n").arg(*steps);
        if (expected && !expected->isEmpty()) body += QStringLiteral("\n## Expected\n\n%1\n").arg(*expected);
        body += QStringLiteral("\n## Environment\n\n- Mentis %1\n- %2, Qt %3\n- Prepared by an agent through the Mentis MCP server\n")
                    .arg(QCoreApplication::applicationVersion(), QSysInfo::prettyProductName(), QString::fromLatin1(qVersion()));
        // A link too long for browsers carries the start of the text; the full text is in the answer.
        constexpr int kMaxLinkBody = 6000;
        const QString linkBody = body.size() > kMaxLinkBody ? body.left(kMaxLinkBody) + QStringLiteral("\n\n(Continues: paste the full text.)") : body;
        const QString url = kNewIssueUrl + QStringLiteral("?title=") + QString::fromUtf8(QUrl::toPercentEncoding(*title))
                            + QStringLiteral("&body=") + QString::fromUtf8(QUrl::toPercentEncoding(linkBody));
        return ToolResult::object({{"title", *title}, {"body", body}, {"url", url},
                                   {"note", "Nothing was sent. Show the text and the link to the user, who opens it to send the issue "
                                            "or copies the text into an email."}});
    }});


    list.append(optionsTool(access));
    list.append(libraryTools(access));  // stickers and markers of the app, for every map
    list.append(paletteTools(access));  // own palettes of the app
    list.append(pitchTemplateTools(access));  // Pitch templates of the app (FEAT-089)
    list.append(currencyTools(access));  // currencies saved in the app
    for (const MapEdit &edit : mapEdits()) {
        McpServer::Tool tool = edit.tool;
        tool.inputSchema = openMapSchema(tool.inputSchema, tool.name);
        tool.call = [access, &edit](const QJsonObject &args) { return routeMap(access(), edit, args); };
        list.append(tool);
    }
    return list;
}

}  // namespace McpMapTools
