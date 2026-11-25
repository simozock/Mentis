#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QVariant>

#include <functional>
#include <QtQml/qqmlregistration.h>

#include <QQuickPaintedItem>

class MobileMap;
#include "nodestyle.h"

struct ImageStyle;
struct MindNode;

// FEAT-119 stage A: the side panel's Style page on the phone. What the desktop's StylePage reads
// (NodeStyle::read of the selected topics: the common value, mixed, set by hand) and the calls it
// makes (MainWindow's StylePage connections: setStyle, setCustomWidths, copyStyle,
// copyStyleToDescendants, resetStyle, setStructure), each one Undo step through MobileMap. The
// choices and their names are StyleChoices', as on the desktop.
class MobileStyle : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MobileMap *map READ map WRITE setMap NOTIFY mapChanged)
    // Bumped when the selection or the map changes: the page's bindings read through it.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int topicCount READ topicCount NOTIFY changed)
    Q_PROPERTY(bool central READ central NOTIFY changed)  // one topic, the central one
    // What the Style page edits: the topics, or alone the picture, the relationship or the boundary
    // (MainWindow::refreshStylePage's pages).
    Q_PROPERTY(int target READ target NOTIFY changed)

public:
    // StyleProperty's values, for QML.
    enum Property {
        FillColor, TextColor, BorderColor, BranchColor, BranchWidth, BranchStyle, Preset, FillStyle, BorderStyle, BorderWidth,
        Shape, FontFamily, FontSize, FontWeight, Italic, Underline, StrikeOut, TextCase, TextAlign, BranchShape, BranchStart,
        BranchEnd, Numbering,
        // FEAT-040 ImageStylePage, FEAT-053 RelationshipStylePage, FEAT-052 BoundaryStylePage.
        PictureBorder, PictureShape, PictureEffect, PictureBorderWidth, PictureBorderColor, PictureShadow, PictureOutline, PictureOpacity,
        RelationShape, RelationPattern, RelationWidth, RelationLineColor, RelationStart, RelationEnd, RelationFont,
        RelationFontSize, RelationTextColor,
        BoundaryShape, BoundaryFillColor, BoundaryPattern, BoundaryWidth, BoundaryLineColor,
        BoundaryTitleFont, BoundaryTitleSize, BoundaryTitleColor, BoundaryTitleBold,  // FEAT-130
        BoundaryFillStyle,  // FEAT-134
        // FEAT-119 stage B: the Map page's colours (SheetStylePanel's Colour, MainWindow's Background color).
        SheetLineColor, SheetBackground
    };
    Q_ENUM(Property)
    enum Target { Topics, Picture, Relation, BoundaryTarget };
    Q_ENUM(Target)
    // The icon kinds of StyleChoices::Icon, for StyleIcon; PictureBorderIcon is ImageStylePage's.
    enum IconKind { FillStyleIcon, BorderStyleIcon, ShapeIcon, BranchLineIcon, LinePatternIcon, BranchShapeIcon, LineStartIcon, LineEndIcon,
                    BoundaryShapeIcon, PictureBorderIcon,
                    // FEAT-122 C2: the Gantt's pictures, GanttStyleChoices::List from BarShape on.
                    GanttIcons };
    Q_ENUM(IconKind)

    explicit MobileStyle(QObject *parent = nullptr);

    MobileMap *map() const { return m_map; }
    void setMap(MobileMap *map);
    int revision() const { return m_revision; }
    int topicCount() const;
    bool central() const;
    int target() const;

    // {empty, mixed, manual, value}: NodeStyle::Reading of the selected topics. BranchShape Auto shows
    // the shape it draws (curves from the central topic, rounded bracket below), as the desktop.
    Q_INVOKABLE QVariantMap reading(int property) const;
    // [{value, name, group}]: a select's choices; group is a title before the first of a group (shapes).
    Q_INVOKABLE QVariantList choices(int property) const;
    // The icon kind of a property's choices, -1 for a text list.
    Q_INVOKABLE int iconKind(int property) const;
    // The value's name among the property's choices; empty when it is not one of them.
    Q_INVOKABLE QString choiceName(int property, const QVariant &value) const;
    Q_INVOKABLE bool choose(int property, const QVariant &value);

    // FEAT-021: the text box width, {empty, mixed, manual, value} in px; Fit only when one is set by hand.
    Q_INVOKABLE QVariantMap widthReading() const;
    Q_INVOKABLE bool canFitWidth() const;
    Q_INVOKABLE bool chooseWidth(qreal px);
    Q_INVOKABLE bool stepWidth(qreal deltaPx);
    Q_INVOKABLE bool fitWidth();

    // FEAT-072 B15: the actions at the bottom, visible and enabled as on the desktop.
    Q_INVOKABLE bool showApplyToRelated() const;
    Q_INVOKABLE bool canApplyToRelated() const;
    Q_INVOKABLE bool canApplyToChildren() const;
    Q_INVOKABLE bool canResetStyle() const;
    Q_INVOKABLE bool applyToRelated();
    Q_INVOKABLE bool applyToChildren();
    Q_INVOKABLE bool resetStyle();

    // FEAT-026: one topic's structure: [{key, name}] (Same as parent first for a topic, Mind map left
    // out inside a tree table), its key, its type's directions [{key, name, enabled}] (enabled: it changes
    // the layout, MapStructure::effectiveDirections) and its direction.
    Q_INVOKABLE QVariantList structureTypes() const;
    Q_INVOKABLE QString structureType() const;
    Q_INVOKABLE QVariantList structureDirections(const QString &typeKey) const;
    Q_INVOKABLE QString structureDirection() const;
    Q_INVOKABLE bool chooseStructure(const QString &typeKey, const QString &directionKey);

    // FEAT-024: the numbering of the selected topics' branches as NumberingFields shows it:
    // {empty, mixed, preset (-1 none, -2 custom), format, separator, trailingSeparator, start,
    // hierarchical, levels}, its presets and separators; chooseNumbering takes the same map. With
    // sheet, the central topic's: the whole map's, on the Map page.
    Q_INVOKABLE QVariantMap numbering(bool sheet = false) const;
    Q_INVOKABLE QStringList numberingPresets(const QString &separator) const;
    Q_INVOKABLE QStringList numberingSeparators() const;
    Q_INVOKABLE int numberingMaxLevels() const;
    Q_INVOKABLE int numberingMaxStart() const;
    Q_INVOKABLE bool numberingValid(const QVariantMap &numbering) const;
    Q_INVOKABLE bool chooseNumbering(const QVariantMap &numbering, bool sheet = false);

    // FEAT-022: the fonts (the default first, its value empty), the weights the shown family offers and
    // the text sizes in px.
    Q_INVOKABLE QVariantList fontFamilies() const;
    Q_INVOKABLE QVariantList fontWeights() const;
    Q_INVOKABLE QVariantList fontSizes() const;
    Q_INVOKABLE bool chooseFontSizePx(qreal px);  // false (nothing set) outside the desktop's range

    // FEAT-020: the presets' previews, in the first selected topic's branch colour and level.
    Q_INVOKABLE QColor presetColor() const;
    Q_INVOKABLE int presetDepth() const;
    Q_INVOKABLE QStringList themeColors() const;
    // FEAT-072 B9: with one topic, the preset field shows it (the desktop's topicPreview): {title,
    // fill, border (invalid = none), text, weight}; empty otherwise.
    Q_INVOKABLE QVariantMap topicPreview() const;

    // The colour sheet (the desktop's ColorPicker): its grid, the recently used colours, and a colour
    // chosen for a property, remembered among the recent ones.
    Q_INVOKABLE QStringList gridColors() const;
    Q_INVOKABLE QStringList recentColors() const;
    Q_INVOKABLE bool chooseColor(int property, const QColor &color);

    // FEAT-040: the selected picture's size {empty, width, height} and sticker; a width or height with
    // the proportions kept or not (ImageStylePage::chooseWidth, chooseHeight), the size it was
    // inserted at, the 90 degree turn and the reset of its style, each one Undo step.
    Q_INVOKABLE QVariantMap pictureSize() const;
    Q_INVOKABLE bool pictureIsSticker() const;
    Q_INVOKABLE bool choosePictureWidth(qreal px, bool keepProportions);
    Q_INVOKABLE bool choosePictureHeight(qreal px, bool keepProportions);
    Q_INVOKABLE bool resetPictureSize();
    Q_INVOKABLE bool rotatePicture();
    Q_INVOKABLE bool resetPictureStyle();

    // FEAT-119 stage B, the Map page (MainWindow's Map page and SheetStylePanel). sheet(): {fontFamily,
    // width (negative = tapered), coloredBranches, showNotes, compact, freeBranches, justify, balanced,
    // balanceEnabled, canReset, paletteId, paletteName, paletteColors, notInstalled, paletteSavedName, variant,
    // variantCount}. Each choice is one Undo step, as on the desktop.
    Q_INVOKABLE QVariantMap sheet() const;
    Q_INVOKABLE QVariantList sheetFonts() const;
    Q_INVOKABLE QVariantList sheetWidths() const;
    // key: fontFamily, width, coloredBranches, showNotes, compact, freeBranches, justify.
    Q_INVOKABLE bool chooseSheet(const QString &key, const QVariant &value);
    Q_INVOKABLE bool chooseBalanced(bool on);
    Q_INVOKABLE bool chooseVariant(int variant);
    Q_INVOKABLE bool resetMapStyle();
    // FEAT-031 the colour theme menu: group 0 Favourites, 1 Colourful, 2 Classic, 3 Custom, each
    // [{id, name, colors, favorite, custom}]; the star; the choice; FEAT-032 adding the sheet's own
    // palette this phone does not have, and removing an own palette (not an Undo step).
    Q_INVOKABLE QVariantList palettes(int group) const;
    Q_INVOKABLE int paletteGroup(const QString &id) const;
    Q_INVOKABLE void setFavoritePalette(const QString &id, bool favorite);
    Q_INVOKABLE bool choosePalette(const QString &id);
    Q_INVOKABLE bool addCurrentPalette();
    Q_INVOKABLE bool removePalette(const QString &id);
    // FEAT-032, the theme editor (NewPaletteDialog, MainWindow::createPalette): the name an empty field
    // gives, whether a name is taken, and the new own theme put on the sheet (one Undo step).
    Q_INVOKABLE QString nextPaletteName() const;
    Q_INVOKABLE bool isPaletteNameTaken(const QString &name) const;
    Q_INVOKABLE bool createPalette(const QString &name, const QStringList &colors);
    // ColorPicker::choose: a colour picked for a field that is not a style (the theme editor's) joins
    // the recently used ones.
    Q_INVOKABLE void rememberColor(const QColor &color);
    // FEAT-072 B16, the map type card: the sheet's variant as the card shows it (after a type change
    // from the central topic's Structure, the type's first variant), [{type, name, variants}] in the
    // gallery's order, and the choice: the sheet's type and the variant's style, one Undo step.
    Q_INVOKABLE QString mapVariant() const;
    Q_INVOKABLE QString mapTypeName(const QString &variant) const;
    Q_INVOKABLE QVariantList mapTypes() const;
    Q_INVOKABLE bool chooseMapVariant(const QString &variant);

signals:
    void mapChanged();
    void changed();

private:
    QList<MindNode *> nodes() const;
    QList<const MindNode *> constNodes() const;
    bool edited(bool changed);
    bool applyWidths(const std::function<qreal(const MindNode *)> &widthFor);
    qreal manualWidthFor(const MindNode *node, qreal requestedPx) const;
    void bump();
    MindNode *pictureNode() const;
    bool choosePictureStyle(const std::function<void(ImageStyle &)> &change);
    bool chooseObject(int property, const QVariant &value);
    QVariantMap objectReading(int property) const;
    QVariantMap sheetReading(int property) const;
    bool chooseSheetColor(int property, const QColor &color);
    bool editSheetStyle(const std::function<void(NodeStyle::SheetStyle &)> &change);

    QPointer<MobileMap> m_map;
    int m_revision = 0;
};

// FEAT-036: a colour theme variant's small map (StyleChoices::drawVariantThumbnail, VariantPicker's tiles).
class VariantThumbnail : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString paletteId MEMBER m_paletteId NOTIFY appearanceChanged)
    Q_PROPERTY(QStringList colors MEMBER m_colors NOTIFY appearanceChanged)
    Q_PROPERTY(int variant MEMBER m_variant NOTIFY appearanceChanged)
public:
    explicit VariantThumbnail(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
signals:
    void appearanceChanged();
private:
    QString m_paletteId;
    QStringList m_colors;
    int m_variant = 0;
};

// FEAT-032: the new colour theme's preview (StyleChoices::drawPalettePreview).
class PalettePreview : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList colors MEMBER m_colors NOTIFY appearanceChanged)
public:
    explicit PalettePreview(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
signals:
    void appearanceChanged();
private:
    QStringList m_colors;
};

// FEAT-031: a palette's colours side by side in a rounded strip with a 1 px border, as the
// desktop's paintStrip (PaletteSelect, PalettePicker's tiles).
class PaletteStrip : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList colors MEMBER m_colors NOTIFY appearanceChanged)
    Q_PROPERTY(QColor borderColor MEMBER m_border NOTIFY appearanceChanged)
public:
    explicit PaletteStrip(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
signals:
    void appearanceChanged();
private:
    QStringList m_colors;
    QColor m_border{0x38, 0x3c, 0x40};  // Theme.darkDivider
};

// FEAT-119: a style choice's icon as StyleChoices draws it (the desktop's select icons), or with
// kind -1 and preset >= 0 the preset's preview pill (the desktop's presetIcon).
class StyleIcon : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int kind READ kind WRITE setKind NOTIFY appearanceChanged)
    Q_PROPERTY(int value READ value WRITE setValue NOTIFY appearanceChanged)
    Q_PROPERTY(QColor ink READ ink WRITE setInk NOTIFY appearanceChanged)
    Q_PROPERTY(QColor muted READ muted WRITE setMuted NOTIFY appearanceChanged)
    // Preset previews: the preset, its name, the branch colour, the level and the theme's colours.
    Q_PROPERTY(int preset READ preset WRITE setPreset NOTIFY appearanceChanged)
    Q_PROPERTY(QString presetName READ presetName WRITE setPresetName NOTIFY appearanceChanged)
    Q_PROPERTY(QColor presetColor READ presetColor WRITE setPresetColor NOTIFY appearanceChanged)
    Q_PROPERTY(int presetDepth READ presetDepth WRITE setPresetDepth NOTIFY appearanceChanged)
    Q_PROPERTY(QStringList themeColors READ themeColors WRITE setThemeColors NOTIFY appearanceChanged)
    // With kind -1 and a non-empty topic (MobileStyle::topicPreview), the selected topic's pill.
    Q_PROPERTY(QVariantMap topic READ topic WRITE setTopic NOTIFY appearanceChanged)

public:
    explicit StyleIcon(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

    int kind() const { return m_kind; }
    void setKind(int kind) { set(m_kind, kind); }
    int value() const { return m_value; }
    void setValue(int value) { set(m_value, value); }
    QColor ink() const { return m_ink; }
    void setInk(const QColor &ink) { set(m_ink, ink); }
    QColor muted() const { return m_muted; }
    void setMuted(const QColor &muted) { set(m_muted, muted); }
    int preset() const { return m_preset; }
    void setPreset(int preset) { set(m_preset, preset); }
    QString presetName() const { return m_presetName; }
    void setPresetName(const QString &name) { set(m_presetName, name); }
    QColor presetColor() const { return m_presetColor; }
    void setPresetColor(const QColor &color) { set(m_presetColor, color); }
    int presetDepth() const { return m_presetDepth; }
    void setPresetDepth(int depth) { set(m_presetDepth, depth); }
    QStringList themeColors() const { return m_themeColors; }
    void setThemeColors(const QStringList &colors) { set(m_themeColors, colors); }
    QVariantMap topic() const { return m_topic; }
    void setTopic(const QVariantMap &topic) { set(m_topic, topic); }

signals:
    void appearanceChanged();

private:
    template <typename T> void set(T &field, const T &value) {
        if (field == value) return;
        field = value;
        emit appearanceChanged();
        update();
    }
    int m_kind = -1;
    int m_value = 0;
    QColor m_ink = Qt::white;
    QColor m_muted = QColor("#747a80");
    int m_preset = -1;
    QString m_presetName;
    QColor m_presetColor;
    int m_presetDepth = 1;
    QStringList m_themeColors;
    QVariantMap m_topic;
};
