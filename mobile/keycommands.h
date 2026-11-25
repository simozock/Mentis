#pragma once

#include "shortcuts.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// FEAT-097: the desktop's keys on a hardware keyboard (a tablet's, or one paired with the phone).
// The commands and their keys are the desktop's list (ShortcutCatalog) with the choices saved in
// the app's settings (FEAT-041); the map page binds a Shortcut to each command the phone has.
// FEAT-125: the Settings page edits them; every instance shares one registry, so a change reaches
// the map page's Shortcuts at once (revision).
class KeyCommands : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // Grows on every change of a key; bindings read it to follow the changes.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    explicit KeyCommands(QObject *parent = nullptr);
    // The commands the phone runs with a key (the map page's Shortcuts), in the catalog's order.
    static QStringList phoneCommands();

    int revision() const { return m_revision; }
    // The command's keys in portable form, for Shortcut.sequences; none for an unknown id.
    Q_INVOKABLE QStringList sequences(const QString &id) const;
    // The phone's commands by area: [{ name, commands: [{ id, name }] }], empty areas left out.
    Q_INVOKABLE QVariantList groups() const;
    // The command's keys as shown (native text), two entries ("" for none).
    Q_INVOKABLE QStringList keyTexts(const QString &id) const;
    Q_INVOKABLE bool isDefault(const QString &id) const;
    // A key in portable form as shown (native text).
    Q_INVOKABLE QString shown(const QString &portable) const;
    // The pressed key in portable form, "" for a modifier alone or a key that is not recorded
    // (Esc, Back: they stop recording). Shift+Tab and Enter as ShortcutRegistry::matches reads them.
    Q_INVOKABLE QString recorded(int key, int modifiers) const;
    // Name of the command that already uses the key (same area or General), "" when it is free or
    // already this command's.
    Q_INVOKABLE QString conflictName(const QString &id, const QString &portable) const;
    // Sets the command's key at index (0 or 1; "" clears it). A key another command uses moves here.
    Q_INVOKABLE void setKey(const QString &id, int index, const QString &portable);
    Q_INVOKABLE void reset(const QString &id);
    Q_INVOKABLE void resetAll();

signals:
    void changed();

private:
    static ShortcutRegistry &registry();
    int m_revision = 0;
};
