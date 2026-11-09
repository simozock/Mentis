#include "updates.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace Updates {

QString currentVersion() { return QStringLiteral(MENTIS_VERSION); }

bool parseVersion(const QString &text, int (&parts)[3]) {
    static const QRegularExpression pattern(QStringLiteral("^v?(\\d+)\\.(\\d+)\\.(\\d+)$"));
    const QRegularExpressionMatch match = pattern.match(text.trimmed());
    if (!match.hasMatch()) return false;
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        parts[i] = match.captured(i + 1).toInt(&ok);
        if (!ok) return false;  // too many digits
    }
    return true;
}

bool isNewer(const QString &candidate, const QString &current) {
    int a[3], b[3];
    if (!parseVersion(candidate, a) || !parseVersion(current, b)) return false;
    return std::lexicographical_compare(std::begin(b), std::end(b), std::begin(a), std::end(a));
}

QString installerFileName(const QString &version) { return QStringLiteral("Mentis-%1-setup-x64.exe").arg(version); }

Release releaseFromPage(const QUrl &page, bool *ok) {
    *ok = false;
    Release release;
    QString path = page.path();
    if (path.endsWith(QLatin1Char('/'))) path.chop(1);
    if (path.endsWith(QLatin1String("/releases"))) {  // the repository has no release yet
        *ok = true;
        return release;
    }
    static const QLatin1String marker("/releases/tag/");
    const qsizetype at = path.lastIndexOf(marker);
    if (at < 0) return release;
    const QString tag = path.mid(at + marker.size());
    int parts[3];
    if (!parseVersion(tag, parts)) return release;
    release.version = QStringLiteral("%1.%2.%3").arg(parts[0]).arg(parts[1]).arg(parts[2]);
    release.pageUrl = page;
    // https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases
    QUrl download = page;
    download.setQuery(QString());
    download.setFragment(QString());
    const QString downloadPath = path.left(at) + QStringLiteral("/releases/download/") + tag + QLatin1Char('/');
    release.installerName = installerFileName(release.version);
    download.setPath(downloadPath + release.installerName);
    release.installerUrl = download;
    download.setPath(downloadPath + QStringLiteral("SHA256SUMS"));
    release.checksumsUrl = download;
    *ok = true;
    return release;
}

Release releaseFromRedirect(const QUrl &asked, const QUrl &location, bool *ok) {
    const QUrl target = asked.resolved(location);
    *ok = false;
    // The installer and its checksums are fetched from the target's host: only the asked one.
    if (location.isEmpty() || target.scheme() != asked.scheme() || target.host() != asked.host()) return {};
    return releaseFromPage(target, ok);
}

QByteArray checksumFor(const QByteArray &sums, const QString &fileName) {
    const QByteArray name = fileName.toUtf8();
    for (QByteArray line : sums.split('\n')) {
        line = line.trimmed();
        const qsizetype space = line.indexOf(' ');
        if (space != 64) continue;
        QByteArray file = line.mid(space).trimmed();
        if (file.startsWith('*')) file.remove(0, 1);  // sha256sum's binary mode
        if (file == name) return line.left(64).toLower();
    }
    return {};
}

bool startupCheckDue(const QDateTime &lastCheck, const QDateTime &now) {
    return !lastCheck.isValid() || lastCheck > now || lastCheck.addDays(1) <= now;
}

bool canInstall() {
#ifdef Q_OS_WIN
    // installer/mentis.iss: the program in {app}/bin, the uninstaller in {app}.
    return QFileInfo::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/../unins000.exe"));
#else
    return false;
#endif
}

bool startInstaller(const QString &installerPath) {
    // Inno Setup's switches (https://jrsoftware.org/ishelp/topic_setupcmdline.htm); /RELAUNCH is
    // read by installer/mentis.iss to start Mentis again after a silent install.
    return QProcess::startDetached(installerPath, {QStringLiteral("/VERYSILENT"), QStringLiteral("/SUPPRESSMSGBOXES"),
                                                   QStringLiteral("/NORESTART"), QStringLiteral("/CLOSEAPPLICATIONS"),
                                                   QStringLiteral("/RELAUNCH=1")});
}

Settings Settings::load() {
    QSettings settings;
    Settings result;
    result.checkAtStartup = settings.value(QStringLiteral("updates/checkAtStartup"), true).toBool();
    result.lastCheck = settings.value(QStringLiteral("updates/lastCheck")).toDateTime();
    result.skippedVersion = settings.value(QStringLiteral("updates/skippedVersion")).toString();
    return result;
}

void Settings::save() const {
    QSettings settings;
    settings.setValue(QStringLiteral("updates/checkAtStartup"), checkAtStartup);
    // An invalid QDateTime does not come back invalid from every backend (the macOS plist did not,
    // GitHub CI 2026-11-02): no check is stored as no key.
    if (lastCheck.isValid()) settings.setValue(QStringLiteral("updates/lastCheck"), lastCheck);
    else settings.remove(QStringLiteral("updates/lastCheck"));
    settings.setValue(QStringLiteral("updates/skippedVersion"), skippedVersion);
}

}  // namespace Updates

namespace {

QNetworkRequest githubRequest(const QUrl &url) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mentis/%1").arg(Updates::currentVersion()));
    request.setTransferTimeout(30000);
    return request;
}

}  // namespace

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

QUrl UpdateChecker::defaultUrl() {
    return QUrl(QStringLiteral("https://github.com/simozock/Mentis/releases/latest"));
}

void UpdateChecker::start(const QUrl &url) {
    QNetworkRequest request = githubRequest(url);
    // Only the redirect's target is needed, not the release page's HTML.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        bool ok = false;
        Updates::Release release;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 300 && status < 400)
            release = Updates::releaseFromRedirect(
                reply->url(), reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl(), &ok);
        else if (reply->error() == QNetworkReply::NoError)  // a page served directly (file:// in tests)
            release = Updates::releaseFromPage(reply->url(), &ok);
        emit finished(ok, release);
    });
}

UpdateDownloader::UpdateDownloader(const Updates::Release &release, const QString &folder, QObject *parent)
    : QObject(parent), m_release(release), m_folder(folder), m_network(new QNetworkAccessManager(this)) {}

QString UpdateDownloader::defaultFolder() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/updates");
}

void UpdateDownloader::start() {
    if (m_release.installerUrl.isEmpty() || m_release.checksumsUrl.isEmpty()) {
        QMetaObject::invokeMethod(this, [this] { fail(Error::Network); }, Qt::QueuedConnection);
        return;
    }
    m_reply = m_network->get(githubRequest(m_release.checksumsUrl));
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply.data()] {
        reply->deleteLater();
        if (m_done) return;
        if (reply->error() != QNetworkReply::NoError) return fail(Error::Network);
        m_expectedHash = Updates::checksumFor(reply->readAll(), m_release.installerName);
        if (m_expectedHash.isEmpty()) return fail(Error::Damaged);
        downloadInstaller();
    });
}

void UpdateDownloader::downloadInstaller() {
    if (!QDir().mkpath(m_folder)) return fail(Error::Disk);
    m_file.setFileName(m_folder + QLatin1Char('/') + m_release.installerName);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail(Error::Disk);
    m_hash.reset();
    QNetworkRequest request = githubRequest(m_release.installerUrl);
    request.setRawHeader("Accept", "application/octet-stream");
    m_reply = m_network->get(request);
    // Written as it arrives: the installer is tens of megabytes, not kept in memory.
    connect(m_reply, &QNetworkReply::readyRead, this, [this, reply = m_reply.data()] {
        const QByteArray chunk = reply->readAll();
        m_hash.addData(chunk);
        if (m_file.write(chunk) != chunk.size()) fail(Error::Disk);
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, &UpdateDownloader::progress);
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply.data()] {
        reply->deleteLater();
        if (m_done) return;
        if (reply->error() != QNetworkReply::NoError) return fail(Error::Network);
        const QByteArray rest = reply->readAll();
        m_hash.addData(rest);
        if (m_file.write(rest) != rest.size() || !m_file.flush()) return fail(Error::Disk);
        m_file.close();
        if (m_hash.result().toHex() != m_expectedHash) return fail(Error::Damaged);
        m_done = true;
        emit finished(Error::None, m_file.fileName());
    });
}

void UpdateDownloader::cancel() { fail(Error::Cancelled); }

void UpdateDownloader::fail(Error error) {
    if (m_done) return;
    m_done = true;
    if (m_reply) m_reply->abort();
    if (m_file.isOpen()) m_file.close();
    if (!m_file.fileName().isEmpty()) m_file.remove();
    emit finished(error, QString());
}
