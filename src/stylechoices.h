#pragma once

#include <QColor>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

#include "mapstructure.h"
#include "nodestyle.h"

class QPainter;

// FEAT-119: the choices of the Style page's selects, in the desktop's menu order, with their names
// (the desktop's, context "StylePage", so both apps share the translations) and their icons, drawn
// as the map draws the style (NodeShapes, NodeSketch, BranchPath). Shared by the desktop's StylePage
// and the phone's side panel; no widget.
namespace StyleChoices {

struct Choice {
    int value = 0;
    QString name;
};
// A titled group of a grid menu (the shapes' Basic, Advanced, Punctual, Flowchart).
struct Group {
    QString title;
    QList<Choice> choices;
};

enum class Icon { FillStyle, BorderStyle, Shape, BranchLine, LinePattern, BranchShape, LineStart, LineEnd, BoundaryShape };

QList<Choice> fillStyles();     // "No fill" last
QList<Choice> borderStyles();   // "No border" last
QList<Group> shapeGroups();
QList<Choice> linePatterns(bool branch);  // branch: also freehand and "None" (last)
// FEAT-130: the boundaries' shapes (Boundary::Shape) and line styles ("None" last), names in the
// context "BoundaryStylePage" (the shapes) and "StylePage" (the line styles).
QList<Choice> boundaryShapes();
QList<Choice> boundaryPatterns();
QList<Choice> branchShapes();   // without Auto: the select shows the shape Auto draws
QList<Choice> lineEnds();
QList<Choice> presets();        // NodePreset, in the select's order
// FEAT-131: ImageEffect, "No effect" first; names in the context "ImageStylePage".
QList<Choice> imageEffects();
QList<Choice> textCases();      // TextCase; the name is the tooltip, caseLabel() the select's text
QString caseLabel(int textCase);  // "Aa", "AA", "aa"
QList<Choice> textAligns();     // TextAlign
QString alignIcon(int textAlign);  // the Hugeicons file of an alignment
QString branchWidthName(int index);  // NodeStyle::kNamedBranchWidths[index]
QString weightName(int weight);
// The font weights a family offers (upright styles, 100-900), Regular and Bold always, plus current.
QList<int> fontWeights(const QString &family, int current);

// The colour picker's grid (9 columns) and its recently used colours (#AARRGGBB, newest first, at
// most kMaxRecentColors, kept in the app's settings).
inline constexpr int kMaxRecentColors = 9;
const QStringList &gridColors();
QStringList recentColors();
void rememberRecentColor(const QColor &color);

// FEAT-026: the map types in the gallery's order (the desktop's StructurePicker, context
// "StructurePicker"), their names and the directions' names.
QList<MapStructure::Type> structureTypes();
QString structureName(MapStructure::Type type);
QString directionName(MapStructure::Direction direction);

// Draws a choice's icon in box (the desktop's 22 x 14 or 36 x 22 boxes, inset by 1.5 px) with ink;
// "none" choices (no fill, no border, no line) in muted, crossed.
void drawIcon(Icon kind, int value, QPainter &painter, const QRectF &box, const QColor &ink, const QColor &muted);
// FEAT-020: a preset's preview, the pill with its name as the map would draw the topic (branch colour,
// the theme's important colours, the level: 0 central, 1 main, 2 sub, 3 below), in rect.
void drawPreset(QPainter &painter, const QRectF &rect, int preset, const QString &name, const QColor &color, int depth,
                const NodeStyle::ImportantColors &important);
// FEAT-072 B9: the selected topic as the Style card shows it, centred in rect: its title in its own
// colours (fill over the map white, border unless invalid, text colour and weight), elided to rect.
void drawTopicPreview(QPainter &painter, const QRectF &rect, const QString &title, const QColor &fill, const QColor &border,
                      const QColor &text, int weight);
// FEAT-031: the favourite palettes (the star of the colour theme menu), in the order they were
// starred, kept in the app's settings ("palettes/favorites"); a removed own palette is left out.
QStringList favoritePaletteIds();
bool isFavoritePalette(const QString &id);
void setFavoritePalette(const QString &id, bool favorite);
// FEAT-036: a colour theme variant's small map in rect: the backdrop, the central topic as a bar of
// its text colour and four main topics with their lines, coloured by the map's own rules.
void drawVariantThumbnail(QPainter &painter, const QRectF &rect, const QString &paletteId, const QStringList &palette, int variant);
// FEAT-032: the new colour theme's preview (NewPaletteDialog): on the map's white, for each of the six
// colours a column with the main topic, a second and a third level coloured by NodeStyle::resolveTree,
// "Aa" in fontPx; a colour not chosen yet is a dashed column.
void drawPalettePreview(QPainter &painter, const QRectF &rect, const QStringList &colors, int fontPx);
// The value of the "none" choice of a kind, or -1 when it has none.
int noneValue(Icon kind);

}  // namespace StyleChoices
