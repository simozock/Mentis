#include "richtitle.h"

#include <QKeyEvent>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace RichTitle {

namespace {
// Proprieta' del formato dei caratteri di Qt per ogni proprieta' di stile
// (https://doc.qt.io/qt-6/qtextformat.html#Property-enum).
QList<int> charProperties(StyleProperty property) {
    switch (property) {
    case StyleProperty::FontFamily: return {QTextFormat::FontFamilies, QTextFormat::FontStyleName};
    case StyleProperty::FontSize: return {QTextFormat::FontPointSize, QTextFormat::FontPixelSize, QTextFormat::FontSizeAdjustment};
    case StyleProperty::FontWeight: return {QTextFormat::FontWeight};
    case StyleProperty::Italic: return {QTextFormat::FontItalic};
    case StyleProperty::Underline: return {QTextFormat::FontUnderline, QTextFormat::TextUnderlineStyle};
    case StyleProperty::StrikeOut: return {QTextFormat::FontStrikeOut};
    case StyleProperty::TextColor: return {QTextFormat::ForegroundBrush};
    default: return {};
    }
}

// The title as a document: the formatted one, or the plain text.
void load(QTextDocument &document, const QString &title, const QString &html) {
    document.setUndoRedoEnabled(false);
    if (html.isEmpty()) document.setPlainText(title);
    else document.setHtml(withoutDocumentFont(html));
}

bool shows(const QTextCharFormat &format, CharStyle style, const BaseStyle &base, int boldWeight) {
    switch (style) {
    case CharStyle::Bold: return (format.hasProperty(QTextFormat::FontWeight) ? format.fontWeight() : base.weight) >= boldWeight;
    case CharStyle::Italic: return format.hasProperty(QTextFormat::FontItalic) ? format.fontItalic() : base.italic;
    case CharStyle::Underline: return format.hasProperty(QTextFormat::FontUnderline) ? format.fontUnderline() : base.underline;
    case CharStyle::StrikeOut: return format.hasProperty(QTextFormat::FontStrikeOut) ? format.fontStrikeOut() : base.strikeOut;
    }
    return false;
}
}  // namespace

bool hasEverywhere(const QString &title, const QString &html, CharStyle style, const BaseStyle &base, int boldWeight) {
    QTextDocument document;
    load(document, title, html);
    bool any = false;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) continue;
            if (!shows(fragment.charFormat(), style, base, boldWeight)) return false;
            any = true;
        }
    return any;
}

QString withEverywhere(const QString &title, const QString &html, CharStyle style, bool on, const BaseStyle &base, int boldWeight) {
    QTextDocument document;
    load(document, title, html);
    QTextCharFormat format;
    switch (style) {
    case CharStyle::Bold: format.setFontWeight(on ? boldWeight : base.weight); break;
    case CharStyle::Italic: format.setFontItalic(on); break;
    case CharStyle::Underline: format.setFontUnderline(on); break;
    case CharStyle::StrikeOut: format.setFontStrikeOut(on); break;
    }
    QTextCursor cursor(&document);
    cursor.select(QTextCursor::Document);
    cursor.mergeCharFormat(format);
    return withoutDocumentFont(document.toHtml());
}

bool PlainFirstTyping::type(const QKeyEvent *event, QTextCursor &cursor) {
    const int key = event->key();
    if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down ||
        key == Qt::Key_Home || key == Qt::Key_End || key == Qt::Key_PageUp || key == Qt::Key_PageDown)
        active = false;
    if (!active || event->text().isEmpty() || !event->text().front().isPrint() ||
        (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)))
        return false;
    active = false;
    // The selection goes first, in its own step: opening selects the whole title, and the empty
    // title in between is no history step (NodeItem::finishEditing).
    if (cursor.hasSelection()) cursor.removeSelectedText();
    cursor.insertText(event->text(), QTextCharFormat());
    return true;
}

bool isInlineProperty(StyleProperty property) { return !charProperties(property).isEmpty(); }

QString withoutDocumentFont(const QString &html) {
    if (html.isEmpty()) return html;
    static const QRegularExpression body(QStringLiteral("<body style=\"([^\"]*)\""));
    static const QRegularExpression font(QStringLiteral("\\s*font-(family|size|weight|style)\\s*:[^;]*;?"));
    const QRegularExpressionMatch match = body.match(html);
    if (!match.hasMatch()) return html;
    QString style = match.captured(1);
    style.remove(font);
    QString result = html;
    result.replace(match.capturedStart(1), match.capturedLength(1), style);
    return result;
}

QString withoutProperty(const QString &html, StyleProperty property) {
    const QList<int> properties = charProperties(property);
    if (html.isEmpty() || properties.isEmpty()) return html;
    QTextDocument document;
    document.setUndoRedoEnabled(false);
    document.setHtml(withoutDocumentFont(html));
    // Prima si raccolgono i tratti, poi si cambiano: cambiare il formato durante la scansione
    // unisce i frammenti uguali e invaliderebbe l'iteratore.
    struct Change { int position, length; QTextCharFormat format; };
    QList<Change> changes;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) continue;
            QTextCharFormat format = fragment.charFormat();
            bool had = false;
            for (const int id : properties)
                if (format.hasProperty(id)) { format.clearProperty(id); had = true; }
            if (had) changes.append({fragment.position(), fragment.length(), format});
        }
    if (changes.isEmpty()) return html;
    QTextCursor cursor(&document);
    for (const Change &change : std::as_const(changes)) {
        cursor.setPosition(change.position);
        cursor.setPosition(change.position + change.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(change.format);
    }
    return withoutDocumentFont(document.toHtml());
}

}  // namespace RichTitle
