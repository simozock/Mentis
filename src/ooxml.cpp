#include "ooxml.h"

#include "zipwriter.h"

#include <QBuffer>

namespace Ooxml {

QString xmlSafe(const QString &text) {
    QString result;
    result.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
            result.append(c);
            result.append(text[++i]);
        } else if (!c.isSurrogate() && (c.unicode() >= 0x20 || c == u'\t' || c == u'\n' || c == u'\r') && c.unicode() != 0xFFFE && c.unicode() != 0xFFFF) {
            result.append(c);
        }
    }
    return result;
}

QByteArray png(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QByteArray part(const std::function<void(QXmlStreamWriter &)> &body) {
    QByteArray bytes;
    QXmlStreamWriter xml(&bytes);
    xml.writeStartDocument(QStringLiteral("1.0"), true);
    body(xml);
    xml.writeEndDocument();
    return bytes;
}

void relationships(QXmlStreamWriter &xml, const QList<Relationship> &list, int firstId) {
    xml.writeDefaultNamespace(kPackageRels);
    xml.writeStartElement(kPackageRels, QStringLiteral("Relationships"));
    int id = firstId;
    for (const Relationship &relationship : list) {
        xml.writeStartElement(kPackageRels, QStringLiteral("Relationship"));
        xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(id++));
        xml.writeAttribute(QStringLiteral("Type"), relationship.type.startsWith(QLatin1String("http")) ? relationship.type : kRelType + relationship.type);
        xml.writeAttribute(QStringLiteral("Target"), xmlSafe(relationship.target));
        if (relationship.external) xml.writeAttribute(QStringLiteral("TargetMode"), QStringLiteral("External"));
        xml.writeEndElement();
    }
    xml.writeEndElement();
}

void element(QXmlStreamWriter &xml, const QString &ns, const QString &name, const QList<std::pair<QString, QString>> &attributes) {
    xml.writeStartElement(ns, name);
    for (const auto &[key, value] : attributes) xml.writeAttribute(key, value);
    xml.writeEndElement();
}

void finish(ZipWriter &zip, const QString &mainPart, const QString &title, QStringList overrides) {
    zip.add(QStringLiteral("docProps/core.xml"), part([&](QXmlStreamWriter &xml) {
        const QString cp = QStringLiteral("http://schemas.openxmlformats.org/package/2006/metadata/core-properties");
        const QString dc = QStringLiteral("http://purl.org/dc/elements/1.1/");
        xml.writeNamespace(cp, QStringLiteral("cp"));
        xml.writeNamespace(dc, QStringLiteral("dc"));
        xml.writeStartElement(cp, QStringLiteral("coreProperties"));
        xml.writeTextElement(dc, QStringLiteral("title"), xmlSafe(title));
        xml.writeTextElement(dc, QStringLiteral("creator"), QStringLiteral("Mentis"));
        xml.writeEndElement();
    }));
    zip.add(QStringLiteral("docProps/app.xml"), part([](QXmlStreamWriter &xml) {
        const QString ep = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/extended-properties");
        xml.writeDefaultNamespace(ep);
        xml.writeStartElement(ep, QStringLiteral("Properties"));
        xml.writeTextElement(ep, QStringLiteral("Application"), QStringLiteral("Mentis"));
        xml.writeEndElement();
    }));
    zip.add(QStringLiteral("_rels/.rels"), part([&](QXmlStreamWriter &xml) {
        relationships(xml, {{QStringLiteral("officeDocument"), mainPart},
                            {QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties"), QStringLiteral("docProps/core.xml")},
                            {QStringLiteral("extended-properties"), QStringLiteral("docProps/app.xml")}});
    }));
    overrides += {QStringLiteral("/docProps/core.xml|application/vnd.openxmlformats-package.core-properties+xml"),
                  QStringLiteral("/docProps/app.xml|application/vnd.openxmlformats-officedocument.extended-properties+xml")};
    zip.add(QStringLiteral("[Content_Types].xml"), part([&](QXmlStreamWriter &xml) {
        const QString ct = QStringLiteral("http://schemas.openxmlformats.org/package/2006/content-types");
        xml.writeDefaultNamespace(ct);
        xml.writeStartElement(ct, QStringLiteral("Types"));
        for (const auto &[extension, type] : {std::pair{"rels", "application/vnd.openxmlformats-package.relationships+xml"},
                                              std::pair{"xml", "application/xml"}, std::pair{"png", "image/png"},
                                              std::pair{"jpeg", "image/jpeg"}})
            element(xml, ct, QStringLiteral("Default"), {{QStringLiteral("Extension"), QString::fromLatin1(extension)}, {QStringLiteral("ContentType"), QString::fromLatin1(type)}});
        for (const QString &entry : std::as_const(overrides)) {
            const qsizetype bar = entry.indexOf(QLatin1Char('|'));
            element(xml, ct, QStringLiteral("Override"), {{QStringLiteral("PartName"), entry.left(bar)}, {QStringLiteral("ContentType"), entry.mid(bar + 1)}});
        }
        xml.writeEndElement();
    }));
}

}  // namespace Ooxml
