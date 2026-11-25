#pragma once

#include "pitch.h"
#include "pitchtemplate.h"

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

class MindMapModel;
class QPainter;

// FEAT-120: the Pitch tab's names and icons, moved unchanged from the desktop's PitchPage (texts in
// its context "PitchPage", so both apps share the translations). Shared by the desktop's Pitch tab
// and the phone's Pitch page; no widget.
namespace PitchChoices {

// The name of a built-in theme, as the Change theme menu shows it.
QString themeName(Pitch::Theme theme);
// The Change theme menu's order: the basic themes, then the illustrated ones.
QList<Pitch::Theme> themeOrder();
// Pitch::Layout's names, in its order (the two rows of the tab).
QStringList layoutNames();
// Pitch::Delivery's names, in its order.
QStringList deliveryNames();
// Topic slide and List slide: Auto saying what it resolves to ("Auto (Visible)"), Visible, Hidden.
QStringList visibilityNames(bool autoShown);
// Pitch::Aspect's names, in its order.
QStringList aspectNames();
// The layout's icon in a 20 x 20 box at the painter's origin, in ink.
void drawLayoutIcon(QPainter *painter, Pitch::Layout layout, const QColor &ink);
// A card of the Change theme menu: a slide titled "Pitch" in look, the cover for an illustrated theme
// or a template.
void drawThemeCard(QPainter *painter, const QRectF &rect, const Pitch::Look &look, bool cover);

// FEAT-120 stage B: the template editor's names and values, moved unchanged from the desktop's
// PitchTemplateEditor (texts in its context "PitchTemplateEditor").
constexpr int kTemplateColorCount = 9;
constexpr int kTemplatePictureCount = 3;  // slides, cover and topics, logo
constexpr int kLogoSizeStep = 5;
// The colours in the editor's order: Background, Text, Titles, Lines, Accent, Cover background,
// Cover text, Topic background, Topic text.
QStringList templateColorNames();
QColor &templateColor(Pitch::Template &value, int index);
QColor templateColor(const Pitch::Template &value, int index);
// What the base look gives colour index when the template leaves it unset.
QColor baseTemplateColor(const Pitch::Look &look, int index);
QStringList templatePictureNames();
QByteArray &templatePicture(Pitch::Template &value, int index);
const QByteArray &templatePicture(const Pitch::Template &value, int index);
// The logo is kept as PNG up to kLogoMaxSide; the backgrounds up to kPictureMaxSide.
bool templatePictureIsLogo(int index);
// "Of the theme" first, then Pitch::ItemStyle's / Pitch::Motif's names in their order.
QStringList itemStyleNames();
QStringList motifNames();
// Pitch::Corner's names, in its order.
QStringList cornerNames();
// The logo sizes offered, kMinLogoSize..kMaxLogoSize in kLogoSizeStep.
QList<int> logoSizes();
// The name a template's exported file gets: characters no file name may hold become "_".
QString templateFileName(const QString &name);

// The Change theme menu's Templates group: the library's templates, the sheet's first when the
// library lacks it (onlyHere, "Name (this map)").
struct TemplateCard {
    Pitch::Template value;
    bool onlyHere = false;
    QString name;  // as the card shows it
};
QList<TemplateCard> templateCards(const MindMapModel &model);

}  // namespace PitchChoices
