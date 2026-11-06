#include "wheelguard.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QEvent>

namespace {
// On each select: the wheel with the list closed is ignored and handled. QApplication then passes it on
// to the parent widgets, and a QGraphicsProxyWidget reports it ignored to the scene, so the map scrolls.
// It must sit on the select itself: since Qt 6 a filter on the application runs before that passing on,
// so stopping the event there would stop it everywhere.
class ComboWheelFilter final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() != QEvent::Wheel) return false;
        const auto *combo = qobject_cast<QComboBox *>(watched);
        if (!combo || (combo->view() && combo->view()->isVisible())) return false;
        event->ignore();
        return true;
    }
};
}  // namespace

void WheelGuard::install() {
    static bool installed = false;
    if (!qApp || installed) return;
    installed = true;
    auto *guard = new WheelGuard(qApp);
    guard->m_comboFilter = new ComboWheelFilter(guard);
    qApp->installEventFilter(guard);
}

bool WheelGuard::eventFilter(QObject *watched, QEvent *event) {
    // Every widget is polished before it is first shown, so before any wheel can reach it.
    if (event->type() == QEvent::Polish && qobject_cast<QComboBox *>(watched)) watched->installEventFilter(m_comboFilter);
    return false;
}
