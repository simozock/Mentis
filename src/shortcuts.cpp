#include "shortcuts.h"

#include <QKeyEvent>
#include <QSettings>

namespace {
QString settingsKey(const QString &id) { return QStringLiteral("shortcuts/") + id; }
}  // namespace

ShortcutRegistry::ShortcutRegistry(QObject *parent) : QObject(parent) {}

QString ShortcutRegistry::areaName(Area area) {
    switch (area) {
    case Area::General: return tr("General");
    case Area::Map: return tr("Map");
    case Area::Outliner: return tr("Outliner");
    case Area::Gantt: return tr("Gantt");
    }
    return {};
}

int ShortcutRegistry::indexOf(const QString &id) const {
    for (int i = 0; i < m_commands.size(); ++i) if (m_commands[i].id == id) return i;
    return -1;
}

QList<QKeySequence> ShortcutRegistry::cleaned(const QList<QKeySequence> &shortcuts) {
    QList<QKeySequence> result;
    for (const QKeySequence &shortcut : shortcuts)
        if (!shortcut.isEmpty() && !result.contains(shortcut) && result.size() < kMaxShortcuts) result.append(shortcut);
    return result;
}

void ShortcutRegistry::add(const QString &id, Area area, const QString &name, const QList<QKeySequence> &defaults) {
    Command command{id, area, name, cleaned(defaults)};
    if (const int index = indexOf(id); index >= 0) m_commands[index] = command;
    else m_commands.append(command);
}

QStringList ShortcutRegistry::ids() const {
    QStringList result;
    for (const Command &command : m_commands) result.append(command.id);
    return result;
}

QList<ShortcutRegistry::Command> ShortcutRegistry::commands(Area area) const {
    QList<Command> result;
    for (const Command &command : m_commands) if (command.area == area) result.append(command);
    return result;
}

bool ShortcutRegistry::contains(const QString &id) const { return indexOf(id) >= 0; }

ShortcutRegistry::Command ShortcutRegistry::command(const QString &id) const {
    const int index = indexOf(id);
    return index >= 0 ? m_commands[index] : Command();
}

QList<QKeySequence> ShortcutRegistry::shortcuts(const QString &id) const {
    const int index = indexOf(id);
    if (index < 0) return {};
    const QSettings settings;
    const QString key = settingsKey(id);
    if (!settings.contains(key)) return m_commands[index].defaults;
    // Forma portabile ("Ctrl+S; Ctrl+K"); le chiavi di prima avevano un solo tasto e si leggono uguale.
    return cleaned(QKeySequence::listFromString(settings.value(key).toString(), QKeySequence::PortableText));
}

bool ShortcutRegistry::isDefault(const QString &id) const {
    const int index = indexOf(id);
    return index >= 0 && shortcuts(id) == m_commands[index].defaults;
}

void ShortcutRegistry::setShortcuts(const QString &id, const QList<QKeySequence> &shortcuts) {
    if (indexOf(id) < 0) return;
    const QList<QKeySequence> value = cleaned(shortcuts);
    if (QSettings().contains(settingsKey(id)) && value == this->shortcuts(id)) return;
    QSettings().setValue(settingsKey(id), QKeySequence::listToString(value, QKeySequence::PortableText));
    emit changed(id);
}

void ShortcutRegistry::reset(const QString &id) {
    if (indexOf(id) < 0 || !QSettings().contains(settingsKey(id))) return;
    QSettings().remove(settingsKey(id));
    emit changed(id);
}

void ShortcutRegistry::resetAll() {
    for (const Command &command : std::as_const(m_commands)) reset(command.id);
}

QString ShortcutRegistry::conflict(const QString &id, const QKeySequence &shortcut) const {
    const int index = indexOf(id);
    if (index < 0 || shortcut.isEmpty()) return {};
    const Area area = m_commands[index].area;
    for (const Command &other : m_commands) {
        if (other.id == id) continue;
        // Aree diverse convivono, tranne Generale, che vale in tutta la finestra.
        const bool overlapping = other.area == area || other.area == Area::General || area == Area::General;
        if (overlapping && shortcuts(other.id).contains(shortcut)) return other.id;
    }
    return {};
}

bool ShortcutRegistry::matches(const QString &id, const QKeyEvent *event) const {
    if (!event) return false;
    // Backtab arriva da Maiusc+Tab: si confronta come Maiusc+Tab, la forma salvata.
    Qt::Key key = Qt::Key(event->key());
    Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (key == Qt::Key_Backtab) { key = Qt::Key_Tab; modifiers |= Qt::ShiftModifier; }
    if (key == Qt::Key_Enter) key = Qt::Key_Return;  // Invio del tastierino come Invio
    const QKeySequence pressed(QKeyCombination(modifiers, key));
    return shortcuts(id).contains(pressed);
}
