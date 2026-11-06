#include "mapvariants.h"

#include "mindmapmodel.h"

namespace MapVariants {

namespace {

using Type = MapStructure::Type;
using Shape = NodeShape;

Role role(Look look, std::optional<Shape> shape = std::nullopt) { return {look, shape, {}, {}, {}}; }
Role underline(qreal width = 2) { return {Look::Underline, {}, {}, {}, width}; }

Variant variant(const char *id, Type type, Role root, Role main, Role sub, Line line = Line::Curve) {
    return {QString::fromLatin1(id), type, root, main, sub, line};
}

QList<Variant> build() {
    QList<Variant> list;
    const Role standard;
    // Mappa mentale (foto di riferimento 01 e 07..11). Nell'ordine della galleria.
    list << variant("mindmap-1", Type::MindMap, standard, standard, standard);
    {
        Variant v = variant("mindmap-2", Type::MindMap, role(Look::Outline), underline(), underline(1));  // foto di riferimento 07
        v.tapered = true;
        v.fontFamily = QStringLiteral("Roboto");  // FEAT-072: the reference fonts, measured live 2026-10-23
        v.branchWidth = 3;
        list << v;
    }
    {
        Variant v = variant("mindmap-3", Type::MindMap, role(Look::Dark, Shape::Pillola), role(Look::Filled, Shape::Pillola), underline(1));  // foto di riferimento 08
        v.fontFamily = QStringLiteral("Montserrat");
        list << v;
    }
    {
        // FEAT-072: lines and underlines are about 4 px at every level (live 2026-10-23).
        Variant v = variant("mindmap-4", Type::MindMap, role(Look::Plain), underline(4), underline(4), Line::Angled);  // foto di riferimento 09
        v.branchWidth = 4;
        v.fontFamily = QStringLiteral("Arvo");
        list << v;
    }
    {
        Variant v = variant("mindmap-5", Type::MindMap, role(Look::Dark), underline(4), underline(1), Line::Square);  // foto di riferimento 10
        v.branchWidth = 1;
        list << v;
    }
    {
        // foto di riferimento 11: a mano libera, pennellate nei principali. Sesta nella galleria
        // (FEAT-072, live 2026-10-23: le sei varianti gratuite sono le prime sei), carattere predefinito.
        Variant v = variant("mindmap-7", Type::MindMap, role(Look::Outline), role(Look::Filled), underline(1));
        v.root.borderStyle = NodeBorderStyle::ContinuoManoLibera;
        v.root.borderWidth = 3;
        v.main.fillStyle = NodeFillStyle::PennellateDiagonali;
        v.main.borderStyle = NodeBorderStyle::ContinuoManoLibera;
        v.freehandLines = true;
        v.branchWidth = 3;
        list << v;
    }
    list << variant("mindmap-6", Type::MindMap, role(Look::Outline, Shape::Rettangolo), role(Look::Outline, Shape::Rettangolo),
                    role(Look::Plain), Line::Square);
    list << variant("mindmap-8", Type::MindMap, role(Look::Outline, Shape::Cerchio), role(Look::Outline, Shape::Cerchio), role(Look::Plain));
    list << variant("mindmap-9", Type::MindMap, role(Look::Outline, Shape::Esagono), role(Look::Outline, Shape::Esagono),
                    role(Look::Plain), Line::Straight);
    list << variant("mindmap-10", Type::MindMap, role(Look::Dark, Shape::Rettangolo), role(Look::Filled, Shape::Rettangolo),
                    role(Look::Light, Shape::Rettangolo), Line::Square);
    list << variant("mindmap-11", Type::MindMap, role(Look::Filled, Shape::Ellisse), role(Look::Light, Shape::Ellisse), role(Look::Plain));
    list << variant("mindmap-12", Type::MindMap, role(Look::Light), role(Look::Light), underline(1));
    // Grafico logico (foto di riferimento 02).
    list << variant("logic-1", Type::LogicChart, standard, standard, standard);
    list << variant("logic-2", Type::LogicChart, role(Look::Outline, Shape::Rettangolo), role(Look::Plain), role(Look::Plain), Line::Square);
    list << variant("logic-3", Type::LogicChart, role(Look::Plain), underline(), underline(1));
    list << variant("logic-4", Type::LogicChart, role(Look::Dark, Shape::Rettangolo), role(Look::Outline, Shape::Rettangolo),
                    role(Look::Outline, Shape::Rettangolo), Line::Square);
    {
        Variant v = variant("logic-5", Type::LogicChart, role(Look::Plain), role(Look::Plain), underline(1), Line::Straight);
        v.branchWidth = 1;
        list << v;
    }
    list << variant("logic-6", Type::LogicChart, role(Look::Filled, Shape::Ellisse), role(Look::Outline, Shape::Pillola), role(Look::Plain));
    list << variant("logic-7", Type::LogicChart, role(Look::Plain), role(Look::Light, Shape::Pillola), role(Look::Plain));
    list << variant("logic-8", Type::LogicChart, role(Look::Plain), role(Look::Light, Shape::Ellisse), role(Look::Plain), Line::Straight);
    list << variant("logic-9", Type::LogicChart, role(Look::Filled, Shape::Rettangolo), role(Look::Filled, Shape::Rettangolo),
                    role(Look::Light, Shape::Rettangolo), Line::Square);
    // Brace map (FEAT-026 phase E, measured live 2026-10-24): the three free variants, in gallery order.
    {
        Variant v = variant("brace-1", Type::Brace, role(Look::Outline, Shape::RettangoloArrotondato), role(Look::Filled),
                            role(Look::Outline));
        v.root.borderWidth = 3;
        v.rootBranchWidth = 3;
        v.rootFontSizePt = 18;  // 24 px Medium, main topics Regular (reference panel)
        v.rootFontWeight = 500;
        v.mainFontWeight = 400;
        v.fontFamily = QStringLiteral("Montserrat");
        list << v;
    }
    {
        Variant v = variant("brace-2", Type::Brace, Role{Look::Underline, Shape::DoppiaLineaSotto, {}, {}, {}},
                            Role{Look::Underline, Shape::DoppiaLineaSotto, {}, {}, {}}, underline());
        v.rootBranchWidth = 3;
        v.rootFontSizePt = 18;  // 24 px Medium, main topics Regular (reference panel)
        v.rootFontWeight = 500;
        v.mainFontWeight = 400;
        list << v;
    }
    {
        Variant v = variant("brace-3", Type::Brace, role(Look::Plain), role(Look::Outline, Shape::EsagonoVerticale), role(Look::Plain));
        v.main.borderStyle = NodeBorderStyle::Tratteggiato;
        v.rootBranchWidth = 3;
        v.rootFontSizePt = 18;  // 24 px Medium, main topics Regular (reference panel)
        v.rootFontWeight = 500;
        v.mainFontWeight = 400;
        list << v;
    }
    // Timeline (FEAT-026 phase E, measured live 2026-10-24): the three free variants, in gallery order. Fonts
    // from the reference panel: central topic 28 px Bold, main topics 18 Medium, subtopics 14 Medium (variant 2:
    // 30 Bold, circles with 16 Demibold; variant 3: 28 Demibold). Raleway (variant 2) is not bundled:
    // Montserrat stands in for it.
    {
        Variant v = variant("timeline-1", Type::Timeline, role(Look::Dark), role(Look::Outline), role(Look::Plain), Line::Square);
        v.rootBranchWidth = 3;
        v.rootFontSizePt = 21;
        v.rootFontWeight = 700;
        v.subFontWeight = 500;
        v.fontFamily = QStringLiteral("Roboto");
        list << v;
    }
    {
        Variant v = variant("timeline-2", Type::Timeline, role(Look::Plain), role(Look::Filled, Shape::Cerchio), role(Look::Plain), Line::Square);
        v.rootBranchWidth = 3;
        v.rootFontSizePt = 22.5;
        v.rootFontWeight = 700;
        v.mainFontSizePt = 12;
        v.mainFontWeight = 600;
        v.subFontWeight = 500;
        v.fontFamily = QStringLiteral("Montserrat");
        list << v;
    }
    {
        Variant v = variant("timeline-3", Type::Timeline, role(Look::Dark, Shape::Cerchio), underline(3), role(Look::Plain), Line::Angled);
        v.rootBranchWidth = 3;
        v.rootFontSizePt = 21;
        v.rootFontWeight = 600;
        v.subFontWeight = 500;
        v.direction = MapStructure::Direction::OffAxis;
        list << v;
    }
    // Fishbone (FEAT-026 phase E, measured live 2026-10-24): the three free variants, in gallery order. The reference
    // panel: 1 Roboto Slab (not bundled: Roboto stands in), central topic 28 px Bold in square brackets,
    // outlined main topics 18 Regular, plain subtopics 14 Regular, lines 3 px
    // from the central topic; 2 dark central topic 28 Demibold, main topics 18 Demibold double-underlined,
    // thin lines; 3 the default font, banner shapes (here the vertical hexagon)
    // drawn by hand, central topic 28 Medium, hatched subtopics. Lines to deeper levels are straight,
    // too.
    {
        Variant v = variant("fishbone-1", Type::Fishbone, role(Look::Outline, Shape::ParentesiQuadre), role(Look::Outline, Shape::Rettangolo),
                            role(Look::Plain), Line::Straight);
        v.root.borderWidth = 3;
        v.rootBranchWidth = 3;
        v.rootFontSizePt = 21;
        v.rootFontWeight = 700;
        v.mainFontWeight = 400;
        v.fontFamily = QStringLiteral("Roboto");
        list << v;
    }
    {
        Variant v = variant("fishbone-2", Type::Fishbone, role(Look::Dark), Role{Look::Underline, Shape::DoppiaLineaSotto, {}, {}, {}},
                            role(Look::Plain), Line::Straight);
        v.rootBranchWidth = 2;
        v.rootFontSizePt = 21;
        v.rootFontWeight = 600;
        v.mainFontWeight = 600;
        list << v;
    }
    {
        Variant v = variant("fishbone-3", Type::Fishbone, role(Look::Outline, Shape::EsagonoVerticale),
                            role(Look::Outline, Shape::EsagonoVerticale), role(Look::Light), Line::Straight);
        v.root.borderStyle = NodeBorderStyle::ContinuoManoLibera;
        v.root.borderWidth = 3;
        v.main.borderStyle = NodeBorderStyle::ContinuoManoLibera;
        v.sub.fillStyle = NodeFillStyle::PennellateDiagonali;
        v.freehandLines = true;
        v.rootBranchWidth = 2;
        v.rootFontSizePt = 21;
        v.rootFontWeight = 500;
        v.mainFontWeight = 400;
        list << v;
    }
    // Organigramma (foto di riferimento 03): linee ad angolo, cambia l'aspetto dei nodi.
    list << variant("org-1", Type::OrgChart, standard, standard, standard);
    list << variant("org-2", Type::OrgChart, role(Look::Outline, Shape::Rettangolo), role(Look::Outline, Shape::Rettangolo),
                    role(Look::Outline, Shape::Rettangolo));
    list << variant("org-3", Type::OrgChart, role(Look::Light), role(Look::Plain), role(Look::Plain));
    list << variant("org-4", Type::OrgChart, underline(4), underline(), role(Look::Plain));
    list << variant("org-5", Type::OrgChart, role(Look::Filled, Shape::Rettangolo), role(Look::Light, Shape::Rettangolo),
                    role(Look::Outline, Shape::Rettangolo));
    list << variant("org-6", Type::OrgChart, role(Look::Dark), role(Look::Outline), role(Look::Outline));
    // Grafico ad albero (foto di riferimento 04).
    list << variant("tree-1", Type::TreeChart, standard, standard, standard);
    list << variant("tree-2", Type::TreeChart, role(Look::Outline, Shape::Rettangolo), role(Look::Light), role(Look::Plain));
    list << variant("tree-3", Type::TreeChart, role(Look::Plain), role(Look::Plain), role(Look::Plain));
    list << variant("tree-4", Type::TreeChart, role(Look::Light, Shape::Ellisse), role(Look::Light, Shape::Ellisse), role(Look::Plain));
    list << variant("tree-5", Type::TreeChart, role(Look::Filled, Shape::Esagono), role(Look::Plain), role(Look::Plain));
    list << variant("tree-6", Type::TreeChart, role(Look::Dark, Shape::Rettangolo), role(Look::Outline, Shape::Rettangolo), role(Look::Plain));
    // Tree table (FEAT-028, the first template, measured live 2026-10-25): dark header, main topics in the
    // palette colour; the cells below take a tint of it (MindMapScene::applyTableCells).
    list << variant("treetable-1", Type::TreeTable, role(Look::Dark, Shape::Rettangolo), role(Look::Filled, Shape::Rettangolo),
                    role(Look::Plain, Shape::Rettangolo));
    return list;
}

}  // namespace

const QList<Variant> &all() {
    static const QList<Variant> variants = build();
    return variants;
}

const Variant *find(const QString &id) {
    if (id.isEmpty()) return nullptr;
    for (const Variant &variant : all()) if (variant.id == id) return &variant;
    return nullptr;
}

QList<const Variant *> ofType(MapStructure::Type type) {
    QList<const Variant *> result;
    for (const Variant &variant : all()) if (variant.type == type) result.append(&variant);
    return result;
}

QList<MapStructure::Type> galleryTypes() {
    return {Type::MindMap, Type::LogicChart, Type::Brace, Type::OrgChart, Type::TreeChart, Type::Timeline, Type::Fishbone, Type::TreeTable};
}

void apply(MindMapModel &model, const QString &id, bool withTopics) {
    const Variant *variant = find(id);
    if (!variant) return;
    MindNode *root = model.root();
    root->structure = MapStructure::typeKey(variant->type);
    root->structureDirection = MapStructure::directionKey(variant->direction.value_or(MapStructure::directions(variant->type).first()));
    if (withTopics && root->children.empty())
        for (int i = 1; i <= 4; ++i) model.addChild(root, MindMapModel::defaultTitle(root, i));
    NodeStyle::SheetStyle style = model.sheetStyle;
    style.variant = variant->id;
    style.tapered = variant->tapered;
    style.branchWidth = variant->branchWidth;
    style.fontFamily = variant->fontFamily;
    model.setSheetStyle(style);
}

}  // namespace MapVariants
