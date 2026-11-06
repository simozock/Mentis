// FEAT-045 phase 3, stage E: the agents export maps (PNG, SVG, PDF, Word, Excel, Markdown),
// set the presentation (Pitch), and replace an existing file only after the user's
// explicit yes. A replaced map is written beside the old one first: the old file goes only once the
// new one is complete.
#include "mcptoolsupport.h"

#include "document.h"
#include "mapexporter.h"
#include "mindmapmodel.h"
#include "pitch.h"
#include "pitchtemplate.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLockFile>
#include <QUuid>

namespace McpMapTools::Support {

std::optional<QString> outputPath(const QJsonObject &args, const QString &key, const QStringList &suffixes,
                                  bool *exists, QString *error) {
    const std::optional<QString> raw = text(args, key, true, error, 4096);
    if (!raw) return std::nullopt;
    if (!QDir::isAbsolutePath(*raw)) { *error = QStringLiteral("%1 must be absolute").arg(key); return std::nullopt; }
    const QFileInfo info(*raw);
    if (!suffixes.contains(info.suffix().toLower())) {
        *error = QStringLiteral("%1 must end with .%2").arg(key, suffixes.join(QStringLiteral(" or .")));
        return std::nullopt;
    }
    const QString folder = QFileInfo(info.absolutePath()).canonicalFilePath();
    if (folder.isEmpty()) { *error = QStringLiteral("The folder of %1 does not exist").arg(QDir::toNativeSeparators(*raw)); return std::nullopt; }
    const QString canonical = folder + QLatin1Char('/') + info.fileName();
    if (info.exists() && !info.isFile()) { *error = QStringLiteral("%1 is a folder").arg(QDir::toNativeSeparators(*raw)); return std::nullopt; }
    *exists = info.exists();
    return canonical;
}

bool fileInUse(const QString &path) {
    QLockFile lock(path + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);
    if (lock.tryLock(0)) { lock.unlock(); return false; }
    return lock.error() == QLockFile::LockFailedError;
}

bool replaceWith(const QString &target, const std::function<bool(const QString &path, QString *error)> &write, QString *error) {
    if (!QFileInfo::exists(target)) return write(target, error);
    const QString backup = target + QStringLiteral(".replaced-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
    if (!QFile::rename(target, backup)) { *error = QStringLiteral("%1 cannot be replaced").arg(QDir::toNativeSeparators(target)); return false; }
    if (!write(target, error)) {
        QFile::remove(target);          // a part written before the error
        QFile::rename(backup, target);  // the old file comes back as it was
        return false;
    }
    QFile::remove(backup);
    return true;
}

namespace {

const QStringList kFormats{QStringLiteral("png"), QStringLiteral("svg"), QStringLiteral("pdf"), QStringLiteral("docx"),
                           QStringLiteral("xlsx"), QStringLiteral("md")};

std::optional<QString> pitchKey(const QJsonObject &args, const QString &key, const std::function<QString(const QString &)> &roundTrip,
                                QString *error) {
    if (!args.contains(key)) return std::nullopt;
    const QString value = args.value(key).toString();
    if (!value.isEmpty() && roundTrip(value) != value) { *error = QStringLiteral("%1: unknown value %2 (list_options lists them)").arg(key, value); return std::nullopt; }
    return value;  // empty = Auto / the default
}

}  // namespace

QJsonObject pitchOptions() {
    QStringList themes, aspects, visibility, deliveries, layouts;
    for (int i = 0; i < 32; ++i) {
        const auto add = [i](QStringList &list, const QString &key) { if (!key.isEmpty() && !list.contains(key)) list.append(key); };
        add(themes, Pitch::themeKey(Pitch::Theme(i)));
        add(aspects, Pitch::aspectKey(Pitch::Aspect(i)));
        add(visibility, Pitch::visibilityKey(Pitch::Visibility(i)));
        add(deliveries, Pitch::deliveryKey(Pitch::Delivery(i)));
        add(layouts, Pitch::layoutKey(Pitch::Layout(i)));
    }
    return {{"pitch_theme", QJsonArray::fromStringList(themes)}, {"pitch_aspect", QJsonArray::fromStringList(aspects)},
            {"pitch_slide", QJsonArray::fromStringList(visibility)}, {"pitch_delivery", QJsonArray::fromStringList(deliveries)},
            {"pitch_layout", QJsonArray::fromStringList(layouts)}};
}

void appendExportEdits(QList<MapEdit> &edits) {
    edits.append({{QStringLiteral("export_map"), QStringLiteral("Export the map"),
                   QStringLiteral("Exports to a file, by its extension: .png, .svg, .pdf (the sheet, or every "
                                  "sheet with all_sheets), .docx and .xlsx (every sheet), .md (the sheet, in a folder with its "
                                  "pictures). An existing file is replaced only after the user's confirmation."),
                   schema({{"sheet", kSheet}, {"file", stringProperty(QStringLiteral("Absolute path of the new file"))},
                           {"all_sheets", QJsonObject{{"type", "boolean"}, {"description", "PDF only"}}}, {"confirm", kConfirm}},
                          {QStringLiteral("file")}), false, {}},
                  false,
                  [](const Access &access, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &) {
                      QString error;
                      bool exists = false;
                      std::optional<QString> file = outputPath(args, QStringLiteral("file"), kFormats, &exists, &error);
                      if (!file) return ToolResult::error(error);
                      const QString format = QFileInfo(*file).suffix().toLower();
                      if (format == QLatin1String("md")) {
                          *file = Document::exportMarkdownPath(*file);  // Name.md -> Name/Name.md, with assets/
                          exists = QFileInfo::exists(*file);
                      }
                      if (exists && !confirmed(access, args, QStringLiteral("overwrite"), *file, &error)) return ToolResult::error(error);
                      const QString title = document.sheets().value(document.currentSheet()).name;
                      if (format == QLatin1String("docx") || format == QLatin1String("xlsx")) {
                          const Document::ExportResult result = document.exportOffice(
                              *file, format == QLatin1String("docx") ? Document::OfficeFormat::Word : Document::OfficeFormat::Excel, title);
                          if (!result.ok) return ToolResult::error(result.error);
                      } else if (format == QLatin1String("md")) {
                          const Document::ExportResult result = document.exportMarkdown(*file);
                          if (!result.ok) return ToolResult::error(result.error);
                      } else {
                          QList<MapExporter::Page> pages;
                          const bool every = format == QLatin1String("pdf") && args.value(QStringLiteral("all_sheets")).toBool();
                          for (int i = 0; i < document.sheets().size(); ++i) {
                              if (!every && i != document.currentSheet()) continue;
                              if (const Document::Result loaded = document.ensureSheetLoaded(i); !loaded.ok) return ToolResult::error(loaded.error);
                              pages.append({document.sheets()[i].name, document.sheetContent(i), QString()});
                          }
                          const MapExporter::Result result = format == QLatin1String("pdf")
                              ? MapExporter::writePdf(MapExporter::PagePlan(pages, MapExporter::PageOptions()), *file, title, false)
                              : MapExporter::write(format == QLatin1String("png") ? MapExporter::Format::Png : MapExporter::Format::Svg,
                                                   pages, *file, MapExporter::Options());
                          if (!result.ok) return ToolResult::error(result.error);
                      }
                      return ToolResult::object({{"file", QDir::toNativeSeparators(*file)}});
                  }});

    edits.append({{QStringLiteral("set_pitch"), QStringLiteral("Set the presentation"),
                   QStringLiteral("Sets the Pitch of the sheet (theme or template, aspect) and, with id, of one topic (its own slide, "
                                  "the list slide of its children, how the points appear, the layout). Empty = Auto; list_options lists "
                                  "values. A theme replaces the sheet's template; template = an id of list_pitch_templates (the sheet "
                                  "keeps a copy), empty = none."),
                   schema({{"sheet", kSheet}, {"theme", stringProperty(QStringLiteral("Sheet: theme"))},
                           {"template", stringProperty(QStringLiteral("Sheet: Pitch template id, empty = none"))},
                           {"aspect", stringProperty(QStringLiteral("Sheet: aspect ratio"))},
                           {"id", stringProperty(QStringLiteral("A topic, for the settings below"))},
                           {"topic_slide", stringProperty(QStringLiteral("Topic: its own slide"))},
                           {"list_slide", stringProperty(QStringLiteral("Topic: a slide listing its children"))},
                           {"delivery", stringProperty(QStringLiteral("Topic: how the points appear"))},
                           {"layout", stringProperty(QStringLiteral("Topic: layout of its slide"))}},
                          {}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      const auto theme = pitchKey(args, QStringLiteral("theme"), [](const QString &k) { return Pitch::themeKey(Pitch::themeFromKey(k)); }, &error);
                      const auto aspect = pitchKey(args, QStringLiteral("aspect"), [](const QString &k) { return Pitch::aspectKey(Pitch::aspectFromKey(k)); }, &error);
                      const auto slide = pitchKey(args, QStringLiteral("topic_slide"), [](const QString &k) { return Pitch::visibilityKey(Pitch::visibilityFromKey(k)); }, &error);
                      const auto list = pitchKey(args, QStringLiteral("list_slide"), [](const QString &k) { return Pitch::visibilityKey(Pitch::visibilityFromKey(k)); }, &error);
                      const auto delivery = pitchKey(args, QStringLiteral("delivery"), [](const QString &k) { return Pitch::deliveryKey(Pitch::deliveryFromKey(k)); }, &error);
                      const auto layout = pitchKey(args, QStringLiteral("layout"), [](const QString &k) { return Pitch::layoutKey(Pitch::layoutFromKey(k)); }, &error);
                      if (!error.isEmpty()) return ToolResult::error(error);
                      const bool topicPart = slide || list || delivery || layout;
                      // FEAT-089: the sheet's template copy (nullopt = unchanged); its base theme becomes the sheet's.
                      std::optional<QJsonObject> copy;
                      std::optional<QString> sheetTheme = theme;
                      if (args.contains(QStringLiteral("template"))) {
                          if (theme) return ToolResult::error(QStringLiteral("Give theme or template, not both"));
                          const QString id = args.value(QStringLiteral("template")).toString();
                          copy = QJsonObject();
                          if (!id.isEmpty()) {
                              const std::optional<Pitch::Template> value = PitchTemplates::find(id);
                              if (!value) return ToolResult::error(QStringLiteral("No template %1 (list_pitch_templates lists them)").arg(id));
                              copy = Pitch::toJson(*value);
                              sheetTheme = Pitch::themeKey(value->base);
                          }
                      } else if (theme) {
                          copy = QJsonObject();
                      }
                      if (!theme && !aspect && !copy && !topicPart) return ToolResult::error(QStringLiteral("Nothing to change"));
                      MindNode *node = nullptr;
                      if (topicPart && !(node = topic(model, args, QStringLiteral("id"), &error))) return ToolResult::error(error);
                      if (sheetTheme || aspect || copy)
                          document.setPitchSheet(sheetTheme.value_or(model.pitchTheme), aspect.value_or(model.pitchAspect), copy);
                      using Field = Document::PitchField;
                      if (slide) document.setPitchTopic(node, Field::TopicSlide, *slide);
                      if (list) document.setPitchTopic(node, Field::ListSlide, *list);
                      if (delivery) document.setPitchTopic(node, Field::Delivery, *delivery);
                      if (layout) document.setPitchTopic(node, Field::Layout, *layout);
                      const std::optional<Pitch::Template> applied = Pitch::sheetTemplate(model);
                      return ToolResult::object({{"theme", model.pitchTheme}, {"aspect", model.pitchAspect},
                                                 {"template", applied ? QJsonValue(applied->id) : QJsonValue()}});
                  }});
}

}  // namespace McpMapTools::Support
