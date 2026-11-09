#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// FEAT-090: new versions from the latest GitHub release of simozock/Mentis. The rules (version
// compare, release and SHA256SUMS parsing, once a day) are free functions, tested without network;
// UpdateChecker and UpdateDownloader are the two network steps, the windows only ask and show.
namespace Updates {

// The running version, from project(Mentis VERSION ...) in CMakeLists.txt.
QString currentVersion();

// "1.2.3" or "v1.2.3": the three numbers; false for anything else (pre-release suffixes included).
bool parseVersion(const QString &text, int (&parts)[3]);
// True only if both are versions and candidate is greater.
bool isNewer(const QString &candidate, const QString &current);

struct Release {
    QString version;       // without "v"
    QUrl pageUrl;          // the release on GitHub
    QUrl installerUrl;     // Mentis-X.Y.Z-setup-x64.exe
    QString installerName;
    QUrl checksumsUrl;     // SHA256SUMS
};
// The release behind a ".../releases/tag/<tag>" page, the target of GitHub's /releases/latest
// redirect. The assets have fixed names (release.yml), so their ".../releases/download/<tag>/<name>"
// addresses are built, not read. A ".../releases" target (no release published): ok, empty release.
// ok = false for anything else, a tag that is not a version included.
Release releaseFromPage(const QUrl &page, bool *ok);
// releaseFromPage on a redirect's Location (relative or absolute); ok = false if it leaves the asked
// scheme and host, since the download addresses are built on it.
Release releaseFromRedirect(const QUrl &asked, const QUrl &location, bool *ok);
QString installerFileName(const QString &version);
// The hex SHA-256 of fileName in a sha256sum file ("<hash>  <name>" or "<hash> *<name>"); empty if absent.
QByteArray checksumFor(const QByteArray &sums, const QString &fileName);

// Whether the startup check runs: on, and no check in the last 24 hours (or a clock moved back).
bool startupCheckDue(const QDateTime &lastCheck, const QDateTime &now);

// The installer can update this copy: Windows, installed by Inno Setup (its uninstaller is next to
// the program). A portable or developer copy only gets the notice, like macOS and Linux.
bool canInstall();
// Starts the downloaded installer silently; it reopens Mentis when done (installer/mentis.iss).
bool startInstaller(const QString &installerPath);

struct Settings {
    bool checkAtStartup = true;
    QDateTime lastCheck;
    QString skippedVersion;
    static Settings load();
    void save() const;
};

}  // namespace Updates

// One request to github.com/.../releases/latest, read from its redirect, not to api.github.com: the
// API allows 60 unauthenticated requests per hour and IP, often used up on shared networks.
// finished exactly once, then the object can be deleted.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);
    static QUrl defaultUrl();
    void start(const QUrl &url = defaultUrl());

signals:
    // ok = false: no network, an error from GitHub or an answer that is not a release page. No
    // release published yet: ok with an empty release.
    void finished(bool ok, const Updates::Release &release);

private:
    QNetworkAccessManager *m_network = nullptr;
};

// Downloads SHA256SUMS, then the installer into folder, hashing while it writes. finished once:
// on success the path of the verified file; otherwise the file is removed and error says why.
class UpdateDownloader : public QObject {
    Q_OBJECT
public:
    enum class Error { None, Network, Damaged, Disk, Cancelled };
    UpdateDownloader(const Updates::Release &release, const QString &folder, QObject *parent = nullptr);
    void start();
    void cancel();
    static QString defaultFolder();

signals:
    void progress(qint64 received, qint64 total);
    void finished(UpdateDownloader::Error error, const QString &path);

private:
    void downloadInstaller();
    void fail(Error error);

    Updates::Release m_release;
    QString m_folder;
    QByteArray m_expectedHash;
    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    bool m_done = false;
};
