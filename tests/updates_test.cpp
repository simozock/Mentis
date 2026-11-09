// FEAT-090: update rules, the two network steps on file:// URLs and the questions of UpdatePrompt.
#include "modaldialog.h"
#include "updateprompt.h"
#include "updates.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {

void writeFile(const QString &path, const QByteArray &data) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}

// A release laid out like github.com: <root>/releases/tag/<tag> is the page releases/latest
// redirects to, the assets under <root>/releases/download/<tag>/. Returns the page's file:// URL.
QUrl releasePage(const QTemporaryDir &root, const QString &tag) {
    QDir(root.path()).mkpath(QStringLiteral("releases/tag"));
    QDir(root.path()).mkpath(QStringLiteral("releases/download/") + tag);
    const QString page = root.filePath(QStringLiteral("releases/tag/") + tag);
    writeFile(page, "<html></html>");
    return QUrl::fromLocalFile(page);
}

// Answers the modals as they open: title -> "primary" or "secondary". Others (the download) are left
// alone. A timer, not nested waits: each answer runs inside the modal's own event loop.
class AutoAnswer {
public:
    explicit AutoAnswer(QHash<QString, QString> answers) : m_answers(std::move(answers)) {
        QObject::connect(&m_timer, &QTimer::timeout, [this] {
            auto *modal = qobject_cast<ModalDialog *>(QApplication::activeModalWidget());
            if (!modal || modal->property("answered").toBool()) return;
            const QString title = modal->findChild<QLabel *>(QStringLiteral("ModalTitle"))->text();
            if (!m_answers.contains(title)) return;
            modal->setProperty("answered", true);
            seen.append(title);
            if (m_answers.value(title) == QLatin1String("secondary"))
                modal->findChild<QPushButton *>(QStringLiteral("ModalSecondary"))->click();
            else
                modal->acceptButton()->click();
        });
        m_timer.start(10);
    }
    QStringList seen;

private:
    QHash<QString, QString> m_answers;
    QTimer m_timer;
};

}  // namespace

class UpdatesTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("Mentis"));
        QCoreApplication::setApplicationName(QStringLiteral("MentisUpdatesTest"));
    }
    void init() { QSettings().clear(); }

    void versions() {
        QVERIFY(Updates::isNewer("0.2.0", "0.1.0"));
        QVERIFY(Updates::isNewer("v1.0.0", "0.9.9"));
        QVERIFY(Updates::isNewer("0.1.10", "0.1.9"));  // numbers, not text
        QVERIFY(!Updates::isNewer("0.1.0", "0.1.0"));
        QVERIFY(!Updates::isNewer("0.0.9", "0.1.0"));
        QVERIFY(!Updates::isNewer("1.0.0-beta", "0.1.0"));
        QVERIFY(!Updates::isNewer("nightly", "0.1.0"));
        QVERIFY(!Updates::isNewer("99999999999.0.0", "0.1.0"));
        int parts[3];
        QVERIFY(Updates::parseVersion(Updates::currentVersion(), parts));  // CMakeLists.txt's version
    }

    void releasePages() {
        bool ok = false;
        const Updates::Release release =
            Updates::releaseFromPage(QUrl("https://github.com/simozock/Mentis/releases/tag/v1.2.3"), &ok);
        QVERIFY(ok);
        QCOMPARE(release.version, QString("1.2.3"));
        QCOMPARE(release.installerName, QString("Mentis-1.2.3-setup-x64.exe"));
        QCOMPARE(release.installerUrl,
                 QUrl("https://github.com/simozock/Mentis/releases/download/v1.2.3/Mentis-1.2.3-setup-x64.exe"));
        QCOMPARE(release.checksumsUrl, QUrl("https://github.com/simozock/Mentis/releases/download/v1.2.3/SHA256SUMS"));
        QCOMPARE(release.pageUrl, QUrl("https://github.com/simozock/Mentis/releases/tag/v1.2.3"));

        // No release published: GitHub redirects to the release list.
        const Updates::Release none = Updates::releaseFromPage(QUrl("https://github.com/simozock/Mentis/releases"), &ok);
        QVERIFY(ok);
        QVERIFY(none.version.isEmpty());

        Updates::releaseFromPage(QUrl("https://github.com/simozock/Mentis/releases/tag/nightly"), &ok);
        QVERIFY(!ok);
        Updates::releaseFromPage(QUrl("https://github.com/simozock/Mentis/releases/tag/v2.0.0-beta"), &ok);
        QVERIFY(!ok);
        Updates::releaseFromPage(QUrl("https://github.com/login"), &ok);
        QVERIFY(!ok);

        // The redirect: relative or absolute on the same host; another host or scheme is refused.
        const QUrl asked("https://github.com/simozock/Mentis/releases/latest");
        QCOMPARE(Updates::releaseFromRedirect(asked, QUrl("/simozock/Mentis/releases/tag/v1.2.3"), &ok).version,
                 QString("1.2.3"));
        QVERIFY(ok);
        Updates::releaseFromRedirect(asked, QUrl("https://github.com/simozock/Mentis/releases/tag/v1.2.3"), &ok);
        QVERIFY(ok);
        Updates::releaseFromRedirect(asked, QUrl("https://example.invalid/simozock/Mentis/releases/tag/v1.2.3"), &ok);
        QVERIFY(!ok);
        Updates::releaseFromRedirect(asked, QUrl("http://github.com/simozock/Mentis/releases/tag/v1.2.3"), &ok);
        QVERIFY(!ok);
        Updates::releaseFromRedirect(asked, QUrl(), &ok);
        QVERIFY(!ok);
    }

    void checksums() {
        const QByteArray hash(64, 'a');
        const QByteArray sums = hash + "  Mentis-1.0.0-setup-x64.exe\n" + QByteArray(64, 'B') + " *other.zip\r\n";
        QCOMPARE(Updates::checksumFor(sums, "Mentis-1.0.0-setup-x64.exe"), hash);
        QCOMPARE(Updates::checksumFor(sums, "other.zip"), QByteArray(64, 'b'));
        QVERIFY(Updates::checksumFor(sums, "missing.exe").isEmpty());
        QVERIFY(Updates::checksumFor("short  Mentis-1.0.0-setup-x64.exe", "Mentis-1.0.0-setup-x64.exe").isEmpty());
    }

    void dailyRule() {
        const QDateTime now(QDate(2026, 11, 2), QTime(12, 0));
        QVERIFY(Updates::startupCheckDue(QDateTime(), now));
        QVERIFY(!Updates::startupCheckDue(now.addSecs(-3600), now));
        QVERIFY(Updates::startupCheckDue(now.addDays(-1), now));
        QVERIFY(Updates::startupCheckDue(now.addDays(3), now));  // clock moved back
    }

    void settingsDefaults() {
        const Updates::Settings settings = Updates::Settings::load();
        QVERIFY(settings.checkAtStartup);
        QVERIFY(!settings.lastCheck.isValid());
        QVERIFY(settings.skippedVersion.isEmpty());
    }

    void checkerReadsRelease() {
        QTemporaryDir dir;
        UpdateChecker checker;
        QSignalSpy finished(&checker, &UpdateChecker::finished);
        checker.start(releasePage(dir, "v3.0.0"));
        QVERIFY(finished.wait());
        QVERIFY(finished.at(0).at(0).toBool());
        QCOMPARE(finished.at(0).at(1).value<Updates::Release>().version, QString("3.0.0"));

        UpdateChecker missing;
        QSignalSpy failed(&missing, &UpdateChecker::finished);
        missing.start(QUrl::fromLocalFile(dir.filePath("releases/tag/v4.0.0")));
        QVERIFY(failed.wait());
        QVERIFY(!failed.at(0).at(0).toBool());
    }

    void downloaderVerifiesHash() {
        QTemporaryDir source;
        QTemporaryDir target;
        const QString name = Updates::installerFileName("3.0.0");
        const QByteArray installer(300000, 'x');
        writeFile(source.filePath(name), installer);
        Updates::Release release;
        release.version = "3.0.0";
        release.installerName = name;
        release.installerUrl = QUrl::fromLocalFile(source.filePath(name));
        release.checksumsUrl = QUrl::fromLocalFile(source.filePath("SHA256SUMS"));

        writeFile(source.filePath("SHA256SUMS"), QCryptographicHash::hash(installer, QCryptographicHash::Sha256).toHex() + "  " + name.toUtf8() + "\n");
        {
            UpdateDownloader downloader(release, target.path());
            QSignalSpy finished(&downloader, &UpdateDownloader::finished);
            downloader.start();
            QVERIFY(finished.wait());
            QCOMPARE(finished.at(0).at(0).value<UpdateDownloader::Error>(), UpdateDownloader::Error::None);
            const QString path = finished.at(0).at(1).toString();
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), installer);
        }

        writeFile(source.filePath("SHA256SUMS"), QByteArray(64, '0') + "  " + name.toUtf8() + "\n");
        {
            UpdateDownloader downloader(release, target.path());
            QSignalSpy finished(&downloader, &UpdateDownloader::finished);
            downloader.start();
            QVERIFY(finished.wait());
            QCOMPARE(finished.at(0).at(0).value<UpdateDownloader::Error>(), UpdateDownloader::Error::Damaged);
            QVERIFY(!QFile::exists(target.filePath(name)));  // a damaged file is not kept
        }

        writeFile(source.filePath("SHA256SUMS"), QByteArray(64, '0') + "  another-file.exe\n");
        {
            UpdateDownloader downloader(release, target.path());
            QSignalSpy finished(&downloader, &UpdateDownloader::finished);
            downloader.start();
            QVERIFY(finished.wait());
            QCOMPARE(finished.at(0).at(0).value<UpdateDownloader::Error>(), UpdateDownloader::Error::Damaged);
        }
    }

    // Startup: a skipped version and a check made today ask nothing.
    void startupRespectsSkipAndDay() {
        QTemporaryDir dir;
        QWidget window;
        UpdatePrompt prompt(&window);
        prompt.releaseUrl = releasePage(dir, "v99.0.0");
        QSignalSpy finished(&prompt, &UpdatePrompt::checkFinished);

        Updates::Settings settings;
        settings.skippedVersion = "99.0.0";
        settings.save();
        prompt.checkAtStartup();
        QVERIFY(finished.wait());
        QVERIFY(Updates::Settings::load().lastCheck.isValid());

        // Checked a moment ago: no request at all, finished at once.
        settings = Updates::Settings::load();
        settings.skippedVersion.clear();
        settings.save();
        finished.clear();
        prompt.checkAtStartup();
        QCOMPARE(finished.count(), 1);

        settings.checkAtStartup = false;
        settings.lastCheck = QDateTime();
        settings.save();
        finished.clear();
        prompt.checkAtStartup();
        QCOMPARE(finished.count(), 1);
        QVERIFY(!Updates::Settings::load().lastCheck.isValid());
    }

    // Startup with no answer (releases deleted, repository moved, no network): nothing is shown.
    void startupIsSilentOnErrors() {
        Updates::Settings settings = Updates::Settings::load();
        settings.checkAtStartup = true;  // the test before turns it off
        settings.lastCheck = QDateTime();  // due
        settings.save();
        QWidget window;
        window.resize(800, 600);
        window.show();
        UpdatePrompt prompt(&window);
        QTemporaryDir dir;
        prompt.releaseUrl = QUrl::fromLocalFile(dir.filePath("releases/tag/v99.0.0"));
        QSignalSpy finished(&prompt, &UpdatePrompt::checkFinished);
        prompt.checkAtStartup();
        QVERIFY(finished.wait());
        QApplication::processEvents();
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(window.findChildren<ModalDialog *>().isEmpty());
        QVERIFY(!Updates::Settings::load().lastCheck.isValid());  // no answer is not a check
    }

    // "Check now" proposes even a skipped version; "Skip this version" remembers it.
    void checkNowAndSkip() {
        QTemporaryDir dir;
        QWidget window;
        window.resize(800, 600);
        window.show();
        UpdatePrompt prompt(&window);
        prompt.releaseUrl = releasePage(dir, "v99.0.0");
        QSignalSpy finished(&prompt, &UpdatePrompt::checkFinished);

        Updates::Settings settings;
        settings.skippedVersion = "99.0.0";
        settings.save();
        {
            AutoAnswer answers({{"Update available", "secondary"}});
            prompt.checkNow(&window);
            QVERIFY(finished.wait());
            QCOMPARE(answers.seen, QStringList{"Update available"});
            QCOMPARE(Updates::Settings::load().skippedVersion, QString("99.0.0"));
        }

        // Up to date: a message.
        prompt.releaseUrl = releasePage(dir, "v" + Updates::currentVersion());
        AutoAnswer answers({{"Updates", "primary"}});
        prompt.checkNow(&window);
        QVERIFY(finished.wait());
        QCOMPARE(answers.seen, QStringList{"Updates"});
    }

    // The whole Windows path: Update, download, Restart and install; the windows are asked to close.
    void downloadAndInstall() {
        QTemporaryDir dir;
        const QString name = Updates::installerFileName("99.0.0");
        const QByteArray installer(1000, 'i');
        const QUrl page = releasePage(dir, "v99.0.0");
        const QString assets = dir.filePath("releases/download/v99.0.0/");
        writeFile(assets + name, installer);
        writeFile(assets + "SHA256SUMS", QCryptographicHash::hash(installer, QCryptographicHash::Sha256).toHex() + "  " + name.toUtf8() + "\n");
        QWidget window;
        window.resize(800, 600);
        window.show();
        UpdatePrompt prompt(&window);
        prompt.releaseUrl = page;
        prompt.canInstall = [] { return true; };
        bool windowsClosed = false;
        QString started;
        prompt.closeWindows = [&windowsClosed] { return windowsClosed; };
        prompt.startInstaller = [&started](const QString &path) {
            started = path;
            return true;
        };
        QSignalSpy finished(&prompt, &UpdatePrompt::checkFinished);

        // Update available -> Update; the download closes by itself; Update ready -> Restart and install.
        AutoAnswer answers({{"Update available", "primary"}, {"Update ready", "primary"}});
        // A window refuses to close: nothing starts and the app keeps quitting on its last window.
        prompt.checkNow(&window);
        QVERIFY(finished.wait());
        QCOMPARE(answers.seen, (QStringList{"Update available", "Update ready"}));
        QVERIFY(started.isEmpty());
        QVERIFY(QGuiApplication::quitOnLastWindowClosed());

        windowsClosed = true;
        prompt.checkNow(&window);
        QVERIFY(finished.wait());
        QCOMPARE(QFileInfo(started).fileName(), name);
        QFile file(started);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), installer);
        QGuiApplication::setQuitOnLastWindowClosed(true);
    }
};

QTEST_MAIN(UpdatesTest)
#include "updates_test.moc"
