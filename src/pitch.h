#pragma once

// FEAT-063 stages A and A2 (docs/features/FEAT-063-pitch-presentazione.md): a presentation made from the map.
// The slides come from the topics and their Pitch settings; this file builds and draws them. No
// widgets: the Pitch tab, its preview and the full-screen window use it.

#include "nodestyle.h"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

class MindMapModel;
class QFont;
class QPainter;
struct MindNode;

namespace Pitch {

// Stored values (empty = Auto / default). Unknown values read as empty.
enum class Visibility { Auto, Visible, Hidden };
enum class Delivery { OneMemorize, One, All };  // default: one at a time, earlier ones dimmed
// Default: bracket. The order of the two rows of the Pitch tab (user's decision 2026-10-26).
enum class Layout { Bracket, Bullets, Right, Fan, Org, Fishbone, Timeline, Grid, Numbered, Circle };
constexpr int kLayoutCount = 10;
// Default: dark. Stage B adds illustrated themes of our own after the three basic ones.
enum class Theme { Dark, Light, Sheet, Proposal, Playful, Launch, Manifesto, Halftone, Paper, Night, Soft, Portfolio, Editorial };
constexpr int kThemeCount = 13;
constexpr int kBasicThemeCount = 3;
enum class Aspect { Auto, Wide, Classic, Tall, TallClassic };  // Auto, 16:9, 4:3, 9:16, 3:4

QString visibilityKey(Visibility value);
Visibility visibilityFromKey(const QString &key);
QString deliveryKey(Delivery value);
Delivery deliveryFromKey(const QString &key);
QString layoutKey(Layout value);
Layout layoutFromKey(const QString &key);
QString themeKey(Theme value);
Theme themeFromKey(const QString &key);
QString aspectKey(Aspect value);
Aspect aspectFromKey(const QString &key);
// The stored form of a value read from a file: itself if known, otherwise empty.
QString validVisibility(const QString &key);
QString validDelivery(const QString &key);
QString validLayout(const QString &key);
QString validTheme(const QString &key);
QString validAspect(const QString &key);
// Width / height of the slide; Auto = screen (the screen's ratio, 16:9 without one).
qreal aspectRatio(Aspect aspect, qreal screenRatio);

constexpr int kMaxListItems = 23;     // vertical layouts: 4% text shrinks to 2% (half), then a new slide
constexpr int kMaxRowItems = 6;       // org chart: one row
constexpr int kMaxSpreadItems = 8;    // fishbone, timeline and circle: around the title or along a line
constexpr int kMaxGridItems = 9;      // grid: 3 x 3 cards
// Stage A2: a list with a figure among its items keeps fewer of them, so the figures have room.
constexpr int kMaxListItemsWithFigures = 8;
constexpr int kMaxGridItemsWithFigures = 6;
// Items per slide of a layout; more continue on the next slide.
int maxItems(Layout layout, bool withFigures = false);
constexpr qreal kDimmedOpacity = 0.4; // earlier items with "one at a time - memorization"

// Stage A2: what a title or an item shows besides its text, as in the topic's node: the figure
// (image, sticker or formula) on its side of the text and the markers on the left.
struct Extras {
    enum class Side { Above, Below, Left, Right };
    QImage figure;            // ready to draw (rotated, sticker outline); null = none
    Side side = Side::Above;
    qreal figureLines = 0;    // figure height in lines of the node's text: the proportion kept
    QSizeF nodeFigureSize;    // in the node, for the aspect ratio and the border width
    ImageStyle figureStyle;
    QStringList markers;
    bool hasFigure() const { return !figure.isNull() && figureLines > 0 && !nodeFigureSize.isEmpty(); }
};
Extras extrasOf(const MindNode *node);

// Where an entry's parts go inside its box. block: their union.
struct EntryLayout {
    QRectF text;
    QRectF figure;            // empty = no figure shown
    QList<QRectF> markers;
    QRectF block;
    int textFlags = 0;        // for QPainter::drawText of the text
};
// Places text, markers and figure inside box. alignment: Qt alignment flags (plus Qt::TextWordWrap
// to wrap the text, otherwise one elided line). The figure is figureLines lines of font tall times
// figureScale, at most figureCap, and shrinks to fit the box; a figure beside the text goes above
// it when the text would be left without room.
EntryLayout layoutEntry(const QRectF &box, int alignment, const QFont &font, const QString &text, const Extras &extras,
                        const QSizeF &figureCap, qreal figureScale = 1);

struct Slide {
    enum class Kind { Topic, List };
    Kind kind = Kind::Topic;
    QString topicId;
    QString title;
    bool cover = false;       // the central topic's topic slide
    QStringList items;        // List: the children's titles shown on this slide
    Extras titleExtras;
    QList<Extras> itemExtras; // one per item; missing ones show none
    bool continued = false;   // List: the rest of a longer list
    int number = 0;           // 1-based place in the presentation; 0 = not known (the preview)
    QString notes;            // stage C: the topic's notes, on the first of its slides (speaker notes)
    Layout layout = Layout::Bracket;
    Delivery delivery = Delivery::OneMemorize;
    // How many advances the slide takes after it appears: one per item when one at a time.
    int steps() const { return kind == Kind::List && delivery != Delivery::All ? int(items.size()) : 0; }
};

// Whether the topic's slides appear, Auto resolved: the topic slide for the central and main topics,
// the list slide for a topic with children.
bool showsTopicSlide(const MindNode *node);
bool showsListSlide(const MindNode *node);
// What Auto resolves to, whatever is chosen (the "Auto (Visible)" label).
bool autoTopicSlide(const MindNode *node);
bool autoListSlide(const MindNode *node);
// Depth first in map order (floating topics after the main branches): topic slide, list slide, then
// the children's slides. Collapsed branches are presented too.
QList<Slide> slides(const MindMapModel &model);
// The slide the Pitch tab previews for a topic: its list slide if it has one, else its topic slide.
Slide previewSlide(const MindMapModel &model, const MindNode *node);

// Stage B: the type of a theme's titles or items. Empty family = Look::family; weight -1 = bold for
// titles, normal for items; stretch in percent (Roboto narrows to 75).
struct FontSpec {
    QString family;
    int weight = -1;
    int stretch = 100;
    bool uppercase = false;
    int spacing = 100;  // letter spacing in percent
};
// A list item as text, in an outlined pill, on a filled badge, or on a label in the branch colours.
enum class ItemStyle { Plain, Pill, Badge, Label };
enum class Motif { None, Band, Clouds, Smudge, Bars, Halftone, Grain, Lines, Blobs, Rules, Frame };
constexpr int kItemStyleCount = 4;
constexpr int kMotifCount = 11;
enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };  // FEAT-089: the logo's
// The Launch smudge: layers of one stroke, each this opaque (the test of contrast composes them).
constexpr int kSmudgeLayers = 12;
constexpr qreal kSmudgeAlphaLight = 0.022;
constexpr qreal kSmudgeAlphaDark = 0.018;

struct Look {
    QColor background;
    QColor text;
    QColor line;
    QList<QColor> branches;  // Sheet theme: the colours of the main branches; empty = line
    QString family;
    // Stage B (illustrated themes). Invalid colours fall back to the ones above.
    QColor title;            // titles of list slides
    QColor coverBackground;  // the cover and the topic slides may have their own ground and text
    QColor coverText;
    QColor topicBackground;
    QColor topicText;
    QColor fill;             // Badge items, the Clouds frame, the Paper and Editorial accents
    QString caption;         // Rules: the central topic's title, in the header
    FontSpec titleFont;
    FontSpec itemFont;
    ItemStyle itemStyle = ItemStyle::Plain;
    Motif motif = Motif::None;
    // FEAT-089: a template's pictures, logo and footer (the built-in themes have none).
    QImage picture;          // covers the list slides, cropped to fill
    QImage coverPicture;     // covers the cover and the topic slides; null = picture
    QImage logo;
    Corner logoCorner = Corner::BottomRight;
    qreal logoSize = 0.1;    // the logo's height, as a share of the slide's
    QString footer;
    bool numbers = false;    // the slide's number at the foot
};
Look look(const MindMapModel &model, Theme theme);
// The look on slide: its ground and text (the cover and the topic slides may differ).
Look lookOn(const Look &look, const Slide &slide);
// Label items: the ground of item i (the branch colours in turn) and its text, the more legible of
// the theme's text and white.
QColor labelFill(const Look &look, int item);
QColor labelText(const Look &look, int item);

// Draws slide into rect (the whole slide). shown: items visible (-1 = all); newest: opacity of the
// last shown item while it fades in.
// Stage C: a text of a slide, captured instead of drawn, for the export's editable text boxes.
struct TextRun {
    QRectF rect;
    int flags = 0;   // QPainter::drawText flags
    QFont font;
    QColor color;
    QString text;    // as drawn (an item on one line is already elided)
    int item = -1;   // the item it belongs to; -1 = the title
};
// texts: when given, the titles and items are not drawn but appended there (all the rest is drawn).
void paint(QPainter *painter, const QRectF &rect, const Slide &slide, const Look &look, int shown = -1, qreal newest = 1.0,
           QList<TextRun> *texts = nullptr);
}  // namespace Pitch
