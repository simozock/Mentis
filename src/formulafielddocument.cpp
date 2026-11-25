#include "formulafielddocument.h"

#include "formulatext.h"

#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace {
// The picture's character keeps the formula's source, delimiters included.
constexpr int kSourceProperty = QTextFormat::UserProperty + 116;

// A formula's $ written as \$: the parser reads it as text (FormulaText::split).
QString escaped(const QString &formula) {
    if (!formula.startsWith(QLatin1Char('$'))) return formula;  // \(...\) stays a formula
    const int open = formula.startsWith(QStringLiteral("$$")) ? 2 : 1;
    const QString delimiter = QStringLiteral("\\$").repeated(open);
    return delimiter + formula.mid(open, formula.size() - 2 * open) + delimiter;
}

// The text format of a picture's character: its font and colour, without the image.
QTextCharFormat textFormatOf(QTextCharFormat format) {
    for (const int property : {int(QTextFormat::ObjectType), int(QTextFormat::ImageName), int(QTextFormat::ImageWidth),
                               int(QTextFormat::ImageHeight), int(QTextFormat::TextVerticalAlignment), kSourceProperty})
        format.clearProperty(property);
    return format;
}
}  // namespace

void FormulaFieldDocument::setLook(const QFont &font, const QColor &color, qreal pixelRatio) {
    if (font == m_font && color == m_color && qFuzzyCompare(pixelRatio, m_pixelRatio)) return;
    m_font = font;
    m_color = color;
    m_pixelRatio = pixelRatio > 0 ? pixelRatio : 2;
    if (!m_document) return;
    // The pictures drawn again in place: the characters and the cursors stay where they are.
    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QString formula = fragment.charFormat().property(kSourceProperty).toString();
            if (formula.isEmpty()) continue;
            const QImage image = picture(formula);
            if (image.isNull()) continue;
            const QString name = QStringLiteral("formula-field://%1").arg(++m_counter);
            m_document->addResource(QTextDocument::ImageResource, QUrl(name), image);
            QTextImageFormat format = fragment.charFormat().toImageFormat();
            format.setName(name);
            format.setWidth(image.deviceIndependentSize().width());
            format.setHeight(image.deviceIndependentSize().height());
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            cursor.setCharFormat(format);
        }
    }
    cursor.endEditBlock();
}

FormulaFieldDocument::Flat FormulaFieldDocument::flatten(const QTextDocument *document, int cursor, int from, int to) {
    Flat flat;
    if (!document) return flat;
    int display = 0;
    const auto inside = [&] { return display >= from && (to < 0 || display < to); };
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        if (block != document->begin()) {
            if (display == cursor) flat.sourceCursor = int(flat.source.size());
            if (inside()) flat.source += QLatin1Char('\n');
            ++display;
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QString text = fragment.text();
            const QString formula = fragment.charFormat().property(kSourceProperty).toString();
            for (const QChar c : text) {
                if (display == cursor) flat.sourceCursor = int(flat.source.size());
                if (inside()) {
                    if (!formula.isEmpty() && c == QChar::ObjectReplacementCharacter) {
                        flat.pictures.append({int(flat.source.size()), int(flat.source.size() + formula.size())});
                        flat.source += formula;
                    } else {
                        flat.source += c;
                    }
                }
                ++display;
            }
        }
    }
    if (cursor >= 0 && flat.sourceCursor < 0) flat.sourceCursor = int(flat.source.size());
    return flat;
}

QString FormulaFieldDocument::sourceOf(const QTextDocument *document, const QString &preedit, int cursor) {
    Flat flat = flatten(document, cursor);
    if (!preedit.isEmpty() && flat.sourceCursor >= 0) flat.source.insert(flat.sourceCursor, preedit);
    return flat.source;
}

QString FormulaFieldDocument::sourceBetween(const QTextDocument *document, int from, int to) {
    return flatten(document, -1, std::min(from, to), std::max(from, to)).source;
}

QString FormulaFieldDocument::layoutSource(const QString &preedit, int cursor) const {
    Flat flat = flatten(m_document, cursor);
    if (!preedit.isEmpty() && flat.sourceCursor >= 0) {
        flat.source.insert(flat.sourceCursor, preedit);
        for (auto &range : flat.pictures)
            if (range.first >= flat.sourceCursor) range = {range.first + int(preedit.size()), range.second + int(preedit.size())};
    }
    // The formulas typed as code (not a picture) are escaped; from the end, so positions hold.
    const QVector<FormulaText::Segment> segments = FormulaText::split(flat.source);
    QString out = flat.source;
    for (auto segment = segments.crbegin(); segment != segments.crend(); ++segment) {
        if (!segment->math || flat.pictures.contains(qMakePair(segment->from, segment->to))) continue;
        out.replace(segment->from, segment->to - segment->from, escaped(flat.source.mid(segment->from, segment->to - segment->from)));
    }
    return out;
}

qreal FormulaFieldDocument::naturalWidth() const {
    qreal widest = 0;
    if (!m_document) return 0;
    const QFontMetricsF metrics(m_font);
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        qreal line = 0;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.charFormat().isImageFormat()) line += fragment.length() * fragment.charFormat().toImageFormat().width();
            else line += metrics.horizontalAdvance(fragment.text());
        }
        widest = std::max(widest, line);
    }
    return std::ceil(widest);
}

QImage FormulaFieldDocument::picture(const QString &formula) {
    const QString key = formula + QLatin1Char('\x1f') + m_font.key() + m_color.name(QColor::HexArgb) + QString::number(m_pixelRatio);
    if (auto found = m_pictures.constFind(key); found != m_pictures.cend()) return *found;
    FormulaText text;
    text.update(formula, m_font, m_color, 0);
    const QImage image = text.isEmpty() ? QImage() : text.toImage(m_pixelRatio);
    m_pictures.insert(key, image);
    return image;
}

void FormulaFieldDocument::apply(const Flat &flat, const QVector<QPair<int, int>> &pictures) {
    // The changes, from the end of the source: the document positions before each one hold.
    struct Change { QPair<int, int> range; bool toPicture; };
    QVector<Change> changes;
    for (const auto &range : flat.pictures)
        if (!pictures.contains(range)) changes.append({range, false});
    for (const auto &range : pictures)
        if (!flat.pictures.contains(range)) changes.append({range, true});
    std::sort(changes.begin(), changes.end(), [](const Change &a, const Change &b) { return a.range.first > b.range.first; });
    // A source position's place in the document: each picture before it is one character.
    const auto displayOf = [&flat](int at) {
        int display = at;
        for (const auto &range : flat.pictures)
            if (range.second <= at) display -= range.second - range.first - 1;
        return display;
    };
    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    for (const Change &change : changes) {
        const int at = displayOf(change.range.first);
        const QString formula = flat.source.mid(change.range.first, change.range.second - change.range.first);
        if (!change.toPicture) {
            cursor.setPosition(at);
            cursor.setPosition(at + 1, QTextCursor::KeepAnchor);
            cursor.insertText(formula, textFormatOf(cursor.charFormat()));
            continue;
        }
        cursor.setPosition(at + 1);
        QTextImageFormat format;
        format.merge(cursor.charFormat());  // the first character's font and colour
        cursor.setPosition(at);
        cursor.setPosition(at + int(formula.size()), QTextCursor::KeepAnchor);
        const QImage image = picture(formula);
        const QString name = QStringLiteral("formula-field://%1").arg(++m_counter);
        m_document->addResource(QTextDocument::ImageResource, QUrl(name), image);
        format.setName(name);
        format.setWidth(image.deviceIndependentSize().width());
        format.setHeight(image.deviceIndependentSize().height());
        format.setVerticalAlignment(QTextCharFormat::AlignMiddle);
        format.setProperty(kSourceProperty, formula);
        cursor.insertImage(format);
    }
    cursor.endEditBlock();
}

int FormulaFieldDocument::undo(bool redo) {
    if (!m_document) return -1;
    QTextCursor cursor(m_document);
    const QString before = sourceOf(m_document);
    bool undone = false;
    while (redo ? m_document->isRedoAvailable() : m_document->isUndoAvailable()) {
        if (redo) m_document->redo(&cursor);
        else m_document->undo(&cursor);
        undone = true;
        if (sourceOf(m_document) != before) break;
    }
    return undone ? cursor.position() : -1;
}

int FormulaFieldDocument::refresh(int cursor) {
    if (!m_document) return -1;
    const Flat flat = flatten(m_document, cursor);
    const int at = flat.sourceCursor;
    // The formulas away from the cursor become pictures, the one it touches is code. A formula
    // that overlaps a picture without being it (the text around changed how it reads) waits for
    // that picture to be code.
    QVector<QPair<int, int>> pictures;
    bool math = false;
    for (const FormulaText::Segment &segment : FormulaText::split(flat.source)) {
        if (!segment.math) continue;
        math = true;
        const QPair<int, int> range{segment.from, segment.to};
        if (at >= 0 && at >= segment.from && at <= segment.to) continue;
        if (!flat.pictures.contains(range)
            && std::any_of(flat.pictures.cbegin(), flat.pictures.cend(), [&range](const QPair<int, int> &picture) {
                   return picture.first < range.second && range.first < picture.second;
               }))
            continue;
        if (!picture(flat.source.mid(segment.from, segment.to - segment.from)).isNull()) pictures.append(range);
    }
    m_hasMath = math;
    if (pictures == flat.pictures) return -1;
    apply(flat, pictures);
    if (at < 0) return -1;
    int display = at;
    for (const auto &range : pictures)
        if (range.second <= at) display -= range.second - range.first - 1;
    return display;
}
