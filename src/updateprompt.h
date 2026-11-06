#pragma once

#include "updates.h"

#include <QObject>
#include <QPointer>
#include <QWidget>
#include <functional>

class UpdateChecker;

// FEAT-090: the questions around an update, over the main window. The silent startup check, the
// "Check now" of Settings, the download with its progress, then closing the windows and starting
// the installer. The rules are in updates.h.
class UpdatePrompt : public QObject {
    Q_OBJECT
public:
    explicit UpdatePrompt(QWidget *window);

    // Once a day and only if turned on; says nothing unless a version to propose is found.
    void checkAtStartup();
    // From Settings: proposes even a skipped version and reports "up to date" and failures.
    // parent: the widget the answers appear over (the Settings dialog).
    void checkNow(QWidget *parent);

    // Closes the windows as on a normal exit (save questions included); false if one stays open.
    // The default closes every top-level window. Tests replace it.
    std::function<bool()> closeWindows;
    // Whether this copy installs updates and how; the defaults are Updates::canInstall and
    // Updates::startInstaller. Tests replace them.
    std::function<bool()> canInstall;
    std::function<bool(const QString &)> startInstaller;
    // The API address; tests point it to a file.
    QUrl releaseUrl = UpdateChecker::defaultUrl();

signals:
    void checkFinished();  // after any answer was shown, or none (tests wait for it)

private:
    void check(bool manual, QWidget *parent);
    void proposeWhenFree(const Updates::Release &release);
    void propose(const Updates::Release &release, QWidget *parent);
    void download(const Updates::Release &release, QWidget *parent);
    void install(const QString &installerPath, const QString &version, QWidget *parent);

    QPointer<QWidget> m_window;
    QPointer<UpdateChecker> m_checker;
};
