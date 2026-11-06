#include "help.h"

#include <QApplication>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QVariant>
#include <QWidget>

namespace Help {

namespace {

std::function<void(const QString &)> &opener() {
    static std::function<void(const QString &)> value;
    return value;
}

constexpr const char *kPageProperty = "mentisHelpPage";

// While a dialog is modal the main window's shortcuts do not run: this one does, for dialogs only.
class DialogKeyFilter final : public QObject {
public:
    explicit DialogKeyFilter(QObject *parent) : QObject(parent) {}
    std::function<bool(const QKeyEvent *)> isHelpKey;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() != QEvent::KeyPress) return QObject::eventFilter(watched, event);
        QWidget *dialog = QApplication::activeModalWidget();
        if (!dialog || !isHelpKey || !isHelpKey(static_cast<QKeyEvent *>(event))) return QObject::eventFilter(watched, event);
        QWidget *focus = QApplication::focusWidget();
        // A shortcut field being recorded (Settings > Shortcuts) takes the key, F1 too.
        for (const QWidget *w = focus; w; w = w->parentWidget())
            if (qobject_cast<const QKeySequenceEdit *>(w)) return QObject::eventFilter(watched, event);
        const QString page = pageOf(focus && dialog->isAncestorOf(focus) ? focus : dialog);
        open(page);
        return true;
    }
};

}  // namespace

void setOpener(std::function<void(const QString &page)> value) { opener() = std::move(value); }

void open(const QString &page) {
    if (opener()) opener()(page.isEmpty() ? QString::fromLatin1(kIndexPage) : page);
}

void setPage(QWidget *widget, const QString &page) {
    if (widget) widget->setProperty(kPageProperty, page);
}

void installDialogKey(std::function<bool(const QKeyEvent *)> isHelpKey) {
    // One filter for the app; a later main window replaces the key test of an earlier one.
    static DialogKeyFilter *installed = nullptr;
    if (!qApp) return;
    if (!installed) {
        installed = new DialogKeyFilter(qApp);
        qApp->installEventFilter(installed);
    }
    installed->isHelpKey = std::move(isHelpKey);
}

QString pageOf(const QWidget *widget) {
    for (const QWidget *w = widget; w; w = w->parentWidget()) {
        const QString page = w->property(kPageProperty).toString();
        if (!page.isEmpty()) return page;
    }
    return {};
}

}  // namespace Help
