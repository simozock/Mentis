#pragma once

// FEAT-063 stage A: the presentation, full screen on the screen given. Forward: Right, Down, Space,
// Enter, Page Down, click; back: Left, Up, Backspace, Page Up; Home / End: first / last slide; Esc
// closes. Standard profile: slides cross-fade in 300 ms, items fade in over 200 ms; Light profile:
// no animation.

#include "pitch.h"

#include <QPixmap>
#include <QWidget>

class QScreen;
class QVariantAnimation;

class PitchWindow final : public QWidget {
    Q_OBJECT
public:
    PitchWindow(const QList<Pitch::Slide> &slides, const Pitch::Look &look, Pitch::Aspect aspect, QScreen *screen);

    // Where the presentation is: slide and items shown on it.
    int slideIndex() const { return m_slide; }
    int shownItems() const { return m_shown; }
    void next();
    void previous();
    void goTo(int slide, bool allItems = false);

    static constexpr int kSlideFadeMs = 300;
    static constexpr int kItemFadeMs = 200;

signals:
    // Closed with Esc: the topic of the last slide shown.
    void finished(const QString &topicId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRectF slideRect() const;
    void paintSlide(QPainter *painter, qreal newest) const;
    void startItemFade();

    QList<Pitch::Slide> m_slides;
    Pitch::Look m_look;
    Pitch::Aspect m_aspect;
    int m_slide = 0;
    int m_shown = 0;
    QPixmap m_before;      // the screen before a slide change, faded out over the new slide
    qreal m_slideFade = 1;  // 0..1, 1 = the new slide only
    qreal m_itemFade = 1;   // opacity of the newest item
    QVariantAnimation *m_animation = nullptr;
};
