#include "modaldialog.h"

#include "help.h"
#include "uitheme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

ModalDialog::ModalDialog(QWidget *parent, const QString &title, const QString &acceptText, int surfaceWidthPx)
    : QDialog(parent ? parent->window() : nullptr, Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground);
    // FEAT-092: modal to the main window only (not to the whole app): the manual window, opened
    // with F1 or "?", stays usable beside the dialog.
    setWindowModality(Qt::WindowModal);
    setObjectName(QStringLiteral("ModalDialog"));
    // FEAT-035 (2026-10-18): modale scuro con i token (AGENTS.md "Interfaccia scura"); prima chiaro.
    setStyleSheet(UiTheme::qss(QStringLiteral(
        "QWidget#ModalSurface { background:transparent; }"
        "QWidget#ModalSeparator { background:@divider; }"
        "QLabel { color:@text; font-size:14px; background:transparent; }"
        "QLabel#ModalTitle { font-size:20px; font-weight:600; }"
        "QLabel#ModalError { color:@textMuted; font-size:12px; }"
        "QLineEdit { background:@surfaceSunken; color:@text; border:1px solid @surfaceSunken; border-radius:4px; min-height:30px;"
        " padding:0 8px; font-size:14px; selection-background-color:@accent; selection-color:@text; }"
        "QLineEdit:focus { border-color:@accent; }"
        "QCheckBox { color:@text; font-size:14px; spacing:8px; }"
        "QCheckBox:disabled { color:@textMuted; }"
        "QPushButton { min-height:32px; max-height:32px; border-radius:4px; padding:0 16px; font-size:14px; }"
        "QPushButton#ModalCancel, QPushButton#ModalSecondary { background:@surfaceRaised; color:@text; border:1px solid @divider; }"
        "QPushButton#ModalCancel:hover, QPushButton#ModalSecondary:hover { background:@divider; }"
        "QPushButton#ModalCancel:pressed, QPushButton#ModalSecondary:pressed { background:@surfaceSunken; }"
        "QPushButton#ModalAccept { background:@accent; color:@text; border:0; font-weight:600; }"
        "QPushButton#ModalAccept:disabled { background:@textMuted; color:@surface; }"
        "QPushButton:focus { outline:none; }"
        // BUG-036: "?" (FEAT-092) looks like the close button, not like Fusion's white tool button.
        "QToolButton#ModalClose, QToolButton#ModalHelp { background:transparent; border:0; border-radius:4px; }"
        "QToolButton#ModalClose:hover, QToolButton#ModalHelp:hover { background:@divider; }")));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    m_surface = new QWidget(this);
    m_surface->setObjectName(QStringLiteral("ModalSurface"));
    m_surface->setFixedWidth(surfaceWidthPx);
    outer->addStretch();
    outer->addWidget(m_surface, 0, Qt::AlignHCenter);
    outer->addStretch();

    auto *surfaceLayout = new QVBoxLayout(m_surface);
    surfaceLayout->setContentsMargins(kPaddingPx, kPaddingPx, kPaddingPx, kPaddingPx);
    surfaceLayout->setSpacing(16);
    auto *heading = new QLabel(title, m_surface);
    heading->setObjectName(QStringLiteral("ModalTitle"));
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->addWidget(heading, 1);
    // FEAT-092: "?" opens the dialog's page of the manual; shown only when it has one (showEvent).
    auto *help = new QToolButton(m_surface);
    help->setObjectName(QStringLiteral("ModalHelp"));
    help->setIcon(QIcon(QStringLiteral(":/assets/hugeicons/help-light.svg")));
    help->setIconSize(QSize(16, 16));
    help->setFixedSize(28, 28);
    help->setToolTip(tr("Help"));
    help->setAccessibleName(tr("Help"));
    help->hide();
    connect(help, &QToolButton::clicked, this, [this] { Help::open(Help::pageOf(this)); });
    titleRow->addWidget(help, 0, Qt::AlignTop);
    m_help = help;
    auto *close = new QToolButton(m_surface);
    close->setObjectName(QStringLiteral("ModalClose"));
    close->setIcon(QIcon(QStringLiteral(":/assets/hugeicons/window-close-light.svg")));
    close->setIconSize(QSize(16, 16));
    close->setFixedSize(28, 28);
    close->setToolTip(tr("Close"));
    close->setAccessibleName(tr("Close"));
    close->hide();
    connect(close, &QToolButton::clicked, this, &QDialog::reject);
    titleRow->addWidget(close, 0, Qt::AlignTop);
    m_close = close;
    surfaceLayout->addLayout(titleRow);
    m_content = new QVBoxLayout;
    m_content->setContentsMargins(0, 0, 0, 0);
    m_content->setSpacing(12);
    surfaceLayout->addLayout(m_content);
    // Separatore sopra i bottoni, da bordo a bordo della superficie (foto 02 di FEAT-035).
    m_separator = new QWidget(m_surface);
    m_separator->setObjectName(QStringLiteral("ModalSeparator"));
    m_separator->setFixedHeight(1);
    m_separator->setAttribute(Qt::WA_StyledBackground);
    surfaceLayout->addWidget(m_separator);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch();
    m_cancel = new QPushButton(tr("Cancel"), m_surface);
    m_cancel->setObjectName(QStringLiteral("ModalCancel"));
    m_cancel->setAutoDefault(false);
    m_accept = new QPushButton(acceptText, m_surface);
    m_accept->setObjectName(QStringLiteral("ModalAccept"));
    m_accept->setDefault(true);  // Invio, se attivo
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_accept);
    surfaceLayout->addLayout(buttons);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_accept, &QPushButton::clicked, this, &QDialog::accept);
}

QPushButton *ModalDialog::addSecondaryButton(const QString &text) {
    auto *button = new QPushButton(text, m_surface);
    button->setObjectName(QStringLiteral("ModalSecondary"));
    button->setAutoDefault(false);
    // Prima di "Annulla" nella riga dei bottoni (dopo lo spazio elastico).
    auto *buttons = static_cast<QHBoxLayout *>(m_surface->layout()->itemAt(m_surface->layout()->count() - 1)->layout());
    buttons->insertWidget(1, button);
    connect(button, &QPushButton::clicked, this, [this] { done(kSecondaryResult); });
    m_secondary = button;
    return button;
}

void ModalDialog::useCloseButton() {
    m_cancel->hide();
    m_close->show();
    if (m_secondary) {
        auto *buttons = static_cast<QHBoxLayout *>(m_surface->layout()->itemAt(m_surface->layout()->count() - 1)->layout());
        buttons->removeWidget(m_secondary);
        buttons->insertWidget(0, m_secondary);  // before the stretch: left edge
    }
}

void ModalDialog::useOwnButtons() {
    m_separator->hide();
    m_cancel->hide();
    m_accept->hide();
    m_accept->setDefault(false);  // Enter does not press a hidden button
    m_close->show();
}

QRect ModalDialog::surfaceRect() const { return m_surface->geometry(); }

ModalDialog::Choice ModalDialog::ask(QWidget *parent, const QString &title, const QString &text, const QString &primaryText,
                                     const QString &secondaryText, const QString &cancelText) {
    ModalDialog dialog(parent, title, primaryText, 420);
    auto *message = new QLabel(text);
    message->setWordWrap(true);
    dialog.contentLayout()->addWidget(message);
    if (!secondaryText.isEmpty()) dialog.addSecondaryButton(secondaryText);
    if (!cancelText.isEmpty()) dialog.cancelButton()->setText(cancelText);
    const int result = dialog.exec();
    if (result == QDialog::Accepted) return Choice::Primary;
    if (result == kSecondaryResult) return Choice::Secondary;
    return Choice::Cancel;
}

ModalDialog::Choice ModalDialog::askWithClose(QWidget *parent, const QString &title, const QString &text,
                                              const QString &primaryText, const QString &secondaryText) {
    ModalDialog dialog(parent, title, primaryText, 420);
    auto *message = new QLabel(text);
    message->setWordWrap(true);
    dialog.contentLayout()->addWidget(message);
    dialog.addSecondaryButton(secondaryText);
    dialog.useCloseButton();
    const int result = dialog.exec();
    if (result == QDialog::Accepted) return Choice::Primary;
    if (result == kSecondaryResult) return Choice::Secondary;
    return Choice::Cancel;
}

void ModalDialog::inform(QWidget *parent, const QString &title, const QString &text) {
    ModalDialog dialog(parent, title, tr("OK"), 420);
    auto *message = new QLabel(text);
    message->setWordWrap(true);
    message->setTextInteractionFlags(Qt::TextSelectableByMouse);  // si puo' copiare il messaggio d'errore
    dialog.contentLayout()->addWidget(message);
    dialog.cancelButton()->hide();
    dialog.exec();
}

QString ModalDialog::askText(QWidget *parent, const QString &title, const QString &label, const QString &acceptText, bool *ok,
                             const QString &text) {
    ModalDialog dialog(parent, title, acceptText, 420);
    auto *caption = new QLabel(label);
    auto *field = new QLineEdit(text);
    field->selectAll();
    dialog.contentLayout()->addWidget(caption);
    dialog.contentLayout()->addWidget(field);
    field->setFocus();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (ok) *ok = accepted;
    return accepted ? field->text() : QString();
}

void ModalDialog::showEvent(QShowEvent *event) {
    if (m_help) m_help->setVisible(!Help::pageOf(this).isEmpty());  // FEAT-092
    // Il velo copre tutta la finestra principale.
    if (QWidget *owner = parentWidget()) {
        setGeometry(owner->window()->geometry());
        owner->window()->installEventFilter(this);  // BUG-040: the scrim follows the window
    }
    // Bug 2026-10-20 (foto dell'utente): la superficie centrata (AlignHCenter) non passa la larghezza
    // ai testi che vanno a capo, che restavano alti una riga e tagliati. Altezza dalla larghezza
    // del contenuto.
    const int inner = m_surface->width() - 2 * kPaddingPx;
    for (QLabel *label : m_surface->findChildren<QLabel *>()) {
        if (!label->wordWrap()) continue;
        label->ensurePolished();
        // An empty label has no height for a width (-1): Qt warned "Negative sizes (0,-1)" for the PDF
        // window's Gantt note opened on the map (user, 2026-10-31).
        if (const int height = label->heightForWidth(inner); height >= 0) label->setMinimumHeight(height);
    }
    QDialog::showEvent(event);
}

void ModalDialog::mousePressEvent(QMouseEvent *event) {
    // BUG-040: the modal covers the main window and blocks its title bar, so the window could not be
    // moved while a modal was open. A left press that no control takes (scrim, title, empty parts of
    // the surface) moves the modal with the system's move (also on Wayland); moveEvent drags the main
    // window along. A maximized window stays where it is, like the system's own title bar.
    QWidget *owner = parentWidget() ? parentWidget()->window() : nullptr;
    if (event->button() == Qt::LeftButton && owner && !owner->isMaximized() && !owner->isFullScreen() &&
        windowHandle() && windowHandle()->startSystemMove()) {
        event->accept();
        return;
    }
    QDialog::mousePressEvent(event);
}

void ModalDialog::moveEvent(QMoveEvent *event) {
    QDialog::moveEvent(event);
    if (!isVisible()) return;
    if (QWidget *owner = parentWidget() ? parentWidget()->window() : nullptr; owner && owner->pos() != pos()) owner->move(pos());
}

bool ModalDialog::eventFilter(QObject *watched, QEvent *event) {
    // The scrim follows the main window when the system moves or resizes it (Win+arrows, Snap).
    if (isVisible() && watched == (parentWidget() ? parentWidget()->window() : nullptr) &&
        (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
        const QRect target = static_cast<QWidget *>(watched)->geometry();
        if (geometry() != target) setGeometry(target);
    }
    return QDialog::eventFilter(watched, event);
}

void ModalDialog::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), UiTheme::color(UiTheme::Token::Scrim));
    // Ombra leggera: pochi rettangoli arrotondati attorno alla superficie, verso il basso (come
    // ToolbarGroupFrame, AGENTS.md "Prestazioni"), poi la superficie scura.
    const QRectF surface = QRectF(m_surface->geometry());
    painter.setPen(Qt::NoPen);
    const int alphas[] = {10, 8, 6, 4};
    for (int i = 0; i < 4; ++i) {
        const qreal grow = i + 1;
        painter.setBrush(QColor(15, 23, 42, alphas[i]));
        painter.drawRoundedRect(surface.adjusted(-grow, -grow + 2, grow, grow + 2), kRadiusPx + grow, kRadiusPx + grow);
    }
    painter.setBrush(UiTheme::color(UiTheme::Token::Surface));
    painter.drawRoundedRect(surface, kRadiusPx, kRadiusPx);
}
