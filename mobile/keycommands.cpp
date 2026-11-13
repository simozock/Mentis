#include "keycommands.h"

#include "shortcutcatalog.h"

#include <QKeySequence>

KeyCommands::KeyCommands(QObject *parent) : QObject(parent) { ShortcutCatalog::addAll(m_registry); }

QStringList KeyCommands::sequences(const QString &id) const {
    QStringList list;
    if (!m_registry.contains(id)) return list;
    for (const QKeySequence &sequence : m_registry.shortcuts(id)) list.append(sequence.toString(QKeySequence::PortableText));
    return list;
}
