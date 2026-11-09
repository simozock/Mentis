#include "richresources.h"

#include <QBuffer>
#include <QImageReader>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextFrame>
#include <QTextImageFormat>
#include <QTextTable>
#include <QUrl>

#include <algorithm>
#include <functional>

namespace RichResources {
namespace {

// Embedded in the HTML (data:) or one of the app's own resources (qrc): nothing to fetch.
bool isEmbedded(const QString &name) {
    return name.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive) || name.startsWith(QStringLiteral("qrc:"), Qt::CaseInsensitive)
           || name.startsWith(QStringLiteral(":/"));
}

// A path on this computer, not on a network share (reading those connects to the server).
QString localPath(const QString &name) {
    const QUrl url(name);
    QString path;
    if (url.isLocalFile()) path = url.toLocalFile();
    else if (url.scheme().isEmpty() || url.scheme().size() == 1) path = name;  // "C:/..." reads as scheme "c"
    if (path.startsWith(QStringLiteral("//")) || path.startsWith(QStringLiteral("\\\\"))) return {};
    return path;
}

QImage readImage(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.read();
}

// Clears a background image (CSS background-image, the background attribute) that is not embedded.
bool clearBackground(QTextFormat &format) {
    if (!format.hasProperty(QTextFormat::BackgroundImageUrl) || isEmbedded(format.stringProperty(QTextFormat::BackgroundImageUrl)))
        return false;
    format.clearProperty(QTextFormat::BackgroundImageUrl);
    return true;
}

// Removes from doc the images and background images that are not embedded; true when it changed.
bool removeExternal(QTextDocument &doc) {
    bool changed = false;
    const std::function<void(QTextFrame *)> frames = [&](QTextFrame *frame) {
        QTextFrameFormat frameFormat = frame->frameFormat();
        if (clearBackground(frameFormat)) { frame->setFrameFormat(frameFormat); changed = true; }
        if (auto *table = qobject_cast<QTextTable *>(frame))
            for (int row = 0; row < table->rows(); ++row)
                for (int column = 0; column < table->columns(); ++column) {
                    QTextTableCell cell = table->cellAt(row, column);
                    QTextCharFormat cellFormat = cell.format();
                    if (clearBackground(cellFormat)) { cell.setFormat(cellFormat); changed = true; }
                }
        for (QTextFrame *child : frame->childFrames()) frames(child);
    };
    frames(doc.rootFrame());
    struct Edit { int position; int length; QTextCharFormat format; };  // length 0: remove one image
    QList<Edit> edits;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat blockFormat = block.blockFormat();
        if (clearBackground(blockFormat)) { QTextCursor(block).setBlockFormat(blockFormat); changed = true; }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            QTextCharFormat format = fragment.charFormat();
            if (format.isImageFormat() && !isEmbedded(format.toImageFormat().name())) {
                for (int i = 0; i < fragment.length(); ++i) edits.append({fragment.position() + i, 0, {}});
                continue;
            }
            if (clearBackground(format)) edits.append({fragment.position(), fragment.length(), format});
        }
    }
    QTextCursor cursor(&doc);
    // From the end: removing an image does not move what is still to change.
    for (auto it = edits.crbegin(); it != edits.crend(); ++it) {
        cursor.setPosition(it->position);
        cursor.setPosition(it->position + qMax(1, it->length), QTextCursor::KeepAnchor);
        if (it->length == 0) cursor.removeSelectedText();
        else cursor.setCharFormat(it->format);
    }
    return changed || !edits.isEmpty();
}

}  // namespace

QString safeToParse(const QString &html) {
    // Qt reads linked and imported style sheets while it parses the HTML (measured 2026-11-09).
    static const QRegularExpression link(QStringLiteral("<\\s*link\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression style(QStringLiteral("<\\s*style\\b[^>]*>(.*?)(<\\s*/\\s*style\\s*>|$)"),
                                          QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QString result = html;
    result.remove(link);
    // Qt's own <style> block has no at-rule; one with @import (or any other) goes.
    for (qsizetype from = 0; result.contains(QLatin1Char('@'));) {
        const QRegularExpressionMatch match = style.match(result, from);
        if (!match.hasMatch()) break;
        if (match.captured(1).contains(QLatin1Char('@'))) result.remove(match.capturedStart(), match.capturedLength());
        else from = match.capturedEnd();
        if (match.capturedLength() == 0) break;
    }
    // BUG-048 (found by fuzzing): Qt lays nested tables out in a time that grows tenfold every four
    // levels (measured 2026-11-09: 12 levels 0.09 s, 16 levels 0.9 s, 20 levels over 30 s), so a
    // few hundred bytes in a title froze the app. Mentis makes no nested tables (a Markdown table
    // is one level): past kMaxTableNesting the tables become paragraphs and their text stays.
    static const QRegularExpression tableTag(QStringLiteral("<\\s*(/?)\\s*table\\b"), QRegularExpression::CaseInsensitiveOption);
    int depth = 0, deepest = 0;
    for (auto it = tableTag.globalMatch(result); it.hasNext() && deepest <= kMaxTableNesting;) {
        depth = it.next().captured(1).isEmpty() ? depth + 1 : std::max(0, depth - 1);
        deepest = std::max(deepest, depth);
    }
    if (deepest > kMaxTableNesting) {
        static const QRegularExpression structure(QStringLiteral("<\\s*/?\\s*(table|tr|tbody|thead|tfoot|caption|col|colgroup)\\b[^>]*>"),
                                                  QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression cellStart(QStringLiteral("<\\s*(td|th)\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression cellEnd(QStringLiteral("<\\s*/\\s*(td|th)\\s*>"), QRegularExpression::CaseInsensitiveOption);
        result.remove(structure);
        result.replace(cellStart, QStringLiteral("<p>"));
        result.replace(cellEnd, QStringLiteral("</p>"));
    }
    return result;
}

QString withoutExternal(const QString &html) {
    const QString parsable = safeToParse(html);
    // Fast path: most titles and notes have no image and no background (thousands at load).
    static const QRegularExpression resource(QStringLiteral("<\\s*img\\b|background\\s*=|url\\s*\\("), QRegularExpression::CaseInsensitiveOption);
    if (!resource.match(parsable).hasMatch()) return parsable;
    QTextDocument doc;
    doc.setHtml(parsable);
    return removeExternal(doc) ? doc.toHtml() : parsable;
}

QString dataUrl(const QImage &image) {
    if (image.isNull()) return {};
    constexpr int kLongestSide = 1024;
    const QImage scaled = std::max(image.width(), image.height()) > kLongestSide
        ? image.scaled(kLongestSide, kLongestSide, Qt::KeepAspectRatio, Qt::SmoothTransformation) : image;
    const bool alpha = scaled.hasAlphaChannel();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !scaled.save(&buffer, alpha ? "PNG" : "JPG", alpha ? -1 : 85)) return {};
    return QStringLiteral("data:image/%1;base64,").arg(alpha ? QStringLiteral("png") : QStringLiteral("jpeg"))
        + QString::fromLatin1(bytes.toBase64());
}

void embedImages(QTextDocument &doc, const QImage &clipboardImage) {
    struct Image { int position; QTextImageFormat format; };
    QList<Image> images;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.charFormat().isImageFormat()) continue;
            for (int i = 0; i < fragment.length(); ++i)
                images.append({fragment.position() + i, fragment.charFormat().toImageFormat()});
        }
    const auto isWeb = [](const Image &image) {
        return !isEmbedded(image.format.name()) && localPath(image.format.name()).isEmpty();
    };
    const bool oneWebImage = std::count_if(images.cbegin(), images.cend(), isWeb) == 1;
    QTextCursor cursor(&doc);
    // From the end, so that removing an image does not move the ones still to change.
    for (auto it = images.crbegin(); it != images.crend(); ++it) {
        const QString name = it->format.name();
        if (isEmbedded(name)) continue;
        const QString path = localPath(name);
        const QString embedded = !path.isEmpty() ? dataUrl(readImage(path))
                               : isWeb(*it) && oneWebImage ? dataUrl(clipboardImage) : QString();
        cursor.setPosition(it->position);
        cursor.setPosition(it->position + 1, QTextCursor::KeepAnchor);
        if (embedded.isEmpty()) { cursor.removeSelectedText(); continue; }
        QTextImageFormat format = it->format;
        format.setName(embedded);
        cursor.setCharFormat(format);
    }
}

}  // namespace RichResources
