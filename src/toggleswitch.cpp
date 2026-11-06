#include "toggleswitch.h"

#include "uitheme.h"

#include <QPainter>

namespace {
// Spazio attorno alla pillola per l'anello del focus.
constexpr int kMarginPx = 2;
}

ToggleSwitch::ToggleSwitch(QWidget *parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(sizeHint());
}

QSize ToggleSwitch::sizeHint() const { return {kTrackWidthPx + 2 * kMarginPx, kTrackHeightPx + 2 * kMarginPx}; }

void ToggleSwitch::paintEvent(QPaintEvent *) {
    using UiTheme::Token;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF track(kMarginPx, kMarginPx, kTrackWidthPx, kTrackHeightPx);
    const qreal radius = kTrackHeightPx / 2.0;
    if (hasFocus()) {
        p.setPen(QPen(UiTheme::color(Token::Accent), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(track.adjusted(-1.5, -1.5, 1.5, 1.5), radius + 1.5, radius + 1.5);
    }
    // On = light grey track (FEAT-072 B7, live: #878c92); the accent stays for focus.
    QColor fill = isChecked() ? QColor(kOnTrack) : (offTrack.isValid() ? offTrack : UiTheme::color(Token::SurfaceRaised));
    if (!isEnabled()) fill.setAlphaF(0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(track, radius, radius);
    const qreal inset = (kTrackHeightPx - kKnobPx) / 2.0;
    const qreal x = isChecked() ? track.right() - inset - kKnobPx : track.left() + inset;
    p.setBrush(isEnabled() ? UiTheme::color(Token::Text) : UiTheme::color(Token::TextMuted));
    p.drawEllipse(QRectF(x, track.top() + inset, kKnobPx, kKnobPx));
}
