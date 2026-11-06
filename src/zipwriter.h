#pragma once

// FEAT-063 stage C: a small ZIP writer for OOXML packages (.pptx), no new dependency. Entries are
// stored without compression: the images inside are already compressed PNGs and the XML is small,
// so deflate would save little (PKWARE APPNOTE 6.3.10, method 0:
// https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT).

#include <QByteArray>
#include <QList>
#include <QString>

class ZipWriter {
public:
    // Adds a file at path (forward slashes, no leading slash) with its contents.
    void add(const QString &path, const QByteArray &data);
    // The archive: the entries in the order added, then the central directory.
    QByteArray archive() const;

    static quint32 crc32(const QByteArray &data);

private:
    struct Entry {
        QByteArray name;
        QByteArray data;
    };
    QList<Entry> m_entries;
};
