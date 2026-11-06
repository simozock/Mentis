#include "segmentedcontrol.h"
#include "uitheme.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QToolButton>

namespace {
// Misure e colori dalla scala e dalla tabella di AGENTS.md: binario raggio 8 (superficie) con
// padding 4, segmenti raggio 4 (8 - 4, raggi concentrici), testo 12 px. Colori dei menu scuri:
// fondo surface, bordo divider, segmento attivo surfaceRaised, hover divider, testo text; il
// contorno di focus usa l'accento #7047eb.
constexpr int kPadding = 4;
constexpr int kSegmentHeight = 24;
constexpr qreal kTrackRadius = 8;
}

SegmentedControl::SegmentedControl(const QStringList &labels, QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    // Focus solo da tastiera: un clic su un segmento cambia pagina senza togliere il focus alla
    // vista che lo aveva (nella mappa Spazio modifica il nodo e le frecce scorrono la vista).
    // I segmenti non prendono il focus.
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(kPadding, kPadding, kPadding, kPadding);
    layout->setSpacing(2);
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    connect(m_group, &QButtonGroup::idClicked, this, [this](int index) { activate(index); });
    setStyleSheet(UiTheme::qss(QStringLiteral(R"(
        QToolButton { background:transparent; border:0; border-radius:4px; color:@text;
                      font-size:12px; font-weight:400; padding:0 12px; }
        QToolButton:hover { background:@divider; }
        QToolButton:checked { background:@surfaceRaised; font-weight:600; }
        QToolButton:disabled { color:@textMuted; background:transparent; }
    )")));
    fitHeight();
    for (const QString &label : labels) addSegment(label);
    // FEAT-035 part B: the segments grow with the text, like the other controls.
    connect(UiTheme::AppearanceNotifier::instance(), &UiTheme::AppearanceNotifier::changed, this, &SegmentedControl::fitHeight);
}

void SegmentedControl::fitHeight() {
    // The track as tall as a combo box beside it: its 30 px min-height inside a 1 px border (32 px
    // today), scaled the same way; the segments inside the track's padding.
    const int track = UiTheme::heightPx(UiTheme::space(kSegmentHeight + 2 * kPadding - 2)) + 2;
    for (QToolButton *segment : std::as_const(m_segments)) segment->setFixedHeight(track - 2 * kPadding);
    setFixedHeight(track);
}

int SegmentedControl::addSegment(const QString &label) {
    auto *segment = new QToolButton(this);
    segment->setText(label);
    segment->setCheckable(true);
    segment->setFocusPolicy(Qt::NoFocus);
    segment->setFixedHeight(height() - 2 * kPadding);
    segment->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    segment->setAccessibleName(label);
    const int index = int(m_segments.size());
    m_group->addButton(segment, index);
    layout()->addWidget(segment);
    m_segments.append(segment);
    if (index == 0) segment->setChecked(true);
    return index;
}

void SegmentedControl::setColors(const Colors &c) {
    m_track = c.track;
    m_border = c.border;
    const auto rgba = [](const QColor &color) {
        return QStringLiteral("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
    };
    setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:0;border-radius:4px;color:%1;font-size:12px;font-weight:400;padding:0 12px;}"
        "QToolButton:hover{background:%2;}QToolButton:checked{background:%3;font-weight:600;}"
        "QToolButton:disabled{color:%4;background:transparent;}")
        .arg(rgba(c.text), rgba(c.hover), rgba(c.active), rgba(c.muted)));
    update();
}

int SegmentedControl::count() const { return int(m_segments.size()); }

int SegmentedControl::currentIndex() const { return m_group->checkedId(); }

void SegmentedControl::setCurrentIndex(int index) {
    if (index < 0 || index >= m_segments.size()) return;
    m_segments[index]->setChecked(true);
}

void SegmentedControl::setSegmentEnabled(int index, bool enabled) {
    if (index >= 0 && index < m_segments.size()) m_segments[index]->setEnabled(enabled);
}

void SegmentedControl::activate(int index) {
    if (index < 0 || index >= m_segments.size() || !m_segments[index]->isEnabled()) return;
    setCurrentIndex(index);
    emit segmentActivated(index);
}

void SegmentedControl::keyPressEvent(QKeyEvent *event) {
    const int current = currentIndex();
    // The nearest enabled segment from `from` towards `step` (the current one if there is none).
    const auto enabledFrom = [this, current](int from, int step) {
        for (int i = from; i >= 0 && i < count(); i += step)
            if (m_segments[i]->isEnabled()) return i;
        return current;
    };
    switch (event->key()) {
    case Qt::Key_Left: activate(enabledFrom(current - 1, -1)); return;
    case Qt::Key_Right: activate(enabledFrom(current + 1, 1)); return;
    case Qt::Key_Home: activate(enabledFrom(0, 1)); return;
    case Qt::Key_End: activate(enabledFrom(count() - 1, -1)); return;
    default: QWidget::keyPressEvent(event);
    }
}

void SegmentedControl::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(m_track);
    painter.setPen(QPen(m_keyboardFocus ? QColor(0x70, 0x47, 0xeb) : m_border, 1));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kTrackRadius, kTrackRadius);
}

// Contorno visibile solo con focus arrivato da tastiera (Tab/Maiusc+Tab), non quando Qt assegna
// il focus da solo, per esempio attivando una finestra in cui nessun widget lo aveva.
void SegmentedControl::focusInEvent(QFocusEvent *event) {
    QWidget::focusInEvent(event);
    m_keyboardFocus = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason;
    update();
}

void SegmentedControl::focusOutEvent(QFocusEvent *event) {
    QWidget::focusOutEvent(event);
    m_keyboardFocus = false;
    update();
}
