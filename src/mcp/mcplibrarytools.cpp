// FEAT-045: the agents manage the sticker and marker library (FEAT-051, the same for every map):
// list it, add an SVG they made (or a file), create, rename, order and remove
// collections, move and remove items. Removing files needs the user's yes (user's decision
// 2026-11-02); hiding or restoring the built-in collections does not. The open windows reload their
// panel after each change.
#include "mcptoolsupport.h"

#include "iconlibrary.h"
#include "markers.h"
#include "mcpbridge.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>

namespace McpMapTools::Support {
namespace {

std::optional<IconLibrary::Kind> kindOf(const QJsonObject &args, QString *error) {
    const QString kind = args.value(QStringLiteral("kind")).toString(QStringLiteral("sticker"));
    if (kind == QLatin1String("sticker")) return IconLibrary::Kind::Sticker;
    if (kind == QLatin1String("marker")) return IconLibrary::Kind::Marker;
    *error = QStringLiteral("kind must be sticker or marker");
    return std::nullopt;
}

const QJsonObject kKind{{"enum", QJsonArray{"sticker", "marker"}}, {"description", "omitted = sticker"}};

// The windows show the library in their panel: they read it again.
void libraryChanged() {
    for (const McpBridge::Instance &window : McpBridge::instances()) McpBridge::forward(window, QStringLiteral("@library_changed"), {});
}

const IconLibrary::Collection *findCollection(const QList<IconLibrary::Collection> &collections, const QString &id) {
    for (const IconLibrary::Collection &collection : collections)
        if (collection.id == id) return &collection;
    return nullptr;
}

QString collectionOf(const QList<IconLibrary::Collection> &collections, const QString &path) {
    for (const IconLibrary::Collection &collection : collections)
        for (const IconLibrary::Item &item : collection.items)
            if (item.path == path) return collection.id;
    return {};
}

using Run = std::function<ToolResult(const Access &, const QJsonObject &, IconLibrary::Kind)>;

McpServer::Tool libraryTool(const std::function<Access()> &access, const QString &name, const QString &title, const QString &description,
                            QJsonObject properties, const QStringList &required, bool readOnly, const Run &run) {
    properties.insert(QStringLiteral("kind"), kKind);
    return {name, title, description, schema(properties, required), readOnly, [access, run, readOnly](const QJsonObject &args) {
        const Access allowed = access();
        if (!allowed.enabled) return ToolResult::error(offMessage());
        QString error;
        const std::optional<IconLibrary::Kind> kind = kindOf(args, &error);
        if (!kind) return ToolResult::error(error);
        const ToolResult result = run(allowed, args, *kind);
        if (!readOnly && !result.isError) libraryChanged();
        return result;
    }};
}

}  // namespace

// A marker of the library (its file) as the id set_marker gives a topic, with the image the map
// keeps (IconPage::markerFor does the same for a click in the panel).
std::optional<std::pair<QString, QByteArray>> libraryMarker(const QString &path) {
    const QList<IconLibrary::Collection> collections = IconLibrary::collections(IconLibrary::Kind::Marker);
    const QString collection = collectionOf(collections, path);
    if (collection.isEmpty() || !path.contains(QLatin1Char('/'))) return std::nullopt;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    const QByteArray bytes = file.readAll();
    const QString sha = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    Markers::registerImage(sha, bytes);
    Markers::setCustomLabel(sha, IconLibrary::displayName(path));
    QString group = collection;
    return std::pair{Markers::customMarker(group.replace(QLatin1Char(':'), QLatin1Char('-')), sha), bytes};
}

bool inStickerLibrary(const QString &path) {
    return !collectionOf(IconLibrary::collections(IconLibrary::Kind::Sticker), path).isEmpty();
}

QList<McpServer::Tool> libraryTools(std::function<Access()> access) {
    using IconLibrary::Kind;
    QList<McpServer::Tool> list;
    const QJsonObject collectionProperty = stringProperty(QStringLiteral("Collection id from list_library"));

    list.append(libraryTool(access, QStringLiteral("list_library"), QStringLiteral("List stickers or markers"),
        QStringLiteral("Lists the sticker (or marker) collections in their order, with their items: path (what set_sticker and "
                       "set_marker take) and name. Built-in collections cannot be deleted, only hidden."),
        {}, {}, true, [](const Access &, const QJsonObject &, Kind kind) {
            QJsonArray collections;
            for (const IconLibrary::Collection &collection : IconLibrary::collections(kind)) {
                QJsonArray items;
                for (const IconLibrary::Item &item : collection.items) items.append(QJsonObject{{"path", item.path}, {"name", item.name}});
                collections.append(QJsonObject{{"id", collection.id}, {"name", collection.name}, {"builtIn", collection.builtIn},
                                               {"hidden", collection.hidden}, {"items", items}});
            }
            return ToolResult::object({{"collections", collections}});
        }));

    list.append(libraryTool(access, QStringLiteral("add_to_library"), QStringLiteral("Add a sticker or marker"),
        QStringLiteral("Adds an image to a collection (omitted = a new one named by collection_name, or the first own one): "
                       "svg = the SVG text you made, or file = an SVG or PNG file (absolute path). At most 2 MB; it must "
                       "draw. Returns its path, for set_sticker or set_marker."),
        {{"collection", collectionProperty}, {"collection_name", stringProperty(QStringLiteral("Name of a new collection"))},
         {"name", stringProperty(QStringLiteral("Name of the sticker, e.g. lotus"))}, {"svg", stringProperty(QStringLiteral("SVG text"))},
         {"file", stringProperty(QStringLiteral("Absolute path of an SVG or PNG"))}},
        {QStringLiteral("name")}, false, [](const Access &allowed, const QJsonObject &args, Kind kind) {
            QString error;
            const std::optional<QString> name = text(args, QStringLiteral("name"), true, &error, 100);
            const std::optional<QString> svg = name ? text(args, QStringLiteral("svg"), false, &error, int(IconLibrary::kMaxFileBytes)) : std::nullopt;
            const std::optional<QString> file = error.isEmpty() ? text(args, QStringLiteral("file"), false, &error, 4096) : std::nullopt;
            if (!name || !error.isEmpty()) return ToolResult::error(error);
            if (bool(svg) == bool(file)) return ToolResult::error(QStringLiteral("Give either svg or file"));
            // The library's own checks, on a file: a written copy of the SVG text, or the chosen file.
            QTemporaryDir scratch;
            QString source;
            if (svg) {
                const QString clean = name->simplified().replace(QLatin1Char('/'), QLatin1Char('-')).replace(QLatin1Char('\\'), QLatin1Char('-'));
                source = scratch.filePath((clean.isEmpty() ? QStringLiteral("sticker") : clean) + QStringLiteral(".svg"));
                QFile out(source);
                if (!out.open(QIODevice::WriteOnly) || out.write(svg->toUtf8()) < 0) return ToolResult::error(QStringLiteral("The SVG could not be written"));
            } else {
                const std::optional<QString> filePath = existingFile(*file, &error);
                if (!filePath) return ToolResult::error(error);
                source = *filePath;
            }
            if (const IconLibrary::Check check = IconLibrary::checkFile(kind, source); !check.ok)
                return ToolResult::error(QStringLiteral("Not usable: %1").arg(check.reason));
            QString collection = args.value(QStringLiteral("collection")).toString();
            const QList<IconLibrary::Collection> collections = IconLibrary::collections(kind);
            if (!collection.isEmpty()) {
                const IconLibrary::Collection *found = findCollection(collections, collection);
                if (!found) return ToolResult::error(QStringLiteral("No collection %1 (list_library lists them)").arg(collection));
                if (found->builtIn) return ToolResult::error(QStringLiteral("%1 is built in: add to an own collection").arg(found->name));
            } else if (const QString newName = args.value(QStringLiteral("collection_name")).toString().simplified(); !newName.isEmpty()) {
                collection = IconLibrary::addCollection(kind, newName);
            } else {
                for (const IconLibrary::Collection &own : collections)
                    if (!own.builtIn) { collection = own.id; break; }
                if (collection.isEmpty())
                    collection = IconLibrary::addCollection(kind, kind == Kind::Sticker ? QStringLiteral("My stickers") : QStringLiteral("My markers"));
            }
            QFile input(source);
            if (!input.open(QIODevice::ReadOnly)) return ToolResult::error(QStringLiteral("The image could not be read"));
            const QString path = IconLibrary::addData(kind, collection, QFileInfo(source).fileName(), input.readAll());
            if (path.isEmpty()) return ToolResult::error(QStringLiteral("The image could not be added"));
            QJsonObject result{{"path", path}, {"collection", collection}};
            if (kind == Kind::Marker)
                if (const auto marker = libraryMarker(path)) result.insert(QStringLiteral("marker"), marker->first);
            return ToolResult::object(result);
        }));

    list.append(libraryTool(access, QStringLiteral("create_collection"), QStringLiteral("Create a collection"),
        QStringLiteral("Creates an own collection at the end."), {{"name", stringProperty(QStringLiteral("Name"))}},
        {QStringLiteral("name")}, false, [](const Access &, const QJsonObject &args, Kind kind) {
            const QString name = args.value(QStringLiteral("name")).toString().simplified();
            if (name.isEmpty() || name.size() > 100) return ToolResult::error(QStringLiteral("name must be 1 to 100 characters"));
            return ToolResult::object({{"id", IconLibrary::addCollection(kind, name)}});
        }));

    list.append(libraryTool(access, QStringLiteral("rename_collection"), QStringLiteral("Rename a collection"),
        QStringLiteral("Gives a collection (also a built-in one) a new name."),
        {{"collection", collectionProperty}, {"name", stringProperty(QStringLiteral("New name"))}},
        {QStringLiteral("collection"), QStringLiteral("name")}, false, [](const Access &, const QJsonObject &args, Kind kind) {
            const QString name = args.value(QStringLiteral("name")).toString().simplified();
            if (name.isEmpty() || name.size() > 100) return ToolResult::error(QStringLiteral("name must be 1 to 100 characters"));
            if (!IconLibrary::renameCollection(kind, args.value(QStringLiteral("collection")).toString(), name))
                return ToolResult::error(QStringLiteral("No collection %1").arg(args.value(QStringLiteral("collection")).toString()));
            return ToolResult::object({{"name", name}});
        }));

    list.append(libraryTool(access, QStringLiteral("move_collection"), QStringLiteral("Order collections"),
        QStringLiteral("Moves a collection to position index (0 = first)."),
        {{"collection", collectionProperty}, {"index", QJsonObject{{"type", "integer"}, {"minimum", 0}}}},
        {QStringLiteral("collection"), QStringLiteral("index")}, false, [](const Access &, const QJsonObject &args, Kind kind) {
            QString error;
            const std::optional<int> at = index(args, &error);
            if (!at) return ToolResult::error(error.isEmpty() ? QStringLiteral("Missing argument: index") : error);
            if (!IconLibrary::moveCollection(kind, args.value(QStringLiteral("collection")).toString(), *at))
                return ToolResult::error(QStringLiteral("The collection could not be moved"));
            return ToolResult::object({{"index", *at}});
        }));

    list.append(libraryTool(access, QStringLiteral("move_library_item"), QStringLiteral("Move a sticker or marker"),
        QStringLiteral("Moves an item to a collection (also its own) at position index (omitted = last): to order or regroup them."),
        {{"path", stringProperty(QStringLiteral("Item path from list_library"))}, {"collection", collectionProperty},
         {"index", QJsonObject{{"type", "integer"}, {"minimum", 0}}}},
        {QStringLiteral("path"), QStringLiteral("collection")}, false, [](const Access &, const QJsonObject &args, Kind kind) {
            QString error;
            const std::optional<int> at = index(args, &error);
            if (!error.isEmpty()) return ToolResult::error(error);
            if (!IconLibrary::moveItem(kind, args.value(QStringLiteral("path")).toString(), args.value(QStringLiteral("collection")).toString(),
                                       at.value_or(-1)))
                return ToolResult::error(QStringLiteral("The item could not be moved (unknown item or collection)"));
            return ToolResult::object({{"collection", args.value(QStringLiteral("collection")).toString()}});
        }));

    list.append(libraryTool(access, QStringLiteral("rename_library_item"), QStringLiteral("Rename a sticker or marker"),
        QStringLiteral("Gives an own item a new name (its file is renamed): returns its new path. Built-in items keep theirs."),
        {{"path", stringProperty(QStringLiteral("Item path from list_library"))}, {"name", stringProperty(QStringLiteral("New name"))}},
        {QStringLiteral("path"), QStringLiteral("name")}, false, [](const Access &, const QJsonObject &args, Kind kind) {
            const QString path = IconLibrary::renameItem(kind, args.value(QStringLiteral("path")).toString(), args.value(QStringLiteral("name")).toString());
            if (path.isEmpty()) return ToolResult::error(QStringLiteral("Not renamed: only own items, with a name of 1 to 100 characters"));
            return ToolResult::object({{"path", path}, {"name", IconLibrary::displayName(path)}});
        }));

    list.append(libraryTool(access, QStringLiteral("remove_library_item"), QStringLiteral("Remove a sticker or marker"),
        QStringLiteral("Removes an item from its collection; an own file is deleted, after the user's explicit confirmation "
                       "(Mentis asks when open; otherwise ask the user in the chat and pass confirm: true)."),
        {{"path", stringProperty(QStringLiteral("Item path from list_library"))}, {"confirm", kConfirm}},
        {QStringLiteral("path")}, false, [](const Access &allowed, const QJsonObject &args, Kind kind) {
            const QString path = args.value(QStringLiteral("path")).toString();
            const QList<IconLibrary::Collection> collections = IconLibrary::collections(kind);
            const QString collection = collectionOf(collections, path);
            if (collection.isEmpty()) return ToolResult::error(QStringLiteral("No item %1 (list_library lists them)").arg(path));
            QString error;
            if (!confirmed(allowed, args, QStringLiteral("delete_library_item"), IconLibrary::displayName(path), &error)) return ToolResult::error(error);
            if (!IconLibrary::removeItem(kind, collection, path)) return ToolResult::error(QStringLiteral("The item could not be removed"));
            return ToolResult::object({{"removed", path}});
        }));

    list.append(libraryTool(access, QStringLiteral("remove_collection"), QStringLiteral("Remove a collection"),
        QStringLiteral("Deletes an own collection with its files (after the user's explicit confirmation, as remove_library_item), "
                       "or hides a built-in one (no confirmation; restore_builtins shows them again)."),
        {{"collection", collectionProperty}, {"confirm", kConfirm}},
        {QStringLiteral("collection")}, false, [](const Access &allowed, const QJsonObject &args, Kind kind) {
            const QList<IconLibrary::Collection> collections = IconLibrary::collections(kind);
            const IconLibrary::Collection *found = findCollection(collections, args.value(QStringLiteral("collection")).toString());
            if (!found) return ToolResult::error(QStringLiteral("No collection %1").arg(args.value(QStringLiteral("collection")).toString()));
            QString error;
            if (!found->builtIn && !confirmed(allowed, args, QStringLiteral("delete_collection"), found->name, &error)) return ToolResult::error(error);
            if (!IconLibrary::removeCollection(kind, found->id)) return ToolResult::error(QStringLiteral("The collection could not be removed"));
            return ToolResult::object({{found->builtIn ? "hidden" : "deleted", found->id}});
        }));

    list.append(libraryTool(access, QStringLiteral("restore_builtins"), QStringLiteral("Restore the built-in collections"),
        QStringLiteral("Shows the hidden built-in collections again, with their original names and items."),
        {}, {}, false, [](const Access &, const QJsonObject &, Kind kind) {
            IconLibrary::restoreBuiltIns(kind);
            return ToolResult::object({{"restored", true}});
        }));
    return list;
}

}  // namespace McpMapTools::Support
