#include "filevalidation.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

namespace FileValidation {
namespace {
QString sizeText(qint64 bytes) { return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat); }

// Esiste, e' un file, si apre e non e' vuoto; altrimenti il motivo.
QString basicProblem(const QString &path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) return QCoreApplication::translate("FileValidation", "The file does not exist.");
    if (info.size() == 0) return QCoreApplication::translate("FileValidation", "The file is empty.");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QCoreApplication::translate("FileValidation", "The file cannot be read.");
    return {};
}

void markLargeFile(Check &check, qint64 bytes) {
    if (bytes <= kLargeFileBytes) return;
    check.large = true;
    check.largeReason = QCoreApplication::translate("FileValidation", "The file is %1: it will make the document bigger and slower.").arg(sizeText(bytes));
}
}  // namespace

Check image(const QString &path) {
    Check check;
    if (const QString problem = basicProblem(path); !problem.isEmpty()) { check.reason = problem; return check; }
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);  // il contenuto, non l'estensione
    if (!reader.canRead()) {
        check.reason = QCoreApplication::translate("FileValidation", "This is not an image format Mentis can read.");
        return check;
    }
    const QSize size = reader.size();
    // Decodifica di prova a misura ridotta: scopre i file rovinati senza caricare tutta l'immagine.
    if (size.isValid() && !size.isEmpty()) reader.setScaledSize(size.scaled(64, 64, Qt::KeepAspectRatio).expandedTo(QSize(1, 1)));
    if (reader.read().isNull()) {
        check.reason = QCoreApplication::translate("FileValidation", "The image is damaged and cannot be shown.");
        return check;
    }
    check.ok = true;
    markLargeFile(check, QFileInfo(path).size());
    if (size.isValid() && qint64(size.width()) * size.height() > kLargeImagePixels) {
        check.large = true;
        check.largeReason = QCoreApplication::translate("FileValidation", "The image is %1x%2 pixels: it will make the document bigger and slower.")
                                .arg(size.width()).arg(size.height());
    }
    return check;
}

Check anyFile(const QString &path) {
    Check check;
    if (const QString problem = basicProblem(path); !problem.isEmpty()) { check.reason = problem; return check; }
    check.ok = true;
    markLargeFile(check, QFileInfo(path).size());
    return check;
}

QString imageFilter() {
    QStringList patterns;
    for (const QByteArray &format : QImageReader::supportedImageFormats()) {
        const QString suffix = QString::fromLatin1(format).toLower();
        if (suffix == QLatin1String("svgz") || suffix == QLatin1String("ico") || suffix == QLatin1String("cur")) continue;
        patterns.append(QStringLiteral("*.") + suffix);
    }
    if (patterns.contains(QStringLiteral("*.jpg")) && !patterns.contains(QStringLiteral("*.jpeg"))) patterns.append(QStringLiteral("*.jpeg"));
    return QCoreApplication::translate("FileValidation", "Images (%1)").arg(patterns.join(QLatin1Char(' ')));
}

QString diskFileName(const QString &name, int number) {
    static const QRegularExpression plainSuffix(QStringLiteral("^[A-Za-z0-9]{1,16}$"));
    const QString suffix = QFileInfo(name).suffix();
    return QStringLiteral("attach%1").arg(number, 2, 10, QLatin1Char('0'))
        + (plainSuffix.match(suffix).hasMatch() ? QLatin1Char('.') + suffix : QString());
}

}  // namespace FileValidation
