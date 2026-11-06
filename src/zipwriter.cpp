#include "zipwriter.h"

#include <array>

namespace {
void put16(QByteArray &out, quint16 value) {
    out.append(char(value & 0xff));
    out.append(char(value >> 8));
}

void put32(QByteArray &out, quint32 value) {
    put16(out, quint16(value & 0xffff));
    put16(out, quint16(value >> 16));
}

// A fixed date (1 January 2026, midnight) in MS-DOS form: the same file gives the same bytes.
constexpr quint16 kDosTime = 0;
constexpr quint16 kDosDate = ((2026 - 1980) << 9) | (1 << 5) | 1;
constexpr quint16 kVersion = 20;     // 2.0: the version that reads stored entries
constexpr quint16 kUtf8Names = 1 << 11;
}  // namespace

quint32 ZipWriter::crc32(const QByteArray &data) {
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> result{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            result[i] = c;
        }
        return result;
    }();
    quint32 crc = 0xffffffffu;
    for (const char byte : data) crc = table[(crc ^ quint8(byte)) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

void ZipWriter::add(const QString &path, const QByteArray &data) { m_entries.append({path.toUtf8(), data}); }

QByteArray ZipWriter::archive() const {
    QByteArray out;
    QByteArray directory;
    for (const Entry &entry : m_entries) {
        const quint32 offset = quint32(out.size());
        const quint32 crc = crc32(entry.data);
        const quint32 size = quint32(entry.data.size());
        // Local file header.
        put32(out, 0x04034b50);
        put16(out, kVersion);
        put16(out, kUtf8Names);
        put16(out, 0);  // stored
        put16(out, kDosTime);
        put16(out, kDosDate);
        put32(out, crc);
        put32(out, size);
        put32(out, size);
        put16(out, quint16(entry.name.size()));
        put16(out, 0);
        out.append(entry.name);
        out.append(entry.data);
        // Central directory record.
        put32(directory, 0x02014b50);
        put16(directory, kVersion);
        put16(directory, kVersion);
        put16(directory, kUtf8Names);
        put16(directory, 0);
        put16(directory, kDosTime);
        put16(directory, kDosDate);
        put32(directory, crc);
        put32(directory, size);
        put32(directory, size);
        put16(directory, quint16(entry.name.size()));
        put16(directory, 0);  // extra
        put16(directory, 0);  // comment
        put16(directory, 0);  // disk
        put16(directory, 0);  // internal attributes
        put32(directory, 0);  // external attributes
        put32(directory, offset);
        directory.append(entry.name);
    }
    const quint32 directoryOffset = quint32(out.size());
    out.append(directory);
    // End of central directory.
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, quint16(m_entries.size()));
    put16(out, quint16(m_entries.size()));
    put32(out, quint32(directory.size()));
    put32(out, directoryOffset);
    put16(out, 0);
    return out;
}
