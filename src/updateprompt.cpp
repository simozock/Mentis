#include "updateprompt.h"

#include "modaldialog.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

UpdatePrompt::UpdatePrompt(QWidget *window) : QObject(window), m_window(window) {
    closeWindows = [] {
        QApplication::closeAllWindows();
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (widget->isVisible() && widget->isWindow() && !widget->inherits("QMenu")) return false;
        return true;
    };
    canInstall = &Updates::canInstall;
    startInstaller = &Updates::startInstaller;
}

void UpdatePrompt::checkAtStartup() {
    // A downloaded installer has done its work (or was abandoned): it is not kept.
    QDir(UpdateDownloader::defaultFolder()).removeRecursively();
    const Updates::Settings settings = Updates::Settings::load();
    if (!settings.checkAtStartup || !Updates::startupCheckDue(settings.lastCheck, QDateTime::currentDateTime())) {
        emit checkFinished();
        return;
    }
    check(false, m_window);
}

void UpdatePrompt::checkNow(QWidget *parent) { check(true, parent); }

void UpdatePrompt::check(bool manual, QWidget *parent) {
    if (m_checker) return;  // one check at a time
    m_checker = new UpdateChecker(this);
    QPointer<QWidget> guard(parent);
    connect(m_checker, &UpdateChecker::finished, this, [this, manual, guard](bool ok, const Updates::Release &release) {
        m_checker->deleteLater();
        m_checker.clear();  // a new check may start before the deferred delete runs
        QWidget *parent = guard ? guard.data() : m_window.data();
        if (ok) {
            Updates::Settings settings = Updates::Settings::load();
            settings.lastCheck = QDateTime::currentDateTime();
            settings.save();
        }
        if (!parent) {
            emit checkFinished();
            return;
        }
        if (!ok) {
            if (manual) ModalDialog::inform(parent, tr("Updates"), tr("Could not check for updates."));
        } else if (Updates::isNewer(release.version, Updates::currentVersion())
                   && (manual || release.version != Updates::Settings::load().skippedVersion)) {
            if (manual) propose(release, parent);
            else proposeWhenFree(release);
        } else if (manual) {
            ModalDialog::inform(parent, tr("Updates"), tr("Mentis is up to date."));
        }
        emit checkFinished();
    });
    m_checker->start(releaseUrl);
}

// The startup question waits while another modal or a menu is open (a file dialog, Settings).
void UpdatePrompt::proposeWhenFree(const Updates::Release &release) {
    if (!m_window) return;
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) {
        QTimer::singleShot(10000, this, [this, release] { proposeWhenFree(release); });
        return;
    }
    // Meanwhile "Check now" may have skipped this version.
    if (release.version != Updates::Settings::load().skippedVersion) propose(release, m_window);
}

void UpdatePrompt::propose(const Updates::Release &release, QWidget *parent) {
    // Only an installed Windows copy with the release's installer can update itself.
    const bool installable = canInstall() && !release.installerUrl.isEmpty() && !release.checksumsUrl.isEmpty();
    const QString text = tr("Mentis %1 is available. You have version %2.").arg(release.version, Updates::currentVersion());
    const ModalDialog::Choice choice =
        ModalDialog::ask(parent, tr("Update available"), text, installable ? tr("Update") : tr("Open release page"),
                         tr("Skip this version"), tr("Later"));
    if (choice == ModalDialog::Choice::Secondary) {
        Updates::Settings settings = Updates::Settings::load();
        settings.skippedVersion = release.version;
        settings.save();
    } else if (choice == ModalDialog::Choice::Primary) {
        if (installable) download(release, parent);
        else QDesktopServices::openUrl(release.pageUrl);
    }
}

void UpdatePrompt::download(const Updates::Release &release, QWidget *parent) {
    ModalDialog dialog(parent, tr("Downloading Mentis %1").arg(release.version), QString(), 420);
    dialog.acceptButton()->hide();
    dialog.acceptButton()->setDefault(false);
    auto *status = new QLabel(tr("Starting the download…"));
    status->setObjectName(QStringLiteral("UpdateProgress"));
    dialog.contentLayout()->addWidget(status);

    UpdateDownloader downloader(release, UpdateDownloader::defaultFolder());
    UpdateDownloader::Error result = UpdateDownloader::Error::Cancelled;
    QString installerPath;
    const QLocale locale;
    connect(&downloader, &UpdateDownloader::progress, &dialog, [status, &locale](qint64 received, qint64 total) {
        if (total > 0)
            status->setText(tr("%1 of %2").arg(locale.formattedDataSize(received), locale.formattedDataSize(total)));
        else
            status->setText(locale.formattedDataSize(received));
    });
    connect(&downloader, &UpdateDownloader::finished, &dialog,
            [&dialog, &result, &installerPath](UpdateDownloader::Error error, const QString &path) {
                result = error;
                installerPath = path;
                if (error == UpdateDownloader::Error::None) dialog.accept();
                else if (error != UpdateDownloader::Error::Cancelled) dialog.reject();
            });
    downloader.start();
    if (dialog.exec() != QDialog::Accepted && result == UpdateDownloader::Error::Cancelled) downloader.cancel();

    switch (result) {
    case UpdateDownloader::Error::None:
        install(installerPath, release.version, parent);
        break;
    case UpdateDownloader::Error::Damaged:
        ModalDialog::inform(parent, tr("Updates"), tr("The downloaded file is damaged. Try again later."));
        break;
    case UpdateDownloader::Error::Disk:
        ModalDialog::inform(parent, tr("Updates"), tr("The update could not be saved on this computer."));
        break;
    case UpdateDownloader::Error::Network:
        ModalDialog::inform(parent, tr("Updates"), tr("The download did not complete. Try again later."));
        break;
    case UpdateDownloader::Error::Cancelled:
        break;
    }
}

void UpdatePrompt::install(const QString &installerPath, const QString &version, QWidget *parent) {
    const ModalDialog::Choice choice = ModalDialog::ask(
        parent, tr("Update ready"),
        tr("Mentis will close, install version %1 and open again. Your maps are saved as when you quit.").arg(version),
        tr("Restart and install"), QString(), tr("Later"));
    if (choice != ModalDialog::Choice::Primary) {
        QFile::remove(installerPath);
        return;
    }
    // The app must not quit with the last window: the installer starts after the windows closed.
    const bool quitOnLastWindow = QGuiApplication::quitOnLastWindowClosed();
    QGuiApplication::setQuitOnLastWindowClosed(false);
    if (!closeWindows()) {  // the user kept a window open: nothing is installed
        QGuiApplication::setQuitOnLastWindowClosed(quitOnLastWindow);
        return;
    }
    // The documents are closed by now: on failure the message stands alone, then the app quits.
    if (!startInstaller(installerPath)) ModalDialog::inform(nullptr, tr("Updates"), tr("The installer could not be started."));
    QCoreApplication::quit();
}
