#pragma once

// OOXML package helpers (ECMA-376 Part 2, Open Packaging Conventions) shared by the .pptx of
// FEAT-063 and the .docx / .xlsx of FEAT-054 stage 4. No widgets.

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <QXmlStreamWriter>

#include <functional>
#include <utility>

class ZipWriter;

namespace Ooxml {

inline const QString kPackageRels = QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships");
inline const QString kRel = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");
inline const QString kRelType = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/");

// XML 1.0 allows no control characters but tab and line breaks (https://www.w3.org/TR/xml/#charsets).
QString xmlSafe(const QString &text);
QByteArray png(const QImage &image);

// A part written with QXmlStreamWriter: the declaration, then what body writes.
QByteArray part(const std::function<void(QXmlStreamWriter &)> &body);
// A relationship of a .rels part; a type without "http" is one of kRelType's.
struct Relationship {
    QString type;
    QString target;
    bool external = false;  // TargetMode="External" (a web address)
};
// A .rels part: rId<firstId>, rId<firstId + 1>... in order.
void relationships(QXmlStreamWriter &xml, const QList<Relationship> &list, int firstId = 1);
// An empty element with its attributes.
void element(QXmlStreamWriter &xml, const QString &ns, const QString &name, const QList<std::pair<QString, QString>> &attributes = {});

// The package's own parts: docProps/core.xml (title, creator Mentis), docProps/app.xml,
// _rels/.rels to mainPart and [Content_Types].xml. overrides: "/part|content type"; the defaults
// rels, xml, png and jpeg are always there.
void finish(ZipWriter &zip, const QString &mainPart, const QString &title, QStringList overrides);

}  // namespace Ooxml
