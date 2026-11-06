#pragma once

// Checks of the OOXML packages ZipWriter writes (.pptx of FEAT-063, .docx / .xlsx of FEAT-054).

#include "zipwriter.h"

#include <QDir>
#include <QMap>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace OoxmlTest {

// The parts of a stored ZIP (what ZipWriter writes), by name; empty if an entry is compressed or
// its CRC-32 is wrong.
inline QMap<QString, QByteArray> unzip(const QByteArray &zip) {
    QMap<QString, QByteArray> parts;
    const auto read16 = [&](qsizetype at) { return quint16(quint8(zip[at])) | quint16(quint8(zip[at + 1])) << 8; };
    const auto read32 = [&](qsizetype at) { return quint32(read16(at)) | quint32(read16(at + 2)) << 16; };
    qsizetype at = 0;
    while (at + 30 <= zip.size() && read32(at) == 0x04034b50) {
        if (read16(at + 8) != 0) return {};  // only stored entries
        const quint32 size = read32(at + 18);
        const quint16 nameLength = read16(at + 26);
        const quint16 extra = read16(at + 28);
        const QString name = QString::fromUtf8(zip.mid(at + 30, nameLength));
        const QByteArray data = zip.mid(at + 30 + nameLength + extra, size);
        if (ZipWriter::crc32(data) != read32(at + 14)) return {};
        parts.insert(name, data);
        at += 30 + nameLength + extra + size;
    }
    return parts;
}

inline bool wellFormed(const QByteArray &xml) {
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) reader.readNext();
    return !reader.hasError();
}

// Empty if every XML part is well formed, has its content type (or the xml default) and every
// internal relationship points at a part of the package; else what is wrong.
inline QString packageProblem(const QMap<QString, QByteArray> &parts) {
    if (parts.isEmpty()) return QStringLiteral("no parts");
    const QByteArray types = parts.value(QStringLiteral("[Content_Types].xml"));
    for (auto it = parts.begin(); it != parts.end(); ++it) {
        const bool xml = it.key().endsWith(QLatin1String(".xml")) || it.key().endsWith(QLatin1String(".rels"));
        if (xml && !wellFormed(it.value())) return it.key() + QStringLiteral(" is not well formed");
        if (it.key().endsWith(QLatin1String(".xml")) && !it.key().startsWith(QLatin1Char('['))
            && !types.contains("PartName=\"/" + it.key().toUtf8() + '"'))
            return it.key() + QStringLiteral(" has no content type");
        if (!it.key().endsWith(QLatin1String(".rels"))) continue;
        const QString base = it.key().section(QStringLiteral("_rels/"), 0, 0);
        static const QRegularExpression relation(QStringLiteral("<Relationship [^>]*Target=\"([^\"]+)\"([^>]*)>"));
        for (auto match = relation.globalMatch(QString::fromUtf8(it.value())); match.hasNext();) {
            const auto found = match.next();
            if (found.captured(2).contains(QLatin1String("External"))) continue;
            const QString target = QDir::cleanPath(base + found.captured(1));
            if (!parts.contains(target)) return it.key() + QStringLiteral(" -> ") + target + QStringLiteral(" is missing");
        }
    }
    return {};
}

}  // namespace OoxmlTest
