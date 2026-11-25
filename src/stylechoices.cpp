#include "stylechoices.h"

#include "boundary.h"
#include "branchpath.h"
#include "custompalettes.h"
#include "mindmapmodel.h"
#include "nodeshape.h"
#include "nodesketch.h"
#include "nodestyle.h"

#include <QCoreApplication>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <memory>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QSettings>

#include <algorithm>
#include <functional>

namespace StyleChoices {

namespace {
// The texts are the desktop's StylePage ones: one context, so both apps use its translations.
QString translated(const char *text) { return QCoreApplication::translate("StylePage", text); }
constexpr quint64 kIconSeed = 7;  // fixed stroke for the icons

// "None": a faint rounded box, crossed.
void drawNone(QPainter &painter, const QRectF &box, const QColor &muted) {
    painter.setPen(QPen(muted, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(box, 3, 3);
    painter.drawLine(box.bottomLeft(), box.topRight());
}

// FEAT-020 part C: patterns and freehand strokes drawn with the map's code on a larger box, scaled to
// the icon's height: three or four rows of the pattern show in the small icon and in the grid menu's
// (photo 23).
void drawSketch(QPainter &painter, const QRectF &box, const std::function<void(QPainter &, const QPainterPath &, qreal)> &draw) {
    const qreal scale = box.height() / 28;
    painter.save();
    painter.translate(box.topLeft());
    painter.scale(scale, scale);
    QPainterPath outline;
    // 3 units in: the patterns' ends stay inside the icon.
    outline.addRoundedRect(QRectF(QPointF(0, 0), box.size() / scale).adjusted(3, 3, -3, -3), 5, 5);
    draw(painter, outline, scale);
    painter.restore();
}
}  // namespace

QList<Choice> fillStyles() {
    // Photo 23's order: diagonal strokes and hatching, then horizontal.
    return {{int(NodeFillStyle::Pieno), QCoreApplication::translate("StylePage", "Filled")},
            {int(NodeFillStyle::PienoManoLibera), QCoreApplication::translate("StylePage", "Hand-drawn fill")},
            {int(NodeFillStyle::PennellateDiagonali), QCoreApplication::translate("StylePage", "Diagonal strokes")},
            {int(NodeFillStyle::TratteggioDiagonale), QCoreApplication::translate("StylePage", "Diagonal hatching")},
            {int(NodeFillStyle::PennellateOrizzontali), QCoreApplication::translate("StylePage", "Horizontal strokes")},
            {int(NodeFillStyle::TratteggioOrizzontale), QCoreApplication::translate("StylePage", "Horizontal hatching")},
            {int(NodeFillStyle::Nessuno), QCoreApplication::translate("StylePage", "No fill")}};
}

QList<Choice> borderStyles() {
    // Photo 04's order: solid, freehand, dashed, freehand dashed.
    return {{int(NodeBorderStyle::Continuo), QCoreApplication::translate("StylePage", "Solid")},
            {int(NodeBorderStyle::ContinuoManoLibera), QCoreApplication::translate("StylePage", "Hand-drawn solid")},
            {int(NodeBorderStyle::Tratteggiato), QCoreApplication::translate("StylePage", "Dashed")},
            {int(NodeBorderStyle::TratteggiatoManoLibera), QCoreApplication::translate("StylePage", "Hand-drawn dashed")},
            {int(NodeBorderStyle::Nessuno), QCoreApplication::translate("StylePage", "No border")}};
}

QList<Group> shapeGroups() {
    // In the menu's order, which is not the order of the values (FEAT-030: the square brackets, added
    // with the fishbone, belong to the Puntuale group).
    using S = NodeShape;
    const auto c = [](NodeShape shape, const char *name) { return Choice{int(shape), translated(name)}; };
    return {{QCoreApplication::translate("StylePage", "Basic"),
             {c(S::RettangoloArrotondato, QT_TRANSLATE_NOOP("StylePage", "Rounded rectangle")), c(S::Rettangolo, QT_TRANSLATE_NOOP("StylePage", "Rectangle")), c(S::LineaSotto, QT_TRANSLATE_NOOP("StylePage", "Line below")),
              c(S::DoppiaLineaSotto, QT_TRANSLATE_NOOP("StylePage", "Double line below")), c(S::Cerchio, QT_TRANSLATE_NOOP("StylePage", "Circle")), c(S::Ellisse, QT_TRANSLATE_NOOP("StylePage", "Ellipse")), c(S::Rombo, QT_TRANSLATE_NOOP("StylePage", "Diamond")),
              c(S::Parallelogramma, QT_TRANSLATE_NOOP("StylePage", "Parallelogram")), c(S::Pillola, QT_TRANSLATE_NOOP("StylePage", "Pill")), c(S::Esagono, QT_TRANSLATE_NOOP("StylePage", "Hexagon")),
              c(S::EsagonoVerticale, QT_TRANSLATE_NOOP("StylePage", "Vertical hexagon")), c(S::Ottagono, QT_TRANSLATE_NOOP("StylePage", "Octagon"))}},
            {QCoreApplication::translate("StylePage", "Advanced"),
             {c(S::Cuore, QT_TRANSLATE_NOOP("StylePage", "Heart")), c(S::Nuvola, QT_TRANSLATE_NOOP("StylePage", "Cloud")), c(S::Stella, QT_TRANSLATE_NOOP("StylePage", "Star")), c(S::Goccia, QT_TRANSLATE_NOOP("StylePage", "Drop")), c(S::Gemma, QT_TRANSLATE_NOOP("StylePage", "Gem")),
              c(S::Scudo, QT_TRANSLATE_NOOP("StylePage", "Shield")), c(S::FrecciaSinistra, QT_TRANSLATE_NOOP("StylePage", "Left arrow")), c(S::FrecciaDestra, QT_TRANSLATE_NOOP("StylePage", "Right arrow")), c(S::Etichetta, QT_TRANSLATE_NOOP("StylePage", "Tag")),
              c(S::Bandiera, QT_TRANSLATE_NOOP("StylePage", "Flag")), c(S::Nota, QT_TRANSLATE_NOOP("StylePage", "Folded note")), c(S::PilaDiSchede, QT_TRANSLATE_NOOP("StylePage", "Card stack")),
              c(S::SchedaConBordoLaterale, QT_TRANSLATE_NOOP("StylePage", "Side-bar card"))}},
            {QCoreApplication::translate("StylePage", "Punctual"),
             {c(S::ParentesiAngolari, QT_TRANSLATE_NOOP("StylePage", "Angle brackets")), c(S::DoppieParentesiAngolari, QT_TRANSLATE_NOOP("StylePage", "Double angle brackets")),
              c(S::ParentesiQuadre, QT_TRANSLATE_NOOP("StylePage", "Square brackets")), c(S::ParentesiTonde, QT_TRANSLATE_NOOP("StylePage", "Round brackets")), c(S::ParentesiGraffe, QT_TRANSLATE_NOOP("StylePage", "Curly brackets")),
              c(S::Angoli, QT_TRANSLATE_NOOP("StylePage", "Corners")), c(S::Virgolette, QT_TRANSLATE_NOOP("StylePage", "Quotation marks"))}},
            {QCoreApplication::translate("StylePage", "Flowchart"),
             {c(S::Decisione, QT_TRANSLATE_NOOP("StylePage", "Decision")), c(S::InputManuale, QT_TRANSLATE_NOOP("StylePage", "Manual input")), c(S::ProcessoPredefinito, QT_TRANSLATE_NOOP("StylePage", "Predefined process")),
              c(S::OperazioneManuale, QT_TRANSLATE_NOOP("StylePage", "Manual operation")), c(S::Database, QT_TRANSLATE_NOOP("StylePage", "Database")), c(S::Documento, QT_TRANSLATE_NOOP("StylePage", "Document")),
              c(S::DocumentiMultipli, QT_TRANSLATE_NOOP("StylePage", "Multiple documents")), c(S::Ritardo, QT_TRANSLATE_NOOP("StylePage", "Delay")), c(S::Dati, QT_TRANSLATE_NOOP("StylePage", "Data")), c(S::Connettore, QT_TRANSLATE_NOOP("StylePage", "Connector")),
              c(S::ConnettoreFuoriPagina, QT_TRANSLATE_NOOP("StylePage", "Off-page connector")), c(S::CerchioDoppio, QT_TRANSLATE_NOOP("StylePage", "Double circle")), c(S::Visualizzazione, QT_TRANSLATE_NOOP("StylePage", "Display")),
              c(S::Trapezio, QT_TRANSLATE_NOOP("StylePage", "Trapezoid")), c(S::TrapezioRovesciato, QT_TRANSLATE_NOOP("StylePage", "Inverted trapezoid"))}}};
}

QList<Choice> linePatterns(bool branch) {
    QList<Choice> items{{Qt::SolidLine, QCoreApplication::translate("StylePage", "Solid line")}};
    if (branch) items.append({NodeStyle::kFreehandSolidLine, QCoreApplication::translate("StylePage", "Hand-drawn solid")});
    items.append({Qt::DashLine, QCoreApplication::translate("StylePage", "Dashed line")});
    if (branch) items.append({NodeStyle::kFreehandDashLine, QCoreApplication::translate("StylePage", "Hand-drawn dashed")});
    items.append({Qt::DotLine, QCoreApplication::translate("StylePage", "Dotted")});
    if (branch) items.append({Qt::NoPen, QCoreApplication::translate("StylePage", "None")});
    return items;
}

QList<Choice> boundaryShapes() {
    // Literal texts: the translation tool finds only those.
    return {{int(Boundary::Shape::RoundedRectangle), QCoreApplication::translate("BoundaryStylePage", "Rounded rectangle")},
            {int(Boundary::Shape::Rectangle), QCoreApplication::translate("BoundaryStylePage", "Rectangle")},
            {int(Boundary::Shape::Waves), QCoreApplication::translate("BoundaryStylePage", "Waves")},
            {int(Boundary::Shape::Scallops), QCoreApplication::translate("BoundaryStylePage", "Scallops")},
            {int(Boundary::Shape::Tension), QCoreApplication::translate("BoundaryStylePage", "Tension")},
            {int(Boundary::Shape::Polygon), QCoreApplication::translate("BoundaryStylePage", "Polygon")},
            {int(Boundary::Shape::Ellipse), QCoreApplication::translate("BoundaryStylePage", "Ellipse")},
            {int(Boundary::Shape::Corners), QCoreApplication::translate("BoundaryStylePage", "Corner marks")},
            {int(Boundary::Shape::Tag), QCoreApplication::translate("BoundaryStylePage", "Tag")},  // FEAT-134
            {int(Boundary::Shape::RoundedLeft), QCoreApplication::translate("BoundaryStylePage", "Rounded left")},
            {int(Boundary::Shape::Frame), QCoreApplication::translate("BoundaryStylePage", "Frame")}};
}

QList<Choice> boundaryPatterns() {
    QList<Choice> items = linePatterns(false);
    items.append({Qt::NoPen, QCoreApplication::translate("StylePage", "None")});
    return items;
}

QList<Choice> branchShapes() {
    return {{int(BranchShape::RoundedBracket), QCoreApplication::translate("StylePage", "Rounded bracket")}, {int(BranchShape::Bracket), QCoreApplication::translate("StylePage", "Bracket")},
            {int(BranchShape::Straight), QCoreApplication::translate("StylePage", "Straight lines")},        {int(BranchShape::Curve), QCoreApplication::translate("StylePage", "Curves")},
            {int(BranchShape::SCurve), QCoreApplication::translate("StylePage", "S-curves")},                {int(BranchShape::Angled), QCoreApplication::translate("StylePage", "Angled lines")},
            {int(BranchShape::RoundedAngled), QCoreApplication::translate("StylePage", "Rounded angled lines")}};
}

QList<Choice> lineEnds() {
    return {{int(BranchEnd::None), QCoreApplication::translate("StylePage", "None")}, {int(BranchEnd::OpenArrow), QCoreApplication::translate("StylePage", "Open arrow")},
            {int(BranchEnd::FilledArrow), QCoreApplication::translate("StylePage", "Filled arrow")}, {int(BranchEnd::Dot), QCoreApplication::translate("StylePage", "Dot")},
            {int(BranchEnd::Square), QCoreApplication::translate("StylePage", "Square")}, {int(BranchEnd::Diamond), QCoreApplication::translate("StylePage", "Diamond")}};
}

QList<Choice> presets() {
    return {{int(NodePreset::MoltoImportante), QCoreApplication::translate("StylePage", "Very important")}, {int(NodePreset::Importante), QCoreApplication::translate("StylePage", "Important")},
            {int(NodePreset::Depenna), QCoreApplication::translate("StylePage", "Strike out")}, {int(NodePreset::Predefinito), QCoreApplication::translate("StylePage", "Default")}};
}

QList<Choice> imageEffects() {
    return {{int(ImageEffect::None), QCoreApplication::translate("ImageStylePage", "No effect")},
            {int(ImageEffect::Grayscale), QCoreApplication::translate("ImageStylePage", "Black and white")},
            {int(ImageEffect::Sepia), QCoreApplication::translate("ImageStylePage", "Sepia")},
            {int(ImageEffect::Invert), QCoreApplication::translate("ImageStylePage", "Negative")},
            {int(ImageEffect::Faded), QCoreApplication::translate("ImageStylePage", "Faded")}};
}

QList<Choice> textCases() {
    return {{int(TextCase::Nessuna), QCoreApplication::translate("StylePage", "No change")}, {int(TextCase::Maiuscolo), QCoreApplication::translate("StylePage", "All uppercase")},
            {int(TextCase::Minuscolo), QCoreApplication::translate("StylePage", "All lowercase")}};
}

QString caseLabel(int textCase) {
    switch (static_cast<TextCase>(textCase)) {
    case TextCase::Maiuscolo: return QStringLiteral("AA");
    case TextCase::Minuscolo: return QStringLiteral("aa");
    default: return QStringLiteral("Aa");
    }
}

QList<Choice> textAligns() {
    return {{int(TextAlign::Sinistra), QCoreApplication::translate("StylePage", "Align left")}, {int(TextAlign::Centro), QCoreApplication::translate("StylePage", "Center")},
            {int(TextAlign::Destra), QCoreApplication::translate("StylePage", "Align right")}, {int(TextAlign::Giustificato), QCoreApplication::translate("StylePage", "Justify")}};
}

QString alignIcon(int textAlign) {
    static const char *const names[] = {"left", "center", "right", "justify"};
    return QStringLiteral(":/assets/hugeicons/text-align-%1.svg").arg(QLatin1String(names[std::clamp(textAlign, 0, 3)]));
}

QString branchWidthName(int index) {
    // Context "branch width": the same English names are also font weights, translated differently.
    switch (index) {
    case 0: return QCoreApplication::translate("StylePage", "Extra thin", "branch width");
    case 1: return QCoreApplication::translate("StylePage", "Thin", "branch width");
    case 2: return QCoreApplication::translate("StylePage", "Medium", "branch width");
    case 3: return QCoreApplication::translate("StylePage", "Bold", "branch width");
    default: return QCoreApplication::translate("StylePage", "Extra bold", "branch width");
    }
}

QString weightName(int weight) {
    switch (weight) {
    case 100: return QCoreApplication::translate("StylePage", "Thin");
    case 200: return QCoreApplication::translate("StylePage", "Extra light");
    case 300: return QCoreApplication::translate("StylePage", "Light");
    case 400: return QCoreApplication::translate("StylePage", "Regular");
    case 500: return QCoreApplication::translate("StylePage", "Medium");
    case 600: return QCoreApplication::translate("StylePage", "Semibold");
    case 700: return QCoreApplication::translate("StylePage", "Bold");
    case 800: return QCoreApplication::translate("StylePage", "Extra bold");
    case 900: return QCoreApplication::translate("StylePage", "Black");
    default: return QString::number(weight);
    }
}

QList<int> fontWeights(const QString &family, int current) {
    const QString shown = family.isEmpty() ? NodeStyle::kDefaultMapFontFamily : family;  // default: Atkinson Hyperlegible
    QSet<int> weights{QFont::Normal, QFont::Bold};
    for (const QString &style : QFontDatabase::styles(shown))
        if (!QFontDatabase::italic(shown, style)) weights.insert(QFontDatabase::weight(shown, style));
    if (current > 0) weights.insert(current);
    QList<int> sorted;
    for (const int weight : std::as_const(weights))
        if (weight >= 100 && weight <= 900 && weight % 100 == 0) sorted.append(weight);
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

const QStringList &gridColors() {
    static const QStringList colors{
        "#ffffff", "#eeeeee", "#d0d0d0", "#adadad", "#999999", "#666666", "#333333", "#111111", "#000000",
        "#fdd834", "#ff9595", "#8ede99", "#0ce6cf", "#8eddf9", "#50c3f7", "#7986cb", "#ba69c8", "#ffabd5",
        "#ffc009", "#ff5252", "#2cd551", "#00a99d", "#2abee0", "#05a8f4", "#4051b5", "#9c27b0", "#ea8aba",
        "#ff9f00", "#ff3c00", "#19a719", "#007c74", "#0096bf", "#0288d1", "#303e9f", "#7b1fa2", "#ce4289",
        "#ff6f00", "#e32c2d", "#15831c", "#025a5a", "#00526b", "#00579b", "#1a227e", "#4a148c", "#a34075"};
    return colors;
}

namespace {
const QString kRecentKey = QStringLiteral("colorPicker/recent");
}

QStringList recentColors() { return QSettings().value(kRecentKey).toStringList().mid(0, kMaxRecentColors); }

void rememberRecentColor(const QColor &color) {
    if (!color.isValid()) return;
    QStringList recent = recentColors();
    const QString name = color.name(QColor::HexArgb);
    recent.removeAll(name);
    recent.prepend(name);
    QSettings().setValue(kRecentKey, recent.mid(0, kMaxRecentColors));
}

QList<MapStructure::Type> structureTypes() {
    using T = MapStructure::Type;
    return {T::MindMap, T::LogicChart, T::Brace, T::OrgChart, T::TreeChart, T::Timeline, T::Fishbone, T::TreeTable};
}

QString structureName(MapStructure::Type type) {
    switch (type) {
    case MapStructure::Type::LogicChart: return QCoreApplication::translate("StructurePicker", "Logic chart");
    case MapStructure::Type::Brace: return QCoreApplication::translate("StructurePicker", "Brace map");
    case MapStructure::Type::OrgChart: return QCoreApplication::translate("StructurePicker", "Org chart");
    case MapStructure::Type::TreeChart: return QCoreApplication::translate("StructurePicker", "Tree chart");
    case MapStructure::Type::Timeline: return QCoreApplication::translate("StructurePicker", "Timeline");
    case MapStructure::Type::Fishbone: return QCoreApplication::translate("StructurePicker", "Fishbone");
    case MapStructure::Type::TreeTable: return QCoreApplication::translate("StructurePicker", "Tree table");
    default: return QCoreApplication::translate("StructurePicker", "Mind map");
    }
}

QString directionName(MapStructure::Direction direction) {
    switch (direction) {
    case MapStructure::Direction::Right: return QCoreApplication::translate("StructurePicker", "Right");
    case MapStructure::Direction::Left: return QCoreApplication::translate("StructurePicker", "Left");
    case MapStructure::Direction::Down: return QCoreApplication::translate("StructurePicker", "Down");
    case MapStructure::Direction::Up: return QCoreApplication::translate("StructurePicker", "Up");
    case MapStructure::Direction::OffAxis: return QCoreApplication::translate("StructurePicker", "Off axis");
    default: return QCoreApplication::translate("StructurePicker", "Both sides");
    }
}

int noneValue(Icon kind) {
    switch (kind) {
    case Icon::FillStyle: return int(NodeFillStyle::Nessuno);
    case Icon::BorderStyle: return int(NodeBorderStyle::Nessuno);
    case Icon::BranchLine:
    case Icon::LinePattern: return Qt::NoPen;
    default: return -1;
    }
}

void drawTopicPreview(QPainter &painter, const QRectF &rect, const QString &title, const QColor &fill, const QColor &border,
                      const QColor &text, int weight) {
    painter.save();
    QFont font = painter.font();
    font.setPixelSize(13);
    font.setWeight(QFont::Weight(std::clamp(weight, 100, 900)));
    const QFontMetricsF metrics(font);
    const QString shown = QString(title).replace(QLatin1Char('\n'), QLatin1Char(' '));
    const qreal textWidth = std::min<qreal>(metrics.horizontalAdvance(shown), rect.width() - 34);
    const qreal width = std::max<qreal>(textWidth + 32, 48);
    const QRectF pill(rect.left() + (rect.width() - width) / 2, rect.top() + 1, width, rect.height() - 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(NodeStyle::kMapBackground);
    painter.drawRoundedRect(pill, 6, 6);
    painter.setPen(border.isValid() ? QPen(border, 2) : QPen(Qt::NoPen));
    painter.setBrush(fill);
    painter.drawRoundedRect(pill.adjusted(1, 1, -1, -1), 5, 5);
    painter.setFont(font);
    painter.setPen(text);
    painter.drawText(pill, Qt::AlignCenter, metrics.elidedText(shown, Qt::ElideRight, textWidth));
    painter.restore();
}

namespace {
const QString kFavoritesKey = QStringLiteral("palettes/favorites");
}

QStringList favoritePaletteIds() {
    QStringList ids;
    for (const QString &id : QSettings().value(kFavoritesKey).toStringList())
        if (CustomPalettes::lookup(id)) ids.append(id);  // a removed own palette disappears
    return ids;
}

bool isFavoritePalette(const QString &id) { return QSettings().value(kFavoritesKey).toStringList().contains(id); }

void setFavoritePalette(const QString &id, bool favorite) {
    QSettings settings;
    QStringList ids = settings.value(kFavoritesKey).toStringList();
    ids.removeAll(id);
    if (favorite) ids.append(id);
    settings.setValue(kFavoritesKey, ids);
}

void drawVariantThumbnail(QPainter &painter, const QRectF &rect, const QString &paletteId, const QStringList &palette, int variant) {
    const NodeStyle::Backdrop backdrop = NodeStyle::backdropFor(paletteId, palette, variant, std::nullopt);
    auto root = std::make_unique<MindNode>();
    for (int i = 0; i < 4; ++i) {
        auto main = std::make_unique<MindNode>();
        main->parent = root.get();
        root->children.push_back(std::move(main));
    }
    NodeStyle::resolveTree(root.get(), palette, backdrop);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = rect;
    painter.setPen(Qt::NoPen);
    painter.setBrush(backdrop.color);
    painter.drawRoundedRect(box, 4, 4);
    const QPointF center = box.center();
    const QRectF central(center.x() - 12, center.y() - 3, 24, 6);
    const qreal mainWidth = 18, mainHeight = 7;
    for (int i = 0; i < 4; ++i) {
        const MindNode *node = root->children[static_cast<size_t>(i)].get();
        const bool right = i % 2 == 0;
        const qreal y = center.y() + (i < 2 ? -9 : 9) - mainHeight / 2;
        const qreal x = right ? box.right() - 8 - mainWidth : box.left() + 8;
        const QRectF branch(x, y, mainWidth, mainHeight);
        painter.setPen(QPen(node->branchColor, 1));
        painter.drawLine(QPointF(right ? central.right() : central.left(), center.y()),
                         QPointF(right ? branch.left() : branch.right(), branch.center().y()));
        painter.setPen(Qt::NoPen);
        painter.setBrush(node->fill);
        painter.drawRoundedRect(branch, 2, 2);
    }
    painter.setBrush(root->text);  // the central topic is only text: a bar of its colour
    painter.drawRoundedRect(central, 2, 2);
    painter.restore();
}

void drawPalettePreview(QPainter &p, const QRectF &rect, const QStringList &colors, int fontPx) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(rect.topLeft());
    const qreal width = rect.width(), height = rect.height();
    p.setPen(Qt::NoPen);
    p.setBrush(NodeStyle::kMapBackground);
    p.drawRoundedRect(QRectF(0, 0, width, height), 8, 8);
    // A test tree: the root, a main topic per colour, each with a child and a grandchild.
    auto root = std::make_unique<MindNode>();
    QList<const MindNode *> mains;
    QStringList palette;
    for (const QString &color : colors) palette.append(color.isEmpty() ? QStringLiteral("#ffffff") : color);
    for (int i = 0; i < CustomPalettes::kColorCount; ++i) {
        auto main = std::make_unique<MindNode>();
        main->parent = root.get();
        auto child = std::make_unique<MindNode>();
        child->parent = main.get();
        auto grandchild = std::make_unique<MindNode>();
        grandchild->parent = child.get();
        child->children.push_back(std::move(grandchild));
        main->children.push_back(std::move(child));
        mains.append(main.get());
        root->children.push_back(std::move(main));
    }
    NodeStyle::resolveTree(root.get(), palette);
    constexpr qreal kPad = 12, kGap = 8, kBoxHeight = 28, kRowGap = 10;
    const qreal column = (width - 2 * kPad - (CustomPalettes::kColorCount - 1) * kGap) / CustomPalettes::kColorCount;
    QFont font = p.font();
    font.setPixelSize(fontPx);
    p.setFont(font);
    for (int i = 0; i < CustomPalettes::kColorCount; ++i) {
        const qreal x = kPad + i * (column + kGap);
        QRectF box(x, kPad, column, kBoxHeight);
        if (colors.value(i).isEmpty()) {
            p.setPen(QPen(QColor("#c4cad2"), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(x, kPad, column, height - 2 * kPad).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
            continue;
        }
        const MindNode *node = mains[i];
        for (int level = 0; level < 3; ++level) {
            p.setPen(Qt::NoPen);
            p.setBrush(node->fill);  // transparent where the level has no fill
            p.drawRoundedRect(box, 6, 6);
            p.setPen(node->text);
            p.drawText(box, Qt::AlignCenter, QStringLiteral("Aa"));
            box.translate(0, kBoxHeight + kRowGap);
            if (!node->children.empty()) node = node->children.front().get();
        }
    }
    p.restore();
}

void drawPreset(QPainter &painter, const QRectF &rect, int preset, const QString &name, const QColor &color, int depth,
                const NodeStyle::ImportantColors &important) {
    // Default without a solid fill also shows the border, so the pill can be seen.
    painter.save();
    const QRectF pill = rect.adjusted(1, 1, -1, -1);
    QColor fill = NodeStyle::kMapBackground;
    QColor border = color;
    QColor text = NodeStyle::darkenedText(color);
    bool strike = false;
    switch (static_cast<NodePreset>(preset)) {
    case NodePreset::MoltoImportante:
    case NodePreset::Importante:
        fill = static_cast<NodePreset>(preset) == NodePreset::MoltoImportante ? important.veryImportant : important.important;
        border = Qt::transparent;
        text = NodeStyle::readableTextOn(fill);
        break;
    case NodePreset::Depenna:
        strike = true;
        break;
    case NodePreset::Predefinito:
        if (depth == 1) {
            fill = color;
            border = Qt::transparent;
            text = NodeStyle::readableTextOn(color);
        } else if (depth == 2) {
            fill = color;
            fill.setAlphaF(0.2f);
        } else if (depth == 0) {
            text = QColor("#172033");
        }
        break;
    }
    if (fill.alpha() < 255) {  // a 20% tint: composed on the map's white
        painter.setPen(Qt::NoPen);
        painter.setBrush(NodeStyle::kMapBackground);
        painter.drawRoundedRect(pill, 6, 6);
    }
    painter.setPen(border.alpha() == 0 ? QPen(Qt::NoPen) : QPen(border, 2));
    painter.setBrush(fill);
    painter.drawRoundedRect(pill.adjusted(1, 1, -1, -1), 5, 5);
    QFont font = painter.font();
    font.setPixelSize(13);
    font.setWeight(QFont::DemiBold);
    font.setStrikeOut(strike);
    painter.setFont(font);
    painter.setPen(text);
    painter.drawText(pill, Qt::AlignCenter, name);
    painter.restore();
}

void drawIcon(Icon kind, int value, QPainter &painter, const QRectF &box, const QColor &ink, const QColor &muted) {
    if (value == noneValue(kind)) {
        drawNone(painter, box, muted);
        return;
    }
    painter.save();
    switch (kind) {
    case Icon::FillStyle: {
        const auto style = static_cast<NodeFillStyle>(value);
        if (style == NodeFillStyle::Pieno) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
            painter.drawRoundedRect(box, 3, 3);
        } else if (style == NodeFillStyle::PienoManoLibera) {
            drawSketch(painter, box, [&ink](QPainter &p, const QPainterPath &outline, qreal) { p.fillPath(NodeSketch::roughArea(outline, kIconSeed), ink); });
        } else {
            drawSketch(painter, box, [&ink, style](QPainter &p, const QPainterPath &outline, qreal scale) {
                const NodeSketch::Pattern pattern = NodeSketch::pattern(style, outline, kIconSeed);
                // Slightly thinner strokes in the small icon: at 22 x 14 px the light flakes would vanish and
                // the pattern would look like a plain fill.
                p.setPen(QPen(ink, pattern.penWidth * (scale < 0.5 ? 0.75 : 1.0), Qt::SolidLine, Qt::FlatCap));
                p.drawLines(pattern.lines);
            });
        }
        break;
    }
    case Icon::BorderStyle: {
        const auto style = static_cast<NodeBorderStyle>(value);
        const Qt::PenStyle pen = style == NodeBorderStyle::Tratteggiato || style == NodeBorderStyle::TratteggiatoManoLibera ? Qt::DashLine : Qt::SolidLine;
        if (NodeSketch::isFreehand(style)) {
            drawSketch(painter, box, [&ink, pen](QPainter &p, const QPainterPath &outline, qreal scale) {
                p.strokePath(NodeSketch::roughOutline(outline, kIconSeed), QPen(ink, 1.5 / scale, pen, Qt::RoundCap, Qt::RoundJoin));
            });
        } else {
            painter.setPen(QPen(ink, 1.5, pen));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(box, 3, 3);
        }
        break;
    }
    case Icon::Shape: {
        const auto shape = static_cast<NodeShape>(value);
        // A "content" box chosen so every shape fits the icon with its proportions.
        const QRectF content(0, 0, 36, 22);
        const QRectF outer = NodeShapes::outerRect(shape, content, 2);
        const qreal scale = std::min(box.width() / outer.width(), box.height() / outer.height());
        painter.translate(box.center());
        painter.scale(scale, scale);
        painter.translate(-outer.center());
        painter.setPen(QPen(ink, 1.5 / scale));
        painter.setBrush(Qt::NoBrush);
        if (NodeShapes::hasOpenBorder(shape))
            for (const QLineF &line : NodeShapes::borderLines(shape, outer, 2)) painter.drawLine(line);
        else
            painter.drawPath(NodeShapes::outline(shape, outer));
        for (const QPainterPath &line : NodeShapes::innerLines(shape, outer)) painter.drawPath(line);
        for (const QPainterPath &mark : NodeShapes::marks(shape, outer)) painter.drawPath(mark);
        painter.fillPath(NodeShapes::filledMarks(shape, outer), ink);
        break;
    }
    case Icon::BoundaryShape: {
        // Drawn on a 72 x 44 boundary so the waves and scallops keep their real measures, then fitted.
        const auto shape = static_cast<Boundary::Shape>(value);
        const QRectF stroke(0, 0, 72, 44);
        const QRectF outer = Boundaries::shapeBounds(shape, stroke);
        const qreal scale = std::min(box.width() / outer.width(), box.height() / outer.height());
        painter.translate(box.center());
        painter.scale(scale, scale);
        painter.translate(-outer.center());
        painter.strokePath(Boundaries::border(shape, stroke), QPen(ink, 1.5 / scale));
        break;
    }
    case Icon::BranchLine:
    case Icon::LinePattern: {
        const bool freehand = value == NodeStyle::kFreehandSolidLine || value == NodeStyle::kFreehandDashLine;
        const Qt::PenStyle pen = value == NodeStyle::kFreehandSolidLine ? Qt::SolidLine
                               : value == NodeStyle::kFreehandDashLine ? Qt::DashLine : static_cast<Qt::PenStyle>(value);
        const qreal scale = box.height() / 28;
        painter.translate(box.topLeft());
        painter.scale(scale, scale);
        const QLineF line(3, 14, box.width() / scale - 3, 14);
        QPainterPath path(line.p1());
        path.lineTo(line.p2());
        if (freehand) path = NodeSketch::roughLine(line, kIconSeed);
        painter.strokePath(path, QPen(ink, 2 / scale, pen, Qt::FlatCap, Qt::RoundJoin));
        break;
    }
    case Icon::BranchShape: {
        // Drawn in a 48 x 32 space (stub and bar at their real 12 px) and scaled to the icon.
        const auto shape = static_cast<BranchShape>(value);
        const qreal scale = std::min(box.width() / 48, box.height() / 32);
        painter.translate(box.center() - QPointF(24, 16) * scale);
        painter.scale(scale, scale);
        painter.setPen(QPen(ink, 1.5 / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const QPointF from(2, 16);
        for (const qreal y : {3.0, 16.0, 29.0}) {
            const QPointF to(46, y);
            painter.drawPath(shape == BranchShape::RoundedBracket || shape == BranchShape::Bracket
                                 ? BranchPath::bracket(from, to, shape == BranchShape::RoundedBracket)
                                 : BranchPath::fan(from, to, value));
        }
        break;
    }
    case Icon::LineStart:
    case Icon::LineEnd: {
        const bool atStart = kind == Icon::LineStart;
        const auto end = static_cast<BranchEnd>(value);
        const qreal y = box.center().y();
        const QPointF from(box.left() + 3, y), to(box.right() - 3, y);
        painter.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(from, to);
        const QPainterPath mark = BranchPath::endMark(end, atStart ? from : to, QPointF(atStart ? -1 : 1, 0), 2.5);
        if (!BranchPath::endIsStroke(end)) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(ink);
        }
        painter.drawPath(mark);
        break;
    }
    }
    painter.restore();
}

}  // namespace StyleChoices
