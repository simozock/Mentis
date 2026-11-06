#include "tour.h"

#include "help.h"
#include "mindmapmodel.h"
#include "uitheme.h"

#include <QApplication>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTextDocument>
#include <QtMath>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kFacePx = 72;      // the mascot beside the text
constexpr int kTextWidthPx = 280;
constexpr int kPaddingPx = 16;
constexpr int kRadiusPx = 8;
constexpr int kArrowPx = 10;     // the arrow towards the control, outside the surface
constexpr int kGapPx = 8;        // between the control (its hole) and the arrow's tip
constexpr int kHoleMarginPx = 6;
constexpr int kArrowLinePx = 3;  // the arrows towards where to click
constexpr int kArrowHeadPx = 12;
constexpr qreal kMinArrowPx = 64;  // shorter arrows would hide under their head
constexpr int kJoinPx = 24;      // controls closer than this share a hole,
constexpr int kJoinAreaPx = 12000;  // when that leaves little empty space between them
constexpr int kRoomPx = 48;      // the bubble keeps this far from the other holes: room for an arrow
}  // namespace

QPixmap Mascot::face(Face face, int sizePx) {
    static const char *const names[] = {"smile", "hello", "look", "think", "cheer"};
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(QStringLiteral(":/assets/mascot/brain-%1.png").arg(QLatin1String(names[static_cast<int>(face)])));
    pixmap = pixmap.scaled(QSize(sizePx, sizePx) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}

bool TourSettings::tourOffered() { return QSettings().value(QStringLiteral("tour/offered"), false).toBool(); }
void TourSettings::setTourOffered() { QSettings().setValue(QStringLiteral("tour/offered"), true); }
bool TourSettings::tipsEnabled() { return QSettings().value(QStringLiteral("tour/tips"), true).toBool(); }
void TourSettings::setTipsEnabled(bool on) { QSettings().setValue(QStringLiteral("tour/tips"), on); }
bool TourSettings::tipSeen(const QString &key) { return QSettings().value(QStringLiteral("tour/seen/") + key, false).toBool(); }
void TourSettings::setTipSeen(const QString &key) { QSettings().setValue(QStringLiteral("tour/seen/") + key, true); }

// --- Veil ---

TourVeil::TourVeil(QWidget *window) : QWidget(window) {
    setObjectName(QStringLiteral("TourVeil"));
    setAttribute(Qt::WA_NoSystemBackground);
    setGeometry(window->rect());
    updateMask();
}

void TourVeil::setOpenings(const QList<QRect> &openings) {
    if (openings == m_openings) return;
    m_openings = openings;
    updateMask();
}

// The veil gets the mouse only inside its mask: the openings, with the holes' rounded margin, are cut
// out of it and the mouse there reaches the controls under the veil.
void TourVeil::updateMask() {
    QRegion region(rect());
    for (const QRect &opening : m_openings) {
        QPainterPath cut;
        cut.addRoundedRect(QRectF(opening.adjusted(-kHoleMarginPx, -kHoleMarginPx, kHoleMarginPx, kHoleMarginPx)), kRadiusPx, kRadiusPx);
        region -= QRegion(cut.toFillPolygon().toPolygon());
    }
    setMask(region);
}

QWidget *TourVeil::below(const QPoint &pos) const {
    // childAt skips the widgets transparent for the mouse: the veil is so only for this lookup.
    auto *self = const_cast<TourVeil *>(this);
    self->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QWidget *under = parentWidget()->childAt(pos);
    self->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    return under;
}

// A press on the window's drag area (the top bar between its buttons) goes on to it: the window
// moves, and a double-click maximizes it, as without the tour.
void TourVeil::passToDragArea(QMouseEvent *event) {
    event->accept();
    const QPoint pos = mapToParent(event->position().toPoint());
    QWidget *under = below(pos);
    if (!under || !under->property("windowDragArea").toBool()) return;
    QMouseEvent passed(event->type(), under->mapFrom(parentWidget(), pos), event->scenePosition(), event->globalPosition(),
                       event->button(), event->buttons(), event->modifiers());
    QCoreApplication::sendEvent(under, &passed);
}

void TourVeil::resizeEvent(QResizeEvent *) { updateMask(); }
void TourVeil::mousePressEvent(QMouseEvent *event) { passToDragArea(event); }
void TourVeil::mouseReleaseEvent(QMouseEvent *event) { event->accept(); }
void TourVeil::mouseDoubleClickEvent(QMouseEvent *event) { passToDragArea(event); }
void TourVeil::mouseMoveEvent(QMouseEvent *event) { event->accept(); }
void TourVeil::wheelEvent(QWheelEvent *event) { event->accept(); }
void TourVeil::contextMenuEvent(QContextMenuEvent *event) { event->accept(); }

void TourVeil::setHoles(const QList<QRect> &holes) {
    if (holes == m_holes) return;
    m_holes = holes;
    update();
}

QRect TourVeil::hole() const {
    QRect all;
    for (const QRect &hole : m_holes) all |= hole;
    return all;
}

void TourVeil::setArrows(const QList<QLineF> &arrows) {
    if (arrows == m_arrows) return;
    m_arrows = arrows;
    update();
}

void TourVeil::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath veil;
    veil.addRect(rect());
    for (const QRect &hole : m_holes) {
        QPainterPath cut;
        cut.addRoundedRect(QRectF(hole.adjusted(-kHoleMarginPx, -kHoleMarginPx, kHoleMarginPx, kHoleMarginPx)), kRadiusPx, kRadiusPx);
        veil = veil.subtracted(cut);
    }
    painter.fillPath(veil, UiTheme::color(UiTheme::Token::Scrim));
    // The arrows: a gentle curve in the accent colour, its head on the control.
    const QColor accent = UiTheme::color(UiTheme::Token::Accent);
    painter.setPen(QPen(accent, kArrowLinePx, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const QLineF &arrow : m_arrows) {
        const QLineF normal = arrow.normalVector().unitVector();
        const QPointF control = arrow.center() + (normal.p2() - normal.p1()) * arrow.length() * 0.2;
        QPainterPath curve(arrow.p1());
        curve.quadTo(control, arrow.p2());
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(curve);
        QLineF back(arrow.p2(), control);
        back.setLength(kArrowHeadPx);
        QLineF left = back, right = back;
        left.setAngle(back.angle() + 28);
        right.setAngle(back.angle() - 28);
        painter.setBrush(accent);
        painter.drawPolygon(QPolygonF({arrow.p2(), left.p2(), right.p2()}));
    }
}

// --- Bubble ---

MascotBubble::MascotBubble(QWidget *window) : QWidget(window) {
    setObjectName(QStringLiteral("MascotBubble"));
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(UiTheme::qss(QStringLiteral(
        "QLabel { color:@text; font-size:14px; background:transparent; }"
        "QPushButton { min-height:32px; max-height:32px; border-radius:4px; padding:0 16px; font-size:14px; }"
        "QPushButton#BubbleSecondary { background:@surfaceRaised; color:@text; border:1px solid @divider; }"
        "QPushButton#BubbleSecondary:hover { background:@divider; }"
        "QPushButton#BubblePrimary { background:@accent; color:@text; border:0; font-weight:600; }"
        "QPushButton:focus { outline:none; }")));
    auto *outer = new QHBoxLayout(this);
    const int margin = kArrowPx + kPaddingPx;
    outer->setContentsMargins(margin, margin, margin, margin);
    outer->setSpacing(12);
    m_face = new QLabel(this);
    m_face->setFixedSize(kFacePx, kFacePx);
    outer->addWidget(m_face, 0, Qt::AlignVCenter);
    auto *column = new QVBoxLayout;
    column->setSpacing(12);
    m_text = new QLabel(this);
    m_text->setObjectName(QStringLiteral("BubbleText"));
    m_text->setTextFormat(Qt::RichText);
    m_text->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_text->setWordWrap(true);
    m_text->setFixedWidth(kTextWidthPx);
    column->addWidget(m_text);
    m_buttons = new QWidget(this);
    auto *row = new QHBoxLayout(m_buttons);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    row->addStretch();
    column->addWidget(m_buttons);
    outer->addLayout(column);
}

// The bubble's texts use a small markup, simple to translate: a line per paragraph, "- " starts a
// bullet, **words** are bold (a control), `keys` are bold on a raised background (a shortcut).
QString MascotBubble::toHtml(const QString &text) {
    static const QRegularExpression bold(QStringLiteral("\\*\\*(.+?)\\*\\*"));
    static const QRegularExpression key(QStringLiteral("`(.+?)`"));
    const QString keyStyle = QStringLiteral("<span style=\"background-color:%1; font-weight:600;\">&nbsp;\\1&nbsp;</span>")
                                 .arg(UiTheme::color(UiTheme::Token::SurfaceRaised).name());
    const QString bullet = QStringLiteral("<span style=\"color:%1;\">&bull;</span>").arg(UiTheme::color(UiTheme::Token::Accent).name());
    const auto inline_ = [&](const QString &line) {
        return line.toHtmlEscaped().replace(bold, QStringLiteral("<b>\\1</b>")).replace(key, keyStyle);
    };
    QString html;
    bool inList = false;
    bool first = true;
    for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString margin = first ? QStringLiteral("0") : QStringLiteral("6px");
        if (line.startsWith(QLatin1String("- "))) {
            if (!inList)
                html += QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\" style=\"margin-top:%1;\">").arg(margin);
            inList = true;
            html += QStringLiteral("<tr><td width=\"16\" valign=\"top\" style=\"padding-top:2px;\">%1</td>"
                                   "<td style=\"padding-top:2px;\">%2</td></tr>").arg(bullet, inline_(line.mid(2)));
        } else {
            if (inList) html += QStringLiteral("</table>");
            inList = false;
            html += QStringLiteral("<p style=\"margin:0; margin-top:%1;\">%2</p>").arg(margin, inline_(line));
        }
        first = false;
    }
    if (inList) html += QStringLiteral("</table>");
    return html;
}

void MascotBubble::setContent(Mascot::Face face, const QString &text) {
    m_face->setPixmap(Mascot::face(face, kFacePx));
    m_plain = text;
    m_text->setText(toHtml(text));
    // The label's own heightForWidth keeps the height of a longer rich text shown before: the text is
    // measured on a document of its own, laid out as the label does (no margin, its font).
    m_text->ensurePolished();  // the style sheet's font
    QTextDocument measure;
    measure.setDocumentMargin(0);
    measure.setDefaultFont(m_text->font());
    measure.setHtml(m_text->text());
    measure.setTextWidth(kTextWidthPx);
    m_text->setFixedHeight(qCeil(measure.size().height()));
}

QString MascotBubble::text() const { return m_plain; }

QPushButton *MascotBubble::addButton(const QString &text, bool primary) {
    auto *button = new QPushButton(text, m_buttons);
    button->setObjectName(primary ? QStringLiteral("BubblePrimary") : QStringLiteral("BubbleSecondary"));
    button->setAutoDefault(false);
    button->setCursor(Qt::PointingHandCursor);
    m_buttons->layout()->addWidget(button);
    button->show();  // a child added to a shown widget stays hidden otherwise
    return button;
}

void MascotBubble::clearButtons() {
    for (QPushButton *button : m_buttons->findChildren<QPushButton *>(Qt::FindDirectChildrenOnly)) {
        button->hide();
        button->deleteLater();
    }
}

void MascotBubble::placeBeside(const QRect &target, const QList<QRect> &avoid) {
    adjustSize();
    const QRect room = parentWidget()->rect().adjusted(kGapPx, kGapPx, -kGapPx, -kGapPx);
    const QSize size = this->size();
    m_side = Side::None;
    if (target.isEmpty()) {
        move(room.center() - QPoint(size.width() / 2, size.height() / 2));
        update();
        return;
    }
    const QRect around = target.adjusted(-kHoleMarginPx - kGapPx, -kHoleMarginPx - kGapPx, kHoleMarginPx + kGapPx, kHoleMarginPx + kGapPx);
    const auto clampX = [&](int x) { return qBound(room.left(), x, qMax(room.left(), room.right() - size.width())); };
    const auto clampY = [&](int y) { return qBound(room.top(), y, qMax(room.top(), room.bottom() - size.height())); };
    // The arrow needs this much of the side facing the target, past the rounded corners.
    const int reach = kRadiusPx + 2 * kArrowPx;
    struct Place {
        Side side;
        QPoint at;
    };
    // Centred on the target first (below, above, right, left), then slid beside each avoided rectangle.
    QList<int> xs{target.center().x() - size.width() / 2}, ys{target.center().y() - size.height() / 2};
    for (const QRect &other : avoid) {
        xs << other.right() + kRoomPx + 1 << other.left() - kRoomPx - size.width();
        ys << other.bottom() + kRoomPx + 1 << other.top() - kRoomPx - size.height();
    }
    QList<Place> places;
    for (int i = 0; i < xs.size(); ++i) {
        places << Place{Side::Above, {clampX(xs.at(i)), around.bottom()}} << Place{Side::Below, {clampX(xs.at(i)), around.top() - size.height()}};
        if (i < ys.size())
            places << Place{Side::Left, {around.right(), clampY(ys.at(i))}} << Place{Side::Right, {around.left() - size.width(), clampY(ys.at(i))}};
    }
    // Where the arrow can stand on the bubble's side, facing the target: empty when it cannot.
    const auto span = [&](const Place &place) -> std::pair<int, int> {
        const QRect at(place.at, size);
        if (place.side == Side::Above || place.side == Side::Below)
            return {qMax(at.left() + reach, target.left()), qMin(at.right() - reach, target.right())};
        return {qMax(at.top() + reach, target.top()), qMin(at.bottom() - reach, target.bottom())};
    };
    const auto fits = [&](const Place &place) {
        const auto [from, to] = span(place);
        return room.contains(QRect(place.at, size)) && from <= to;
    };
    const auto clear = [&](const Place &place) {
        for (const QRect &other : avoid)
            if (QRect(place.at, size).intersects(other.adjusted(-kRoomPx, -kRoomPx, kRoomPx, kRoomPx))) return false;
        return true;
    };
    const Place *chosen = nullptr;
    for (const Place &place : places)
        if (fits(place) && clear(place)) { chosen = &place; break; }
    if (!chosen)
        for (const Place &place : places)
            if (fits(place)) { chosen = &place; break; }
    if (!chosen) {
        // A control as large as the window (the Gantt, the outliner): inside it, at its top right.
        move(clampX(target.right() - size.width() - kGapPx), clampY(target.top() + kGapPx));
        update();
        return;
    }
    m_side = chosen->side;
    const auto [from, to] = span(*chosen);
    const int middle = (from + to) / 2;
    m_arrowAt = m_side == Side::Above || m_side == Side::Below ? middle - chosen->at.x() : middle - chosen->at.y();
    move(chosen->at);
    update();
}

void MascotBubble::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF surface = QRectF(rect()).adjusted(kArrowPx, kArrowPx, -kArrowPx, -kArrowPx);
    // The modal's light shadow: a few rounded rectangles, no effect (AGENTS.md "Prestazioni").
    painter.setPen(Qt::NoPen);
    const int alphas[] = {10, 8, 6, 4};
    for (int i = 0; i < 4; ++i) {
        const qreal grow = i + 1;
        painter.setBrush(QColor(15, 23, 42, alphas[i]));
        painter.drawRoundedRect(surface.adjusted(-grow, -grow + 2, grow, grow + 2), kRadiusPx + grow, kRadiusPx + grow);
    }
    QPainterPath shape;
    shape.addRoundedRect(surface, kRadiusPx, kRadiusPx);
    if (m_side != Side::None) {
        const bool across = m_side == Side::Above || m_side == Side::Below;
        const qreal length = across ? surface.width() : surface.height();
        const qreal at = qBound(qreal(kRadiusPx + kArrowPx), qreal(m_arrowAt - kArrowPx), length - kRadiusPx - kArrowPx) + (across ? surface.left() : surface.top());
        QPolygonF arrow;
        if (m_side == Side::Above) arrow << QPointF(at - kArrowPx, surface.top() + 1) << QPointF(at, 0) << QPointF(at + kArrowPx, surface.top() + 1);
        else if (m_side == Side::Below) arrow << QPointF(at - kArrowPx, surface.bottom() - 1) << QPointF(at, height()) << QPointF(at + kArrowPx, surface.bottom() - 1);
        else if (m_side == Side::Left) arrow << QPointF(surface.left() + 1, at - kArrowPx) << QPointF(0, at) << QPointF(surface.left() + 1, at + kArrowPx);
        else arrow << QPointF(surface.right() - 1, at - kArrowPx) << QPointF(width(), at) << QPointF(surface.right() - 1, at + kArrowPx);
        QPainterPath tip;
        tip.addPolygon(arrow);
        shape = shape.united(tip);
    }
    painter.fillPath(shape, UiTheme::color(UiTheme::Token::Surface));
}

// --- Tour ---

GuidedTour::GuidedTour(Host host, QObject *parent) : QObject(parent), m_host(std::move(host)) {}

GuidedTour::~GuidedTour() {
    if (isRunning()) Tips::setSuppressed(false);
    delete m_bubble.data();
    delete m_veil.data();
}

void GuidedTour::build() {
    if (!m_veil) m_veil = new TourVeil(m_host.window);
    if (!m_bubble) m_bubble = new MascotBubble(m_host.window);
    if (!m_timer) {
        // The waiting steps and the place of the control, checked a few times a second only while the
        // tour is shown: cheaper than following every change of the map and of the window.
        m_timer = new QTimer(this);
        m_timer->setInterval(200);
        connect(m_timer, &QTimer::timeout, this, &GuidedTour::refresh);
    }
    m_timer->start();
    // The window's shortcuts reach their actions through the application: only the allowed ones pass.
    qApp->removeEventFilter(this);
    qApp->installEventFilter(this);
    Tips::setSuppressed(true);
    m_veil->show();
    m_bubble->show();
    m_veil->raise();
    m_bubble->raise();
}

void GuidedTour::offer() {
    TourSettings::setTourOffered();
    m_kind = Kind::Map;
    build();
    m_offering = true;
    m_bubble->clearButtons();
    m_bubble->setContent(Mascot::Face::Hello, tr("Hi, I'm Mentis, your study brain!\nShall I show you around? It takes **two minutes**."));
    connect(m_bubble->addButton(tr("Not now"), false), &QPushButton::clicked, this, &GuidedTour::stop);
    connect(m_bubble->addButton(tr("Start the tour"), true), &QPushButton::clicked, this, [this] { start(Kind::Map); });
    refresh();
}

bool GuidedTour::offerArea(Kind kind) {
    const QString key = kind == Kind::Gantt ? QStringLiteral("gantt") : QStringLiteral("outliner");
    if (kind == Kind::Map || isRunning() || !TourSettings::tipsEnabled() || TourSettings::tipSeen(key)) return false;
    TourSettings::setTipSeen(key);
    m_kind = kind;
    build();
    m_offering = true;
    m_bubble->clearButtons();
    m_bubble->setContent(Mascot::Face::Hello, kind == Kind::Gantt
        ? tr("Your first time in the **Gantt**!\nShall I show you what is where? It takes a minute.")
        : tr("Your first time in the **outliner**!\nShall I show you what is where? It takes a minute."));
    connect(m_bubble->addButton(tr("Not now"), false), &QPushButton::clicked, this, &GuidedTour::stop);
    connect(m_bubble->addButton(tr("Show me"), true), &QPushButton::clicked, this, [this, kind] { start(kind); });
    refresh();
    return true;
}

void GuidedTour::start(Kind kind) {
    // Not offered again: the map's tour shows the outliner and the Gantt too.
    if (kind == Kind::Map) TourSettings::setTourOffered();
    if (kind != Kind::Gantt) TourSettings::setTipSeen(QStringLiteral("outliner"));
    if (kind != Kind::Outliner) TourSettings::setTipSeen(QStringLiteral("gantt"));
    m_kind = kind;
    m_specs = specs();
    m_offering = false;
    m_practice = false;
    m_revisited = false;
    m_step = 0;
    build();
    show();
}

void GuidedTour::stop() {
    if (m_timer) m_timer->stop();
    qApp->removeEventFilter(this);
    if (m_bubble) m_bubble->deleteLater();
    if (m_veil) m_veil->deleteLater();
    m_bubble = nullptr;
    m_veil = nullptr;
    m_offering = false;
    Tips::setSuppressed(false);
    emit finished();
}

QString GuidedTour::step() const {
    return m_offering || m_step >= m_specs.size() ? QString() : QString::fromLatin1(m_specs.at(m_step).id);
}

int GuidedTour::stepCount() const { return m_specs.size(); }

void GuidedTour::next() {
    if (m_kind == Kind::Map && m_step == 0 && !m_practice) {
        m_practice = true;
        if (m_host.startPracticeMap) m_host.startPracticeMap();
    }
    if (m_step + 1 >= m_specs.size()) return;
    m_revisited = false;
    ++m_step;
    show();
}

void GuidedTour::back() {
    if (m_step == 0) return;
    m_revisited = true;
    --m_step;
    show();
}

// The steps of each tour, in order; the last one is the end. The texts name the controls as the
// window and the manual do, short, in the bubble's markup (MascotBubble::toHtml).
QList<GuidedTour::Spec> GuidedTour::specs() const {
    using F = Mascot::Face;
    const auto names = [](std::initializer_list<const char *> list) {
        QStringList out;
        for (const char *name : list) out << QString::fromLatin1(name);
        return out;
    };
    QList<Spec> map = {
            {"home", F::Hello, tr("This is the **Home**: your maps live here.\nI'll make you a practice map to try things on."),
             names({"newMap"})},
            {"centralTopic", F::Look, tr("This is the **central topic**, the subject of your map.\n- Double-click it\n- Type a subject\n"
                                         "- Press `Enter`"), names({"root"}), {}, Wait::RootTitle},
            {"subtopic", F::Look, tr("Press `Tab` to add a **subtopic**, then type its title."), names({"root"}), {},
             Wait::MoreNodes},
            {"sibling", F::Look, tr("Press `Enter` twice:\n- the first ends the title\n- the second adds a topic **at the same level**"),
             names({"lastTopic"}), {}, Wait::MoreNodes},
            {"topicBar", F::Look, tr("A selected topic has a **small bar** above it: relationship, link, image, note, checkbox, sticker, "
                                     "format painter.\n- **+** beside it: a subtopic\n- **+** below it: a topic at the same level"),
             names({"topicBar", "lastTopic"}), {}, Wait::None, {}, {}, names({"childKnob", "siblingKnob"})},
            {"textBar", F::Smile, tr("Double-click a title and **select some words**: a bar appears with bold, italic, colour and "
                                     "more, just for them."), names({"lastTopic"}), {}, Wait::None, {}, {}, names({"lastTopic"}),
             names({"text"})},
            {"drag", F::Look, tr("**Drag** a topic:\n- onto empty space to place it\n- onto another topic to move it there"),
             names({"allTopics"}), {}, Wait::Moved},
            {"toolbar", F::Look, tr("The **top bar** has the same commands: sibling, subtopic, relationship, summary, boundary.\n"
                                    "**Insert** adds images, notes, links, formulas and more."), names({"topicGroup", "structureGroup"})},
            {"panelPages", F::Look, tr("The **side panel** has three pages:\n- **Style**: the selected topics\n- **Pitch**: how they look "
                                       "on the slides\n- **Map**: structure, colour theme, background"),
             names({"panelPages", "panel"}), QStringLiteral("stylePanel")},
            {"style", F::Look, tr("Try it: **click a topic** (the arrow shows one), then pick another colour on **Style**."),
             names({"sidePanel", "selectedTopic"}), QStringLiteral("stylePanel"), Wait::Styled, {}, {}, names({"selectedTopic"})},
            {"icons", F::Look, tr("**Markers and Stickers**: click a topic, then a marker or a sticker to add it.\n- Markers: priority, "
                                  "progress, flags\n- **New collection**: your own stickers\n- Right-click a sticker to edit it\n"
                                  "The lit button shows or hides this panel."),
             names({"iconPanel", "selectedTopic", "iconToggle"}), QStringLiteral("iconPanel"), Wait::None, {}, {},
             names({"selectedTopic", "iconToggle"})},
            {"pitch", F::Smile, tr("**Present** (`F5`) turns the map into slides, one branch at a time.\nThe **Pitch** page decides how "
                                   "each topic looks."), names({"present"})},
            {"focus", F::Smile, tr("**Focus mode** hides the bars and leaves just the map; `Esc` brings everything back.\n**?** opens "
                                   "the manual."), names({"focusGroup"}), {}, Wait::None, {}, {}, names({"help"})},
            {"sheets", F::Look, tr("A document can hold many **sheets**:\n- pick one here, **+** adds one\n- drag a sheet in the list "
                                   "to reorder it\n- right-click to rename, duplicate or delete"), names({"sheets"}), {}, Wait::None,
             {}, {}, names({"addSheet"})},
            {"views", F::Look, tr("The same topics in three **views**:\n- **Mind map**\n- **Outliner**: a list\n- **Gantt**: a plan in "
                                  "time\n**Next** shows you the other two."),
             names({"outlineToggle", "ganttToggle"}), QStringLiteral("map"), Wait::None, {}, {},
             names({"outlineToggle", "ganttToggle"})},
            {"save", F::Smile, tr("Back on the map. Mentis **saves as you go**: this button shows the state, `Ctrl+S` saves now."),
             names({"save"}), QStringLiteral("map")},
            {"discover", F::Smile, tr("There is more to discover:\n- map types and styles\n- **Settings** and each panel's options\n"
                                      "- connecting your **AI agent**\nFound a bug? Please report it: it helps a lot!"),
             names({"mainMenu"}), {}, Wait::None, tr("Report a bug"), QStringLiteral("https://github.com/simozock/Mentis/issues/new/choose")},
            {"end", F::Cheer, tr("That's it! Press `F1` anywhere for the manual on what you are doing.\nKeep the practice map?")},
    };
    QList<Spec> outliner = {
            {"list", F::Hello, tr("The **outliner** is the sheet as a list: a row per topic, indented by level.\nChanges show on "
                                  "the map at once."), names({"outline"}), QStringLiteral("outline")},
            {"write", F::Look, tr("Click a row to edit it:\n- `Enter`: a new row\n- `Tab` / `Shift+Tab`: one level in / out\n"
                                  "- `Shift+Enter`: a new line"), names({"outline"}), QStringLiteral("outline"), Wait::None, {}, {},
             {}, names({"text"})},
            {"tools", F::Look, tr("Up here:\n- one level in, one level out\n- **+** adds content: text, formula, image, note, sticker, "
                                  "marker, file, label, checkbox"), names({"outlineGroup"}), QStringLiteral("outline")},
            {"format", F::Smile, tr("**Select some words** while you write: a bar offers bold, italic, strikethrough, underline and "
                                    "highlight.\nMarkdown works too."), names({"outline"}), QStringLiteral("outline"), Wait::None,
             {}, {}, {}, names({"text"})},
            {"rows", F::Look, tr("- Drag a row by its **six dots** to move its branch\n- Click a **bullet** to fold its subtopics\n"
                                 "- Right-click a row to copy, duplicate or delete it"), names({"outline"}), QStringLiteral("outline")},
            {"views", F::Look, tr("These buttons switch to the **mind map** and the **Gantt**."),
             names({"outlineToggle", "ganttToggle"}), QStringLiteral("outline"), Wait::None, {}, {},
             names({"outlineToggle", "ganttToggle"})},
            {"end", F::Cheer, tr("That's the outliner! Press `F1` here for its manual.")},
    };
    QList<Spec> gantt = {
            {"plan", F::Hello, tr("The **Gantt** is the sheet as a plan: tasks on the left, their bars on a timeline.\nEach task is a "
                                  "topic of the map."), names({"gantt"}), QStringLiteral("gantt")},
            {"actions", F::Look, tr("Here:\n- **Add task** (`Enter`) and **Add subtask**\n- **Level up** / **Level down** (`Shift+Tab` / "
                                    "`Tab`)\n- **Link in a chain**: dependencies of the selected tasks\n- **Go to today**"),
             names({"ganttActions"}), QStringLiteral("gantt")},
            {"bar", F::Look, tr("Up here:\n- the **side panel**\n- **Resources**: people and the working week\n- **Critical path**: the "
                                "tasks that set the end date\n- **Baseline**: snapshots of the plan to compare with"),
             names({"ganttGroup"}), QStringLiteral("gantt")},
            {"panel", F::Look, tr("The side panel has two pages:\n- **Task**: name, style, dates, progress, people, dependencies\n"
                                  "- **Gantt**: the style of tasks, summaries, milestones and arrows"),
             names({"ganttPanel"}), QStringLiteral("ganttPanel")},
            {"columns", F::Look, tr("On the column names:\n- right-click to show or hide Start, End, Duration, %, Priority, People\n"
                                    "- drag a name to reorder the columns"), names({"ganttHeader"}), QStringLiteral("gantt")},
            {"timeline", F::Smile, tr("- Drag a **bar** to move it\n- Drag its **right edge** to change the duration\n- **Days**, "
                                      "**Weeks**, **Months** set the scale; `Ctrl+wheel` zooms"), names({"ganttScale"}),
             QStringLiteral("gantt")},
            {"end", F::Cheer, tr("That's the Gantt! Press `F1` here for its manual.")},
    };
    if (m_kind == Kind::Outliner) return outliner;
    if (m_kind == Kind::Gantt) return gantt;
    // The map's tour goes on in the outliner and in the Gantt after the views step, without their
    // ends and the outliner's own views step, then comes back to the map.
    QList<Spec> tour;
    for (const Spec &spec : map) {
        if (qstrcmp(spec.id, "save") == 0)
            for (const Spec &other : outliner + gantt)
                if (qstrcmp(other.id, "end") != 0 && qstrcmp(other.id, "views") != 0) tour << other;
        tour << spec;
    }
    return tour;
}

void GuidedTour::show() {
    if (!m_bubble || m_specs.isEmpty()) return;
    const Spec &spec = m_specs.at(m_step);
    if (!spec.prepare.isEmpty() && m_host.prepare) m_host.prepare(spec.prepare);
    // A topic to click must be in sight, not under the panel the step has just opened.
    if (spec.targets.contains(QStringLiteral("selectedTopic")) && m_host.reveal) {
        MindMapModel *model = m_host.model ? m_host.model() : nullptr;
        const QList<const MindNode *> selected = m_host.selection ? m_host.selection() : QList<const MindNode *>();
        const MindNode *root = model ? model->root() : nullptr;
        if (const MindNode *topic = !selected.isEmpty() ? selected.first() : root && !root->children.empty() ? root->children.back().get() : root)
            m_host.reveal(topic);
    }
    m_entry = snapshot();
    m_bubble->clearButtons();
    m_bubble->setContent(spec.face, spec.text);
    const bool last = m_step + 1 == m_specs.size();
    if (last && m_kind == Kind::Map) {
        connect(m_bubble->addButton(tr("Discard it"), false), &QPushButton::clicked, this, [this] {
            stop();
            if (m_host.discardMap) m_host.discardMap();
        });
        connect(m_bubble->addButton(tr("Keep the map"), true), &QPushButton::clicked, this, [this] {
            stop();
            if (m_host.keepMap) m_host.keepMap();
        });
    } else if (last) {
        connect(m_bubble->addButton(tr("Back"), false), &QPushButton::clicked, this, &GuidedTour::back);
        connect(m_bubble->addButton(tr("Done"), true), &QPushButton::clicked, this, &GuidedTour::stop);
    } else {
        if (!spec.link.isEmpty()) {
            const QUrl link(spec.link);
            connect(m_bubble->addButton(spec.linkText, false), &QPushButton::clicked, this, [link] { QDesktopServices::openUrl(link); });
        }
        if (m_step > 0) connect(m_bubble->addButton(tr("Back"), false), &QPushButton::clicked, this, &GuidedTour::back);
        connect(m_bubble->addButton(tr("Skip the tour"), false), &QPushButton::clicked, this, &GuidedTour::stop);
        if (!waits()) connect(m_bubble->addButton(tr("Next"), true), &QPushButton::clicked, this, &GuidedTour::next);
    }
    refresh();
}

bool GuidedTour::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Shortcut && isRunning() && !(m_host.shortcutAllowed && m_host.shortcutAllowed(watched)))
        return true;
    return QObject::eventFilter(watched, event);
}

// A step that asks to do something (it waits, or asks to click a topic), or one about a view the user
// may try (the outliner, the Gantt), lets its controls be used; the others are only shown, arrows
// included: a button there would leave the tour.
bool GuidedTour::usable() const {
    if (m_offering || m_step >= m_specs.size()) return false;
    const Spec &spec = m_specs.at(m_step);
    static const QStringList views{QStringLiteral("outline"), QStringLiteral("gantt"), QStringLiteral("ganttHeader"),
                                   QStringLiteral("ganttScale"), QStringLiteral("ganttPanel")};
    return spec.wait != Wait::None || spec.pointers.contains(QStringLiteral("selectedTopic")) ||
           spec.pointers.contains(QStringLiteral("lastTopic")) ||
           std::any_of(spec.targets.begin(), spec.targets.end(), [](const QString &name) { return views.contains(name); });
}

bool GuidedTour::waits() const {
    return !m_revisited && !m_offering && m_step < m_specs.size() && m_specs.at(m_step).wait != Wait::None;
}

GuidedTour::Snapshot GuidedTour::snapshot() const {
    Snapshot shot;
    MindMapModel *model = m_host.model ? m_host.model() : nullptr;
    if (!model || !model->root()) return shot;
    shot.rootTitle = model->root()->title;
    shot.sheet = model->sheetStyle;
    shot.palette = model->paletteId;
    const std::function<void(const MindNode *)> walk = [&](const MindNode *node) {
        ++shot.nodes;
        shot.styles.insert(node->id, node->style);
        shot.layout += QStringLiteral("%1>%2:%3,%4,%5;").arg(node->id, node->parent ? node->parent->id : QString())
                           .arg(node->manualOffset.x()).arg(node->manualOffset.y()).arg(static_cast<int>(node->side));
        for (const auto &child : node->children) walk(child.get());
    };
    walk(model->root());
    return shot;
}

bool GuidedTour::done() const {
    const Snapshot now = snapshot();
    switch (m_specs.at(m_step).wait) {
    case Wait::RootTitle: return !now.rootTitle.trimmed().isEmpty() && now.rootTitle != m_entry.rootTitle;
    case Wait::MoreNodes: return now.nodes > m_entry.nodes;
    case Wait::Moved: return now.nodes == m_entry.nodes && now.layout != m_entry.layout;
    case Wait::Styled: return now.styles != m_entry.styles || !(now.sheet == m_entry.sheet) || now.palette != m_entry.palette;
    case Wait::None: return false;
    }
    return false;
}

// Where a target of the steps is, in window coordinates: none when hidden or missing.
QList<QRect> GuidedTour::targetRects(const QString &name) const {
    MindMapModel *model = m_host.model ? m_host.model() : nullptr;
    const MindNode *root = model ? model->root() : nullptr;
    const auto nodeRect = [this](const MindNode *node) { return node && m_host.nodeRect ? m_host.nodeRect(node) : QRect(); };
    QList<QRect> rects;
    if (name == QLatin1String("root")) {
        rects << nodeRect(root);
    } else if (name == QLatin1String("lastTopic")) {
        rects << (root && !root->children.empty() ? nodeRect(root->children.back().get()) : nodeRect(root));
    } else if (name == QLatin1String("selectedTopic")) {
        // The selected topics, or the last one made, to be selected.
        for (const MindNode *node : m_host.selection ? m_host.selection() : QList<const MindNode *>()) rects << nodeRect(node);
        if (rects.isEmpty()) return targetRects(QStringLiteral("lastTopic"));
    } else if (name == QLatin1String("allTopics")) {
        QRect all;
        const std::function<void(const MindNode *)> walk = [&](const MindNode *node) {
            all |= nodeRect(node);
            for (const auto &child : node->children) walk(child.get());
        };
        if (root) walk(root);
        rects << all;
    } else if (QWidget *control = m_host.control ? m_host.control(name) : nullptr; control && control->isVisible()) {
        rects << QRect(m_host.window->mapFromGlobal(control->mapToGlobal(QPoint())), control->size());
    }
    QList<QRect> shown;
    for (const QRect &rect : rects)
        if (const QRect inside = rect.intersected(m_host.window->rect()); !inside.isEmpty()) shown << inside;
    return shown;
}

namespace {
// Controls close to each other share a hole (a topic and its bar, the two view switches); the others
// keep their own, so that a hole never takes in the empty space between them (a panel and a topic).
QList<QRect> joined(QList<QRect> rects) {
    const auto area = [](const QRect &rect) { return qint64(rect.width()) * rect.height(); };
    const auto join = [&](const QRect &a, const QRect &b) {
        const qint64 empty = area(a | b) - area(a) - area(b);
        return a.adjusted(-kJoinPx, -kJoinPx, kJoinPx, kJoinPx).intersects(b) && empty * 2 <= area(a) + area(b) && empty <= kJoinAreaPx;
    };
    for (bool again = true; again;) {
        again = false;
        for (int i = 0; i < rects.size() && !again; ++i)
            for (int j = i + 1; j < rects.size() && !again; ++j)
                if (join(rects.at(i), rects.at(j))) {
                    rects[i] |= rects.takeAt(j);
                    again = true;
                }
    }
    return rects;
}

// From the bubble's edge to the edge of a control, the shortest way.
QLineF arrowBetween(const QRect &bubble, const QRect &control) {
    const QRectF to = QRectF(control).adjusted(-kHoleMarginPx - 2, -kHoleMarginPx - 2, kHoleMarginPx + 2, kHoleMarginPx + 2);
    const QRectF from = QRectF(bubble).adjusted(kArrowPx, kArrowPx, -kArrowPx, -kArrowPx);
    const auto nearest = [](const QRectF &rect, QPointF point) {
        return QPointF(qBound(rect.left(), point.x(), rect.right()), qBound(rect.top(), point.y(), rect.bottom()));
    };
    // The head on the middle of the control's side facing the bubble: a corner could be another
    // control's, in a group.
    QPointF end = to.center();
    if (from.top() >= to.bottom()) end.setY(to.bottom());
    else if (from.bottom() <= to.top()) end.setY(to.top());
    else if (from.left() >= to.right()) end.setX(to.right());
    else end.setX(to.left());
    QPointF start = nearest(from, end);
    // A bubble beside the control would leave a stub: the arrow starts further along the bubble's
    // edge, towards its middle, so that it is long enough to be seen.
    const qreal length = QLineF(start, end).length();
    if (length < kMinArrowPx) {
        const qreal slide = std::sqrt(kMinArrowPx * kMinArrowPx - length * length);
        const bool across = qFuzzyCompare(start.y(), from.top()) || qFuzzyCompare(start.y(), from.bottom());
        if (across) start.setX(qBound(from.left(), start.x() + (start.x() > from.center().x() ? -slide : slide), from.right()));
        else start.setY(qBound(from.top(), start.y() + (start.y() > from.center().y() ? -slide : slide), from.bottom()));
    }
    return {start, end};
}
}  // namespace

void GuidedTour::refresh() {
    if (!m_bubble || !m_veil) return;
    if (waits() && done()) {
        next();
        return;
    }
    m_veil->setGeometry(m_host.window->rect());
    // A panel opened by a step may be raised over the veil: the veil and the bubble stay on top.
    const QObjectList &siblings = m_host.window->children();
    if (siblings.isEmpty() || siblings.last() != m_bubble) {
        m_veil->raise();
        m_bubble->raise();
    }
    if (m_offering || m_step >= m_specs.size()) {
        m_veil->setHoles({});
        m_veil->setOpenings({});
        m_veil->setArrows({});
        m_bubble->placeBeside({});
        return;
    }
    const Spec &spec = m_specs.at(m_step);
    QList<QRect> rects, pointed;
    for (const QString &name : spec.targets) rects << targetRects(name);
    for (const QString &name : spec.pointers) pointed << targetRects(name);
    // On the steps about topics or text, their small bars are not dimmed: the user may need them.
    static const QStringList aboutTopics{QStringLiteral("root"), QStringLiteral("lastTopic"), QStringLiteral("selectedTopic"),
                                         QStringLiteral("allTopics"), QStringLiteral("topicBar"), QStringLiteral("outline")};
    const bool topics = std::any_of(spec.targets.begin(), spec.targets.end(), [](const QString &name) { return aboutTopics.contains(name); });
    QList<QRect> bars, usableBars;
    for (const char *kind : {"topic", "text"}) {
        const QString name = QString::fromLatin1(kind);
        const QList<QRect> shown = topics && m_host.floatingBars ? m_host.floatingBars(name) : QList<QRect>();
        bars << shown;
        if (spec.bars.contains(name)) usableBars << shown;
    }
    QList<QRect> holes = joined(rects + bars);
    const QRect first = targetRects(spec.targets.value(0)).value(0);
    QRect anchor;
    for (const QRect &hole : holes)
        if (hole.intersects(first)) anchor = hole;
    QList<QRect> avoid;
    for (const QRect &hole : holes)
        if (hole != anchor) avoid << hole;
    m_veil->setHoles(holes);
    // The window's own buttons (minimize, maximize, close) are always open.
    // The floating bars are lit but used only on the steps about them (the topic bar would leave the
    // step, writing the central topic).
    QList<QRect> openings = usable() ? rects + usableBars : QList<QRect>();
    for (QWidget *control : m_host.window->findChildren<QWidget *>())
        if (control->property("controlKind").isValid() && control->isVisible())  // also the side panel's copies
            openings << QRect(m_host.window->mapFromGlobal(control->mapToGlobal(QPoint())), control->size());
    m_veil->setOpenings(openings);
    m_bubble->placeBeside(anchor, avoid);
    QList<QLineF> arrows;
    // Each place to look at or click gets an arrow, but the one the bubble points at by itself.
    for (const QRect &rect : pointed)
        if (rect != anchor && !(anchor.contains(rect) && rect.width() * 2 > anchor.width() && rect.height() * 2 > anchor.height()))
            arrows << arrowBetween(m_bubble->geometry(), rect);
    m_veil->setArrows(arrows);
}

// --- Tips ---

namespace {
QPointer<MascotBubble> s_tip;
bool s_suppressed = false;
}  // namespace

bool Tips::offer(QWidget *window, const QString &key, QWidget *anchor, const QString &text, const QString &page) {
    if (s_suppressed || s_tip || !window || !TourSettings::tipsEnabled() || TourSettings::tipSeen(key)) return false;
    TourSettings::setTipSeen(key);
    auto *bubble = new MascotBubble(window);
    s_tip = bubble;
    bubble->setContent(Mascot::Face::Think, text);
    if (!page.isEmpty())
        QObject::connect(bubble->addButton(QCoreApplication::translate("Tips", "Read more"), false), &QPushButton::clicked, bubble,
                         [bubble, page] {
                             bubble->deleteLater();
                             Help::open(page);
                         });
    QObject::connect(bubble->addButton(QCoreApplication::translate("Tips", "Got it"), true), &QPushButton::clicked, bubble,
                     &QObject::deleteLater);
    bubble->show();
    bubble->raise();
    bubble->placeBeside(anchor && anchor->isVisible() ? QRect(anchor->mapTo(window, QPoint()), anchor->size()) : QRect());
    return true;
}

MascotBubble *Tips::current() { return s_tip; }

void Tips::setSuppressed(bool suppressed) {
    s_suppressed = suppressed;
    if (suppressed && s_tip) s_tip->deleteLater();
}
