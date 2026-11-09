#include "noteeditor.h"

#include "markdownshortcuts.h"
#include "nodelinks.h"
#include "richresources.h"
#include "richtitle.h"
#include "uitheme.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QDesktopServices>
#include <QLineEdit>
#include <QUrl>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QTextList>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>
#include <functional>

namespace {
constexpr int kRadiusPx = 8;
constexpr int kHeadingSizePx = 18;
constexpr int kBodySizePx = 14;

// Icone Hugeicons (tratto scuro) ricolorate in chiaro per la barra scura.
QIcon lightIcon(const QString &file) {
    QPixmap pixmap = QIcon(QStringLiteral(":/assets/hugeicons/") + file).pixmap(40, 40);
    QPainter tint(&pixmap);
    tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tint.fillRect(pixmap.rect(), UiTheme::color(UiTheme::Token::Text));
    tint.end();
    return QIcon(pixmap);
}
}  // namespace


namespace {
// BUG-044: pasted images are embedded in the note (data: URLs), so they stay in the map and show
// on any computer, and nothing in the pasted HTML is read from a file or the network (RichResources).
class NoteTextEdit final : public QTextEdit {
public:
    using QTextEdit::QTextEdit;

protected:
    bool canInsertFromMimeData(const QMimeData *source) const override {
        return source->hasImage() || QTextEdit::canInsertFromMimeData(source);
    }
    void insertFromMimeData(const QMimeData *source) override {
        const QImage picture = source->hasImage() ? qvariant_cast<QImage>(source->imageData()) : QImage();
        if (source->hasHtml()) {
            QTextDocument pasted;
            pasted.setHtml(RichResources::safeToParse(source->html()));
            RichResources::embedImages(pasted, picture);
            pasted.setHtml(RichResources::withoutExternal(pasted.toHtml()));  // backgrounds too, before it is drawn
            textCursor().insertFragment(QTextDocumentFragment(&pasted));
        } else if (const QString image = RichResources::dataUrl(picture); !image.isEmpty()) {
            textCursor().insertImage(image);
        } else {
            QTextEdit::insertFromMimeData(source);
            return;
        }
        ensureCursorVisible();
    }
};
}  // namespace

NoteEditor::NoteEditor(QWidget *parent, Mode mode) : QFrame(parent), m_mode(mode) {
    setObjectName(QStringLiteral("NoteEditor"));
    setAttribute(Qt::WA_TranslucentBackground);
    const bool inlineMode = mode == Mode::Inline;
    if (!inlineMode) setFixedWidth(kWidthPx);
    setStyleSheet(inlineMode ? QStringLiteral("QFrame#NoteEditor { background:transparent; } QTextEdit { background:transparent; border:0; }")
                             : UiTheme::qss(QStringLiteral(
        "QFrame#NoteEditor { background:transparent; }"
        "QToolButton { background:transparent; border:0; border-radius:4px; }"
        "QToolButton:hover { background:@divider; }"
        "QToolButton:checked { background:@surfaceRaised; }"
        "QLineEdit#NoteLinkField { background:@surfaceSunken; color:@text; border:1px solid @surfaceSunken; border-radius:4px;"
        " min-height:28px; padding:0 8px; font-size:13px; }"
        "QLineEdit#NoteLinkField:focus { border-color:@accent; }"
        "QTextEdit { background:transparent; border:0; color:@text; font-size:14px;"
        " selection-background-color:@accent; selection-color:@text; }"
        "QScrollBar:vertical { background:transparent; width:8px; margin:2px; }"
        "QScrollBar::handle:vertical { background:@surfaceRaised; border-radius:2px; min-height:24px; }"
        "QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background:transparent; height:0; }")));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 16);  // dentro la superficie disegnata (4 px per l'ombra)
    layout->setSpacing(4);
    // Bottoni e campo del collegamento: nel riquadro scuro, oppure nella barra chiara flottante.
    QVBoxLayout *hostLayout = layout;
    m_buttonHost = this;
    if (inlineMode) {
        m_toolbarFrame = new QFrame(parent);
        m_toolbarFrame->setObjectName(QStringLiteral("NoteToolbar"));
        // Come i gruppi flottanti chiari della mappa (AGENTS.md).
        m_toolbarFrame->setStyleSheet(QStringLiteral(
            "QFrame#NoteToolbar { background:rgba(255,255,255,245); border:1px solid rgba(15,23,42,25); border-radius:8px; }"
            "QToolButton { background:transparent; border:0; border-radius:4px; color:#202630; }"
            "QToolButton:hover, QToolButton:checked { background:rgba(15,23,42,15); }"
            "QLineEdit#NoteLinkField { background:#ffffff; color:#202630; border:1px solid #c4cad2; border-radius:4px;"
            " min-height:26px; padding:0 6px; font-size:13px; }"
            "QLineEdit#NoteLinkField:focus { border-color:#7047eb; }"));
        hostLayout = new QVBoxLayout(m_toolbarFrame);
        hostLayout->setContentsMargins(4, 4, 4, 4);
        hostLayout->setSpacing(4);
        m_buttonHost = m_toolbarFrame;
        m_toolbarFrame->hide();
    }
    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(2);
    hostLayout->addLayout(toolbar);
    // FEAT-059: riga del collegamento (nascosta finche' non si preme la catena).
    m_linkRow = new QWidget(m_buttonHost);
    auto *linkLayout = new QHBoxLayout(m_linkRow);
    linkLayout->setContentsMargins(0, 0, 0, 4);
    linkLayout->setSpacing(4);
    m_linkField = new QLineEdit(m_linkRow);
    m_linkField->setObjectName(QStringLiteral("NoteLinkField"));
    m_linkField->setPlaceholderText(tr("Enter the link address"));
    auto *confirm = new QToolButton(m_linkRow);
    confirm->setObjectName(QStringLiteral("NoteLinkConfirm"));
    confirm->setText(QStringLiteral("✓"));
    confirm->setFixedSize(28, 28);
    confirm->setToolTip(tr("Apply link"));
    linkLayout->addWidget(m_linkField, 1);
    linkLayout->addWidget(confirm);
    m_linkRow->hide();
    m_linkField->installEventFilter(this);  // Esc chiude solo il campo
    connect(m_linkField, &QLineEdit::returnPressed, this, [this] { applyLink(m_linkField->text()); });
    connect(confirm, &QToolButton::clicked, this, [this] { applyLink(m_linkField->text()); });
    hostLayout->addWidget(m_linkRow);
    m_text = new NoteTextEdit(this);
    m_text->setObjectName(QStringLiteral("NoteText"));
    m_text->setAcceptRichText(true);
    m_text->setPlaceholderText(tr("Write a note"));
    m_text->setTabChangesFocus(false);
    m_text->installEventFilter(this);
    m_text->viewport()->installEventFilter(this);  // Ctrl+clic su un collegamento
    layout->addWidget(m_text, 1);

    const auto charButton = [this, toolbar](const QString &name, const QString &icon, const QString &tip,
                                            const std::function<void(QTextCharFormat &, bool)> &apply) {
        QToolButton *button = addButton(name, icon, tip, true);
        connect(button, &QToolButton::clicked, this, [this, apply](bool on) {
            QTextCharFormat format;
            apply(format, on);
            m_text->mergeCurrentCharFormat(format);
            m_text->setFocus();
        });
        toolbar->addWidget(button);
    };
    charButton(QStringLiteral("bold"), QStringLiteral("text-bold.svg"), tr("Bold (Ctrl+B)"),
               [](QTextCharFormat &format, bool on) { format.setFontWeight(on ? QFont::Bold : QFont::Normal); });
    charButton(QStringLiteral("italic"), QStringLiteral("text-italic.svg"), tr("Italic (Ctrl+I)"),
               [](QTextCharFormat &format, bool on) { format.setFontItalic(on); });
    charButton(QStringLiteral("underline"), QStringLiteral("text-underline.svg"), tr("Underline (Ctrl+U)"),
               [](QTextCharFormat &format, bool on) { format.setFontUnderline(on); });
    charButton(QStringLiteral("strike"), QStringLiteral("text-strikethrough.svg"), tr("Strikethrough"),
               [](QTextCharFormat &format, bool on) { format.setFontStrikeOut(on); });
    // FEAT-068: evidenziato e codice, accesi e spenti dai bottoni (il Markdown li mette scrivendo).
    const auto toggleButton = [this, toolbar](const QString &name, const QString &icon, const QString &tip,
                                              void (*apply)(QTextCursor &, bool)) {
        QToolButton *button = addButton(name, icon, tip, true);
        connect(button, &QToolButton::clicked, this, [this, apply](bool on) {
            QTextCursor cursor = m_text->textCursor();
            apply(cursor, on);
            m_text->setTextCursor(cursor);
            m_text->setFocus();
            updateButtons();
        });
        toolbar->addWidget(button);
    };
    toggleButton(QStringLiteral("highlight"), QStringLiteral("text-highlight.svg"), tr("Highlight"), &MarkdownShortcuts::setHighlight);
    toggleButton(QStringLiteral("code"), QStringLiteral("text-code.svg"), tr("Code"), &MarkdownShortcuts::setCode);
    toolbar->addSpacing(8);
    const auto listButton = [this, toolbar](const QString &name, const QString &icon, const QString &tip, QTextListFormat::Style style) {
        QToolButton *button = addButton(name, icon, tip, true);
        connect(button, &QToolButton::clicked, this, [this, style](bool on) {
            QTextCursor cursor = m_text->textCursor();
            if (on) {
                QTextListFormat format;
                format.setStyle(style);
                cursor.createList(format);
            } else if (QTextList *list = cursor.currentList()) {
                list->remove(cursor.block());
                QTextBlockFormat block = cursor.blockFormat();
                block.setIndent(0);
                cursor.setBlockFormat(block);
            }
            m_text->setFocus();
            updateButtons();
        });
        toolbar->addWidget(button);
    };
    listButton(QStringLiteral("bullets"), QStringLiteral("outliner.svg"), tr("Bulleted list"), QTextListFormat::ListDisc);
    listButton(QStringLiteral("numbers"), QStringLiteral("list-number.svg"), tr("Numbered list"), QTextListFormat::ListDecimal);
    QToolButton *heading = addButton(QStringLiteral("heading"), QStringLiteral("heading.svg"), tr("Heading"), true);
    connect(heading, &QToolButton::clicked, this, [this](bool on) {
        // Titolo = riga in grassetto 18 px (heading level 2 nell'HTML, "##" in Markdown).
        QTextCursor cursor = m_text->textCursor();
        cursor.beginEditBlock();  // one step of undo
        QTextBlockFormat block = cursor.blockFormat();
        block.setHeadingLevel(on ? 2 : 0);
        cursor.setBlockFormat(block);
        // Each piece keeps its format, with the size in pixels instead of any point size: a point size
        // of 0 (as before) is invalid and Qt warned at every drawing of the note.
        const QTextBlock line = cursor.block();
        QList<std::pair<int, QTextCharFormat>> parts;
        for (auto fragment = line.begin(); !fragment.atEnd(); ++fragment)
            parts.append({fragment.fragment().position(), fragment.fragment().charFormat()});
        for (int i = 0; i < parts.size(); ++i) {
            QTextCharFormat format = parts[i].second;
            format.setFontWeight(on ? QFont::Bold : QFont::Normal);
            format.clearProperty(QTextFormat::FontPointSize);
            format.setProperty(QTextFormat::FontPixelSize, on ? kHeadingSizePx : kBodySizePx);
            cursor.setPosition(parts[i].first);
            cursor.setPosition(i + 1 < parts.size() ? parts[i + 1].first : line.position() + line.length() - 1, QTextCursor::KeepAnchor);
            cursor.setCharFormat(format);
        }
        cursor.endEditBlock();
        QTextCharFormat typing = m_text->currentCharFormat();  // what is typed next on the line
        typing.setFontWeight(on ? QFont::Bold : QFont::Normal);
        typing.clearProperty(QTextFormat::FontPointSize);
        typing.setProperty(QTextFormat::FontPixelSize, on ? kHeadingSizePx : kBodySizePx);
        m_text->setCurrentCharFormat(typing);
        m_text->setFocus();
    });
    toolbar->addWidget(heading);
    // FEAT-059: collegamento a una pagina web sul testo selezionato.
    QToolButton *link = addButton(QStringLiteral("link"), QStringLiteral("link.svg"), tr("Link (Ctrl+click to open)"), false);
    connect(link, &QToolButton::clicked, this, [this] {
        const QTextCharFormat format = m_text->textCursor().charFormat();
        m_linkField->setText(format.isAnchor() ? format.anchorHref() : QString());
        m_linkRow->show();
        m_linkField->setFocus();
        m_linkField->selectAll();
        fitHeight();
    });
    toolbar->addWidget(link);
    if (!inlineMode) toolbar->addStretch();

    connect(m_text, &QTextEdit::cursorPositionChanged, this, &NoteEditor::updateButtons);
    connect(m_text, &QTextEdit::selectionChanged, this, &NoteEditor::updateButtons);  // anche Seleziona tutto
    connect(m_text, &QTextEdit::currentCharFormatChanged, this, &NoteEditor::updateButtons);
    connect(m_text->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, &NoteEditor::fitHeight);
    hide();
}

NoteEditor::~NoteEditor() { qApp->removeEventFilter(this); }

QToolButton *NoteEditor::addButton(const QString &name, const QString &icon, const QString &tip, bool checkable) {
    auto *button = new QToolButton(m_buttonHost);
    button->setObjectName(QStringLiteral("NoteFormat_") + name);
    // Barra scura: icone chiare; barra chiara flottante (Inline): icone scure come sono.
    button->setIcon(m_mode == Mode::Inline ? QIcon(QStringLiteral(":/assets/hugeicons/") + icon) : lightIcon(icon));
    button->setIconSize(QSize(16, 16));
    button->setFixedSize(28, 28);
    button->setCheckable(checkable);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    button->setFocusPolicy(Qt::NoFocus);
    m_buttons.append(button);
    return button;
}

QToolButton *NoteEditor::formatButton(const QString &name) const {
    return m_buttonHost->findChild<QToolButton *>(QStringLiteral("NoteFormat_") + name);
}

const QColor &NoteEditor::linkColor() {
    static const QColor color("#4f8cff");  // leggibile sul riquadro scuro e sulla mappa chiara
    return color;
}

void NoteEditor::applyLink(const QString &typed) {
    const QString address = typed.trimmed().isEmpty() ? QString() : NodeLinks::webAddress(typed);
    QTextCursor cursor = m_text->textCursor();
    if (!cursor.hasSelection() && cursor.charFormat().isAnchor()) {
        // Cursore dentro un collegamento: si seleziona tutto il collegamento.
        const QString href = cursor.charFormat().anchorHref();
        while (cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor) && cursor.charFormat().anchorHref() == href) {}
        cursor.clearSelection();
        cursor.movePosition(QTextCursor::NextCharacter);
        while (cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor) && cursor.charFormat().anchorHref() == href) {}
    }
    QTextCharFormat format;
    if (address.isEmpty()) {
        format.setAnchor(false);
        format.setAnchorHref(QString());
        format.setFontUnderline(false);
        format.clearForeground();
        cursor.mergeCharFormat(format);
    } else {
        format.setAnchor(true);
        format.setAnchorHref(address);
        format.setFontUnderline(true);
        format.setForeground(linkColor());
        if (cursor.hasSelection()) cursor.mergeCharFormat(format);
        else cursor.insertText(address, format);
    }
    m_linkRow->hide();
    m_text->setFocus();
    fitHeight();
}

void NoteEditor::updateButtons() {
    const QTextCharFormat format = m_text->currentCharFormat();
    const QTextCursor cursor = m_text->textCursor();
    const auto set = [this](const QString &name, bool on) {
        if (QToolButton *button = formatButton(name)) { const QSignalBlocker blocker(button); button->setChecked(on); }
    };
    set(QStringLiteral("bold"), format.fontWeight() >= QFont::Bold);
    set(QStringLiteral("italic"), format.fontItalic());
    set(QStringLiteral("underline"), format.fontUnderline());
    set(QStringLiteral("strike"), format.fontStrikeOut());
    set(QStringLiteral("highlight"), MarkdownShortcuts::isHighlight(format));  // FEAT-068
    set(QStringLiteral("code"), MarkdownShortcuts::isCode(format));
    const QTextList *list = cursor.currentList();
    set(QStringLiteral("bullets"), list && list->format().style() == QTextListFormat::ListDisc);
    set(QStringLiteral("numbers"), list && list->format().style() == QTextListFormat::ListDecimal);
    set(QStringLiteral("heading"), cursor.blockFormat().headingLevel() > 0);
}

void NoteEditor::fitHeight() {
    if (m_mode == Mode::Inline) {
        const int height = qCeil(m_text->document()->size().height()) + 2 * m_paddingY;
        setFixedHeight(height);
        if (m_toolbarFrame) m_toolbarFrame->adjustSize();
        if (height != m_lastHeight && isVisible()) {
            m_lastHeight = height;
            emit contentHeightChanged(height);  // la riga dell'outliner si allunga
        }
        return;
    }
    const int content = qCeil(m_text->document()->size().height()) + 8;
    const int toolbar = 28 + 4 + 12 + 16 + (m_linkRow && m_linkRow->isVisible() ? 32 : 0);
    setFixedHeight(std::clamp(content + toolbar, kMinHeightPx, kMaxHeightPx));
    if (isVisible() && !m_anchor.isNull()) moveNextTo(m_anchor);
}

void NoteEditor::openFor(const QString &nodeId, const QString &plain, const QString &html, const QRect &anchor) {
    if (isVisible()) commitAndClose();
    m_nodeId = nodeId;
    {
        const QSignalBlocker blocker(m_text);
        if (html.isEmpty()) m_text->setPlainText(plain);
        else m_text->setHtml(html);
    }
    m_text->moveCursor(QTextCursor::End);
    m_linkRow->hide();
    fitHeight();
    moveNextTo(anchor);
    show();
    raise();
    m_text->setFocus();
    updateButtons();
    qApp->installEventFilter(this);  // clic fuori = chiusura
}

void NoteEditor::openInline(const QString &nodeId, const QString &plain, const QString &html, const QRect &box,
                            const QFont &font, const QColor &text, const QColor &background, int paddingX, int paddingY) {
    if (isVisible()) commitAndClose();
    m_nodeId = nodeId;
    m_background = background;
    m_paddingX = paddingX;
    m_paddingY = paddingY;
    m_lastHeight = -1;
    layout()->setContentsMargins(paddingX, paddingY, paddingX, paddingY);
    m_text->setFont(font);
    m_text->document()->setDefaultFont(font);
    m_text->document()->setDocumentMargin(0);
    QPalette palette = m_text->palette();
    palette.setColor(QPalette::Text, text);
    m_text->setPalette(palette);
    {
        const QSignalBlocker blocker(m_text);
        if (html.isEmpty()) m_text->setPlainText(plain);
        else m_text->setHtml(html);
    }
    m_text->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_text->moveCursor(QTextCursor::End);
    m_linkRow->hide();
    setGeometry(box);
    m_text->document()->setTextWidth(box.width() - 2 * paddingX);
    show();
    raise();
    fitHeight();
    moveInline(box);
    m_toolbarFrame->show();
    m_toolbarFrame->raise();
    m_text->setFocus();
    updateButtons();
    qApp->installEventFilter(this);  // clic fuori = chiusura
}

void NoteEditor::moveInline(const QRect &box) {
    move(box.topLeft());
    setFixedWidth(box.width());
    if (!m_toolbarFrame) return;
    m_toolbarFrame->adjustSize();
    // Barra sopra il riquadro, allineata a destra (sulla riga del titolo).
    const int x = std::max(0, box.right() - m_toolbarFrame->width());
    const int y = std::max(0, box.top() - m_toolbarFrame->height() - 4);
    m_toolbarFrame->move(x, y);
}

void NoteEditor::moveNextTo(const QRect &anchor) {
    m_anchor = anchor;
    QWidget *window = parentWidget();
    if (!window) return;
    constexpr int kGapPx = 8;
    const QRect local(window->mapFromGlobal(anchor.topLeft()), anchor.size());
    int x = local.right() + kGapPx;
    if (x + width() > window->width() - kGapPx) x = local.left() - kGapPx - width();  // a sinistra
    x = std::clamp(x, kGapPx, std::max(kGapPx, window->width() - width() - kGapPx));
    const int y = std::clamp(local.top(), kGapPx, std::max(kGapPx, window->height() - height() - kGapPx));
    move(x, y);
}

void NoteEditor::commitAndClose() {
    if (!isVisible()) return;
    qApp->removeEventFilter(this);
    hide();
    if (m_toolbarFrame) m_toolbarFrame->hide();
    const QString plain = m_text->toPlainText();
    // HTML senza il font del documento (come i titoli): il riquadro scuro non entra nella nota.
    const QString html = plain.trimmed().isEmpty() ? QString() : RichTitle::withoutDocumentFont(m_text->toHtml());
    const QString id = m_nodeId;
    m_nodeId.clear();
    emit finished(id, plain, html);
}

bool NoteEditor::eventFilter(QObject *watched, QEvent *event) {
    // Ctrl+clic su un collegamento: si apre nel browser (il clic semplice scrive).
    if (watched == m_text->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->modifiers() & Qt::ControlModifier) {
            const QString href = m_text->anchorAt(mouse->position().toPoint());
            if (!href.isEmpty()) {
                if (NodeLinks::canOpen(QUrl(href))) QDesktopServices::openUrl(QUrl(href));  // BUG-043
                return true;
            }
        }
    }
    if (watched == m_linkField && event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        m_linkRow->hide();
        m_text->setFocus();
        return true;
    }
    if (watched == m_text && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) { commitAndClose(); return true; }
        // FEAT-068: il tasto lo scrive il campo, poi le scorciatoie Markdown guardano cosa c'e'.
        if (key->text().size() == 1 && MarkdownShortcuts::isTrigger(key->text().front()) &&
            !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) && !m_typingShortcut) {
            m_typingShortcut = true;
            static_cast<QObject *>(m_text)->event(event);
            m_typingShortcut = false;
            QTextCursor cursor = m_text->textCursor();
            MarkdownShortcuts::Options options;
            options.context = MarkdownShortcuts::Context::Note;
            options.headingPx = kHeadingSizePx;
            options.bodyPx = kBodySizePx;
            if (MarkdownShortcuts::afterTyping(cursor, key->text().front(), options).changed) {
                m_text->setTextCursor(cursor);
                updateButtons();
            }
            return true;
        }
        if (key->modifiers() & Qt::ControlModifier) {
            const auto toggle = [this](const QString &name) {
                if (QToolButton *button = formatButton(name)) button->click();
            };
            if (key->key() == Qt::Key_B) { toggle(QStringLiteral("bold")); return true; }
            if (key->key() == Qt::Key_I) { toggle(QStringLiteral("italic")); return true; }
            if (key->key() == Qt::Key_U) { toggle(QStringLiteral("underline")); return true; }
        }
    }
    if (event->type() == QEvent::MouseButtonPress && isVisible()) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QPoint inside = mapFromGlobal(mouse->globalPosition().toPoint());
        // Clic fuori dal riquadro (non sui suoi menu o suggerimenti): si chiude e si salva.
        auto *widget = qobject_cast<QWidget *>(watched);
        const QPoint global = mouse->globalPosition().toPoint();
        const bool onToolbar = m_toolbarFrame && m_toolbarFrame->isVisible()
                               && m_toolbarFrame->rect().contains(m_toolbarFrame->mapFromGlobal(global));
        if (widget && widget->window() == window() && !rect().contains(inside) && !onToolbar) commitAndClose();
    }
    return QFrame::eventFilter(watched, event);
}

void NoteEditor::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (m_mode == Mode::Inline) {  // lo stesso riquadro grigio della riga, sotto il testo
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_background);
        painter.drawRoundedRect(QRectF(rect()), 4, 4);
        return;
    }
    // Ombra leggera come i modali, poi la superficie scura.
    const QRectF box = QRectF(rect()).adjusted(4, 4, -4, -4);
    painter.setPen(Qt::NoPen);
    const int alphas[] = {10, 8, 6, 4};
    for (int i = 0; i < 4; ++i) {
        const qreal grow = i + 1;
        painter.setBrush(QColor(15, 23, 42, alphas[i]));
        painter.drawRoundedRect(box.adjusted(-grow, -grow + 2, grow, grow + 2), kRadiusPx + grow, kRadiusPx + grow);
    }
    painter.setPen(QPen(UiTheme::color(UiTheme::Token::Divider), 1));
    painter.setBrush(UiTheme::color(UiTheme::Token::Surface));
    painter.drawRoundedRect(box, kRadiusPx, kRadiusPx);
}
