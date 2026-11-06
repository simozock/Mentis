#include "datepicker.h"
#include "localization.h"
#include "uitheme.h"

#include <QGuiApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QWheelEvent>

#include <algorithm>

namespace {

constexpr int kColumns = 7;
constexpr int kRows = 6;              // every month fits in six weeks
constexpr int kRadius = 8;            // surfaces (ui-foundations.md)
constexpr int kControlRadius = 4;     // highlights
constexpr int kGap = 8;               // between header, weekdays, grid and footer
constexpr int kChevronPx = 16;

QString capitalized(const QString &text) { return text.isEmpty() ? text : text.left(1).toUpper() + text.mid(1); }

// The Hugeicons arrow of the fields, tinted and turned (0 = down, 90 = left, -90 = right).
QPixmap arrow(const QColor &color, int degrees) {
    QPixmap pixmap = QIcon(QStringLiteral(":/assets/hugeicons/arrow-down.svg")).pixmap(kChevronPx * 2, kChevronPx * 2);
    QPainter tint(&pixmap);
    tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tint.fillRect(pixmap.rect(), color);
    tint.end();
    return degrees ? pixmap.transformed(QTransform().rotate(degrees), Qt::SmoothTransformation) : pixmap;
}

}  // namespace

QLocale DatePicker::namesLocale() { return QLocale(Localization::effectiveLanguage(Localization::preference())); }

DatePicker::DatePicker(QWidget *parent, const Colors &colors)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint), m_colors(colors) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(2 * kPadding + kColumns * kCell + 2 * kShadowSide,
                 2 * kPadding + kHeaderHeight + kGap + kWeekdayHeight + kRows * kCell + kGap + kFooterHeight + kShadowTop + kShadowBottom);
    setDate({});
}

void DatePicker::setRange(const QDate &minimum, const QDate &maximum) {
    m_minimum = minimum;
    m_maximum = maximum;
    update();
}

void DatePicker::setDate(const QDate &date) {
    m_date = date;
    m_focus = date.isValid() ? date : QDate::currentDate();
    showMonth(m_focus);
}

QString DatePicker::monthTitle() const {
    return capitalized(m_names.standaloneMonthName(m_month.month())) + u' ' + QString::number(m_month.year());
}

QString DatePicker::weekdayName(int day) const { return capitalized(m_names.standaloneDayName(day, QLocale::ShortFormat).left(2)); }

void DatePicker::showMonth(const QDate &anyDay) {
    m_month = QDate(anyDay.year(), anyDay.month(), 1);
    update();
}

void DatePicker::popup(const QRect &globalAnchor) {
    const QScreen *screen = QGuiApplication::screenAt(globalAnchor.center());
    const QRect available = screen ? screen->availableGeometry() : QRect(globalAnchor.topLeft(), size());
    // The card's left edge on the field's, 4 px below it (the shadow's margin is outside the card).
    QPoint pos(globalAnchor.left() - kShadowSide, globalAnchor.bottom() + 1 + 4 - kShadowTop);
    if (pos.y() + height() > available.bottom() && globalAnchor.top() - 4 - height() + kShadowBottom >= available.top())
        pos.setY(globalAnchor.top() - 4 - height() + kShadowBottom);
    pos.setX(std::clamp(pos.x(), available.left(), std::max(available.left(), available.right() + 1 - width())));
    move(pos);
    show();
    setFocus(Qt::PopupFocusReason);
}

QRect DatePicker::box() const { return rect().adjusted(kShadowSide, kShadowTop, -kShadowSide, -kShadowBottom); }

QRect DatePicker::previousRect() const {
    const QRect card = box();
    return {card.right() + 1 - kPadding - 2 * kHeaderHeight, card.top() + kPadding, kHeaderHeight, kHeaderHeight};
}

QRect DatePicker::nextRect() const { return previousRect().translated(kHeaderHeight, 0); }

QRect DatePicker::todayRect() const {
    const QRect card = box();
    const int width = fontMetrics().horizontalAdvance(tr("Today")) + 2 * kGap;
    return {card.left() + kPadding - kGap, card.bottom() + 1 - kPadding - kFooterHeight, width, kFooterHeight};
}

QDate DatePicker::firstShown() const {
    // The week starts on the locale's first day (Monday in Italy, Sunday in the US).
    const int offset = (m_month.dayOfWeek() - int(QLocale().firstDayOfWeek()) + kColumns) % kColumns;
    return m_month.addDays(-offset);
}

QRect DatePicker::dayRect(const QDate &date) const {
    const qint64 index = firstShown().daysTo(date);
    if (index < 0 || index >= kColumns * kRows) return {};
    const QRect card = box();
    const int top = card.top() + kPadding + kHeaderHeight + kGap + kWeekdayHeight;
    return {card.left() + kPadding + int(index % kColumns) * kCell, top + int(index / kColumns) * kCell, kCell, kCell};
}

QDate DatePicker::dayAt(const QPoint &pos) const {
    const QRect card = box();
    const QRect grid(card.left() + kPadding, card.top() + kPadding + kHeaderHeight + kGap + kWeekdayHeight, kColumns * kCell, kRows * kCell);
    if (!grid.contains(pos)) return {};
    return firstShown().addDays((pos.y() - grid.top()) / kCell * kColumns + (pos.x() - grid.left()) / kCell);
}

bool DatePicker::selectable(const QDate &date) const {
    return date.isValid() && (!m_minimum.isValid() || date >= m_minimum) && (!m_maximum.isValid() || date <= m_maximum);
}

void DatePicker::moveFocus(const QDate &date) {
    if (!selectable(date)) return;
    m_focus = date;
    if (date.year() != m_month.year() || date.month() != m_month.month()) showMonth(date);
    update();
}

void DatePicker::pick(const QDate &date) {
    if (!selectable(date)) return;
    m_date = date;
    emit picked(date);
    close();
}

void DatePicker::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = box();
    // Shadow as FloatingGroup's: four wider, fainter outlines, outside the card only.
    QPainterPath outside;
    outside.addRect(QRectF(rect()));
    QPainterPath cardPath;
    cardPath.addRoundedRect(card, kRadius, kRadius);
    painter.save();
    painter.setClipPath(outside.subtracted(cardPath));
    painter.setPen(Qt::NoPen);
    static constexpr int kAlpha[] = {4, 3, 2, 1};
    for (int step = 0; step < 4; ++step) {
        const qreal grow = step + 1;
        painter.setBrush(QColor(15, 23, 42, kAlpha[step] * 2));  // twice the toolbar's: the card floats over a card
        painter.drawRoundedRect(card.adjusted(-grow, -grow + 1, grow, grow + 1), kRadius + grow, kRadius + grow);
    }
    painter.restore();
    painter.setBrush(m_colors.card);
    painter.setPen(QPen(m_colors.line, 1));
    painter.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);

    QFont font = this->font();
    font.setPixelSize(UiTheme::fontPx(14));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(m_colors.text);
    const QRect header(int(card.left()) + kPadding, int(card.top()) + kPadding, kColumns * kCell, kHeaderHeight);
    painter.drawText(header, Qt::AlignLeft | Qt::AlignVCenter, monthTitle());
    // Month arrows, as the panel's chevrons; a hover background like the panel's buttons.
    const auto drawArrow = [&](const QRect &area, int degrees, bool enabled) {
        if (enabled && area.contains(m_hover)) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_colors.hover);
            painter.drawRoundedRect(QRectF(area).adjusted(2, 2, -2, -2), kControlRadius, kControlRadius);
        }
        painter.setOpacity(enabled ? 1.0 : 0.4);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(QRect(area.center() - QPoint(kChevronPx / 2 - 1, kChevronPx / 2 - 1), QSize(kChevronPx, kChevronPx)),
                           arrow(m_colors.text, degrees));
        painter.setOpacity(1.0);
    };
    drawArrow(previousRect(), 90, selectable(m_month.addDays(-1)));
    drawArrow(nextRect(), -90, selectable(m_month.addMonths(1)));

    font.setPixelSize(UiTheme::fontPx(12));
    font.setWeight(QFont::Normal);
    painter.setFont(font);
    painter.setPen(m_colors.muted);
    const int weekdayTop = header.bottom() + 1 + kGap;
    for (int column = 0; column < kColumns; ++column) {
        const int day = (int(QLocale().firstDayOfWeek()) - 1 + column) % kColumns + 1;
        painter.drawText(QRect(header.left() + column * kCell, weekdayTop, kCell, kWeekdayHeight), Qt::AlignCenter, weekdayName(day));
    }

    const QDate today = QDate::currentDate();
    const QDate hovered = dayAt(m_hover);
    const QFont plain = font;
    QFont bold = font;
    bold.setWeight(QFont::DemiBold);
    for (QDate day = firstShown(), last = firstShown().addDays(kColumns * kRows - 1); day <= last; day = day.addDays(1)) {
        const QRectF cell = QRectF(dayRect(day)).adjusted(2, 2, -2, -2);
        const bool enabled = selectable(day);
        const bool inMonth = day.month() == m_month.month();
        painter.setPen(Qt::NoPen);
        if (day == m_date) {
            painter.setBrush(m_colors.accent);
            painter.drawRoundedRect(cell, kControlRadius, kControlRadius);
        } else if (enabled && (day == hovered || (day == m_focus && hasFocus() && !hovered.isValid()))) {
            painter.setBrush(m_colors.hover);
            painter.drawRoundedRect(cell, kControlRadius, kControlRadius);
        }
        if (day == today && day != m_date) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(m_colors.accent, 1));
            painter.drawRoundedRect(cell.adjusted(0.5, 0.5, -0.5, -0.5), kControlRadius, kControlRadius);
        }
        painter.setFont(day == m_date || day == today ? bold : plain);
        painter.setPen(day == m_date ? QColor(Qt::white) : day == today ? m_colors.accent : inMonth ? m_colors.text : m_colors.muted);
        painter.setOpacity(enabled ? 1.0 : 0.4);
        painter.drawText(cell, Qt::AlignCenter, QString::number(day.day()));
        painter.setOpacity(1.0);
    }

    // Footer: a divider, then Today as a text button in the accent colour.
    const QRect todayArea = todayRect();
    painter.setPen(QPen(m_colors.line, 1));
    const qreal divider = todayArea.top() - kGap / 2.0 - 0.5;
    painter.drawLine(QPointF(card.left() + 1, divider), QPointF(card.right() - 1, divider));
    if (todayArea.contains(m_hover) && selectable(today)) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_colors.hover);
        painter.drawRoundedRect(QRectF(todayArea).adjusted(0, 2, 0, -2), kControlRadius, kControlRadius);
    }
    painter.setFont(bold);
    painter.setPen(m_colors.accent);
    painter.setOpacity(selectable(today) ? 1.0 : 0.4);
    painter.drawText(todayArea, Qt::AlignCenter, tr("Today"));
}

void DatePicker::mouseMoveEvent(QMouseEvent *event) {
    m_hover = event->position().toPoint();
    const bool pointer = selectable(dayAt(m_hover)) || previousRect().contains(m_hover) || nextRect().contains(m_hover)
                         || todayRect().contains(m_hover);
    setCursor(pointer ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

void DatePicker::leaveEvent(QEvent *) {
    m_hover = {-1, -1};
    update();
}

void DatePicker::mousePressEvent(QMouseEvent *event) {
    const QPoint pos = event->position().toPoint();
    if (!rect().contains(pos)) {
        QWidget::mousePressEvent(event);  // a click outside closes the popup without a choice
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    if (previousRect().contains(pos)) stepMonth(-1);
    else if (nextRect().contains(pos)) stepMonth(1);
    else if (todayRect().contains(pos)) pick(QDate::currentDate());
    else if (const QDate day = dayAt(pos); day.isValid()) pick(day);
}

void DatePicker::wheelEvent(QWheelEvent *event) {
    if (const int delta = event->angleDelta().y(); delta != 0) stepMonth(delta > 0 ? -1 : 1);
}

void DatePicker::stepMonth(int months) {
    // The keys' day goes along (same day, clamped to the range), so the keys go on from the month shown.
    const QDate first = m_month.addMonths(months);
    if (!selectable(months < 0 ? first.addMonths(1).addDays(-1) : first)) return;
    QDate focus = m_focus.addMonths(months);
    if (m_minimum.isValid()) focus = std::max(focus, m_minimum);
    if (m_maximum.isValid()) focus = std::min(focus, m_maximum);
    m_focus = focus;
    showMonth(first);
}

void DatePicker::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_Left: moveFocus(m_focus.addDays(-1)); break;
    case Qt::Key_Right: moveFocus(m_focus.addDays(1)); break;
    case Qt::Key_Up: moveFocus(m_focus.addDays(-kColumns)); break;
    case Qt::Key_Down: moveFocus(m_focus.addDays(kColumns)); break;
    case Qt::Key_PageUp: stepMonth(-1); break;
    case Qt::Key_PageDown: stepMonth(1); break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space: pick(m_focus); break;
    case Qt::Key_Escape: close(); break;
    default: QWidget::keyPressEvent(event); return;
    }
    m_hover = {-1, -1};  // the keys' day is shown, not the one under a still mouse
    update();
}

// ---------------------------------------------------------------------------------------------

DateField::DateField(QWidget *parent) : QDateEdit(parent) {
    // Qt's popup mode keeps the drop-down space for the chevron (the clicks and keys that would open
    // Qt's calendar open DatePicker); without spin buttons a style sheet shrinks the text to 1 px.
    setCalendarPopup(true);
}

void DateField::setPickerColors(const DatePicker::Colors &colors) { m_colors = colors; }

void DateField::setChevronColor(const QColor &color) {
    m_chevron = color;
    update();
}

void DateField::openPicker() {
    if (picker() || !isEnabled() || isReadOnly()) return;  // a closed one deletes itself later
    m_picker = new DatePicker(this, m_colors);
    m_picker->setRange(minimumDate(), maximumDate());
    m_picker->setDate(date());
    connect(m_picker, &DatePicker::picked, this, [this](const QDate &day) {
        setDate(day);
        emit picked(day);
    });
    m_picker->popup(QRect(mapToGlobal(QPoint(0, 0)), size()));
}

void DateField::paintChevron(QWidget *field, const QColor &color) {
    QPainter painter(field);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setOpacity(field->isEnabled() ? 1.0 : 0.4);
    painter.drawPixmap(QRect(field->width() - 8 - kChevronPx, (field->height() - kChevronPx) / 2, kChevronPx, kChevronPx), arrow(color, 0));
}

void DateField::paintEvent(QPaintEvent *event) {
    QDateEdit::paintEvent(event);
    paintChevron(this, m_chevron.isValid() ? m_chevron : palette().color(QPalette::Text));
}

void DateField::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && event->position().x() >= width() - 8 - kChevronPx - 4) {
        openPicker();
        return;
    }
    QDateEdit::mousePressEvent(event);
}

void DateField::keyPressEvent(QKeyEvent *event) {
    // The keys that open QDateEdit's own calendar.
    const bool altArrow = (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) && event->modifiers() & Qt::AltModifier;
    if (event->key() == Qt::Key_F4 || event->key() == Qt::Key_Select || altArrow) {
        openPicker();
        return;
    }
    QDateEdit::keyPressEvent(event);
}
