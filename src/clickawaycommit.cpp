#include "clickawaycommit.h"

#include <QApplication>
#include <QLineEdit>
#include <QMouseEvent>

void ClickAwayCommit::install() {
    static bool installed = false;
    if (!qApp || installed) return;
    installed = true;
    qApp->installEventFilter(new ClickAwayCommit(qApp));
}

bool ClickAwayCommit::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::MouseButtonPress || !watched->isWidgetType()) return false;
    auto *field = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    if (!field) return false;
    const auto *target = static_cast<QWidget *>(watched);
    // Inside the field (its − and +), or in another window (a select's list, a menu): no change.
    if (target == field || field->isAncestorOf(target) || target->window() != field->window()) return false;
    // A widget that takes the focus on a click, or an ancestor that does (Qt gives it the focus, the
    // field loses it by itself): no change. This keeps the outline's format bar, whose ClickFocus with
    // the field as focus proxy leaves the field open (mainwindow.cpp).
    for (const QWidget *widget = target; widget; widget = widget->isWindow() ? nullptr : widget->parentWidget())
        if (widget->isEnabled() && (widget->focusPolicy() & Qt::ClickFocus)) return false;
    field->clearFocus();  // before the click acts, so a button pressed next works on the confirmed value
    return false;
}
