#include "clipboardpaste.h"

#include "nodelinks.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace ClipboardPaste {

QString webAddressIn(const QString &text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QRegularExpression(QStringLiteral("\\s")))) return {};
    const QString lower = trimmed.toLower();
    const bool written = lower.startsWith(QStringLiteral("http://")) || lower.startsWith(QStringLiteral("https://")) ||
                         lower.startsWith(QStringLiteral("mailto:")) || lower.startsWith(QStringLiteral("www."));
    return written ? NodeLinks::webAddress(trimmed) : QString();
}

Content read(const QMimeData *mime, const QString &branchMimeType) {
    Content content;
    if (!mime) return content;
    if (mime->hasFormat(branchMimeType)) { content.kind = Kind::Branch; return content; }
    QString remoteAddress;
    if (mime->hasUrls()) {
        const QList<QUrl> urls = mime->urls();
        for (const QUrl &url : urls)
            if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) content.files.append(url.toLocalFile());
        if (!content.files.isEmpty()) { content.kind = Kind::Files; return content; }
        if (urls.size() == 1) remoteAddress = webAddressIn(urls.front().toString());
    }
    const QString text = mime->hasText() ? mime->text() : QString();
    const QString textAddress = webAddressIn(text);
    if (mime->hasImage() && (text.trimmed().isEmpty() || !textAddress.isEmpty())) {
        content.image = qvariant_cast<QImage>(mime->imageData());
        if (!content.image.isNull()) { content.kind = Kind::Image; return content; }
    }
    content.address = !textAddress.isEmpty() ? textAddress : (text.trimmed().isEmpty() ? remoteAddress : QString());
    if (!content.address.isEmpty()) { content.kind = Kind::WebLink; return content; }
    if (!text.isEmpty()) content.kind = Kind::Text;
    return content;
}

QString saveImage(const QImage &image) {
    if (image.isNull()) return {};
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    // One folder per content, so the image keeps a readable name (it becomes imageOriginalName).
    // Written again each time: a fresh date keeps it from the 30-day pruning until it is embedded.
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex().left(32));
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                           + QStringLiteral("/embedded-assets/pasted/") + hash;
    const QString path = folder + QStringLiteral("/pasted-image.png");
    if (!QDir().mkpath(folder)) return {};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(png) != png.size() || !file.commit()) return {};
    return path;
}

}  // namespace ClipboardPaste
