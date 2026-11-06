#include "sidepanel.h"
#include "uitheme.h"
#include "segmentedcontrol.h"

#include <QEvent>
#include <QFrame>
#include <QLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
// FEAT-072 B2-B3 (checked live 2026-10-21): no bordered inner surface; selector and pages 12 px
// from the panel edges (the page sections add 8 more, so their content sits at 20 px).
constexpr int kOuterMargin = 12;
}

SidePanel::SidePanel(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kOuterMargin, 0, kOuterMargin, kOuterMargin);
    layout->setSpacing(kOuterMargin);

    m_selector = new SegmentedControl({}, this);
    connect(m_selector, &SegmentedControl::segmentActivated, this, [this](int index) {
        m_pages->setCurrentIndex(index);
        emit pageActivated(index);
    });
    layout->addWidget(m_selector);

    auto *surface = new QFrame(this);
    surface->setObjectName(QStringLiteral("SidePanelSurface"));
    surface->setStyleSheet(QStringLiteral("QFrame#SidePanelSurface { background:transparent; border:0; }"));
    auto *surfaceLayout = new QVBoxLayout(surface);
    surfaceLayout->setContentsMargins(0, 0, 0, 0);
    surfaceLayout->setSpacing(0);

    // Lo scorrimento riguarda solo il contenuto: il selettore resta sempre raggiungibile.
    m_pages = new QStackedWidget(surface);
    m_pages->setStyleSheet(QStringLiteral("QStackedWidget { background:transparent; }"));
    surfaceLayout->addWidget(m_pages);
    layout->addWidget(surface, 1);
}

int SidePanel::addPage(const QString &label, QWidget *page) {
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Thin scrollbar (FEAT-072 B1): the native one (17 px on Windows) made the
    // panel wider, because its room is kept even when it is hidden (BUG-010).
    scroll->setStyleSheet(UiTheme::qss(QStringLiteral(
        "QScrollArea { background:transparent; border:0; }"
        "QScrollArea > QWidget > QWidget { background:transparent; }"
        "QScrollBar:vertical { background:transparent; width:8px; margin:0; }"
        "QScrollBar::handle:vertical { background:@surfaceRaised; border-radius:3px; min-height:24px; }"
        "QScrollBar::handle:vertical:hover { background:@textMuted; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }")));
    scroll->setWidget(page);
    m_pages->addWidget(scroll);
    page->installEventFilter(this);  // pagine che cambiano contenuto dopo l'aggiunta
    const int index = m_selector->addSegment(label);
    updateContentMinimumWidth();
    return index;
}

QScrollArea *SidePanel::scrollOf(int index) const { return static_cast<QScrollArea *>(m_pages->widget(index)); }

bool SidePanel::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::LayoutRequest) updateContentMinimumWidth();
    return QWidget::eventFilter(watched, event);
}

void SidePanel::updateContentMinimumWidth() {
    int widest = 0;
    for (int i = 0; i < m_pages->count(); ++i) {
        const QWidget *page = scrollOf(i)->widget();
        widest = std::max({widest, page->minimumSizeHint().width(), page->minimumWidth()});
    }
    if (m_pages->count() == 0) return;
    // Spazio per la barra verticale, che compare quando la pagina e' piu' alta del pannello.
    const int width = widest + scrollOf(0)->verticalScrollBar()->sizeHint().width();
    if (width == m_pages->minimumWidth()) return;
    for (int i = 0; i < m_pages->count(); ++i) scrollOf(i)->setMinimumWidth(width);
    m_pages->setMinimumWidth(width);
    // I layout degli antenati tengono in cache il minimo: si ricalcolano subito, cosi' chi riceve il
    // segnale legge gia' il valore nuovo.
    for (QWidget *widget = m_pages->parentWidget(); widget; widget = widget->parentWidget()) {
        if (widget->layout()) widget->layout()->invalidate();
        widget->updateGeometry();  // svuota anche la cache delle misure nel layout del genitore
    }
    emit contentMinimumWidthChanged();
}

int SidePanel::count() const { return m_pages->count(); }

int SidePanel::currentPage() const { return m_pages->currentIndex(); }

QWidget *SidePanel::page(int index) const {
    return index >= 0 && index < m_pages->count() ? scrollOf(index)->widget() : nullptr;
}

void SidePanel::setCurrentPage(int index) {
    if (index < 0 || index >= m_pages->count()) return;
    m_pages->setCurrentIndex(index);
    m_selector->setCurrentIndex(index);
}
