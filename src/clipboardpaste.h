#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

class QMimeData;

// FEAT-094: what Ctrl+V on a selected topic does with the clipboard, without widgets (testable alone).
namespace ClipboardPaste {

enum class Kind {
    None,
    Branch,   // a branch copied in Mentis: pasted as a branch (unchanged behaviour)
    Files,    // files copied in the file manager: an image file becomes the image, the rest attachments
    Image,    // a bitmap (screenshot, "Copy image"): becomes the topic's image
    WebLink,  // one web address: becomes the topic's web link
    Text,     // any other text: pasted as Markdown topics (unchanged behaviour)
};

struct Content {
    Kind kind = Kind::None;
    QStringList files;  // Kind::Files: local paths
    QImage image;       // Kind::Image
    QString address;    // Kind::WebLink: the address as NodeLinks::webAddress gives it
};

// Reads the clipboard in this order: Mentis branch, local files, image, web address, text.
// An image that also carries text other than its own address (an Office table copies a picture too)
// stays text, as before.
Content read(const QMimeData *mime, const QString &branchMimeType);

// The address when the text is a single web address written with its scheme (http, https, mailto)
// or starting with "www."; empty for anything else, so a plain word is never taken as an address.
QString webAddressIn(const QString &text);

// Writes a pasted image as PNG under the embedded-assets cache (pruned after 30 days like the rest,
// main.cpp) until the next save embeds it; the same image gives the same path. Empty on failure.
QString saveImage(const QImage &image);

}  // namespace ClipboardPaste
