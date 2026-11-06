// FEAT-089: the agents manage the Pitch templates of the app's library (the same for every map):
// list them, create or change one (pictures from files), import and export
// .mentis-theme files, delete one after the user's yes. set_pitch (mcpexporttools.cpp) applies one
// to a sheet.
#include "mcptoolsupport.h"

#include "pitchtemplate.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

namespace McpMapTools::Support {
namespace {

// The template as the agents read it: the file's keys, with true in place of each picture's data.
QJsonObject templateJson(const Pitch::Template &value) {
    QJsonObject object = Pitch::toJson(value);
    object.remove(QStringLiteral("format"));
    for (const char *key : {"picture", "coverPicture", "logo"})
        if (object.contains(QLatin1String(key))) object.insert(QLatin1String(key), true);
    return object;
}

const QJsonObject kOptions{
    {"base", "a pitch_theme of list_options, or dark"},
    {"item_style", QJsonArray{"plain", "pill", "badge", "label"}},
    {"motif", QJsonArray{"none", "band", "clouds", "smudge", "bars", "halftone", "grain", "lines", "blobs", "rules", "frame"}},
    {"logo_corner", QJsonArray{"top-left", "top-right", "bottom-left", "bottom-right"}},
    {"colors", QJsonArray{"background", "text", "title", "line", "fill", "coverBackground", "coverText", "topicBackground", "topicText"}}};

// Applies the arguments to object (Pitch::toJson form); false with error for a value that the
// template would not keep (unknown key, wrong colour, unreadable picture).
bool applyArguments(const QJsonObject &args, QJsonObject &object, QString *error) {
    if (args.contains(QStringLiteral("name"))) {
        const QString name = args.value(QStringLiteral("name")).toString().simplified();
        if (name.isEmpty() || name.size() > 100) { *error = QStringLiteral("name must be 1 to 100 characters"); return false; }
        object.insert(QStringLiteral("name"), name);
    }
    if (args.contains(QStringLiteral("base"))) {
        const QString base = args.value(QStringLiteral("base")).toString();
        const Pitch::Theme theme = Pitch::themeFromKey(base);
        if (base != QLatin1String("dark") && Pitch::themeKey(theme) != base) { *error = QStringLiteral("base: unknown theme %1").arg(base); return false; }
        object.insert(QStringLiteral("base"), Pitch::themeKey(theme));
    }
    if (args.contains(QStringLiteral("colors"))) {
        QJsonObject colors = object.value(QStringLiteral("colors")).toObject();
        const QJsonObject given = args.value(QStringLiteral("colors")).toObject();
        const QJsonArray known = kOptions.value(QStringLiteral("colors")).toArray();
        for (auto it = given.begin(); it != given.end(); ++it) {
            if (!known.contains(it.key())) { *error = QStringLiteral("colors: unknown key %1").arg(it.key()); return false; }
            if (it.value().isNull()) { colors.remove(it.key()); continue; }  // the base theme's again
            const QColor color(it.value().toString());
            if (!it.value().isString() || !color.isValid()) { *error = QStringLiteral("colors.%1 must be like #2563eb or null").arg(it.key()); return false; }
            colors.insert(it.key(), color.name());
        }
        object.insert(QStringLiteral("colors"), colors);
    }
    // Plain keys: the argument's name and the file's key; empty = the base theme's / none.
    const std::pair<const char *, const char *> plain[] = {{"title_font", "titleFont"}, {"item_font", "itemFont"}, {"item_style", "itemStyle"},
                                                           {"motif", "motif"}, {"logo_corner", "logoCorner"}, {"footer", "footer"}};
    for (const auto &[argument, key] : plain) {
        if (!args.contains(QLatin1String(argument))) continue;
        const QString value = args.value(QLatin1String(argument)).toString();
        if (value.size() > kMaxTextLength) { *error = QStringLiteral("%1 is too long").arg(QLatin1String(argument)); return false; }
        if (value.isEmpty()) object.remove(QLatin1String(key));
        else object.insert(QLatin1String(key), value);
    }
    if (args.contains(QStringLiteral("logo_size"))) {
        const int size = args.value(QStringLiteral("logo_size")).toInt(-1);
        if (size < Pitch::kMinLogoSize || size > Pitch::kMaxLogoSize) {
            *error = QStringLiteral("logo_size must be %1 to %2").arg(Pitch::kMinLogoSize).arg(Pitch::kMaxLogoSize);
            return false;
        }
        object.insert(QStringLiteral("logoSize"), size);
    }
    if (args.contains(QStringLiteral("numbers"))) object.insert(QStringLiteral("numbers"), args.value(QStringLiteral("numbers")).toBool());
    const std::tuple<const char *, const char *, int, bool> pictures[] = {{"picture", "picture", Pitch::kPictureMaxSide, false},
                                                                          {"cover_picture", "coverPicture", Pitch::kPictureMaxSide, false},
                                                                          {"logo", "logo", Pitch::kLogoMaxSide, true}};
    for (const auto &[argument, key, maxSide, png] : pictures) {
        if (!args.contains(QLatin1String(argument))) continue;
        const QString raw = args.value(QLatin1String(argument)).toString();
        if (raw.isEmpty()) { object.remove(QLatin1String(key)); continue; }
        const std::optional<QString> path = existingFile(raw, error);
        if (!path) return false;
        QString reason;
        const QByteArray data = Pitch::loadPicture(*path, maxSide, png, &reason);
        if (data.isEmpty()) { *error = QStringLiteral("%1: not a picture (%2)").arg(QLatin1String(argument), reason); return false; }
        object.insert(QLatin1String(key), QString::fromLatin1(data.toBase64()));
    }
    // A value the template drops on reading (an unknown style, motif or corner) is an error.
    const std::optional<Pitch::Template> read = Pitch::templateFromJson(object);
    if (!read) { *error = QStringLiteral("Not a valid template"); return false; }
    const QJsonObject kept = Pitch::toJson(*read);
    for (const char *key : {"itemStyle", "motif", "logoCorner"})
        if (object.contains(QLatin1String(key)) && kept.value(QLatin1String(key)) != object.value(QLatin1String(key))) {
            *error = QStringLiteral("%1: unknown value %2").arg(QLatin1String(key), object.value(QLatin1String(key)).toString());
            return false;
        }
    object = kept;
    return true;
}

}  // namespace

QList<McpServer::Tool> pitchTemplateTools(std::function<Access()> access) {
    QList<McpServer::Tool> list;
    const auto guarded = [access](std::function<ToolResult(const Access &, const QJsonObject &)> run) {
        return [access, run](const QJsonObject &args) {
            const Access allowed = access();
            if (!allowed.enabled) return ToolResult::error(offMessage());
            return run(allowed, args);
        };
    };
    const QJsonObject idProperty = stringProperty(QStringLiteral("Template id from list_pitch_templates"));

    list.append({QStringLiteral("list_pitch_templates"), QStringLiteral("List Pitch templates"),
                 QStringLiteral("Lists the Pitch templates of the app's library (own themes of the presentation: a base theme with "
                                "own colours, fonts, pictures, logo and footer), and the values save_pitch_template takes. "
                                "set_pitch with template applies one to a sheet."),
                 schema({}, {}), true, guarded([](const Access &, const QJsonObject &) {
                     QJsonArray templates;
                     for (const Pitch::Template &value : PitchTemplates::all()) templates.append(templateJson(value));
                     return ToolResult::object({{"templates", templates}, {"options", kOptions}});
                 })});

    list.append({QStringLiteral("save_pitch_template"), QStringLiteral("Create or change a Pitch template"),
                 QStringLiteral("Creates a Pitch template in the app's library (no id) or changes one (id): only the values given "
                                "change. colors: an object of #rrggbb (null = the base theme's). Fonts, item_style, motif, footer: "
                                "empty = the base theme's. picture (list slides), cover_picture (cover and topic slides), logo: "
                                "absolute path of a picture, empty = none; they are reduced and kept in the "
                                "template. Sheets that use it take the change when it is applied again (set_pitch)."),
                 schema({{"id", idProperty}, {"name", stringProperty(QStringLiteral("Name of the template"))},
                         {"base", stringProperty(QStringLiteral("Base theme"))},
                         {"colors", QJsonObject{{"type", "object"}, {"description", "background, text, title, line, fill, coverBackground, "
                                                                                    "coverText, topicBackground, topicText"}}},
                         {"title_font", stringProperty(QStringLiteral("Font family of the titles"))},
                         {"item_font", stringProperty(QStringLiteral("Font family of the items"))},
                         {"item_style", stringProperty(QStringLiteral("plain, pill, badge or label"))},
                         {"motif", stringProperty(QStringLiteral("Decoration, e.g. none or frame"))},
                         {"picture", stringProperty(QStringLiteral("Background of the list slides"))},
                         {"cover_picture", stringProperty(QStringLiteral("Background of the cover and topic slides"))},
                         {"logo", stringProperty(QStringLiteral("Logo picture"))},
                         {"logo_corner", stringProperty(QStringLiteral("top-left, top-right, bottom-left or bottom-right"))},
                         {"logo_size", QJsonObject{{"type", "integer"}, {"minimum", Pitch::kMinLogoSize}, {"maximum", Pitch::kMaxLogoSize},
                                                   {"description", "Percent of the slide's height"}}},
                         {"footer", stringProperty(QStringLiteral("Text at the foot of every slide"))},
                         {"numbers", QJsonObject{{"type", "boolean"}, {"description", "Slide numbers at the foot"}}}},
                        {}), false,
                 guarded([](const Access &allowed, const QJsonObject &args) {
                     const QString id = args.value(QStringLiteral("id")).toString();
                     std::optional<Pitch::Template> start;
                     if (id.isEmpty()) {
                         const QString base = args.value(QStringLiteral("base")).toString();
                         start = PitchTemplates::create(Pitch::themeFromKey(base));
                     } else if (!(start = PitchTemplates::find(id))) {
                         return ToolResult::error(QStringLiteral("No template %1 (list_pitch_templates lists them)").arg(id));
                     }
                     QJsonObject object = Pitch::toJson(*start);
                     QString error;
                     if (!applyArguments(args, object, &error)) return ToolResult::error(error);
                     const Pitch::Template value = *Pitch::templateFromJson(object);
                     if (!PitchTemplates::save(value, &error)) return ToolResult::error(QStringLiteral("Not saved: %1").arg(error));
                     return ToolResult::object(templateJson(value));
                 })});

    list.append({QStringLiteral("import_pitch_template"), QStringLiteral("Import a Pitch template"),
                 QStringLiteral("Adds a .mentis-theme file to the app's library (one with the same id is "
                                "replaced); returns the template."),
                 schema({{"file", stringProperty(QStringLiteral("Absolute path of the .mentis-theme file"))}}, {QStringLiteral("file")}), false,
                 guarded([](const Access &allowed, const QJsonObject &args) {
                     QString error;
                     const std::optional<QString> path = existingFile(args.value(QStringLiteral("file")).toString(), &error);
                     if (!path) return ToolResult::error(error);
                     const std::optional<Pitch::Template> value = PitchTemplates::readFile(*path, &error);
                     if (!value) return ToolResult::error(error);
                     if (!PitchTemplates::save(*value, &error)) return ToolResult::error(QStringLiteral("Not saved: %1").arg(error));
                     return ToolResult::object(templateJson(*value));
                 })});

    list.append({QStringLiteral("export_pitch_template"), QStringLiteral("Export a Pitch template"),
                 QStringLiteral("Writes a template of the library to a .mentis-theme file, to share it. An "
                                "existing file is replaced only after the user's confirmation."),
                 schema({{"id", idProperty}, {"file", stringProperty(QStringLiteral("Absolute path of the new .mentis-theme file"))},
                         {"confirm", kConfirm}},
                        {QStringLiteral("id"), QStringLiteral("file")}), false,
                 guarded([](const Access &allowed, const QJsonObject &args) {
                     const std::optional<Pitch::Template> value = PitchTemplates::find(args.value(QStringLiteral("id")).toString());
                     if (!value) return ToolResult::error(QStringLiteral("No template %1").arg(args.value(QStringLiteral("id")).toString()));
                     QString error;
                     bool exists = false;
                     const std::optional<QString> file = outputPath(args, QStringLiteral("file"), {QLatin1String(Pitch::kTemplateSuffix)}, &exists, &error);
                     if (!file) return ToolResult::error(error);
                     if (exists && !confirmed(allowed, args, QStringLiteral("overwrite"), *file, &error)) return ToolResult::error(error);
                     if (!PitchTemplates::writeFile(*value, *file, &error)) return ToolResult::error(QStringLiteral("Not written: %1").arg(error));
                     return ToolResult::object({{"file", QDir::toNativeSeparators(*file)}});
                 })});

    list.append({QStringLiteral("delete_pitch_template"), QStringLiteral("Delete a Pitch template"),
                 QStringLiteral("Deletes a template from the app's library, after the user's explicit confirmation (Mentis asks "
                                "when open; otherwise ask in the chat and pass confirm: true). Maps that use it keep their copy."),
                 schema({{"id", idProperty}, {"confirm", kConfirm}}, {QStringLiteral("id")}), false,
                 guarded([](const Access &allowed, const QJsonObject &args) {
                     const QString id = args.value(QStringLiteral("id")).toString();
                     const std::optional<Pitch::Template> value = PitchTemplates::find(id);
                     if (!value) return ToolResult::error(QStringLiteral("No template %1").arg(id));
                     QString error;
                     if (!confirmed(allowed, args, QStringLiteral("delete_pitch_template"), value->name, &error)) return ToolResult::error(error);
                     if (!PitchTemplates::remove(id)) return ToolResult::error(QStringLiteral("The template could not be deleted"));
                     return ToolResult::object({{"deleted", id}});
                 })});
    return list;
}

}  // namespace McpMapTools::Support
