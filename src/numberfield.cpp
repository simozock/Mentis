#include "numberfield.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpressionValidator>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kGlyphPx = 8;           // the − and + strokes
constexpr int kHoverInset = 3;        // the hover background inside the button
constexpr int kControlRadius = 4;     // highlights (ui-foundations.md)
constexpr int kRepeatDelayMs = 400;   // a held button: first repeat, then one every kRepeatMs
constexpr int kRepeatMs = 60;
constexpr double kBigStep = 10;       // Shift, Page Up/Down

}  // namespace

NumberField::NumberField(QWidget *parent) : QLineEdit(parent) {
    // The buttons take the side padding of the panels' style sheets; the number sits between them.
    setStyleSheet(QStringLiteral("QLineEdit { padding-left:0; padding-right:0; }"));
    setTextMargins(kButtonPx, 0, kButtonPx, 0);
    setAlignment(Qt::AlignCenter);
    setPlaceholderText(QStringLiteral("—"));
    setMouseTracking(true);
    m_repeat.setSingleShot(false);
    connect(&m_repeat, &QTimer::timeout, this, [this] {
        m_repeat.setInterval(kRepeatMs);
        if (m_pressed && canStep(m_pressed)) step(m_pressed);
        else m_repeat.stop();
    });
    connect(this, &QLineEdit::editingFinished, this, &NumberField::confirmText);
    updateValidator();
    showValue();
}

void NumberField::setRange(double minimum, double maximum) {
    m_minimum = minimum;
    m_maximum = std::max(minimum, maximum);
    m_value = std::clamp(m_value, m_minimum, m_maximum);
    updateValidator();
    showValue();
}

void NumberField::setDecimals(int decimals) {
    m_decimals = std::max(0, decimals);
    updateValidator();
    showValue();
}

void NumberField::setValue(double value) {
    m_value = std::clamp(rounded(value), m_minimum, m_maximum);
    m_hasValue = true;
    showValue();
}

void NumberField::clearValue() {
    m_hasValue = false;
    showValue();
}

QString NumberField::format(double value) const { return QString::number(rounded(value)); }

double NumberField::rounded(double value) const {
    const double scale = std::pow(10.0, m_decimals);
    return std::round(value * scale) / scale;
}

void NumberField::updateValidator() {
    // Digits only (as many as the limits have), a minus with a negative minimum, a comma or point
    // with decimals. Every partial number is accepted, so Enter and leaving always confirm.
    const int digits = int(std::max(QString::number(qint64(std::abs(m_minimum))).size(), QString::number(qint64(std::abs(m_maximum))).size()));
    QString pattern = QStringLiteral("\\s*%1\\d{0,%2}").arg(m_minimum < 0 ? QStringLiteral("-?") : QString()).arg(digits);
    if (m_decimals > 0) pattern += QStringLiteral("([.,]\\d{0,%1})?").arg(m_decimals);
    pattern += QStringLiteral("\\s*");
    delete validator();
    setValidator(new QRegularExpressionValidator(QRegularExpression(pattern), this));
}

void NumberField::showValue() {
    const QSignalBlocker blocker(this);
    setText(m_hasValue ? format(m_value) : QString());
    update();
}

void NumberField::confirmText() {
    const QString typed = text().trimmed();
    if (typed == (m_hasValue ? format(m_value) : QString())) return;  // nothing typed: no choice
    bool ok = false;
    const double number = QString(typed).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    if (!ok) {  // empty or a lone sign: the value of before
        showValue();
        return;
    }
    const double value = std::clamp(rounded(number), m_minimum, m_maximum);
    const bool changed = !m_hasValue || value != m_value;
    setValue(value);
    if (changed) emit valueChosen(value);
}

void NumberField::step(double steps) {
    if (!isEnabled() || isReadOnly()) return;
    if (!m_hasValue && m_stepsEachItem && text().trimmed().isEmpty()) {
        emit valueStepped(steps * m_step);
        return;
    }
    // From a number typed and not confirmed yet, else from the value shown (none: the minimum).
    bool typed = false;
    const double number = text().trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&typed);
    const double base = typed ? number : m_hasValue ? m_value : m_minimum;
    const double value = std::clamp(rounded(base + steps * m_step), m_minimum, m_maximum);
    const bool changed = !m_hasValue || value != m_value;
    setValue(value);
    if (changed) emit valueChosen(value);
}

QRect NumberField::minusRect() const { return {0, 0, kButtonPx, height()}; }

QRect NumberField::plusRect() const { return {width() - kButtonPx, 0, kButtonPx, height()}; }

int NumberField::buttonAt(const QPoint &pos) const {
    if (minusRect().contains(pos)) return -1;
    if (plusRect().contains(pos)) return 1;
    return 0;
}

bool NumberField::canStep(int direction) const {
    if (!isEnabled() || isReadOnly()) return false;
    if (!m_hasValue) return true;
    return direction < 0 ? m_value > m_minimum : m_value < m_maximum;
}

void NumberField::paintEvent(QPaintEvent *event) {
    QLineEdit::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor text = palette().color(QPalette::Text);
    for (const int direction : {-1, 1}) {
        const QRect area = direction < 0 ? minusRect() : plusRect();
        const bool enabled = canStep(direction);
        if (enabled && (m_hover == direction || m_pressed == direction)) {
            QColor hover = text;
            hover.setAlpha(m_pressed == direction ? 40 : 24);
            painter.setPen(Qt::NoPen);
            painter.setBrush(hover);
            painter.drawRoundedRect(QRectF(area).adjusted(kHoverInset, kHoverInset, -kHoverInset, -kHoverInset), kControlRadius, kControlRadius);
        }
        painter.setOpacity(enabled ? 1.0 : 0.4);
        painter.setPen(QPen(text, 1.5, Qt::SolidLine, Qt::RoundCap));
        const QPointF center = QRectF(area).center();
        painter.drawLine(center - QPointF(kGlyphPx / 2.0, 0), center + QPointF(kGlyphPx / 2.0, 0));
        if (direction > 0) painter.drawLine(center - QPointF(0, kGlyphPx / 2.0), center + QPointF(0, kGlyphPx / 2.0));
        painter.setOpacity(1.0);
    }
}

void NumberField::keyPressEvent(QKeyEvent *event) {
    const bool big = event->modifiers() & Qt::ShiftModifier;
    switch (event->key()) {
    case Qt::Key_Up: step(big ? kBigStep : 1); return;
    case Qt::Key_Down: step(big ? -kBigStep : -1); return;
    case Qt::Key_PageUp: step(kBigStep); return;
    case Qt::Key_PageDown: step(-kBigStep); return;
    case Qt::Key_Escape:
        // Esc puts the value back; with nothing typed it goes on (the Gantt panel closes).
        if (text().trimmed() != (m_hasValue ? format(m_value) : QString())) {
            showValue();
            return;
        }
        break;
    default: break;
    }
    QLineEdit::keyPressEvent(event);
}

void NumberField::mousePressEvent(QMouseEvent *event) {
    const int direction = event->button() == Qt::LeftButton ? buttonAt(event->position().toPoint()) : 0;
    if (!direction) {
        QLineEdit::mousePressEvent(event);
        return;
    }
    if (!canStep(direction)) return;
    m_pressed = direction;
    step(direction);
    m_repeat.start(kRepeatDelayMs);
    update();
}

void NumberField::mouseDoubleClickEvent(QMouseEvent *event) {
    // A fast second click on − or + is one more step, not a word selection.
    if (event->button() == Qt::LeftButton && buttonAt(event->position().toPoint())) mousePressEvent(event);
    else QLineEdit::mouseDoubleClickEvent(event);
}

void NumberField::mouseReleaseEvent(QMouseEvent *event) {
    if (m_pressed) {
        m_pressed = 0;
        m_repeat.stop();
        update();
        return;
    }
    QLineEdit::mouseReleaseEvent(event);
}

void NumberField::mouseMoveEvent(QMouseEvent *event) {
    const int hover = buttonAt(event->position().toPoint());
    if (hover != m_hover) {
        m_hover = hover;
        setCursor(hover ? Qt::PointingHandCursor : Qt::IBeamCursor);
        update();
    }
    if (!m_pressed) QLineEdit::mouseMoveEvent(event);
}

void NumberField::leaveEvent(QEvent *event) {
    m_hover = 0;
    update();
    QLineEdit::leaveEvent(event);
}

void NumberField::hideEvent(QHideEvent *event) {
    m_pressed = 0;
    m_repeat.stop();
    QLineEdit::hideEvent(event);
}

void NumberField::changeEvent(QEvent *event) {
    if (event->type() == QEvent::EnabledChange && !isEnabled()) {
        m_pressed = m_hover = 0;
        m_repeat.stop();
    }
    QLineEdit::changeEvent(event);
}
