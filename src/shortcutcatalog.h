#pragma once

#include <QCoreApplication>

class ShortcutRegistry;

// FEAT-041, FEAT-097: every command of Mentis with its name and default keys, in the order the
// Settings list them. The desktop and the phone register the same list, so a key is the same on
// both; each front end runs the commands it has. The names keep the MainWindow context they were
// first translated in.
class ShortcutCatalog {
    Q_DECLARE_TR_FUNCTIONS(MainWindow)
public:
    static void addAll(ShortcutRegistry &registry);
};
