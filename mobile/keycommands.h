#pragma once

#include "shortcuts.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

// FEAT-097: the desktop's keys on a hardware keyboard (a tablet's, or one paired with the phone).
// The commands and their keys are the desktop's list (ShortcutCatalog) with the choices saved in
// the app's settings (FEAT-041); the map page binds a Shortcut to each command the phone has.
class KeyCommands : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    explicit KeyCommands(QObject *parent = nullptr);
    // The command's keys in portable form, for Shortcut.sequences; none for an unknown id.
    Q_INVOKABLE QStringList sequences(const QString &id) const;

private:
    ShortcutRegistry m_registry;
};
