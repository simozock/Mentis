#include "texteditmenu.h"

#include "uitheme.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QFocusEvent>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QTextCursor>

namespace TextEditMenu {

QString styleSheet() {
    return UiTheme::qss(QStringLiteral(
        "QMenu{background:@surface;color:@text;border:1px solid @divider;border-radius:8px;padding:4px;}"
        " QMenu::item{min-width:176px;padding:8px 24px 8px 12px;margin:2px 0;border-radius:4px;}"
        " QMenu::item:selected{background:@surfaceRaised;}"
        " QMenu::separator{height:1px;background:@divider;margin:4px 8px;}"
        " QMenu::item:disabled{color:#6b7280;}"));
}

void exec(QWidget *parent, const QPoint &screenPos, bool hasSelection,
          const std::function<void(QKeySequence::StandardKey)> &perform, const Spelling &spelling) {
    QMenu menu(parent);
    menu.setObjectName(QStringLiteral("TextEditMenu"));
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(styleSheet());
    const QMimeData *clipboard = QApplication::clipboard()->mimeData();
    const bool canPaste = clipboard && clipboard->hasText();
    const auto add = [&](const char *text, QKeySequence::StandardKey key, bool enabled) {
        QAction *action = menu.addAction(QCoreApplication::translate("TextEditMenu", text));
        action->setShortcut(QKeySequence(key));
        action->setShortcutVisibleInContextMenu(true);
        action->setEnabled(enabled);
        action->setData(static_cast<int>(key));
    };
    // FEAT-044: up to 5 suggestions, "Learn spelling", "Ignore", then the editing commands.
    QList<QAction *> suggestions;
    QAction *learn = nullptr;
    QAction *ignore = nullptr;
    const QString word = spelling.word.text;
    if (!word.isEmpty()) {
        SpellCheck::Backend *checker = SpellCheck::backend();
        for (const QString &suggestion : checker ? checker->suggestions(word, 5) : QStringList())
            suggestions.append(menu.addAction(suggestion));
        if (suggestions.isEmpty())
            menu.addAction(QCoreApplication::translate("TextEditMenu", "No suggestions"))->setEnabled(false);
        menu.addSeparator();
        learn = menu.addAction(QCoreApplication::translate("TextEditMenu", "Learn spelling"));
        ignore = menu.addAction(QCoreApplication::translate("TextEditMenu", "Ignore"));
        menu.addSeparator();
    }
    add(QT_TRANSLATE_NOOP("TextEditMenu", "Cut"), QKeySequence::Cut, hasSelection);
    add(QT_TRANSLATE_NOOP("TextEditMenu", "Copy"), QKeySequence::Copy, hasSelection);
    add(QT_TRANSLATE_NOOP("TextEditMenu", "Paste"), QKeySequence::Paste, canPaste);
    menu.addSeparator();
    add(QT_TRANSLATE_NOOP("TextEditMenu", "Select all"), QKeySequence::SelectAll, true);
    QAction *chosen = menu.exec(screenPos);
    if (!chosen) return;
    if (chosen == learn) SpellCheck::learn(word);
    else if (chosen == ignore) SpellCheck::ignore(word);
    else if (suggestions.contains(chosen)) { if (spelling.replace) spelling.replace(chosen->text()); }
    else perform(static_cast<QKeySequence::StandardKey>(chosen->data().toInt()));
}

void replaceWord(QTextCursor &cursor, const SpellCheck::Word &word, const QString &replacement) {
    cursor.setPosition(word.position);
    cursor.setPosition(word.position + word.length, QTextCursor::KeepAnchor);
    cursor.insertText(replacement);
}

namespace {
class Filter final : public QObject {
public:
    explicit Filter(QPlainTextEdit *edit) : QObject(edit), m_edit(edit) {}
    bool eventFilter(QObject *watched, QEvent *event) override {
        // FEAT-044: only the field with the focus is checked, not the whole Outliner; a menu
        // taking the focus does not end the editing.
        if (watched == m_edit && event->type() == QEvent::FocusIn) SpellCheck::attach(m_edit->document());
        if (watched == m_edit && event->type() == QEvent::FocusOut &&
            static_cast<QFocusEvent *>(event)->reason() != Qt::PopupFocusReason)
            SpellCheck::detach(m_edit->document());
        if (event->type() != QEvent::ContextMenu) return false;
        const QPoint screen = static_cast<QContextMenuEvent *>(event)->globalPos();
        Spelling spelling;
        const QPoint local = m_edit->viewport()->mapFromGlobal(screen);
        spelling.word = SpellCheck::misspelledAt(m_edit->document(), m_edit->cursorForPosition(local).position());
        spelling.replace = [edit = m_edit, word = spelling.word](const QString &replacement) {
            QTextCursor cursor = edit->textCursor();
            replaceWord(cursor, word, replacement);
            edit->setTextCursor(cursor);
        };
        exec(m_edit, screen, m_edit->textCursor().hasSelection(), [edit = m_edit](QKeySequence::StandardKey key) {
            if (key == QKeySequence::Cut) edit->cut();
            else if (key == QKeySequence::Copy) edit->copy();
            else if (key == QKeySequence::Paste) edit->paste();
            else edit->selectAll();
        }, spelling);
        return true;  // the event goes no further (no system menu, no Outliner menu)
    }

private:
    QPlainTextEdit *m_edit;
};
}  // namespace

void attach(QPlainTextEdit *edit) {
    auto *filter = new Filter(edit);
    edit->setContextMenuPolicy(Qt::DefaultContextMenu);
    edit->viewport()->setContextMenuPolicy(Qt::DefaultContextMenu);
    edit->installEventFilter(filter);
    edit->viewport()->installEventFilter(filter);
}

}  // namespace TextEditMenu
