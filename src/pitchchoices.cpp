#include "pitchchoices.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {
QString translated(const char *text) { return QCoreApplication::translate("PitchPage", text); }
QString editorText(const char *text) { return QCoreApplication::translate("PitchTemplateEditor", text); }
using ColorMember = QColor Pitch::Template::*;
constexpr std::array<ColorMember, PitchChoices::kTemplateColorCount> kColorMembers{
    &Pitch::Template::background, &Pitch::Template::text, &Pitch::Template::title,
    &Pitch::Template::line, &Pitch::Template::fill, &Pitch::Template::coverBackground,
    &Pitch::Template::coverText, &Pitch::Template::topicBackground, &Pitch::Template::topicText};
constexpr std::array<QByteArray Pitch::Template::*, PitchChoices::kTemplatePictureCount> kPictureMembers{
    &Pitch::Template::picture, &Pitch::Template::coverPicture, &Pitch::Template::logo};
}  // namespace

namespace PitchChoices {

QString themeName(Pitch::Theme theme) {
    static const char *const names[Pitch::kThemeCount] = {
        QT_TRANSLATE_NOOP("PitchPage", "Dark"),        QT_TRANSLATE_NOOP("PitchPage", "Light"),
        QT_TRANSLATE_NOOP("PitchPage", "Sheet theme"), QT_TRANSLATE_NOOP("PitchPage", "Proposal"),
        QT_TRANSLATE_NOOP("PitchPage", "Playful"),     QT_TRANSLATE_NOOP("PitchPage", "Launch"),
        QT_TRANSLATE_NOOP("PitchPage", "Manifesto"),   QT_TRANSLATE_NOOP("PitchPage", "Halftone"),
        QT_TRANSLATE_NOOP("PitchPage", "Paper"),       QT_TRANSLATE_NOOP("PitchPage", "Night"),
        QT_TRANSLATE_NOOP("PitchPage", "Soft"),        QT_TRANSLATE_NOOP("PitchPage", "Portfolio"),
        QT_TRANSLATE_NOOP("PitchPage", "Editorial")};
    return translated(names[int(theme)]);
}

QList<Pitch::Theme> themeOrder() {
    using T = Pitch::Theme;
    return {T::Light, T::Dark, T::Sheet, T::Proposal, T::Paper, T::Playful, T::Launch,
            T::Night, T::Manifesto, T::Halftone, T::Soft, T::Portfolio, T::Editorial};
}

QStringList layoutNames() {
    return {translated(QT_TRANSLATE_NOOP("PitchPage", "Bracket")),   translated(QT_TRANSLATE_NOOP("PitchPage", "Bullet list")),
            translated(QT_TRANSLATE_NOOP("PitchPage", "Aligned right")), translated(QT_TRANSLATE_NOOP("PitchPage", "Fan")),
            translated(QT_TRANSLATE_NOOP("PitchPage", "Org chart")), translated(QT_TRANSLATE_NOOP("PitchPage", "Fishbone")),
            translated(QT_TRANSLATE_NOOP("PitchPage", "Timeline")),  translated(QT_TRANSLATE_NOOP("PitchPage", "Grid")),
            translated(QT_TRANSLATE_NOOP("PitchPage", "Numbered list")), translated(QT_TRANSLATE_NOOP("PitchPage", "Circle"))};
}

QStringList deliveryNames() {
    return {translated(QT_TRANSLATE_NOOP("PitchPage", "One at a time - memorize")), translated(QT_TRANSLATE_NOOP("PitchPage", "One at a time")),
            translated(QT_TRANSLATE_NOOP("PitchPage", "All together"))};
}

QStringList visibilityNames(bool autoShown) {
    const QString visible = translated(QT_TRANSLATE_NOOP("PitchPage", "Visible"));
    const QString hidden = translated(QT_TRANSLATE_NOOP("PitchPage", "Hidden"));
    return {translated(QT_TRANSLATE_NOOP("PitchPage", "Auto (%1)")).arg(autoShown ? visible : hidden), visible, hidden};
}

QStringList aspectNames() {
    return {translated(QT_TRANSLATE_NOOP("PitchPage", "Auto")), QStringLiteral("16:9"), QStringLiteral("4:3"), QStringLiteral("9:16"),
            QStringLiteral("3:4")};
}

void drawLayoutIcon(QPainter *painter, Pitch::Layout layout, const QColor &ink) {
    QPainter &p = *painter;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const auto lines = [&p](qreal x0, qreal x1, std::initializer_list<qreal> ys) {
        for (qreal y : ys) p.drawLine(QPointF(x0, y), QPointF(x1, y));
    };
    switch (layout) {
    case Pitch::Layout::Bracket: {
        QPainterPath brace(QPointF(9, 5));
        brace.quadTo(7, 5, 7, 7); brace.lineTo(7, 9); brace.quadTo(7, 10, 5.5, 10);
        brace.quadTo(7, 10, 7, 11); brace.lineTo(7, 13); brace.quadTo(7, 15, 9, 15);
        p.drawPath(brace);
        p.drawLine(QPointF(2, 10), QPointF(4, 10));
        lines(11, 18, {6, 10, 14});
        break;
    }
    case Pitch::Layout::Bullets:
        for (qreal y : {5.0, 10.0, 15.0}) { p.drawPoint(QPointF(3.5, y)); p.drawLine(QPointF(7, y), QPointF(17, y)); }
        break;
    case Pitch::Layout::Right:
        lines(3, 17, {5});
        lines(7, 17, {10, 15});
        break;
    case Pitch::Layout::Fan: {
        for (qreal y : {5.0, 10.0, 15.0}) {
            QPainterPath branch(QPointF(3, 10));
            branch.cubicTo(QPointF(7, 10), QPointF(7, y), QPointF(10, y));
            p.drawPath(branch);
            p.drawLine(QPointF(12, y), QPointF(17, y));
        }
        break;
    }
    case Pitch::Layout::Org:
        p.drawLine(QPointF(7, 4), QPointF(13, 4));
        p.drawLine(QPointF(10, 4), QPointF(10, 9));
        p.drawLine(QPointF(4, 9), QPointF(16, 9));
        for (qreal x : {4.0, 10.0, 16.0}) p.drawLine(QPointF(x, 9), QPointF(x, 14));
        lines(2.5, 5.5, {16}); lines(8.5, 11.5, {16}); lines(14.5, 17.5, {16});
        break;
    case Pitch::Layout::Fishbone:
        p.drawLine(QPointF(2, 10), QPointF(14, 10));  // spine
        p.drawLine(QPointF(15, 7), QPointF(18, 10)); p.drawLine(QPointF(15, 13), QPointF(18, 10));  // head
        for (qreal x : {6.0, 11.0}) { p.drawLine(QPointF(x + 2, 10), QPointF(x, 4)); p.drawLine(QPointF(x + 2, 10), QPointF(x, 16)); }
        break;
    case Pitch::Layout::Timeline:
        p.drawLine(QPointF(2, 10), QPointF(18, 10));
        for (qreal x : {5.0, 10.0, 15.0}) p.drawEllipse(QPointF(x, 10), 1.3, 1.3);
        p.drawLine(QPointF(5, 8), QPointF(5, 5)); p.drawLine(QPointF(10, 12), QPointF(10, 15)); p.drawLine(QPointF(15, 8), QPointF(15, 5));
        break;
    case Pitch::Layout::Grid:
        for (qreal x : {2.5, 10.5})
            for (qreal y : {3.5, 11.0}) p.drawRoundedRect(QRectF(x, y, 7, 5.5), 1.2, 1.2);
        break;
    case Pitch::Layout::Numbered: {
        QFont font = p.font();
        font.setPixelSize(5);
        font.setBold(true);
        p.setFont(font);
        for (int i = 0; i < 3; ++i) {
            p.drawText(QRectF(1, 2.5 + i * 5, 5, 5), Qt::AlignCenter, QString::number(i + 1));
            p.drawLine(QPointF(7.5, 5 + i * 5), QPointF(17, 5 + i * 5));
        }
        break;
    }
    case Pitch::Layout::Circle:
        p.drawEllipse(QPointF(10, 10), 2.5, 2.5);
        for (int i = 0; i < 5; ++i) {
            const qreal angle = -std::numbers::pi / 2 + 2 * std::numbers::pi * i / 5;
            p.drawEllipse(QPointF(10 + 7 * std::cos(angle), 10 + 7 * std::sin(angle)), 1.4, 1.4);
        }
        break;
    }
    p.restore();
}

void drawThemeCard(QPainter *painter, const QRectF &rect, const Pitch::Look &look, bool cover) {
    Pitch::Slide sample;
    sample.title = QStringLiteral("Pitch");
    sample.cover = cover;
    sample.number = 1;
    Pitch::paint(painter, rect, sample, look);
}

QStringList templateColorNames() {
    return {editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Background")), editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Text")),
            editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Titles")), editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Lines")),
            editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Accent")), editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Cover background")),
            editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Cover text")), editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Topic background")),
            editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Topic text"))};
}

QColor &templateColor(Pitch::Template &value, int index) { return value.*kColorMembers[std::clamp(index, 0, kTemplateColorCount - 1)]; }
QColor templateColor(const Pitch::Template &value, int index) { return value.*kColorMembers[std::clamp(index, 0, kTemplateColorCount - 1)]; }

QColor baseTemplateColor(const Pitch::Look &look, int index) {
    const QColor values[kTemplateColorCount] = {look.background, look.text, look.title.isValid() ? look.title : look.text, look.line,
                                                look.fill.isValid() ? look.fill : look.line,
                                                look.coverBackground.isValid() ? look.coverBackground : look.background,
                                                look.coverText.isValid() ? look.coverText : look.text,
                                                look.topicBackground.isValid() ? look.topicBackground : look.background,
                                                look.topicText.isValid() ? look.topicText : look.text};
    return values[std::clamp(index, 0, kTemplateColorCount - 1)];
}

QStringList templatePictureNames() {
    return {editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Slides")), editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Cover and topics")),
            editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Logo"))};
}

QByteArray &templatePicture(Pitch::Template &value, int index) {
    return value.*kPictureMembers[std::clamp(index, 0, kTemplatePictureCount - 1)];
}

const QByteArray &templatePicture(const Pitch::Template &value, int index) {
    return value.*kPictureMembers[std::clamp(index, 0, kTemplatePictureCount - 1)];
}

bool templatePictureIsLogo(int index) { return index == kTemplatePictureCount - 1; }

QStringList itemStyleNames() {
    return {editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Of the theme")), editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Text")),
            editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Pill")), editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Badge")),
            editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Label"))};
}

QStringList motifNames() {
    return {editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Of the theme")), editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "None")),
            editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Band")),         editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Clouds")),
            editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Smudge")),       editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Bars")),
            editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Halftone")),     editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Grain")),
            editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Lines")),        editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Blobs")),
            editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Rules")),        editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Frame"))};
}

QStringList cornerNames() {
    return {editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Top left")), editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Top right")),
            editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Bottom left")), editorText(QT_TRANSLATE_NOOP("PitchTemplateEditor", "Bottom right"))};
}

QList<int> logoSizes() {
    QList<int> sizes;
    for (int size = Pitch::kMinLogoSize; size <= Pitch::kMaxLogoSize; size += kLogoSizeStep) sizes.append(size);
    return sizes;
}

QString templateFileName(const QString &name) {
    QString file = name;
    for (QChar &c : file)
        if (QStringLiteral("\\/:*?\"<>|").contains(c)) c = QLatin1Char('_');
    return file + QLatin1Char('.') + QLatin1String(Pitch::kTemplateSuffix);
}

QList<TemplateCard> templateCards(const MindMapModel &model) {
    QList<TemplateCard> cards;
    const auto sheet = Pitch::sheetTemplate(model);
    const QList<Pitch::Template> library = PitchTemplates::all();
    const bool sheetOnly = sheet && std::none_of(library.cbegin(), library.cend(), [&](const Pitch::Template &t) { return t.id == sheet->id; });
    if (sheetOnly) cards.append({*sheet, true, translated(QT_TRANSLATE_NOOP("PitchPage", "%1 (this map)")).arg(sheet->name)});
    for (const Pitch::Template &value : library) cards.append({value, false, value.name});
    return cards;
}

}  // namespace PitchChoices
