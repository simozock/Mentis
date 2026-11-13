#pragma once

// FEAT-096: the maps of the layout tests, built the same way with or without the scene
// (tests/layout_regression_test.cpp with the desktop UI, tests/domain_layout_test.cpp with MentisDomain
// alone). Only Document and the model: no item, no widget.

#include "document.h"
#include "mapstructure.h"
#include "mapvariants.h"
#include "mindmapmodel.h"
#include "nodegeometry.h"

#include <QList>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <functional>

namespace LayoutFixtures {

struct Fixture {
    QString name;
    QString type;       // MapStructure key of the sheet
    QString direction;  // MapStructure direction key
    bool mixed = false; // main branches with their own structures
    // What changes a topic's measure: rich and formula titles, images, formula content, labels, task,
    // markers, link buttons, hand width, shapes, borders, font. Notes: "Show all notes"; justify:
    // the sheet's Justify.
    enum class Content { None, All, Notes, Justify } content = Content::None;
};

inline QList<Fixture> all() {
    QList<Fixture> list;
    for (const MapStructure::Type type : {MapStructure::Type::MindMap, MapStructure::Type::LogicChart,
                                          MapStructure::Type::OrgChart, MapStructure::Type::TreeChart,
                                          MapStructure::Type::Brace, MapStructure::Type::Timeline,
                                          MapStructure::Type::Fishbone, MapStructure::Type::TreeTable})
        for (const MapStructure::Direction direction : MapStructure::directions(type)) {
            const QString typeKey = MapStructure::typeKey(type), directionKey = MapStructure::directionKey(direction);
            list.append({typeKey + QLatin1Char('-') + directionKey, typeKey, directionKey});
        }
    list.append({QStringLiteral("mixed"), MapStructure::typeKey(MapStructure::Type::MindMap),
                 MapStructure::directionKey(MapStructure::Direction::BothSides), true});
    using Content = Fixture::Content;
    const auto key = [](MapStructure::Type type) { return MapStructure::typeKey(type); };
    const auto direction = [](MapStructure::Direction d) { return MapStructure::directionKey(d); };
    list.append({QStringLiteral("content-mindmap"), key(MapStructure::Type::MindMap), direction(MapStructure::Direction::BothSides), false, Content::All});
    list.append({QStringLiteral("content-org"), key(MapStructure::Type::OrgChart), direction(MapStructure::Direction::Down), false, Content::All});
    list.append({QStringLiteral("content-treetable"), key(MapStructure::Type::TreeTable), direction(MapStructure::Direction::Down), false, Content::All});
    list.append({QStringLiteral("content-notes"), key(MapStructure::Type::MindMap), direction(MapStructure::Direction::BothSides), false, Content::Notes});
    list.append({QStringLiteral("content-justify"), key(MapStructure::Type::LogicChart), direction(MapStructure::Direction::Right), false, Content::Justify});
    return list;
}

// Unique key per topic: the titles from the central topic down (the fixture's titles are unique).
inline QString keyOf(const MindNode *node) {
    QStringList path;
    for (const MindNode *n = node; n; n = n->parent) path.prepend(n->title);
    return path.join(QLatin1Char('/'));
}

// The topics the annotations (summaries, boundaries) go on.
struct Topics { MindNode *a1 = nullptr, *a2 = nullptr, *b1 = nullptr, *b2 = nullptr; };

// The fixture's map in document's active sheet, not laid out. formulaSize gives a formula content's
// box (the desktop: NodeItem::formulaInsertionSize); without it the map has no formulas, which only a
// formula renderer measures. Sets "Show all notes" for the Notes fixture: reset it afterwards.
// started runs once the sheet is open and contentSet after the content (the scene's test creates the
// scene there and rebuilds it: the hidden topics keep the places the scene gave them while they were
// added, as recorded).
inline Topics build(Document &document, const Fixture &fixture, const std::function<QSizeF(const MindNode *)> &formulaSize,
                    const std::function<void()> &started = {}, const std::function<void()> &contentSet = {}) {
    MindMapModel &model = *document.model();
    model.clear(QStringLiteral("Central Topic"));
    MapVariants::apply(model, MapVariants::all().first().id, false);
    document.startUntitled();
    if (started) started();
    MindNode *root = model.root();
    document.setStructure(root, fixture.type, fixture.direction);

    // Short, long (wraps) and multi-line titles; deep, wide, collapsed and empty branches.
    MindNode *a = model.addChild(root, QStringLiteral("Alpha"));
    MindNode *a1 = model.addChild(a, QStringLiteral("Alpha one"));
    MindNode *a2 = model.addChild(a, QStringLiteral("Alpha two has a much longer title that wraps over more than one line in its box"));
    MindNode *a3 = model.addChild(a, QStringLiteral("Alpha three\nsecond line"));
    MindNode *b = model.addChild(root, QStringLiteral("Beta"));
    MindNode *b1 = model.addChild(b, QStringLiteral("Beta one"));
    MindNode *b1a = model.addChild(b1, QStringLiteral("Beta one first"));
    MindNode *b1b = model.addChild(b1, QStringLiteral("Beta one second"));
    MindNode *b2 = model.addChild(b, QStringLiteral("Beta two"));
    MindNode *c = model.addChild(root, QStringLiteral("Gamma"));
    model.addChild(c, QStringLiteral("Gamma hidden one"));
    model.addChild(c, QStringLiteral("Gamma hidden two"));
    MindNode *d = model.addChild(root, QStringLiteral("Delta"));
    MindNode *d1 = model.addChild(d, QStringLiteral("Delta one"));
    MindNode *d2 = model.addChild(d, QStringLiteral("Delta two"));
    MindNode *e = model.addChild(root, QStringLiteral("Epsilon"));
    if (fixture.content != Fixture::Content::None) {
        // Content on the topics directly; style as choices by hand, resolved by the cascade.
        const bool formulas = static_cast<bool>(formulaSize);
        root->style.textAlign = TextAlign::Centro;
        a->title = QStringLiteral("Alpha bold");
        a->titleHtml = QStringLiteral("<p>Alpha <b>bold</b> <i>part</i></p>");
        a1->title = formulas ? QStringLiteral("Area $\\pi r^2$ of a circle") : QStringLiteral("Area pi r2 of a circle");
        a2->customWidth = 160;
        a3->style.textAlign = TextAlign::Giustificato;
        b->style.shape = NodeShape::Ellisse;
        b->style.borderWidth = 4;
        b1->labels = {QStringLiteral("one"), QStringLiteral("a longer second label"), QStringLiteral("three")};
        b1->task = MindNode::Task::Open;
        b1->markers = {QStringLiteral("priority:1")};
        b1a->topicLink = QStringLiteral("missing-topic");
        b1a->webLink = QStringLiteral("https://example.com");
        b1a->files = {NodeFile{QStringLiteral("missing.pdf"), QString(), QStringLiteral("missing.pdf"), 10}};
        b1a->notes = formulas ? QStringLiteral("A note on the topic\nwith a second line and $x^2$")
                              : QStringLiteral("A note on the topic\nwith a second line and x2");
        b1b->imagePath = QStringLiteral("missing-image.png");  // only imageSize is measured
        b1b->imageSize = QSizeF(120, 80);
        b1b->imagePlacement = MindNode::ImagePlacement::Left;
        b2->notes = QStringLiteral("Short note");
        c->style.fontSize = 20;
        c->style.fontWeight = QFont::Bold;
        c->style.italic = true;
        c->style.textCase = TextCase::Maiuscolo;
        if (formulas) {
            d->formula = QStringLiteral("\\frac{a}{b}");
            d->imageSize = formulaSize(d);
        }
        d1->style.shape = NodeShape::Rombo;
        d2->title.clear();
        d2->imagePath = QStringLiteral("missing-image.png");
        d2->imageSize = QSizeF(90, 60);
        e->style.shape = NodeShape::LineaSotto;
        e->style.borderStyle = NodeBorderStyle::ContinuoManoLibera;
        NodeGeometry::showAllNotes() = fixture.content == Fixture::Content::Notes;
        model.sheetStyle.justify = fixture.content == Fixture::Content::Justify;
        model.refreshStyles();
        if (contentSet) contentSet();
    }
    if (fixture.mixed) {
        document.setStructure(a, MapStructure::typeKey(MapStructure::Type::LogicChart), QString());
        document.setStructure(b, MapStructure::typeKey(MapStructure::Type::OrgChart), QString());
        document.setStructure(c, MapStructure::typeKey(MapStructure::Type::Brace), QString());
        document.setStructure(d, MapStructure::typeKey(MapStructure::Type::TreeChart), QString());
    }
    document.setCollapsed(c, true);
    document.addFloatingTopic(QPointF(-400, -300), QStringLiteral("Floating"));
    return {a1, a2, b1, b2};
}

}  // namespace LayoutFixtures
