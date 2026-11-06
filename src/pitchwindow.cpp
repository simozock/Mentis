#include "pitchwindow.h"

#include "performance.h"

#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QVariantAnimation>

#include <algorithm>

PitchWindow::PitchWindow(const QList<Pitch::Slide> &slides, const Pitch::Look &look, Pitch::Aspect aspect, QScreen *screen)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint), m_slides(slides), m_look(look), m_aspect(aspect) {
    setObjectName(QStringLiteral("PitchWindow"));
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::BlankCursor);
    setFocusPolicy(Qt::StrongFocus);
    if (screen) {
        setScreen(screen);
        setGeometry(screen->geometry());
    }
    m_animation = new QVariantAnimation(this);
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        if (!m_before.isNull()) m_slideFade = value.toReal();
        else m_itemFade = value.toReal();
        update();
    });
    connect(m_animation, &QVariantAnimation::finished, this, [this] {
        m_before = QPixmap();
        m_slideFade = m_itemFade = 1;
        update();
    });
}

QRectF PitchWindow::slideRect() const {
    const qreal ratio = Pitch::aspectRatio(m_aspect, height() > 0 ? qreal(width()) / height() : 0);
    const QSizeF size = QSizeF(ratio, 1).scaled(QSizeF(this->size()), Qt::KeepAspectRatio);
    return {QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size};
}

void PitchWindow::paintSlide(QPainter *painter, qreal newest) const {
    if (m_slides.isEmpty()) {
        painter->fillRect(rect(), m_look.background);
        return;
    }
    // The bands beside a slide of another ratio, in the slide's ground (a theme's cover may differ).
    painter->fillRect(rect(), Pitch::lookOn(m_look, m_slides[m_slide]).background);
    // A slide without steps ("all at once", a topic slide) shows every item at once (BUG-032).
    const Pitch::Slide &slide = m_slides[m_slide];
    Pitch::paint(painter, slideRect(), slide, m_look, slide.steps() > 0 ? m_shown : -1, newest);
}

void PitchWindow::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    paintSlide(&painter, m_itemFade);
    if (!m_before.isNull()) {
        painter.setOpacity(1 - m_slideFade);
        painter.drawPixmap(0, 0, m_before);
    }
}

void PitchWindow::startItemFade() {
    if (!Performance::animationsEnabled() || !isVisible()) { update(); return; }
    m_animation->stop();
    m_before = QPixmap();
    m_itemFade = 0;
    m_animation->setDuration(kItemFadeMs);
    m_animation->start();
}

void PitchWindow::next() {
    if (m_slides.isEmpty()) return;
    if (m_shown < m_slides[m_slide].steps()) {
        ++m_shown;
        startItemFade();
        return;
    }
    if (m_slide + 1 < m_slides.size()) goTo(m_slide + 1);
}

void PitchWindow::previous() {
    if (m_slides.isEmpty()) return;
    if (m_shown > 0) {
        --m_shown;
        m_itemFade = 1;
        update();
        return;
    }
    if (m_slide > 0) goTo(m_slide - 1, true);
}

void PitchWindow::goTo(int slide, bool allItems) {
    if (m_slides.isEmpty()) return;
    slide = std::clamp(slide, 0, int(m_slides.size()) - 1);
    m_animation->stop();
    m_before = QPixmap();
    const bool fade = isVisible() && Performance::animationsEnabled();
    if (fade) m_before = grab();  // what is on screen now, faded out over the new slide
    m_slide = slide;
    m_shown = allItems ? m_slides[slide].steps() : 0;
    m_itemFade = 1;
    m_slideFade = fade ? 0 : 1;
    if (fade) {
        m_animation->setDuration(kSlideFadeMs);
        m_animation->start();
    }
    update();
}

void PitchWindow::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_Right: case Qt::Key_Down: case Qt::Key_Space: case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_PageDown:
        next();
        return;
    case Qt::Key_Left: case Qt::Key_Up: case Qt::Key_Backspace: case Qt::Key_PageUp:
        previous();
        return;
    case Qt::Key_Home: goTo(0); return;
    case Qt::Key_End: goTo(int(m_slides.size()) - 1, true); return;
    case Qt::Key_Escape: {
        // Gone from the screen first, then the map is told (user's report 2026-10-26: telling it while
        // still full screen let the presentation come back on the next mouse move).
        m_animation->stop();
        const QString topic = m_slides.isEmpty() ? QString() : m_slides[m_slide].topicId;
        hide();
        close();
        emit finished(topic);
        return;
    }
    default: QWidget::keyPressEvent(event);
    }
}

void PitchWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) next();
}
