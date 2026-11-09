#pragma once

#include <QImage>
#include <QString>

class QTextDocument;

// BUG-044: files named inside rich text (titles, notes). That HTML comes from the document, so it
// may be someone else's: <img src="//server/x.png"> made Windows connect to that server (sending
// the user's credentials) as soon as the map was drawn. Qt reads such files by itself, whatever
// resource provider is set (measured 2026-11-09, Qt 6.12): the HTML has to be cleaned first.
namespace RichResources {

// Tables inside tables (BUG-048): past this depth they become paragraphs.
inline constexpr int kMaxTableNesting = 4;

// html without what Qt would read while parsing it, <link> tags and <style> blocks with an at-rule
// (@import). When the tables Qt builds from it nest deeper than kMaxTableNesting, which Qt takes
// very long to lay out, only its plain text, in paragraphs. Safe to give to
// QTextDocument::setHtml; images are still there.
QString safeToParse(const QString &html);

// html with no external resource: safeToParse, then images and background images that are not
// embedded (data: URLs) or the app's own (qrc) removed. Unchanged when it has none.
QString withoutExternal(const QString &html);

// Image embedded in the HTML as a data: URL, at most 1024 px on its longest side (JPEG, or PNG
// when it has transparency); empty when the image is null.
QString dataUrl(const QImage &image);

// Pasting into a note: every image of doc becomes a data: URL so it stays in the map. A local file
// is read (not a network share); a web image takes the clipboard's own picture (clipboardImage),
// which the browser copies with it, when it is the only one; images that cannot be read are removed.
void embedImages(QTextDocument &doc, const QImage &clipboardImage);

}  // namespace RichResources
