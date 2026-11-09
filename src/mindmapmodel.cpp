#include "mindmapmodel.h"
#include "markdownshortcuts.h"
#include "markers.h"
#include "nodeshape.h"
#include "markdownimport.h"
#include "pitch.h"
#include "pitchtemplate.h"
#include "richresources.h"
#include "richtitle.h"
#include "latexrenderer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>
#include <QFileInfo>
#include <QSettings>
#include <algorithm>
#include <cmath>
#include <functional>

namespace Labels {
bool same(const QString &a, const QString &b) { return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0; }

QStringList normalized(const QStringList &labels, bool sorted) {
    QStringList result;
    for (const QString &label : labels) {
        const QString clean = label.simplified();
        if (clean.isEmpty()) continue;
        if (std::none_of(result.cbegin(), result.cend(), [&clean](const QString &kept) { return same(kept, clean); })) result.append(clean);
    }
    // Case ignored first: under the C locale (Linux CI) localeAwareCompare put "Zeta" before "alfa".
    if (sorted) std::sort(result.begin(), result.end(), [](const QString &a, const QString &b) {
        const int folded = a.toLower().localeAwareCompare(b.toLower());
        return folded != 0 ? folded < 0 : a.localeAwareCompare(b) < 0;
    });
    return result;
}

bool autoSort() { return QSettings().value(QStringLiteral("labels/autoSort"), true).toBool(); }
void setAutoSort(bool on) { QSettings().setValue(QStringLiteral("labels/autoSort"), on); }
}  // namespace Labels

qreal sanitizedManualNodeWidth(double widthPx) {
    if (!std::isfinite(widthPx) || widthPx <= 0) return 0;
    return std::min<qreal>(widthPx, kMaxManualNodeWidthPx);
}

namespace {
std::unique_ptr<MindNode> makeNode(const QString &title, MindNode *parent = nullptr) {
    auto node = std::make_unique<MindNode>();
    node->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    node->title = title;
    node->parent = parent;
    return node;
}

int rowOf(const MindNode *node) {
    if (!node || !node->parent) return 0;
    const auto &siblings = node->parent->children;
    for (int i = 0; i < static_cast<int>(siblings.size()); ++i)
        if (siblings[i].get() == node) return i;
    return -1;
}

// Valori di un nodo che finiscono nei suoi campi JSON (file .mentis e istantanea dello storico).
// Sono l'unico ingresso di fieldsToJson, cosi' due nodi con valori uguali hanno per forza campi
// uguali: la cache di toFlatSnapshot si fida di questo confronto. Un campo nuovo del file va
// aggiunto qui; il confronto (operator== predefinito) lo include da solo. Coordinate e misure
// sono double e non QPointF/QSizeF, il cui operator== e' approssimato (qFuzzyCompare).
// FEAT-075: a topic's planning data in the form of the "gantt" key and of GanttPlan's tasks;
// defaults left out, so a topic never planned writes nothing.
QJsonObject ganttTopicJson(const MindNode *node) {
    QJsonObject gantt;
    if (node->ganttStart.isValid()) gantt.insert(QLatin1StringView("start"), node->ganttStart.toString(Qt::ISODate));
    if (node->ganttDuration != 1) gantt.insert(QLatin1StringView("duration"), node->ganttDuration);
    if (node->ganttProgress != 0) gantt.insert(QLatin1StringView("progress"), node->ganttProgress);
    if (node->ganttPriority != 0) gantt.insert(QLatin1StringView("priority"), node->ganttPriority);  // FEAT-078
    if (!node->ganttPeople.isEmpty()) {  // FEAT-081; FEAT-083: {"id", "percent"} when not 100 %
        QJsonArray people;
        for (const QString &id : node->ganttPeople) {
            const int percent = node->percentOf(id);
            if (percent == MindNode::kMaxPercent) people.append(id);
            else people.append(QJsonObject{{QLatin1StringView("id"), id}, {QLatin1StringView("percent"), percent}});
        }
        gantt.insert(QLatin1StringView("people"), people);
    }
    if (node->ganttLabel) gantt.insert(QLatin1StringView("label"), *node->ganttLabel);
    if (!node->ganttShape.isEmpty()) gantt.insert(QLatin1StringView("shape"), node->ganttShape);  // FEAT-080
    if (!node->ganttFill.isEmpty()) gantt.insert(QLatin1StringView("fill"), node->ganttFill);
    if (!node->ganttBorder.isEmpty()) gantt.insert(QLatin1StringView("border"), node->ganttBorder);
    return gantt;
}

struct NodeFieldValues {
    QString id;
    QString title;
    QString titleHtml;
    QString notes;
    QString notesHtml;  // FEAT-059
    double x = 0;
    double y = 0;
    bool floating = false;
    QString imagePath;
    QString imageAssetId;
    QString imageOriginalName;
    int imagePlacement = 0;
    QString topicLink;  // FEAT-027
    QString webLink;
    double imageWidth = 0;
    double imageHeight = 0;
    double manualDx = 0;
    double manualDy = 0;
    double width = 0;
    // FEAT-040: aspetto dell'immagine; chiavi scritte solo se diverse dal predefinito.
    int imageBorder = 0;
    double imageBorderWidth = ImageStyle::kDefaultBorderWidthPx;
    QString imageBorderColor;
    bool imageShadow = false;
    int imageOpacity = ImageStyle::kMaxOpacity;
    bool imageOutline = false;  // FEAT-051
    int imageRotation = 0;
    int task = 0;  // FEAT-048: MindNode::Task
    QStringList labels;  // FEAT-060
    QList<NodeFile> files;  // FEAT-049
    QString formula;        // FEAT-064
    QString formulaBackground;
    bool imageSticker = false;  // FEAT-051
    QStringList markers;
    QString side;                // FEAT-012: "left", "right" o vuoto (automatico)
    QString collapsed;           // FEAT-016: "yes" o vuoto
    QString structure;           // FEAT-026
    QString structureDirection;
    NodeStyleOverrides style;
    int stackOrder = 0;          // FEAT-057: "stack", absent = 0
    QString pitchTopicSlide;     // FEAT-063: absent = Auto / default
    QString pitchListSlide;
    QString pitchDelivery;
    QString pitchLayout;
    QJsonObject gantt;           // FEAT-075: absent = no planning data
    bool operator==(const NodeFieldValues &) const = default;
};

NodeFieldValues fieldValues(const MindNode *node) {
    return {node->id, node->title, node->titleHtml, node->notes, node->notesHtml,
            node->position.x(), node->position.y(), node->floating,
            node->imagePath, node->imageAssetId, node->imageOriginalName, static_cast<int>(node->imagePlacement),
            node->topicLink, node->webLink,
            node->imageSize.width(), node->imageSize.height(),
            node->manualOffset.x(), node->manualOffset.y(), node->customWidth,
            static_cast<int>(node->imageStyle.border), node->imageStyle.borderWidth,
            node->imageStyle.borderColor.name(QColor::HexArgb), node->imageStyle.shadow, node->imageStyle.opacity,
            node->imageStyle.outline, node->imageStyle.rotation,
            static_cast<int>(node->task), node->labels, node->files, node->formula,
            node->formula.isEmpty() || node->formulaBackground == QColor(Qt::white) ? QString() : node->formulaBackground.name(QColor::HexArgb),
            node->imageSticker && !node->imagePath.isEmpty(), node->markers,
            node->side == MindNode::Side::Left ? QStringLiteral("left") : node->side == MindNode::Side::Right ? QStringLiteral("right") : QString(),
            node->parent && node->collapsed ? QStringLiteral("yes") : QString(),
            node->structure, node->structureDirection, node->style, node->stackOrder,
            node->pitchTopicSlide, node->pitchListSlide, node->pitchDelivery, node->pitchLayout,
            ganttTopicJson(node)};
}

// Campi del singolo nodo, senza figli: condivisi da nodeToJson (file .mentis) e
// dall'istantanea piatta dello storico, così l'elenco dei campi resta uno solo.
// Con `previous` (i valori da cui e' stato scritto `fields`) si scrivono solo i campi il cui valore
// e' diverso: e' l'aggiornamento della cache di toFlatSnapshot. Il risultato e' lo stesso di una
// scrittura completa perche' QJsonObject tiene le chiavi in ordine, qualunque sia l'ordine degli
// inserimenti (verificato da history_snapshot_test).
void writeFields(QJsonObject &fields, const NodeFieldValues &values, const NodeFieldValues *previous = nullptr) {
    // Chiavi QLatin1StringView: QJsonObject::insert le usa senza convertirle in QString (prima
    // ogni chiave allocava una QString nuova, 20 e piu' per nodo a ogni passo di storico; misura in
    // FEAT-022). Contenuto identico a prima.
    auto put = [&fields, &values, previous](const char *key, auto member, auto &&value) {
        if (!previous || previous->*member != values.*member) fields.insert(QLatin1StringView(key), QJsonValue(value));
    };
    put("id", &NodeFieldValues::id, values.id);
    put("title", &NodeFieldValues::title, values.title);
    put("notes", &NodeFieldValues::notes, values.notes);
    put("x", &NodeFieldValues::x, values.x);
    put("floating", &NodeFieldValues::floating, values.floating);
    put("y", &NodeFieldValues::y, values.y);
    put("image", &NodeFieldValues::imagePath, values.imagePath);
    put("imageAssetId", &NodeFieldValues::imageAssetId, values.imageAssetId);
    put("imageOriginalName", &NodeFieldValues::imageOriginalName, values.imageOriginalName);
    put("imagePlacement", &NodeFieldValues::imagePlacement, values.imagePlacement);
    put("imageWidth", &NodeFieldValues::imageWidth, values.imageWidth);
    put("imageHeight", &NodeFieldValues::imageHeight, values.imageHeight);
    // B7: sempre presenti (anche a 0) perché lo storico registra solo i campi presenti.
    put("manualDx", &NodeFieldValues::manualDx, values.manualDx);
    put("manualDy", &NodeFieldValues::manualDy, values.manualDy);
    // Sempre presente per lo stesso motivo (0 = larghezza automatica), 2026-10-13.
    put("width", &NodeFieldValues::width, values.width);
    // Chiavi presenti solo se non vuote: titolo formattato e collegamenti (FEAT-027).
    auto putIfAny = [&fields, &values, previous](const char *key, QString NodeFieldValues::*member) {
        if (previous && previous->*member == values.*member) return;
        if (!(values.*member).isEmpty()) fields.insert(QLatin1StringView(key), values.*member);
        else fields.remove(QLatin1StringView(key));
    };
    putIfAny("titleHtml", &NodeFieldValues::titleHtml);
    putIfAny("notesHtml", &NodeFieldValues::notesHtml);  // FEAT-059
    putIfAny("topicLink", &NodeFieldValues::topicLink);
    putIfAny("webLink", &NodeFieldValues::webLink);
    putIfAny("formula", &NodeFieldValues::formula);  // FEAT-064
    putIfAny("side", &NodeFieldValues::side);  // FEAT-012
    putIfAny("collapsed", &NodeFieldValues::collapsed);  // FEAT-016
    putIfAny("structure", &NodeFieldValues::structure);  // FEAT-026
    putIfAny("structureDirection", &NodeFieldValues::structureDirection);
    putIfAny("formulaBackground", &NodeFieldValues::formulaBackground);  // assente = bianco
    putIfAny("pitchTopicSlide", &NodeFieldValues::pitchTopicSlide);  // FEAT-063
    putIfAny("pitchListSlide", &NodeFieldValues::pitchListSlide);
    putIfAny("pitchDelivery", &NodeFieldValues::pitchDelivery);
    putIfAny("pitchLayout", &NodeFieldValues::pitchLayout);
    // FEAT-040: presenti solo se diversi dal predefinito; una chiave tolta e' registrata dallo
    // storico come Null (B12), come per lo stile.
    static const NodeFieldValues defaults = [] {
        NodeFieldValues values;
        values.imageBorderColor = ImageStyle().borderColor.name(QColor::HexArgb);
        return values;
    }();
    auto putIfChanged = [&fields, &values, previous](const char *key, auto member) {
        if (previous && previous->*member == values.*member) return;
        if (values.*member != defaults.*member) fields.insert(QLatin1StringView(key), QJsonValue(values.*member));
        else fields.remove(QLatin1StringView(key));
    };
    putIfChanged("imageBorder", &NodeFieldValues::imageBorder);
    putIfChanged("imageBorderWidth", &NodeFieldValues::imageBorderWidth);
    putIfChanged("imageBorderColor", &NodeFieldValues::imageBorderColor);
    putIfChanged("imageShadow", &NodeFieldValues::imageShadow);
    putIfChanged("imageOpacity", &NodeFieldValues::imageOpacity);
    putIfChanged("imageOutline", &NodeFieldValues::imageOutline);
    putIfChanged("imageRotation", &NodeFieldValues::imageRotation);
    // FEAT-051: adesivo e marcatori; assenti = no.
    if (!previous || previous->imageSticker != values.imageSticker) {
        if (values.imageSticker) fields.insert(QLatin1StringView("imageSticker"), true);
        else fields.remove(QLatin1StringView("imageSticker"));
    }
    if (!previous || previous->markers != values.markers) {
        if (values.markers.isEmpty()) fields.remove(QLatin1StringView("markers"));
        else fields.insert(QLatin1StringView("markers"), QJsonArray::fromStringList(values.markers));
    }
    // FEAT-060: etichette; assenti = nessuna.
    if (!previous || previous->labels != values.labels) {
        if (values.labels.isEmpty()) fields.remove(QLatin1StringView("labels"));
        else fields.insert(QLatin1StringView("labels"), QJsonArray::fromStringList(values.labels));
    }
    // FEAT-049: file allegati; assenti = nessuno. Nel .mentis BmapStorage sostituisce path con l'asset.
    if (!previous || previous->files != values.files) {
        if (values.files.isEmpty()) fields.remove(QLatin1StringView("files"));
        else {
            QJsonArray files;
            for (const NodeFile &file : values.files) {
                QJsonObject entry{{QStringLiteral("name"), file.name}, {QStringLiteral("size"), double(file.size)}};
                if (!file.path.isEmpty()) entry.insert(QStringLiteral("path"), file.path);
                if (!file.assetId.isEmpty()) entry.insert(QStringLiteral("assetId"), file.assetId);
                files.append(entry);
            }
            fields.insert(QLatin1StringView("files"), files);
        }
    }
    // FEAT-057: stacking rank among the children of the central topic; absent = 0.
    if (!previous || previous->stackOrder != values.stackOrder) {
        if (values.stackOrder == 0) fields.remove(QLatin1StringView("stack"));
        else fields.insert(QLatin1StringView("stack"), values.stackOrder);
    }
    // FEAT-075: planning data of the Gantt view; absent = no date, 1 day, 0%.
    if (!previous || previous->gantt != values.gantt) {
        if (values.gantt.isEmpty()) fields.remove(QLatin1StringView("gantt"));
        else fields.insert(QLatin1StringView("gantt"), values.gantt);
    }
    // FEAT-048: "open" o "done"; assente = nessuna casella.
    if (!previous || previous->task != values.task) {
        if (values.task == static_cast<int>(MindNode::Task::None)) fields.remove(QLatin1StringView("task"));
        else fields.insert(QLatin1StringView("task"), values.task == static_cast<int>(MindNode::Task::Done) ? QStringLiteral("done") : QStringLiteral("open"));
    }
    // FEAT-019: solo i valori di stile scelti a mano (stesse chiavi di prima, quindi i file
    // precedenti si leggono come valori scelti a mano). Un campo che sparisce e' registrato
    // dallo storico come Null (B12, MapHistory::commit).
    auto putStyle = [&fields, &values, previous](const char *key, auto member, auto convert) {
        const auto &value = values.style.*member;
        if (previous && previous->style.*member == value) return;
        if (value) fields.insert(QLatin1StringView(key), QJsonValue(convert(*value)));
        else fields.remove(QLatin1StringView(key));
    };
    const auto color = [](const QColor &value) { return value.name(QColor::HexArgb); };
    const auto same = [](const auto &value) { return value; };
    putStyle("fill", &NodeStyleOverrides::fill, color);
    putStyle("text", &NodeStyleOverrides::text, color);
    putStyle("border", &NodeStyleOverrides::border, color);
    putStyle("branchColor", &NodeStyleOverrides::branchColor, color);
    putStyle("branchWidth", &NodeStyleOverrides::branchWidth, same);
    // FEAT-022: testo del nodo, solo se scelto a mano.
    putStyle("fontFamily", &NodeStyleOverrides::fontFamily, same);
    putStyle("fontSize", &NodeStyleOverrides::fontSize, same);
    putStyle("fontWeight", &NodeStyleOverrides::fontWeight, same);
    putStyle("italic", &NodeStyleOverrides::italic, same);
    putStyle("underline", &NodeStyleOverrides::underline, same);
    putStyle("strikeOut", &NodeStyleOverrides::strikeOut, same);
    putStyle("textCase", &NodeStyleOverrides::textCase, NodeStyle::textCaseKey);
    putStyle("textAlign", &NodeStyleOverrides::textAlign, NodeStyle::textAlignKey);
    putStyle("branchShape", &NodeStyleOverrides::branchShape, NodeStyle::branchShapeKey);  // FEAT-072
    putStyle("branchStart", &NodeStyleOverrides::branchStart, NodeStyle::branchEndKey);  // FEAT-023
    putStyle("branchEnd", &NodeStyleOverrides::branchEnd, NodeStyle::branchEndKey);
    putStyle("numbering", &NodeStyleOverrides::numbering, Numbering::toJson);  // FEAT-024
    putStyle("branchStyle", &NodeStyleOverrides::branchStyle, [](Qt::PenStyle value) { return static_cast<int>(value); });
    // FEAT-020: chiavi nuove, solo se scelte a mano; i valori sono nomi (non numeri) cosi' le
    // parti successive possono aggiungerne senza cambiare quelli esistenti.
    putStyle("preset", &NodeStyleOverrides::preset, NodeStyle::presetKey);
    putStyle("fillStyle", &NodeStyleOverrides::fillStyle, NodeStyle::fillStyleKey);
    putStyle("borderStyle", &NodeStyleOverrides::borderStyle, NodeStyle::borderStyleKey);
    putStyle("borderWidth", &NodeStyleOverrides::borderWidth, same);
    putStyle("shape", &NodeStyleOverrides::shape, NodeStyle::shapeKey);
}

QJsonObject fieldsToJson(const NodeFieldValues &values) {
    QJsonObject fields;
    writeFields(fields, values);
    return fields;
}

QJsonObject nodeFields(const MindNode *node) { return fieldsToJson(fieldValues(node)); }

QJsonObject nodeToJson(const MindNode *node) {
    QJsonObject out = nodeFields(node);
    QJsonArray children;
    for (const auto &child : node->children) children.append(nodeToJson(child.get()));
    out["children"] = children;
    return out;
}

// Campi di un nodo letti dal JSON (stessi nomi di nodeFields), senza id, genitore e figli.
// Separata da nodeFromJson (P3, 2026-10-13) per poterla applicare anche a un nodo esistente
// nell'annulla incrementale. Le righe sono quelle che prima stavano in nodeFromJson.
void applyNodeFields(MindNode *node, const QJsonObject &in) {
    node->title = in["title"].toString(QObject::tr("Topic"));
    node->titleHtml = RichResources::withoutExternal(in["titleHtml"].toString());  // BUG-044
    node->topicLink = in["topicLink"].toString();  // FEAT-027: assenti = nessun collegamento
    node->webLink = in["webLink"].toString();
    node->notes = in["notes"].toString();
    node->notesHtml = node->notes.isEmpty() ? QString() : RichResources::withoutExternal(in["notesHtml"].toString());  // FEAT-059, BUG-044
    // FEAT-019: valori di stile scelti a mano; assenti o non validi = ricavati dalla cascata.
    auto color = [&in](const char *key) -> std::optional<QColor> {
        const QColor value(in[QLatin1String(key)].toString());
        return value.isValid() ? std::optional<QColor>(value) : std::nullopt;
    };
    NodeStyleOverrides style;
    style.fill = color("fill");
    style.text = color("text");
    style.border = color("border");
    style.branchColor = color("branchColor");
    if (const QJsonValue width = in["branchWidth"]; width.isDouble() && NodeStyle::isValidValue(StyleProperty::BranchWidth, width.toDouble()))
        style.branchWidth = width.toDouble();
    if (const QJsonValue pen = in["branchStyle"]; pen.isDouble() && NodeStyle::isValidValue(StyleProperty::BranchStyle, pen.toInt()))
        style.branchStyle = static_cast<Qt::PenStyle>(pen.toInt());
    // FEAT-020: nomi sconosciuti o spessori fuori intervallo = non scelti a mano.
    style.preset = NodeStyle::presetFromKey(in["preset"].toString());
    style.fillStyle = NodeStyle::fillStyleFromKey(in["fillStyle"].toString());
    style.borderStyle = NodeStyle::borderStyleFromKey(in["borderStyle"].toString());
    style.shape = NodeStyle::shapeFromKey(in["shape"].toString());
    if (const QJsonValue width = in["borderWidth"]; width.isDouble() && NodeStyle::isValidValue(StyleProperty::BorderWidth, width.toDouble()))
        style.borderWidth = width.toDouble();
    // FEAT-022: valori non validi = non scelti a mano.
    auto valid = [&in](const char *key, StyleProperty property) -> std::optional<QVariant> {
        const QVariant value = in[QLatin1String(key)].toVariant();
        return NodeStyle::isValidValue(property, value) ? std::optional<QVariant>(value) : std::nullopt;
    };
    if (const auto value = valid("fontFamily", StyleProperty::FontFamily); value && !value->toString().trimmed().isEmpty())
        style.fontFamily = value->toString().trimmed();
    if (const auto value = valid("fontSize", StyleProperty::FontSize)) style.fontSize = value->toDouble();
    if (const auto value = valid("fontWeight", StyleProperty::FontWeight)) style.fontWeight = value->toInt();
    if (const auto value = valid("italic", StyleProperty::Italic)) style.italic = value->toBool();
    if (const auto value = valid("underline", StyleProperty::Underline)) style.underline = value->toBool();
    if (const auto value = valid("strikeOut", StyleProperty::StrikeOut)) style.strikeOut = value->toBool();
    style.textCase = NodeStyle::textCaseFromKey(in["textCase"].toString());
    style.textAlign = NodeStyle::textAlignFromKey(in["textAlign"].toString());
    style.branchShape = NodeStyle::branchShapeFromKey(in["branchShape"].toString());
    style.branchStart = NodeStyle::branchEndFromKey(in["branchStart"].toString());  // FEAT-023
    style.branchEnd = NodeStyle::branchEndFromKey(in["branchEnd"].toString());
    style.numbering = Numbering::fromJson(in["numbering"]);  // FEAT-024; not valid = none
    node->style = style;
    node->floating = in["floating"].toBool(false);
    node->position = {in["x"].toDouble(), in["y"].toDouble()};
    node->manualOffset = {in["manualDx"].toDouble(), in["manualDy"].toDouble()};  // B7; assenti nei file vecchi = 0
    const QString side = in["side"].toString();  // FEAT-012
    node->side = side == QLatin1String("left") ? MindNode::Side::Left : side == QLatin1String("right") ? MindNode::Side::Right : MindNode::Side::Auto;
    const QString collapsed = in["collapsed"].toString();  // FEAT-016
    node->collapsed = collapsed == QLatin1String("yes");
    node->structure = in["structure"].toString();  // FEAT-026
    node->structureDirection = in["structureDirection"].toString();
    node->customWidth = sanitizedManualNodeWidth(in["width"].toDouble(0));  // assente nei file vecchi = automatica
    node->imagePath = in["image"].toString();
    node->imageAssetId = in["imageAssetId"].toString();
    node->imageOriginalName = in["imageOriginalName"].toString();
    node->imagePlacement = static_cast<MindNode::ImagePlacement>(in["imagePlacement"].toInt(static_cast<int>(MindNode::ImagePlacement::Above)));
    node->imageSize = {in["imageWidth"].toDouble(180), in["imageHeight"].toDouble(120)};
    // FEAT-040: chiavi assenti o non valide = predefinito.
    ImageStyle look;
    if (const int border = in["imageBorder"].toInt(-1); border >= 0 && border <= static_cast<int>(ImageStyle::Border::Dotted))
        look.border = static_cast<ImageStyle::Border>(border);
    if (const double width = in["imageBorderWidth"].toDouble(-1); width >= NodeStyle::kMinBorderWidthPx && width <= NodeStyle::kMaxBorderWidthPx)
        look.borderWidth = width;
    if (const QColor color(in["imageBorderColor"].toString()); color.isValid()) look.borderColor = color;
    look.shadow = in["imageShadow"].toBool(false);
    look.opacity = std::clamp(in["imageOpacity"].toInt(ImageStyle::kMaxOpacity), ImageStyle::kMinOpacity, ImageStyle::kMaxOpacity);
    look.outline = in["imageOutline"].toBool(false);
    if (const int rotation = in["imageRotation"].toInt(0); rotation == 90 || rotation == 180 || rotation == 270) look.rotation = rotation;
    node->imageStyle = look;
    QStringList labels;  // FEAT-060
    for (const QJsonValue &label : in["labels"].toArray()) labels.append(label.toString());
    node->labels = Labels::normalized(labels, false);
    node->imageSticker = in["imageSticker"].toBool(false);  // FEAT-051
    node->markers.clear();
    for (const QJsonValue &marker : in["markers"].toArray())
        if (Markers::isValid(marker.toString())) node->markers = Markers::with(node->markers, Markers::seriesOf(marker.toString()), marker.toString());
    node->formula = in["formula"].toString();  // FEAT-064
    const QColor formulaBackground(in["formulaBackground"].toString());
    node->formulaBackground = formulaBackground.isValid() ? formulaBackground : QColor(Qt::white);
    node->files.clear();  // FEAT-049
    for (const QJsonValue &value : in["files"].toArray()) {
        const QJsonObject entry = value.toObject();
        NodeFile file{entry["path"].toString(), entry["assetId"].toString(), entry["name"].toString(),
                      qint64(entry["size"].toDouble())};
        if (file.name.isEmpty()) file.name = QFileInfo(file.path).fileName();
        if (!file.path.isEmpty() || !file.assetId.isEmpty()) node->files.append(file);
    }
    // FEAT-075: values out of range are left to GanttPlan's checks (the Gantt view repairs them).
    const QJsonObject gantt = in["gantt"].toObject();
    node->ganttStart = QDate::fromString(gantt["start"].toString(), Qt::ISODate);
    node->ganttDuration = std::clamp(gantt["duration"].toInt(1), GanttPlan::kMinDuration, GanttPlan::kMaxDuration);
    node->ganttProgress = std::clamp(gantt["progress"].toInt(0), 0, GanttPlan::kMaxProgress);
    node->ganttPriority = std::clamp(gantt["priority"].toInt(0), 0, MindNode::kMaxPriority);  // FEAT-078
    node->ganttPeople.clear();  // FEAT-081: ids, duplicates and empty entries dropped
    node->ganttPercent.clear();  // FEAT-083: an id alone = 100 %; a percentage out of range is clamped
    for (const QJsonValue &person : gantt["people"].toArray()) {
        const QString id = person.isObject() ? person.toObject().value(QLatin1StringView("id")).toString() : person.toString();
        if (id.isEmpty() || node->ganttPeople.contains(id)) continue;
        node->ganttPeople.append(id);
        if (person.isObject()) {
            const int percent = std::clamp(person.toObject().value(QLatin1StringView("percent")).toInt(MindNode::kMaxPercent),
                                           MindNode::kMinPercent, MindNode::kMaxPercent);
            if (percent != MindNode::kMaxPercent) node->ganttPercent.insert(id, percent);
        }
    }
    node->ganttLabel = gantt["label"].isBool() ? std::optional<bool>(gantt["label"].toBool()) : std::nullopt;
    node->ganttShape = gantt["shape"].toString();  // FEAT-080
    node->ganttFill = gantt["fill"].toString();
    node->ganttBorder = gantt["border"].toString();
    const QString task = in["task"].toString();  // FEAT-048
    node->task = task == QLatin1String("done") ? MindNode::Task::Done : task == QLatin1String("open") ? MindNode::Task::Open : MindNode::Task::None;
    node->stackOrder = in["stack"].toInt(0);  // FEAT-057
    // FEAT-063: unknown values = Auto / default.
    node->pitchTopicSlide = Pitch::validVisibility(in["pitchTopicSlide"].toString());
    node->pitchListSlide = Pitch::validVisibility(in["pitchListSlide"].toString());
    node->pitchDelivery = Pitch::validDelivery(in["pitchDelivery"].toString());
    node->pitchLayout = Pitch::validLayout(in["pitchLayout"].toString());
}

}  // namespace

int jsonTreeDepth(const QJsonObject &node) {
    int deepest = 0;
    std::vector<std::pair<QJsonObject, int>> pending{{node, 1}};
    while (!pending.empty() && deepest <= kMaxTreeDepth) {
        const auto [current, depth] = std::move(pending.back());
        pending.pop_back();
        deepest = std::max(deepest, depth);
        for (const QJsonValue &child : current.value(QStringLiteral("children")).toArray())
            pending.emplace_back(child.toObject(), depth + 1);
    }
    return deepest;
}

namespace {
std::unique_ptr<MindNode> nodeFromJson(const QJsonObject &in, MindNode *parent) {
    auto node = makeNode(in["title"].toString(QObject::tr("Topic")), parent);
    node->id = in["id"].toString(node->id);
    applyNodeFields(node.get(), in);
    for (const auto child : in["children"].toArray()) node->children.push_back(nodeFromJson(child.toObject(), node.get()));
    return node;
}

// Una copia deve essere indipendente dall'originale: con gli stessi id, storico
// (istantanee per id) e relazioni confonderebbero i due rami.
void assignFreshIds(MindNode *node) {
    node->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (const auto &child : node->children) assignFreshIds(child.get());
}

// FEAT-052: summaries in the sheet JSON, {id, topics, topic} with the topic in the node format.
QJsonArray summariesToJson(const QList<Summary> &summaries) {
    QJsonArray array;
    for (const Summary &summary : summaries)
        array.append(QJsonObject{{"id", summary.id}, {"topics", QJsonArray::fromStringList(summary.topics)},
                                 {"topic", nodeToJson(summary.topic.get())}});
    return array;
}

// Entries without an id, topics or topic are skipped; old id lists are not read (no backward
// compatibility during development, user rule).
// BUG-047 (found by fuzzing): the ids of a map's topics, the summary topics' included, are all
// different and none is empty ("" is the central topic's parent in the flat snapshot). A repeated
// one made toFlatSnapshot/fromFlatSnapshot (the history) loop until the stack ran out. A topic
// without an id, or with one that is not text, gets a new one (nodeFromJson). Also checks the
// depth of each summary topic's branch (BUG-046).
bool validTopicTrees(const QJsonObject &root, const QJsonArray &summaries) {
    std::vector<QJsonObject> pending{root};
    for (const QJsonValue &summary : summaries) {
        const QJsonObject topic = summary.toObject().value(QStringLiteral("topic")).toObject();
        if (topic.isEmpty()) continue;
        if (jsonTreeDepth(topic) > kMaxTreeDepth) return false;
        pending.push_back(topic);
    }
    QSet<QString> seen;
    while (!pending.empty()) {
        const QJsonObject node = std::move(pending.back());
        pending.pop_back();
        if (const QJsonValue id = node.value(QStringLiteral("id")); id.isString()) {
            if (id.toString().isEmpty() || seen.contains(id.toString())) return false;
            seen.insert(id.toString());
        }
        for (const QJsonValue &child : node.value(QStringLiteral("children")).toArray()) pending.push_back(child.toObject());
    }
    return true;
}

QList<Summary> summariesFromJson(const QJsonArray &array) {
    QList<Summary> summaries;
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        Summary summary;
        summary.id = object.value(QStringLiteral("id")).toString();
        for (const QJsonValue &topic : object.value(QStringLiteral("topics")).toArray())
            if (const QString id = topic.toString(); !id.isEmpty() && !summary.topics.contains(id)) summary.topics.append(id);
        if (summary.id.isEmpty() || summary.topics.isEmpty() || !object.value(QStringLiteral("topic")).isObject()) continue;
        summary.topic = nodeFromJson(object.value(QStringLiteral("topic")).toObject(), nullptr);
        summary.topic->summaryTopic = true;
        summary.topic->floating = false;
        summary.topic->collapsed = false;
        summaries.append(summary);
    }
    return summaries;
}
}

// Cache dei campi per nodo di toFlatSnapshot (proprietario: il modello, solo thread UI).
// Chiave: id del nodo. Invalidazione: nessun segnale da tenere aggiornato; a ogni istantanea si
// confrontano tutti i valori che finiscono nel JSON (NodeFieldValues, genitore, riga) e il JSON si
// ricostruisce se uno solo e' diverso. Limite: una voce per nodo della mappa attuale (le voci dei
// nodi tolti si scartano). Uguale nei profili Standard e Leggero. Il JSON e' condiviso con lo
// storico (copia implicita), quindi la memoria aggiunta e' quella dei valori, a loro volta stringhe
// condivise con i nodi.
struct MindMapModel::FlatSnapshotCache {
    struct Entry {
        NodeFieldValues values;
        QString parentId;
        int row = -1;
        quint32 generation = 0;
        QJsonObject json;
    };
    QHash<QString, Entry> entries;
    quint32 generation = 0;
};

MindMapModel::~MindMapModel() = default;

MindMapModel::MindMapModel(QObject *parent) : QAbstractItemModel(parent), m_flatCache(std::make_unique<FlatSnapshotCache>()) {
    // FEAT-019: prima connessione, quindi i colori sono ricalcolati prima che scena, Outliner e
    // storico reagiscano a structureChanged().
    connect(this, &MindMapModel::structureChanged, this, &MindMapModel::refreshStyles);
    clear();
}

void MindMapModel::refreshStyles() {
    relinkSummaries();  // FEAT-052: topics moved under another parent take the summary topic along
    dropSummaryPlanning();
    QList<MindNode *> changed = NodeStyle::resolveTree(m_root.get(), activePalette, backdrop(), sheetStyle, summaryTopics());
    // FEAT-024: numbers of the whole tree. The central topic does not number the floating topics; each
    // floating topic can number its own branch.
    QList<MindNode *> renumbered;
    const auto children = [](MindNode *node, const auto &visit) {
        for (const auto &child : node->children) if (node->parent || !child->floating) visit(child.get());
    };
    const auto setting = [](MindNode *node) { return node->style.numbering ? &*node->style.numbering : nullptr; };
    const auto assignTo = [&renumbered](MindNode *node, const QString &number) {
        if (node->number == number) return;
        node->number = number;
        renumbered.append(node);
    };
    Numbering::assign(m_root.get(), children, setting, assignTo);
    for (const auto &child : m_root->children)
        if (child->floating) Numbering::assign(child.get(), children, setting, assignTo);
    for (MindNode *node : std::as_const(renumbered))
        if (!changed.contains(node)) changed.append(node);
    if (!changed.isEmpty()) emit stylesChanged(changed);
    // After stylesChanged: the scene re-measures there and lays the map out again. Told first by
    // dataChanged it re-measured the topics without a layout, and stylesChanged then saw no change
    // (bug 2026-10-25, the user's photo: children overlapping their numbered parent).
    for (MindNode *node : std::as_const(renumbered))
        if (const QModelIndex index = indexFromNode(node); index.isValid()) emit dataChanged(index, index, {Qt::DisplayRole});
}

void MindMapModel::setPalette(const QString &id, const QStringList &colors, const QString &name) {
    if (id == paletteId && colors == activePalette && name == paletteName && paletteVariant < 0 && !background) return;
    const NodeStyle::Backdrop before = backdrop();
    paletteId = id;
    activePalette = colors;
    paletteName = name;
    paletteVariant = -1;
    background.reset();
    refreshStyles();
    if (!(backdrop() == before)) emit backdropChanged();
    emit styleEdited();
}

bool MindMapModel::setBackdrop(int variant) {
    if (variant >= NodeStyle::kVariantCount || variant >= activePalette.size()) return false;
    const int nextVariant = std::max(variant, -1);
    if (nextVariant == paletteVariant && !background) return false;
    paletteVariant = nextVariant;
    background.reset();
    refreshStyles();
    emit backdropChanged();
    emit styleEdited();
    return true;
}

bool MindMapModel::setBackground(const std::optional<QColor> &freeBackground) {
    std::optional<QColor> next;
    if (freeBackground && freeBackground->isValid()) next = freeBackground->toRgb();
    if (next == background) return false;
    background = next;
    refreshStyles();
    emit backdropChanged();
    emit styleEdited();
    return true;
}

void MindMapModel::setTheme(const QString &id, const QStringList &colors, const QString &name, int variant,
                            const std::optional<QColor> &freeBackground) {
    paletteId = id;
    activePalette = colors;
    paletteName = name;
    paletteVariant = variant >= 0 && variant < NodeStyle::kVariantCount && variant < colors.size() ? variant : -1;
    background = freeBackground && freeBackground->isValid() ? std::optional<QColor>(freeBackground->toRgb()) : std::nullopt;
    refreshStyles();
    emit backdropChanged();
}

bool MindMapModel::setSheetStyle(const NodeStyle::SheetStyle &style) {
    NodeStyle::SheetStyle next = style;
    if (next.branchWidth < 0) next.branchWidth = 0;
    if (next == sheetStyle) return false;
    sheetStyle = next;
    refreshStyles();
    emit styleEdited();
    return true;
}

void MindMapModel::copyThemeFrom(const MindMapModel &other) {
    sheetStyle = other.sheetStyle;  // FEAT-043: il foglio nuovo prende anche lo stile del foglio attivo
    setTheme(other.paletteId, other.activePalette, other.paletteName, other.paletteVariant, other.background);
}

void MindMapModel::addMarkerImage(const QString &sha, const QByteArray &data) {
    if (sha.isEmpty() || data.isEmpty() || markerImages.value(sha) == data) return;
    markerImages.insert(sha, data);
    Markers::registerImage(sha, data);
    m_markerImagesDirty = true;
}

// Solo le immagini dei marcatori ancora usati nel foglio (un marcatore tolto non lascia dati).
QJsonObject MindMapModel::markerImagesJson() const {
    if (markerImages.isEmpty()) return {};
    QSet<QString> used;
    std::function<void(const MindNode *)> visit = [&](const MindNode *node) {
        for (const QString &marker : node->markers)
            if (Markers::isCustom(marker)) used.insert(Markers::customImage(marker));
        for (const auto &child : node->children) visit(child.get());
    };
    visit(m_root.get());
    QJsonObject images;
    for (auto it = markerImages.cbegin(); it != markerImages.cend(); ++it)
        if (used.contains(it.key())) images.insert(it.key(), QString::fromLatin1(it.value().toBase64()));
    if (images != m_markerImagesJson || m_markerImagesDirty) { m_markerImagesJson = images; m_markerImagesDirty = false; }
    return m_markerImagesJson;
}

void MindMapModel::readMarkerImages(const QJsonObject &object) {
    const QJsonObject images = object.value(QStringLiteral("markerImages")).toObject();
    for (auto it = images.begin(); it != images.end(); ++it) {
        const QByteArray data = QByteArray::fromBase64(it.value().toString().toLatin1());
        markerImages.insert(it.key(), data);
        Markers::registerImage(it.key(), data);
    }
    m_markerImagesDirty = true;
}

GanttPlan MindMapModel::gantt() const {
    QJsonArray tasks;
    std::function<void(const MindNode *, const QString &)> visit = [&](const MindNode *node, const QString &parent) {
        QJsonObject task = ganttTopicJson(node);
        // A topic with a checkbox (FEAT-048) is a milestone: empty = not done, checked = 100%.
        if (node->task != MindNode::Task::None) {
            task.insert(QStringLiteral("milestone"), true);
            task.insert(QStringLiteral("progress"), node->task == MindNode::Task::Done ? GanttPlan::kMaxProgress : 0);
        }
        task.insert(QStringLiteral("id"), node->id);
        task.insert(QStringLiteral("name"), node->title);
        task.insert(QStringLiteral("color"), node->branchColor.name(QColor::HexRgb));  // the bar is the branch's colour
        if (!parent.isEmpty()) task.insert(QStringLiteral("parent"), parent);
        tasks.append(task);
        for (const auto &child : node->children) visit(child.get(), node->id);
    };
    for (const auto &child : m_root->children) visit(child.get(), QString());
    QJsonObject json = m_ganttLinks;
    json.insert(QStringLiteral("tasks"), tasks);
    // Links to topics deleted since stay in the sheet (Undo brings them back) but are left out here.
    return GanttPlan::fromJson(json);
}

GanttAssignments MindMapModel::ganttAssignments() const {
    GanttAssignments result;
    std::function<void(const MindNode *)> visit = [&](const MindNode *node) {
        if (!node->ganttPeople.isEmpty()) {
            QVector<GanttAssignment> &people = result[node->id];
            for (const QString &id : node->ganttPeople) people.append({id, node->percentOf(id)});
        }
        for (const auto &child : node->children) visit(child.get());
    };
    for (const auto &child : m_root->children) visit(child.get());
    return result;
}

void MindMapModel::setGanttLinks(const GanttPlan &plan) {
    QJsonObject links = plan.toJson();
    links.remove(QStringLiteral("tasks"));
    // Links to topics not in the sheet now (deleted, moved to another sheet) are kept: they come back
    // with the topic.
    QJsonArray dependencies = links.value(QStringLiteral("dependencies")).toArray();
    for (const QJsonValue &value : m_ganttLinks.value(QStringLiteral("dependencies")).toArray()) {
        const QJsonObject link = value.toObject();
        if (!plan.task(link.value(QStringLiteral("from")).toString()) || !plan.task(link.value(QStringLiteral("to")).toString()))
            dependencies.append(link);
    }
    if (dependencies.isEmpty()) links.remove(QStringLiteral("dependencies"));
    else links.insert(QStringLiteral("dependencies"), dependencies);
    if (links == m_ganttLinks) return;
    m_ganttLinks = links;
    emit ganttChanged();
}

void MindMapModel::readGantt(const QJsonObject &object) {
    QJsonObject links = object.value(QStringLiteral("gantt")).toObject();
    links.remove(QStringLiteral("tasks"));
    if (links == m_ganttLinks) return;
    m_ganttLinks = links;
    emit ganttChanged();
}

void MindMapModel::readPalette(const QJsonObject &object) {
    QStringList colors;
    for (const auto &value : object["palette"].toArray()) colors.append(value.toString());
    const QString id = object["paletteId"].toString();
    paletteName.clear();
    // FEAT-043: stile del foglio; chiavi facoltative, assenti = predefiniti.
    sheetStyle = NodeStyle::SheetStyle();
    sheetStyle.fontFamily = object["sheetFont"].toString().trimmed();
    // Anche i valori in px di prima (4, 6...) restano validi.
    if (const double width = object["sheetBranchWidth"].toDouble(0); width >= NodeStyle::kMinBranchWidthPx && width <= NodeStyle::kMaxBranchWidthPx)
        sheetStyle.branchWidth = width;
    sheetStyle.tapered = object["sheetBranchTapered"].toBool(false);  // FEAT-023
    sheetStyle.coloredBranches = object["coloredBranches"].toBool(true);
    if (const QColor color(object["branchLineColor"].toString()); color.isValid()) sheetStyle.lineColor = color;
    sheetStyle.showNotes = object["showNotes"].toBool(false);  // FEAT-059
    sheetStyle.compact = object["compactMap"].toBool(false);  // FEAT-043
    sheetStyle.freeBranches = object["freeBranches"].toBool(false);
    sheetStyle.justify = object["justifyTopics"].toBool(false);
    sheetStyle.variant = object["sheetVariant"].toString();  // FEAT-026 fase D
    balanced = object["balanced"].toBool(true);  // FEAT-012; file di prima: vedi fromJson
    pitchTheme = Pitch::validTheme(object["pitchTheme"].toString());  // FEAT-063
    pitchAspect = Pitch::validAspect(object["pitchAspect"].toString());
    // FEAT-089: a copy that is not a template is dropped (the theme applies).
    pitchTemplate = Pitch::templateFromJson(object["pitchTemplate"].toObject()) ? object["pitchTemplate"].toObject() : QJsonObject();
    // FEAT-036: chiavi facoltative; assenti = nessuna variante e sfondo bianco.
    paletteVariant = std::clamp(object["paletteVariant"].toInt(-1), -1, NodeStyle::kVariantCount - 1);
    background.reset();
    if (const QColor color(object["background"].toString()); object.contains("background") && color.isValid()) background = color;
    if (const NodeStyle::Palette *base = NodeStyle::findPalette(id)) {
        paletteId = id;
        activePalette = base->colors;
    } else if (object.contains("paletteId") && !colors.isEmpty()) {
        paletteId = id;
        activePalette = colors;
        paletteName = object["paletteName"].toString();  // FEAT-032: palette personalizzata
    } else if (const NodeStyle::Palette *match = NodeStyle::findPaletteByColors(colors)) {
        paletteId = match->id;
        activePalette = match->colors;
    } else {
        paletteId = NodeStyle::kDefaultPaletteId;
        activePalette = NodeStyle::findPalette(paletteId)->colors;
    }
}

bool MindMapModel::setStyle(const QList<MindNode *> &nodes, StyleProperty property, const QVariant &value) {
    if (nodes.isEmpty() || nodes.contains(nullptr) || !NodeStyle::isValidValue(property, value)) return false;
    bool changed = false;
    // FEAT-022: la scelta vale per tutto il titolo e toglie la stessa proprieta' data a singole
    // parole del titolo formattato, nello stesso passo di storico.
    QList<MindNode *> titlesChanged;
    for (MindNode *node : nodes) {
        changed = NodeStyle::setManualValue(node, property, value) || changed;
        if (!RichTitle::isInlineProperty(property) || node->titleHtml.isEmpty()) continue;
        const QString html = RichTitle::withoutProperty(node->titleHtml, property);
        if (html == node->titleHtml) continue;
        node->titleHtml = html;
        titlesChanged.append(node);
    }
    // FEAT-030 (checked live 2026-10-26): a Puntuale shape also turns the fill off; it and the flowchart
    // shapes with inner lines turn a missing border into a thin solid one, in the same history step.
    if (const auto shape = static_cast<NodeShape>(value.toInt()); property == StyleProperty::Shape && NodeShapes::showsBorderWhenChosen(shape)) {
        for (MindNode *node : nodes) {
            if (NodeShapes::isPunctual(shape))
                changed = NodeStyle::setManualValue(node, StyleProperty::FillStyle, static_cast<int>(NodeFillStyle::Nessuno)) || changed;
            if (node->borderStyle != NodeBorderStyle::Nessuno) continue;
            changed = NodeStyle::setManualValue(node, StyleProperty::BorderStyle, static_cast<int>(NodeBorderStyle::Continuo)) || changed;
            changed = NodeStyle::setManualValue(node, StyleProperty::BorderWidth, NodeStyle::kPunctualBorderWidthPx) || changed;
        }
    }
    if (!changed && titlesChanged.isEmpty()) return false;
    refreshStyles();
    if (!titlesChanged.isEmpty()) {
        // Scena e outliner rileggono il titolo formattato (il nodo centrale non ha indice).
        emit stylesChanged(titlesChanged);
        for (MindNode *node : std::as_const(titlesChanged))
            if (const QModelIndex index = indexFromNode(node); index.isValid()) emit dataChanged(index, index, {TitleHtmlRole});
    }
    emit styleEdited();
    return true;
}

bool MindMapModel::clearStyle(const QList<MindNode *> &nodes, StyleProperty property) {
    if (nodes.isEmpty() || nodes.contains(nullptr)) return false;
    bool changed = false;
    for (MindNode *node : nodes) changed = NodeStyle::clearManualValue(node, property) || changed;
    if (!changed) return false;
    refreshStyles();
    emit styleEdited();
    return true;
}

bool MindMapModel::setLinks(const QList<LinkChange> &changes) {
    for (const LinkChange &change : changes) if (!change.node) return false;
    QList<MindNode *> changed;
    for (const LinkChange &change : changes) {
        bool nodeChanged = false;
        if (change.topicLink && *change.topicLink != change.node->topicLink) { change.node->topicLink = *change.topicLink; nodeChanged = true; }
        if (change.webLink && *change.webLink != change.node->webLink) { change.node->webLink = *change.webLink; nodeChanged = true; }
        if (nodeChanged && !changed.contains(change.node)) changed.append(change.node);
    }
    if (changed.isEmpty()) return false;
    emit linksEdited(changed);
    return true;
}

bool MindMapModel::setCustomWidths(const QList<std::pair<MindNode *, qreal>> &widths) {
    for (const auto &[node, width] : widths) if (!node) return false;
    QList<MindNode *> changed;
    for (const auto &[node, width] : widths) {
        const qreal clean = sanitizedManualNodeWidth(width);
        if (clean == node->customWidth) continue;
        node->customWidth = clean;
        changed.append(node);
    }
    if (changed.isEmpty()) return false;
    emit stylesChanged(changed);
    emit styleEdited();
    return true;
}

QJsonObject MindMapModel::subtreeToJson(const MindNode *node) const {
    return node ? nodeToJson(node) : QJsonObject{};
}

MindNode *MindMapModel::insertSubtree(MindNode *parentNode, int row, const QJsonObject &json, bool freshIds) {
    if (!parentNode || json.isEmpty()) return nullptr;
    if (levelOf(parentNode) + jsonTreeDepth(json) > kMaxTreeDepth) { emit depthLimitReached(); return nullptr; }  // BUG-046
    auto node = nodeFromJson(json, parentNode);
    if (freshIds) assignFreshIds(node.get());
    MindNode *const summary = summaryTopicOf(parentNode);  // FEAT-052: a summary topic's branch
    if (summary) node->floating = false;
    row = qBound(0, row, static_cast<int>(parentNode->children.size()));
    auto *result = node.get();
    if (summary) emit summaryBranchAboutToChange(summary);
    else beginInsertRows(indexFromNode(parentNode), row, row);
    parentNode->children.insert(parentNode->children.begin() + row, std::move(node));
    if (summary) emit summaryBranchChanged(summary);
    else endInsertRows();
    emit structureChanged();
    return result;
}

void MindMapModel::replaceRoot(const QJsonObject &json) {
    beginResetModel();
    m_root = nodeFromJson(json, nullptr);
    m_root->floating = false;
    m_root->collapsed = false;
    m_root->side = MindNode::Side::Auto;
    m_root->manualOffset = {};
    m_root->position = {0, 0};
    relations.clear();
    summaries.clear();
    boundaries.clear();
    NodeStyle::resolveTree(m_root.get(), activePalette, backdrop(), sheetStyle, summaryTopics());
    endResetModel();
    emit structureChanged();
}

bool MindMapModel::resetStyle(const QList<MindNode *> &nodes) {
    if (nodes.isEmpty() || nodes.contains(nullptr)) return false;
    bool changed = false;
    for (MindNode *node : nodes) {
        if (node->style == NodeStyleOverrides()) continue;
        node->style = NodeStyleOverrides();
        changed = true;
    }
    if (!changed) return false;
    refreshStyles();
    emit styleEdited();
    return true;
}

namespace {

// FEAT-025: what Reset map style keeps of the sheet style (the layout switches and the notes).
NodeStyle::SheetStyle sheetLookReset(const NodeStyle::SheetStyle &style) {
    NodeStyle::SheetStyle reset;
    reset.showNotes = style.showNotes;
    reset.compact = style.compact;
    reset.freeBranches = style.freeBranches;
    reset.justify = style.justify;
    reset.variant = style.variant;
    return reset;
}

Relationship relationLookReset(Relationship relationship) {
    relationship.lineColor.reset();
    relationship.lineWidth.reset();
    relationship.linePattern.reset();
    relationship.startMark.reset();
    relationship.endMark.reset();
    relationship.fontFamily.clear();
    relationship.fontSize.reset();
    relationship.textColor.reset();
    return relationship;
}

Boundary boundaryLookReset(Boundary boundary) {
    boundary.lineColor.reset();
    boundary.lineWidth.reset();
    boundary.linePattern.reset();
    boundary.fillColor.reset();
    return boundary;
}

}  // namespace

bool MindMapModel::hasMapStyle() const {
    if (background || !(sheetLookReset(sheetStyle) == sheetStyle)) return true;
    for (const Relationship &relationship : relations) if (!(relationLookReset(relationship) == relationship)) return true;
    for (const Boundary &boundary : boundaries) if (!(boundaryLookReset(boundary) == boundary)) return true;
    for (const MindNode *topic : summaryTopics()) if (topic->style != NodeStyleOverrides()) return true;
    const std::function<bool(const MindNode *)> styled = [&](const MindNode *node) {
        if (node->style != NodeStyleOverrides()) return true;
        return std::any_of(node->children.cbegin(), node->children.cend(), [&](const auto &child) { return styled(child.get()); });
    };
    return styled(m_root.get());
}

bool MindMapModel::resetMapStyle() {
    if (!hasMapStyle()) return false;
    const bool backdropReset = background.has_value();
    background.reset();
    sheetStyle = sheetLookReset(sheetStyle);
    for (Relationship &relationship : relations) relationship = relationLookReset(relationship);
    for (Boundary &boundary : boundaries) boundary = boundaryLookReset(boundary);
    for (MindNode *topic : summaryTopics()) topic->style = NodeStyleOverrides();
    const std::function<void(MindNode *)> reset = [&](MindNode *node) {
        node->style = NodeStyleOverrides();
        for (const auto &child : node->children) reset(child.get());
    };
    reset(m_root.get());
    refreshStyles();
    if (backdropReset) emit backdropChanged();
    emit styleEdited();
    return true;
}

bool MindMapModel::copyStyle(const MindNode *source, const QList<MindNode *> &targets) {
    if (!source || targets.contains(nullptr)) return false;
    bool changed = false;
    for (MindNode *node : targets) {
        if (node == source || node->style == source->style) continue;
        node->style = source->style;
        changed = true;
    }
    if (!changed) return false;
    refreshStyles();
    emit styleEdited();
    return true;
}

MindMapModel::TopicStyle MindMapModel::topicStyle(const MindNode *node) const {
    TopicStyle style{NodeStyle::effectiveStyle(node), node->ganttLabel.value_or(!node->children.empty())};
    if (!sheetStyle.coloredBranches) {
        // As NodeStyle::resolveTree: the line takes the branch colour only below a colour chosen by hand.
        bool chosen = false;
        for (const MindNode *up = node; up && !chosen; up = up->parent) chosen = up->style.branchColor.has_value();
        if (!chosen) style.style.branchColor.reset();
    }
    return style;
}

bool MindMapModel::pasteStyle(const TopicStyle &style, const QList<MindNode *> &targets) {
    if (targets.contains(nullptr)) return false;
    bool styled = false, labelled = false;
    for (MindNode *node : targets) {
        // As GanttView::setNameOnBar: only a choice that differs from the default (summaries yes) is kept.
        const bool byDefault = !node->children.empty();
        const std::optional<bool> label = style.nameOnBar == byDefault ? std::nullopt : std::optional<bool>(style.nameOnBar);
        if (node->ganttLabel != label) {
            node->ganttLabel = label;
            labelled = true;
        }
        if (node->style != style.style) {
            node->style = style.style;
            styled = true;
        }
    }
    if (!styled && !labelled) return false;
    if (styled) refreshStyles();
    if (labelled) emit ganttChanged();
    emit styleEdited();
    return true;
}

bool MindMapModel::copyStyleToDescendants(const QList<MindNode *> &sources) {
    if (sources.contains(nullptr)) return false;
    // Each descendant takes the style of its nearest selected ancestor, read before any change.
    QHash<const MindNode *, NodeStyleOverrides> styles;
    for (const MindNode *source : sources) styles.insert(source, source->style);
    bool changed = false;
    const std::function<void(MindNode *, const NodeStyleOverrides &)> visit = [&](MindNode *node, const NodeStyleOverrides &style) {
        for (const auto &child : node->children) {
            if (child->floating) continue;
            const auto own = styles.constFind(child.get());
            if (own == styles.cend() && child->style != style) {
                child->style = style;
                changed = true;
            }
            visit(child.get(), own == styles.cend() ? style : *own);
        }
    };
    for (MindNode *source : sources) {
        bool nested = false;
        for (const MindNode *up = source->parent; up && !nested; up = up->parent) nested = styles.contains(up);
        if (!nested) visit(source, source->style);
    }
    if (!changed) return false;
    refreshStyles();
    emit styleEdited();
    return true;
}

QModelIndex MindMapModel::index(int row, int column, const QModelIndex &parentIndex) const {
    if (column != 0 || row < 0) return {};
    auto *parentNode = parentIndex.isValid() ? nodeFromIndex(parentIndex) : m_root.get();
    if (!parentNode || row >= static_cast<int>(parentNode->children.size())) return {};
    return createIndex(row, column, parentNode->children[row].get());
}

QModelIndex MindMapModel::parent(const QModelIndex &child) const {
    auto *node = nodeFromIndex(child);
    if (!node || !node->parent || node->parent == m_root.get()) return {};
    return createIndex(rowOf(node->parent), 0, node->parent);
}

int MindMapModel::rowCount(const QModelIndex &parentIndex) const {
    const auto *node = parentIndex.isValid() ? nodeFromIndex(parentIndex) : m_root.get();
    return node ? static_cast<int>(node->children.size()) : 0;
}

QVariant MindMapModel::data(const QModelIndex &idx, int role) const {
    const auto *node = nodeFromIndex(idx);
    if (!node) return {};
    // FEAT-024: the number before the title, shown only (the edited text is the title).
    if (role == Qt::DisplayRole) return LatexRenderer::toDisplayText(Numbering::withNumber(node->number, node->title));
    if (role == Qt::EditRole) return node->title;
    if (role == TitleHtmlRole) return node->titleHtml;
    return {};
}

bool MindMapModel::setData(const QModelIndex &idx, const QVariant &value, int role) {
    auto *node = nodeFromIndex(idx);
    if (!node || role != Qt::EditRole) return false;
    // B2 (2026-10-13): il delegate dell'outliner chiama setData a ogni chiusura del campo,
    // anche senza modifiche. Con lo stesso testo non si tocca nulla: resta la formattazione
    // (titleHtml) fatta sulla mappa e la mappa non viene segnata come modificata.
    if (value.toString() == node->title) return true;
    node->title = value.toString();
    // Una modifica proveniente dall'outliner sostituisce l'intero titolo con testo semplice.
    node->titleHtml.clear();
    emit dataChanged(idx, idx, {role, Qt::DisplayRole});
    emit structureChanged();
    return true;
}

bool MindMapModel::setTitle(const QModelIndex &idx, const QString &title, const QString &html) {
    auto *node = nodeFromIndex(idx);
    if (!node) return false;
    if (node->title == title && node->titleHtml == html) return true;  // come B2 in setData
    node->title = title;
    node->titleHtml = RichResources::withoutExternal(html);  // BUG-044
    emit dataChanged(idx, idx, {Qt::EditRole, Qt::DisplayRole, TitleHtmlRole});
    emit structureChanged();
    return true;
}

Qt::ItemFlags MindMapModel::flags(const QModelIndex &idx) const {
    if (!idx.isValid()) return Qt::ItemIsDropEnabled;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable |
           Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
}

QStringList MindMapModel::mimeTypes() const { return {"application/x-mentis-node"}; }

QMimeData *MindMapModel::mimeData(const QModelIndexList &indexes) const {
    auto *mime = new QMimeData;
    for (const auto &index : indexes) {
        if (auto *node = nodeFromIndex(index)) {
            mime->setData("application/x-mentis-node", node->id.toUtf8());
            break;
        }
    }
    return mime;
}

bool MindMapModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                                const QModelIndex &parentIndex) {
    if (action == Qt::IgnoreAction) return true;
    if (action != Qt::MoveAction || column > 0 || !data->hasFormat("application/x-mentis-node")) return false;
    const QString id = QString::fromUtf8(data->data("application/x-mentis-node"));
    MindNode *moving = nullptr;
    std::function<void(MindNode *)> find = [&](MindNode *candidate) {
        if (!candidate || moving) return;
        if (candidate->id == id) { moving = candidate; return; }
        for (const auto &child : candidate->children) find(child.get());
    };
    find(m_root.get());
    auto *newParent = parentIndex.isValid() ? nodeFromIndex(parentIndex) : m_root.get();
    return moveNode(moving, newParent, row);
}

MindNode *MindMapModel::nodeFromIndex(const QModelIndex &idx) const { return idx.isValid() ? static_cast<MindNode *>(idx.internalPointer()) : nullptr; }

QList<MindNode *> MindMapModel::summaryTopics() const {
    QList<MindNode *> topics;
    for (const Summary &summary : summaries) topics.append(summary.topic.get());
    return topics;
}

const Summary *MindMapModel::summaryOfTopic(const MindNode *topic) const {
    for (const Summary &summary : summaries)
        if (summary.topic.get() == topic) return &summary;
    return nullptr;
}

void MindMapModel::dropSummaryPlanning() {
    QSet<QString> summaries;
    bool changed = false;
    std::function<void(MindNode *)> visit = [&](MindNode *node) {
        for (const auto &child : node->children) {
            if (child->children.empty()) continue;
            summaries.insert(child->id);
            if (!child->ganttPeople.isEmpty()) {
                child->ganttPeople.clear();
                child->ganttPercent.clear();
                changed = true;
            }
            visit(child.get());
        }
    };
    visit(m_root.get());
    const QJsonArray dependencies = m_ganttLinks.value(QStringLiteral("dependencies")).toArray();
    QJsonArray kept;
    for (const QJsonValue &value : dependencies) {
        const QJsonObject link = value.toObject();
        if (!summaries.contains(link.value(QStringLiteral("from")).toString())
            && !summaries.contains(link.value(QStringLiteral("to")).toString()))
            kept.append(link);
    }
    if (kept.size() != dependencies.size()) {
        if (kept.isEmpty()) m_ganttLinks.remove(QStringLiteral("dependencies"));
        else m_ganttLinks.insert(QStringLiteral("dependencies"), kept);
        changed = true;
    }
    if (changed) emit ganttChanged();
}

void MindMapModel::relinkSummaries() {
    if (summaries.isEmpty()) return;
    QHash<QString, MindNode *> tree;
    QList<MindNode *> pending{m_root.get()};
    while (!pending.isEmpty()) {
        MindNode *node = pending.takeLast();
        tree.insert(node->id, node);
        for (const auto &child : node->children) pending.append(child.get());
    }
    for (Summary &summary : summaries) {
        summary.topics.removeIf([&tree](const QString &id) { return !tree.contains(id); });
        summary.topic->parent = summary.topics.isEmpty() ? nullptr : tree.value(summary.topics.front())->parent;
    }
    summaries.removeIf([](const Summary &summary) { return summary.topics.isEmpty() || !summary.topic->parent; });
}

MindNode *MindMapModel::nodeById(const QString &id) const {
    if (id.isEmpty() || !m_root) return nullptr;
    QList<MindNode *> pending{m_root.get()};
    for (const Summary &summary : summaries) pending.append(summary.topic.get());  // FEAT-052: with their branches
    while (!pending.isEmpty()) {
        MindNode *node = pending.takeLast();
        if (node->id == id) return node;
        for (const auto &child : node->children) pending.append(child.get());
    }
    return nullptr;
}

QModelIndex MindMapModel::indexFromNode(MindNode *node) const {
    if (!node || node == m_root.get() || summaryTopicOf(node)) return {};  // FEAT-052: not in the outliner
    return createIndex(rowOf(node), 0, node);
}

int MindMapModel::levelOf(const MindNode *node) {
    int level = 0;
    for (; node; node = node->parent) ++level;
    return level;
}

int MindMapModel::branchLevels(const MindNode *node) {
    if (!node) return 0;
    int deepest = 0;
    std::vector<std::pair<const MindNode *, int>> pending{{node, 1}};
    while (!pending.empty()) {
        const auto [current, level] = pending.back();
        pending.pop_back();
        deepest = std::max(deepest, level);
        for (const auto &child : current->children) pending.emplace_back(child.get(), level + 1);
    }
    return deepest;
}

MindNode *MindMapModel::addChild(MindNode *parentNode) {
    if (!parentNode) parentNode = m_root.get();
    return addChild(parentNode, defaultTitle(parentNode, siblingCountFor(parentNode) + 1));
}

MindNode *MindMapModel::addChild(MindNode *parentNode, const QString &title) {
    if (!parentNode) parentNode = m_root.get();
    if (levelOf(parentNode) >= kMaxTreeDepth) { emit depthLimitReached(); return nullptr; }  // BUG-046
    MindNode *const summary = summaryTopicOf(parentNode);  // FEAT-052: a summary topic's branch, not in the rows
    // Gli argomenti mobili stanno in fondo ai figli del centrale (come nell'outliner): un
    // ramo principale nuovo va prima del primo.
    const int row = parentNode == m_root.get() ? firstFloatingRow() : static_cast<int>(parentNode->children.size());
    if (summary) emit summaryBranchAboutToChange(summary);
    else beginInsertRows(indexFromNode(parentNode), row, row);
    parentNode->children.insert(parentNode->children.begin() + row, makeNode(title, parentNode));
    auto *result = parentNode->children.at(row).get();
    if (summary) emit summaryBranchChanged(summary);
    else endInsertRows();
    emit structureChanged();
    return result;
}

int MindMapModel::siblingCountFor(const MindNode *parent) const {
    // Sotto il centrale gli argomenti mobili non contano: "Argomento principale N" numera i rami.
    return parent == m_root.get() ? firstFloatingRow() : int(parent->children.size());
}

int MindMapModel::firstFloatingRow() const {
    const auto &children = m_root->children;
    const auto it = std::find_if(children.cbegin(), children.cend(), [](const auto &child) { return child->floating; });
    return int(it - children.cbegin());
}

QList<MindNode *> MindMapModel::stackingOrder() const {
    QList<MindNode *> order;
    for (const auto &child : m_root->children) order.append(child.get());
    std::stable_sort(order.begin(), order.end(), [](const MindNode *a, const MindNode *b) { return a->stackOrder < b->stackOrder; });
    return order;
}

QString MindMapModel::defaultTitle(const MindNode *parent, int number) {
    return (parent && !parent->parent ? tr("Main Topic %1") : tr("Subtopic %1")).arg(number);
}

MindNode *MindMapModel::addSibling(MindNode *node) {
    if (node && node->summaryTopic) return nullptr;  // FEAT-052: a summary topic has no row
    if (!node || !node->parent) return addChild(m_root.get());
    auto *parentNode = node->parent;
    MindNode *const summary = summaryTopicOf(parentNode);
    const int row = node->floating ? firstFloatingRow() : rowOf(node) + 1;
    if (summary) emit summaryBranchAboutToChange(summary);
    else beginInsertRows(indexFromNode(parentNode), row, row);
    auto created = makeNode(defaultTitle(parentNode, siblingCountFor(parentNode) + 1), parentNode);
    auto *result = created.get();
    parentNode->children.insert(parentNode->children.begin() + row, std::move(created));
    if (summary) emit summaryBranchChanged(summary);
    else endInsertRows();
    emit structureChanged();
    return result;
}

MindNode *MindMapModel::addSiblingBefore(MindNode *node) {
    if (!node || !node->parent || node->floating || node->summaryTopic) return nullptr;
    auto *parentNode = node->parent;
    MindNode *const summary = summaryTopicOf(parentNode);
    const int row = rowOf(node);
    if (summary) emit summaryBranchAboutToChange(summary);
    else beginInsertRows(indexFromNode(parentNode), row, row);
    auto created = makeNode(defaultTitle(parentNode, siblingCountFor(parentNode) + 1), parentNode);
    auto *result = created.get();
    parentNode->children.insert(parentNode->children.begin() + row, std::move(created));
    if (summary) emit summaryBranchChanged(summary);
    else endInsertRows();
    emit structureChanged();
    return result;
}

MindNode *MindMapModel::insertParent(MindNode *node) {
    if (node && levelOf(node) + branchLevels(node) > kMaxTreeDepth) { emit depthLimitReached(); return nullptr; }  // BUG-046
    MindNode *created = addSiblingBefore(node);
    if (!created) return nullptr;
    created->side = node->side;
    created->manualOffset = node->manualOffset;
    node->side = MindNode::Side::Auto;
    moveNode(node, created, 0);  // drops the node's manual position (a new parent)
    const auto takeOver = [node, created](QStringList &topics) {
        for (QString &id : topics) if (id == node->id) id = created->id;
    };
    for (Summary &summary : summaries) takeOver(summary.topics);
    for (Boundary &boundary : boundaries) takeOver(boundary.topics);
    emit structureChanged();  // again, with the ids moved (the signals above came half-way)
    return created;
}

bool MindMapModel::removeNode(MindNode *node, bool pruneAnnotations) {
    if (!node || node == m_root.get() || !node->parent || node->summaryTopic) return false;
    QSet<QString> removed;
    std::function<void(const MindNode *)> collect = [&](const MindNode *current) {
        removed.insert(current->id);
        for (const auto &child : current->children) collect(child.get());
    };
    collect(node);
    // FEAT-052: always, so no summary topic keeps a removed parent; a summary left without topics goes.
    for (Summary &summary : summaries) summary.topics.removeIf([&](const QString &id) { return removed.contains(id); });
    for (const Summary &summary : std::as_const(summaries))
        if (summary.topics.isEmpty()) { removed.insert(summary.topic->id); summary.topic->parent = nullptr; }
    summaries.removeIf([](const Summary &summary) { return summary.topics.isEmpty(); });
    if (pruneAnnotations) {
        // FEAT-052: a boundary left without topics goes too, with the relationships ending on it.
        for (Boundary &boundary : boundaries) boundary.topics.removeIf([&](const QString &id) { return removed.contains(id); });
        for (const Boundary &boundary : std::as_const(boundaries)) if (boundary.topics.isEmpty()) removed.insert(boundary.id);
        boundaries.removeIf([](const Boundary &boundary) { return boundary.topics.isEmpty(); });
        relations.removeIf([&](const Relationship &relation) {
            return removed.contains(relation.from) || removed.contains(relation.to);
        });
    }
    auto *parentNode = node->parent;
    MindNode *const summary = summaryTopicOf(parentNode);
    const int row = rowOf(node);
    if (summary) emit summaryBranchAboutToChange(summary);
    else beginRemoveRows(indexFromNode(parentNode), row, row);
    parentNode->children.erase(parentNode->children.begin() + row);
    if (summary) emit summaryBranchChanged(summary);
    else endRemoveRows();
    emit structureChanged();
    return true;
}

bool MindMapModel::indentNode(MindNode *node) {
    if (!node || !node->parent || node->summaryTopic) return false;
    auto *oldParent = node->parent;
    const int row = rowOf(node);
    if (row <= 0) return false;
    auto *newParent = oldParent->children[row - 1].get();
    if (levelOf(newParent) + branchLevels(node) > kMaxTreeDepth) { emit depthLimitReached(); return false; }  // BUG-046
    MindNode *const summary = summaryTopicOf(oldParent);  // FEAT-052: no rows to move there
    // B8 (2026-10-13): il reset del modello riapriva tutti i rami chiusi dell'outliner
    // (modelReset -> expandAll) e ricostruiva da zero mappa e outliner. Con
    // beginMoveRows/endMoveRows la vista sposta solo la riga e conserva aperto/chiuso.
    const int destinationRow = static_cast<int>(newParent->children.size());
    const bool moveSignals = summary || beginMoveRows(indexFromNode(oldParent), row, row, indexFromNode(newParent), destinationRow);
    if (summary) emit summaryBranchAboutToChange(summary);
    else if (!moveSignals) beginResetModel();  // non dovrebbe accadere: ripiego sul comportamento precedente
    auto moved = std::move(oldParent->children[row]);
    oldParent->children.erase(oldParent->children.begin() + row);
    moved->parent = newParent;
    moved->manualOffset = {};  // B7: cambia genitore, lo spostamento manuale non ha più senso
    moved->stackOrder = 0;     // FEAT-057: the rank belongs to the old siblings
    newParent->children.push_back(std::move(moved));
    if (summary) emit summaryBranchChanged(summary);
    else if (moveSignals) endMoveRows(); else endResetModel();
    emit structureChanged();
    return true;
}

bool MindMapModel::outdentNode(MindNode *node) {
    if (!node || !node->parent || node->parent == m_root.get() || node->summaryTopic || node->parent->summaryTopic) return false;
    auto *oldParent = node->parent;
    MindNode *const summary = summaryTopicOf(oldParent);  // FEAT-052: no rows to move there
    auto *newParent = oldParent->parent;
    const int row = rowOf(node);
    const int parentRow = rowOf(oldParent);
    if (row < 0 || parentRow < 0) return false;
    // B8 (2026-10-13): spostamento di riga invece del reset (vedi indentNode).
    const bool moveSignals = summary || beginMoveRows(indexFromNode(oldParent), row, row, indexFromNode(newParent), parentRow + 1);
    if (summary) emit summaryBranchAboutToChange(summary);
    else if (!moveSignals) beginResetModel();
    auto moved = std::move(oldParent->children[row]);
    oldParent->children.erase(oldParent->children.begin() + row);
    moved->parent = newParent;
    moved->manualOffset = {};  // B7: cambia genitore, lo spostamento manuale non ha più senso
    moved->stackOrder = 0;     // FEAT-057: the rank belongs to the old siblings
    newParent->children.insert(newParent->children.begin() + parentRow + 1, std::move(moved));
    if (summary) emit summaryBranchChanged(summary);
    else if (moveSignals) endMoveRows(); else endResetModel();
    emit structureChanged();
    return true;
}

bool MindMapModel::moveNode(MindNode *node, MindNode *newParent, int row) {
    if (!node || node == m_root.get() || !node->parent || !newParent || node == newParent) return false;
    // FEAT-052: a summary topic has no row; a topic stays in its tree (the main one, or one summary
    // topic's branch).
    MindNode *const summary = summaryTopicOf(newParent);
    if (node->summaryTopic || summaryTopicOf(node->parent) != summary) return false;
    for (auto *ancestor = newParent; ancestor; ancestor = ancestor->parent)
        if (ancestor == node) return false;
    if (newParent != node->parent && levelOf(newParent) + branchLevels(node) > kMaxTreeDepth) {  // BUG-046
        emit depthLimitReached();
        return false;
    }

    auto *oldParent = node->parent;
    const int oldRow = rowOf(node);
    if (oldRow < 0) return false;
    const int requestedRow = row < 0 ? static_cast<int>(newParent->children.size()) : row;

    // Correzione crash: prima il decremento avveniva DOPO il clamp. Trascinando un nodo
    // "dopo se stesso" quando era l'unico figlio (oldRow 0, requestedRow 1), dopo
    // l'erase la lista era vuota, il clamp dava 0 e il decremento -1: l'insert
    // avveniva prima dell'inizio del vettore (memoria corrotta, crash).
    // Ora si calcola la destinazione nella lista senza il nodo, poi si limita.
    int destination = requestedRow;
    if (oldParent == newParent && oldRow < requestedRow) --destination;
    const int sizeWithoutNode = static_cast<int>(newParent->children.size()) - (oldParent == newParent ? 1 : 0);
    destination = std::clamp(destination, 0, sizeWithoutNode);
    // Rilasciato nella stessa posizione (es. su se stesso): nessuna modifica, quindi
    // niente reset del modello né passo inutile nello storico.
    if (oldParent == newParent && destination == oldRow) return false;
    // B8 (2026-10-13): spostamento di riga invece del reset (vedi indentNode).
    // beginMoveRows vuole la destinazione contata nella lista che contiene ancora il nodo:
    // nello stesso genitore, dopo la posizione di partenza, vale destination + 1.
    const int qtDestination = (oldParent == newParent && destination >= oldRow) ? destination + 1 : destination;
    const bool moveSignals = summary || beginMoveRows(indexFromNode(oldParent), oldRow, oldRow, indexFromNode(newParent), qtDestination);
    if (summary) emit summaryBranchAboutToChange(summary);
    else if (!moveSignals) beginResetModel();
    auto moved = std::move(oldParent->children[oldRow]);
    oldParent->children.erase(oldParent->children.begin() + oldRow);
    moved->parent = newParent;
    if (oldParent != newParent) { moved->manualOffset = {}; moved->stackOrder = 0; }  // B7, FEAT-057: vedi indentNode
    newParent->children.insert(newParent->children.begin() + destination, std::move(moved));
    if (summary) emit summaryBranchChanged(summary);
    else if (moveSignals) endMoveRows(); else endResetModel();
    emit structureChanged();
    return true;
}

void MindMapModel::clear(const QString &rootTitle) {
    beginResetModel();
    m_root = makeNode(rootTitle);
    relations.clear(); summaries.clear(); boundaries.clear();
    paletteId = NodeStyle::kDefaultPaletteId;
    paletteName.clear();
    sheetStyle = NodeStyle::SheetStyle();
    paletteVariant = -1;
    background.reset();
    activePalette = NodeStyle::findPalette(paletteId)->colors;
    if (!m_ganttLinks.isEmpty()) { m_ganttLinks = {}; emit ganttChanged(); }  // FEAT-075
    m_root->position = {0, 0};
    // FEAT-019: colori risolti prima che la scena ricrei i nodi, cosi' non vanno ridisegnati dopo.
    NodeStyle::resolveTree(m_root.get(), activePalette, backdrop(), sheetStyle, summaryTopics());
    endResetModel();
    emit structureChanged();
    emit backdropChanged();  // FEAT-036
}

QString MindMapModel::toIndentedText() const {
    QString out;
    std::function<void(const MindNode *, int)> write = [&](const MindNode *node, int depth) {
        // 2026-10-14: un titolo con "a capo" (Maiusc+Invio) spezzava l'elenco in più voci.
        out += QString(depth * 2, ' ') + QString(node->title).replace(QLatin1Char('\n'), QLatin1Char(' ')) + '\n';
        for (const auto &child : node->children) write(child.get(), depth + 1);
    };
    write(m_root.get(), 0);
    return out;
}

// FEAT-059: righe Markdown della nota. Con i formati: grassetto, corsivo, barrato e titoli in Markdown;
// gli elenchi della nota diventano "• " e "1\. ", altrimenti rileggendo il file sarebbero rami figli
// (MarkdownImport li riconverte in elenchi della nota).
// Scritte qui blocco per blocco (QTextDocument::toMarkdown non riportava grassetto e corsivo delle
// note salvate). Il sottolineato non esiste in Markdown e si perde.
static QStringList noteMarkdownLines(const MindNode *node) {
    if (node->notesHtml.isEmpty()) return node->notes.split(QLatin1Char('\n'));
    QTextDocument document;
    document.setHtml(node->notesHtml);
    QStringList lines;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        QString text;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) continue;
            const QTextCharFormat format = fragment.charFormat();
            QString piece = fragment.text();
            const QString trimmed = piece.trimmed();
            if (trimmed.isEmpty()) { text += piece; continue; }
            // I simboli stanno attorno al testo senza spazi (come vuole Markdown).
            const qsizetype lead = piece.indexOf(trimmed);
            QString marked = trimmed;
            if (MarkdownShortcuts::isCode(format)) {  // FEAT-068: codice cosi' com'e', senza altri simboli
                text += piece.left(lead) + QLatin1Char('`') + trimmed + QLatin1Char('`') + piece.mid(lead + trimmed.size());
                continue;
            }
            if (MarkdownShortcuts::isHighlight(format)) marked = QStringLiteral("==") + marked + QStringLiteral("==");  // FEAT-068
            if (format.fontStrikeOut()) marked = QStringLiteral("~~") + marked + QStringLiteral("~~");
            if (format.fontItalic()) marked = QLatin1Char('*') + marked + QLatin1Char('*');
            if (format.fontWeight() >= QFont::DemiBold && block.blockFormat().headingLevel() == 0)
                marked = QStringLiteral("**") + marked + QStringLiteral("**");
            if (format.isAnchor() && !format.anchorHref().isEmpty())  // collegamento nel testo della nota
                marked = QLatin1Char('[') + marked + QStringLiteral("](") + format.anchorHref() + QLatin1Char(')');
            text += piece.left(lead) + marked + piece.mid(lead + trimmed.size());
        }
        if (const QTextList *list = block.textList()) {
            const bool numbered = list->format().style() == QTextListFormat::ListDecimal;
            const QString indent((list->format().indent() - 1) * 2, QLatin1Char(' '));
            text = indent + (numbered ? QString::number(list->itemNumber(block) + 1) + QStringLiteral("\\. ") : QStringLiteral("• ")) + text;
        } else if (block.blockFormat().headingLevel() > 0 && !text.trimmed().isEmpty()) {
            // Titolo della nota in grassetto: "## " rileggendo il file diventerebbe un ramo.
            text = QStringLiteral("**") + text.trimmed() + QStringLiteral("**");
        }
        lines.append(text);
    }
    while (!lines.isEmpty() && lines.back().trimmed().isEmpty()) lines.removeLast();
    return lines;
}

QString MindMapModel::toMarkdown(bool styles, const MarkdownExtras &extras) const {
    QString out;
    std::function<void(const MindNode *, int)> write = [&](const MindNode *node, int depth) {
        const QString noteIndent(depth * 2 + 2, QLatin1Char(' '));
        // 2026-10-14: gli "a capo" del titolo diventano <br>, che fromMarkdown riconverte.
        QString title = Numbering::withNumber(node->number, node->title).replace(QLatin1Char('\n'), QStringLiteral("<br>"));  // FEAT-024
        // FEAT-024: a number such as "1." or "1)" would start an ordered list after "- "; escaped, it stays text.
        static const QRegularExpression listMarker(QStringLiteral("^(\\d{1,9})([.)])(?=\\s|$)"));
        title.replace(listMarker, QStringLiteral("\\1\\\\2"));  // Qt keeps a backslash not followed by a digit
        // FEAT-054: con un collegamento web la voce diventa [titolo](indirizzo).
        if (extras.webLinks && !node->webLink.isEmpty())
            title = QLatin1Char('[') + title.replace(QLatin1Char(']'), QStringLiteral("\\]")) + QStringLiteral("](") + node->webLink + QLatin1Char(')');
        // FEAT-048: casella di spunta come lista di cose da fare di Markdown.
        const QString box = node->task == MindNode::Task::Done ? QStringLiteral("[x] ")
                            : node->task == MindNode::Task::Open ? QStringLiteral("[ ] ") : QString();
        // FEAT-060: etichette in fondo come "#etichetta" ("_" al posto degli spazi).
        for (const QString &label : node->labels) title += QStringLiteral(" #") + QString(label).replace(QLatin1Char(' '), QLatin1Char('_'));
        out += QString(depth * 2, ' ') + "- " + box + title + '\n';
        if (styles) out += noteIndent + QString("<!-- mindmap: fill=%1 text=%2 border=%3 x=%4 y=%5 -->\n")
            .arg(node->fill.name(), node->text.name(), node->border.name()).arg(node->position.x()).arg(node->position.y());
        // FEAT-054 (2026-10-18): ogni riga della nota rientrata sotto la sua voce (prima dalla seconda
        // riga in poi si tornava a capo senza rientro); le righe vuote separano i paragrafi.
        if (!node->formula.isEmpty()) out += noteIndent + QStringLiteral("$$") + node->formula + QStringLiteral("$$\n");  // FEAT-064
        if (!node->notes.isEmpty())
            for (const QString &line : noteMarkdownLines(node))
                out += (line.isEmpty() ? QString() : noteIndent + line) + '\n';
        // L'immagine (percorso del file originale, che altrove non esiste) solo con i dati di stile.
        if (styles && !node->imagePath.isEmpty()) out += noteIndent + QString("![image](%1)\n").arg(node->imagePath);
        if (extras.extraLines)
            for (const QString &line : extras.extraLines(node)) out += noteIndent + line + '\n';
        for (const auto &child : node->children) write(child.get(), depth + 1);
    };
    write(m_root.get(), 0);
    return out;
}

bool MindMapModel::fromMarkdown(const QString &markdown, QString *error) {
    // FEAT-055: stesse regole dell'incolla nell'outliner (titoli, rientro, note, formati; i commenti
    // scritti da toMarkdown si ignorano). Prima si leggevano solo le righe con -, * o +.
    const MarkdownImport::Result parsed = MarkdownImport::parse(markdown);
    if (parsed.items.empty()) { if (error) *error = tr("The file contains no text to import."); return false; }
    // La prima voce di primo livello e' il nodo centrale; le altre diventano suoi figli, come prima.
    QJsonObject rootJson = MarkdownImport::toSubtreeJson(parsed.items.front(), true);
    QJsonArray children = rootJson.value(QStringLiteral("children")).toArray();
    for (size_t i = 1; i < parsed.items.size(); ++i) children.append(MarkdownImport::toSubtreeJson(parsed.items[i]));
    rootJson.insert(QStringLiteral("children"), children);
    beginResetModel();
    m_root = nodeFromJson(rootJson, nullptr);
    // I nodi di prima non esistono piu': relazioni, sommari e confini che li usavano spariscono.
    relations.clear(); summaries.clear(); boundaries.clear();
    // FEAT-019: colori risolti prima che la scena ricrei i nodi, cosi' non vanno ridisegnati dopo.
    NodeStyle::resolveTree(m_root.get(), activePalette, backdrop(), sheetStyle, summaryTopics());
    endResetModel();
    emit structureChanged();
    emit backdropChanged();  // FEAT-036
    return true;
}

QJsonObject MindMapModel::toJson() const {
    const QJsonArray relationArray = Relationships::toJson(relations);  // FEAT-053
    const QJsonArray summaryArray = summariesToJson(summaries);  // FEAT-052
    const QJsonArray boundaryArray = Boundaries::toJson(boundaries);  // FEAT-052
    QJsonArray paletteArray; for (const auto &color : activePalette) paletteArray.append(color);
    QJsonObject result{{"format", "mentis-1"}, {"root", nodeToJson(m_root.get())}, {"relations", relationArray}, {"summaries", summaryArray}, {"boundaries", boundaryArray}, {"palette", paletteArray}, {"paletteId", paletteId}};
    if (!paletteName.isEmpty()) result.insert(QStringLiteral("paletteName"), paletteName);  // FEAT-032
    if (paletteVariant >= 0) result.insert(QStringLiteral("paletteVariant"), paletteVariant);  // FEAT-036
    if (background) result.insert(QStringLiteral("background"), background->name());
    // FEAT-043: stile del foglio, solo i valori diversi dal predefinito.
    if (!sheetStyle.fontFamily.isEmpty()) result.insert(QStringLiteral("sheetFont"), sheetStyle.fontFamily);
    if (sheetStyle.branchWidth > 0) result.insert(QStringLiteral("sheetBranchWidth"), sheetStyle.branchWidth);
    if (sheetStyle.tapered) result.insert(QStringLiteral("sheetBranchTapered"), true);  // FEAT-023
    if (!sheetStyle.coloredBranches) result.insert(QStringLiteral("coloredBranches"), false);
    if (sheetStyle.lineColor != NodeStyle::kDefaultBranchLineColor) result.insert(QStringLiteral("branchLineColor"), sheetStyle.lineColor.name());
    if (sheetStyle.showNotes) result.insert(QStringLiteral("showNotes"), true);  // FEAT-059
    if (sheetStyle.compact) result.insert(QStringLiteral("compactMap"), true);  // FEAT-043
    if (sheetStyle.freeBranches) result.insert(QStringLiteral("freeBranches"), true);
    if (sheetStyle.justify) result.insert(QStringLiteral("justifyTopics"), true);
    if (!sheetStyle.variant.isEmpty()) result.insert(QStringLiteral("sheetVariant"), sheetStyle.variant);  // FEAT-026
    result.insert(QStringLiteral("balanced"), balanced);  // FEAT-012: sempre (vedi fromJson)
    if (!pitchTheme.isEmpty()) result.insert(QStringLiteral("pitchTheme"), pitchTheme);  // FEAT-063
    if (!pitchAspect.isEmpty()) result.insert(QStringLiteral("pitchAspect"), pitchAspect);
    if (!pitchTemplate.isEmpty()) result.insert(QStringLiteral("pitchTemplate"), pitchTemplate);  // FEAT-089
    if (!m_ganttLinks.isEmpty()) result.insert(QStringLiteral("gantt"), m_ganttLinks);  // FEAT-075
    if (const QJsonObject images = markerImagesJson(); !images.isEmpty()) result.insert(QStringLiteral("markerImages"), images);  // FEAT-051
    return result;
}

bool MindMapModel::fromJson(const QJsonObject &object, QString *error) {
    if (!object["root"].isObject() || jsonTreeDepth(object["root"].toObject()) > kMaxTreeDepth  // BUG-046
        || !validTopicTrees(object["root"].toObject(), object["summaries"].toArray())) {  // BUG-047
        if (error) *error = tr("Invalid map format.");
        return false;
    }
    beginResetModel();
    m_root = nodeFromJson(object["root"].toObject(), nullptr);
    relations = Relationships::listFromJson(object["relations"].toArray());  // FEAT-053
    summaries = summariesFromJson(object["summaries"].toArray());  // FEAT-052
    boundaries = Boundaries::listFromJson(object["boundaries"].toArray());  // FEAT-052
    readPalette(object);
    readMarkerImages(object);  // FEAT-051
    readGantt(object);  // FEAT-075
    relinkSummaries();
    // FEAT-012 (decisione 2026-10-14): un file di prima senza la chiave si apre bilanciato se nessun
    // nodo e' stato spostato a mano; altrimenti con il bilanciamento spento e i rami principali a
    // destra, come li disponeva il layout di allora.
    if (!object.contains(QStringLiteral("balanced"))) {
        std::function<bool(const MindNode *)> moved = [&moved](const MindNode *node) {
            if (!node->manualOffset.isNull()) return true;
            return std::any_of(node->children.cbegin(), node->children.cend(), [&moved](const auto &child) { return moved(child.get()); });
        };
        balanced = !moved(m_root.get());
        if (!balanced)
            for (const auto &child : m_root->children)
                if (child->side == MindNode::Side::Auto) child->side = MindNode::Side::Right;
    }
    // FEAT-019: colori risolti prima che la scena ricrei i nodi, cosi' non vanno ridisegnati dopo.
    NodeStyle::resolveTree(m_root.get(), activePalette, backdrop(), sheetStyle, summaryTopics());
    endResetModel();
    emit structureChanged();
    emit backdropChanged();  // FEAT-036
    return true;
}

QHash<QString, QJsonObject> MindMapModel::toFlatSnapshot() const {
    QHash<QString, QJsonObject> snapshot;
    // Cache per nodo (vedi FlatSnapshotCache): un nodo con gli stessi valori, genitore e riga
    // dell'istantanea precedente riusa il suo QJsonObject (copia implicita, nessuna allocazione).
    // MapHistory::commit trova allora lo stesso dato condiviso e il confronto e' immediato.
    auto &entries = m_flatCache->entries;
    const quint32 generation = ++m_flatCache->generation;
    int visited = 0;
    std::function<void(const MindNode *, const QString &, int)> visit = [&](const MindNode *node, const QString &parentId, int row) {
        NodeFieldValues values = fieldValues(node);
        FlatSnapshotCache::Entry &entry = entries[node->id];
        const bool fresh = entry.json.isEmpty();  // un nodo non ha mai il JSON vuoto
        if (fresh || !(entry.values == values) || entry.parentId != parentId || entry.row != row) {
            // Voce gia' fatta: si cambiano solo i campi diversi (dopo una ridisposizione di solito
            // "x" e "y"). entry.json e' condiviso con lo storico: il primo insert ne fa una copia.
            writeFields(entry.json, values, fresh ? nullptr : &entry.values);
            if (fresh || entry.parentId != parentId) entry.json.insert(QLatin1StringView("parent"), parentId);
            if (fresh || entry.row != row) entry.json.insert(QLatin1StringView("row"), row);
            entry.values = std::move(values);
            entry.parentId = parentId;
            entry.row = row;
        }
        entry.generation = generation;
        snapshot.insert(node->id, entry.json);
        ++visited;
        int childRow = 0;
        for (const auto &child : node->children) visit(child.get(), node->id, childRow++);
    };
    snapshot.reserve(static_cast<qsizetype>(entries.size()) + 1);
    visit(m_root.get(), QString(), 0);
    // Nodi non piu' nella mappa: la cache resta grande quanto la mappa attuale.
    if (entries.size() > visited)
        entries.removeIf([generation](const auto &it) { return it.value().generation != generation; });
    // Stessi campi globali di toJson(), ma senza serializzare di nuovo l'albero.
    const QJsonArray relationArray = Relationships::toJson(relations);  // FEAT-053
    const QJsonArray summaryArray = summariesToJson(summaries);  // FEAT-052
    const QJsonArray boundaryArray = Boundaries::toJson(boundaries);  // FEAT-052
    QJsonArray paletteArray; for (const auto &color : activePalette) paletteArray.append(color);
    QJsonObject map{{"format", "mentis-1"}, {"rootId", m_root->id}, {"relations", relationArray},
                    {"summaries", summaryArray}, {"boundaries", boundaryArray}, {"palette", paletteArray},
                    {"paletteId", paletteId}};
    if (!paletteName.isEmpty()) map.insert(QStringLiteral("paletteName"), paletteName);  // FEAT-032
    if (paletteVariant >= 0) map.insert(QStringLiteral("paletteVariant"), paletteVariant);  // FEAT-036
    if (background) map.insert(QStringLiteral("background"), background->name());
    // FEAT-043: stile del foglio, solo i valori diversi dal predefinito.
    if (!sheetStyle.fontFamily.isEmpty()) map.insert(QStringLiteral("sheetFont"), sheetStyle.fontFamily);
    if (sheetStyle.branchWidth > 0) map.insert(QStringLiteral("sheetBranchWidth"), sheetStyle.branchWidth);
    if (sheetStyle.tapered) map.insert(QStringLiteral("sheetBranchTapered"), true);  // FEAT-023
    if (!sheetStyle.coloredBranches) map.insert(QStringLiteral("coloredBranches"), false);
    if (sheetStyle.lineColor != NodeStyle::kDefaultBranchLineColor) map.insert(QStringLiteral("branchLineColor"), sheetStyle.lineColor.name());
    if (sheetStyle.showNotes) map.insert(QStringLiteral("showNotes"), true);  // FEAT-059
    if (sheetStyle.compact) map.insert(QStringLiteral("compactMap"), true);  // FEAT-043
    if (sheetStyle.freeBranches) map.insert(QStringLiteral("freeBranches"), true);
    if (sheetStyle.justify) map.insert(QStringLiteral("justifyTopics"), true);
    if (!sheetStyle.variant.isEmpty()) map.insert(QStringLiteral("sheetVariant"), sheetStyle.variant);  // FEAT-026
    map.insert(QStringLiteral("balanced"), balanced);  // FEAT-012
    if (!pitchTheme.isEmpty()) map.insert(QStringLiteral("pitchTheme"), pitchTheme);  // FEAT-063
    if (!pitchAspect.isEmpty()) map.insert(QStringLiteral("pitchAspect"), pitchAspect);
    if (!pitchTemplate.isEmpty()) map.insert(QStringLiteral("pitchTemplate"), pitchTemplate);  // FEAT-089
    if (!m_ganttLinks.isEmpty()) map.insert(QStringLiteral("gantt"), m_ganttLinks);  // FEAT-075
    if (const QJsonObject images = markerImagesJson(); !images.isEmpty()) map.insert(QStringLiteral("markerImages"), images);  // FEAT-051
    snapshot.insert(QStringLiteral("@map"), map);
    return snapshot;
}

bool MindMapModel::fromFlatSnapshot(const QHash<QString, QJsonObject> &snapshot, QString *error) {
    QJsonObject map = snapshot.value(QStringLiteral("@map"));
    const QString rootId = map.take("rootId").toString();
    // Ricostruisce l'albero annidato raggruppando i nodi per genitore e ordinandoli per "row".
    QHash<QString, QList<QJsonObject>> childrenByParent;
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it)
        if (it.key() != QStringLiteral("@map")) childrenByParent[it.value()["parent"].toString()].append(it.value());
    QSet<QString> built;  // BUG-047: a topic met twice (a cycle) stops the rebuild
    bool cycle = false;
    std::function<QJsonObject(const QString &)> build = [&](const QString &id) {
        QJsonObject node = snapshot.value(id);
        if (cycle || built.contains(id)) { cycle = true; return node; }
        built.insert(id);
        auto children = childrenByParent.value(id);
        std::sort(children.begin(), children.end(), [](const QJsonObject &a, const QJsonObject &b) { return a["row"].toInt() < b["row"].toInt(); });
        QJsonArray array;
        for (const auto &child : children) array.append(build(child["id"].toString()));
        node.remove("parent"); node.remove("row");
        node["children"] = array;
        return node;
    };
    if (!snapshot.contains(rootId)) { if (error) *error = tr("Invalid history."); return false; }
    map["root"] = build(rootId);
    if (cycle) { if (error) *error = tr("Invalid history."); return false; }
    return fromJson(map, error);
}

void MindMapModel::clearManualOffsets() {
    std::function<void(MindNode *)> visit = [&](MindNode *node) {
        node->manualOffset = {};
        for (const auto &child : node->children) visit(child.get());
    };
    visit(m_root.get());
}

bool MindMapModel::applyFlatChanges(const QHash<QString, QJsonObject> &target, const QStringList &changedKeys) {
    const QString mapKey = QStringLiteral("@map");
    if (target.value(mapKey).value(QStringLiteral("rootId")).toString() != m_root->id) return false;
    // Indice id -> nodo, aggiornato a ogni inserimento/rimozione.
    QHash<QString, MindNode *> byId;
    std::function<void(MindNode *)> index = [&](MindNode *node) {
        byId.insert(node->id, node);
        for (const auto &child : node->children) index(child.get());
    };
    std::function<void(MindNode *)> unindex = [&](MindNode *node) {
        byId.remove(node->id);
        for (const auto &child : node->children) unindex(child.get());
    };
    index(m_root.get());
    const auto targetRow = [&](const QString &id) { return target.value(id).value(QStringLiteral("row")).toInt(); };
    const auto targetParent = [&](const QString &id) { return target.value(id).value(QStringLiteral("parent")).toString(); };
    QSet<MindNode *> touchedParents;

    // 1. Nodi che nello stato target non esistono: si rimuovono (con il loro ramo).
    QList<MindNode *> toRemove;
    for (const QString &key : changedKeys)
        if (key != mapKey && !target.contains(key) && byId.contains(key)) toRemove.append(byId.value(key));
    // The tops of the removed branches are chosen before removing anything: removing a branch frees
    // its descendants, which may come later in the list (a pasted branch undone, CI 2026-11-03).
    QList<MindNode *> branchTops;
    for (auto *node : std::as_const(toRemove)) {
        bool insideRemoved = false;
        for (auto *ancestor = node->parent; ancestor && !insideRemoved; ancestor = ancestor->parent)
            insideRemoved = toRemove.contains(ancestor);
        if (!insideRemoved && node->parent) branchTops.append(node);
    }
    for (auto *node : std::as_const(branchTops)) {
        touchedParents.insert(node->parent);
        unindex(node);
        removeNode(node, false);
    }
    touchedParents.removeIf([&](MindNode *node) { return !byId.contains(node->id) || byId.value(node->id) != node; });

    // 2. Nodi nuovi: prima i genitori, poi i figli (più passate finché si inserisce qualcosa).
    QStringList pending;
    for (const QString &key : changedKeys)
        if (key != mapKey && target.contains(key) && !byId.contains(key)) pending.append(key);
    while (!pending.isEmpty()) {
        QStringList next;
        for (const QString &id : std::as_const(pending)) {
            MindNode *parentNode = byId.value(targetParent(id));
            if (!parentNode) { next.append(id); continue; }
            auto node = makeNode(QString(), parentNode);
            node->id = id;
            applyNodeFields(node.get(), target.value(id));
            const int row = std::clamp(targetRow(id), 0, static_cast<int>(parentNode->children.size()));
            auto *created = node.get();
            beginInsertRows(indexFromNode(parentNode), row, row);
            parentNode->children.insert(parentNode->children.begin() + row, std::move(node));
            endInsertRows();
            byId.insert(id, created);
            touchedParents.insert(parentNode);
        }
        if (next.size() == pending.size()) return false;  // genitore mai trovato: stato incoerente
        pending = next;
    }

    // 3. Nodi cambiati: campi, ed eventuale nuovo genitore.
    for (const QString &key : changedKeys) {
        if (key == mapKey || !target.contains(key)) continue;
        MindNode *node = byId.value(key);
        if (!node) return false;
        const QJsonObject &fields = target.value(key);
        if (node->parent) {
            MindNode *newParent = byId.value(targetParent(key));
            if (!newParent) return false;
            touchedParents.insert(node->parent);
            touchedParents.insert(newParent);
            if (newParent != node->parent && !moveNode(node, newParent, -1)) return false;
        }
        QJsonObject current = nodeFields(node);
        QJsonObject wanted = fields;
        wanted.remove(QStringLiteral("parent")); wanted.remove(QStringLiteral("row"));
        if (current != wanted) {
            // Dopo un moveNode manualOffset è azzerato: qui torna quello dello stato target.
            applyNodeFields(node, fields);
            const QModelIndex idx = indexFromNode(node);
            if (idx.isValid()) emit dataChanged(idx, idx, {Qt::EditRole, Qt::DisplayRole});
        }
    }

    // 4. Ordine dei figli come nello stato target.
    for (MindNode *parentNode : std::as_const(touchedParents)) {
        QList<QPair<int, QString>> wanted;
        for (const auto &child : parentNode->children) {
            if (!target.contains(child->id)) return false;
            wanted.append({targetRow(child->id), child->id});
        }
        std::sort(wanted.begin(), wanted.end());
        for (int i = 0; i < wanted.size(); ++i) {
            if (parentNode->children[i]->id == wanted[i].second) continue;
            MindNode *node = byId.value(wanted[i].second);
            if (!node || !moveNode(node, parentNode, i)) return false;
        }
    }

    // 5. Relazioni, sommari, confini e palette.
    if (changedKeys.contains(mapKey)) {
        const QJsonObject object = target.value(mapKey);
        relations = Relationships::listFromJson(object["relations"].toArray());  // FEAT-053
        // FEAT-052: a summary kept keeps its topic object (the scene's item points to it).
        QList<Summary> next = summariesFromJson(object["summaries"].toArray());
        for (Summary &summary : next)
            for (const Summary &old : std::as_const(summaries))
                if (old.id == summary.id) {
                    applyNodeFields(old.topic.get(), nodeFields(summary.topic.get()));
                    old.topic->id = summary.topic->id;
                    // Its branch is read again (the scene remakes those items).
                    emit summaryBranchAboutToChange(old.topic.get());
                    old.topic->children = std::move(summary.topic->children);
                    for (const auto &child : old.topic->children) child->parent = old.topic.get();
                    emit summaryBranchChanged(old.topic.get());
                    summary.topic = old.topic;
                }
        summaries = next;
        relinkSummaries();
        boundaries = Boundaries::listFromJson(object["boundaries"].toArray());  // FEAT-052
        readPalette(object);
        readMarkerImages(object);  // FEAT-051
        readGantt(object);  // FEAT-075
        emit backdropChanged();  // FEAT-036: variante o sfondo tornati indietro
    }

    // 6. Controllo: le voci toccate devono coincidere con il target (campi, genitore, riga).
    for (const QString &key : changedKeys) {
        if (key == mapKey || !target.contains(key)) continue;
        MindNode *node = byId.value(key);
        if (!node) return false;
        QJsonObject fields = nodeFields(node);
        fields["parent"] = node->parent ? node->parent->id : QString();
        fields["row"] = rowOf(node);
        if (fields != target.value(key)) return false;
    }
    emit structureChanged();
    return true;
}
