#include "ganttnames.h"

#include "formulatext.h"
#include "ganttplan.h"
#include "mindmapmodel.h"
#include "nodegeometry.h"
#include "richtitle.h"

#include <QPainter>
#include <QTextCursor>
#include <QTextDocument>

namespace GanttNames {

namespace {

// A text and the format flags of its characters.
struct Flagged {
    QString text;
    QVector<quint8> flags;
};

// As QString::simplified: no spaces at the ends, each run of spaces one space (with the first one's
// flags).
Flagged simplified(const QString &text, const QVector<quint8> &flags) {
    Flagged out;
    bool space = false;
    quint8 spaceFlags = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isSpace()) {
            if (!out.text.isEmpty() && !space) {
                space = true;
                spaceFlags = flags.value(i);
            }
            continue;
        }
        if (space) {
            out.text += QLatin1Char(' ');
            out.flags.append(spaceFlags);
            space = false;
        }
        out.text += c;
        out.flags.append(flags.value(i));
    }
    return out;
}

// The shown name with the stored title's flags; no flags when there are none.
Flagged shown(const MindNode *topic) {
    Flagged out{GanttTask::shown(topic->title), {}};
    if (topic->titleHtml.isEmpty() || FormulaText::containsMath(topic->title)) return out;
    QTextDocument document;
    document.setHtml(RichTitle::withoutDocumentFont(topic->titleHtml));
    const int offset = OutlineFormats::titleOffsetIn(document.toPlainText(), topic->title);
    if (offset < 0) return out;
    const QVector<quint8> all = OutlineFormats::inlineFlagsPerChar(document, OutlineFormats::storedBoldWeights(topic).bold);
    Flagged mapped = simplified(topic->title, all.mid(offset, topic->title.size()));
    if (mapped.text != out.text) return out;
    if (std::none_of(mapped.flags.cbegin(), mapped.flags.cend(), [](quint8 value) { return value != 0; })) return out;
    return mapped;
}

}  // namespace

bool formattable(const MindNode *topic) { return topic && !FormulaText::containsMath(topic->title); }

OutlineFormats::BoldWeights rowWeights(bool boldRow) { return boldRow ? OutlineFormats::kHeavyBold : OutlineFormats::kNormalBold; }

QList<QTextLayout::FormatRange> ranges(const MindNode *topic, bool boldRow) {
    if (!topic) return {};
    const Flagged name = shown(topic);
    if (name.flags.isEmpty()) return {};
    return OutlineFormats::inlineRanges(name.flags, 0, int(name.text.size()), rowWeights(boldRow).bold);
}

void load(QTextDocument *field, const MindNode *topic, bool boldRow) {
    if (!field || !topic) return;
    field->setPlainText(GanttTask::shown(topic->title));
    QTextCursor cursor(field);
    if (boldRow) {  // a summary's text is bold, as its row: bold in it is Black
        cursor.select(QTextCursor::Document);
        QTextCharFormat regular;
        regular.setFontWeight(rowWeights(true).regular);
        cursor.mergeCharFormat(regular);
    }
    for (const QTextLayout::FormatRange &range : ranges(topic, boldRow)) {
        cursor.setPosition(range.start);
        cursor.setPosition(range.start + range.length, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(range.format);
    }
    field->setModified(false);
}

bool edited(const MindNode *topic, const QTextDocument &field, bool boldRow, QString *title, QString *html) {
    if (!topic) return false;
    const OutlineFormats::BoldWeights weights = rowWeights(boldRow);
    const Flagged typed = simplified(field.toPlainText(), OutlineFormats::inlineFlagsPerChar(field, weights.bold));
    if (typed.text.isEmpty()) return false;
    const Flagged before = shown(topic);
    if (!formattable(topic)) {
        if (typed.text == before.text) return false;
        *title = typed.text;
        html->clear();
        return true;
    }
    const bool sameFlags = before.flags.isEmpty()
                               ? std::none_of(typed.flags.cbegin(), typed.flags.cend(), [](quint8 value) { return value != 0; })
                               : typed.flags == before.flags;
    if (typed.text == before.text && sameFlags) return false;
    // The simplified name as a document with the typed formats, merged into the stored title.
    QTextDocument document;
    document.setPlainText(typed.text);
    QTextCursor cursor(&document);
    for (const QTextLayout::FormatRange &range : OutlineFormats::inlineRanges(typed.flags, 0, int(typed.text.size()), weights.bold)) {
        cursor.setPosition(range.start);
        cursor.setPosition(range.start + range.length, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(range.format);
    }
    *title = typed.text;
    *html = OutlineFormats::mergeOutlineFormats(topic->title, topic->titleHtml, document, NodeGeometry::titleFont(topic), weights.bold,
                                                OutlineFormats::storedBoldWeights(topic));
    return true;
}

qreal paint(QPainter *painter, const QRectF &rect, const QString &text, const QList<QTextLayout::FormatRange> &ranges) {
    if (text.isEmpty() || rect.width() <= 0) return 0;
    const auto layOut = [&](QTextLayout &layout, const QString &shown, qsizetype kept) {
        layout.setText(shown);
        layout.setFont(painter->font());
        QList<QTextLayout::FormatRange> clipped;
        for (QTextLayout::FormatRange range : ranges) {
            if (range.start >= kept) continue;
            range.length = int(std::min<qsizetype>(range.length, kept - range.start));
            clipped.append(range);
        }
        layout.setFormats(clipped);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        layout.setTextOption(option);
        layout.beginLayout();
        QTextLine line = layout.createLine();
        line.setLineWidth(1e6);
        layout.endLayout();
        return line.naturalTextWidth();
    };
    QTextLayout layout;
    qreal width = layOut(layout, text, text.size());
    if (width > rect.width()) {
        // The most characters that fit before "…" (formats change the width): a binary search.
        const QChar ellipsis(0x2026);
        qsizetype low = 0, high = text.size() - 1;
        while (low < high) {
            const qsizetype middle = (low + high + 1) / 2;
            if (layOut(layout, text.left(middle) + ellipsis, middle) <= rect.width()) low = middle;
            else high = middle - 1;
        }
        width = layOut(layout, text.left(low) + ellipsis, low);
    }
    const QTextLine line = layout.lineAt(0);
    painter->save();
    painter->setClipRect(rect, Qt::IntersectClip);
    layout.draw(painter, QPointF(rect.left(), rect.top() + (rect.height() - line.height()) / 2));
    painter->restore();
    return std::min(width, rect.width());
}

}  // namespace GanttNames
