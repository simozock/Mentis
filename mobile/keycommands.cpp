#include "keycommands.h"

#include "shortcutcatalog.h"

#include <QCoreApplication>
#include <QKeySequence>
#include <QVariantMap>

namespace {
QKeySequence fromPortable(const QString &portable) { return QKeySequence(portable, QKeySequence::PortableText); }
}  // namespace

ShortcutRegistry &KeyCommands::registry() {
    // One for the app, made after the translators (the names are translated when added).
    static ShortcutRegistry *shared = [] {
        auto *created = new ShortcutRegistry(QCoreApplication::instance());
        ShortcutCatalog::addAll(*created);
        return created;
    }();
    return *shared;
}

KeyCommands::KeyCommands(QObject *parent) : QObject(parent) {
    connect(&registry(), &ShortcutRegistry::changed, this, [this] { ++m_revision; emit changed(); });
}

QStringList KeyCommands::phoneCommands() {
    // MapPage.qml's KeyCommand, FormatKey and ArrowCommand (mobile_test checks both ways).
    static const QStringList ids{
        QStringLiteral("home"), QStringLiteral("save"), QStringLiteral("undo"), QStringLiteral("redo"),
        QStringLiteral("fit"), QStringLiteral("zoomIn"), QStringLiteral("zoomOut"), QStringLiteral("actualSize"),
        QStringLiteral("find"), QStringLiteral("image"), QStringLiteral("copyStyle"), QStringLiteral("pasteStyle"),
        QStringLiteral("prevSheet"), QStringLiteral("nextSheet"), QStringLiteral("toggleIconPanel"),
        QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("underline"), QStringLiteral("strikeOut"),
        QStringLiteral("addChild"), QStringLiteral("addSibling"), QStringLiteral("topicBefore"), QStringLiteral("parentTopic"),
        QStringLiteral("edit"), QStringLiteral("editAtEnd"), QStringLiteral("remove"), QStringLiteral("mapCopy"),
        QStringLiteral("mapPaste"), QStringLiteral("note"), QStringLiteral("link"), QStringLiteral("formula"),
        QStringLiteral("labels"), QStringLiteral("attachFiles"), QStringLiteral("addTask"), QStringLiteral("markers"),
        QStringLiteral("stickers"), QStringLiteral("toggleCollapse"), QStringLiteral("goToCentral"),
        QStringLiteral("moveUp"), QStringLiteral("moveDown"), QStringLiteral("mapSelectLeft"),
        QStringLiteral("mapSelectRight"), QStringLiteral("mapSelectUp"), QStringLiteral("mapSelectDown"),
        QStringLiteral("mapAddLeft"), QStringLiteral("mapAddRight"), QStringLiteral("mapAddUp"), QStringLiteral("mapAddDown"),
        QStringLiteral("outlineIndent"), QStringLiteral("outlineOutdent"), QStringLiteral("ganttIndent"),
        QStringLiteral("ganttOutdent"), QStringLiteral("ganttAddTask"),
    };
    return ids;
}

QStringList KeyCommands::sequences(const QString &id) const {
    QStringList list;
    if (!registry().contains(id)) return list;
    for (const QKeySequence &sequence : registry().shortcuts(id)) list.append(sequence.toString(QKeySequence::PortableText));
    return list;
}

QVariantList KeyCommands::groups() const {
    using Area = ShortcutRegistry::Area;
    const QStringList phone = phoneCommands();
    QVariantList result;
    for (const Area area : {Area::General, Area::Map, Area::Outliner, Area::Gantt}) {
        QVariantList commands;
        for (const ShortcutRegistry::Command &command : registry().commands(area))
            if (phone.contains(command.id)) commands.append(QVariantMap{{QStringLiteral("id"), command.id}, {QStringLiteral("name"), command.name}});
        if (commands.isEmpty()) continue;
        result.append(QVariantMap{{QStringLiteral("name"), ShortcutRegistry::areaName(area)}, {QStringLiteral("commands"), commands}});
    }
    return result;
}

QStringList KeyCommands::keyTexts(const QString &id) const {
    const QList<QKeySequence> keys = registry().shortcuts(id);
    QStringList texts;
    for (int i = 0; i < ShortcutRegistry::kMaxShortcuts; ++i) texts.append(keys.value(i).toString(QKeySequence::NativeText));
    return texts;
}

bool KeyCommands::isDefault(const QString &id) const { return registry().isDefault(id); }

QString KeyCommands::shown(const QString &portable) const { return fromPortable(portable).toString(QKeySequence::NativeText); }

QString KeyCommands::recorded(int key, int modifiers) const {
    switch (key) {
    case Qt::Key_Shift: case Qt::Key_Control: case Qt::Key_Meta: case Qt::Key_Alt: case Qt::Key_AltGr:
    case Qt::Key_CapsLock: case Qt::Key_NumLock: case Qt::Key_ScrollLock: case Qt::Key_Escape: case Qt::Key_Back:
    case Qt::Key_unknown: case 0:
        return {};
    default: break;
    }
    Qt::KeyboardModifiers mods = Qt::KeyboardModifiers(modifiers) & ~Qt::KeypadModifier;
    if (key == Qt::Key_Backtab) { key = Qt::Key_Tab; mods |= Qt::ShiftModifier; }
    if (key == Qt::Key_Enter) key = Qt::Key_Return;
    return QKeySequence(QKeyCombination(mods, Qt::Key(key))).toString(QKeySequence::PortableText);
}

QString KeyCommands::conflictName(const QString &id, const QString &portable) const {
    const QKeySequence key = fromPortable(portable);
    if (key.isEmpty() || registry().shortcuts(id).contains(key)) return {};
    const QString other = registry().conflict(id, key);
    return other.isEmpty() ? QString() : registry().command(other).name;
}

void KeyCommands::setKey(const QString &id, int index, const QString &portable) {
    if (!registry().contains(id) || index < 0 || index >= ShortcutRegistry::kMaxShortcuts) return;
    const QKeySequence key = fromPortable(portable);
    if (!key.isEmpty() && !registry().shortcuts(id).contains(key)) {
        if (const QString other = registry().conflict(id, key); !other.isEmpty()) {
            QList<QKeySequence> remaining = registry().shortcuts(other);
            remaining.removeAll(key);
            registry().setShortcuts(other, remaining);  // the key moves to this command
        }
    }
    QList<QKeySequence> keys = registry().shortcuts(id);
    while (keys.size() < ShortcutRegistry::kMaxShortcuts) keys.append(QKeySequence());
    keys[index] = key;
    registry().setShortcuts(id, keys);
}

void KeyCommands::reset(const QString &id) { registry().reset(id); }

void KeyCommands::resetAll() { registry().resetAll(); }
