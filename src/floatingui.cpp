#include "floatingui.h"
#include "nodestyle.h"
#include "uitheme.h"
#include "performance.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QScrollBar>
#include <QChildEvent>
#include <QPixmap>
#include <QPointer>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <algorithm>

namespace {
QList<QPointer<FloatingGroup>> &groups() {
    static QList<QPointer<FloatingGroup>> list;
    return list;
}
QString rgba(const QColor &color) {
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}
QColor mixed(const QColor &a, const QColor &b, qreal t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}
QPixmap tintedPixmap(const QIcon &icon, const QColor &color) {
    QPixmap pixmap = icon.pixmap(QSize(FloatingUi::kIconSize, FloatingUi::kIconSize), 2.0);
    QPainter tint(&pixmap);
    tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tint.fillRect(pixmap.rect(), color);
    return pixmap;
}
}  // namespace

namespace FloatingUi {
Tone toneFor(const QColor &background) {
    Tone tone;
    tone.dark = NodeStyle::isDarkBackground(background);
    if (tone.dark) {
        // Sfondo appena schiarito (foto di riferimento su blu notte e verde petrolio).
        tone.fill = mixed(background, Qt::white, 0.12);
        tone.border = QColor(255, 255, 255, 22);
        tone.text = QColor("#f3f4f6");
        tone.muted = QColor(255, 255, 255, 175);
        tone.hover = QColor(255, 255, 255, 26);
        tone.pressed = QColor(255, 255, 255, 46);
        tone.checkedFill = QColor(255, 255, 255, 235);
        tone.checkedIcon = QColor("#1f2937");
        return tone;
    }
    const bool neutral = background.hslSaturation() < 20 && background.lightness() > 235;
    // Bianco come prima sugli sfondi quasi bianchi; tinta schiarita sugli sfondi colorati chiari.
    tone.fill = neutral ? QColor(255, 255, 255, 214) : mixed(background, Qt::white, 0.45);
    tone.border = QColor(15, 23, 42, 13);
    tone.text = QColor("#1f2937");
    tone.muted = neutral ? QColor("#6b7280") : QColor("#4b5563");
    tone.hover = QColor(15, 23, 42, 15);
    tone.pressed = QColor(15, 23, 42, 28);
    tone.checkedFill = QColor("#1f2937");
    tone.checkedIcon = Qt::white;
    return tone;
}

namespace {
Tone &currentTone() {
    static Tone current = toneFor(Qt::white);
    return current;
}
}  // namespace

const Tone &tone() { return currentTone(); }

void setBackdrop(const QColor &background) {
    const Tone next = toneFor(background);
    Tone &current = currentTone();
    if (next.fill == current.fill && next.dark == current.dark) return;
    current = next;
    for (const QPointer<FloatingGroup> &group : groups())
        if (group) group->applyTone();
}

void retint(QToolButton *button) {
    if (!button) return;
    // Icona di partenza: quella messa da chi crea il bottone (la tinta fatta qui si riconosce dalla chiave).
    QIcon base = button->icon();
    if (button->property("floatingTintKey").toLongLong() == base.cacheKey())
        base = button->property("floatingBaseIcon").value<QIcon>();
    if (base.isNull()) return;
    button->setProperty("floatingBaseIcon", base);
    const Tone &current = tone();
    QIcon icon;
    const QPixmap normal = current.dark ? tintedPixmap(base, current.text) : base.pixmap(QSize(kIconSize, kIconSize), 2.0);
    const QPixmap checked = tintedPixmap(base, current.checkedIcon);
    for (const QIcon::Mode mode : {QIcon::Normal, QIcon::Active}) {
        icon.addPixmap(normal, mode, QIcon::Off);
        icon.addPixmap(checked, mode, QIcon::On);
    }
    button->setIcon(icon);
    button->setProperty("floatingTintKey", icon.cacheKey());
}

QString controlsStyleSheet() {
    // Colori dal tono (tabella di AGENTS.md sul bianco: hover rgba(15,23,42,15), premuto
    // rgba(15,23,42,28), testo #1f2937, secondario #6b7280, selezione #b3d4fc / #202630).
    const Tone &t = tone();
    return QStringLiteral(R"(
        QToolButton { background:transparent; border:0; border-radius:6px; padding:0; }
        QToolButton:hover { background:@hover; }
        QToolButton:pressed { background:@pressed; }
        QToolButton:checked { background:@checkedFill; }
        QToolButton::menu-indicator { image:none; }
        QPushButton { color:@text; background:transparent; border:0; border-radius:6px;
                      padding:0 8px; min-height:32px; max-height:32px; font-size:14px; }
        QPushButton:hover { background:@hover; }
        QPushButton:pressed { background:@pressed; }
        QLabel { color:@muted; background:transparent; font-size:12px; padding:0 8px; }
        QLabel#MapTitle { color:@text; font-size:14px; font-weight:600; padding:0; }
        QLabel#MapSubtitle { color:@muted; font-size:12px; padding:0; }
        QLineEdit { color:@text; background:transparent; border:0; padding:0; margin:0;
                    font-size:14px; font-weight:600;
                    selection-background-color:#b3d4fc; selection-color:#202630; }
        /* Bug 2026-10-14 (foto dell'utente: percentuale dello zoom invisibile): il campo di una
           select modificabile usa le regole della QComboBox, non quelle di QLineEdit, quindi
           restava con la palette di sistema (testo chiaro con Windows in modalità scura). La
           select si disegna da sé (FloatingSelect::paintEvent): queste regole valgono per il campo. */
        QComboBox { color:@text; background:transparent; border:0; padding:0;
                    font-size:14px; font-weight:600;
                    selection-background-color:#b3d4fc; selection-color:#202630; }
    )").replace(QStringLiteral("@hover"), rgba(t.hover)).replace(QStringLiteral("@pressed"), rgba(t.pressed))
       .replace(QStringLiteral("@checkedFill"), rgba(t.checkedFill)).replace(QStringLiteral("@text"), rgba(t.text))
       .replace(QStringLiteral("@muted"), rgba(t.muted));
}

void setupIconButton(QToolButton *button) {
    button->setFixedSize(kControlSize, kControlSize);
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setFocusPolicy(Qt::NoFocus);
}
}

FloatingGroup::FloatingGroup(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(kShadowSide + kPadding, kShadowTop + kPadding,
                               kShadowSide + kPadding, kShadowBottom + kPadding);
    layout->setSpacing(FloatingUi::kSpacing);
    // Stile dei controlli in un solo posto per tutti i gruppi (2026-10-14).
    setStyleSheet(FloatingUi::controlsStyleSheet());
    groups().append(this);
}

FloatingGroup::~FloatingGroup() { groups().removeAll(this); }

void FloatingGroup::addWidget(QWidget *widget) {
    layout()->addWidget(widget);
    if (auto *button = qobject_cast<QToolButton *>(widget)) FloatingUi::retint(button);
}

void FloatingGroup::childEvent(QChildEvent *event) {
    QWidget::childEvent(event);
    if (event->type() != QEvent::ChildAdded) return;
    if (auto *button = qobject_cast<QToolButton *>(event->child()))
        QTimer::singleShot(0, button, [button] { FloatingUi::retint(button); });  // dopo il setIcon di chi lo crea
}

void FloatingGroup::applyTone() {
    setStyleSheet(FloatingUi::controlsStyleSheet());
    for (QToolButton *button : findChildren<QToolButton *>()) FloatingUi::retint(button);
    for (QWidget *child : findChildren<QWidget *>()) child->update();
    update();
}

void FloatingGroup::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(kShadowSide, kShadowTop, -kShadowSide, -kShadowBottom);
    // Ombra: 4 contorni sempre più larghi e trasparenti, spostati di 1 px in basso.
    // Costa pochi rettangoli arrotondati per gruppo, senza buffer fuori schermo.
    painter.setPen(Qt::NoPen);
    // Gli strati si sommano (alfa cumulato ~ 4+3+2+1 = 10 sul bordo): ombra appena percettibile.
    static constexpr int kAlpha[] = {4, 3, 2, 1};
    // L'ombra viene disegnata solo fuori dal riquadro: con il riempimento semitrasparente,
    // prima trasparivano anche gli strati sottostanti e il riquadro risultava grigio.
    QPainterPath outside;
    outside.addRect(QRectF(rect()));
    QPainterPath boxPath;
    boxPath.addRoundedRect(box, kRadius, kRadius);
    painter.save();
    painter.setClipPath(outside.subtracted(boxPath));
    for (int step = 0; step < 4; ++step) {
        const qreal grow = step + 1;
        painter.setBrush(QColor(15, 23, 42, kAlpha[step]));
        painter.drawRoundedRect(box.adjusted(-grow, -grow + 1, grow, grow + 1), kRadius + grow, kRadius + grow);
    }
    painter.restore();
    // Riquadro del tono (bianco semitrasparente sul bianco) con un filo di bordo.
    painter.setBrush(FloatingUi::tone().fill);
    painter.setPen(QPen(FloatingUi::tone().border, 1));
    painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
}

// Elenco a comparsa: fondo surface, bordo divider (o i colori passati, FloatingListPopup::Colors), raggio 8, padding 4, righe da 28 px con
// raggio 4, al massimo dieci righe con scorrimento. I nomi troppo lunghi finiscono con i tre
// puntini; il tooltip di ogni voce ha il testo intero.
FloatingListPopup::Colors FloatingListPopup::themed() {
    using UiTheme::Token;
    return {UiTheme::color(Token::Surface), UiTheme::color(Token::Divider), UiTheme::color(Token::Text),
            UiTheme::color(Token::Divider), UiTheme::color(Token::SurfaceRaised)};
}

FloatingListPopup::FloatingListPopup(const QComboBox *source, int width, const Colors &colors)
    : QFrame(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint), m_colors(colors) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(FloatingUi::kPadding, FloatingUi::kPadding, FloatingUi::kPadding, FloatingUi::kPadding);
    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setUniformItemSizes(true);
    m_list->setMouseTracking(true);  // hover delle righe
    const auto css = [](const QColor &color) {
        return QStringLiteral("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
    };
    m_list->setStyleSheet(QStringLiteral(R"(
        QListWidget{background:transparent;color:@text;border:0;outline:0;font-size:@sizepx;}
        QListWidget::item{color:@text;height:28px;padding:0 8px;border-radius:4px;}
        QListWidget::item:hover{background:@hover;}
        QListWidget::item:selected{color:@text;background:@selected;}
        QScrollBar:vertical{background:transparent;width:6px;margin:0;}
        QScrollBar::handle:vertical{background:#c4cad2;min-height:24px;border-radius:3px;}
        QScrollBar::handle:vertical:hover{background:#9aa2ac;}
        QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
        QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}
    )").replace(QStringLiteral("@text"), css(colors.text)).replace(QStringLiteral("@hover"), css(colors.hover))
        .replace(QStringLiteral("@selected"), css(colors.selected)).replace(QStringLiteral("@size"), QString::number(colors.fontPixelSize)));
    for (int i = 0; i < source->count(); ++i) {
        auto *item = new QListWidgetItem(source->itemText(i), m_list);
        item->setData(Qt::UserRole, i);
        item->setToolTip(source->itemText(i));
        item->setSizeHint(QSize(0, kRowHeight));
        if (i == source->currentIndex()) m_list->setCurrentItem(item);
    }
    layout->addWidget(m_list);
    const int rows = std::clamp(int(source->count()), 1, kMaxRows);
    setFixedSize(width, rows * kRowHeight + 2 * FloatingUi::kPadding);
    if (m_list->currentItem()) m_list->scrollToItem(m_list->currentItem(), QAbstractItemView::PositionAtCenter);
    const auto pick = [this](QListWidgetItem *item) {
        if (!item) return;
        const int index = item->data(Qt::UserRole).toInt();
        const auto callback = chosen;
        close();
        if (callback) callback(index);
    };
    QObject::connect(m_list, &QListWidget::itemClicked, this, pick);
    QObject::connect(m_list, &QListWidget::itemActivated, this, pick);  // Invio
    m_list->installEventFilter(this);
    m_list->viewport()->installEventFilter(this);  // FEAT-004: dragging rows when moved is set
    m_list->setFocus();
    m_dropLine = new QWidget(m_list->viewport());
    m_dropLine->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_dropLine->setStyleSheet(UiTheme::qss(QStringLiteral("background:@accent;border-radius:1px;")));
    m_dropLine->hide();
    m_autoScroll = new QTimer(this);
    m_autoScroll->setInterval(60);
    QObject::connect(m_autoScroll, &QTimer::timeout, this, [this] {
        QScrollBar *bar = m_list->verticalScrollBar();
        bar->setValue(bar->value() + m_autoScrollStep * bar->singleStep());
        updateDrag(m_lastDragPos);
    });
}

int FloatingListPopup::dropGap(int y) const {
    const int count = m_list->count();
    QListWidgetItem *item = m_list->itemAt(QPoint(1, std::clamp(y, 0, m_list->viewport()->height() - 1)));
    if (!item) return y < 0 ? 0 : count;
    const int row = m_list->row(item);
    const QRect rect = m_list->visualItemRect(item);
    return y < rect.center().y() ? row : row + 1;
}

void FloatingListPopup::updateDrag(const QPoint &viewportPos) {
    m_lastDragPos = viewportPos;
    const int gap = dropGap(viewportPos.y());
    const int count = m_list->count();
    const QRect anchor = gap < count ? m_list->visualItemRect(m_list->item(gap))
                                     : m_list->visualItemRect(m_list->item(count - 1)).translated(0, kRowHeight);
    // Centred on the gap between two rows; kept inside the viewport above the first and below the last.
    const int y = std::clamp(anchor.top() - 1, 0, std::max(0, m_list->viewport()->height() - 2));
    m_dropLine->setGeometry(0, y, m_list->viewport()->width(), 2);
    m_dropLine->show();
    // Near the top or bottom edge the list scrolls (only when it has more rows than fit).
    const int edge = kRowHeight / 2;
    m_autoScrollStep = viewportPos.y() < edge ? -1 : viewportPos.y() > m_list->viewport()->height() - edge ? 1 : 0;
    if (m_autoScrollStep && !m_autoScroll->isActive()) m_autoScroll->start();
    else if (!m_autoScrollStep) m_autoScroll->stop();
}

void FloatingListPopup::endDrag() {
    if (m_dragging && m_activeItem) m_list->setCurrentItem(m_activeItem);  // also when Esc cancels
    m_dragRow = -1;
    m_dragging = false;
    m_dropLine->hide();
    m_autoScroll->stop();
    m_list->viewport()->unsetCursor();
}

void FloatingListPopup::mousePressEvent(QMouseEvent *event) {
    // Con un Qt::Popup aperto Qt manda al popup anche i clic fuori: chi è fuori lo chiude.
    if (!rect().contains(event->position().toPoint())) { close(); return; }
    QFrame::mousePressEvent(event);
}

bool FloatingListPopup::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_list && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        if (m_dragging) {  // the first Esc cancels the drag, the list stays open
            endDrag();
            m_swallowRelease = true;
        } else {
            close();
        }
        return true;
    }
    // FEAT-004: a press and a move past the drag distance starts a drag; the release moves the row
    // and is swallowed, so it does not pick the row. A plain click picks it as before.
    if (moved && watched == m_list->viewport()) {
        const auto *mouse = event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove ||
                event->type() == QEvent::MouseButtonRelease ? static_cast<QMouseEvent *>(event) : nullptr;
        switch (mouse ? event->type() : QEvent::None) {
        case QEvent::MouseButtonPress:
            m_swallowRelease = false;
            if (mouse->button() == Qt::LeftButton) {
                QListWidgetItem *item = m_list->itemAt(mouse->position().toPoint());
                m_dragRow = item ? m_list->row(item) : -1;
                m_activeItem = m_list->currentItem();  // the press makes the pressed row current
                m_pressPos = mouse->position().toPoint();
            }
            break;
        case QEvent::MouseMove:
            if (m_dragRow < 0 || !(mouse->buttons() & Qt::LeftButton)) break;
            if (!m_dragging && (mouse->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
                m_dragging = true;
                m_list->viewport()->setCursor(Qt::ClosedHandCursor);
            }
            if (m_dragging) { updateDrag(mouse->position().toPoint()); return true; }
            break;
        case QEvent::MouseButtonRelease:
            if (mouse->button() != Qt::LeftButton) break;
            if (m_swallowRelease) { m_swallowRelease = false; return true; }
            if (m_dragging) {
                const int from = m_dragRow;
                const int gap = dropGap(mouse->position().toPoint().y());
                const int to = gap > from ? gap - 1 : gap;
                endDrag();
                if (to != from) {
                    QListWidgetItem *item = m_list->takeItem(from);
                    m_list->insertItem(to, item);
                    for (int row = 0; row < m_list->count(); ++row) m_list->item(row)->setData(Qt::UserRole, row);
                }
                if (m_activeItem) m_list->setCurrentItem(m_activeItem);
                if (to != from) {
                    const auto callback = moved;
                    callback(from, to);
                }
                return true;
            }
            m_dragRow = -1;
            break;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void FloatingListPopup::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(m_colors.border, 1));
    painter.setBrush(m_colors.surface);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}

FloatingSelect::FloatingSelect(QWidget *parent) : QComboBox(parent) {
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::NoFocus);
    // FEAT-035 part B: as tall as the floating buttons at the chosen text size and density.
    const auto fit = [this] { setFixedHeight(UiTheme::heightPx(UiTheme::space(FloatingUi::kControlSize))); };
    fit();
    connect(UiTheme::AppearanceNotifier::instance(), &UiTheme::AppearanceNotifier::changed, this, fit);
}

QRect FloatingSelect::textRect() const {
    const int chevronLeft = width() - FloatingUi::kTextPaddingX - FloatingUi::kChevronSize;
    return QRect(FloatingUi::kTextPaddingX, 0, std::max(0, chevronLeft - 4 - FloatingUi::kTextPaddingX), height());
}

void FloatingSelect::placeLineEdit() {
    // QComboBox mette il campo dove dice lo stile; qui va dove la select disegna il testo.
    QLineEdit *edit = lineEdit();
    if (!edit) return;
    // Filtro per riportare il campo al suo posto (eventFilter). Installarlo di nuovo non lo duplica.
    edit->installEventFilter(this);
    edit->setGeometry(lineEditRect());
}

QRect FloatingSelect::lineEditRect() const {
    // QLineEdit disegna il testo 2 px dentro il proprio bordo (QLineEditPrivate::horizontalMargin,
    // qtbase/src/widgets/widgets/qlineedit_p.h): il campo parte 2 px prima, così il testo inizia a
    // 8 px dal bordo della select, come il nome del foglio disegnato in paintEvent.
    constexpr int kLineEditInnerMargin = 2;
    return textRect().adjusted(-kLineEditInnerMargin, 0, 0, 0);
}

// Bug 2026-10-14 (foto dell'utente: percentuale dello zoom attaccata al bordo sinistro della
// select): nell'app QComboBox riportava il campo nel rettangolo calcolato dallo stile (circa 2 px
// dal bordo) dopo che placeLineEdit l'aveva messo a 8 px, come il nome del foglio. Con il filtro
// installato in placeLineEdit arrivano qui gli spostamenti del campo: se non è dove la select
// disegna il testo, ci torna subito.
bool FloatingSelect::eventFilter(QObject *watched, QEvent *event) {
    const bool handled = QComboBox::eventFilter(watched, event);
    if (QLineEdit *edit = lineEdit(); edit && watched == edit &&
        (event->type() == QEvent::Move || event->type() == QEvent::Resize) && edit->geometry() != lineEditRect())
        placeLineEdit();  // setGeometry con lo stesso rettangolo non genera altri eventi: nessun ciclo
    return handled;
}

void FloatingSelect::resizeEvent(QResizeEvent *event) { QComboBox::resizeEvent(event); placeLineEdit(); }
void FloatingSelect::showEvent(QShowEvent *event) { QComboBox::showEvent(event); placeLineEdit(); }
void FloatingSelect::changeEvent(QEvent *event) { QComboBox::changeEvent(event); placeLineEdit(); }

void FloatingSelect::showPopup() {
    // Come la select dei font: un clic sulla select con l'elenco appena chiuso (Qt ripete la
    // pressione che ha chiuso il popup) lo lascia chiuso invece di riaprirlo.
    if (m_popup) { m_popup->close(); return; }
    if (m_popupClosed.isValid() && m_popupClosed.elapsed() < 250) return;
    m_pressed = false;  // il popup prende il mouse: il rilascio non arriva alla select
    auto *popup = new FloatingListPopup(this, std::max(width(), 96));
    m_popup = popup;
    QObject::connect(popup, &QObject::destroyed, this, [this] { m_popupClosed.start(); update(); });
    popup->chosen = [this](int index) {
        setCurrentIndex(index);
        emit activated(index);
    };
    if (reordered) popup->moved = [this](int from, int to) { if (reordered) reordered(from, to); };
    // Le select fisse stanno in basso: l'elenco si apre sopra, a 4 px, e sotto solo se sopra
    // manca spazio.
    const QRect global(mapToGlobal(QPoint(0, 0)), size());
    QPoint at(global.left(), global.top() - 4 - popup->height());
    if (const QScreen *s = screen()) {
        const QRect available = s->availableGeometry();
        if (at.y() < available.top()) at.setY(global.bottom() + 1 + 4);
        at.setX(std::clamp(at.x(), available.left(), std::max(available.left(), available.right() - popup->width())));
    }
    popup->move(at);
    popup->show();
    update();
}

void FloatingSelect::hidePopup() { if (m_popup) m_popup->close(); }

void FloatingSelect::flash() {
    if (m_flashAnimation) return;  // controllato: non si accumulano lampeggi
    if (!Performance::animationsEnabled()) {
        // Profilo per hardware datato: evidenziazione fissa per lo stesso tempo, senza animazione.
        m_flash = 1;
        update();
        QTimer::singleShot(900, this, [this] { m_flash = 0; update(); });
        return;
    }
    auto *animation = new QVariantAnimation(this);
    animation->setDuration(900);
    // Due impulsi morbidi: sale, scende a metà, risale, si spegne.
    animation->setKeyValueAt(0.0, 0.0);
    animation->setKeyValueAt(0.2, 1.0);
    animation->setKeyValueAt(0.45, 0.25);
    animation->setKeyValueAt(0.7, 1.0);
    animation->setKeyValueAt(1.0, 0.0);
    animation->setEasingCurve(QEasingCurve::InOutSine);
    QObject::connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_flash = value.toReal();
        update();
    });
    QObject::connect(animation, &QVariantAnimation::finished, this, [this] { m_flash = 0; update(); });
    m_flashAnimation = animation;
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void FloatingSelect::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect());
    const bool open = !m_popup.isNull();
    painter.setPen(Qt::NoPen);
    if (m_pressed || open || underMouse()) {
        painter.setBrush(m_pressed ? FloatingUi::tone().pressed : FloatingUi::tone().hover);
        painter.drawRoundedRect(box, FloatingUi::kControlRadius, FloatingUi::kControlRadius);
    }
    if (m_flash > 0) {
        // Accento #7047eb (AGENTS.md "Colori") al massimo al 18% di opacità, con un filo di bordo.
        painter.setBrush(QColor(0x70, 0x47, 0xeb, qRound(46 * m_flash)));
        painter.setPen(QPen(QColor(0x70, 0x47, 0xeb, qRound(140 * m_flash)), 1));
        painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), FloatingUi::kControlRadius, FloatingUi::kControlRadius);
        painter.setPen(Qt::NoPen);
    }
    const QRect chevronRect(width() - FloatingUi::kTextPaddingX - FloatingUi::kChevronSize,
                            (height() - FloatingUi::kChevronSize) / 2, FloatingUi::kChevronSize, FloatingUi::kChevronSize);
    static const QIcon chevron(QStringLiteral(":/assets/hugeicons/arrow-down.svg"));
    if (FloatingUi::tone().dark) painter.drawPixmap(chevronRect, tintedPixmap(chevron, FloatingUi::tone().text));
    else chevron.paint(&painter, chevronRect);
    if (isEditable()) return;  // il testo lo mostra il campo (placeLineEdit)
    QFont labelFont = font();
    labelFont.setPixelSize(UiTheme::fontPx(14));
    labelFont.setWeight(QFont::DemiBold);
    painter.setFont(labelFont);
    painter.setPen(FloatingUi::tone().text);
    const QRect text = textRect();
    painter.drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetrics(labelFont).elidedText(currentText(), Qt::ElideRight, text.width()));
}

void FloatingSelect::mousePressEvent(QMouseEvent *event) {
    // Select modificabile: il campo prende i clic sul testo; la freccia apre l'elenco. La zona
    // della freccia è quella disegnata qui, non quella calcolata dallo stile di sistema.
    if (isEditable() && event->button() == Qt::LeftButton && event->position().x() > textRect().right()) {
        showPopup();
        event->accept();
        return;
    }
    m_pressed = true;
    update();
    QComboBox::mousePressEvent(event);
}
void FloatingSelect::mouseReleaseEvent(QMouseEvent *event) { m_pressed = false; update(); QComboBox::mouseReleaseEvent(event); }
void FloatingSelect::leaveEvent(QEvent *event) { m_pressed = false; update(); QComboBox::leaveEvent(event); }
