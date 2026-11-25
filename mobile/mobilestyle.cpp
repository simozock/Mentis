#include "mobilestyle.h"

#include "bmapstorage.h"
#include "boundary.h"
#include "document.h"
#include "formulatext.h"
#include "ganttstylechoices.h"
#include "mapsnapshot.h"
#include "mapstructure.h"
#include "mindmapmodel.h"
#include "mobilemap.h"
#include "nodegeometry.h"
#include "nodestyle.h"
#include "numbering.h"
#include "relationship.h"
#include "stylechoices.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {
QVariantMap readingMap(const NodeStyle::Reading &reading) {
    return {{QStringLiteral("empty"), reading.empty}, {QStringLiteral("mixed"), reading.mixed},
            {QStringLiteral("manual"), !reading.empty && !reading.mixed && reading.allManual},
            {QStringLiteral("value"), reading.empty || reading.mixed ? QVariant() : reading.value}};
}

QVariantList choiceList(const QList<StyleChoices::Choice> &choices) {
    QVariantList list;
    for (const StyleChoices::Choice &choice : choices)
        list.append(QVariantMap{{QStringLiteral("value"), choice.value}, {QStringLiteral("name"), choice.name}});
    return list;
}

QVariantMap choice(const QVariant &value, const QString &name) {
    return {{QStringLiteral("value"), value}, {QStringLiteral("name"), name}};
}

// The named line widths of the relationships' and boundaries' selects (RelationshipStylePage).
QVariantList namedWidths() {
    QVariantList list;
    for (int i = 0; i < NodeStyle::kNamedBranchWidths.size(); ++i) list.append(choice(NodeStyle::kNamedBranchWidths[i], StyleChoices::branchWidthName(i)));
    return list;
}

// A value of one object, always its own (ImageStylePage's reading).
QVariantMap ownReading(const QVariant &value) {
    return {{QStringLiteral("empty"), false}, {QStringLiteral("mixed"), false}, {QStringLiteral("manual"), true}, {QStringLiteral("value"), value}};
}

// A colour of a relationship or boundary: its own, or the default shown (manual = its own).
QVariantMap colorReading(const std::optional<QColor> &own, const QColor &fallback) {
    return {{QStringLiteral("empty"), false}, {QStringLiteral("mixed"), false}, {QStringLiteral("manual"), own.has_value()},
            {QStringLiteral("value"), own.value_or(fallback)}};
}

// The desktop's StylePage::pxToPt and ptToPx: sizes are shown in px, kept in points.
qreal pxToPt(qreal px) { return px * 3 / 4; }
qreal ptToPx(qreal pt) { return pt * 4 / 3; }
}  // namespace

MobileStyle::MobileStyle(QObject *parent) : QObject(parent) {}

void MobileStyle::setMap(MobileMap *map) {
    if (m_map == map) return;
    if (m_map) disconnect(m_map, nullptr, this, nullptr);
    m_map = map;
    if (m_map)
        for (auto signal : {&MobileMap::selectionChanged, &MobileMap::contentChanged, &MobileMap::snapshotChanged, &MobileMap::opened})
            connect(m_map, signal, this, &MobileStyle::bump);
    emit mapChanged();
    bump();
}

void MobileStyle::bump() {
    ++m_revision;
    emit changed();
}

QList<MindNode *> MobileStyle::nodes() const { return m_map && m_map->isOpen() ? m_map->targetNodes() : QList<MindNode *>{}; }

QList<const MindNode *> MobileStyle::constNodes() const {
    QList<const MindNode *> list;
    for (MindNode *node : nodes()) list.append(node);
    return list;
}

int MobileStyle::topicCount() const { return int(nodes().size()); }

int MobileStyle::target() const {
    // MainWindow::refreshStylePage: the picture's page, then the boundary's, then the relationship's.
    if (!m_map || !m_map->isOpen()) return Topics;
    if (m_map->pictureSelected()) return Picture;
    if (m_map->selectedBoundary()) return BoundaryTarget;
    if (m_map->selectedRelation()) return Relation;
    return Topics;
}

MindNode *MobileStyle::pictureNode() const { return m_map && m_map->isOpen() && m_map->pictureSelected() ? m_map->selectedNode() : nullptr; }

bool MobileStyle::central() const {
    const QList<MindNode *> selected = nodes();
    return selected.size() == 1 && !selected.front()->parent;
}

bool MobileStyle::edited(bool changed) {
    // MobileMap::pasteStyle: the change, its layout and one Undo step.
    if (!changed) return false;
    m_map->m_document->markChanged();
    m_map->finishEdit(m_map->m_selectedId);
    return true;
}

QVariantMap MobileStyle::reading(int property) const {
    if (property >= SheetLineColor) return sheetReading(property);
    if (property >= PictureBorder) return objectReading(property);
    const QList<const MindNode *> selected = constNodes();
    NodeStyle::Reading reading = NodeStyle::read(selected, static_cast<StyleProperty>(property));
    if (property == BranchShape && !reading.empty && !reading.mixed && static_cast<::BranchShape>(reading.value.toInt()) == ::BranchShape::Auto)
        reading.value = int(selected.front()->parent ? ::BranchShape::RoundedBracket : ::BranchShape::Curve);
    if (property == FontSize && !reading.empty && !reading.mixed) reading.value = ptToPx(reading.value.toDouble());  // shown in px
    return readingMap(reading);
}

QVariantList MobileStyle::choices(int property) const {
    switch (property) {
    case PictureBorder:
        return {choice(int(ImageStyle::Border::Solid), QCoreApplication::translate("ImageStylePage", "Solid")),
                choice(int(ImageStyle::Border::Dashed), QCoreApplication::translate("ImageStylePage", "Dashed")),
                choice(int(ImageStyle::Border::Dotted), QCoreApplication::translate("ImageStylePage", "Dotted")),
                choice(int(ImageStyle::Border::None), QCoreApplication::translate("ImageStylePage", "No border"))};
    case RelationShape:
        return {choice(int(Relationship::Shape::Curved), QCoreApplication::translate("RelationshipStylePage", "Curved")),
                choice(int(Relationship::Shape::Straight), QCoreApplication::translate("RelationshipStylePage", "Straight")),
                choice(int(Relationship::Shape::Elbow), QCoreApplication::translate("RelationshipStylePage", "Elbow"))};
    case BoundaryShape: return choiceList(StyleChoices::boundaryShapes());
    case BoundaryFillStyle: return choiceList(StyleChoices::fillStyles());  // FEAT-134
    case RelationPattern: return choiceList(StyleChoices::linePatterns(false));
    case BoundaryPattern: return choiceList(StyleChoices::boundaryPatterns());
    case RelationWidth:
    case BoundaryWidth: return namedWidths();
    case RelationStart:
    case RelationEnd: return choiceList(StyleChoices::lineEnds());
    case RelationFont:
    case BoundaryTitleFont: {
        // RelationshipStylePage, BoundaryStylePage: the sheet's font first (value empty), then the installed fonts.
        QVariantList list{choice(QString(), NodeStyle::kDefaultMapFontFamily)};
        for (const QString &family : QFontDatabase::families())
            if (!family.startsWith(QLatin1Char('@')) && family != NodeStyle::kDefaultMapFontFamily) list.append(choice(family, family));
        return list;
    }
    case RelationFontSize: {
        // The 13 px default is not among the usual sizes: it comes first, as on the desktop.
        QVariantList list{choice(Relationships::kDefaultFontSize, QString::number(int(Relationships::kDefaultFontSize)))};
        for (const int px : NodeStyle::kFontSizesPx) list.append(choice(qreal(px), QString::number(px)));
        return list;
    }
    case BoundaryTitleSize: {
        QVariantList list;
        for (const int px : NodeStyle::kFontSizesPx) list.append(choice(qreal(px), QString::number(px)));
        return list;
    }
    case Preset: return choiceList(StyleChoices::presets());
    case FillStyle: return choiceList(StyleChoices::fillStyles());
    case BorderStyle: return choiceList(StyleChoices::borderStyles());
    case BranchStyle: return choiceList(StyleChoices::linePatterns(true));
    case BranchShape: return choiceList(StyleChoices::branchShapes());
    case BranchStart:
    case BranchEnd: return choiceList(StyleChoices::lineEnds());
    case TextCase: return choiceList(StyleChoices::textCases());
    case PictureEffect: return choiceList(StyleChoices::imageEffects());  // FEAT-131
    case TextAlign: return choiceList(StyleChoices::textAligns());
    case FontFamily: return fontFamilies();
    case FontWeight: return fontWeights();
    case FontSize: return fontSizes();
    case BranchWidth: {
        // A width outside the named ones (an older file) is shown as a number, as on the desktop.
        QVariantList list;
        for (int i = 0; i < NodeStyle::kNamedBranchWidths.size(); ++i)
            list.append(QVariantMap{{QStringLiteral("value"), NodeStyle::kNamedBranchWidths[i]}, {QStringLiteral("name"), StyleChoices::branchWidthName(i)}});
        const NodeStyle::Reading width = NodeStyle::read(constNodes(), StyleProperty::BranchWidth);
        if (!width.empty && !width.mixed && !NodeStyle::kNamedBranchWidths.contains(width.value.toDouble()))
            list.append(QVariantMap{{QStringLiteral("value"), width.value.toDouble()},
                                    {QStringLiteral("name"), QCoreApplication::translate("StylePage", "%1 px").arg(width.value.toDouble())}});
        return list;
    }
    case Shape:
    case PictureShape: {  // FEAT-129: the topic shapes
        QVariantList list;
        for (const StyleChoices::Group &group : StyleChoices::shapeGroups())
            for (qsizetype i = 0; i < group.choices.size(); ++i)
                list.append(QVariantMap{{QStringLiteral("value"), group.choices[i].value}, {QStringLiteral("name"), group.choices[i].name},
                                        {QStringLiteral("group"), i == 0 ? group.title : QString()}});
        return list;
    }
    default: return {};
    }
}

int MobileStyle::iconKind(int property) const {
    switch (property) {
    case FillStyle:
    case BoundaryFillStyle: return FillStyleIcon;
    case BorderStyle: return BorderStyleIcon;
    case Shape:
    case PictureShape: return ShapeIcon;
    case BranchStyle: return BranchLineIcon;
    case BranchShape: return BranchShapeIcon;
    case BranchStart: return LineStartIcon;
    case BranchEnd: return LineEndIcon;
    case PictureBorder: return PictureBorderIcon;
    case RelationPattern:
    case BoundaryPattern: return LinePatternIcon;
    case BoundaryShape: return BoundaryShapeIcon;
    case RelationStart: return LineStartIcon;
    case RelationEnd: return LineEndIcon;
    default: return -1;
    }
}

QString MobileStyle::choiceName(int property, const QVariant &value) const {
    for (const QVariant &item : choices(property)) {
        const QVariantMap choice = item.toMap();
        if (choice.value(QStringLiteral("value")) == value) return choice.value(QStringLiteral("name")).toString();
    }
    return {};
}

bool MobileStyle::choose(int property, const QVariant &value) {
    if (property >= SheetLineColor) return chooseSheetColor(property, value.value<QColor>());
    if (property >= PictureBorder) return chooseObject(property, value);
    const QList<MindNode *> selected = nodes();
    if (selected.isEmpty()) return false;
    QVariant stored = value;
    // QML's numbers are doubles: the model's enums and weights are ints, colours QColor.
    switch (property) {
    case FillColor: case TextColor: case BorderColor: case BranchColor: stored = value.value<QColor>(); break;
    case BranchWidth: case BorderWidth: case FontSize: stored = value.toDouble(); break;
    case FontFamily: stored = value.toString(); break;
    case Italic: case Underline: case StrikeOut: stored = value.toBool(); break;
    case Numbering: return false;  // chooseNumbering
    default: stored = value.toInt(); break;
    }
    return edited(m_map->m_document->model()->setStyle(selected, static_cast<StyleProperty>(property), stored));
}

QVariantMap MobileStyle::widthReading() const {
    // StylePage::setNodes: whole px; light only when every topic's width is set by hand.
    NodeStyle::Reading width;
    const QList<MindNode *> selected = nodes();
    const MapSnapshot *snapshot = m_map ? m_map->snapshot() : nullptr;
    for (const MindNode *node : selected) {
        const MapSnapshot::Topic *topic = snapshot ? snapshot->topicOf(node) : nullptr;
        if (!topic) return readingMap(NodeStyle::Reading());
        const qreal px = std::round(topic->geometry->textRect().width());
        if (width.empty) {
            width.empty = false;
            width.value = px;
            width.allManual = true;
        }
        width.mixed = width.mixed || px != width.value.toDouble();
        width.allManual = width.allManual && node->customWidth > 0;
    }
    return readingMap(width);
}

bool MobileStyle::canFitWidth() const {
    const QList<MindNode *> selected = nodes();
    return std::any_of(selected.cbegin(), selected.cend(), [](const MindNode *node) { return node->customWidth > 0; });
}

qreal MobileStyle::manualWidthFor(const MindNode *node, qreal requestedPx) const {
    // MindMapScene::manualWidthFor: between the topic's minimum (its widest character with the
    // margins) and kMaxManualNodeWidthPx.
    const qreal requested = std::round(requestedPx);
    const MapSnapshot::Topic *topic = m_map->snapshot() ? m_map->snapshot()->topicOf(node) : nullptr;
    if (!topic) return std::clamp(requested, 1.0, kMaxManualNodeWidthPx);
    if (requested >= std::ceil(topic->geometry->textRect().width())) return std::min(requested, kMaxManualNodeWidthPx);
    return std::clamp(requested, std::min(topic->geometry->minimumCustomWidth(), kMaxManualNodeWidthPx), kMaxManualNodeWidthPx);
}

bool MobileStyle::applyWidths(const std::function<qreal(const MindNode *)> &widthFor) {
    QList<std::pair<MindNode *, qreal>> widths;
    for (MindNode *node : nodes()) widths.append({node, widthFor(node)});
    return !widths.isEmpty() && edited(m_map->m_document->model()->setCustomWidths(widths));
}

bool MobileStyle::chooseWidth(qreal px) {
    return applyWidths([this, px](const MindNode *node) { return manualWidthFor(node, px); });
}

bool MobileStyle::stepWidth(qreal deltaPx) {
    const MapSnapshot *snapshot = m_map ? m_map->snapshot() : nullptr;
    return applyWidths([this, snapshot, deltaPx](const MindNode *node) {
        const MapSnapshot::Topic *topic = snapshot ? snapshot->topicOf(node) : nullptr;
        return manualWidthFor(node, (topic ? std::round(topic->geometry->textRect().width()) : 0) + deltaPx);
    });
}

bool MobileStyle::fitWidth() { return applyWidths([](const MindNode *) { return 0.0; }); }

bool MobileStyle::showApplyToRelated() const {
    const QList<MindNode *> selected = nodes();
    return selected.size() == 1 && selected.front()->parent;
}

bool MobileStyle::canApplyToRelated() const {
    const QList<MindNode *> selected = nodes();
    return selected.size() == 1 && selected.front()->style != NodeStyleOverrides();
}

bool MobileStyle::canApplyToChildren() const {
    const QList<MindNode *> selected = nodes();
    return std::any_of(selected.cbegin(), selected.cend(), [](const MindNode *node) {
        return std::any_of(node->children.cbegin(), node->children.cend(), [](const auto &child) { return !child->floating; });
    });
}

bool MobileStyle::canResetStyle() const {
    const QList<MindNode *> selected = nodes();
    return std::any_of(selected.cbegin(), selected.cend(), [](const MindNode *node) { return node->style != NodeStyleOverrides(); });
}

bool MobileStyle::applyToRelated() {
    // MainWindow (FEAT-072 B15): the same style to the siblings, floating ones apart.
    const QList<MindNode *> selected = nodes();
    if (selected.size() != 1 || !selected.front()->parent) return false;
    MindNode *source = selected.front();
    QList<MindNode *> related;
    for (const auto &child : source->parent->children)
        if (child->floating == source->floating) related.append(child.get());
    return edited(m_map->m_document->model()->copyStyle(source, related));
}

bool MobileStyle::applyToChildren() { return edited(m_map->m_document->model()->copyStyleToDescendants(nodes())); }

bool MobileStyle::resetStyle() { return edited(m_map->m_document->model()->resetStyle(nodes())); }

QVariantList MobileStyle::structureTypes() const {
    // StructurePicker: Same as parent for a topic; no Mind map inside a tree table (FEAT-028).
    const QList<MindNode *> selected = nodes();
    if (selected.size() != 1) return {};
    QVariantList list;
    const bool topic = selected.front()->parent;
    if (topic) list.append(QVariantMap{{QStringLiteral("key"), QString()}, {QStringLiteral("name"), QCoreApplication::translate("StructurePicker", "Same as parent")}});
    const bool inTable = topic && MapStructure::insideTreeTable(selected.front());
    for (const MapStructure::Type type : StyleChoices::structureTypes()) {
        if (inTable && type == MapStructure::Type::MindMap) continue;
        list.append(QVariantMap{{QStringLiteral("key"), MapStructure::typeKey(type)}, {QStringLiteral("name"), StyleChoices::structureName(type)}});
    }
    return list;
}

QString MobileStyle::structureType() const {
    const QList<MindNode *> selected = nodes();
    if (selected.size() != 1) return {};
    const MindNode *node = selected.front();
    return node->structure.isEmpty() && !node->parent ? MapStructure::typeKey(MapStructure::Type::MindMap) : node->structure;
}

QVariantList MobileStyle::structureDirections(const QString &typeKey) const {
    QVariantList list;
    if (typeKey.isEmpty()) return list;  // inherited: the parent's direction
    const MapStructure::Type type = MapStructure::typeFromKey(typeKey);
    const QList<MindNode *> selected = nodes();
    // Only the directions that change the layout can be picked (StructurePicker on the desktop).
    const QList<MapStructure::Direction> effective = selected.size() == 1
        ? MapStructure::effectiveDirections(selected.front(), type) : MapStructure::directions(type);
    for (const MapStructure::Direction direction : MapStructure::directions(type))
        list.append(QVariantMap{{QStringLiteral("key"), MapStructure::directionKey(direction)}, {QStringLiteral("name"), StyleChoices::directionName(direction)},
                                {QStringLiteral("enabled"), effective.contains(direction)}});
    return list;
}

QString MobileStyle::structureDirection() const {
    const QList<MindNode *> selected = nodes();
    const QString type = structureType();
    if (selected.size() != 1 || type.isEmpty()) return {};
    return MapStructure::directionKey(MapStructure::directionFromKey(selected.front()->structureDirection, MapStructure::typeFromKey(type)));
}

bool MobileStyle::chooseStructure(const QString &typeKey, const QString &directionKey) {
    // StructurePicker: a new type starts with its first direction. MainWindow::setStructure.
    const QList<MindNode *> selected = nodes();
    if (selected.size() != 1) return false;
    QString direction = directionKey;
    if (!typeKey.isEmpty() && direction.isEmpty())
        direction = MapStructure::directionKey(MapStructure::directions(MapStructure::typeFromKey(typeKey)).front());
    if (!m_map->m_document->setStructure(selected.front(), typeKey, typeKey.isEmpty() ? QString() : direction)) return false;
    m_map->finishEdit(m_map->m_selectedId);
    return true;
}

QVariantMap MobileStyle::numbering(bool sheet) const {
    // NumberingFields::setNodes.
    QList<const MindNode *> selected = constNodes();
    if (sheet) selected = m_map && m_map->isOpen() ? QList<const MindNode *>{m_map->m_document->model()->root()} : QList<const MindNode *>{};
    const NodeStyle::Reading reading = NodeStyle::read(selected, StyleProperty::Numbering);
    BranchNumbering numbering;
    for (const MindNode *node : selected)
        if (node->style.numbering) { numbering = *node->style.numbering; break; }
    int preset = -1;
    if (!numbering.format.isEmpty()) preset = ::Numbering::presetIndex(numbering) >= 0 ? ::Numbering::presetIndex(numbering) : -2;
    return {{QStringLiteral("empty"), reading.empty}, {QStringLiteral("mixed"), reading.mixed}, {QStringLiteral("preset"), preset},
            {QStringLiteral("format"), numbering.format}, {QStringLiteral("separator"), QString(numbering.separator)},
            {QStringLiteral("trailingSeparator"), numbering.trailingSeparator}, {QStringLiteral("start"), numbering.start},
            {QStringLiteral("hierarchical"), numbering.hierarchical}, {QStringLiteral("levels"), numbering.levels}};
}

QStringList MobileStyle::numberingPresets(const QString &separator) const {
    return ::Numbering::presetFormats(separator.isEmpty() ? QChar(u'.') : separator.front());
}

QStringList MobileStyle::numberingSeparators() const {
    QStringList list;
    for (const QChar separator : ::Numbering::separators()) list.append(QString(separator));
    return list;
}

int MobileStyle::numberingMaxLevels() const { return ::Numbering::kMaxLevels; }
int MobileStyle::numberingMaxStart() const { return ::Numbering::kMaxStart; }

namespace {
BranchNumbering numberingOf(const QVariantMap &map) {
    BranchNumbering numbering;
    numbering.format = map.value(QStringLiteral("format")).toString().trimmed();
    const QString separator = map.value(QStringLiteral("separator")).toString();
    numbering.separator = separator.isEmpty() ? QChar(u'.') : separator.front();
    numbering.trailingSeparator = map.value(QStringLiteral("trailingSeparator")).toBool();
    numbering.start = map.value(QStringLiteral("start"), 1).toInt();
    numbering.hierarchical = map.value(QStringLiteral("hierarchical"), true).toBool();
    numbering.levels = map.value(QStringLiteral("levels")).toInt();
    return numbering;
}
}  // namespace

bool MobileStyle::numberingValid(const QVariantMap &map) const {
    const BranchNumbering numbering = numberingOf(map);
    return numbering.format.isEmpty() || ::Numbering::isValid(numbering);
}

bool MobileStyle::chooseNumbering(const QVariantMap &map, bool sheet) {
    QList<MindNode *> selected = nodes();
    if (sheet) selected = m_map && m_map->isOpen() ? QList<MindNode *>{m_map->m_document->model()->root()} : QList<MindNode *>{};
    const BranchNumbering numbering = numberingOf(map);
    if (selected.isEmpty() || !numberingValid(map)) return false;
    return edited(m_map->m_document->model()->setStyle(selected, StyleProperty::Numbering, QVariant::fromValue(numbering)));
}

QVariantList MobileStyle::fontFamilies() const {
    // StylePage::textSection: the default first (its family's name, value empty), then the fonts;
    // a font of the map not installed here is shown by its name (StylePage::showText).
    QVariantList list{QVariantMap{{QStringLiteral("value"), QString()}, {QStringLiteral("name"), NodeStyle::kDefaultMapFontFamily}}};
    const QStringList families = QFontDatabase::families();
    for (const QString &family : families)
        if (!family.startsWith(QLatin1Char('@')) && family != NodeStyle::kDefaultMapFontFamily)
            list.append(QVariantMap{{QStringLiteral("value"), family}, {QStringLiteral("name"), family}});
    const NodeStyle::Reading family = NodeStyle::read(constNodes(), StyleProperty::FontFamily);
    const QString shown = family.value.toString();
    if (!family.empty && !family.mixed && !shown.isEmpty() && !families.contains(shown))
        list.append(QVariantMap{{QStringLiteral("value"), shown},
                                {QStringLiteral("name"), QCoreApplication::translate("StylePage", "%1 (not installed)").arg(shown)}});
    return list;
}

QVariantList MobileStyle::fontWeights() const {
    const QList<const MindNode *> selected = constNodes();
    const NodeStyle::Reading family = NodeStyle::read(selected, StyleProperty::FontFamily);
    const NodeStyle::Reading weight = NodeStyle::read(selected, StyleProperty::FontWeight);
    QVariantList list;
    for (const int value : StyleChoices::fontWeights(family.mixed || family.empty ? QString() : family.value.toString(),
                                                     weight.mixed || weight.empty ? 0 : weight.value.toInt()))
        list.append(QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("name"), StyleChoices::weightName(value)}});
    return list;
}

QVariantList MobileStyle::fontSizes() const {
    QVariantList list;
    for (const int px : NodeStyle::kFontSizesPx) list.append(QVariantMap{{QStringLiteral("value"), px}, {QStringLiteral("name"), QString::number(px)}});
    return list;
}

bool MobileStyle::chooseFontSizePx(qreal px) {
    const qreal size = pxToPt(px);
    if (!NodeStyle::isValidValue(StyleProperty::FontSize, size)) return false;
    return choose(FontSize, size);
}

QColor MobileStyle::presetColor() const {
    const QList<MindNode *> selected = nodes();
    if (!selected.isEmpty()) return selected.front()->branchColor;
    const QStringList colors = themeColors();
    return QColor(colors.value(0, QStringLiteral("#ff6b6b")));
}

int MobileStyle::presetDepth() const {
    const QList<MindNode *> selected = nodes();
    int depth = 0;
    if (!selected.isEmpty())
        for (const MindNode *node = selected.front(); node->parent; node = node->parent) ++depth;
    return std::min(depth, 3);
}

QStringList MobileStyle::themeColors() const {
    return m_map && m_map->isOpen() ? m_map->m_document->model()->activePalette : QStringList();
}

QVariantMap MobileStyle::topicPreview() const {
    const QList<MindNode *> selected = nodes();
    if (selected.size() != 1) return {};
    const MindNode *node = selected.front();
    const bool border = node->borderWidth > 0 && node->border.alpha() > 0;  // as StylePage's topicPreview
    return {{QStringLiteral("title"), node->title}, {QStringLiteral("fill"), node->fill},
            {QStringLiteral("border"), border ? node->border : QColor()}, {QStringLiteral("text"), node->text},
            {QStringLiteral("weight"), node->fontWeight}};
}

QStringList MobileStyle::gridColors() const { return StyleChoices::gridColors(); }
QStringList MobileStyle::recentColors() const { return StyleChoices::recentColors(); }

bool MobileStyle::chooseColor(int property, const QColor &color) {
    // ColorPicker::choose: every chosen colour joins the recent ones.
    if (!color.isValid()) return false;
    StyleChoices::rememberRecentColor(color);
    return choose(property, color);
}

QVariantMap MobileStyle::objectReading(int property) const {
    const QVariantMap none{{QStringLiteral("empty"), true}, {QStringLiteral("mixed"), false}, {QStringLiteral("manual"), false}, {QStringLiteral("value"), QVariant()}};
    if (property <= PictureOpacity) {
        const MindNode *node = pictureNode();
        if (!node) return none;
        const ImageStyle &style = node->imageStyle;
        switch (property) {
        case PictureBorder: return ownReading(int(style.border));
        case PictureShape: return ownReading(int(style.shape));
        case PictureEffect: return ownReading(int(style.effect));
        case PictureBorderWidth: return ownReading(style.borderWidth);
        case PictureBorderColor: return ownReading(style.borderColor);
        case PictureShadow: return ownReading(style.shadow);
        case PictureOutline: return ownReading(style.outline);
        default: return ownReading(style.opacity);
        }
    }
    if (!m_map || !m_map->isOpen()) return none;
    const NodeStyle::Backdrop backdrop = m_map->m_document->model()->backdrop();
    if (property <= RelationTextColor) {
        const MapSnapshot::Relation *relation = m_map->selectedRelation();
        if (!relation) return none;
        const Relationship &r = relation->relationship;
        switch (property) {
        case RelationShape: return ownReading(int(r.shape));
        case RelationPattern: return ownReading(r.linePattern.value_or(Relationships::kDefaultLinePattern));
        case RelationWidth: return ownReading(r.lineWidth.value_or(Relationships::kDefaultLineWidth));
        case RelationLineColor: return colorReading(r.lineColor, NodeStyle::annotationColor(Relationships::kDefaultLineColor, backdrop));
        case RelationStart: return ownReading(int(r.startMark.value_or(Relationships::kDefaultStartMark)));
        case RelationEnd: return ownReading(int(r.endMark.value_or(Relationships::kDefaultEndMark)));
        case RelationFont: return ownReading(r.fontFamily);
        case RelationFontSize: return ownReading(r.fontSize.value_or(Relationships::kDefaultFontSize));
        default: return colorReading(r.textColor, NodeStyle::annotationColor(QColor("#303640"), backdrop));
        }
    }
    const MapSnapshot::BoundaryShape *shape = m_map->selectedBoundary();
    if (!shape) return none;
    const Boundary &b = shape->boundary;
    // MainWindow::refreshStylePage: the backdrop's readable ink at the boundaries' alphas.
    const QColor ink = NodeStyle::annotationColor(Qt::black, backdrop);
    QColor line = ink, fill = ink;
    line.setAlphaF(Boundaries::kLineAlpha);
    fill.setAlphaF(Boundaries::kFillAlpha);
    switch (property) {
    case BoundaryShape: return ownReading(int(b.shape));
    case BoundaryFillColor: return colorReading(b.fillColor, fill);
    case BoundaryFillStyle: return ownReading(int(b.fillStyle));
    case BoundaryPattern: return ownReading(b.linePattern.value_or(Boundaries::kDefaultLinePattern));
    case BoundaryWidth: return ownReading(b.lineWidth.value_or(Boundaries::kDefaultLineWidth));
    case BoundaryTitleFont: return ownReading(b.titleFont);
    case BoundaryTitleSize: return ownReading(b.titleSize.value_or(Boundaries::kTitleFontPx));
    case BoundaryTitleColor: return colorReading(b.titleColor, Boundaries::look(Boundary{}, backdrop, QString()).titleText);
    case BoundaryTitleBold: return ownReading(b.titleBold);
    default: return colorReading(b.lineColor, line);
    }
}

bool MobileStyle::chooseObject(int property, const QVariant &value) {
    if (!m_map || !m_map->isOpen()) return false;
    if (property <= PictureOpacity) {
        return choosePictureStyle([property, value](ImageStyle &style) {
            switch (property) {
            case PictureBorder: style.border = static_cast<ImageStyle::Border>(value.toInt()); break;
            case PictureShape: style.shape = static_cast<NodeShape>(value.toInt()); break;
            case PictureEffect: style.effect = static_cast<ImageEffect>(value.toInt()); break;
            case PictureBorderWidth:
                style.borderWidth = std::clamp(value.toDouble(), NodeStyle::kMinBorderWidthPx, NodeStyle::kMaxBorderWidthPx);
                break;
            case PictureBorderColor: style.borderColor = value.value<QColor>(); break;
            case PictureShadow: style.shadow = value.toBool(); break;
            case PictureOutline: style.outline = value.toBool(); break;
            default: style.opacity = std::clamp(int(std::lround(value.toDouble())), ImageStyle::kMinOpacity, ImageStyle::kMaxOpacity); break;
            }
        });
    }
    if (property <= RelationTextColor) {
        // MainWindow's RelationshipStylePage::edited: Document::updateRelations, one step.
        const MapSnapshot::Relation *relation = m_map->selectedRelation();
        if (!relation) return false;
        Relationship changed = relation->relationship;
        switch (property) {
        case RelationShape: changed.shape = static_cast<Relationship::Shape>(value.toInt()); break;
        case RelationPattern: changed.linePattern = value.toInt(); break;
        case RelationWidth: changed.lineWidth = value.toDouble(); break;
        case RelationLineColor: changed.lineColor = value.value<QColor>(); break;
        case RelationStart: changed.startMark = static_cast<::BranchEnd>(value.toInt()); break;
        case RelationEnd: changed.endMark = static_cast<::BranchEnd>(value.toInt()); break;
        case RelationFont: changed.fontFamily = value.toString(); break;
        case RelationFontSize: changed.fontSize = value.toDouble(); break;
        default: changed.textColor = value.value<QColor>(); break;
        }
        if (changed == relation->relationship || !m_map->m_document->updateRelations({changed})) return false;
        m_map->setMessage(QString());
        m_map->rebuildSnapshot(false);
        emit m_map->selectionChanged();  // the selected line's look is the selection's
        return true;
    }
    // MainWindow's BoundaryStylePage::edited: the width changes the room, so the layout comes with it.
    const MapSnapshot::BoundaryShape *shape = m_map->selectedBoundary();
    if (!shape) return false;
    Boundary changed = shape->boundary;
    switch (property) {
    case BoundaryShape: changed.shape = static_cast<Boundary::Shape>(value.toInt()); break;
    case BoundaryFillColor: changed.fillColor = value.value<QColor>(); break;
    case BoundaryFillStyle: changed.fillStyle = static_cast<NodeFillStyle>(value.toInt()); break;
    case BoundaryPattern: changed.linePattern = value.toInt(); break;
    case BoundaryWidth: changed.lineWidth = value.toDouble(); break;
    case BoundaryTitleFont: changed.titleFont = value.toString(); break;
    case BoundaryTitleSize: changed.titleSize = value.toDouble(); break;
    case BoundaryTitleColor: changed.titleColor = value.value<QColor>(); break;
    case BoundaryTitleBold: changed.titleBold = value.toBool(); break;
    default: changed.lineColor = value.value<QColor>(); break;
    }
    if (changed == shape->boundary || !m_map->m_document->updateBoundaries({changed})) return false;
    const QString id = changed.id;
    m_map->finishEdit(QString());
    m_map->setSelectedBoundary(id);
    return true;
}

bool MobileStyle::choosePictureStyle(const std::function<void(ImageStyle &)> &change) {
    MindNode *node = pictureNode();
    if (!node) return false;
    ImageStyle style = node->imageStyle;
    change(style);
    if (style == node->imageStyle || !m_map->m_document->setImageStyle(node, style)) return false;
    m_map->finishEdit(node->id);  // the same topic: the picture stays selected
    return true;
}

QVariantMap MobileStyle::pictureSize() const {
    const MindNode *node = pictureNode();
    if (!node) return {{QStringLiteral("empty"), true}};
    return {{QStringLiteral("empty"), false}, {QStringLiteral("width"), std::round(node->imageSize.width())},
            {QStringLiteral("height"), std::round(node->imageSize.height())}};
}

bool MobileStyle::pictureIsSticker() const {
    const MindNode *node = pictureNode();
    return node && node->imageSticker && !node->imagePath.isEmpty();
}

bool MobileStyle::choosePictureWidth(qreal px, bool keepProportions) {
    // ImageStylePage::chooseWidth.
    const MindNode *node = pictureNode();
    if (!node || node->imageSize.isEmpty()) return false;
    const QSizeF size = node->imageSize;
    const qreal width = std::clamp(std::round(px), NodeGeometry::kMinImageWidthPx, NodeGeometry::kMaxImageWidthPx);
    const qreal height = keepProportions ? std::clamp<qreal>(std::round(width * size.height() / size.width()), 1, NodeGeometry::kMaxImageWidthPx)
                                         : size.height();
    return m_map->resizePicture({width, height});
}

bool MobileStyle::choosePictureHeight(qreal px, bool keepProportions) {
    // ImageStylePage::chooseHeight.
    const MindNode *node = pictureNode();
    if (!node || node->imageSize.isEmpty()) return false;
    const QSizeF size = node->imageSize;
    const qreal height = std::clamp<qreal>(std::round(px), 1, NodeGeometry::kMaxImageWidthPx);
    const qreal width = keepProportions ? std::clamp(std::round(height * size.width() / size.height()), NodeGeometry::kMinImageWidthPx,
                                                     NodeGeometry::kMaxImageWidthPx)
                                        : size.width();
    return m_map->resizePicture({width, height});
}

bool MobileStyle::resetPictureSize() {
    // MainWindow's ImageStylePage::sizeResetRequested: the size it is inserted at, turned with the
    // picture; an embedded image measured from its original, not the thumbnail (FEAT-037).
    const MindNode *node = pictureNode();
    if (!node) return false;
    const auto turned = [node](QSizeF size) { return node->imageStyle.rotation % 180 ? size.transposed() : size; };
    if (!node->formula.isEmpty()) return m_map->resizePicture(turned(FormulaText::boxSize(node->formula, NodeGeometry::titleFont(node))));
    QString path = node->imagePath;
    if (!node->imageAssetId.isEmpty()) {
        const QString original = BmapStorage::originalAssetPath(m_map->m_document->assetSourcePath(), node->imageAssetId, node->imageOriginalName);
        if (QFileInfo(original).isFile()) path = original;
    }
    return m_map->resizePicture(turned(NodeGeometry::insertionImageSize(path)));
}

bool MobileStyle::rotatePicture() {
    // MainWindow::rotateVisual: the turn and the layout, one step.
    MindNode *node = pictureNode();
    if (!node || !m_map->m_document->rotateImage(node)) return false;
    m_map->finishEdit(node->id);
    return true;
}

bool MobileStyle::resetPictureStyle() {
    // ImageStylePage's Reset style: border, shadow, opacity, shape; a sticker keeps its outline and the
    // rotation stays (it is not a style).
    const bool sticker = pictureIsSticker();
    return choosePictureStyle([sticker](ImageStyle &style) {
        const int rotation = style.rotation;
        style = ImageStyle();
        style.outline = sticker;
        style.rotation = rotation;
    });
}

StyleIcon::StyleIcon(QQuickItem *parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }

void StyleIcon::paint(QPainter *painter) {
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF box = boundingRect();
    if (m_kind < 0) {
        if (!m_topic.isEmpty()) {
            StyleChoices::drawTopicPreview(*painter, box, m_topic.value(QStringLiteral("title")).toString(),
                                           m_topic.value(QStringLiteral("fill")).value<QColor>(), m_topic.value(QStringLiteral("border")).value<QColor>(),
                                           m_topic.value(QStringLiteral("text")).value<QColor>(), m_topic.value(QStringLiteral("weight")).toInt());
            return;
        }
        if (m_preset >= 0)
            StyleChoices::drawPreset(*painter, box, m_preset, m_presetName, m_presetColor, m_presetDepth, NodeStyle::importantColors(m_themeColors));
        return;
    }
    const QRectF inset = box.adjusted(1.5, 1.5, -1.5, -1.5);
    if (m_kind >= MobileStyle::GanttIcons) {
        GanttStyleChoices::drawIcon(GanttStyleChoices::List(m_kind - MobileStyle::GanttIcons), m_value, *painter, inset, m_ink);
        return;
    }
    if (m_kind == MobileStyle::PictureBorderIcon) {
        // ImageStylePage's border select: the line's rectangle; "No border" as the topics' none.
        const auto border = static_cast<ImageStyle::Border>(m_value);
        if (border == ImageStyle::Border::None) {
            StyleChoices::drawIcon(StyleChoices::Icon::BorderStyle, StyleChoices::noneValue(StyleChoices::Icon::BorderStyle), *painter, inset, m_ink, m_muted);
            return;
        }
        const Qt::PenStyle pen = border == ImageStyle::Border::Dashed ? Qt::DashLine : border == ImageStyle::Border::Dotted ? Qt::DotLine : Qt::SolidLine;
        painter->setPen(QPen(m_ink, 1.5, pen));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(inset);
        return;
    }
    StyleChoices::drawIcon(static_cast<StyleChoices::Icon>(m_kind), m_value, *painter, inset, m_ink, m_muted);
}
