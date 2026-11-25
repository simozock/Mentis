// FEAT-119 stage B: MobileStyle's Map page, the desktop's Map page (MainWindow) and SheetStylePanel.

#include "mobilestyle.h"

#include "custompalettes.h"
#include "document.h"
#include "maplayout.h"
#include "mapsnapshot.h"
#include "mapstructure.h"
#include "mapvariants.h"
#include "mindmapmodel.h"
#include "mobilemap.h"
#include "stylechoices.h"

#include <QCoreApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {
QVariantMap item(const QVariant &value, const QString &name) { return {{QStringLiteral("value"), value}, {QStringLiteral("name"), name}}; }

QString panelText(const char *text) { return QCoreApplication::translate("SheetStylePanel", text); }

QVariantMap paletteMap(const NodeStyle::Palette &palette) {
    return {{QStringLiteral("id"), palette.id}, {QStringLiteral("name"), NodeStyle::displayName(palette)},
            {QStringLiteral("colors"), palette.colors}, {QStringLiteral("favorite"), StyleChoices::isFavoritePalette(palette.id)},
            {QStringLiteral("custom"), palette.group == NodeStyle::PaletteGroup::Personalizzata}};
}
}  // namespace

QVariantMap MobileStyle::sheet() const {
    if (!m_map || !m_map->isOpen()) return {};
    const MindMapModel *model = m_map->m_document->model();
    const NodeStyle::SheetStyle &style = model->sheetStyle;
    // MainWindow::refreshStylePage: with a one-sided direction there is nothing to balance.
    const bool twoSided = MapStructure::isTwoSided(MapStructure::directionOf(model->root()));
    // PaletteSelect::paletteName.
    const auto palette = CustomPalettes::lookup(model->paletteId);
    QString name;
    if (palette) name = NodeStyle::displayName(*palette);
    else if (!model->paletteName.isEmpty()) name = QCoreApplication::translate("PaletteSelect", "%1 (not installed)").arg(model->paletteName);
    else name = model->paletteId.isEmpty() ? QCoreApplication::translate("PaletteSelect", "Custom")
                                           : QCoreApplication::translate("PaletteSelect", "Template palette");
    return {{QStringLiteral("fontFamily"), style.fontFamily},
            {QStringLiteral("width"), style.tapered ? -style.branchWidth : style.branchWidth},
            {QStringLiteral("coloredBranches"), style.coloredBranches},
            {QStringLiteral("showNotes"), style.showNotes},
            {QStringLiteral("compact"), style.compact},
            {QStringLiteral("freeBranches"), style.freeBranches},
            {QStringLiteral("justify"), style.justify},
            {QStringLiteral("balanced"), twoSided && model->balanced},
            {QStringLiteral("balanceEnabled"), twoSided && !style.freeBranches},
            {QStringLiteral("canReset"), model->hasMapStyle()},
            {QStringLiteral("paletteId"), model->paletteId},
            {QStringLiteral("paletteName"), name},
            {QStringLiteral("paletteColors"), model->activePalette},
            {QStringLiteral("notInstalled"), !model->paletteName.isEmpty() && !palette},
            {QStringLiteral("paletteSavedName"), model->paletteName},
            {QStringLiteral("variant"), model->paletteVariant},
            {QStringLiteral("variantCount"), std::min(int(model->activePalette.size()), NodeStyle::kVariantCount)}};
}

QVariantList MobileStyle::sheetFonts() const {
    // SheetStylePanel: "Default" (value empty), the installed fonts, a font of the file not installed.
    QVariantList list{item(QString(), panelText("Default"))};
    const QStringList families = QFontDatabase::families();
    for (const QString &family : families)
        if (!family.startsWith(QLatin1Char('@'))) list.append(item(family, family));
    const QString shown = m_map && m_map->isOpen() ? m_map->m_document->model()->sheetStyle.fontFamily : QString();
    if (!shown.isEmpty() && !families.contains(shown)) list.append(item(shown, panelText("%1 (not installed)").arg(shown)));
    return list;
}

QVariantList MobileStyle::sheetWidths() const {
    // SheetStylePanel: "Default", the named widths, then their tapered versions (negative values); a
    // width in px of an older file at the end.
    QVariantList list{item(0.0, panelText("Default"))};
    for (int i = 0; i < NodeStyle::kNamedBranchWidths.size(); ++i) list.append(item(NodeStyle::kNamedBranchWidths[i], StyleChoices::branchWidthName(i)));
    for (int i = 0; i < NodeStyle::kNamedBranchWidths.size(); ++i)
        list.append(item(-NodeStyle::kNamedBranchWidths[i], panelText("%1 - Tapered").arg(StyleChoices::branchWidthName(i))));
    if (m_map && m_map->isOpen()) {
        const NodeStyle::SheetStyle &style = m_map->m_document->model()->sheetStyle;
        if (style.branchWidth > 0 && !NodeStyle::kNamedBranchWidths.contains(style.branchWidth))
            list.append(item(style.tapered ? -style.branchWidth : style.branchWidth,
                             style.tapered ? panelText("%1 px - Tapered").arg(style.branchWidth) : panelText("%1 px").arg(style.branchWidth)));
    }
    return list;
}

bool MobileStyle::editSheetStyle(const std::function<void(NodeStyle::SheetStyle &)> &change) {
    if (!m_map || !m_map->isOpen()) return false;
    MindMapModel *model = m_map->m_document->model();
    NodeStyle::SheetStyle style = model->sheetStyle;
    change(style);
    if (style == model->sheetStyle) return false;
    // MainWindow's sheetStyleChosen: turning Free branch positions on, every main branch keeps the
    // side it has; off, the moves by hand go and, balanced, the sides are automatic again. Same step.
    if (style.freeBranches != model->sheetStyle.freeBranches) {
        const MapLayout *layout = m_map->snapshot() ? m_map->snapshot()->layout() : nullptr;
        for (const auto &child : model->root()->children) {
            if (child->floating) continue;
            if (style.freeBranches) {
                if (child->side == MindNode::Side::Auto && layout)
                    child->side = layout->sideOf(child.get()) < 0 ? MindNode::Side::Left : MindNode::Side::Right;
            } else if (model->balanced) {
                child->side = MindNode::Side::Auto;
            }
        }
        if (!style.freeBranches) {
            const std::function<void(MindNode *)> clear = [&clear](MindNode *node) {
                node->manualOffset = {};
                for (const auto &child : node->children) clear(child.get());
            };
            clear(model->root());
        }
    }
    return edited(model->setSheetStyle(style));  // the font and the layout options lay the map out again
}

bool MobileStyle::chooseSheet(const QString &key, const QVariant &value) {
    if (key == QLatin1String("fontFamily"))
        return editSheetStyle([family = value.toString()](NodeStyle::SheetStyle &style) { style.fontFamily = family; });
    if (key == QLatin1String("width"))
        return editSheetStyle([width = value.toDouble()](NodeStyle::SheetStyle &style) {
            style.branchWidth = std::abs(width);
            style.tapered = width < 0;
        });
    const bool on = value.toBool();
    if (key == QLatin1String("coloredBranches")) return editSheetStyle([on](NodeStyle::SheetStyle &style) { style.coloredBranches = on; });
    if (key == QLatin1String("showNotes")) return editSheetStyle([on](NodeStyle::SheetStyle &style) { style.showNotes = on; });
    if (key == QLatin1String("compact")) return editSheetStyle([on](NodeStyle::SheetStyle &style) { style.compact = on; });
    if (key == QLatin1String("freeBranches")) return editSheetStyle([on](NodeStyle::SheetStyle &style) { style.freeBranches = on; });
    if (key == QLatin1String("justify")) return editSheetStyle([on](NodeStyle::SheetStyle &style) { style.justify = on; });
    return false;
}

bool MobileStyle::chooseBalanced(bool on) {
    // MainWindow::setBalanced: the sides the layout gives, then the layout, one step.
    if (!m_map || !m_map->isOpen() || !m_map->snapshot()) return false;
    MindMapModel *model = m_map->m_document->model();
    QHash<const MindNode *, int> sides;
    for (const auto &child : model->root()->children) sides.insert(child.get(), m_map->snapshot()->layout()->sideOf(child.get()));
    if (!m_map->m_document->setBalanced(on, sides)) return false;
    m_map->finishEdit(m_map->m_selectedId);
    return true;
}

bool MobileStyle::chooseVariant(int variant) {
    if (!m_map || !m_map->isOpen()) return false;
    return edited(m_map->m_document->model()->setBackdrop(variant));
}

bool MobileStyle::resetMapStyle() {
    // SheetStylePanel's Reset map style: the sheet's topics, relationships and boundaries, its
    // background and line settings; the layout with it.
    if (!m_map || !m_map->isOpen() || !m_map->m_document->resetMapStyle()) return false;
    m_map->finishEdit(m_map->m_selectedId);
    return true;
}

QVariantMap MobileStyle::sheetReading(int property) const {
    if (!m_map || !m_map->isOpen())
        return {{QStringLiteral("empty"), true}, {QStringLiteral("mixed"), false}, {QStringLiteral("manual"), false}, {QStringLiteral("value"), QVariant()}};
    const MindMapModel *model = m_map->m_document->model();
    QColor value;
    bool manual = false;
    if (property == SheetLineColor) {
        // SheetStylePanel::setSheetStyle: the central topic's text when the sheet has no colour of its own.
        value = model->sheetStyle.lineColor.isValid() ? model->sheetStyle.lineColor : model->root()->text;
        manual = model->sheetStyle.lineColor.isValid();
    } else {
        value = model->backdrop().color;
        manual = model->background.has_value() || model->paletteVariant >= 0;
    }
    return {{QStringLiteral("empty"), false}, {QStringLiteral("mixed"), false}, {QStringLiteral("manual"), manual}, {QStringLiteral("value"), value}};
}

bool MobileStyle::chooseSheetColor(int property, const QColor &color) {
    if (!m_map || !m_map->isOpen() || !color.isValid()) return false;
    if (property == SheetLineColor) return editSheetStyle([color](NodeStyle::SheetStyle &style) { style.lineColor = color; });
    return edited(m_map->m_document->model()->setBackground(color));
}

QVariantList MobileStyle::palettes(int group) const {
    // PalettePicker's pages.
    QVariantList list;
    if (group == 0) {
        for (const QString &id : StyleChoices::favoritePaletteIds())
            if (const auto palette = CustomPalettes::lookup(id)) list.append(paletteMap(*palette));
    } else if (group == 3) {
        for (const NodeStyle::Palette &palette : CustomPalettes::all()) list.append(paletteMap(palette));
    } else {
        const NodeStyle::PaletteGroup wanted = group == 1 ? NodeStyle::PaletteGroup::Colorato : NodeStyle::PaletteGroup::Classico;
        for (const NodeStyle::Palette &palette : NodeStyle::basePalettes())
            if ((palette.group == NodeStyle::PaletteGroup::Colorato) == (wanted == NodeStyle::PaletteGroup::Colorato)) list.append(paletteMap(palette));
    }
    return list;
}

int MobileStyle::paletteGroup(const QString &id) const {
    // PalettePicker opens on the current palette's group, Colourful otherwise.
    const auto current = CustomPalettes::lookup(id);
    if (current && current->group == NodeStyle::PaletteGroup::Classico) return 2;
    if (current && current->group == NodeStyle::PaletteGroup::Personalizzata) return 3;
    return 1;
}

void MobileStyle::setFavoritePalette(const QString &id, bool favorite) {
    StyleChoices::setFavoritePalette(id, favorite);
    bump();
}

bool MobileStyle::choosePalette(const QString &id) {
    // MainWindow's paletteChosen: a base or own palette, one step.
    if (!m_map || !m_map->isOpen()) return false;
    const auto palette = CustomPalettes::lookup(id);
    if (!palette) return false;
    MindMapModel *model = m_map->m_document->model();
    if (model->paletteId == id && model->activePalette == palette->colors) return false;
    const bool custom = palette->group == NodeStyle::PaletteGroup::Personalizzata;
    model->setPalette(id, palette->colors, custom ? palette->name : QString());
    return edited(true);
}

bool MobileStyle::addCurrentPalette() {
    // PaletteSelect's addCurrent: the sheet's own palette joins this phone's, under a free name.
    if (!m_map || !m_map->isOpen()) return false;
    const MindMapModel *model = m_map->m_document->model();
    if (model->paletteName.isEmpty() || CustomPalettes::lookup(model->paletteId)) return false;
    QString name = model->paletteName;
    for (int number = 2; CustomPalettes::isNameTaken(name); ++number) name = QStringLiteral("%1 %2").arg(model->paletteName).arg(number);
    const bool added = !CustomPalettes::add(name, model->activePalette, model->paletteId).isEmpty();
    bump();
    return added;
}

bool MobileStyle::removePalette(const QString &id) {
    // The sheets that use it keep their colours; asked first by the page (not an Undo step).
    const bool removed = CustomPalettes::remove(id);
    bump();
    return removed;
}

QString MobileStyle::nextPaletteName() const { return CustomPalettes::nextDefaultName(); }

bool MobileStyle::isPaletteNameTaken(const QString &name) const { return CustomPalettes::isNameTaken(name); }

bool MobileStyle::createPalette(const QString &name, const QStringList &colors) {
    // MainWindow::createPalette: the theme joins the own ones, then the sheet takes it, one step.
    if (!m_map || !m_map->isOpen() || colors.size() != CustomPalettes::kColorCount || colors.contains(QString())) return false;
    const QString shown = name.trimmed().isEmpty() ? CustomPalettes::nextDefaultName() : name.trimmed();
    if (CustomPalettes::isNameTaken(shown)) return false;
    const QString id = CustomPalettes::add(shown, colors);
    if (id.isEmpty()) return false;
    m_map->m_document->model()->setPalette(id, colors, shown);
    return edited(true);
}

void MobileStyle::rememberColor(const QColor &color) {
    if (color.isValid()) StyleChoices::rememberRecentColor(color);
}

QString MobileStyle::mapVariant() const {
    // MainWindow::refreshStylePage: the card names the sheet's type.
    if (!m_map || !m_map->isOpen()) return {};
    const MindMapModel *model = m_map->m_document->model();
    QString shown = model->sheetStyle.variant;
    const MapStructure::Type type = MapStructure::typeFromKey(model->root()->structure);
    const MapVariants::Variant *variant = MapVariants::find(shown);
    if (!variant || variant->type != type) {
        const QList<const MapVariants::Variant *> ofType = MapVariants::ofType(type);
        shown = ofType.isEmpty() ? QString() : ofType.first()->id;
    }
    return MapVariants::find(shown) ? shown : MapVariants::all().first().id;  // MapTypeSelect::setVariant
}

QString MobileStyle::mapTypeName(const QString &variant) const {
    const MapVariants::Variant *found = MapVariants::find(variant);
    return found ? StyleChoices::structureName(found->type) : QString();
}

QVariantList MobileStyle::mapTypes() const {
    QVariantList list;
    for (MapStructure::Type type : MapVariants::galleryTypes()) {
        QStringList ids;
        for (const MapVariants::Variant *variant : MapVariants::ofType(type)) ids.append(variant->id);
        list.append(QVariantMap{{QStringLiteral("type"), MapStructure::typeKey(type)}, {QStringLiteral("name"), StyleChoices::structureName(type)},
                                {QStringLiteral("variants"), ids}});
    }
    return list;
}

bool MobileStyle::chooseMapVariant(const QString &variantId) {
    // MainWindow::chooseSheetVariant: the sheet's type (the moves by hand go, as changing the type)
    // and the variant's style, laid out, one step.
    if (!m_map || !m_map->isOpen()) return false;
    MindMapModel *model = m_map->m_document->model();
    const MapVariants::Variant *variant = MapVariants::find(variantId);
    if (!variant || variantId == model->sheetStyle.variant) return false;
    m_map->m_document->setStructure(model->root(), MapStructure::typeKey(variant->type),
                                    MapStructure::directionKey(MapStructure::directions(variant->type).first()));
    MapVariants::apply(*model, variant->id, false);
    return edited(true);
}

VariantThumbnail::VariantThumbnail(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &VariantThumbnail::appearanceChanged, this, [this] { update(); });
}

void VariantThumbnail::paint(QPainter *painter) {
    if (m_colors.isEmpty() || m_variant >= m_colors.size()) return;
    StyleChoices::drawVariantThumbnail(*painter, boundingRect(), m_paletteId, m_colors, m_variant);
}

PaletteStrip::PaletteStrip(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &PaletteStrip::appearanceChanged, this, [this] { update(); });
}

void PaletteStrip::paint(QPainter *painter) {
    // The desktop's paintStrip.
    if (m_colors.isEmpty()) return;
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF rect = boundingRect();
    QPainterPath clip;
    clip.addRoundedRect(rect, 4, 4);
    painter->save();
    painter->setClipPath(clip);
    const qreal width = rect.width() / m_colors.size();
    for (int i = 0; i < m_colors.size(); ++i)
        painter->fillRect(QRectF(rect.left() + i * width, rect.top(), width + 0.5, rect.height()), QColor(m_colors[i]));
    painter->restore();
    painter->setPen(QPen(m_border, 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
}

PalettePreview::PalettePreview(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &PalettePreview::appearanceChanged, this, [this] { update(); });
}

void PalettePreview::paint(QPainter *painter) { StyleChoices::drawPalettePreview(*painter, boundingRect(), m_colors, 12); }
