#include "mainwindow.h"
#include "numbering.h"
#include "uitheme.h"
#include "mcpbridge.h"
#include "mcpmaptools.h"
#include "mcptoolsupport.h"
#include "modaldialog.h"
#include "pitchexport.h"
#include "custompalettes.h"
#include "filevalidation.h"
#include "noteeditor.h"
#include "formuladialog.h"
#include "markers.h"
#include "toggleswitch.h"
#include "mapstructure.h"
#include "stickers.h"
#include "iconlibrary.h"
#include "iconpanel.h"
#include "markdownshortcuts.h"
#include "labeleditor.h"
#include "palettedialog.h"
#include "linkdialogs.h"
#include "exportdialog.h"
#include "pagedialog.h"
#include "officeexportdialog.h"
#include "pitchpage.h"
#include "pitchtemplate.h"
#include "pitchwindow.h"
#include "bmapstorage.h"
#include "formulatext.h"
#include "localization.h"
#include "markdownimport.h"
#include "clipboardpaste.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "richtitle.h"
#include "settingsdialog.h"
#include "floatingui.h"
#include "structurepicker.h"
#include "segmentedcontrol.h"
#include "sidepanel.h"
#include "stylecontrols.h"
#include "performance.h"
#include "texteditmenu.h"
#include "topictools.h"
#include "ganttview.h"
#include "resourcesdialog.h"

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QImageReader>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsProxyWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QAbstractSpinBox>
#include <QTextEdit>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPainterPath>
#include <QPainter>
#include <QCryptographicHash>
#include <QImage>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QRubberBand>
#include <QPushButton>
#include <QRegion>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QAbstractTextDocumentLayout>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QStylePainter>
#include <QStyleOptionComboBox>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTimer>
#include <QLocale>
#include <QFileSystemWatcher>
#include <QTreeView>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QMouseEvent>
#include <QListWidget>
#include <QLockFile>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QBuffer>
#include <QPixmapCache>
#include <QElapsedTimer>
#include <QScreen>
#include <QtMath>
#include <QWindow>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrentRun>
#include <algorithm>
#include "mapvariants.h"
#include "variantgallery.h"
#include "navigationpanel.h"
#include "boundaryitem.h"
#include "gantttaskpanel.h"
#include "help.h"
#include "manualwindow.h"
#include "relationshipitem.h"
#include "tour.h"
#include "updateprompt.h"

namespace {
// Schede della Home: il bordo compare solo al passaggio del mouse e legge il colore
// corrente della selezione dei nodi, quindi segue subito la preferenza globale.
template<typename Button>
class HomeCardButton final : public Button {
public:
    using Button::Button;
protected:
    void paintEvent(QPaintEvent *event) override {
        Button::paintEvent(event);
        if (!this->underMouse()) return;
        const auto &style = NodeItem::selectionStyle();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal width = style.width;
        painter.setPen(QPen(style.selectedColor, width));
        painter.setBrush(Qt::NoBrush);
        const qreal inset = width / 2.0;
        painter.drawRoundedRect(QRectF(this->rect()).adjusted(inset, inset, -inset, -inset), 8, 8);
    }
};

// Superficie dell'editor: sfondo della mappa, angoli arrotondati a pannello aperto. Disegnata qui
// e non con un foglio di stile (2026-10-19: cambiare il foglio di stile a ogni apertura del pannello
// ridisegnava lo stile di tutto l'editor, circa 200 ms per clic).
class EditorSurface final : public QWidget {
public:
    explicit EditorSurface(QWidget *parent = nullptr) : QWidget(parent) { setObjectName(QStringLiteral("editorSurface")); }
    void setLook(const QColor &color, int radius) {
        if (color == m_color && radius == m_radius) return;
        m_color = color;
        m_radius = radius;
        update();
    }
    QColor color() const { return m_color; }
    int radius() const { return m_radius; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_color);
        painter.drawRoundedRect(QRectF(rect()), m_radius, m_radius);
    }

private:
    QColor m_color = Qt::white;
    int m_radius = 0;
};

// Dissolvenza sopra il canvas: un solo rettangolo a gradiente, senza blur o buffer
// fuori schermo. Non riceve input, quindi non cambia l'interazione con la mappa.
class EditorTopFade final : public QWidget {
public:
    explicit EditorTopFade(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFixedHeight(96);
    }
    // FEAT-036: la dissolvenza va verso lo sfondo del foglio (prima sempre bianco).
    void setColor(const QColor &color) { m_color = color; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        QLinearGradient fade(0, 0, 0, height());
        const auto at = [this](int alpha) { QColor color = m_color; color.setAlpha(alpha); return color; };
        fade.setColorAt(0.0, at(242));
        fade.setColorAt(0.5, at(196));
        fade.setColorAt(1.0, at(0));
        painter.fillRect(rect(), fade);
    }

private:
    QColor m_color = Qt::white;
};

// Formattazione condivisa tra mappa e outliner (richieste 2026-10-13 e 2026-10-14). L'outliner
// mostra e modifica grassetto, corsivo, barrato e sottolineato del titolo formattato
// (MindNode::titleHtml); font, dimensione e colore restano nell'HTML e si vedono solo sulla mappa.
// Il peso dipende dal contesto (BoldWeights): dove il testo è già in grassetto (voci di primo
// livello e titolo principale nell'outliner, nodo principale sulla mappa) il grassetto è Black
// (900), perché Segoe UI non ha l'800 e lo rende come Bold 700 (misurato con QFontInfo).
enum InlineFlag : quint8 { kInlineItalic = 1, kInlineStrike = 2, kInlineUnderline = 4, kInlineBold = 8, kInlineHighlight = 16 };

// Peso del testo in grassetto e del testo normale in un contesto (riga dell'outliner o titolo
// salvato per la mappa). Un carattere è "in grassetto" se il suo peso arriva almeno a bold.
struct BoldWeights {
    int bold = QFont::Bold;
    int regular = QFont::Normal;
};
// Testo normale a peso 400 (voci dell'outliner sotto il primo livello, nodi della mappa).
constexpr BoldWeights kNormalBold{QFont::Bold, QFont::Normal};
// Testo già in grassetto (voci di primo livello e titolo principale nell'outliner, nodo principale).
constexpr BoldWeights kHeavyBold{QFont::Black, QFont::Bold};

quint8 inlineFlags(const QTextCharFormat &format, int boldWeight = QFont::Bold) {
    return (format.fontItalic() ? kInlineItalic : 0) | (format.fontStrikeOut() ? kInlineStrike : 0) |
           (format.fontUnderline() ? kInlineUnderline : 0) |
           (format.fontWeight() >= boldWeight ? kInlineBold : 0) |
           (MarkdownShortcuts::isHighlight(format) ? kInlineHighlight : 0);  // FEAT-068 "==testo=="
}

// Formato da disegnare: il peso si imposta solo per il grassetto, il resto eredita quello della
// riga (impostarlo a 400 toglierebbe il grassetto delle voci di primo livello).
QTextCharFormat inlineFormat(quint8 flags, int boldWeight = QFont::Bold) {
    QTextCharFormat format = flags & kInlineHighlight ? MarkdownShortcuts::highlightFormat() : QTextCharFormat();
    format.setFontItalic(flags & kInlineItalic);
    format.setFontStrikeOut(flags & kInlineStrike);
    format.setFontUnderline(flags & kInlineUnderline);
    if (flags & kInlineBold) format.setFontWeight(boldWeight);
    return format;
}

// FEAT-022: pesi del titolo salvato di un nodo: normale = peso del nodo, grassetto = 700 o 900
// (NodeItem::boldWeightFor). Con i predefiniti sono kNormalBold e, per il nodo principale, kHeavyBold.
BoldWeights storedBoldWeights(const MindNode *node) { return {NodeItem::boldWeightFor(node), node->fontWeight}; }

// Formato da salvare in titleHtml: il peso è sempre esplicito, così il grassetto si può anche togliere.
QTextCharFormat inlineStoredFormat(quint8 flags, BoldWeights weights) {
    QTextCharFormat format = inlineFormat(flags & ~kInlineBold);
    format.setFontWeight(flags & kInlineBold ? weights.bold : weights.regular);
    return format;
}

// Flag di ogni carattere del documento, indicizzati come le posizioni di QTextCursor e come
// toPlainText() (il separatore tra paragrafi occupa una posizione e vale 0).
QVector<quint8> inlineFlagsPerChar(const QTextDocument &document, int boldWeight = QFont::Bold) {
    QVector<quint8> flags(document.characterCount(), 0);
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) continue;
            const quint8 value = inlineFlags(fragment.charFormat(), boldWeight);
            for (int i = 0; i < fragment.length(); ++i) flags[fragment.position() + i] = value;
        }
    return flags;
}

// Tratti consecutivi con gli stessi flag, a partire da offset e per length caratteri.
QList<QTextLayout::FormatRange> inlineRanges(const QVector<quint8> &flags, int offset, int length,
                                             int boldWeight = QFont::Bold) {
    QList<QTextLayout::FormatRange> ranges;
    for (int i = 0; i < length;) {
        const quint8 value = flags.value(offset + i);
        int end = i + 1;
        while (end < length && flags.value(offset + end) == value) ++end;
        if (value) ranges.append({i, end - i, inlineFormat(value, boldWeight)});
        i = end;
    }
    return ranges;
}

// Inizio del titolo nel testo del documento: NodeItem::finishEditing salva come titolo il testo
// senza spazi iniziali e finali, mentre l'HTML li conserva. -1 se l'HTML non corrisponde al titolo.
int titleOffsetIn(const QString &plain, const QString &title) {
    int offset = 0;
    while (offset < plain.size() && plain.at(offset).isSpace()) ++offset;
    return plain.mid(offset, title.size()) == title ? offset : -1;
}

// Corsivo, barrato e sottolineato del titolo, nelle posizioni del testo semplice (title).
// Vuoto se non ce ne sono, se il titolo ha formule o se l'HTML non corrisponde al titolo.
// storedBold: peso del grassetto nel titolo salvato (Black per il nodo principale, già in grassetto
// sulla mappa); displayBold: peso con cui disegnarlo nella riga dell'outliner (2026-10-14).
QList<QTextLayout::FormatRange> titleInlineRanges(const QString &title, const QString &html,
                                                  int storedBold = QFont::Bold, int displayBold = QFont::Bold) {
    if (html.isEmpty() || FormulaText::containsMath(title)) return {};
    QTextDocument document;
    document.setHtml(RichTitle::withoutDocumentFont(html));  // FEAT-022: vale il font del nodo
    const int offset = titleOffsetIn(document.toPlainText(), title);
    if (offset < 0) return {};
    return inlineRanges(inlineFlagsPerChar(document, storedBold), offset, int(title.size()), displayBold);
}

// Nuovo titleHtml dopo una modifica fatta nell'outliner. Si parte dal titolo formattato salvato
// (oldHtml), si sostituisce solo il tratto di testo cambiato (prefisso e suffisso comuni restano
// con la loro formattazione completa) e si applicano corsivo, barrato e sottolineato del campo.
// I caratteri nuovi prendono le altre proprietà (grassetto, colore...) dal carattere precedente,
// come quando si scrive sulla mappa. Titolo senza HTML e senza formattazione: resta testo semplice
// (stringa vuota). Nessun cambiamento: restituisce oldHtml così com'è (niente "Modificato").
// editedBold: peso del grassetto nel campo dell'outliner; stored: pesi da scrivere nel titolo
// salvato (kHeavyBold per il nodo principale, kNormalBold per gli altri). 2026-10-14.
QString mergeOutlineFormats(const QString &oldTitle, const QString &oldHtml, const QTextDocument &edited,
                            const QFont &font, int editedBold = QFont::Bold, BoldWeights stored = kNormalBold) {
    const QString newText = edited.toPlainText();
    // Le formule restano testo semplice, come sulla mappa (NodeItem::finishEditing).
    if (FormulaText::containsMath(newText)) return {};
    const QVector<quint8> wanted = inlineFlagsPerChar(edited, editedBold);
    const bool anyWanted = std::any_of(wanted.cbegin(), wanted.cend(), [](quint8 value) { return value != 0; });
    if (oldHtml.isEmpty() && !anyWanted) return {};

    QTextDocument document;
    document.setUndoRedoEnabled(false);
    document.setDefaultFont(font);
    if (oldHtml.isEmpty()) document.setPlainText(oldTitle);
    else document.setHtml(RichTitle::withoutDocumentFont(oldHtml));  // FEAT-022: vale il font del nodo
    // Tratto del documento che corrisponde al titolo (gli spazi attorno restano dove sono).
    const QString oldPlain = document.toPlainText();
    int offset = titleOffsetIn(oldPlain, oldTitle);
    const QString oldText = offset < 0 ? oldPlain : oldTitle;
    if (offset < 0) offset = 0;

    const int common = int(qMin(oldText.size(), newText.size()));
    int prefix = 0;
    while (prefix < common && oldText.at(prefix) == newText.at(prefix)) ++prefix;
    int suffix = 0;
    while (suffix < common - prefix && oldText.at(oldText.size() - 1 - suffix) == newText.at(newText.size() - 1 - suffix)) ++suffix;
    bool changed = prefix + suffix != oldText.size() || prefix + suffix != newText.size();
    QTextCursor cursor(&document);
    if (changed) {
        cursor.setPosition(offset + prefix);
        cursor.setPosition(offset + int(oldText.size()) - suffix, QTextCursor::KeepAnchor);
        // "\n" diventa un nuovo paragrafo (https://doc.qt.io/qt-6/qtextcursor.html#insertText).
        cursor.insertText(newText.mid(prefix, newText.size() - prefix - suffix));
        if (document.toPlainText().mid(offset, newText.size()) != newText) {
            // Caso imprevisto (caratteri che toPlainText converte): si riparte dal testo nuovo.
            document.setPlainText(newText);
            offset = 0;
        }
    }
    const QVector<quint8> current = inlineFlagsPerChar(document, stored.bold);
    for (int i = 0; i < newText.size();) {
        const quint8 value = wanted.value(i);
        if (newText.at(i) == QLatin1Char('\n') || current.value(offset + i) == value) { ++i; continue; }
        int end = i + 1;
        while (end < newText.size() && newText.at(end) != QLatin1Char('\n') && wanted.value(end) == value &&
               current.value(offset + end) != value) ++end;
        cursor.setPosition(offset + i);
        cursor.setPosition(offset + end, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(inlineStoredFormat(value, stored));
        // FEAT-068: l'evidenziato tolto nel campo si toglie anche dal titolo salvato.
        if (!(value & kInlineHighlight) && (current.value(offset + i) & kInlineHighlight)) MarkdownShortcuts::setHighlight(cursor, false);
        changed = true;
        i = end;
    }
    if (!changed) return oldHtml;
    return document.toHtml();
}

// Barra dell'outliner per la selezione nel campo di modifica: solo corsivo, barrato e
// sottolineato (richiesta 2026-10-13). Stesso aspetto della barra della mappa (TextFormatBar in
// nodeitem.cpp, AGENTS.md "Formattazione della selezione nella mappa"). I bottoni non prendono
// il focus, così il campo resta aperto e la selezione resta visibile.
class OutlineFormatBar final : public QWidget {
    // Senza Q_OBJECT tr() userebbe il contesto "QWidget" e non troverebbe la traduzione (ADR-003).
    Q_DECLARE_TR_FUNCTIONS(OutlineFormatBar)
public:
    explicit OutlineFormatBar(QWidget *parent) : QWidget(parent) {
        setObjectName("OutlineFormatBar");
        setAttribute(Qt::WA_StyledBackground);
        setStyleSheet(R"(
            QWidget#OutlineFormatBar { background:#ffffff; border:1px solid rgba(15,23,42,13); border-radius:10px; }
            QToolButton { border:0; border-radius:6px; padding:0; background:transparent; }
            QToolButton:hover, QToolButton:checked { background:rgba(15,23,42,15); }
            QToolButton:pressed { background:rgba(15,23,42,28); }
        )");
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(4, 4, 4, 4);
        row->setSpacing(2);
        row->setSizeConstraint(QLayout::SetFixedSize);
        // Grassetto (2026-10-14, richiesta dell'utente): primo, come nella barra della mappa.
        m_bold = button(":/assets/hugeicons/text-bold.svg", tr("Bold"));
        m_italic = button(":/assets/hugeicons/text-italic.svg", tr("Italic"));
        m_strike = button(":/assets/hugeicons/text-strikethrough.svg", tr("Strikethrough"));
        m_underline = button(":/assets/hugeicons/text-underline.svg", tr("Underline"));
        m_highlight = button(":/assets/hugeicons/text-highlight.svg", tr("Highlight"));  // FEAT-068
        row->addWidget(m_bold);
        row->addWidget(m_italic);
        row->addWidget(m_strike);
        row->addWidget(m_underline);
        row->addWidget(m_highlight);
        connect(m_highlight, &QToolButton::clicked, this, [this](bool on) { if (applyHighlight) applyHighlight(on); });
        connect(m_bold, &QToolButton::clicked, this, [this](bool on) {
            // Togliere il grassetto riporta il peso della riga (700 al primo livello), non 400.
            QTextCharFormat format; format.setFontWeight(on ? weights.bold : weights.regular); apply(format);
        });
        connect(m_italic, &QToolButton::clicked, this, [this](bool on) {
            QTextCharFormat format; format.setFontItalic(on); apply(format);
        });
        connect(m_strike, &QToolButton::clicked, this, [this](bool on) {
            QTextCharFormat format; format.setFontStrikeOut(on); apply(format);
        });
        connect(m_underline, &QToolButton::clicked, this, [this](bool on) {
            QTextCharFormat format; format.setFontUnderline(on); apply(format);
        });
        // Al clic su un widget senza focus Qt dà il focus al primo antenato che lo accetta (qui la
        // vista), e il campo che lo perde si chiude. Con ClickFocus e il campo come focus proxy
        // (OutlineDelegate::createEditor) il clic sulla barra lascia il focus al campo.
        setFocusPolicy(Qt::ClickFocus);
        hide();
    }

    std::function<void(const QTextCharFormat &)> applyFormat;
    std::function<void(bool)> applyHighlight;  // FEAT-068: accende o toglie l'evidenziato
    // Pesi del grassetto del campo collegato (kHeavyBold al primo livello e nel titolo principale).
    BoldWeights weights = kNormalBold;

    // FEAT-095: Ctrl+B and the others on the selection, as a click on the button.
    void toggle(RichTitle::CharStyle style) {
        switch (style) {
        case RichTitle::CharStyle::Bold: m_bold->click(); break;
        case RichTitle::CharStyle::Italic: m_italic->click(); break;
        case RichTitle::CharStyle::Underline: m_underline->click(); break;
        case RichTitle::CharStyle::StrikeOut: m_strike->click(); break;
        }
    }

    // Stato dei bottoni dal formato del testo selezionato (come TextFormatBar::sync).
    void sync(const QTextCharFormat &format) {
        const QSignalBlocker b0(m_bold), b1(m_italic), b2(m_strike), b3(m_underline), b4(m_highlight);
        m_bold->setChecked(format.fontWeight() >= weights.bold);
        m_italic->setChecked(format.fontItalic());
        m_strike->setChecked(format.fontStrikeOut());
        m_underline->setChecked(format.fontUnderline());
        m_highlight->setChecked(MarkdownShortcuts::isHighlight(format));
    }

protected:
    // Un clic sullo sfondo della barra (fra i bottoni) non deve arrivare alla vista, che
    // chiuderebbe il campo di modifica.
    void mousePressEvent(QMouseEvent *event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent *event) override { event->accept(); }
    void mouseDoubleClickEvent(QMouseEvent *event) override { event->accept(); }

private:
    QToolButton *button(const QString &icon, const QString &tip) {
        auto *result = new QToolButton;
        result->setIcon(QIcon(icon));
        result->setIconSize({20, 20});
        result->setToolTip(tip);
        result->setFixedSize(32, 32);
        result->setCheckable(true);
        result->setFocusPolicy(Qt::NoFocus);
        return result;
    }
    void apply(const QTextCharFormat &format) { if (applyFormat) applyFormat(format); }

    QToolButton *m_bold = nullptr;
    QToolButton *m_italic = nullptr;
    QToolButton *m_strike = nullptr;
    QToolButton *m_underline = nullptr;
    QToolButton *m_highlight = nullptr;  // FEAT-068
};

// BUG-001: i campi di testo dell'outliner sono alti esattamente quanto il loro testo, ma
// QPlainTextEdit considera visibile l'altezza meno 1 px (QPlainTextEditPrivate::adjustScrollbars,
// qtbase/src/widgets/widgets/qplaintextedit.cpp) e lascia scorrere di una riga. Trascinando una
// selezione fuori dal campo, lo scorrimento automatico (QPlainTextEdit::timerEvent) spostava il
// testo fuori dalla vista; un clic nel campo veniva poi ignorato e risaliva all'outliner. I campi
// mostrano sempre tutto il testo, quindi lo scorrimento verticale resta fermo a 0.
void preventVerticalScroll(QPlainTextEdit *edit) {
    QScrollBar *bar = edit->verticalScrollBar();
    QObject::connect(bar, &QAbstractSlider::rangeChanged, edit, [bar](int, int maximum) {
        if (maximum != 0) bar->setRange(0, 0);
    });
    bar->setRange(0, 0);
}

// Campo di modifica dell'outliner su più righe (richiesta 2026-10-12: Maiusc+Invio va a capo
// restando nello stesso nodo, come nella mappa). Prima era il QLineEdit standard del delegate,
// che ha una riga sola. Offre gli stessi metodi di QLineEdit usati dall'outliner (testo,
// posizione del cursore, selezione), così il resto del codice cambia poco.
// Invio, Tab e Canc/Backspace su campo vuoto restano gestiti da OutlineDelegate::eventFilter.
// FEAT-095: the character keys of an outliner field; true when the event was one of them.
bool outlineCharStyleKey(QPlainTextEdit *field, QKeyEvent *event, const std::function<int(const QKeyEvent *)> &styleKey,
                         OutlineFormatBar *bar) {
    const int style = styleKey ? styleKey(event) : -1;
    if (style < 0) return false;
    if (bar && field->textCursor().hasSelection()) bar->toggle(RichTitle::CharStyle(style));  // nothing without one
    return true;
}

class OutlineTitleEditor final : public QPlainTextEdit {
public:
    explicit OutlineTitleEditor(QWidget *parent) : QPlainTextEdit(parent) {
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // (richiesta 2026-10-13: come nella mappa, il testo va a capo alla larghezza della riga;
        // una parola più larga della riga va a capo anche al suo interno. Stesso a capo del testo
        // disegnato, vedi OutlineDelegate::wrappedText.)
        setLineWrapMode(QPlainTextEdit::WidgetWidth);
        setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        setTabChangesFocus(false);
        document()->setDocumentMargin(0);
        setContentsMargins(0, 0, 0, 0);
        viewport()->setAutoFillBackground(false);
        preventVerticalScroll(this);  // BUG-001
    }

    QString text() const { return toPlainText(); }
    void setText(const QString &text) { setPlainText(text); }
    int cursorPosition() const { return textCursor().position(); }
    void setCursorPosition(int position) {
        QTextCursor cursor = textCursor();
        cursor.setPosition(qBound(0, position, document()->characterCount() - 1));
        setTextCursor(cursor);
    }
    QString selectedText() const {
        // QTextCursor usa U+2029 tra i paragrafi: si restituisce "\n" come nel titolo salvato.
        return textCursor().selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    }
    void setSelection(int start, int length) {
        QTextCursor cursor = textCursor();
        cursor.setPosition(start);
        cursor.setPosition(start + length, QTextCursor::KeepAnchor);
        setTextCursor(cursor);
    }
    void end(bool mark) { moveCursor(QTextCursor::End, mark ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor); }
    int lineCount() const { return document()->blockCount(); }
    // Altezza reale delle righe secondo il layout di QPlainTextEdit (lineSpacing intero può
    // essere più basso di qualche frazione di pixel per riga e far scorrere il testo).
    int contentHeight() const {
        const QRectF first = blockBoundingGeometry(document()->firstBlock());
        const QRectF last = blockBoundingGeometry(document()->lastBlock());
        return qCeil(last.bottom() - first.top());
    }

protected:
    void keyPressEvent(QKeyEvent *event) override {
        // Maiusc+Invio: a capo con "\n" (nuovo paragrafo). Il comportamento standard di
        // QPlainTextEdit inserirebbe U+2028, che la mappa non tratta come a capo.
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            event->modifiers().testFlag(Qt::ShiftModifier)) {
            insertPlainText(QStringLiteral("\n"));
            return;
        }
        if (outlineCharStyleKey(this, event, charStyleKey, formatBar)) return;
        if (QTextCursor cursor = textCursor(); plainTyping.type(event, cursor)) { setTextCursor(cursor); ensureCursorVisible(); }
        else QPlainTextEdit::keyPressEvent(event);
        typedShortcut(event);
    }
    void mousePressEvent(QMouseEvent *event) override { plainTyping.active = false; QPlainTextEdit::mousePressEvent(event); }

public:
    std::function<int(const QKeyEvent *)> charStyleKey;  // FEAT-095 (MindMapScene::charStyleKey)
    OutlineFormatBar *formatBar = nullptr;
    RichTitle::PlainFirstTyping plainTyping;  // FEAT-095: on from the field's opening
    int boldWeight = QFont::Bold;  // FEAT-068: peso del grassetto della riga (rowBoldWeights)
    int pendingTask = 0;           // FEAT-068: "- [ ] " a inizio titolo, casella alla conferma
    // FEAT-094: true when the pasted clipboard went onto the item (image, files, branch).
    std::function<bool()> pasteContent;

protected:
    void insertFromMimeData(const QMimeData *source) override {
        if (source == QApplication::clipboard()->mimeData() && pasteContent && pasteContent()) return;
        QPlainTextEdit::insertFromMimeData(source);
    }

private:
    // FEAT-068: scorciatoie Markdown sul carattere appena scritto (come sulla mappa).
    void typedShortcut(QKeyEvent *event) {
        if (event->text().size() != 1 || (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) return;
        QTextCursor cursor = textCursor();
        MarkdownShortcuts::Options options;
        options.boldWeight = boldWeight;
        const MarkdownShortcuts::Result result = MarkdownShortcuts::afterTyping(cursor, event->text().front(), options);
        if (!result.changed) return;
        setTextCursor(cursor);
        if (result.task) pendingTask = result.task;
    }
};

// Titolo principale dell'outliner (richiesta 2026-10-13). Prima era un QLineEdit: usava il colore
// di selezione del sistema (arancione su Windows, diverso dalle voci), non mostrava corsivo,
// barrato e sottolineato del titolo e non poteva avere la barra di formattazione delle voci.
// Ora è un campo come quello delle voci (QPlainTextEdit con formati), alto quanto il suo testo,
// con gli stessi due eventi del QLineEdit usati da MainWindow: fine modifica e Invio.
class OutlineRootTitleEdit final : public QPlainTextEdit {
public:
    explicit OutlineRootTitleEdit(QWidget *parent = nullptr) : QPlainTextEdit(parent) {
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // A capo come le voci: tra le parole, dentro una parola solo se non sta nella riga.
        setLineWrapMode(QPlainTextEdit::WidgetWidth);
        setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        setTabChangesFocus(true);  // come il QLineEdit: Tab passa all'elenco
        document()->setDocumentMargin(0);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        TextEditMenu::attach(this);  // FEAT-042: our menu instead of the system one
        connect(document(), &QTextDocument::contentsChanged, this, [this] { fitHeight(); });
        preventVerticalScroll(this);  // BUG-001
    }

    std::function<void()> editingFinished;  // perdita del focus o Invio (come QLineEdit::editingFinished)
    std::function<void()> returnPressed;    // Invio senza Maiusc (come QLineEdit::returnPressed)
    // FEAT-055: testo incollato; true se l'ha gestito MainWindow (Markdown che diventa la mappa),
    // false per incollarlo nel campo come testo.
    std::function<bool(const QString &)> pasteText;
    std::function<bool()> pasteContent;  // FEAT-094: as OutlineTitleEditor::pasteContent
    std::function<int(const QKeyEvent *)> charStyleKey;  // FEAT-095: as OutlineTitleEditor
    OutlineFormatBar *formatBar = nullptr;
    RichTitle::PlainFirstTyping plainTyping;  // FEAT-095: on when the field takes the focus

    QString text() const { return toPlainText(); }
    void setText(const QString &text) { setPlainText(text); }
    // Altezza minima per il titolo con formule, disegnato al posto del testo (updateRootFormula).
    void setFormulaHeight(int height) { m_formulaHeight = height; fitHeight(); }

    // Alto quanto le righe del testo più il padding del foglio di stile (o quanto la formula).
    void fitHeight() {
        const QRectF first = blockBoundingGeometry(document()->firstBlock());
        const QRectF last = blockBoundingGeometry(document()->lastBlock());
        const int content = qCeil(last.bottom() - first.top());
        // Padding and frame of the style sheet. Taken from contentsRect, not from the viewport: while
        // the outliner is hidden the viewport keeps a stale height (bug 2026-11-03, title cut).
        const QMargins margins = viewportMargins();
        const int chrome = height() - contentsRect().height() + margins.top() + margins.bottom();
        const int target = qMax(content + chrome, m_formulaHeight);
        if (target != height()) setFixedHeight(target);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            // Maiusc+Invio va a capo con "\n", come nelle voci; Invio conferma e crea una voce.
            if (event->modifiers().testFlag(Qt::ShiftModifier)) { insertPlainText(QStringLiteral("\n")); return; }
            if (editingFinished) editingFinished();
            if (returnPressed) returnPressed();
            return;
        }
        if (outlineCharStyleKey(this, event, charStyleKey, formatBar)) return;
        if (QTextCursor cursor = textCursor(); plainTyping.type(event, cursor)) { setTextCursor(cursor); ensureCursorVisible(); }
        else QPlainTextEdit::keyPressEvent(event);
        // FEAT-068: scorciatoie Markdown in linea (il titolo principale non ha la casella).
        if (event->text().size() == 1 && event->text() != QLatin1String(" ") &&
            !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
            QTextCursor cursor = textCursor();
            MarkdownShortcuts::Options options;
            options.boldWeight = QFont::Black;  // gia' in grassetto (kHeavyBold)
            if (MarkdownShortcuts::afterTyping(cursor, event->text().front(), options).changed) setTextCursor(cursor);
        }
    }
    void insertFromMimeData(const QMimeData *source) override {
        if (source == QApplication::clipboard()->mimeData() && pasteContent && pasteContent()) return;
        if (source->hasText() && pasteText && pasteText(source->text())) return;
        QPlainTextEdit::insertFromMimeData(source);
    }
    void focusInEvent(QFocusEvent *event) override {
        if (event->reason() != Qt::PopupFocusReason) plainTyping.active = true;  // a click right after ends it
        QPlainTextEdit::focusInEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override { plainTyping.active = false; QPlainTextEdit::mousePressEvent(event); }
    void focusOutEvent(QFocusEvent *event) override {
        QPlainTextEdit::focusOutEvent(event);
        // Un popup (es. menu) non conclude la modifica, come in QLineEdit.
        if (event->reason() == Qt::PopupFocusReason) return;
        if (editingFinished) editingFinished();
        // Bug 2026-10-13 (foto dell'utente): il foglio di stile dà lo stesso colore alla selezione
        // anche senza focus, quindi passando a un'altra voce il titolo restava evidenziato. Come
        // per le voci (il cui campo si chiude), uscendo dal titolo la selezione si toglie.
        QTextCursor cursor = textCursor();
        if (cursor.hasSelection()) { cursor.clearSelection(); setTextCursor(cursor); }
    }
    void resizeEvent(QResizeEvent *event) override {
        QPlainTextEdit::resizeEvent(event);
        fitHeight();  // la larghezza cambia l'a capo
    }
    void changeEvent(QEvent *event) override {
        QPlainTextEdit::changeEvent(event);
        if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) fitHeight();
    }

private:
    int m_formulaHeight = 0;
};

// Titolo del nodo principale nel campo, con corsivo, barrato e sottolineato del titolo
// formattato (come OutlineDelegate::setEditorData per le voci).
void loadRootTitle(OutlineRootTitleEdit *edit, const MindNode *root) {
    // setPlainText scrive il testo con il formato corrente del cursore: dopo un corsivo applicato
    // dalla barra tutto il titolo ricaricato diventerebbe corsivo. Si riparte da un formato vuoto.
    edit->setCurrentCharFormat(QTextCharFormat());
    edit->setText(root->title);
    // Il titolo principale è già in grassetto: il suo grassetto è Black, sulla mappa e qui (2026-10-14).
    const QList<QTextLayout::FormatRange> formats = titleInlineRanges(root->title, root->titleHtml, NodeItem::boldWeightFor(root), kHeavyBold.bold);
    if (formats.isEmpty()) return;
    QTextCursor cursor(edit->document());
    for (const auto &range : formats) {
        cursor.setPosition(range.start);
        cursor.setPosition(range.start + range.length, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(range.format);
    }
    edit->document()->clearUndoRedoStacks();  // il formato iniziale non è una modifica
}

// FEAT-036: colori dell'outliner. Chiari come prima con uno sfondo della mappa chiaro; con uno
// sfondo scuro l'outliner diventa scuro con colori neutri propri (non quelli della mappa).
struct OutlineColors {
    QColor page, text, marker, selection, dropInside, dropLine, badge, grip, rootFocus;
};
OutlineColors lightOutlineColors() {
    return {QColor("#ffffff"), QColor("#202630"), QColor("#202630"), QColor("#e7efff"), QColor("#dce9ff"),
            QColor("#8fb8f4"), QColor(15, 23, 42, 15), QColor("#7f8792"), QColor("#edf3ff")};
}
OutlineColors darkOutlineColors() {
    using UiTheme::Token;
    QColor dropInside = UiTheme::color(Token::Accent);
    dropInside.setAlpha(90);
    return {UiTheme::color(Token::SurfaceSunken), UiTheme::color(Token::Text), UiTheme::color(Token::Text),
            UiTheme::color(Token::SurfaceRaised), dropInside, UiTheme::color(Token::Accent), QColor(255, 255, 255, 30),
            UiTheme::color(Token::TextMuted), UiTheme::color(Token::Surface)};
}
OutlineColors &outlineColors() {
    static OutlineColors colors = lightOutlineColors();
    return colors;
}
// FEAT-059: fondo del riquadro della nota nell'outliner (chiaro o scuro come l'outliner).
QColor outlineNoteBackground() {
    return outlineColors().page == QColor("#ffffff") ? QColor(15, 23, 42, 10) : QColor(255, 255, 255, 18);
}

class OutlineTreeView final : public QTreeView {
public:
    enum class DropHint { None, Before, Inside, After };

    explicit OutlineTreeView(QWidget *parent = nullptr) : QTreeView(parent) {
        setMouseTracking(true);
        viewport()->setMouseTracking(true);
    }

    QModelIndex hoveredIndex() const { return m_hovered; }
    // Più voci selezionate con Ctrl/Maiusc+clic (richiesta 2026-10-13): come sulla mappa, il
    // gruppo si può solo cancellare. Niente maniglia, niente trascinamento, niente scrittura.
    bool hasMultipleSelection() const {
        return selectionModel() && selectionModel()->selectedRows().size() > 1;
    }
    QModelIndex dropIndex() const { return m_dropIndex; }
    DropHint dropHint() const { return m_dropHint; }

    // Zoom della pagina outliner: tutte le misure in pixel della vista e del delegate
    // passano da px(), così testo, rientri, maniglia e marcatori crescono insieme.
    qreal zoom() const { return m_zoom; }
    int px(int value) const { return qRound(value * m_zoom); }
    void setZoom(qreal zoom) { m_zoom = zoom; scheduleDelayedItemsLayout(); viewport()->update(); }
    // Ctrl+rotellina: passo di zoom (+1 / -1), gestito dalla finestra come lo zoom della mappa.
    std::function<void(int)> zoomStep;
    // Delta della rotellina non ancora diventato un passo di zoom (touchpad: eventi piccoli).
    int m_wheelZoomRemainder = 0;
    struct NoteCache {
        QString plain, html;
        int width = 0, fontPx = 0;
        std::shared_ptr<QTextDocument> document;
    };
    mutable QHash<const MindNode *, NoteCache> m_noteCache;  // FEAT-059
    QPersistentModelIndex m_noteEditIndex;
    int m_noteEditHeight = 0;

public:
    QRect contentRect(const QModelIndex &index) const {
        QRect item = visualRect(index);
        if (index == m_dropIndex && m_dropHint == DropHint::Before) item.adjust(0, px(14), 0, 0);
        if (index == m_dropIndex && m_dropHint == DropHint::After) item.adjust(0, 0, 0, -px(14));
        item.setBottom(item.bottom() - noteLinesHeight(index));  // FEAT-059: righe della nota sotto
        return item;
    }
    // FEAT-059 (rivisto il 2026-10-19): la nota sta tutta sotto il titolo in un riquadro
    // grigio e si scrive li' (NoteEditor in modo Inline, aperto da MainWindow); nessuna icona.
    static bool hasNoteText(const QModelIndex &index) {
        const auto *node = index.isValid() ? static_cast<const MindNode *>(index.internalPointer()) : nullptr;
        return node && !node->notes.trimmed().isEmpty();
    }
    QFont noteFont() const {
        QFont small = font();
        small.setWeight(QFont::Normal);
        small.setPixelSize(px(UiTheme::fontPx(14)));
        return small;
    }
    int notePaddingX() const { return px(10); }
    int notePaddingY() const { return px(8); }
    // Larghezza del testo della nota: colonna meno rientro, spazio prima del titolo e margini.
    int noteTextWidth(const QModelIndex &index) const {
        int depth = 0;
        for (QModelIndex up = index.parent(); up.isValid(); up = up.parent()) ++depth;
        return qMax(40, columnWidth(0) - depth * indentation() - leadPx(index) - px(12) - 3 - px(12) - 2 * notePaddingX());
    }
    // Documento della nota impaginato alla sua larghezza, in cache per nodo (si rifa' se cambiano
    // testo, formati, larghezza o zoom).
    QTextDocument *noteDocument(const QModelIndex &index) const {
        const auto *node = static_cast<const MindNode *>(index.internalPointer());
        NoteCache &cache = m_noteCache[node];
        const int width = noteTextWidth(index);
        if (!cache.document || cache.plain != node->notes || cache.html != node->notesHtml || cache.width != width || cache.fontPx != px(14)) {
            cache.document = std::make_shared<QTextDocument>();
            cache.document->setDefaultFont(noteFont());
            cache.document->setDocumentMargin(0);
            if (node->notesHtml.isEmpty()) cache.document->setPlainText(node->notes);
            else cache.document->setHtml(node->notesHtml);
            FormulaText::renderInDocument(cache.document.get(), noteFont(), outlineColors().grip);  // FEAT-064
            cache.document->setTextWidth(width);
            cache.plain = node->notes;
            cache.html = node->notesHtml;
            cache.width = width;
            cache.fontPx = px(14);
        }
        return cache.document.get();
    }
    bool isNoteEditing(const QModelIndex &index) const { return index.isValid() && index == m_noteEditIndex; }
    // Altezza del riquadro della nota (testo + margini); 0 senza nota.
    int noteBoxHeight(const QModelIndex &index) const {
        if (isNoteEditing(index) && m_noteEditHeight > 0) return m_noteEditHeight;
        if (hasNoteText(index)) return qCeil(noteDocument(index)->size().height()) + 2 * notePaddingY();
        return 0;
    }
    // Altezza della riga sotto il titolo: riquadro della nota e 8 px, poi le pillole delle etichette
    // (FEAT-060) e 8 px; 0 senza nota ne' etichette.
    int noteLinesHeight(const QModelIndex &index) const {
        const int box = noteBoxHeight(index);
        const QList<QRect> pills = labelPills(index, 0);
        return pictureBlockHeight(index) + (box == 0 ? 0 : box + px(8)) + (pills.isEmpty() ? 0 : pills.back().bottom() + 1 + px(8));
    }
    // FEAT-065 (decisioni dell'utente 2026-10-19): immagine, adesivo o formula sotto il
    // titolo e prima della nota. Stessa misura che ha sulla mappa (decisione 2026-10-20, prima alta
    // al massimo 160 px), con lo zoom dell'outliner; un clic la sceglie (maniglie come sulla mappa).
    static bool hasPicture(const QModelIndex &index) {
        const MindNode *node = rowNode(index);
        return node && (!node->imagePath.isEmpty() || !node->formula.isEmpty());
    }
    QSize pictureSize(const QModelIndex &index) const {
        if (!hasPicture(index)) return {};
        const MindNode *node = rowNode(index);
        const QSizeF size = (index == m_pictureIndex && m_resizeCorner ? m_resizePreview : node->imageSize) * m_zoom;
        return size.toSize().expandedTo(QSize(1, 1));
    }
    int pictureBlockHeight(const QModelIndex &index) const { return hasPicture(index) ? pictureSize(index).height() + px(8) : 0; }
    QRect pictureRect(const QModelIndex &index) const {
        if (!hasPicture(index)) return {};
        return {QPoint(belowTitleLeft(index), contentRect(index).bottom() + 1 + px(4)), pictureSize(index)};
    }
    bool isPictureSelected(const QModelIndex &index) const { return index.isValid() && index == m_pictureIndex; }
    QModelIndex selectedPicture() const { return m_pictureIndex; }
    void clearPictureSelection() {
        if (!m_pictureIndex.isValid()) return;
        const QModelIndex previous = m_pictureIndex;
        m_pictureIndex = QPersistentModelIndex();
        m_resizeCorner = 0;
        viewport()->update(visualRect(previous));
    }
    // Maniglie agli angoli dell'immagine scelta (1 alto sinistra, 2 alto destra, 3 basso destra, 4 basso sinistra).
    QRect pictureHandle(const QModelIndex &index, int corner) const {
        const QRect picture = pictureRect(index);
        const int size = px(8);
        const QPoint at = corner == 1 ? picture.topLeft() : corner == 2 ? picture.topRight() + QPoint(1, 0)
                        : corner == 3 ? picture.bottomRight() + QPoint(1, 1) : picture.bottomLeft() + QPoint(0, 1);
        return {at - QPoint(size / 2, size / 2), QSize(size, size)};
    }
    int pictureHandleAt(const QModelIndex &index, const QPoint &point) const {
        if (!isPictureSelected(index)) return 0;
        for (int corner = 1; corner <= 4; ++corner)
            if (pictureHandle(index, corner).adjusted(-2, -2, 2, 2).contains(point)) return corner;
        return 0;
    }
    // Marcatori (FEAT-051) a sinistra del titolo, prima della casella, come sulla mappa.
    int markersWidth(const QModelIndex &index) const {
        const MindNode *node = rowNode(index);
        return node ? int(node->markers.size()) * px(22) : 0;
    }
    QRect mapMarkerRect(const QModelIndex &index, int i) const {
        const QRect item = contentRect(index);
        return {item.left() + px(46) + i * px(22), item.top(), px(22), item.height()};
    }
    int mapMarkerAt(const QModelIndex &index, const QPoint &point) const {
        const MindNode *node = rowNode(index);
        if (!node) return -1;
        for (int i = 0; i < node->markers.size(); ++i)
            if (mapMarkerRect(index, i).contains(point)) return i;
        return -1;
    }
    // Collegamenti (FEAT-027) e graffetta dei file (FEAT-049) a destra, subito dopo il titolo.
    enum class TrailingIcon { TopicLink, WebLink, Files };
    QList<std::pair<TrailingIcon, QRect>> trailingIcons(const QModelIndex &index) const {
        QList<std::pair<TrailingIcon, QRect>> icons;
        const MindNode *node = rowNode(index);
        if (!node) return icons;
        QList<TrailingIcon> kinds;
        if (!node->topicLink.isEmpty()) kinds.append(TrailingIcon::TopicLink);
        if (!node->webLink.isEmpty()) kinds.append(TrailingIcon::WebLink);
        if (!node->files.isEmpty()) kinds.append(TrailingIcon::Files);
        if (kinds.isEmpty()) return icons;
        const QRect item = contentRect(index);
        const int size = px(24);
        // Titoli con formule: la loro larghezza disegnata qui non e' nota (titleRect arriva a fine riga).
        int left = qMin(titleRect(index).right() - px(4), item.right() - int(kinds.size()) * size - px(8));
        for (const TrailingIcon kind : kinds) {
            icons.append({kind, QRect(left, item.center().y() - size / 2, size, size)});
            left += size;
        }
        return icons;
    }
    int trailingIconAt(const QModelIndex &index, const QPoint &point) const {
        const auto icons = trailingIcons(index);
        for (int i = 0; i < icons.size(); ++i)
            if (icons[i].second.contains(point)) return i;
        return -1;
    }
    std::function<QPixmap(const MindNode *)> picture;                  // disegno della mappa (MindMapScene::nodePicture)
    std::function<void(const QModelIndex &, int)> mapMarkerClicked;    // clic = valore seguente
    std::function<void(const QModelIndex &, TrailingIcon, const QPoint &)> trailingIconClicked;
    std::function<void(const QModelIndex &, const QSizeF &)> pictureResized;  // misura nuova sulla mappa
    std::function<void(const QModelIndex &)> pictureActivated;          // doppio clic (formula: modifica)
    // Inizio del testo del titolo: nota e pillole si allineano qui.
    int belowTitleLeft(const QModelIndex &index) const { return contentRect(index).left() + leadPx(index) + px(12) + 3; }
    // Riquadro della nota sotto il titolo, allineato all'inizio del testo del titolo.
    QRect noteBoxRect(const QModelIndex &index) const {
        const int height = noteBoxHeight(index);
        if (height == 0) return {};
        const QRect item = contentRect(index);
        const int left = belowTitleLeft(index);
        return {left, item.bottom() + 1 + pictureBlockHeight(index), qMax(1, item.right() - px(12) - left), height};
    }
    // FEAT-060: pillole delle etichette sotto la nota (o sotto il titolo), a capo sulla larghezza
    // della colonna. top = bordo alto del blocco (0 per misurarlo).
    QFont labelFont() const {
        QFont small = font();
        small.setWeight(QFont::Normal);
        small.setPixelSize(px(UiTheme::fontPx(12)));
        return small;
    }
    QList<QRect> labelPills(const QModelIndex &index, int top) const {
        QList<QRect> pills;
        const MindNode *node = rowNode(index);
        if (!node || node->labels.isEmpty()) return pills;
        const QFontMetrics metrics(labelFont());
        const int width = noteTextWidth(index) + 2 * notePaddingX();
        const int height = px(20), gap = px(4);
        int x = 0, y = top;
        for (const QString &label : node->labels) {
            const int pill = qMin(width, metrics.horizontalAdvance(label) + 2 * px(8));
            if (x > 0 && x + pill > width) { x = 0; y += height + gap; }
            pills.append(QRect(x, y, pill, height));
            x += pill + gap;
        }
        return pills;
    }
    QList<QRect> labelRects(const QModelIndex &index) const {
        const int box = noteBoxHeight(index);
        const QRect item = contentRect(index);
        QList<QRect> pills = labelPills(index, item.bottom() + 1 + pictureBlockHeight(index) + (box == 0 ? 0 : box + px(8)));
        for (QRect &pill : pills) pill.translate(belowTitleLeft(index), 0);
        return pills;
    }
    int labelAt(const QModelIndex &index, const QPoint &point) const {
        const QList<QRect> rects = labelRects(index);
        for (int i = 0; i < rects.size(); ++i)
            if (rects[i].contains(point)) return i;
        return -1;
    }
    std::function<void(const QModelIndex &)> labelClicked;
    // Riga con la nota in scrittura (anche una nota nuova, ancora vuota) e altezza del suo campo.
    void setNoteEditing(const QModelIndex &index, int height) {
        m_noteEditIndex = index;
        m_noteEditHeight = height;
        scheduleDelayedItemsLayout();
        viewport()->update();
    }
    std::function<void(const QModelIndex &)> noteClicked;
    QRect gripRect(const QModelIndex &index) const {
        QRect item = contentRect(index);
        return {item.left(), item.top(), px(22), item.height()};
    }
    QRect markerRect(const QModelIndex &index) const {
        QRect item = contentRect(index);
        return {item.left() + px(22), item.top(), px(24), item.height()};
    }
    // FEAT-048: casella di spunta della voce, fra il marcatore e il titolo (posto "sinistra" del
    // contratto dei posti, come sulla mappa). leadPx = spazio prima del titolo: maniglia e
    // marcatore (46) piu' la casella se c'e'.
    static const MindNode *rowNode(const QModelIndex &index) {
        return index.isValid() ? static_cast<const MindNode *>(index.internalPointer()) : nullptr;
    }
    static bool hasTask(const QModelIndex &index) {
        const MindNode *node = rowNode(index);
        return node && node->task != MindNode::Task::None;
    }
    int leadPx(const QModelIndex &index) const { return px(46) + markersWidth(index) + (hasTask(index) ? px(24) : 0); }
    QRect taskRect(const QModelIndex &index) const {
        if (!hasTask(index)) return {};
        const QRect item = contentRect(index);
        return {item.left() + px(46) + markersWidth(index), item.top(), px(24), item.height()};
    }
    std::function<void(const QModelIndex &)> taskClicked;
    // Zona del titolo: qui il cursore diventa a I e il clic scrive direttamente nel testo.
    // Il testo parte dopo rientro (46) e padding (12) scalati, più 3 px di margine fisso di
    // QCommonStyle; finisce poco dopo l'ultima lettera (o a fine riga per i titoli con formule,
    // la cui larghezza disegnata qui non è nota).
    QRect titleRect(const QModelIndex &index) const {
        const QRect item = contentRect(index);
        const int left = item.left() + leadPx(index) + px(12) + 3;
        int right = item.right();
        const QString title = index.data(Qt::EditRole).toString();
        if (!FormulaText::containsMath(title)) {
            QFont rowFont = font();
            if (!index.parent().isValid()) rowFont.setWeight(QFont::Bold);
            // Titolo su più righe (Maiusc+Invio): conta la riga più larga. Con il campo aperto
            // si usa il testo che si sta scrivendo.
            QString text = index.data(Qt::DisplayRole).toString();
            if (const auto *editor = dynamic_cast<const QPlainTextEdit *>(indexWidget(index))) text = editor->toPlainText();
            const QFontMetrics metrics(rowFont);
            int widest = 0;
            for (const QString &line : text.split(QLatin1Char('\n'))) widest = qMax(widest, metrics.horizontalAdvance(line));
            right = qMin(right, left + widest + px(8));
        }
        return QRect(QPoint(left - px(4), item.top()), QPoint(right, item.bottom()));
    }

    // L'override protetto di edit() sotto nasconderebbe l'overload pubblico edit(index) usato altrove.
    using QTreeView::edit;

protected:
    // Unico punto da cui passano tutte le aperture del campo (clic, tasti, menu): con più voci
    // selezionate nessuna si può modificare.
    bool edit(const QModelIndex &index, EditTrigger trigger, QEvent *event) override {
        if (hasMultipleSelection()) return false;
        return QTreeView::edit(index, trigger, event);
    }

    void wheelEvent(QWheelEvent *event) override {
        if ((event->modifiers() & Qt::ControlModifier) && zoomStep) {
            // (B10, 2026-10-13: stesso difetto della mappa, vedi MindMapView::wheelEvent: con delta
            // verticale 0 lo zoom scendeva e sui touchpad ogni piccolo evento era un passo intero.)
            const int delta = event->angleDelta().y();
            event->accept();
            if (delta == 0) return;
            if ((delta > 0) != (m_wheelZoomRemainder > 0)) m_wheelZoomRemainder = 0;
            m_wheelZoomRemainder += delta;
            for (; m_wheelZoomRemainder >= 120; m_wheelZoomRemainder -= 120) zoomStep(1);
            for (; m_wheelZoomRemainder <= -120; m_wheelZoomRemainder += 120) zoomStep(-1);
            return;
        }
        QTreeView::wheelEvent(event);
    }

    // Il testo delle righe va a capo alla larghezza della colonna (2026-10-13): se la larghezza
    // cambia, le altezze delle righe vanno ricalcolate. QTreeView non lo fa da solo (verificato
    // con Qt 6.4: dopo un ridimensionamento le righe restavano alte come prima).
    void resizeEvent(QResizeEvent *event) override {
        QTreeView::resizeEvent(event);
        if (event->size().width() != event->oldSize().width()) scheduleDelayedItemsLayout();
    }

    void mousePressEvent(QMouseEvent *event) override {
        const QModelIndex index = indexAt(event->position().toPoint());
        const QPoint point = event->position().toPoint();
        // FEAT-065: maniglie dell'immagine scelta, immagine, marcatori e icone dopo il titolo.
        if (event->button() == Qt::LeftButton && index.isValid()) {
            if (const int corner = pictureHandleAt(index, point)) {
                m_resizeCorner = corner;
                m_resizeStart = point;
                m_resizeStartSize = rowNode(index)->imageSize;
                m_resizeStartShown = pictureSize(index);
                m_resizePreview = m_resizeStartSize;
                event->accept();
                return;
            }
            if (pictureRect(index).contains(point)) {
                if (currentIndex() != index || selectionModel()->selectedRows().size() != 1)
                    selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                if (m_pictureIndex.isValid() && m_pictureIndex != index) clearPictureSelection();
                m_pictureIndex = index;
                setFocus(Qt::MouseFocusReason);  // Canc toglie l'immagine
                viewport()->update(visualRect(index));
                event->accept();
                return;
            }
            if (const int marker = mapMarkerAt(index, point); marker >= 0) {
                clearPictureSelection();
                if (mapMarkerClicked) mapMarkerClicked(index, marker);
                viewport()->update(visualRect(index));
                event->accept();
                return;
            }
            if (const int icon = trailingIconAt(index, point); icon >= 0) {
                clearPictureSelection();
                const auto icons = trailingIcons(index);
                if (trailingIconClicked) trailingIconClicked(index, icons[icon].first, viewport()->mapToGlobal(icons[icon].second.bottomLeft()));
                event->accept();
                return;
            }
        }
        if (event->button() == Qt::LeftButton) clearPictureSelection();
        // Selezione multipla: un clic senza Ctrl/Maiusc torna a una sola voce selezionata, senza
        // aprire la modifica né afferrare la maniglia (come sulla mappa, il clic seleziona). Il
        // clic successivo sulla voce si comporta come sempre.
        if (event->button() == Qt::LeftButton && index.isValid() && hasMultipleSelection() &&
            !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) &&
            !markerRect(index).contains(event->position().toPoint())) {
            m_dragIndex = QPersistentModelIndex();
            selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            viewport()->update();
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && index.isValid() &&
            markerRect(index).contains(event->position().toPoint()) && model()->hasChildren(index)) {
            setExpanded(index, !isExpanded(index));
            viewport()->update(visualRect(index));
            event->accept();
            return;
        }
        // FEAT-060: pillole delle etichette: clic = riquadro Etichette, tasto destro = menu.
        // (Il tasto destro passa da showOutlineContextMenu, che ha il menu della pillola.)
        if (event->button() == Qt::LeftButton && index.isValid() && labelAt(index, event->position().toPoint()) >= 0) {
            if (labelClicked) labelClicked(index);
            event->accept();
            return;
        }
        // FEAT-059: clic sulla nota sotto il titolo: si scrive li'.
        if (event->button() == Qt::LeftButton && index.isValid() && noteBoxRect(index).contains(event->position().toPoint())) {
            if (noteClicked) noteClicked(index);
            event->accept();
            return;
        }
        // FEAT-048: clic sulla casella: spunta o toglie, senza aprire la modifica.
        if (event->button() == Qt::LeftButton && index.isValid() && taskRect(index).contains(event->position().toPoint())) {
            if (taskClicked) taskClicked(index);
            viewport()->update(visualRect(index));
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && index.isValid() && gripRect(index).contains(event->position().toPoint()) &&
            !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
            setCurrentIndex(index);
            m_dragIndex = index;
            m_dragStart = event->position().toPoint();
            // Mano chiusa appena si afferra la maniglia, prima ancora del trascinamento.
            viewport()->setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        // Clic sul titolo: la modifica parte subito e il clic arriva al campo di testo, che
        // mette il cursore nel punto cliccato (trascinando seleziona). Il campo è trasparente
        // e allineato al testo disegnato, quindi non si vede nessuna casella che si apre.
        if (event->button() == Qt::LeftButton && index.isValid() &&
            !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) &&
            titleRect(index).contains(event->position().toPoint()) &&
            (model()->flags(index) & Qt::ItemIsEditable)) {
            m_dragIndex = QPersistentModelIndex();
            // Con la selezione multipla (2026-10-13) la voce cliccata deve restare l'unica selezionata.
            if (currentIndex() != index || selectionModel()->selectedRows().size() != 1)
                selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            if (!indexWidget(index)) edit(index);
            if (QWidget *editor = indexWidget(index)) {
                m_textEditor = editor;
                forwardMouse(event, editor);
            }
            event->accept();
            return;
        }
        m_dragIndex = QPersistentModelIndex();
        QTreeView::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override {
        // Doppio clic nel testo in modifica: selezione della parola, come in un editor.
        const QModelIndex index = indexAt(event->position().toPoint());
        if (index.isValid() && pictureRect(index).contains(event->position().toPoint())) {  // FEAT-065
            if (pictureActivated) pictureActivated(index);
            event->accept();
            return;
        }
        if (QWidget *editor = index.isValid() ? indexWidget(index) : nullptr;
            editor && titleRect(index).contains(event->position().toPoint())) {
            m_textEditor = editor;
            forwardMouse(event, editor);
            event->accept();
            return;
        }
        QTreeView::mouseDoubleClickEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        // FEAT-065: maniglia dell'immagine trascinata: proporzioni bloccate, l'angolo segue il mouse
        // (l'immagine parte da sinistra, quindi gli angoli di sinistra crescono verso sinistra).
        if (m_resizeCorner && m_pictureIndex.isValid() && event->buttons().testFlag(Qt::LeftButton)) {
            const int direction = m_resizeCorner == 2 || m_resizeCorner == 3 ? 1 : -1;
            const qreal shown = qMax(1, m_resizeStartShown.width());
            const qreal factor = qMax(0.01, (shown + direction * (event->position().x() - m_resizeStart.x())) / shown);
            const qreal width = std::clamp(std::round(m_resizeStartSize.width() * factor), NodeItem::kMinImageWidthPx, NodeItem::kMaxImageWidthPx);
            m_resizePreview = QSizeF(width, qMax<qreal>(1, std::round(width * m_resizeStartSize.height() / qMax<qreal>(1, m_resizeStartSize.width()))));
            scheduleDelayedItemsLayout();  // l'altezza della riga segue l'immagine
            viewport()->update();
            event->accept();
            return;
        }
        // Selezione del testo in corso: il movimento va al campo di modifica.
        if (m_textEditor && event->buttons().testFlag(Qt::LeftButton)) {
            forwardMouse(event, m_textEditor);
            event->accept();
            return;
        }
        const QModelIndex hovered = indexAt(event->position().toPoint());
        // (richiesta 2026-10-12: sui 6 pallini il cursore mostra la presa)
        // Cursore a I sopra il titolo; sulla maniglia mano aperta (CSS "grab"), mano chiusa
        // (CSS "grabbing") con il tasto premuto; freccia altrove.
        const QPoint pos = event->position().toPoint();
        // Con più voci selezionate niente mano (non si spostano) e niente cursore a I (non si scrive).
        const bool multiple = hasMultipleSelection();
        if (m_dragIndex.isValid() && event->buttons().testFlag(Qt::LeftButton))
            viewport()->setCursor(Qt::ClosedHandCursor);
        else if (!multiple && hovered.isValid() && gripRect(hovered).contains(pos))
            viewport()->setCursor(Qt::OpenHandCursor);
        else if (hovered.isValid() && (pictureHandleAt(hovered, pos) == 1 || pictureHandleAt(hovered, pos) == 3))
            viewport()->setCursor(Qt::SizeFDiagCursor);  // FEAT-065
        else if (hovered.isValid() && pictureHandleAt(hovered, pos))
            viewport()->setCursor(Qt::SizeBDiagCursor);
        else if (hovered.isValid() && (mapMarkerAt(hovered, pos) >= 0 || trailingIconAt(hovered, pos) >= 0))
            viewport()->setCursor(Qt::PointingHandCursor);
        else if (!multiple && hovered.isValid() && titleRect(hovered).contains(pos))
            viewport()->setCursor(Qt::IBeamCursor);
        else
            viewport()->unsetCursor();
        if (hovered != m_hovered) {
            const QModelIndex previous = m_hovered;
            m_hovered = hovered;
            if (previous.isValid()) viewport()->update(visualRect(previous));
            if (m_hovered.isValid()) viewport()->update(visualRect(m_hovered));
        }
        if (m_dragIndex.isValid() && event->buttons().testFlag(Qt::LeftButton) &&
            (event->position().toPoint() - m_dragStart).manhattanLength() >= QApplication::startDragDistance()) {
            startDrag(Qt::MoveAction);
            m_dragIndex = QPersistentModelIndex();
            return;
        }
        // Bug 2026-10-13 (selezione multipla): maniglia premuta ma movimento ancora sotto la soglia
        // di trascinamento. Prima l'evento andava a QTreeView::mouseMoveEvent, che con
        // ExtendedSelection seleziona trascinando dal punto dell'ultima pressione che ha visto lui
        // (la pressione sulla maniglia non gli arriva, quindi è un punto vecchio): le voci tra quel
        // punto e la maniglia diventavano selezionate e startDrag() rifiutava lo spostamento
        // perché c'era una selezione multipla. Con SingleSelection, prima, non succedeva.
        if (m_dragIndex.isValid() && event->buttons().testFlag(Qt::LeftButton)) {
            event->accept();
            return;
        }
        QTreeView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (m_resizeCorner) {  // FEAT-065: la misura nuova vale anche sulla mappa (un passo)
            m_resizeCorner = 0;
            const QPersistentModelIndex index = m_pictureIndex;
            if (index.isValid() && m_resizePreview != m_resizeStartSize && pictureResized) pictureResized(index, m_resizePreview);
            scheduleDelayedItemsLayout();
            viewport()->update();
            event->accept();
            return;
        }
        m_dragIndex = QPersistentModelIndex();
        // Rilasciata la maniglia senza trascinare: torna la mano aperta (o la freccia se fuori).
        const QModelIndex released = indexAt(event->position().toPoint());
        if (released.isValid() && gripRect(released).contains(event->position().toPoint()))
            viewport()->setCursor(Qt::OpenHandCursor);
        if (m_textEditor) {
            forwardMouse(event, m_textEditor);
            m_textEditor = nullptr;
            event->accept();
            return;
        }
        QTreeView::mouseReleaseEvent(event);
    }

    // Inoltra un evento del mouse dalla vista al campo di modifica, con le coordinate
    // convertite: il campo gestisce da sé posizione del cursore, selezione e doppio clic.
    void forwardMouse(QMouseEvent *event, QWidget *target) {
        // BUG-002: se il campo ignora l'evento, Qt lo propaga al suo genitore, cioè di nuovo a questa
        // vista, che lo reinoltrerebbe al campo all'infinito fino a esaurire lo stack. Durante un
        // inoltro l'evento che torna indietro si ferma qui (il chiamante lo accetta).
        if (m_forwardingMouse) return;
        const QScopedValueRollback forwarding(m_forwardingMouse, true);
        // Campo multiriga (QPlainTextEdit): il mouse lo gestisce il suo viewport, non il widget esterno.
        if (auto *area = qobject_cast<QAbstractScrollArea *>(target)) target = area->viewport();
        const QPointF local = target->mapFrom(viewport(), event->position().toPoint());
        QMouseEvent copy(event->type(), local, event->globalPosition(), event->button(),
                         event->buttons(), event->modifiers());
        QApplication::sendEvent(target, &copy);
    }

    // Prima si usava lo startDrag() standard di QAbstractItemView, che:
    // - disegna la riga trascinata completamente opaca e larga quanto tutta la vista,
    //   coprendo le righe sotto;
    // - calcola il punto di aggancio dalla posizione di pressione salvata da
    //   QAbstractItemView::mousePressEvent, che qui non viene chiamato quando si preme
    //   sulla maniglia: l'anteprima finiva quindi spostata rispetto al cursore (inferito
    //   dalla lettura del codice, non dai sorgenti Qt).
    // Ora l'anteprima è molto trasparente, larga solo quanto il testo e agganciata
    // esattamente nel punto in cui è stata afferrata.
    void startDrag(Qt::DropActions supportedActions) override {
        if (hasMultipleSelection()) return;  // più voci selezionate: non si spostano (2026-10-13)
        const QModelIndex index = m_dragIndex.isValid() ? QModelIndex(m_dragIndex) : currentIndex();
        if (!index.isValid()) { QTreeView::startDrag(supportedActions); return; }
        QMimeData *mime = model()->mimeData({index});
        if (!mime) return;

        QRect rect = visualRect(index);
        QFont rowFont = font();
        if (!index.parent().isValid()) rowFont.setWeight(QFont::Bold);
        const int textWidth = QFontMetrics(rowFont).horizontalAdvance(index.data(Qt::DisplayRole).toString());
        rect.setWidth(qMin(rect.width(), leadPx(index) + textWidth + px(32)));
        const QPixmap source = viewport()->grab(rect);
        QPixmap ghost(source.size());
        ghost.setDevicePixelRatio(source.devicePixelRatio());
        ghost.fill(Qt::transparent);
        {
            QPainter painter(&ghost);
            painter.setOpacity(kDragGhostOpacity);
            painter.drawPixmap(0, 0, source);
        }

        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(ghost);
        // Punto di aggancio = punto della riga in cui è stato premuto il mouse.
        drag->setHotSpot(m_dragStart - rect.topLeft());
        drag->exec(supportedActions, Qt::MoveAction);
    }

    void leaveEvent(QEvent *event) override {
        const QModelIndex previous = m_hovered;
        m_hovered = QPersistentModelIndex();
        if (previous.isValid()) viewport()->update(visualRect(previous));
        QTreeView::leaveEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->mimeData()->hasFormat("application/x-mentis-node")) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }
        QTreeView::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent *event) override {
        const QModelIndex index = indexAt(event->position().toPoint());
        if (!index.isValid() || !event->mimeData()->hasFormat("application/x-mentis-node")) {
            clearDropHint();
            event->ignore();
            return;
        }
        const QRect row = contentRect(index);
        const qreal relativeY = row.height() > 0
            ? qreal(event->position().y() - row.top()) / qreal(row.height()) : 0.5;
        const DropHint hint = relativeY < 0.30 ? DropHint::Before
                            : relativeY > 0.70 ? DropHint::After : DropHint::Inside;
        setDropHint(index, hint);
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override {
        clearDropHint();
        QTreeView::dragLeaveEvent(event);
    }

    void dropEvent(QDropEvent *event) override {
        if (!m_dropIndex.isValid() || m_dropHint == DropHint::None) {
            clearDropHint();
            event->ignore();
            return;
        }
        const QPersistentModelIndex target = m_dropIndex;
        const DropHint hint = m_dropHint;
        QModelIndex parentIndex;
        int row = -1;
        if (hint == DropHint::Inside) {
            parentIndex = target;
        } else {
            parentIndex = target.parent();
            row = target.row() + (hint == DropHint::After ? 1 : 0);
        }
        // Il modello viene resettato durante lo spostamento: nessun indice grafico
        // del drag deve sopravvivere a dropMimeData().
        clearDropHint();
        const bool moved = model()->dropMimeData(event->mimeData(), Qt::MoveAction, row, 0, parentIndex);
        if (moved) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        } else {
            event->ignore();
        }
    }

private:
    void setDropHint(const QModelIndex &index, DropHint hint) {
        if (m_dropIndex == index && m_dropHint == hint) return;
        m_dropIndex = index;
        m_dropHint = hint;
        doItemsLayout();
        viewport()->update();
    }

    void clearDropHint() {
        if (!m_dropIndex.isValid() && m_dropHint == DropHint::None) return;
        m_dropIndex = QPersistentModelIndex();
        m_dropHint = DropHint::None;
        doItemsLayout();
        viewport()->update();
    }

    // Opacità dell'anteprima durante il trascinamento (0 = invisibile, 1 = opaca).
    // Prima era 0.35, alzata su richiesta per vedere meglio la riga trascinata.
    static constexpr qreal kDragGhostOpacity = 0.5;
    QPersistentModelIndex m_hovered;
    QPersistentModelIndex m_pictureIndex;  // FEAT-065: immagine scelta (con le maniglie)
    int m_resizeCorner = 0;
    QPoint m_resizeStart;
    QSizeF m_resizeStartSize, m_resizePreview;
    QSize m_resizeStartShown;
    QPersistentModelIndex m_dragIndex;
    QPersistentModelIndex m_dropIndex;
    DropHint m_dropHint = DropHint::None;
    QPoint m_dragStart;
    qreal m_zoom = 1.0;
    // Campo di modifica che sta ricevendo il trascinamento del mouse (selezione del testo).
    QPointer<QWidget> m_textEditor;
    bool m_forwardingMouse = false;  // vero mentre forwardMouse invia un evento al campo (BUG-002)
};

// FEAT-065: icona della riga nel colore del testo dell'outliner (chiaro sul tema scuro).
QPixmap outlineIcon(const QString &path, const QColor &color, int px) {
    const QString key = QStringLiteral("bc-outline-icon:%1:%2:%3").arg(path, color.name()).arg(px);
    QPixmap pixmap;
    if (QPixmapCache::find(key, &pixmap)) return pixmap;
    pixmap = QIcon(path).pixmap(QSize(px, px), 2.0);
    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
    painter.end();
    QPixmapCache::insert(key, pixmap);
    return pixmap;
}

class OutlineDelegate final : public QStyledItemDelegate {
public:
    OutlineDelegate(std::function<void()> addSibling, std::function<void()> indent,
                    std::function<void()> outdent, QObject *parent)
        : QStyledItemDelegate(parent), m_addSibling(std::move(addSibling)),
          m_indent(std::move(indent)), m_outdent(std::move(outdent)) {}

    // Fra due passi della stessa modifica del titolo (vedi setModelData): il passo va registrato
    // subito nello storico, come NodeItem::historyStepFinished per la mappa.
    std::function<void()> historyStepFinished;
    std::function<void(const QModelIndex &, int)> taskTyped;  // FEAT-068: "- [ ] " a inizio titolo
    std::function<bool(const QModelIndex &)> pasteWhileEditing;  // FEAT-094
    std::function<int(const QKeyEvent *)> charStyleKey;  // FEAT-095

    // Conferma e chiude il campo aperto, se c'è (2026-10-14, MainWindow::commitPendingEdits): come
    // Invio, senza dipendere dalla perdita del focus, che con la finestra non attiva non arriva.
    void commitOpenEditor() {
        if (!m_editor) return;
        QWidget *editor = m_editor;
        emit commitData(editor);
        emit closeEditor(editor, QAbstractItemDelegate::NoHint);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        const auto *view = dynamic_cast<const OutlineTreeView *>(parent());
        QStyleOptionViewItem content(option);
        // Misure del delegate scalate con lo zoom dell'outliner (1:1 senza vista).
        const auto px = [view](int value) { return view ? view->px(value) : value; };
        const qreal z = view ? view->zoom() : 1.0;
        if (view && view->dropIndex() == index && view->dropHint() == OutlineTreeView::DropHint::Before)
            content.rect.adjust(0, px(14), 0, 0);
        if (view && view->dropIndex() == index && view->dropHint() == OutlineTreeView::DropHint::After)
            content.rect.adjust(0, 0, 0, -px(14));

        // Gli argomenti mobili stanno in fondo, sotto una linea che li separa dai rami.
        if (!index.parent().isValid() && index.row() > 0) {
            const auto *node = static_cast<const MindNode *>(index.internalPointer());
            const auto *previous = static_cast<const MindNode *>(index.siblingAtRow(index.row() - 1).internalPointer());
            if (node && previous && node->floating && !previous->floating) {
                painter->save();
                painter->setPen(QPen(outlineColors().badge, 1));
                painter->drawLine(QPointF(option.rect.left() + px(22), option.rect.top() + 0.5), QPointF(option.rect.right() - px(4), option.rect.top() + 0.5));
                painter->restore();
            }
        }
        const bool selected = content.state.testFlag(QStyle::State_Selected);
        if (view && view->dropIndex() == index && view->dropHint() == OutlineTreeView::DropHint::Inside) {
            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(outlineColors().dropInside);
            QRect highlight = content.rect.adjusted(px(22), px(2), -px(4), -px(2));
            painter->drawRoundedRect(highlight, 4, 4);
            painter->restore();
        } else if (selected) {
            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(outlineColors().selection);
            QRect highlight = content.rect.adjusted(px(22), px(2), -px(4), -px(2));
            painter->drawRoundedRect(highlight, 4, 4);
            painter->restore();
        }
        // Evita che il tema di sistema colori separatamente ramo e indentazione.
        content.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus);
        // Su una riga selezionata l'hover non deve coprire l'evidenziazione azzurra:
        // senza State_MouseOver la regola QSS "QTreeView::item:hover" non viene applicata
        // all'area del testo. La zona della maniglia (primi 22 px) resta grigia perché
        // è disegnata dalla vista, fuori dal rettangolo azzurro.
        content.state &= ~QStyle::State_MouseOver;
        content.rect.adjust(view ? view->leadPx(index) : px(46), 0, 0, 0);
        const int noteHeight = view ? view->noteLinesHeight(index) : 0;  // FEAT-059
        content.rect.setBottom(content.rect.bottom() - noteHeight);
        // Riga in modifica: il testo lo mostra il campo trasparente sopra; disegnarlo anche
        // qui lo farebbe vedere doppio (soprattutto con le formule, dove il campo mostra il sorgente).
        const bool editing = view && view->indexWidget(index);
        // initStyleOption, che rimette il testo del modello; il vecchio titolo restava visibile sotto
        // il campo trasparente e si sovrapponeva a quello digitato. Ora il testo si toglie in initStyleOption.)
        m_paintingEditedRow = editing;
        // Titolo con corsivo, barrato o sottolineato (richiesta 2026-10-13): lo stile disegna
        // solo testo semplice, quindi qui il testo si toglie (initStyleOption) e lo si disegna
        // dopo con i suoi formati, nello stesso rettangolo e con lo stesso colore dello stile.
        const QList<QTextLayout::FormatRange> formats = editing ? QList<QTextLayout::FormatRange>() : formatsFor(index);
        QStyleOptionViewItem formatted(content);
        if (!formats.isEmpty()) initStyleOption(&formatted, index);
        m_paintingFormattedRow = !formats.isEmpty();
        QStyledItemDelegate::paint(painter, content, index);
        m_paintingFormattedRow = false;
        m_paintingEditedRow = false;
        if (!formats.isEmpty()) drawFormattedText(painter, formatted, index, formats);
        // Titolo con formule: il testo semplice è stato tolto in initStyleOption e qui
        // si disegna il rendering completo (testo + formule) nella stessa area.
        if (const FormulaText *formula = editing ? nullptr : formulaFor(option, index))
            // 12 = padding QSS della riga (scalato); 3 = margine fisso del testo di QCommonStyle (PM_FocusFrameHMargin + 1), non scalato.
            formula->draw(painter, content.rect.adjusted(px(12) + 3, 0, -px(12), 0), Qt::AlignLeft | Qt::AlignVCenter);

        if (!view) return;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);

        if (view->dropIndex() == index && view->dropHint() != OutlineTreeView::DropHint::Inside) {
            const bool before = view->dropHint() == OutlineTreeView::DropHint::Before;
            const QRect row = option.rect;
            const int y = before ? row.top() + px(6) : row.bottom() - px(6);
            painter->setBrush(outlineColors().dropLine);
            painter->drawRoundedRect(QRect(row.left() + px(22), y - px(2), qMax(px(20), row.width() - px(30)), px(4)), 2 * z, 2 * z);
            painter->drawEllipse(QPointF(row.left() + px(22), y), 4 * z, 4 * z);
        }

        const QRect marker = view->markerRect(index);
        const QPointF center(marker.center());
        painter->setBrush(outlineColors().marker);
        if (index.model()->hasChildren(index)) {
            QPolygonF triangle;
            if (view->isExpanded(index))
                triangle << QPointF(center.x() - 5 * z, center.y() - 3 * z) << QPointF(center.x() + 5 * z, center.y() - 3 * z)
                         << QPointF(center.x(), center.y() + 4 * z);
            else
                triangle << QPointF(center.x() - 3 * z, center.y() - 5 * z) << QPointF(center.x() - 3 * z, center.y() + 5 * z)
                         << QPointF(center.x() + 4 * z, center.y());
            painter->drawPolygon(triangle);
        } else {
            painter->drawEllipse(center, 3.1 * z, 3.1 * z);
        }
        // FEAT-048: casella di spunta come sulla mappa (vuota: bordo del testo; spuntata: piena del
        // colore del ramo con la spunta chiara o scura).
        if (const QRect task = view->taskRect(index); !task.isEmpty()) {
            const MindNode *node = OutlineTreeView::rowNode(index);
            const qreal box = 16 * z;
            const QRectF check(QPointF(task.center()) - QPointF(box / 2, box / 2), QSizeF(box, box));
            if (node->task == MindNode::Task::Done) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(node->branchColor);
                painter->drawRoundedRect(check, 4 * z, 4 * z);
                QPainterPath tick;
                tick.moveTo(check.left() + 3.5 * z, check.center().y() + 0.5 * z);
                tick.lineTo(check.left() + 6.5 * z, check.bottom() - 4 * z);
                tick.lineTo(check.right() - 3.5 * z, check.top() + 4.5 * z);
                painter->setPen(QPen(NodeStyle::readableTextOn(node->branchColor), 2 * z, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter->setBrush(Qt::NoBrush);
                painter->drawPath(tick);
            } else {
                painter->setPen(QPen(outlineColors().text, 1.5 * z));
                painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(check.adjusted(0.75, 0.75, -0.75, -0.75), 4 * z, 4 * z);
            }
            painter->setPen(Qt::NoPen);
            painter->setBrush(outlineColors().marker);
        }
        // FEAT-065: marcatori come sulla mappa (16 px), prima della casella.
        if (const MindNode *node = OutlineTreeView::rowNode(index); node && !node->markers.isEmpty()) {
            const qreal icon = 16 * z;
            for (int i = 0; i < node->markers.size(); ++i) {
                const QPointF centre = QRectF(view->mapMarkerRect(index, i)).center();
                Markers::paint(painter, QRectF(centre - QPointF(icon / 2, icon / 2), QSizeF(icon, icon)), node->markers[i]);
            }
            painter->setPen(Qt::NoPen);
        }
        // FEAT-065: immagine, adesivo o formula sotto il titolo; scelta: contorno e maniglie nel
        // colore della selezione, come sulla mappa.
        if (const QRect picture = view->pictureRect(index); !picture.isEmpty()) {
            const MindNode *node = OutlineTreeView::rowNode(index);
            const QPixmap pixels = view->picture ? view->picture(node) : QPixmap();
            painter->save();
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            if (pixels.isNull()) {
                painter->setPen(QPen(outlineColors().grip, 1, Qt::DashLine));
                painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(QRectF(picture).adjusted(0.5, 0.5, -0.5, -0.5), px(4), px(4));
            } else {
                painter->setOpacity(node->imageStyle.opacity / 100.0);
                painter->drawPixmap(picture, pixels, pixels.rect());
                painter->setOpacity(1.0);
            }
            if (view->isPictureSelected(index)) {
                const QColor accent = NodeItem::selectionStyle().selectedColor;
                painter->setPen(QPen(accent, 1));
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(QRectF(picture).adjusted(0.5, 0.5, -0.5, -0.5));
                painter->setBrush(Qt::white);
                for (int corner = 1; corner <= 4; ++corner) painter->drawRect(QRectF(view->pictureHandle(index, corner)).adjusted(0.5, 0.5, -0.5, -0.5));
            }
            painter->restore();
        }
        // FEAT-065: collegamenti e graffetta dei file subito dopo il titolo.
        for (const auto &[kind, rect] : view->trailingIcons(index)) {
            const QString path = kind == OutlineTreeView::TrailingIcon::TopicLink ? QStringLiteral(":/assets/hugeicons/link.svg")
                                 : kind == OutlineTreeView::TrailingIcon::WebLink ? QStringLiteral(":/assets/hugeicons/link-web.svg")
                                                                                  : QStringLiteral(":/assets/hugeicons/attachment.svg");
            const int icon = px(16);
            painter->drawPixmap(QRect(rect.center() - QPoint(icon / 2, icon / 2), QSize(icon, icon)), outlineIcon(path, outlineColors().text, icon));
        }
        // FEAT-059: nota sotto il titolo, tutta, in un riquadro grigio (testo grigio). Durante la
        // scrittura la disegna il campo che sta sopra.
        if (const QRect box = view->noteBoxRect(index); !box.isEmpty() && !view->isNoteEditing(index)) {
            painter->save();
            painter->setPen(Qt::NoPen);
            painter->setBrush(outlineNoteBackground());
            painter->drawRoundedRect(box, px(4), px(4));
            painter->translate(box.left() + view->notePaddingX(), box.top() + view->notePaddingY());
            QAbstractTextDocumentLayout::PaintContext context;
            context.palette.setColor(QPalette::Text, outlineColors().grip);
            context.clip = QRectF(0, 0, box.width(), box.height());
            view->noteDocument(index)->documentLayout()->draw(painter, context);
            painter->restore();
        }
        // FEAT-060: pillole delle etichette, come sulla mappa (chiare o invertite sul tema scuro).
        if (const QList<QRect> pills = view->labelRects(index); !pills.isEmpty()) {
            const bool dark = outlineColors().page != QColor("#ffffff");
            const MindNode *node = OutlineTreeView::rowNode(index);
            painter->save();
            painter->setFont(view->labelFont());
            for (int i = 0; i < pills.size(); ++i) {
                const QRectF pill = QRectF(pills[i]).adjusted(0.5, 0.5, -0.5, -0.5);
                painter->setPen(QPen(dark ? QColor(255, 255, 255, 110) : QColor(15, 23, 42, 40), 1));
                painter->setBrush(dark ? QColor(255, 255, 255, 20) : QColor(255, 255, 255, 230));
                painter->drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
                painter->setPen(dark ? QColor(255, 255, 255, 220) : QColor("#6b7280"));
                drawLabelText(painter, pills[i], node->labels.value(i), px(8));
            }
            painter->restore();
        }

        // Nodo chiuso: badge rotondo accanto al titolo con il numero di nodi nascosti (figli e
        // sottonodi a ogni livello), richiesta 2026-10-12. Non durante la modifica: il campo
        // cambia larghezza mentre si scrive.
        if (!editing && index.model()->hasChildren(index) && !view->isExpanded(index)) {
            const int hidden = descendantCount(index);
            // Fine del testo: inizio del titolo + larghezza (testo semplice o formula).
            const QRect title = view->titleRect(index);
            const int textLeft = title.left() + px(4);
            int textRight = title.right() - px(8);
            if (const FormulaText *formula = formulaFor(option, index)) textRight = textLeft + qCeil(formula->size().width());
            if (const auto icons = view->trailingIcons(index); !icons.isEmpty()) textRight = icons.back().second.right() - px(4);  // FEAT-065
            // Badge: testo 12px peso 600 (scala tipografica), altezza 20, padding orizzontale 4,
            // distanza 8 dal titolo, raggio = metà altezza (forma a pillola/cerchio).
            QFont badgeFont = view->font();
            badgeFont.setPixelSize(px(UiTheme::fontPx(12)));
            badgeFont.setWeight(QFont::DemiBold);
            const QString label = QString::number(hidden);
            const int height = px(20);
            const int width = qMax(height, QFontMetrics(badgeFont).horizontalAdvance(label) + 2 * px(4));
            const QRectF badge(textRight + px(8), option.rect.center().y() - height / 2.0 + 0.5, width, height);
            painter->setBrush(outlineColors().badge);
            painter->drawRoundedRect(badge, height / 2.0, height / 2.0);
            painter->setPen(outlineColors().text);
            painter->setFont(badgeFont);
            painter->drawText(badge, Qt::AlignCenter, label);
            painter->setPen(Qt::NoPen);
        }

        // Con più voci selezionate i 6 pallini non compaiono: il gruppo non si sposta (2026-10-13).
        if (view->hoveredIndex() == index && !view->hasMultipleSelection()) {
            const QRect grip = view->gripRect(index);
            painter->setBrush(outlineColors().grip);
            const int centerY = grip.center().y();
            for (int column = 0; column < 2; ++column)
                for (int row = -1; row <= 1; ++row)
                    painter->drawEllipse(QPointF(grip.left() + 6 * z + column * 6 * z, centerY + row * 6 * z), 1.6 * z, 1.6 * z);
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        // Riga in modifica: si misura il testo del campo (initStyleOption legge m_measuringEditor).
        m_measuringEditor = m_editor && index == m_editIndex;
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        // Testo come lo vede lo stile, già spezzato in righe da initStyleOption (a capo dell'utente
        // e a capo automatico alla larghezza della riga, 2026-10-13).
        QStyleOptionViewItem measured(option);
        initStyleOption(&measured, index);
        m_measuringEditor = false;
        // Titolo su più righe (Maiusc+Invio): sopra e sotto il testo resta lo stesso spazio
        // di una riga singola (48 di altezza minima QSS − una riga di testo), non solo il padding 4.
        const QString title = (m_editor && index == m_editIndex) ? m_editor->text() : index.data(Qt::EditRole).toString();
        if (const int lines = int(measured.text.count(QChar::LineSeparator) + measured.text.count(QLatin1Char('\n'))) + 1;
            lines > 1 && !FormulaText::containsMath(title)) {
            const auto *zoomView = dynamic_cast<const OutlineTreeView *>(parent());
            const int lineSpacing = QFontMetrics(option.font).lineSpacing();
            const int rowMin = zoomView ? zoomView->px(48) : 48;
            size.setHeight(qMax(size.height(), lines * lineSpacing + (rowMin - lineSpacing)));
        }
        // Le righe con formule alte (frazioni, matrici...) diventano più alte.
        if (const FormulaText *formula = formulaFor(option, index))
            size.setHeight(qMax(size.height(), qCeil(formula->size().height()) + 16));
        const auto *view = dynamic_cast<const OutlineTreeView *>(parent());
        if (view && view->dropIndex() == index &&
            (view->dropHint() == OutlineTreeView::DropHint::Before ||
             view->dropHint() == OutlineTreeView::DropHint::After))
            size.rheight() += view->px(14);
        if (view) size.rheight() += view->noteLinesHeight(index);  // FEAT-059
        return size;
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override {
        auto *editor = new OutlineTitleEditor(parent);
        editor->boldWeight = rowBoldWeights(index).bold;  // FEAT-068
        // Scrittura diretta nel testo: il campo ha lo stesso font della riga (grassetto al primo
        // livello, dimensione dello zoom) ed è trasparente e senza bordo, quindi non si nota.
        QFont font = option.font;
        if (!index.parent().isValid()) font.setWeight(QFont::Bold);
        editor->setFont(font);
        // Colore della selezione da Impostazioni > Selezione (2026-10-13), predefinito #b3d4fc.
        editor->setStyleSheet(QString("QPlainTextEdit{background:transparent;border:0;padding:0;margin:0;color:%2;"
                                      "selection-background-color:%1;selection-color:#202630;}")
                                  .arg(NodeItem::selectionStyle().textSelectionColor.name(), outlineColors().text.name()));
        // FEAT-042: our text menu instead of the system one; the event stops there, so the
        // Outliner's own menu does not open with the field open.
        TextEditMenu::attach(editor);
        editor->installEventFilter(const_cast<OutlineDelegate *>(this));
        // Righe aggiunte o tolte mentre si scrive: la riga dell'outliner cambia altezza.
        const QPersistentModelIndex persistent(index);
        // (2026-10-13: con l'a capo automatico le righe cambiano anche senza nuovi paragrafi;
        // documentSizeChanged di QPlainTextDocumentLayout arriva a ogni cambio del numero di righe)
        connect(editor->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, [this, persistent] {
            emit const_cast<OutlineDelegate *>(this)->sizeHintChanged(persistent);
        });
        editor->pasteContent = [this, persistent] { return pasteWhileEditing && pasteWhileEditing(persistent); };
        m_editor = editor;
        m_editIndex = persistent;
        // Barra corsivo/barrato/sottolineato sopra la selezione (richiesta 2026-10-13). Una sola
        // barra per vista; si nasconde quando il campo si chiude. È figlia della finestra e non
        // del viewport, così sopra la prima riga può uscire dall'elenco (sul titolo della mappa).
        if (!m_formatBar) m_formatBar = new OutlineFormatBar(parent->window());
        m_formatBar->setFocusProxy(editor);
        m_formatBar->weights = rowBoldWeights(index);  // 2026-10-14
        editor->formatBar = m_formatBar;
        editor->charStyleKey = charStyleKey;
        editor->plainTyping.active = true;  // FEAT-095
        m_formatBar->applyFormat = [editor](const QTextCharFormat &format) {
            // Applicato alla selezione: è un passo di annullamento del campo (Ctrl+Z) e poi un
            // passo distinto dello storico (setModelData).
            editor->mergeCurrentCharFormat(format);
        };
        m_formatBar->applyHighlight = [editor](bool on) {
            QTextCursor cursor = editor->textCursor();
            MarkdownShortcuts::setHighlight(cursor, on);
            editor->setTextCursor(cursor);
        };
        connect(editor, &QPlainTextEdit::selectionChanged, this, [this, editor] { updateFormatBar(editor); });
        connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this, editor] { updateFormatBar(editor); });
        connect(editor, &QObject::destroyed, m_formatBar.data(), &QWidget::hide);
        return editor;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        if (auto *title = dynamic_cast<OutlineTitleEditor *>(editor)) {
            title->setText(index.data(Qt::EditRole).toString());
            // Corsivo, barrato e sottolineato del titolo formattato (fatti sulla mappa o qui).
            const QList<QTextLayout::FormatRange> formats = formatsFor(index);
            if (!formats.isEmpty()) {
                QTextCursor cursor(title->document());
                for (const auto &range : formats) {
                    cursor.setPosition(range.start);
                    cursor.setPosition(range.start + range.length, QTextCursor::KeepAnchor);
                    cursor.mergeCharFormat(range.format);
                }
                // Il formato iniziale non è una modifica: Ctrl+Z nel campo non deve toglierlo.
                title->document()->clearUndoRedoStacks();
            }
            return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        if (auto *title = dynamic_cast<OutlineTitleEditor *>(editor)) {
            auto *mapModel = qobject_cast<MindMapModel *>(model);
            const MindNode *node = mapModel ? mapModel->nodeFromIndex(index) : nullptr;
            if (!node) { model->setData(index, title->text(), Qt::EditRole); return; }
            // (richiesta 2026-10-13: setData sostituisce il titolo con testo semplice e cancella
            // titleHtml, quindi la formattazione fatta sulla mappa spariva a ogni modifica del testo
            // nell'outliner. Ora si conserva e si aggiungono corsivo, barrato e sottolineato del campo.)
            // Come sulla mappa (NodeItem::finishEditing) ogni passo di annullamento del campo diventa
            // un passo di storico distinto: si annulla il documento fino all'apertura salvando ogni
            // stato, poi lo si ripristina con redo (il campo può restare aperto).
            QTextDocument *document = title->document();
            std::vector<std::unique_ptr<QTextDocument>> steps;
            steps.emplace_back(document->clone());
            int undone = 0;
            while (document->isUndoAvailable()) {
                document->undo();
                ++undone;
                steps.emplace(steps.begin(), document->clone());
            }
            for (int i = 0; i < undone && document->isRedoAvailable(); ++i) document->redo();
            const QFont font = NodeItem::titleFont(node);
            // steps[0] è il titolo all'apertura del campo: non è una modifica. Se il campo non ha
            // passi di annullamento si confronta comunque il suo stato con il modello.
            for (size_t i = steps.size() > 1 ? 1 : 0; i < steps.size(); ++i) {
                const bool last = i + 1 == steps.size();
                const QString text = steps[i]->toPlainText();
                // Stati intermedi vuoti (titolo cancellato e riscritto) non sono passi utili.
                if (!last && text.isEmpty()) continue;
                // Nel campo il grassetto ha il peso della riga; nel titolo salvato quello della mappa (700).
                const QString html = mergeOutlineFormats(node->title, node->titleHtml, *steps[i], font,
                                                         rowBoldWeights(index).bold, storedBoldWeights(node));
                if (text == node->title && html == node->titleHtml) continue;  // nessun cambiamento
                mapModel->setTitle(index, text, html);
                if (!last && historyStepFinished) historyStepFinished();
            }
            // FEAT-068: casella scritta a inizio titolo; dopo il titolo, come sulla mappa.
            if (title->pendingTask && taskTyped) {
                const int task = std::exchange(title->pendingTask, 0);
                const QPersistentModelIndex persistent(index);
                const auto callback = taskTyped;
                QTimer::singleShot(0, mapModel, [callback, persistent, task] { if (persistent.isValid()) callback(persistent, task); });
            }
            return;
        }
        QStyledItemDelegate::setModelData(editor, model, index);
    }

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override {
        const auto *view = dynamic_cast<const OutlineTreeView *>(parent());
        if (!view) { QStyledItemDelegate::updateEditorGeometry(editor, option, index); return; }
        // Il testo del campo deve cadere esattamente dove era disegnato: inizio del testo
        // (rientro 46 + padding 12 scalati + 3 di QCommonStyle) meno i 2 px di margine
        // interno di QLineEdit. Così all'inizio della modifica le lettere non si spostano.
        QRect rect = option.rect;
        rect.setBottom(rect.bottom() - view->noteLinesHeight(index));  // FEAT-059: solo la parte del titolo
        const QRect titleArea = rect;
        rect.setLeft(rect.left() + view->leadPx(index) + view->px(12) + 3 + kEditorTextOffset);
        // Il campo va a capo alla sua larghezza: deve essere largo quanto il testo disegnato
        // (padding 12 scalato + margine di 3 px di QCommonStyle, come a sinistra).
        rect.setRight(rect.right() - view->px(12) - 3);
        // Campo multiriga: alto quanto le sue righe e centrato in verticale come il testo
        // disegnato dalla riga (QCommonStyle centra il blocco di testo).
        if (auto *title = dynamic_cast<OutlineTitleEditor *>(editor)) {
            const int height = title->contentHeight() + 2 * kEditorPadding;
            // Centratura come QStyle::alignedRect (stesso arrotondamento del testo disegnato):
            // verificato al pixel con una e due righe (test t4, passi 2 e 9).
            // Centrato nella parte del titolo, non in tutta la riga: con nota o etichette sotto il campo
            // finiva sopra di loro (2026-10-19).
            const int top = titleArea.top() + (titleArea.height() - height) / 2;
            rect = QRect(rect.left(), top, rect.width(), height);
            editor->setGeometry(rect);
            updateFormatBar(title);  // la riga si è spostata o ha cambiato altezza
            return;
        }
        editor->setGeometry(rect);
    }

    // Numero di nodi sotto index, a tutte le profondità. Si calcola solo per le righe chiuse
    // visibili e costa quanto il ramo nascosto: nessuna cache da invalidare a ogni modifica.
    static int descendantCount(const QModelIndex &index) {
        const QAbstractItemModel *model = index.model();
        const int rows = model->rowCount(index);
        int total = rows;
        for (int row = 0; row < rows; ++row) total += descendantCount(model->index(row, 0, index));
        return total;
    }

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        // Voci di primo livello (figlie dirette del titolo) in grassetto.
        // Il peso 700 è quello dei titoli in AGENTS.md; vale anche per le formule (stesso font).
        if (!index.parent().isValid()) option->font.setWeight(QFont::Bold);
        // Se il titolo contiene formule, il testo lo disegna paint() con FormulaText.
        if (FormulaText::containsMath(index.data(Qt::EditRole).toString())) option->text.clear();
        // Riga in modifica: il testo lo mostra il campo trasparente sopra. Il flag vale solo
        // durante paint(), così sizeHint() misura ancora il testo e l'altezza della riga non cambia.
        if (m_paintingEditedRow) option->text.clear();
        // Misura della riga in modifica con il testo del campo; "\n" diventa U+2028, il
        // separatore di riga che QCommonStyle usa per andare a capo nel testo della voce.
        if (m_measuringEditor) option->text = m_editor->text().replace(QLatin1Char('\n'), QChar::LineSeparator);
        // A capo alla larghezza della riga (richiesta 2026-10-13, come nella mappa).
        // Il corsivo cambia la larghezza delle lettere: l'a capo si calcola con i formati del titolo
        // (o del campo aperto), gli stessi usati per disegnarlo (drawFormattedText).
        if (!option->text.isEmpty()) {
            const QList<QTextLayout::FormatRange> formats = m_measuringEditor
                ? inlineRanges(inlineFlagsPerChar(*m_editor->document(), rowBoldWeights(index).bold), 0,
                               int(m_editor->text().size()), rowBoldWeights(index).bold)
                : formatsFor(index);
            option->text = wrappedText(option->text, option->font, textWidth(index), formats);
        }
        // Riga con formati: il testo lo disegna paint() (drawFormattedText). Come per la riga in
        // modifica, il flag vale solo durante paint(): sizeHint() misura ancora il testo.
        if (m_paintingFormattedRow) option->text.clear();
    }

    // Larghezza del testo di una riga: colonna, meno rientro del livello, meno maniglia e
    // marcatore (46), padding QSS a sinistra e a destra (12) e i 3 px che QCommonStyle lascia per
    // lato (PM_FocusFrameHMargin + 1). È la stessa larghezza a cui lo stile spezza il testo e a cui
    // va a capo il campo di modifica (updateEditorGeometry).
    int textWidth(const QModelIndex &index) const {
        const auto *view = dynamic_cast<const OutlineTreeView *>(parent());
        if (!view) return QWIDGETSIZE_MAX;
        int depth = 0;
        for (QModelIndex up = index.parent(); up.isValid(); up = up.parent()) ++depth;
        return qMax(1, view->columnWidth(0) - depth * view->indentation() - view->leadPx(index) - 2 * view->px(12) - 2 * 3);
    }

    // Lo stile (QCommonStyle) va a capo solo tra una parola e l'altra: una parola più larga della
    // riga usciva tagliata. Qui il testo si impagina con WrapAtWordBoundaryOrAnywhere, come i nodi
    // della mappa (https://doc.qt.io/qt-6/qtextoption.html#WrapMode-enum), e ogni riga finisce con
    // U+2028: lo stile disegna e misura le stesse righe, senza bisogno di riscriverne il disegno.
    // Risultato in cache per testo, font e larghezza (paint e sizeHint lo chiedono spesso).
    // (2026-10-13: i formati, in particolare il corsivo, cambiano dove va a capo il testo)
    QString wrappedText(const QString &text, const QFont &font, int width,
                        const QList<QTextLayout::FormatRange> &formats = {}) const {
        QString key = text + QChar(0x1f) + font.toString() + QChar(0x1f) + QString::number(width);
        for (const auto &range : formats)
            key += QStringLiteral("|%1,%2,%3").arg(range.start).arg(range.length).arg(inlineFlags(range.format));
        if (const auto it = m_wrapCache.constFind(key); it != m_wrapCache.cend()) return it.value();
        QTextLayout layout(QString(text).replace(QLatin1Char('\n'), QChar::LineSeparator), font);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.setFormats(formats);
        QString result;
        layout.beginLayout();
        for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
            line.setLineWidth(width);
            QString part = layout.text().mid(line.textStart(), line.textLength());
            if (!result.isEmpty() && !result.endsWith(QChar::LineSeparator)) result += QChar::LineSeparator;
            result += part;
        }
        layout.endLayout();
        if (m_wrapCache.size() > 2000) m_wrapCache.clear();
        m_wrapCache.insert(key, result);
        return result;
    }

    // Corsivo, barrato e sottolineato del titolo della riga (vuoto = testo semplice, disegnato
    // dallo stile). In cache per titolo e HTML: leggere l'HTML a ogni ridisegno costerebbe troppo.
    QList<QTextLayout::FormatRange> formatsFor(const QModelIndex &index) const {
        const QString html = index.data(MindMapModel::TitleHtmlRole).toString();
        if (html.isEmpty()) return {};
        const QString title = index.data(Qt::EditRole).toString();
        // Il peso del grassetto disegnato dipende dal livello della riga (2026-10-14).
        const int displayBold = rowBoldWeights(index).bold;
        // FEAT-022: nel titolo salvato il grassetto dipende dal peso del nodo.
        const auto *mapModel = qobject_cast<const MindMapModel *>(index.model());
        const MindNode *node = mapModel ? mapModel->nodeFromIndex(index) : nullptr;
        const int storedBold = node ? NodeItem::boldWeightFor(node) : kNormalBold.bold;
        // FEAT-024: the shown text starts with the number and a space; the ranges move after them.
        const QString number = index.data(MindMapModel::NumberRole).toString();
        const int shift = number.isEmpty() || title.isEmpty() ? 0 : int(number.size()) + 1;
        const QString key = title + QChar(0x1f) + html + QChar(0x1f) + QString::number(displayBold) + QChar(0x1f) + QString::number(storedBold)
                            + QChar(0x1f) + QString::number(shift);
        if (const auto it = m_formatCache.constFind(key); it != m_formatCache.cend()) return it.value();
        if (m_formatCache.size() > 2000) m_formatCache.clear();
        QList<QTextLayout::FormatRange> ranges = titleInlineRanges(title, html, storedBold, displayBold);
        for (QTextLayout::FormatRange &range : ranges) range.start += shift;
        return m_formatCache.insert(key, ranges).value();
    }

    // Pesi del grassetto nella riga: le voci di primo livello sono già in grassetto (700), quindi
    // lì il grassetto è Black (900); nelle altre è Bold (700). Richiesta dell'utente 2026-10-14.
    static BoldWeights rowBoldWeights(const QModelIndex &index) {
        return index.parent().isValid() ? kNormalBold : kHeavyBold;
    }

    // Disegna il titolo con i suoi formati come lo disegnerebbe QCommonStyle (Qt 6.4,
    // QCommonStylePrivate::viewItemDrawText): rettangolo SE_ItemViewItemText meno il margine
    // PM_FocusFrameHMargin + 1 per lato, righe una sotto l'altra, blocco centrato con
    // QStyle::alignedRect, colore Text della palette. L'a capo è lo stesso di wrappedText.
    void drawFormattedText(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index,
                           const QList<QTextLayout::FormatRange> &formats) const {
        const QWidget *widget = option.widget;
        const QStyle *style = widget ? widget->style() : QApplication::style();
        const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, widget) + 1;
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &option, widget).adjusted(margin, 0, -margin, 0);
        QTextLayout layout(index.data(Qt::DisplayRole).toString().replace(QLatin1Char('\n'), QChar::LineSeparator), option.font);
        QTextOption textOption;
        textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        textOption.setTextDirection(option.direction);
        textOption.setAlignment(QStyle::visualAlignment(option.direction, option.displayAlignment));
        layout.setTextOption(textOption);
        layout.setFormats(formats);
        qreal height = 0;
        layout.beginLayout();
        for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
            line.setLineWidth(textRect.width());
            line.setPosition(QPointF(0, height));
            height += line.height();
        }
        layout.endLayout();
        const QRect block = QStyle::alignedRect(Qt::LayoutDirectionAuto, option.displayAlignment,
                                                layout.boundingRect().size().toSize(), textRect);
        QPalette::ColorGroup group = option.state & QStyle::State_Enabled ? QPalette::Normal : QPalette::Disabled;
        if (group == QPalette::Normal && !(option.state & QStyle::State_Active)) group = QPalette::Inactive;
        painter->save();
        painter->setPen(option.palette.color(group, QPalette::Text));
        layout.draw(painter, QPointF(textRect.x(), block.top()));
        painter->restore();
    }

    // Mostra la barra sopra la selezione del campo, o la nasconde se non c'è selezione o se il
    // titolo ha formule (restano testo semplice). Centrata sul primo tratto selezionato, 8 px
    // sopra (come la barra della mappa sopra il nodo); sotto se in alto manca spazio.
    void updateFormatBar(OutlineTitleEditor *editor) const {
        if (!m_formatBar || editor != m_editor) return;
        const QTextCursor cursor = editor->textCursor();
        if (!cursor.hasSelection() || FormulaText::containsMath(editor->text()) || !editor->isVisible()) {
            m_formatBar->hide();
            return;
        }
        m_formatBar->sync(cursor.charFormat());
        QTextCursor start(cursor), end(cursor);
        start.setPosition(cursor.selectionStart());
        end.setPosition(cursor.selectionEnd());
        // Rettangoli del primo e dell'ultimo carattere selezionato nel viewport dell'outliner.
        QWidget *viewport = editor->parentWidget();
        const QPoint editorOrigin = editor->viewport()->mapTo(viewport, QPoint());
        const QRect first = editor->cursorRect(start).translated(editorOrigin);
        const QRect last = editor->cursorRect(end).translated(editorOrigin);
        // Selezione scorsa fuori dall'elenco: la barra non resta a galleggiare sulla finestra.
        if (!viewport->rect().intersects(first.united(last))) {
            m_formatBar->hide();
            return;
        }
        const int right = last.top() == first.top() ? last.left() : editorOrigin.x() + editor->viewport()->width();
        QWidget *host = m_formatBar->parentWidget();
        const QPoint offset = viewport->mapTo(host, QPoint());
        const QSize size = m_formatBar->sizeHint();
        int x = offset.x() + (first.left() + right) / 2 - size.width() / 2;
        int y = offset.y() + first.top() - 8 - size.height();
        if (y < 0) y = offset.y() + last.bottom() + 8;
        x = qBound(0, x, qMax(0, host->width() - size.width()));
        m_formatBar->setGeometry(QRect(QPoint(x, y), size));
        m_formatBar->raise();
        m_formatBar->show();
    }

    // Rendering in cache per titolo, font e larghezza disponibile: il parsing LaTeX si
    // fa una volta sola, non a ogni ridisegno della riga.
    const FormulaText *formulaFor(const QStyleOptionViewItem &option, const QModelIndex &index) const {
        const QString edited = index.data(Qt::EditRole).toString();
        if (!FormulaText::containsMath(edited)) return nullptr;
        const QString title = Numbering::withNumber(index.data(MindMapModel::NumberRole).toString(), edited);  // FEAT-024
        const auto *view = dynamic_cast<const OutlineTreeView *>(parent());
        const int width = qMax(200, option.rect.width() - (view ? view->leadPx(index) : 46) - 20);
        // paint() e sizeHint() passano l'opzione originale, senza initStyleOption: il grassetto
        // delle voci di primo livello va applicato anche qui, altrimenti le righe con formule restano normali.
        QFont font = option.font;
        if (!index.parent().isValid()) font.setWeight(QFont::Bold);
        const QString key = title + QChar(0x1f) + font.toString() + QChar(0x1f) + QString::number(width);
        auto it = m_formulaCache.find(key);
        if (it == m_formulaCache.end()) {
            if (m_formulaCache.size() > 500) m_formulaCache.clear();
            FormulaText formula;
            formula.update(title, font, QColor("#202630"), width);
            it = m_formulaCache.insert(key, formula);
        }
        return &it.value();
    }

    bool eventFilter(QObject *object, QEvent *event) override {
        // Lo scorrimento sposta il campo insieme al viewport (QWidget::scroll, senza passare da
        // updateEditorGeometry) e la vista lo nasconde quando si chiude: la barra di formattazione,
        // figlia della finestra, lo segue o sparisce.
        if (m_formatBar && object == m_editor &&
            (event->type() == QEvent::Hide || event->type() == QEvent::Show || event->type() == QEvent::Move))
            QTimer::singleShot(0, this, [this] { if (m_editor) updateFormatBar(m_editor); else if (m_formatBar) m_formatBar->hide(); });
        if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            // Invio conferma; Maiusc+Invio va al campo, che va a capo (OutlineTitleEditor::keyPressEvent).
            if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
                !keyEvent->modifiers().testFlag(Qt::ShiftModifier)) {
                auto *editor = qobject_cast<QWidget *>(object);
                emit commitData(editor);
                emit closeEditor(editor, QAbstractItemDelegate::NoHint);
                QTimer::singleShot(0, this, [this] {
                    if (auto *view = qobject_cast<QTreeView *>(parent())) view->setFocus();
                });
                return true;
            }
            std::function<void()> callback;
            if (keyEvent->key() == Qt::Key_Tab && !(keyEvent->modifiers() & Qt::ShiftModifier)) callback = m_indent;
            else if (keyEvent->key() == Qt::Key_Backtab || (keyEvent->key() == Qt::Key_Tab && keyEvent->modifiers() & Qt::ShiftModifier)) callback = m_outdent;
            else if ((keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace) &&
                     dynamic_cast<OutlineTitleEditor *>(object) && dynamic_cast<OutlineTitleEditor *>(object)->text().isEmpty()) callback = m_outdent;
            if (callback) {
                auto *editor = qobject_cast<QWidget *>(object);
                emit commitData(editor);
                emit closeEditor(editor, QAbstractItemDelegate::NoHint);
                QTimer::singleShot(0, this, std::move(callback));
                return true;
            }
        }
        return QStyledItemDelegate::eventFilter(object, event);
    }

private:
    std::function<void()> m_addSibling;
    std::function<void()> m_indent;
    std::function<void()> m_outdent;
    mutable QHash<QString, FormulaText> m_formulaCache;
    mutable QHash<QString, QString> m_wrapCache;  // vedi wrappedText
    mutable QHash<QString, QList<QTextLayout::FormatRange>> m_formatCache;  // vedi formatsFor
    // Vero solo mentre paint() disegna una riga con formati (il testo lo disegna drawFormattedText).
    mutable bool m_paintingFormattedRow = false;
    mutable QPointer<OutlineFormatBar> m_formatBar;  // vedi createEditor
    // Vero solo mentre paint() disegna la riga che ha il campo di modifica aperto.
    mutable bool m_paintingEditedRow = false;
    // Campo aperto e sua riga: sizeHint() misura il testo del campo (righe aggiunte con
    // Maiusc+Invio) invece di quello ancora salvato nel modello.
    mutable QPointer<OutlineTitleEditor> m_editor;
    mutable QPersistentModelIndex m_editIndex;
    mutable bool m_measuringEditor = false;
    // Allineamento del testo del campo a quello disegnato (px, misurati con il test t4):
    // spostamento orizzontale e spazio sopra/sotto le righe.
    static constexpr int kEditorTextOffset = 0;
    static constexpr int kEditorPadding = 0;
};
}

namespace {
// FEAT-085: while a text is typed, the copy/paste style keys go to the text: on Windows AltGr comes
// as Ctrl+Alt, and AltGr+C or AltGr+V type a letter on some layouts (Polish ć, Hungarian @). An
// accepted ShortcutOverride sends the key to the field instead of the shortcut.
class TypingKeysFilter final : public QObject {
public:
    TypingKeysFilter(std::function<bool(const QKeyEvent *)> reserved, QObject *parent)
        : QObject(parent), m_reserved(std::move(reserved)) {}

protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::ShortcutOverride && m_reserved(static_cast<QKeyEvent *>(event))) event->accept();
        return false;
    }

private:
    std::function<bool(const QKeyEvent *)> m_reserved;
};
}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_document(new Document(this)), m_shortcuts(new ShortcutRegistry(this)), m_model(m_document->model()), m_scene(new MindMapScene(m_model, this)), m_view(new MindMapView(this)) {
    setWindowFlag(Qt::FramelessWindowHint);
    SettingsDialog::loadSelectionStyle();  // bordo di selezione e hover dei nodi (2026-10-13)
    setWindowTitle(tr("Mentis"));
    // The app's icon (user, 2026-10-31): one picture per size, so small sizes stay sharp.
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256, 512}) icon.addFile(QStringLiteral(":/assets/appicon/mentis-%1.png").arg(size), QSize(size, size));
    setWindowIcon(icon);
    resize(1400, 900);
    // Scala dell'interfaccia (vedi analisi/ottimizzazioni-2026-10-12.md, sezione 9):
    // - spazi e dimensioni multipli di 4px: 4, 8, 12, 16, 24, 32, 48;
    // - raggi: 4px per controlli ed elementi interni (pulsanti, campi, righe, voci di menu),
    //   8px per contenitori e superfici sovrapposte (finestra, menu, pannelli, card, nodi),
    //   come in Windows 11; un elemento annidato usa raggio esterno meno il margine;
    // - testo: 12 (didascalie), 14 (corpo), 18 (corpo grande), 20, 28 (titoli).
    setStyleSheet(UiTheme::qss(R"(
        QMainWindow { background:transparent; color:@text; border:0; }
        QDialog { background:@surfaceSunken; color:@text; }
        QMenuBar, QMenu, QToolBar, QDockWidget { background: @surface; color: @text; }
        QMenuBar::item:selected, QMenu::item:selected { background: @surfaceRaised; }
        QDockWidget::title { padding: 8px; background: @surface; font-weight: 600; }
        QDockWidget#InspectorDock { background:@surfaceSunken; border:1px solid @divider; border-radius:8px; }
        QWidget#InspectorTitleBar { background: @surfaceSunken; border-top-left-radius: 8px; border-top-right-radius: 8px; }
        QLabel#InspectorTitle { color: @text; font-weight: 600; }
        QWidget#InspectorPanel { background: @surfaceSunken; border-bottom-left-radius: 8px; border-bottom-right-radius: 8px; }
        QPushButton { background: @surfaceRaised; color: @text; border: 1px solid @divider; border-radius: 4px; min-height: 20px; padding: 5px 16px; }
        QPushButton:hover { background: @divider; border-color: #67d6b2; }
        QPushButton#accent { background: #7047eb; border: none; font-weight: 700; }
        QLineEdit, QPlainTextEdit, QDoubleSpinBox, QTableWidget { background: #fbfcfe; color: #18202b; border: 1px solid #cfd5dd; border-radius: 4px; padding: 5px 8px; }
        QStatusBar { background: @surface; color: @text; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 2px; }
        QScrollBar::handle:vertical { background: #c4cad2; min-height: 48px; border-radius: 3px; }
        QScrollBar::handle:vertical:hover { background: #9aa2ac; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px 4px; }
        QScrollBar::handle:horizontal { background: #c4cad2; min-width: 48px; border-radius: 3px; }
        QScrollBar::handle:horizontal:hover { background: #9aa2ac; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
    )"));
    setContentsMargins(0, 0, 0, 0);
    {
        // P8: "session/running" resta vero se l'app non arriva al distruttore (crash, spegnimento).
        QSettings settings;
        m_uncleanPreviousExit = settings.value("session/running", false).toBool();
        settings.setValue("session/running", true);
    }
    // FEAT-066: il Document chiude i passi di storico e salva da solo; dall'interfaccia vuole sapere
    // se un tasto del mouse e' premuto (trascinamento in corso) o se si scrive il titolo di un
    // argomento (BUG-039: titolo e mappa ridisposta sono un solo passo) e l'anteprima della Home.
    m_document->setInteractionInProgress([this] {
        return QApplication::mouseButtons() != Qt::NoButton || (m_scene && m_scene->editingTitle());
    });
    m_document->setPreviewProvider([this] { return documentPreviewJpeg(); });
    connect(m_document, &Document::dirtyChanged, this, &MainWindow::onDirtyChanged);
    connect(m_document, &Document::historyChanged, this, &MainWindow::updateHistoryActions);
    connect(m_document, &Document::saveStateChanged, this, [this] {
        updateMapSubtitle();
        if (m_document->isSaving() && m_saveStateButton) m_saveStateButton->repaint();  // icona prima del salvataggio
    });
    connect(m_document, &Document::autoSaveFailed, this, [this](const QString &error) {
        statusBar()->showMessage(tr("Autosave failed: %1").arg(error), 5000);
    });
    connect(m_document, &Document::sheetsChanged, this, &MainWindow::syncSheetSelect);
    // P3 (2026-10-13): un passo di storico si applica senza ricostruire mappa e outliner; la scena
    // rimanda i ridisegni mentre il modello cambia e la selezione resta dov'era.
    connect(m_document, &Document::aboutToRestore, this, [this] {
        MindNode *selected = m_scene->selectedNode();
        m_selectedBeforeRestore = selected ? selected->id : QString();
        m_scene->setDeferredUpdates(true);
    });
    connect(m_document, &Document::incrementalRestoreApplied, this, [this] { m_scene->setDeferredUpdates(false); });
    connect(m_document, &Document::restored, this, &MainWindow::onHistoryRestored);
    connect(m_document, &Document::annotationsChanged, m_scene, &MindMapScene::refreshAnnotations);
    // FEAT-025: a relationship or boundary style can make Reset map style available.
    connect(m_document, &Document::annotationsChanged, this, [this] {
        if (m_sheetStylePanel) m_sheetStylePanel->setResetMapEnabled(m_model->hasMapStyle());
    });
    connect(m_document, &Document::nodesChanged, this, [this](const QList<MindNode *> &nodes) {
        for (MindNode *node : nodes) m_scene->updateNode(node);
    });
    createActions();
    installEventFilter(this);
    createMenusAndToolbar();
    m_view->setScene(m_scene);
    applyPerformanceProfile();  // 2026-10-14: profilo Standard o Leggero
    createDocks();
    m_pages = new QStackedWidget;
    m_homePage = createHomePage();
    m_editorPage = createEditorPage();
    m_pages->addWidget(m_homePage);
    m_pages->addWidget(m_editorPage);
    connect(m_pages, &QStackedWidget::currentChanged, this, [this] {
        updateWindowFrame(m_inspectorDock && m_inspectorDock->isVisible());  // FEAT-036: bordo solo fuori dalla mappa
    });
    setCentralWidget(m_pages);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::updatePanelContext);  // FEAT-017
    connect(m_scene, &MindMapScene::nodeSelected, this, [this](MindNode *node) {
        updateInspector(node);
        if (!m_relationSource.isEmpty() && node && node->id != m_relationSource) finishRelation(node->id);
    });
    // FEAT-052: a boundary clicked while a relationship is being drawn is its target.
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        if (m_relationSource.isEmpty() || !m_scene->selectedNodes().isEmpty()) return;
        const QStringList boundaries = m_scene->selectedBoundaryIds();
        if (boundaries.size() == 1 && boundaries.front() != m_relationSource) finishRelation(boundaries.front());
    });
    connect(m_scene, &MindMapScene::boundaryEdited, this, [this](const Boundary &boundary) {
        if (m_document->updateBoundaries({boundary})) { m_scene->autoLayout(); m_document->flushPendingStep(); }  // a title changes the room above
        refreshStylePage();
    });
    connect(m_scene, &MindMapScene::boundaryMenuRequested, this, &MainWindow::showBoundaryMenu);
    connect(m_scene, &MindMapScene::relationEdited, this, [this](const Relationship &relationship) {
        if (!m_document->updateRelations({relationship})) m_scene->refreshAnnotations();  // refused: back to the saved line
        refreshStylePage();
    });
    connect(m_scene, &MindMapScene::relationMenuRequested, this, &MainWindow::showRelationMenu);
    // B3 (2026-10-13): m_selected e m_relationSource puntano a nodi del modello. Aprendo
    // un'altra mappa, applicando un modello o eliminando il nodo restavano puntati a memoria
    // già liberata (il reset del modello non emette currentChanged). Si azzerano appena
    // prima che i nodi vengano distrutti, qualunque sia la funzione che li distrugge.
    connect(m_model, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        cancelFloatingPlacement();  // la scena sta per svuotarsi
        m_selected = nullptr;
        if (!m_relationSource.isEmpty()) { m_relationSource.clear(); statusBar()->clearMessage(); }
    });
    connect(m_model, &QAbstractItemModel::rowsAboutToBeRemoved, this, [this](const QModelIndex &parent, int first, int last) {
        auto *parentNode = parent.isValid() ? m_model->nodeFromIndex(parent) : m_model->root();
        if (!parentNode) return;
        // Vero se "node" è una delle righe rimosse o sta dentro uno dei loro rami.
        const auto isRemoved = [&](const MindNode *node) {
            for (; node; node = node->parent)
                if (node->parent == parentNode) {
                    const int row = m_model->indexFromNode(const_cast<MindNode *>(node)).row();
                    return row >= first && row <= last;
                }
            return false;
        };
        if (isRemoved(m_selected)) m_selected = nullptr;
        if (isRemoved(nodeById(m_relationSource))) { m_relationSource.clear(); statusBar()->clearMessage(); }
    });
    connect(m_scene, &MindMapScene::nodeChanged, this, [this] { setDirty(); });
    // Un passo intermedio della modifica del titolo diventa subito un passo di storico (vedi
    // NodeItem::finishEditing), invece di unirsi al successivo nell'attesa del timer.
    connect(m_scene, &MindMapScene::historyStepFinished, this, [this] {
        m_document->flushPendingStep();
    });
    // La scena ora segue da sola i segnali del modello (righe inserite/rimosse, dati
    // cambiati, reset): il rebuild() qui la ricostruiva una seconda volta a ogni modifica.
    connect(m_model, &MindMapModel::structureChanged, this, [this] { refreshOutline(); updateBottomBar(); setDirty(); });
    // FEAT-019: ogni modifica di stile (anche su piu' nodi) diventa subito un passo di storico,
    // senza unirsi alla modifica successiva nell'attesa del timer.
    connect(m_model, &MindMapModel::styleEdited, this, [this] {
        setDirty();
        m_document->flushPendingStep();
    });
    // FEAT-027: ogni modifica dei collegamenti e' subito un passo (anche di due fogli).
    connect(m_model, &MindMapModel::linksEdited, this, [this] {
        setDirty();
        commitLinkStep();
        refreshLinkStates();
    });
    connect(m_model, &MindMapModel::structureChanged, this, [this] { refreshLinkStates(); });
    connect(m_scene, &MindMapScene::linkActivated, this, &MainWindow::followLink);
    connect(m_scene, &MindMapScene::linkMenuRequested, this, &MainWindow::showLinkMenu);
    // FEAT-048: clic e tasto destro sulla casella di spunta.
    connect(m_scene, &MindMapScene::taskToggled, this, [this](MindNode *node) { m_document->toggleTask(node); refreshOutline(); });
    connect(m_scene, &MindMapScene::taskMenuRequested, this, &MainWindow::showTaskMenu);
    connect(m_scene, &MindMapScene::nodeMenuRequested, this, &MainWindow::showNodeMenu);
    // FEAT-072 F3: floating bar and "+" buttons of the selected topic.
    m_topicTools = new TopicTools(m_view, m_scene, this);
    m_topicTools->enabled = [this] {
        return m_pages && m_pages->currentWidget() == m_editorPage && workspaceView() == WorkspaceView::Map;
    };
    m_topicTools->relation = [this](MindNode *) { beginRelation(); };
    m_topicTools->webLink = [this](MindNode *node) { editWebLink(node); };
    m_topicTools->image = [this](MindNode *node) { updateInspector(node); chooseImage(); };
    m_topicTools->note = [this](MindNode *node) { openNote(node); };
    m_topicTools->checkbox = [this](MindNode *node) { toggleTaskBoxes({node}); };
    m_topicTools->sticker = [this](MindNode *) { showIconPanel(1); };
    m_topicTools->formatPainter = [this](MindNode *node) { copyStyleOf(node); setFormatPainter(true); };
    m_topicTools->keepFormatPainter = [this] { setFormatPainter(true, true); };
    m_topicTools->stopFormatPainter = [this] { setFormatPainter(false); };
    connect(m_document, &Document::sheetsChanged, this, [this] { setFormatPainter(false); });
    m_topicTools->addChild = [this] { addChild(); };
    m_topicTools->addSibling = [this] { addSibling(); };
    connect(m_scene, &MindMapScene::taskTyped, this, &MainWindow::setTypedTask);  // FEAT-068
    // FEAT-049: graffetta dei file e file trascinati sulla mappa.
    connect(m_scene, &MindMapScene::filesActivated, this, &MainWindow::showFilesMenu);
    connect(m_scene, &MindMapScene::formulaActivated, this, &MainWindow::editFormula);  // FEAT-064
    connect(m_scene, &MindMapScene::formulaMenuRequested, this, &MainWindow::showFormulaMenu);
    // FEAT-051: clic sul marcatore = valore seguente della serie; tasto destro = menu.
    connect(m_scene, &MindMapScene::markerClicked, this, [this](MindNode *node, const QString &marker) {
        setMarker(node, Markers::seriesOf(marker), Markers::next(marker));
    });
    connect(m_scene, &MindMapScene::markerMenuRequested, this, &MainWindow::showMarkerMenu);
    connect(m_scene, &MindMapScene::imageMenuRequested, this, &MainWindow::showImageMenu);
    connect(m_scene, &MindMapScene::reorderRequested, this, &MainWindow::reorderNode);  // FEAT-015
    // FEAT-016: chiude o riapre i rami del nodo (un passo, con il ridisposto); il nodo resta scelto.
    connect(m_scene, &MindMapScene::collapseRequested, this, [this](MindNode *node) {
        if (!m_document->setCollapsed(node, !node->collapsed)) return;
        m_scene->autoLayout();
        m_document->flushPendingStep();
        m_scene->selectNode(node);
    });
    connect(m_scene, &MindMapScene::floatRequested, this, [this](MindNode *node, const QPointF &position) {
        if (!m_document->makeFloating(node, position)) return;
        m_scene->refreshStacking();  // FEAT-057: on top of the other branches
        m_scene->autoLayout();
        m_document->flushPendingStep();  // un passo, con il ridisposto
        m_scene->selectNode(node);
        refreshOutline();
    });
    connect(m_view, &MindMapView::filesDropped, this, &MainWindow::dropFiles);
    m_openedFilesWatcher = new QFileSystemWatcher(this);
    m_openedFilesCheck = new QTimer(this);
    m_openedFilesCheck->setSingleShot(true);
    m_openedFilesCheck->setInterval(600);  // gli editor salvano a piu' riprese: si aspetta che finiscano
    connect(m_openedFilesWatcher, &QFileSystemWatcher::fileChanged, m_openedFilesCheck, qOverload<>(&QTimer::start));
    connect(m_openedFilesCheck, &QTimer::timeout, this, &MainWindow::checkOpenedFiles);
    // FEAT-059: riquadro della nota. Alla chiusura un passo di storico (nota, icona e ridisposizione).
    m_noteEditor = new NoteEditor(this);
    connect(m_scene, &MindMapScene::noteActivated, this, &MainWindow::openNote);
    connect(m_noteEditor, &NoteEditor::finished, this, &MainWindow::finishNote);
    // FEAT-060: riquadro "Etichette" (clic su una pillola o + > Etichetta) e menu della pillola.
    m_labelEditor = new LabelEditor(this);
    connect(m_scene, &MindMapScene::labelActivated, this, &MainWindow::openLabels);
    connect(m_scene, &MindMapScene::labelMenuRequested, this, &MainWindow::showLabelMenu);
    connect(m_labelEditor, &LabelEditor::finished, this, &MainWindow::finishLabels);
    // "Evidenzia argomenti collegati" finisce con un clic sulla mappa o con Esc.
    m_view->viewport()->installEventFilter(this);
    m_view->installEventFilter(this);
    // Il riquadro segue il nodo quando la mappa scorre o cambia zoom.
    const auto followNode = [this] {
        if (m_noteEditor->isOpen())
            if (const MindNode *node = nodeById(m_noteEditor->nodeId())) m_noteEditor->moveNextTo(noteAnchor(node));
        if (m_labelEditor->isOpen())
            if (const MindNode *node = nodeById(m_labelEditor->nodeId())) m_labelEditor->moveNextTo(noteAnchor(node));
    };
    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, followNode);
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, followNode);
    connect(m_view, &MindMapView::zoomChanged, this, followNode);
    // FEAT-094: Ctrl+V of an image, files or a branch while a topic title is typed.
    m_scene->pasteWhileEditing = [this](MindNode *node) { return pasteWhileEditing(node, false); };
    m_scene->charStyleKey = [this](const QKeyEvent *event) { return charStyleKey(event); };  // FEAT-095
    // FEAT-037: originale delle immagini incorporate, dal file aperto o dalla copia di lavoro di un
    // documento senza nome. Vuoto = si resta sulla miniatura.
    m_scene->originalImagePath = [this](const MindNode *node) {
        const QString source = m_document->assetSourcePath();
        return source.isEmpty() ? QString() : BmapStorage::originalAssetPath(source, node->imageAssetId, node->imageOriginalName);
    };
    m_scene->autoLayout();
    updateInspector(m_model->root());
    m_scene->selectNode(m_model->root());
    m_document->startUntitled();
    // Il suggerimento iniziale resterebbe fisso nella barra in basso: ora lo zoom ha un tooltip.
    showHome();
    installWindowFrame();
    m_homePage->installEventFilter(this);  // the side panel is built once the Home is on screen
    // FEAT-045 phase 2: agents reach this window's map while they are turned on in Settings.
    m_agentHost = new McpBridge::Host([this](const QString &tool, const QJsonObject &arguments) {
        return runAgentTool(tool, arguments);
    }, this);
    m_agentHost->setPath(m_document->filePath());
    m_agentHost->markActive();
    m_agentHost->setListening(McpMapTools::Access::load().enabled);
    connect(m_document, &Document::saveStateChanged, m_agentHost, [this] { m_agentHost->setPath(m_document->filePath()); });
    connect(m_document, &Document::sheetsChanged, m_agentHost, [this] { m_agentHost->setPath(m_document->filePath()); });
}

McpServer::ToolResult MainWindow::runAgentTool(const QString &tool, const QJsonObject &arguments) {
    using ToolResult = McpServer::ToolResult;
    if (!McpMapTools::Access::load().enabled) return ToolResult::error(McpMapTools::offMessage());
    // A dialog waits for the user (save the changes?, a modal of the map): the map must not change
    // under it, the call arrives through its event loop.
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget())
        return ToolResult::error(QStringLiteral("Mentis is waiting for the user (a dialog or menu is open): try again in a moment"));
    commitPendingEdits();  // the user's text first (user, 2026-11-02)
    McpMapTools::Access access = McpMapTools::Access::load();
    access.ask = [this](const QString &kind, const QString &subject) -> std::optional<bool> { return confirmForAgent(kind, subject); };
    // An agent working on a file asks here before deleting a sheet or replacing a file.
    if (tool == QLatin1String("@library_changed")) {  // an agent changed the stickers or markers
        for (int page : {0, 1})
            if (IconPage *icons = iconPage(page)) icons->reload();
        return ToolResult::object({{"reloaded", true}});
    }
    if (tool == QLatin1String("@confirm"))
        return ToolResult::object({{"confirmed", confirmForAgent(arguments.value(QStringLiteral("kind")).toString(),
                                                                 arguments.value(QStringLiteral("subject")).toString())}});
    if (tool == QLatin1String("save_open_map")) return saveForAgent(access, arguments);
    if (tool == QLatin1String("open_dialog")) return openDialogForAgent(arguments);
    if (tool == QLatin1String("open_map")) return openMapForAgent(arguments);
    const McpMapTools::MapEdit *edit = McpMapTools::mapEdit(tool);
    if (!edit || !edit->edit) return ToolResult::error(QStringLiteral("Unknown tool: %1").arg(tool));
    const QString sheet = arguments.value(QStringLiteral("sheet")).toString();
    if (!sheet.isEmpty()) {
        const int index = m_document->sheetIndex(sheet);
        if (index < 0) return ToolResult::error(QStringLiteral("No sheet with id %1 (read_map lists them)").arg(sheet));
        switchSheet(index);  // the window shows the agent's sheet (user, 2026-11-02)
        if (m_document->currentSheet() != index) return ToolResult::error(QStringLiteral("Cannot show the sheet %1").arg(sheet));
    }
    const int shownSheet = m_document->currentSheet();
    ToolResult result = edit->edit(access, arguments, *m_document, *m_scene, *m_model);
    if (!edit->write || result.isError) return result;
    m_scene->autoLayout();
    m_document->flushPendingStep();  // the whole call is one Undo step
    if (m_pages->currentWidget() != m_editorPage) showEditor();
    if (m_document->currentSheet() != shownSheet) onSheetShown();
    else refreshOutline();
    for (const QString key : {QStringLiteral("id"), QStringLiteral("firstId"), QStringLiteral("rootId")}) {
        if (MindNode *node = m_model->nodeById(result.structured.value(key).toString())) {
            m_scene->selectNode(node);
            m_view->ensureVisible(m_scene->nodeSceneRect(node), 48, 48);
            break;
        }
    }
    statusBar()->showMessage(agentMessage(tool, arguments, result.structured), 4000);
    return result;
}

McpServer::ToolResult MainWindow::saveForAgent(const McpMapTools::Access &access, const QJsonObject &arguments) {
    using ToolResult = McpServer::ToolResult;
    namespace Support = McpMapTools::Support;
    QString target = m_document->filePath();
    QString error;
    if (arguments.contains(QStringLiteral("path"))) {
        bool exists = false;
        const std::optional<QString> path = Support::outputPath(arguments, QStringLiteral("path"), {QStringLiteral("mentis")}, &exists, &error);
        if (!path) return ToolResult::error(error);
        const bool own = QFileInfo(*path).canonicalFilePath() == QFileInfo(m_document->filePath()).canonicalFilePath();
        if (exists && !own) {
            if (Support::fileInUse(*path)) return ToolResult::error(QStringLiteral("That map is open in another Mentis window: it cannot be replaced"));
            if (!Support::confirmed(access, arguments, QStringLiteral("overwrite"), *path, &error)) return ToolResult::error(error);
        }
        target = *path;
    } else if (target.isEmpty()) {
        return ToolResult::error(QStringLiteral("The open map has no name yet: give path, an absolute .mentis file"));
    }
    const bool own = QFileInfo(target).canonicalFilePath() == QFileInfo(m_document->filePath()).canonicalFilePath();
    const auto save = [this](const QString &path, QString *failure) {
        const Document::Result result = m_document->save(path, BmapStorage::SaveReason::Explicit);
        if (!result.ok) *failure = result.error;
        return result.ok;
    };
    if (!(own ? save(target, &error) : Support::replaceWith(target, save, &error)))
        return ToolResult::error(QStringLiteral("The map was not saved: %1").arg(error));
    rememberRecent(target);
    m_agentHost->setPath(target);
    statusBar()->showMessage(agentMessage(QStringLiteral("save_open_map"), arguments, {}), 4000);
    return ToolResult::object({{"path", QDir::toNativeSeparators(target)}});
}

McpServer::ToolResult MainWindow::openDialogForAgent(const QJsonObject &arguments) {
    using ToolResult = McpServer::ToolResult;
    const QString dialog = arguments.value(QStringLiteral("dialog")).toString();
    const QString sheet = arguments.value(QStringLiteral("sheet")).toString();
    if (!sheet.isEmpty()) {
        const int index = m_document->sheetIndex(sheet);
        if (index < 0) return ToolResult::error(QStringLiteral("No sheet with id %1").arg(sheet));
        switchSheet(index);
    }
    // The export windows' choices, checked before anything opens.
    std::optional<ExportFrame> frame;
    if (arguments.contains(QStringLiteral("frame"))) {
        const QJsonObject value = arguments.value(QStringLiteral("frame")).toObject();
        static const QStringList styles{"none", "solid", "dashed", "dotted", "double", "greek-key", "waves", "chain", "dentils"};
        ExportFrame chosen;
        const int style = int(styles.indexOf(value.value(QStringLiteral("style")).toString(QStringLiteral("solid"))));
        if (style < 0) return ToolResult::error(QStringLiteral("frame.style must be one of %1").arg(styles.join(QStringLiteral(", "))));
        chosen.style = ExportFrame::Style(style);
        if (value.contains(QStringLiteral("thickness")))
            chosen.thickness = std::clamp(value.value(QStringLiteral("thickness")).toDouble(), ExportFrame::kMinThickness, ExportFrame::kMaxThickness);
        if (value.contains(QStringLiteral("color"))) {
            const QColor color(value.value(QStringLiteral("color")).toString());
            if (!color.isValid()) return ToolResult::error(QStringLiteral("frame.color must be a colour like #7047eb"));
            chosen.color = color;
        }
        chosen.rounded = value.value(QStringLiteral("rounded")).toBool();
        frame = chosen;
    }
    const QString scopeKey = arguments.value(QStringLiteral("scope")).toString();
    const std::optional<int> scope = scopeKey == QLatin1String("sheet") ? std::optional(0) : scopeKey == QLatin1String("all") ? std::optional(1)
                                   : scopeKey == QLatin1String("branch") && !exportBranchId().isEmpty() ? std::optional(2) : std::nullopt;
    const auto boolean = [&arguments](const char *key) {
        const QJsonValue value = arguments.value(QLatin1String(key));
        return value.isBool() ? std::optional(value.toBool()) : std::nullopt;
    };

    std::function<void()> open;
    const QString id = arguments.value(QStringLiteral("id")).toString();
    const auto topicDialog = [this, &id](void (MainWindow::*show)(MindNode *)) -> std::function<void()> {
        MindNode *node = m_model->nodeById(id);
        if (!node || node->summaryTopic) return {};
        m_scene->selectNode(node);
        m_view->ensureVisible(m_scene->nodeSceneRect(node), 48, 48);
        return [this, id, show] { if (MindNode *current = m_model->nodeById(id)) (this->*show)(current); };
    };
    if (dialog == QLatin1String("export_image")) {
        ExportDialog::Preset preset;
        if (scope) preset.scope = ExportDialog::Scope(*scope);
        preset.gantt = boolean("gantt");
        preset.background = boolean("background");
        if (arguments.value(QStringLiteral("scale")).isDouble()) preset.scale = arguments.value(QStringLiteral("scale")).toInt();
        preset.frame = frame;
        m_agentImagePreset = preset;
        open = [this] { exportImage(); };
    } else if (dialog == QLatin1String("export_pdf") || dialog == QLatin1String("print")) {
        PageDialog::Preset preset;
        preset.scope = scope;
        if (arguments.contains(QStringLiteral("kind"))) preset.kind = arguments.value(QStringLiteral("kind")).toString();
        preset.background = boolean("background");
        preset.frame = frame;
        m_agentPagePreset = preset;
        open = [this, print = dialog == QLatin1String("print")] { exportPdfOrPrint(print); };
    } else if (dialog == QLatin1String("export_word")) {
        open = [this] { exportOffice(Document::OfficeFormat::Word); };
    } else if (dialog == QLatin1String("export_excel")) {
        open = [this] { exportOffice(Document::OfficeFormat::Excel); };
    } else if (dialog == QLatin1String("export_pitch")) {
        open = [this] { exportPresentation(); };
    } else if (dialog == QLatin1String("gantt_resources")) {
        open = [this] { openResources(); };
    } else if (dialog == QLatin1String("new_palette")) {
        open = [this] { createPalette(); };
    } else if (dialog == QLatin1String("present")) {
        open = [this] { presentPitch(); };
    } else if (dialog == QLatin1String("sticker_editor")) {
        const QString sticker = arguments.value(QStringLiteral("sticker")).toString();
        QString collection;
        for (const IconLibrary::Collection &candidate : IconLibrary::collections(IconLibrary::Kind::Sticker))
            for (const IconLibrary::Item &item : candidate.items)
                if (item.path == sticker) collection = candidate.id;
        if (collection.isEmpty()) return ToolResult::error(QStringLiteral("No sticker %1 (list_library lists them)").arg(sticker));
        open = [this, sticker, collection] { if (IconPage *page = showIconPanel(1)) page->editSticker(sticker, collection); };
    } else if (dialog == QLatin1String("settings")) {
        static const QStringList pages{"general", "appearance", "editor", "shortcuts"};
        m_agentSettingsPage = std::max(0, int(pages.indexOf(arguments.value(QStringLiteral("page")).toString())));
        open = [this] { showSettings(m_agentSettingsPage == 3); };
    } else if (dialog == QLatin1String("formula")) {
        open = topicDialog(&MainWindow::editFormula);
    } else if (dialog == QLatin1String("topic_link")) {
        open = topicDialog(&MainWindow::editTopicLink);
    } else if (dialog == QLatin1String("web_link")) {
        open = topicDialog(&MainWindow::editWebLink);
    } else if (dialog == QLatin1String("labels")) {
        open = topicDialog(&MainWindow::openLabels);
    } else if (dialog == QLatin1String("note")) {
        open = topicDialog(&MainWindow::openNote);
    } else {
        return ToolResult::error(QStringLiteral("Unknown dialog %1").arg(dialog));
    }
    if (!open) return ToolResult::error(QStringLiteral("No topic with id %1 in this sheet").arg(id));
    if (dialog != QLatin1String("settings") && m_pages->currentWidget() != m_editorPage) showEditor();
    raise();
    activateWindow();
    // After this call has answered: a modal window runs its own loop until the user closes it.
    QTimer::singleShot(0, this, open);
    return ToolResult::object({{"opened", dialog}});
}

void MainWindow::openAtStart(const QString &path) { readFile(QFileInfo(path).absoluteFilePath()); }

McpServer::ToolResult MainWindow::openMapForAgent(const QJsonObject &arguments) {
    using ToolResult = McpServer::ToolResult;
    const QString path = QFileInfo(arguments.value(QStringLiteral("path")).toString()).canonicalFilePath();
    if (path.isEmpty()) return ToolResult::error(QStringLiteral("No map at %1").arg(arguments.value(QStringLiteral("path")).toString()));
    const QJsonObject opened{{"path", QDir::toNativeSeparators(path)}};
    if (path == QFileInfo(m_document->filePath()).canonicalFilePath()) {  // already here: shown, nothing to ask
        if (m_pages->currentWidget() != m_editorPage) showEditor();
        raise();
        activateWindow();
        return ToolResult::object(opened);
    }
    if (!confirmForAgent(QStringLiteral("open_map"), path))
        return ToolResult::error(QStringLiteral("The user did not want to open the map in Mentis"));
    if (!maybeSave()) return ToolResult::error(QStringLiteral("The user kept the map open in Mentis (it has unsaved changes)"));
    if (!readFile(path)) return ToolResult::error(QStringLiteral("Mentis could not open the map"));
    statusBar()->showMessage(tr("Agent: opened the map"), 4000);
    return ToolResult::object(opened);
}

bool MainWindow::confirmForAgent(const QString &kind, const QString &subject) {
    QString text = tr("An agent wants to replace the file %1.").arg(QDir::toNativeSeparators(subject));
    QString action = tr("Replace");
    if (kind == QLatin1String("delete_sheet")) text = tr("An agent wants to delete the sheet \"%1\" and all its topics.").arg(subject);
    else if (kind == QLatin1String("delete_library_item")) text = tr("An agent wants to delete \"%1\" from your stickers or markers.").arg(subject);
    else if (kind == QLatin1String("delete_collection")) text = tr("An agent wants to delete the collection \"%1\" with its files.").arg(subject);
    else if (kind == QLatin1String("remove_person")) text = tr("An agent wants to remove %1, who is assigned to tasks, from the map.").arg(subject);
    else if (kind == QLatin1String("delete_palette")) text = tr("An agent wants to delete your colour theme \"%1\".").arg(subject);
    else if (kind == QLatin1String("delete_pitch_template")) text = tr("An agent wants to delete your Pitch template \"%1\".").arg(subject);
    else if (kind == QLatin1String("open_map")) text = tr("An agent wants to open the map %1.").arg(QDir::toNativeSeparators(subject));
    if (kind == QLatin1String("remove_person")) action = tr("Remove");
    else if (kind == QLatin1String("open_map")) action = tr("Open");
    else if (kind != QLatin1String("overwrite")) action = tr("Delete");
    raise();
    activateWindow();
    return ModalDialog::ask(this, tr("Agent"), text, action) == ModalDialog::Choice::Primary;
}

QString MainWindow::agentMessage(const QString &tool, const QJsonObject &arguments, const QJsonObject &result) const {
    const QString title = arguments.value(QStringLiteral("title")).toString().simplified();
    const MindNode *node = m_model->nodeById(result.value(QStringLiteral("id")).toString());
    const QString topic = node ? node->title.simplified() : title;
    if (tool == QLatin1String("add_topic")) return tr("Agent: added \"%1\"").arg(topic);
    if (tool == QLatin1String("update_topic")) return tr("Agent: changed \"%1\"").arg(topic);
    if (tool == QLatin1String("move_topic")) return tr("Agent: moved \"%1\"").arg(topic);
    if (tool == QLatin1String("delete_topic")) return tr("Agent: deleted a branch");
    if (tool == QLatin1String("add_sheet")) return tr("Agent: added the sheet \"%1\"").arg(title);
    if (tool == QLatin1String("rename_sheet")) return tr("Agent: renamed the sheet \"%1\"").arg(title);
    if (tool == QLatin1String("import_markdown")) return tr("Agent: added an outline");
    return tr("Agent: saved the map");
}

void MainWindow::installWindowFrame() {
    if (!m_frameHooks.contentAt) {
        m_frameHooks.contentAt = [this](const QPoint &pos) {
            QWidget *under = childAt(pos);
            // FEAT-093: the tour's veil covers the window; the frame looks at what is under it.
            if (auto *veil = dynamic_cast<TourVeil *>(under)) under = veil->below(pos);
            for (QToolButton *button : std::as_const(m_maximizeButtons))
                if (button && under == button) {
                    m_hotMaximize = button;
                    return WindowFrame::Hit::MaximizeButton;
                }
            // The same areas that move the window with the mouse elsewhere (eventFilter).
            return under && under->property("windowDragArea").toBool() ? WindowFrame::Hit::Caption : WindowFrame::Hit::Client;
        };
        // Windows owns the mouse over the button (Snap layouts): its hover and pressed look are set here.
        m_frameHooks.maximizeState = [this](bool hovered, bool pressed) {
            for (QToolButton *button : std::as_const(m_maximizeButtons)) {
                if (!button) continue;
                const bool hot = button == m_hotMaximize;
                button->setAttribute(Qt::WA_UnderMouse, hot && hovered);
                button->setDown(hot && pressed);
                button->update();
            }
        };
        m_frameHooks.maximizeClicked = [this] { if (m_hotMaximize) m_hotMaximize->click(); };
    }
    m_systemCorners = WindowFrame::install(this);
    if (m_systemCorners) clearMask();  // a mask would hide the system's corners and shadow
}

void MainWindow::setFocusMode(bool on) {
    if (on == m_focusMode) return;
    m_focusMode = on;
    if (on) {
        m_focusWasMaximized = isMaximized();
        showFullScreen();
        statusBar()->showMessage(tr("Focus mode: Esc to exit"), 3000);
    } else if (isFullScreen()) {
        m_focusWasMaximized ? showMaximized() : showNormal();
    }
    WindowFrame::setRoundedCorners(this, !on);  // no rounded corners on a full screen
    syncFocusModeControls();
}

void MainWindow::syncFocusModeControls() {
    if (m_focusButton) m_focusButton->setChecked(m_focusMode);
    if (QAction *action = m_actions.value(QStringLiteral("focusMode"))) action->setChecked(m_focusMode);
}

void MainWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
    // FEAT-045 phase 2: an agent's call without a path goes to the window used last.
    if (event->type() == QEvent::ActivationChange && isActiveWindow() && m_agentHost) m_agentHost->markActive();
    // The frame's corners (paintEvent) and the mask: the platform can send the resize before the new
    // state (offscreen on macOS), leaving the rounded mask on a full screen window.
    if (event->type() == QEvent::WindowStateChange) { updateCornerMask(); update(); }
    // Win+Down, the Maximize button...: a window that is not full screen is not in focus mode. Checked
    // once the pending window events are through: the platform can report an older state late (the
    // offscreen one does), which would end focus mode just entered.
    if (event->type() == QEvent::WindowStateChange && m_focusMode && !isFullScreen()) {
        QTimer::singleShot(0, this, [this] {
            if (!m_focusMode || isFullScreen()) return;
            m_focusMode = false;
            WindowFrame::setRoundedCorners(this, true);
            syncFocusModeControls();
        });
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    // Reached only by keys the focused widget left unused: editing, menus and the map's own Esc
    // (relationship, format painter, highlights) come first.
    // FEAT-093: an Esc nobody used leaves the tour (the map's editing keeps its own Esc).
    if (m_tour && m_tour->isRunning() && event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        m_tour->stop();
        event->accept();
        return;
    }
    if (m_focusMode && event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        setFocusMode(false);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

bool MainWindow::event(QEvent *event) {
    const bool handled = QMainWindow::event(event);
    // The style belongs to the native window: a new one (recreated by Qt) gets it again.
    if (event->type() == QEvent::WinIdChange && internalWinId()) installWindowFrame();
    return handled;
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    if (m_frameHooks.contentAt && WindowFrame::handleMessage(this, message, result, m_frameHooks)) return true;
    return QMainWindow::nativeEvent(eventType, message, result);
}

QAction *MainWindow::makeAction(const QString &id, ShortcutRegistry::Area area, const QString &text,
                                const QList<QKeySequence> &defaults, const std::function<void()> &slot) {
    auto *action = new QAction(text, this);
    m_shortcuts->add(id, area, text, defaults);
    action->setShortcuts(m_shortcuts->shortcuts(id));
    connect(action, &QAction::triggered, this, slot);
    m_actions.insert(id, action);
    return action;
}

// FEAT-072 F5 (checked live 2026-10-22): sidebar on the left (brand, New map, Recent, Open
// local file; More templates and Settings at the bottom), main area with the page title, a search
// button for the recent maps and the cards: "New map" first, then the recent maps. "More templates"
// shows our templates in the same area. The account and "Create with AI" are left out.
QWidget *MainWindow::createHomePage() {
    auto *page = new QWidget;
    page->setObjectName("homePage");
    // BUG-041: Home's empty parts move the window, like the editor's top bar (controls and cards do not).
    const auto dragArea = [this](QWidget *widget) {
        widget->setProperty("windowDragArea", true);
        widget->installEventFilter(this);
    };
    dragArea(page);
    auto *outer = new QHBoxLayout(page);
    outer->setContentsMargins(kHomeMarginPx, kHomeMarginPx, 0, kHomeMarginPx);
    outer->setSpacing(0);
    // Hugeicons are dark in the files: light on the dark Home (AGENTS.md "Icone").
    const auto lightIcon = [](const QString &path) {
        QPixmap pixmap = QIcon(path).pixmap(QSize(16, 16), qApp->devicePixelRatio());
        QPainter tint(&pixmap);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(pixmap.rect(), UiTheme::color(UiTheme::Token::Text));
        tint.end();
        return QIcon(pixmap);
    };

    auto *navigation = new QWidget;
    navigation->setFixedWidth(kHomeSidebarWidthPx);
    navigation->setObjectName("HomeNav");
    navigation->setAttribute(Qt::WA_StyledBackground);
    navigation->setStyleSheet(UiTheme::qss(
        "QWidget#HomeNav{background:@surface;border-radius:10px;}"
        "QToolButton#HomeRow{color:@text;background:transparent;border:0;border-radius:6px;padding:0 8px;"
        " font-size:13px;text-align:left;min-height:30px;max-height:30px;}"
        "QToolButton#HomeRow:hover{background:@divider;}"
        "QToolButton#HomeRow:checked{background:@surfaceRaised;}"));
    auto *nav = new QVBoxLayout(navigation);
    nav->setContentsMargins(12, 12, 12, 12);
    nav->setSpacing(4);
    auto *brandRow = new QHBoxLayout;
    brandRow->setContentsMargins(4, 4, 4, 12);
    brandRow->setSpacing(8);
    auto *brandIcon = new QLabel;
    brandIcon->setPixmap(windowIcon().pixmap(QSize(18, 18)));
    brandIcon->setStyleSheet("background:transparent;");
    brandRow->addWidget(brandIcon);
    auto *brand = new QLabel(QStringLiteral("Mentis"));
    brand->setStyleSheet("font-size:14px;font-weight:700;color:white;background:transparent;");
    brandRow->addWidget(brand, 1);
    nav->addLayout(brandRow);
    dragArea(navigation); dragArea(brandIcon); dragArea(brand);
    auto *newButton = new QPushButton(tr("New map"));
    newButton->setObjectName("accent");
    newButton->setIcon([] {  // white "+" on the accent button
        QPixmap pixmap = QIcon(QStringLiteral(":/assets/hugeicons/plus.svg")).pixmap(QSize(16, 16), qApp->devicePixelRatio());
        QPainter tint(&pixmap);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(pixmap.rect(), Qt::white);
        return QIcon(pixmap);
    }());
    newButton->setFixedHeight(30);
    connect(newButton, &QPushButton::clicked, this, &MainWindow::newMap);
    nav->addWidget(newButton);
    nav->addSpacing(4);
    const auto row = [&](const QString &icon, const QString &text, bool checkable) {
        auto *button = new QToolButton;
        button->setObjectName("HomeRow");
        button->setIcon(lightIcon(icon));
        button->setIconSize(QSize(16, 16));
        button->setText(QStringLiteral("  ") + text);  // about 10 px between icon and text
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setCheckable(checkable);
        button->setAutoExclusive(checkable);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    m_homeRecentRow = row(":/assets/hugeicons/clock.svg", tr("Recent"), true);
    m_homeRecentRow->setChecked(true);
    nav->addWidget(m_homeRecentRow);
    auto *openRow = row(":/assets/hugeicons/folder-open.svg", tr("Open local file"), false);
    connect(openRow, &QToolButton::clicked, this, &MainWindow::openMap);
    nav->addWidget(openRow);
    nav->addStretch();
    m_homeTemplatesRow = row(":/assets/hugeicons/templates.svg", tr("More templates"), true);
    nav->addWidget(m_homeTemplatesRow);
    auto *settingsRow = row(":/assets/hugeicons/settings-general.svg", tr("Settings"), false);
    connect(settingsRow, &QToolButton::clicked, this, [this] { showSettings(); });
    nav->addWidget(settingsRow);
    outer->addWidget(navigation);

    // Main area: title row (title, search on the right) and the cards of the chosen page.
    auto *content = new QWidget;
    content->setStyleSheet(UiTheme::qss("background:@surfaceSunken;"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(24, 4, 24, 0);
    contentLayout->setSpacing(16);
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(8);
    auto *title = new QLabel(tr("Recent"));
    title->setObjectName("HomeTitle");
    title->setStyleSheet("font-size:20px;font-weight:700;color:white;");
    titleRow->addWidget(title);
    titleRow->addStretch();
    dragArea(content); dragArea(title);
    m_homeSearch = new QLineEdit;
    m_homeSearch->setObjectName("HomeSearch");
    m_homeSearch->setPlaceholderText(tr("Search recent maps"));
    m_homeSearch->setClearButtonEnabled(true);
    m_homeSearch->setFixedSize(220, 32);
    m_homeSearch->setStyleSheet(UiTheme::qss("QLineEdit{color:@text;background:@surface;border:1px solid @divider;border-radius:6px;padding:0 8px;font-size:13px;}"
                                             "QLineEdit:focus{border-color:@accent;}"));
    m_homeSearch->hide();
    titleRow->addWidget(m_homeSearch);
    auto *searchButton = new QToolButton;
    searchButton->setObjectName("HomeSearchButton");
    searchButton->setIcon(lightIcon(":/assets/hugeicons/search.svg"));
    searchButton->setIconSize(QSize(16, 16));
    searchButton->setFixedSize(32, 32);
    searchButton->setToolTip(tr("Search recent maps"));
    searchButton->setStyleSheet(UiTheme::qss("QToolButton{background:@surface;border:0;border-radius:6px;} QToolButton:hover{background:@surfaceRaised;}"));
    titleRow->addWidget(searchButton);
    contentLayout->addLayout(titleRow);
    connect(searchButton, &QToolButton::clicked, this, [this] {
        const bool show = !m_homeSearch->isVisible();
        m_homeSearch->setVisible(show);
        if (show) m_homeSearch->setFocus();
        else m_homeSearch->clear();
    });
    connect(m_homeSearch, &QLineEdit::textChanged, this, &MainWindow::refreshRecentFiles);

    m_homeStack = new QStackedWidget;
    // Recent: "New map" card, then the recent maps.
    auto *recentScroll = new QScrollArea;
    recentScroll->setWidgetResizable(true);
    recentScroll->setFrameShape(QFrame::NoFrame);
    auto *recentCards = new QWidget;
    recentCards->setStyleSheet("background:transparent;");
    m_recentGrid = new QGridLayout(recentCards);
    m_recentGrid->setContentsMargins(0, 0, 0, 0);
    m_recentGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_recentGrid->setHorizontalSpacing(24);
    m_recentGrid->setVerticalSpacing(16);
    recentScroll->setWidget(recentCards);
    dragArea(recentCards);
    m_homeStack->addWidget(recentScroll);
    // More templates: the gallery of map types, then our templates.
    auto *templatesScroll = new QScrollArea;
    templatesScroll->setWidgetResizable(true);
    templatesScroll->setFrameShape(QFrame::NoFrame);
    auto *templateCards = new QWidget;
    templateCards->setStyleSheet("background:transparent;");
    auto *templateGrid = new QGridLayout(templateCards);
    templateGrid->setContentsMargins(0, 0, 0, 0);
    templateGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    templateGrid->setHorizontalSpacing(24);
    templateGrid->setVerticalSpacing(16);
    const QStringList templateNames{tr("All templates"), tr("Free map"),
                                    tr("Classic mind map"), tr("Project"), tr("Night theme"),
                                    tr("Brainstorming"), tr("SWOT analysis"), tr("Action plan")};
    for (int i = 0; i < templateNames.size(); ++i) {
        QWidget *card = nullptr;
        if (i == 0) {
            card = homeCard(QPixmap(), templateNames[i], [this] { if (maybeSave() && chooseTemplate()) showEditor(); });
        } else {
            card = homeCard(templatePreview(i - 1), templateNames[i], [this, i] {
                if (!maybeSave()) return;
                applyTemplate(i - 1);
                showEditor();
            });
        }
        templateGrid->addWidget(card, i / kHomeColumns, i % kHomeColumns);
    }
    templatesScroll->setWidget(templateCards);
    dragArea(templateCards);
    m_homeStack->addWidget(templatesScroll);
    contentLayout->addWidget(m_homeStack, 1);
    outer->addWidget(content, 1);

    const auto showHomePage = [this, title, searchButton](bool templates) {
        m_homeStack->setCurrentIndex(templates ? 1 : 0);
        title->setText(templates ? tr("More templates") : tr("Recent"));
        searchButton->setVisible(!templates);
        if (templates) { m_homeSearch->clear(); m_homeSearch->hide(); }
    };
    connect(m_homeRecentRow, &QToolButton::clicked, this, [showHomePage] { showHomePage(false); });
    connect(m_homeTemplatesRow, &QToolButton::clicked, this, [showHomePage] { showHomePage(true); });
    return page;
}

// FEAT-072 F5: card of the Home: a 160x100 rounded tile (picture or a big "+") and the
// name under it; the border of the hover follows the selection colour (HomeCardButton).
QWidget *MainWindow::homeCard(const QPixmap &picture, const QString &name, const std::function<void()> &open) {
    auto *card = new QWidget;
    card->setObjectName("HomeCard");
    card->setFixedWidth(kHomeCardWidthPx);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto *tile = new HomeCardButton<QToolButton>;
    tile->setFixedSize(kHomeCardWidthPx, kHomeCardHeightPx);
    tile->setCursor(Qt::PointingHandCursor);
    tile->setToolTip(name);
    tile->setAccessibleName(name);
    if (picture.isNull()) {
        tile->setText(QStringLiteral("+"));
        tile->setStyleSheet(UiTheme::qss("QToolButton{font-size:40px;font-weight:200;color:@text;background:@surface;border:0;border-radius:8px;}"
                                         "QToolButton:pressed{background:@surfaceRaised;}"));
    } else {
        tile->setIcon(QIcon(picture.scaled(QSize(kHomeCardWidthPx, kHomeCardHeightPx) * devicePixelRatioF(),
                                           Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)));
        tile->setIconSize(QSize(kHomeCardWidthPx - 8, kHomeCardHeightPx - 8));
        tile->setStyleSheet("QToolButton{background:#ffffff;border:0;border-radius:8px;padding:4px;}");
    }
    connect(tile, &QToolButton::clicked, this, open);
    layout->addWidget(tile);
    auto *label = new QLabel;
    label->setAlignment(Qt::AlignHCenter);
    label->setStyleSheet(UiTheme::qss("color:@text;font-size:12px;background:transparent;"));
    label->setText(label->fontMetrics().elidedText(name, Qt::ElideRight, kHomeCardWidthPx));
    layout->addWidget(label);
    return card;
}

QWidget *MainWindow::createEditorPage() {
    auto *page = new EditorSurface;  // FEAT-036: sfondo della mappa (updateWindowFrame)
    m_scene->setBackgroundBrush(QColor("#ffffff"));
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setStyleSheet(R"(
        QGraphicsView { background:#ffffff; border:0; border-radius:0; }
        QScrollBar:vertical {
            background:transparent;
            width:10px;
            margin:56px 2px 16px 2px;
        }
        QScrollBar::handle:vertical {
            background:#c4cad2;
            min-height:48px;
            border-radius:3px;
        }
        QScrollBar::handle:vertical:hover { background:#9aa2ac; }
        QScrollBar::handle:vertical:pressed { background:#5f6873; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }
        QAbstractScrollArea::corner { background:transparent; border:0; }
    )");
    auto *layout = new QGridLayout(page); layout->setContentsMargins(12, 12, 12, 8); layout->setSpacing(0);
    auto *top = new QWidget; top->setObjectName("EditorTopBar"); top->setFixedHeight(48); top->setStyleSheet("background:transparent;");
    top->setProperty("windowDragArea", true); top->installEventFilter(this);
    auto *row = new QHBoxLayout(top); m_topBarLayout = row; row->setContentsMargins(0, 0, 0, 0); row->setSpacing(8);

    auto *hamburger = new QToolButton; hamburger->setIcon(QIcon(":/assets/hugeicons/menu.svg")); hamburger->setToolTip(tr("Main menu"));
    // FEAT-036: icona chiara sugli sfondi scuri. Proprieta', non nome: il nome diventa poi
    // "PlainToolButton" per lo stile (2026-10-19: per questo l'icona restava scura).
    hamburger->setProperty("mainMenuButton", true);
    hamburger->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(hamburger);
    menu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->setStyleSheet(UiTheme::qss(R"(
        QMenu { background:@surface; color:@text; border:1px solid @divider; border-radius:8px; padding:4px; }
        QMenu::item { min-width:280px; padding:8px 24px 8px 12px; margin:2px 0; border-radius:4px; }
        QMenu::item:selected { background:@surfaceRaised; color:white; }
        QMenu::item:disabled { color:@textMuted; }
        QMenu::separator { height:1px; background:@divider; margin:4px 12px; }
        QMenu::right-arrow { margin-right:12px; }
    )"));
    menu->setObjectName(QStringLiteral("MainMenu"));
    m_mainMenu = menu;  // filled by buildMainMenu once the map "+" menu exists (its items go in Insert)
    hamburger->setMenu(menu); row->addWidget(hamburger);

    auto *home = new QToolButton; home->setIcon(QIcon(":/assets/hugeicons/home.svg")); home->setToolTip(tr("Open the library"));
    connect(home, &QToolButton::clicked, this, [this] { if (maybeSave()) showHome(); }); row->addWidget(home);
    auto *mapTitle = new QLabel(tr("Mind map")); mapTitle->setStyleSheet("color:#202630;font-size:14px;font-weight:600;padding-left:8px;");
    mapTitle->setProperty("windowDragArea", true); mapTitle->installEventFilter(this); row->addWidget(mapTitle);
    row->addStretch();

    auto addTool = [this, row](const QString &iconPath, const QString &tooltip, const QString &actionId) {
        auto *button = new QToolButton; button->setIcon(QIcon(iconPath)); button->setToolTip(tooltip);
        connect(button, &QToolButton::clicked, m_actions[actionId], &QAction::trigger); row->addWidget(button); return button;
    };
    auto *siblingButton = addTool(":/assets/hugeicons/sibling.svg", tr("Add sibling; with no selection, adds to the main topic"), "addSibling");
    auto *childButton = addTool(":/assets/hugeicons/child.svg", tr("Add a child to the selected node"), "addChild");
    auto *relation = new QToolButton; relation->setIcon(QIcon(":/assets/hugeicons/relation.svg")); relation->setToolTip(tr("Create a relationship; with no selection, creates a floating node")); connect(relation, &QToolButton::clicked, this, &MainWindow::beginRelation); row->addWidget(relation);
    auto *summary = new QToolButton; summary->setIcon(QIcon(":/assets/hugeicons/summary.svg")); summary->setToolTip(tr("Add a summary to the selected node")); connect(summary, &QToolButton::clicked, this, &MainWindow::addSummary); row->addWidget(summary);
    auto *boundary = new QToolButton; boundary->setIcon(QIcon(":/assets/hugeicons/boundary.svg")); boundary->setToolTip(tr("Draw a dashed boundary around the selected nodes")); connect(boundary, &QToolButton::clicked, this, &MainWindow::addBoundary); row->addWidget(boundary);
    auto *insert = new QToolButton; insert->setIcon(QIcon(":/assets/hugeicons/plus.svg")); insert->setToolTip(tr("Insert content")); insert->setPopupMode(QToolButton::InstantPopup);
    auto *insertMenu = new QMenu(insert);
    insertMenu->setTitle(tr("Insert"));  // title when shown as the topic menu's submenu (FEAT-042)
    m_mapInsertMenu = insertMenu;
    // Argomento mobile (richiesta dell'utente 2026-10-21); icona del "+" in attesa di una propria.
    insertMenu->addAction(QIcon(":/assets/hugeicons/menu-plus.svg"), tr("Floating topic"), this, &MainWindow::startFloatingPlacement);
    insertMenu->addSeparator();
    // FEAT-041 stage B (Q4): the entries show the keys of their commands.
    showMenuKey(insertMenu->addAction(QIcon(":/assets/hugeicons/menu-image.svg"), tr("Image…"), this, &MainWindow::chooseImage), "image");
    showMenuKey(insertMenu->addAction(QIcon(":/assets/hugeicons/menu-formula.svg"), tr("LaTeX formula…"), this, &MainWindow::insertFormula), "formula");
    insert->setMenu(insertMenu); row->addWidget(insert);
    insertMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint); insertMenu->setAttribute(Qt::WA_TranslucentBackground);
    // Icona a 12 px dal bordo della voce (QMenu::icon left), come il testo del menu principale:
    // Qt disegna l'icona al bordo della voce ignorando il padding (richiesta 2026-10-12);
    // padding-left 20 lascia 12 px tra icona (16 px) e testo.
    insertMenu->setStyleSheet(UiTheme::qss("QMenu{background:@surface;color:@text;border:1px solid @divider;border-radius:8px;padding:4px;} QMenu::item{min-width:176px;padding:8px 24px 8px 20px;margin:2px 0;border-radius:4px;} QMenu::item:selected{background:@surfaceRaised;} QMenu::icon{left:12px;} "));
    // FEAT-027: Collegamento -> Argomento… / Pagina web…, solo con un nodo selezionato.
    auto *linkMenu = insertMenu->addMenu(QIcon(":/assets/hugeicons/menu-link.svg"), tr("Link"));
    linkMenu->setObjectName(QStringLiteral("LinkMenu"));
    linkMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint); linkMenu->setAttribute(Qt::WA_TranslucentBackground);
    linkMenu->setStyleSheet(insertMenu->styleSheet());
    showMenuKey(linkMenu->addAction(QIcon(":/assets/hugeicons/menu-link.svg"), tr("Topic…"), m_actions["topicLink"], &QAction::trigger), "topicLink");
    showMenuKey(linkMenu->addAction(QIcon(":/assets/hugeicons/menu-link-web.svg"), tr("Web page…"), m_actions["webLink"], &QAction::trigger), "webLink");
    // FEAT-059: nota del nodo selezionato.
    showMenuKey(insertMenu->addAction(QIcon(":/assets/hugeicons/menu-note.svg"), tr("Note"), m_actions["note"], &QAction::trigger), "note");
    // FEAT-051: adesivo (nel posto dell'immagine) e marcatori.
    // Tappa 2: aprono la pagina del pannello laterale.
    showMenuKey(insertMenu->addAction(QIcon(":/assets/hugeicons/menu-sticker.svg"), tr("Sticker…"), this, [this] { showIconPanel(1); }), "stickers");
    showMenuKey(insertMenu->addAction(QIcon(":/assets/hugeicons/menu-marker.svg"), tr("Marker"), this, [this] { showIconPanel(0); }), "markers");
    // FEAT-049: file del computer, incorporati nel documento.
    showMenuKey(insertMenu->addAction(QIcon(":/assets/hugeicons/menu-attachment.svg"), tr("File…"), m_actions["attachFiles"], &QAction::trigger), "attachFiles");
    // FEAT-060: etichette del nodo selezionato.
    showMenuKey(insertMenu->addAction(QIcon(":/assets/hugeicons/menu-label.svg"), tr("Label"), m_actions["labels"], &QAction::trigger), "labels");
    // FEAT-048: "Cose da fare" > "Casella di spunta" sui nodi selezionati.
    auto *todoMenu = insertMenu->addMenu(QIcon(":/assets/hugeicons/menu-todo.svg"), tr("To-do"));
    todoMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    todoMenu->setAttribute(Qt::WA_TranslucentBackground);
    todoMenu->setStyleSheet(insertMenu->styleSheet());
    showMenuKey(todoMenu->addAction(QIcon(":/assets/hugeicons/menu-checkbox.svg"), tr("Checkbox"), this, [this] { toggleTaskBoxes(m_scene->selectedNodes()); }), "addTask");
    connect(insertMenu, &QMenu::aboutToShow, this, [this, linkMenu] { linkMenu->menuAction()->setEnabled(m_scene->selectedNodes().size() == 1); });

    auto *indentRight = new QToolButton; indentRight->setIcon(QIcon(":/assets/hugeicons/indent-right.svg")); indentRight->setToolTip(tr("Indent (Tab)")); connect(indentRight, &QToolButton::clicked, this, &MainWindow::indentOutlineNode); row->addWidget(indentRight);
    auto *indentLeft = new QToolButton; indentLeft->setIcon(QIcon(":/assets/hugeicons/indent-left.svg")); indentLeft->setToolTip(tr("Outdent (Shift+Tab)")); connect(indentLeft, &QToolButton::clicked, this, &MainWindow::outdentOutlineNode); row->addWidget(indentLeft);
    auto *outlineAdd = new QToolButton; outlineAdd->setIcon(QIcon(":/assets/hugeicons/plus.svg")); outlineAdd->setToolTip(tr("Add content to the branch")); outlineAdd->setPopupMode(QToolButton::InstantPopup);
    auto *outlineAddMenu = new QMenu(outlineAdd);
    // Stesso aspetto del menu + della mappa (prima usava lo stile generico, più stretto).
    outlineAddMenu->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint); outlineAddMenu->setAttribute(Qt::WA_TranslucentBackground);
    outlineAddMenu->setStyleSheet(insertMenu->styleSheet());
    outlineAddMenu->addAction(QIcon(":/assets/hugeicons/menu-text.svg"), tr("Text"), this, &MainWindow::addOutlineSibling);
    outlineAddMenu->addAction(QIcon(":/assets/hugeicons/menu-formula.svg"), tr("LaTeX formula…"), this, [this] { if (auto *node = outlineSelectedNode()) { updateInspector(node); insertFormula(); } });
    outlineAddMenu->addAction(QIcon(":/assets/hugeicons/menu-image.svg"), tr("Image…"), this, [this] { if (auto *node = outlineSelectedNode()) { updateInspector(node); chooseImage(); } });
    outlineAddMenu->addAction(QIcon(":/assets/hugeicons/menu-note.svg"), tr("Note"), this, [this] {
        if (auto *node = outlineSelectedNode()) openNote(node);  // FEAT-059
    });
    outlineAddMenu->addAction(QIcon(":/assets/hugeicons/menu-sticker.svg"), tr("Sticker…"), this, [this] { showIconPanel(1); });  // FEAT-051
    outlineAddMenu->addAction(QIcon(":/assets/hugeicons/menu-marker.svg"), tr("Marker"), this, [this] { showIconPanel(0); });
    outlineAddMenu->addAction(QIcon(":/assets/hugeicons/menu-attachment.svg"), tr("File…"), this, [this] {
        if (auto *node = outlineSelectedNode()) chooseFilesToAttach(node);  // FEAT-049
    });
    outlineAddMenu->addAction(QIcon(":/assets/hugeicons/menu-label.svg"), tr("Label"), this, [this] {
        if (auto *node = outlineSelectedNode()) openLabels(node);  // FEAT-060
    });
    outlineAddMenu->addAction(QIcon(":/assets/hugeicons/menu-checkbox.svg"), tr("Checkbox"), this, [this] {
        if (auto *node = outlineSelectedNode()) toggleTaskBoxes({node});  // FEAT-048
    });
    outlineAddMenu->setTitle(tr("Insert"));  // titolo usato quando compare come sottomenu del tasto destro
    m_outlineAddMenu = outlineAddMenu;
    buildMainMenu(m_mainMenu);  // FEAT-072 F1: after the map "+" menu, whose items go in Insert
    outlineAdd->setMenu(outlineAddMenu); row->addWidget(outlineAdd);
    row->addStretch();
    auto *fit = addTool(":/assets/hugeicons/fit.svg", tr("Fit to window"), "fit");
    // FEAT-087: its own group, shown in every view (the view group is the map's).
    m_focusButton = new QToolButton;
    m_focusButton->setObjectName(QStringLiteral("FocusModeToggle"));
    m_focusButton->setIcon(QIcon(":/assets/hugeicons/focus.svg"));
    m_focusButton->setToolTip(tr("Focus mode"));
    m_focusButton->setCheckable(true);
    connect(m_focusButton, &QToolButton::clicked, this, [this] { setFocusMode(!m_focusMode); });
    row->insertWidget(row->indexOf(fit) + 1, m_focusButton);
    // FEAT-092: the manual's page of the place the user is in, in the same group (every view).
    auto *helpButton = new QToolButton;
    helpButton->setObjectName(QStringLiteral("HelpButton"));
    helpButton->setIcon(QIcon(QStringLiteral(":/assets/hugeicons/help.svg")));
    helpButton->setToolTip(tr("User manual (F1)"));
    connect(helpButton, &QToolButton::clicked, this, [this] { showManual(helpPage()); });
    row->insertWidget(row->indexOf(m_focusButton) + 1, helpButton);

    auto *style = new QToolButton; style->setIcon(QIcon(":/assets/hugeicons/panel.svg")); style->setToolTip(tr("Show/hide side panel"));
    // Acceso a pannello aperto (FloatingUi::Tone::checkedFill).
    style->setObjectName(QStringLiteral("PanelToggle"));
    style->setCheckable(true);
    // 2026-10-14: apertura animata (toggleInspector), solo con il profilo Standard.
    connect(style, &QToolButton::clicked, this, [this] { toggleDockPage(0); });
    // FEAT-051 tappa 2: faccina accanto, per marcatori e adesivi nella stessa colonna; accesa a
    // pagina aperta.
    auto *iconsToggle = new QToolButton; iconsToggle->setIcon(QIcon(":/assets/hugeicons/sticker.svg"));  // scuro come panel.svg: retint lo schiarisce sugli sfondi scuri
    iconsToggle->setToolTip(tr("Markers and stickers"));
    iconsToggle->setObjectName(QStringLiteral("IconPanelToggle"));
    iconsToggle->setCheckable(true);
    connect(iconsToggle, &QToolButton::clicked, this, [this] { toggleDockPage(1); });
    // FEAT-063: Present, first on the left of the group (user's decision 2026-10-26).
    auto *present = new QToolButton; present->setIcon(QIcon(":/assets/hugeicons/present.svg"));
    present->setToolTip(tr("Present (F5)"));
    present->setObjectName(QStringLiteral("PresentButton"));
    connect(present, &QToolButton::clicked, this, &MainWindow::presentPitch);
    row->addWidget(present); row->addWidget(iconsToggle); row->addWidget(style);
    auto makeGroup = [top, row](const QList<QToolButton *> &buttons) {
        const int insertionIndex = row->indexOf(buttons.first());
        auto *group = new ToolbarGroupFrame(top);
        group->setObjectName("ToolbarGroup");
        for (auto *button : buttons) group->layout()->addWidget(button);
        row->insertWidget(insertionIndex, group, 0, Qt::AlignVCenter);
        return static_cast<QWidget *>(group);
    };
    hamburger->setObjectName("PlainToolButton"); auto *navigationGroup = makeGroup({home});
    // Il titolo resta nel gruppo di navigazione. Lo stato testuale e' stato sostituito
    // dal pulsante di salvataggio flottante richiesto, per non occupare spazio orizzontale.
    row->removeWidget(mapTitle);
    mapTitle->setObjectName("MapTitle"); mapTitle->setStyleSheet(QString());
    auto *titleBox = new QWidget; titleBox->setStyleSheet("background:transparent;");
    auto *titleBoxLayout = new QVBoxLayout(titleBox); titleBoxLayout->setContentsMargins(8, 0, 12, 0); titleBoxLayout->setSpacing(0);
    titleBoxLayout->addStretch(); titleBoxLayout->addWidget(mapTitle); titleBoxLayout->addStretch();
    static_cast<QHBoxLayout *>(navigationGroup->layout())->addWidget(titleBox);

    m_saveStateButton = new QToolButton;
    m_saveStateButton->setObjectName("SaveStateButton");
    connect(m_saveStateButton, &QToolButton::clicked, m_actions["save"], &QAction::trigger);
    row->insertWidget(row->indexOf(navigationGroup) + 1, m_saveStateButton);
    auto *saveStateGroup = makeGroup({m_saveStateButton});
    Q_UNUSED(saveStateGroup);
    updateMapSubtitle();
    auto *topicGroup = makeGroup({siblingButton, childButton});
    auto *structureGroup = makeGroup({relation, summary, boundary, insert});
    auto *outlineGroup = makeGroup({indentRight, indentLeft, outlineAdd});
    auto *viewGroup = makeGroup({fit});
    makeGroup({m_focusButton, helpButton});
    auto *panelGroup = makeGroup({present, iconsToggle, style});
    m_mapToolbarButtons = {topicGroup, structureGroup, viewGroup, panelGroup};
    m_outlineToolbarButtons = {outlineGroup};
    outlineGroup->hide();
    // Gantt: the top bar's middle group, in the order panel, resources, critical path, baseline
    // (user, 2026-10-30).
    m_ganttGroup = new ToolbarGroupFrame(top);
    m_ganttGroup->setObjectName("ToolbarGroup");
    // FEAT-080: the Gantt's side panel (Task and Gantt pages), as the map's PanelToggle.
    auto *ganttPanel = new QToolButton;
    ganttPanel->setObjectName(QStringLiteral("ganttPanelButton"));
    ganttPanel->setIcon(QIcon(QStringLiteral(":/assets/hugeicons/panel-left.svg")));
    ganttPanel->setCheckable(true);
    ganttPanel->setAccessibleName(tr("Show/hide side panel"));
    ganttPanel->setToolTip(tr("Show/hide side panel"));
    m_ganttGroup->layout()->addWidget(ganttPanel);
    // FEAT-079: the document's people and the sheet's working week, in a modal.
    auto *resources = new QToolButton;
    resources->setObjectName(QStringLiteral("ganttResourcesButton"));
    resources->setIcon(QIcon(QStringLiteral(":/assets/hugeicons/user-group.svg")));
    resources->setAccessibleName(tr("Resources"));
    resources->setToolTip(tr("Resources: the people of the project and the working week"));
    connect(resources, &QToolButton::clicked, this, &MainWindow::openResources);
    m_ganttGroup->layout()->addWidget(resources);
    // The critical path shown or not (user, 2026-10-28): an icon button that stays pressed while it
    // is shown, like Bold on bold text.
    auto *critical = new QToolButton;
    critical->setObjectName(QStringLiteral("ganttCriticalButton"));
    critical->setIcon(QIcon(QStringLiteral(":/assets/hugeicons/alert.svg")));
    critical->setCheckable(true);
    critical->setAccessibleName(tr("Critical path"));
    critical->setToolTip(tr("Critical path: show the tasks and links that set the end date"));
    m_ganttGroup->layout()->addWidget(critical);
    // FEAT-082: snapshots of the plan to compare with, in a popover; pressed while comparing.
    auto *baseline = new QToolButton;
    baseline->setObjectName(QStringLiteral("ganttBaselineButton"));
    baseline->setIcon(QIcon(QStringLiteral(":/assets/hugeicons/camera.svg")));
    baseline->setCheckable(true);
    baseline->setAccessibleName(tr("Baseline"));
    baseline->setToolTip(tr("Baseline: snapshots of the plan to compare with"));
    m_ganttGroup->layout()->addWidget(baseline);
    row->insertWidget(row->indexOf(outlineGroup) + 1, m_ganttGroup, 0, Qt::AlignVCenter);
    m_ganttGroup->hide();
    row->addSpacing(8);
    m_windowControls = new QWidget;
    m_windowControls->setStyleSheet("background:transparent;");
    auto *windowControlsLayout = new QHBoxLayout(m_windowControls);
    windowControlsLayout->setContentsMargins(0, 0, 0, 0); windowControlsLayout->setSpacing(0);
    auto *minimize = new QToolButton; minimize->setProperty("windowControl", true); minimize->setProperty("controlKind", "minimize"); minimize->setToolTip(tr("Minimize")); minimize->setIcon(QIcon(":/assets/hugeicons/window-minimize.svg"));
    connect(minimize, &QToolButton::clicked, this, &QWidget::showMinimized); windowControlsLayout->addWidget(minimize);
    auto *maximize = new QToolButton; maximize->setProperty("windowControl", true); maximize->setProperty("controlKind", "maximize"); maximize->setToolTip(tr("Maximize or restore")); maximize->setIcon(QIcon(":/assets/hugeicons/window-maximize.svg"));
    connect(maximize, &QToolButton::clicked, this, [this] { isMaximized() ? showNormal() : showMaximized(); }); windowControlsLayout->addWidget(maximize);
    m_maximizeButtons.append(maximize);
    auto *closeButton = new QToolButton; closeButton->setProperty("windowControl", true); closeButton->setProperty("controlKind", "close"); closeButton->setProperty("closeControl", true); closeButton->setToolTip(tr("Close")); closeButton->setIcon(QIcon(":/assets/hugeicons/window-close.svg"));
    connect(closeButton, &QToolButton::clicked, this, &QWidget::close); windowControlsLayout->addWidget(closeButton);
    row->addWidget(m_windowControls);
    // Seconda serie di controlli, fissa nell'intestazione del pannello laterale (2026-10-14, richiesta
    // dell'utente). Prima la stessa serie si spostava tra barra dell'editor e pannello; ora il pannello
    // ha già i suoi controlli e aprendolo spariscono solo quelli dell'editor: durante lo scorrimento
    // sembrano essere sempre stati nel pannello. Ogni pulsante ripete il clic del corrispondente.
    m_panelWindowControls = new QWidget;
    m_panelWindowControls->setStyleSheet("background:transparent;");
    auto *panelControlsLayout = new QHBoxLayout(m_panelWindowControls);
    panelControlsLayout->setContentsMargins(0, 0, 0, 0); panelControlsLayout->setSpacing(0);
    for (QToolButton *source : {minimize, maximize, closeButton}) {
        auto *button = new QToolButton;
        button->setProperty("controlKind", source->property("controlKind"));
        button->setToolTip(source->toolTip());
        button->setFixedSize(40, 32); button->setIconSize({16, 16});  // come i controlli della barra
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, source, &QToolButton::click);
        panelControlsLayout->addWidget(button);
        if (source == maximize) m_maximizeButtons.append(button);
    }
    styleWindowControls(m_panelWindowControls, true);
    // m_panelWindowControls goes into the side panel title bar, built by ensureInspector().
    top->setStyleSheet(R"(
        QWidget#EditorTopBar { background:transparent; }
        QWidget#ToolbarGroup { background:transparent; border:0; }
        QToolButton#PlainToolButton { background:transparent; border:0; border-radius:6px; padding:0; }
        QToolButton#PlainToolButton:hover { background:rgba(15,23,42,15); }
        QToolButton#PlainToolButton::menu-indicator { image:none; }
        /* Bottoni, titolo e sottotitolo dei gruppi: FloatingUi::controlsStyleSheet (tono dello sfondo). */
    )");
    for (auto *button : top->findChildren<QToolButton *>()) {
        if (button->property("windowControl").toBool()) continue;
        button->setFixedSize(32, 32); button->setIconSize({20, 20});
    }
    const QString controlStyle = "QToolButton{background:transparent;border:0;border-radius:4px;padding:0;} QToolButton:hover{background:#dce3ea;} QToolButton:pressed{background:#cbd4df;}";
    const QString closeStyle = "QToolButton{background:transparent;border:0;border-radius:4px;padding:0;} QToolButton:hover{background:#e5484d;} QToolButton:pressed{background:#c9343a;}";
    for (auto *button : {minimize, maximize, closeButton}) {
        button->setFixedSize(40, 32); button->setIconSize({16, 16});
        button->setStyleSheet(button->property("closeControl").toBool() ? closeStyle : controlStyle);
    }
    // FEAT-085: Copy/Paste style on the three views of the sheet; the side panel's fields keep the keys.
    for (const auto id : {"copyStyle", "pasteStyle"}) {
        m_actions[id]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        for (QWidget *view : {static_cast<QWidget *>(m_view), static_cast<QWidget *>(m_outline)})
            view->addAction(m_actions[id]);  // the Gantt's in ensureGanttView()
    }
    // FEAT-041 stage B: the new commands' keys (Ctrl+U, Ctrl+K...) also stay with the text being typed.
    qApp->installEventFilter(new TypingKeysFilter([this](const QKeyEvent *key) {
        if (key->modifiers() == Qt::NoModifier || !typingInField()) return false;
        if (m_shortcuts->matches(QStringLiteral("copyStyle"), key) || m_shortcuts->matches(QStringLiteral("pasteStyle"), key)) return true;
        const QKeySequence pressed(key->keyCombination());
        for (const QStringList *ids : {&m_mapCommands, &m_windowCommands})
            for (const QString &id : *ids)
                for (const QKeySequence &shortcut : m_actions[id]->shortcuts())
                    if (shortcut.matches(pressed) == QKeySequence::ExactMatch) return true;
        return false;
    }, this));
    // The style sheet is set while the stack is empty: set on a filled container it re-styles every
    // control below at once.
    m_workspacePages = new QStackedWidget;
    m_workspacePages->setStyleSheet("QStackedWidget{border:0;border-radius:0;background:#ffffff;}");
    m_workspacePages->addWidget(m_view); m_workspacePages->addWidget(m_outlinePage);
    layout->addWidget(m_workspacePages, 0, 0);

    // Il canvas resta visibile sotto la fascia superiore, ma i contenuti si dissolvono
    // progressivamente dietro ai controlli. Il widget e' trasparente agli eventi mouse.
    auto *topFade = new EditorTopFade(page);
    layout->addWidget(topFade, 0, 0, Qt::AlignTop);
    // FEAT-078: the Gantt's cards start below the top bars, nothing scrolls under them: no fade
    // (it greyed the cards' tops, user's photos 2026-10-27).
    connect(m_workspacePages, &QStackedWidget::currentChanged, topFade,
            [this, topFade] { topFade->setVisible(workspaceView() != WorkspaceView::Gantt); });

    auto *bottomBar = new FloatingGroup(page);
    bottomBar->setObjectName("EditorBottomBar");
    // I messaggi brevi (es. "Modifica annullata") prima apparivano nella barra di stato
    // scura in fondo alla finestra, ora nascosta: li mostriamo qui, a sinistra.
    auto *hintLabel = new QLabel(bottomBar); hintLabel->setObjectName("HintLabel"); hintLabel->hide();
    m_nodeCountLabel = new QLabel(bottomBar);
    // Etichette alte quanto i controlli, testo 12 px secondario (FloatingUi::controlsStyleSheet).
    for (QLabel *label : {hintLabel, m_nodeCountLabel}) label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // Select modificabile: si sceglie dall'elenco o si scrive un valore (es. 137%).
    m_zoomCombo = new FloatingSelect(bottomBar);
    m_zoomCombo->setEditable(true);
    m_zoomCombo->setInsertPolicy(QComboBox::NoInsert);
    for (const int zoom : {25, 50, 75, 100, 125, 150, 200, 300, 400})
        m_zoomCombo->addItem(QString::number(zoom) + "%", zoom);
    m_zoomCombo->setCurrentText("100%");
    m_zoomCombo->setToolTip(tr("Ctrl+wheel: zoom"));
    m_outlineToggle = new QPushButton(tr("Outliner"), bottomBar);
    m_ganttToggle = new QPushButton(tr("Gantt"), bottomBar);
    for (QPushButton *button : {m_outlineToggle, m_ganttToggle}) {
        button->setFlat(true);
        button->setFocusPolicy(Qt::NoFocus);
    }
    // FEAT-075: time scale of the Gantt chart, next to the zoom (FEAT-078).
    m_ganttScale = new FloatingSelect(bottomBar);
    m_ganttScale->setObjectName(QStringLiteral("ganttScale"));
    m_ganttScale->addItem(tr("Days"), int(GanttChart::Scale::Days));
    m_ganttScale->addItem(tr("Weeks"), int(GanttChart::Scale::Weeks));
    m_ganttScale->addItem(tr("Months"), int(GanttChart::Scale::Months));
    m_ganttScale->hide();
    // FEAT-035 part B: the labels as tall as the controls, the selects as wide as their text, at the
    // chosen text size and density (88 and 104 px at 14 px).
    const auto fitBottomBar = [this, hintLabel] {
        const int height = UiTheme::heightPx(UiTheme::space(FloatingUi::kControlSize));
        for (QLabel *label : {hintLabel, m_nodeCountLabel}) label->setFixedHeight(height);
        m_zoomCombo->setFixedWidth(std::max(88, UiTheme::fontPx(88)));
        m_ganttScale->setFixedWidth(std::max(104, UiTheme::fontPx(104)));
    };
    fitBottomBar();
    connect(UiTheme::AppearanceNotifier::instance(), &UiTheme::AppearanceNotifier::changed, bottomBar, fitBottomBar);
    connect(m_ganttScale, &QComboBox::currentIndexChanged, this, [this] {
        if (m_ganttView) m_ganttView->setScale(GanttChart::Scale(m_ganttScale->currentData().toInt()));
    });
    bottomBar->addWidget(hintLabel);
    bottomBar->addWidget(m_nodeCountLabel);
    bottomBar->addWidget(m_zoomCombo);
    bottomBar->addWidget(m_ganttScale);
    bottomBar->addWidget(m_outlineToggle);
    bottomBar->addWidget(m_ganttToggle);
    connect(statusBar(), &QStatusBar::messageChanged, hintLabel, [hintLabel](const QString &message) {
        hintLabel->setText(message); hintLabel->setVisible(!message.isEmpty());
    });
    connect(m_zoomCombo, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        QString value = text; value.remove('%');
        bool ok = false; const int percent = value.trimmed().toInt(&ok);
        if (!ok) return;
        if (m_workspacePages->currentWidget() == m_outlinePage) applyOutlineZoom(percent);
        else if (workspaceView() == WorkspaceView::Gantt) m_ganttView->chart()->setZoomPercent(percent);
        else m_view->setZoomPercent(percent);
    });
    connect(m_zoomCombo->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        // A value outside the Gantt's range shows the clamped one.
        if (workspaceView() != WorkspaceView::Gantt) return;
        const QSignalBlocker blocker(m_zoomCombo);
        m_zoomCombo->setCurrentText(QString::number(m_ganttView->chart()->zoomPercent()) + "%");
    });
    connect(m_view, &MindMapView::resized, this, &MainWindow::keepSelectionBesidePanel);
    connect(m_view, &MindMapView::zoomChanged, this, [this](int percent) {
        // Con l'outliner visibile il selettore mostra lo zoom dell'outliner, non quello della mappa.
        if (m_workspacePages->currentWidget() == m_outlinePage) return;
        const QSignalBlocker blocker(m_zoomCombo);
        m_zoomCombo->setCurrentText(QString::number(percent) + "%");
    });
    // The two buttons show the views not shown (FEAT-075): on the map Outliner and Gantt, on the
    // outliner Mind map and Gantt, on the Gantt Mind map and Outliner.
    connect(m_outlineToggle, &QPushButton::clicked, this, [this] {
        setWorkspaceView(workspaceView() == WorkspaceView::Map ? WorkspaceView::Outline : WorkspaceView::Map);
    });
    connect(m_ganttToggle, &QPushButton::clicked, this, [this] {
        setWorkspaceView(workspaceView() == WorkspaceView::Gantt ? WorkspaceView::Outline : WorkspaceView::Gantt);
    });
    updateBottomBar();
    auto *bottomInset = new QWidget(page);
    bottomInset->setStyleSheet("background:transparent;");
    auto *bottomInsetLayout = new QVBoxLayout(bottomInset);
    // Il riquadro bianco resta a 16 px dai bordi: si tolgono i margini riservati all'ombra.
    bottomInsetLayout->setContentsMargins(0, 0, 16 - FloatingGroup::kShadowSide, 16 - FloatingGroup::kShadowBottom);
    bottomInsetLayout->setSpacing(0);
    bottomInsetLayout->addWidget(bottomBar);
    layout->addWidget(bottomInset, 0, 0, Qt::AlignRight | Qt::AlignBottom);

    // Stesso gruppo della barra in alto (ToolbarGroupFrame: bianco, raggio 10, padding 4,
    // 2 px tra i controlli, ombra leggera disegnata a mano senza effetti con buffer).
    auto *sheetBar = new FloatingGroup(page);
    sheetBar->setObjectName("SheetBar");
    m_sheetCombo = new FloatingSelect(sheetBar);
    m_sheetCombo->setObjectName(QStringLiteral("sheetSelect"));
    m_sheetCombo->setFixedWidth(176);
    auto *addSheetButton = new QToolButton(sheetBar);
    addSheetButton->setObjectName(QStringLiteral("AddSheetButton"));  // the tour points at it
    // Icona Hugeicons 20 px al posto del carattere "+", che il font non centrava nel bottone.
    addSheetButton->setIcon(QIcon(":/assets/hugeicons/plus.svg"));
    addSheetButton->setToolTip(tr("Add sheet"));
    FloatingUi::setupIconButton(addSheetButton);
    sheetBar->addWidget(m_sheetCombo);
    sheetBar->addWidget(addSheetButton);
    connect(m_sheetCombo, QOverload<int>::of(&QComboBox::activated), this, &MainWindow::switchSheet);
    connect(m_sheetCombo, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        if (m_sheetCombo) m_sheetCombo->setToolTip(text);
    });
    connect(addSheetButton, &QToolButton::clicked, this, &MainWindow::addSheet);
    // FEAT-004: right-click on the select, as a sheet tab menu.
    m_sheetCombo->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sheetCombo, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        menu.setAttribute(Qt::WA_TranslucentBackground);
        menu.setStyleSheet(UiTheme::qss(m_outlineAddMenu->styleSheet() +
                           " QMenu::item:disabled{color:#6b7280;} QMenu::item{padding-left:12px;}"));
        QAction *rename = menu.addAction(tr("Rename sheet…"));
        QAction *duplicate = menu.addAction(tr("Duplicate sheet"));
        QAction *remove = menu.addAction(tr("Delete sheet"));
        remove->setEnabled(m_document->sheets().size() > 1);
        QAction *chosen = menu.exec(m_sheetCombo->mapToGlobal(pos));
        if (chosen == rename) renameSheet();
        else if (chosen == duplicate) duplicateSheet();
        else if (chosen == remove) deleteSheet();
    });
    if (auto *select = dynamic_cast<FloatingSelect *>(m_sheetCombo))
        select->reordered = [this](int from, int to) { m_document->moveSheet(from, to); };  // FEAT-004
    auto *sheetInset = new QWidget(page);
    sheetInset->setStyleSheet("background:transparent;");
    auto *sheetInsetLayout = new QVBoxLayout(sheetInset);
    // Il riquadro bianco resta a 16 px dai bordi: si tolgono i margini riservati all'ombra.
    sheetInsetLayout->setContentsMargins(16 - ToolbarGroupFrame::kShadowSide, 0, 0,
                                         16 - ToolbarGroupFrame::kShadowBottom);
    sheetInsetLayout->addWidget(sheetBar);
    layout->addWidget(sheetInset, 0, 0, Qt::AlignLeft | Qt::AlignBottom);
    syncSheetSelect();
    layout->addWidget(top, 0, 0, Qt::AlignTop);
    topFade->stackUnder(top);
    top->raise();
    return page;
}

// FEAT-072 F5: "New map" first, then the recent maps (filtered by the search field).
void MainWindow::refreshRecentFiles() {
    while (auto *item = m_recentGrid->takeAt(0)) { delete item->widget(); delete item; }
    const QString filter = m_homeSearch ? m_homeSearch->text().trimmed() : QString();
    int card = 0;
    if (filter.isEmpty()) m_recentGrid->addWidget(homeCard(QPixmap(), tr("New map"), [this] { newMap(); }), 0, card++);
    for (const QString &path : QSettings().value("recentFiles").toStringList()) {
        const QFileInfo info(path);
        if (!info.exists() || !info.completeBaseName().contains(filter, Qt::CaseInsensitive)) continue;
        m_recentGrid->addWidget(homeCard(recentMapPreview(path), info.completeBaseName(), [this, path] { if (maybeSave()) readFile(path); }),
                                card / kHomeColumns, card % kHomeColumns);
        ++card;
        if (card == kHomeMaxRecent + 1) break;
    }
}

QPixmap MainWindow::recentMapPreview(const QString &path) const {
    constexpr int kPreviewWidth = 480;   // doppia risoluzione rispetto alla scheda (HiDPI)
    constexpr int kPreviewHeight = 272;
    const QFileInfo source(path);
    const QByteArray cacheKey = QCryptographicHash::hash(
        source.absoluteFilePath().toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString cacheDirectory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/recent-previews");
    const QString cachePath = cacheDirectory + QLatin1Char('/') + QString::fromLatin1(cacheKey) + QStringLiteral(".jpg");
    const QFileInfo cached(cachePath);
    QImage preview;
    // 2026-10-14: prima si legge l'anteprima salvata nel documento (salvataggio esplicito, chiusura,
    // ritorno alla Home). Prima la Home caricava e disegnava l'intera mappa sul thread
    // dell'interfaccia, e il salvataggio automatico, cambiando la data del file, la faceva rifare
    // a ogni ritorno alla Home.
    if (const QByteArray embedded = BmapStorage::loadPreview(path); !embedded.isEmpty()) preview.loadFromData(embedded, "JPG");
    if (preview.isNull() && cached.exists() && cached.lastModified() >= source.lastModified()) preview.load(cachePath);

    // File senza anteprima (creati prima del 2026-10-14): con il profilo Leggero non si carica la
    // mappa solo per disegnarla, si mostra il segnaposto.
    if (preview.isNull() && Performance::generateMissingPreviews()) {
        MindMapModel previewModel;
        QString error;
        if (BmapStorage::load(path, previewModel, &error, false)) {
            MindMapScene previewScene(&previewModel);
            previewScene.setBackgroundBrush(Qt::white);
            const QRectF content = previewScene.itemsBoundingRect();
            if (!content.isEmpty()) {
                preview = QImage(kPreviewWidth, kPreviewHeight, QImage::Format_RGB32);
                preview.fill(Qt::white);
                QPainter painter(&preview);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setRenderHint(QPainter::TextAntialiasing);
                const QRectF target(16, 16, kPreviewWidth - 32, kPreviewHeight - 32);
                previewScene.render(&painter, target, content, Qt::KeepAspectRatio);
                painter.end();
                QDir().mkpath(cacheDirectory);
                preview.save(cachePath, "JPG", 82);
            }
        }
    }

    if (preview.isNull()) {
        preview = QImage(kPreviewWidth, kPreviewHeight, QImage::Format_RGB32);
        preview.fill(Qt::white);
        QPainter painter(&preview);
        painter.setPen(QPen(QColor("#c4cad2"), 4));
        painter.drawRect(QRect(196, 80, 88, 112));
        painter.drawLine(212, 112, 268, 112);
        painter.drawLine(212, 136, 268, 136);
    }

    // Gli angoli della miniatura seguono il raggio 8px delle superfici.
    QPixmap rounded(kPreviewWidth, kPreviewHeight);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, kPreviewWidth, kPreviewHeight), 16, 16);
    painter.setClipPath(clip);
    painter.drawImage(0, 0, preview);
    painter.setClipping(false);
    painter.setPen(QPen(QColor(15, 23, 42, 28), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(1, 1, kPreviewWidth - 2, kPreviewHeight - 2), 16, 16);
    return rounded;
}

QPixmap MainWindow::templatePreview(int templateIndex) const {
    constexpr int kWidth = 464;
    constexpr int kHeight = 208;
    const bool night = templateIndex == 3;
    QPixmap preview(kWidth, kHeight);
    preview.fill(night ? UiTheme::color(UiTheme::Token::Surface) : Qt::white);
    QPainter painter(&preview);
    painter.setRenderHint(QPainter::Antialiasing);

    struct PreviewNode { QPointF center; QString text; QColor fill; QSizeF size{104, 32}; };
    QList<PreviewNode> nodes;
    QList<QPair<int, int>> edges;
    const QColor rootFill = night ? QColor("#465bc2") : QColor("#7dd3e8");
    const QList<QColor> colors{QColor("#ffd166"), QColor("#ff8a8f"), QColor("#8bd99b"), QColor("#8294d6")};
    const auto addRootAndChildren = [&](const QString &root, const QStringList &children,
                                        const QPointF &rootCenter, const QList<QPointF> &positions) {
        nodes.append({rootCenter, root, rootFill, QSizeF(120, 36)});
        for (int i = 0; i < children.size(); ++i) {
            nodes.append({positions.value(i), children[i], colors[i % colors.size()]});
            edges.append({0, i + 1});
        }
    };

    switch (templateIndex) {
    case 0:
        nodes.append({QPointF(232, 104), tr("Central Topic"), rootFill, QSizeF(132, 40)});
        break;
    case 2:
        addRootAndChildren(tr("Project"), {tr("Research"), tr("Development"), tr("Launch")},
                           QPointF(92, 104), {QPointF(280, 48), QPointF(326, 104), QPointF(280, 160)});
        break;
    case 4:
        addRootAndChildren(tr("Brainstorming"), {tr("Idea 1"), tr("Idea 2"), tr("Idea 3")},
                           QPointF(232, 104), {QPointF(88, 48), QPointF(376, 48), QPointF(376, 160)});
        break;
    case 5:
        addRootAndChildren(tr("SWOT analysis"), {tr("Strengths"), tr("Weaknesses"), tr("Opportunities"), tr("Threats")},
                           QPointF(232, 104), {QPointF(82, 48), QPointF(382, 48), QPointF(82, 160), QPointF(382, 160)});
        break;
    case 6:
        addRootAndChildren(tr("Action plan"), {tr("Goal"), tr("Tasks"), tr("Next step")},
                           QPointF(72, 104), {QPointF(216, 48), QPointF(286, 104), QPointF(368, 160)});
        break;
    default:
        addRootAndChildren(tr("Central Topic"), {tr("Idea 1"), tr("Idea 2"), tr("Idea 3"), tr("Idea 4")},
                           QPointF(232, 104), {QPointF(78, 48), QPointF(386, 48), QPointF(78, 160), QPointF(386, 160)});
        break;
    }

    painter.setPen(QPen(night ? QColor("#8294d6") : QColor("#c4cad2"), 3));
    for (const auto &edge : std::as_const(edges)) {
        QPainterPath branch;
        branch.moveTo(nodes[edge.first].center);
        const QPointF destination = nodes[edge.second].center;
        const qreal middle = (nodes[edge.first].center.x() + destination.x()) / 2.0;
        branch.cubicTo(QPointF(middle, nodes[edge.first].center.y()), QPointF(middle, destination.y()), destination);
        painter.drawPath(branch);
    }
    QFont font = UiTheme::documentFont(); font.setPixelSize(12); font.setWeight(QFont::DemiBold); painter.setFont(font);  // a map
    for (const auto &node : std::as_const(nodes)) {
        const QRectF box(node.center.x() - node.size.width() / 2.0, node.center.y() - node.size.height() / 2.0,
                         node.size.width(), node.size.height());
        painter.setPen(QPen(node.fill.darker(112), 2));
        painter.setBrush(node.fill);
        painter.drawRoundedRect(box, 8, 8);
        painter.setPen(node.fill.lightnessF() < 0.42 ? Qt::white : QColor("#202630"));
        painter.drawText(box.adjusted(6, 2, -6, -2), Qt::AlignCenter, node.text);
    }
    return preview;
}

void MainWindow::showHome() {
    if (m_homeRecentRow && !m_homeRecentRow->isChecked()) m_homeRecentRow->click();  // back on Recent
    refreshRecentFiles();
    m_pages->setCurrentWidget(m_homePage);
    if (m_outlineDock) m_outlineDock->hide();
    if (m_inspectorDock) m_inspectorDock->hide();
    if (m_navigationDock) m_navigationDock->hide();
    if (m_editorToolbar) m_editorToolbar->hide();
    if (m_leftRail) m_leftRail->hide();
    statusBar()->hide();
}

void MainWindow::showEditor() {
    ensureInspector();
    m_pages->setCurrentWidget(m_editorPage);
    if (m_editorToolbar) m_editorToolbar->show();
    if (m_leftRail) m_leftRail->show();
    setOutlineMode(false); statusBar()->hide();
}

void MainWindow::rememberRecent(const QString &path) {
    if (path.isEmpty()) return;
    QSettings settings; auto recent = settings.value("recentFiles").toStringList();
    recent.removeAll(path); recent.prepend(path); while (recent.size() > 12) recent.removeLast();
    settings.setValue("recentFiles", recent);
}

void MainWindow::createActions() {
    using Area = ShortcutRegistry::Area;
    const auto keys = [](std::initializer_list<QKeySequence> list) { return QList<QKeySequence>(list); };
    makeAction("home", Area::General, tr("Library"), keys({QKeySequence("Ctrl+Shift+H"), QKeySequence("Ctrl+H")}), [this] { if (maybeSave()) showHome(); });
    makeAction("new", Area::General, tr("New"), keys({QKeySequence(QKeySequence::New)}), [this] { newMap(); });
    makeAction("open", Area::General, tr("Open…"), keys({QKeySequence(QKeySequence::Open)}), [this] { openMap(); });
    makeAction("save", Area::General, tr("Save"), keys({QKeySequence(QKeySequence::Save)}), [this] { saveMap(); });
    makeAction("saveAs", Area::General, tr("Save as…"), keys({QKeySequence(QKeySequence::SaveAs)}), [this] { saveMapAs(); });
    // BUG-017: azioni registrate (prima solo nel menu File, nascosto con la barra dei menu).
    makeAction("importMarkdown", Area::General, tr("Import Markdown…"), {}, [this] { importMarkdown(); });
    makeAction("exportMarkdown", Area::General, tr("Export Markdown…"), {}, [this] { exportMarkdown(); });
    makeAction("exportImage", Area::General, tr("Export image…"), {}, [this] { exportImage(); });
    makeAction("exportPdf", Area::General, tr("Export PDF…"), {}, [this] { exportPdfOrPrint(false); });
    makeAction("exportPresentation", Area::General, tr("Export Pitch…"), {}, [this] { exportPresentation(); });
    makeAction("exportWord", Area::General, tr("Export Word…"), {}, [this] { exportOffice(Document::OfficeFormat::Word); });
    makeAction("exportExcel", Area::General, tr("Export Excel…"), {}, [this] { exportOffice(Document::OfficeFormat::Excel); });
    makeAction("print", Area::General, tr("Print…"), {QKeySequence::Print}, [this] { exportPdfOrPrint(true); });
    makeAction("present", Area::General, tr("Present"), keys({QKeySequence(Qt::Key_F5), QKeySequence("Ctrl+Alt+P")}), [this] { presentPitch(); });
    makeAction("undo", Area::General, tr("Undo"), keys({QKeySequence(QKeySequence::Undo)}), [this] { undo(); });
    // Ctrl+Y e' la seconda predefinita di Ripristina (standard su Windows); prima era aggiunta a mano.
    makeAction("redo", Area::General, tr("Redo"), keys({QKeySequence("Ctrl+Shift+Z"), QKeySequence("Ctrl+Y")}), [this] { redo(); });
    // B7 (2026-10-13): "Disponi automaticamente" dimentica anche gli spostamenti fatti a mano.
    makeAction("layout", Area::General, tr("Auto layout"), keys({QKeySequence("Ctrl+L")}), [this] { m_model->clearManualOffsets(); m_scene->autoLayout(); setDirty(); });
    makeAction("fit", Area::General, tr("Fit to window"), keys({QKeySequence("Ctrl+0")}), [this] { m_view->fitInView(m_scene->itemsBoundingRect().adjusted(-80,-80,80,80), Qt::KeepAspectRatio); });
    // FEAT-087: ZEN mode and its key.
    makeAction("focusMode", Area::General, tr("Focus mode"), keys({QKeySequence("Ctrl+Alt+F")}), [this] { setFocusMode(!m_focusMode); })
        ->setCheckable(true);
    makeAction("image", Area::General, tr("Add image…"), keys({QKeySequence("Ctrl+Shift+I")}), [this] { chooseImage(); });
    // FEAT-027: cronologia dei salti fatti con i collegamenti.
    makeAction("linkBack", Area::General, tr("Back through links"), keys({QKeySequence("Alt+Left")}), [this] { navigateLinkHistory(true); });
    makeAction("linkForward", Area::General, tr("Forward through links"), keys({QKeySequence("Alt+Right")}), [this] { navigateLinkHistory(false); });
    makeAction("shortcuts", Area::General, tr("Shortcuts…"), {}, [this] { showSettings(true); });
    // FEAT-092: the manual's page of the place the user is in (F1; on macOS Qt's Help key).
    makeAction("help", Area::General, tr("User manual"), keys({QKeySequence(QKeySequence::HelpContents)}),
               [this] { showManual(helpPage()); });
    // Guarded: the tests make and destroy several main windows; a closed one opens nothing.
    Help::setOpener([window = QPointer<MainWindow>(this)](const QString &page) {
        if (window) window->showManual(page);
    });
    Help::installDialogKey([window = QPointer<MainWindow>(this)](const QKeyEvent *event) {
        return window && window->m_shortcuts->matches("help", event);
    });
    makeAction("settings", Area::General, tr("Settings…"), keys({QKeySequence("Ctrl+,"), QKeySequence("Ctrl+Shift+P")}), [this] { showSettings(); });
    // FEAT-085: keys on the map, the outliner and the Gantt (added to those views with the Gantt view).
    makeAction("copyStyle", Area::General, tr("Copy style"), keys({QKeySequence("Ctrl+Alt+C")}), [this] { copyStyle(); });
    makeAction("pasteStyle", Area::General, tr("Paste style"), keys({QKeySequence("Ctrl+Alt+V")}), [this] { pasteStyle(); });
    makeAction("find", Area::General, tr("Find"), keys({QKeySequence(QKeySequence::Find)}), [this] {
        // Ctrl+F again closes the open panel (user's request 2026-10-22).
        if (m_navigationDock && m_navigationDock->isVisible()) emit m_navigationPanel->closeRequested();
        else showNavigationPanel();
    });
    // Mappa: valgono solo con il fuoco sulla mappa (B1, vedi createMenusAndToolbar).
    makeAction("addChild", Area::Map, tr("Add child"), keys({QKeySequence(Qt::Key_Tab)}), [this] { addChild(); });
    makeAction("addSibling", Area::Map, tr("Add sibling"), keys({QKeySequence(Qt::Key_Return)}), [this] { addSibling(); });
    // F2 selects the whole title; Space opens it with the cursor at the end (2026-11-04, user request).
    makeAction("edit", Area::Map, tr("Edit text"), keys({QKeySequence(Qt::Key_F2)}), [this] { editSelected(); });
    makeAction("editAtEnd", Area::Map, tr("Edit text at the end"), keys({QKeySequence(Qt::Key_Space)}), [this] { editSelected(true); });
    makeAction("remove", Area::Map, tr("Remove"), keys({QKeySequence(QKeySequence::Delete)}), [this] { removeSelected(); });
    // FEAT-057: stacking order of floating topics and freely placed main branches. No default keys
    // since FEAT-041 stage B: Ctrl+Shift+[ / ] go to the sheets and Ctrl+] to the Style panel.
    using Arrange = Document::Arrange;
    makeAction("bringToFront", Area::Map, tr("Bring to front"), {}, [this] { arrangeSelected(Arrange::ToFront); });
    makeAction("bringForward", Area::Map, tr("Bring forward"), {}, [this] { arrangeSelected(Arrange::Forward); });
    makeAction("sendBackward", Area::Map, tr("Send backward"), {}, [this] { arrangeSelected(Arrange::Backward); });
    makeAction("sendToBack", Area::Map, tr("Send to back"), {}, [this] { arrangeSelected(Arrange::ToBack); });
    createTopicCommands();
    // Outliner: tasti gestiti in eventFilter con ShortcutRegistry::matches (prima fissi nel codice).
    // Navigazione con le frecce sulla mappa (richiesta dell'utente 2026-10-21), gestita in eventFilter.
    m_shortcuts->add("mapSelectLeft", Area::Map, tr("Select the node to the left"), keys({QKeySequence(Qt::Key_Left)}));
    m_shortcuts->add("mapSelectRight", Area::Map, tr("Select the node to the right"), keys({QKeySequence(Qt::Key_Right)}));
    m_shortcuts->add("mapSelectUp", Area::Map, tr("Select the node above"), keys({QKeySequence(Qt::Key_Up)}));
    m_shortcuts->add("mapSelectDown", Area::Map, tr("Select the node below"), keys({QKeySequence(Qt::Key_Down)}));
    m_shortcuts->add("outlineNewItem", Area::Outliner, tr("New item"), keys({QKeySequence(Qt::Key_Return)}));
    m_shortcuts->add("outlineIndent", Area::Outliner, tr("Indent"), keys({QKeySequence(Qt::Key_Tab)}));
    m_shortcuts->add("outlineOutdent", Area::Outliner, tr("Outdent"), keys({QKeySequence("Shift+Tab"), QKeySequence(Qt::Key_Backspace)}));
    m_shortcuts->add("outlineCopy", Area::Outliner, tr("Copy"), keys({QKeySequence(QKeySequence::Copy)}));
    m_shortcuts->add("outlineCut", Area::Outliner, tr("Cut"), keys({QKeySequence(QKeySequence::Cut)}));
    m_shortcuts->add("outlinePaste", Area::Outliner, tr("Paste"), keys({QKeySequence(QKeySequence::Paste)}));
    m_shortcuts->add("outlineDuplicate", Area::Outliner, tr("Duplicate"), keys({QKeySequence("Ctrl+D")}));
    m_shortcuts->add("outlineDelete", Area::Outliner, tr("Delete"), keys({QKeySequence(Qt::Key_Delete)}));
    // FEAT-075: keys of the Gantt table, handled by GanttView.
    m_shortcuts->add("ganttAddTask", Area::Gantt, tr("Add task"), keys({QKeySequence(Qt::Key_Return)}));
    m_shortcuts->add("ganttIndent", Area::Gantt, tr("Make subtask"), keys({QKeySequence(Qt::Key_Tab)}));
    m_shortcuts->add("ganttOutdent", Area::Gantt, tr("Move up a level"), keys({QKeySequence("Shift+Tab")}));
    m_shortcuts->add("ganttDelete", Area::Gantt, tr("Delete tasks"), keys({QKeySequence(Qt::Key_Delete)}));
    m_shortcuts->add("ganttChain", Area::Gantt, tr("Link in a chain"), keys({QKeySequence("Ctrl+Shift+L")}));
    // Una scorciatoia cambiata nelle Impostazioni vale subito (decisione 2026-10-17).
    connect(m_shortcuts, &ShortcutRegistry::changed, this, [this](const QString &id) {
        if (QAction *action = m_actions.value(id)) action->setShortcuts(m_shortcuts->shortcuts(id));
        if (id == QLatin1String("save")) updateMapSubtitle();  // il suggerimento mostra il tasto di Salva
    });
}

void MainWindow::createMenusAndToolbar() {
    auto *file = menuBar()->addMenu(tr("&File"));
    for (const auto id : {"new", "open", "save", "saveAs"}) file->addAction(m_actions[id]);
    file->addSeparator();
    file->addAction(m_actions["importMarkdown"]);
    file->addAction(m_actions["exportMarkdown"]);
    file->addSeparator(); file->addAction(tr("Quit"), QKeySequence::Quit, this, &QWidget::close);
    auto *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_actions["undo"]); edit->addAction(m_actions["redo"]); edit->addSeparator();
    for (const auto id : {"addChild", "addSibling", "edit", "remove", "image"}) edit->addAction(m_actions[id]);
    edit->addSeparator(); edit->addAction(m_actions["shortcuts"]); edit->addAction(m_actions["settings"]);
    auto *view = menuBar()->addMenu(tr("&View")); view->addAction(m_actions["layout"]); view->addAction(m_actions["fit"]);
    view->addAction(m_actions["find"]);
    menuBar()->hide();
    // La barra dei menu è nascosta, quindi le azioni presenti solo nei suoi menu non
    // ricevono le scorciatoie: Ctrl+Z / Ctrl+Y non facevano nulla. Aggiunte anche alla
    // finestra, le loro scorciatoie restano attive sia nella mappa sia nell'outliner.
    // Nei campi di testo Ctrl+Z continua ad annullare la digitazione del campo stesso.
    // FEAT-041 (2026-11-04): every General command is on the window. A key given in Settings to a
    // command reached only through a proxy menu entry or the hidden bar (Import/Export, Add image)
    // did nothing. Copy/Paste style stay on their views (FEAT-085).
    for (const ShortcutRegistry::Command &command : m_shortcuts->commands(ShortcutRegistry::Area::General))
        if (QAction *action = m_actions.value(command.id); action && command.id != "copyStyle" && command.id != "pasteStyle")
            addAction(action);
    // B1 (2026-10-13): Tab, Invio, F2 e Canc stavano solo nella barra dei menu nascosta,
    // quindi sulla mappa non scattavano. Aggiunte alla vista con contesto
    // WidgetWithChildrenShortcut: valgono solo quando il fuoco è sulla mappa, così non
    // rubano Invio/Tab/Canc all'outliner, alla ricerca o all'ispettore. Durante la modifica
    // del titolo il campo li riserva già con ShortcutOverride (InlineTitleEditor).
    QStringList onMap{"addChild", "addSibling", "edit", "editAtEnd", "remove", "bringToFront", "bringForward", "sendBackward", "sendToBack"};
    onMap += m_mapCommands;  // FEAT-041 stage B (Q3: map only)
    for (const QString &id : std::as_const(onMap)) {
        m_actions[id]->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        m_view->addAction(m_actions[id]);
    }
    // B4 (2026-10-13): il messaggio di "Relazione" promette Esc per annullare, ma Esc non era
    // gestito e il clic successivo creava comunque la relazione. L'azione è attiva solo
    // mentre si sceglie la destinazione, quindi negli altri momenti Esc resta libero; durante
    // la modifica del titolo il campo si tiene Esc con ShortcutOverride.
    m_cancelRelationAction = new QAction(this);
    m_cancelRelationAction->setShortcut(QKeySequence(Qt::Key_Escape));
    m_cancelRelationAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_cancelRelationAction->setEnabled(false);
    connect(m_cancelRelationAction, &QAction::triggered, this, [this] {
        m_cancelRelationAction->setEnabled(false);
        if (m_relationSource.isEmpty()) return;
        m_relationSource.clear();
        statusBar()->showMessage(tr("Relationship canceled"), 2500);
    });
    m_view->addAction(m_cancelRelationAction);
    m_editorToolbar = nullptr;
}

void MainWindow::createDocks() {
    m_outlinePage = new QWidget;
    m_outlinePage->setObjectName(QStringLiteral("OutlinePage"));
    m_outlinePage->setStyleSheet("QWidget#OutlinePage{background:#ffffff;}");  // FEAT-036: vedi applyBackdrop
    auto *outlineColumns = new QHBoxLayout(m_outlinePage);
    // A destra il margine di 32 px ora è fatto da spazio (22) + barra di scorrimento (10), vedi
    // sotto: la barra sta sul bordo della finestra (richiesta 2026-10-13).
    outlineColumns->setContentsMargins(32, 72, 0, 32);
    outlineColumns->setSpacing(0);
    auto *outlineContent = new QWidget;
    outlineContent->setStyleSheet("background:transparent;");
    auto *outlineLayout = new QVBoxLayout(outlineContent);
    outlineLayout->setContentsMargins(0, 0, 0, 0);
    outlineLayout->setSpacing(8);
    // La pagina usa circa 1/4 vuoto + 2/4 contenuto + 1/4 vuoto.
    // I 32 px esterni restano il margine minimo quando la finestra diventa stretta.
    outlineColumns->addStretch(1);
    outlineColumns->addWidget(outlineContent, 2);
    outlineColumns->addStretch(1);
    // (2026-10-13: campo con formati e barra di formattazione come le voci, vedi OutlineRootTitleEdit)
    auto *rootEdit = new OutlineRootTitleEdit;
    m_outlineRootEdit = rootEdit;
    outlineLayout->addWidget(m_outlineRootEdit);
    m_outlineRootEdit->installEventFilter(this);
    m_outlineRootEdit->viewport()->installEventFilter(this);  // il titolo con formule si disegna nel viewport
    connect(m_outlineRootEdit, &QPlainTextEdit::textChanged, this, [this] { updateRootFormula(); });
    // Barra corsivo/barrato/sottolineato sopra il testo selezionato del titolo principale
    // (richiesta 2026-10-13), uguale a quella delle voci (OutlineFormatBar): figlia della finestra,
    // ClickFocus con il campo come focus proxy, così un clic sulla barra non chiude la modifica.
    auto *rootFormatBar = new OutlineFormatBar(this);
    rootFormatBar->setFocusProxy(rootEdit);
    rootFormatBar->applyFormat = [rootEdit](const QTextCharFormat &format) { rootEdit->mergeCurrentCharFormat(format); };
    rootFormatBar->applyHighlight = [rootEdit](bool on) {  // FEAT-068
        QTextCursor cursor = rootEdit->textCursor();
        MarkdownShortcuts::setHighlight(cursor, on);
        rootEdit->setTextCursor(cursor);
    };
    rootEdit->formatBar = rootFormatBar;  // FEAT-095
    rootEdit->charStyleKey = [this](const QKeyEvent *event) { return charStyleKey(event); };
    rootFormatBar->weights = kHeavyBold;  // titolo già in grassetto: il grassetto è Black (2026-10-14)
    const auto updateRootFormatBar = [this, rootEdit, rootFormatBar] {
        const QTextCursor cursor = rootEdit->textCursor();
        // Come OutlineDelegate::updateFormatBar: niente barra senza selezione, con formule o a campo chiuso.
        if (!rootEdit->hasFocus() || !cursor.hasSelection() || FormulaText::containsMath(rootEdit->text()) ||
            !rootEdit->isVisible()) {
            rootFormatBar->hide();
            return;
        }
        rootFormatBar->sync(cursor.charFormat());
        QTextCursor start(cursor), end(cursor);
        start.setPosition(cursor.selectionStart());
        end.setPosition(cursor.selectionEnd());
        const QPoint origin = rootEdit->viewport()->mapTo(this, QPoint());
        const QRect first = rootEdit->cursorRect(start).translated(origin);
        const QRect last = rootEdit->cursorRect(end).translated(origin);
        const int right = last.top() == first.top() ? last.left() : origin.x() + rootEdit->viewport()->width();
        const QSize size = rootFormatBar->sizeHint();
        int x = (first.left() + right) / 2 - size.width() / 2;
        int y = first.top() - 8 - size.height();
        if (y < 0) y = last.bottom() + 8;
        x = qBound(0, x, qMax(0, width() - size.width()));
        rootFormatBar->setGeometry(QRect(QPoint(x, y), size));
        rootFormatBar->raise();
        rootFormatBar->show();
    };
    connect(rootEdit, &QPlainTextEdit::selectionChanged, this, updateRootFormatBar);
    connect(rootEdit, &QPlainTextEdit::cursorPositionChanged, this, updateRootFormatBar);
    m_rootFormatBarUpdate = updateRootFormatBar;
    m_outline = new OutlineTreeView;
    m_outline->setObjectName(QStringLiteral("OutlineTree"));
    // FEAT-059: la nota sotto il titolo si scrive li' (campo sul posto con la barra flottante).
    m_outlineNoteEditor = new NoteEditor(m_outline->viewport(), NoteEditor::Mode::Inline);
    connect(m_outlineNoteEditor, &NoteEditor::finished, this, &MainWindow::finishNote);
    connect(m_outlineNoteEditor, &NoteEditor::contentHeightChanged, this, [this](int height) {
        auto *view = static_cast<OutlineTreeView *>(m_outline);
        if (MindNode *node = nodeById(m_outlineNoteEditor->nodeId())) view->setNoteEditing(m_model->indexFromNode(node), height);
        QTimer::singleShot(0, this, &MainWindow::placeOutlineNote);
    });
    connect(m_outline->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::placeOutlineNote);
    static_cast<OutlineTreeView *>(m_outline)->noteClicked = [this](const QModelIndex &index) {
        if (MindNode *node = m_model->nodeFromIndex(index)) openOutlineNote(node);
    };
    // FEAT-048: la casella nell'outliner spunta il nodo come sulla mappa (un passo).
    static_cast<OutlineTreeView *>(m_outline)->labelClicked = [this](const QModelIndex &index) {
        if (MindNode *node = m_model->nodeFromIndex(index)) openLabels(node);  // FEAT-060
    };
    static_cast<OutlineTreeView *>(m_outline)->taskClicked = [this](const QModelIndex &index) {
        if (MindNode *node = m_model->nodeFromIndex(index); node && m_document->toggleTask(node)) m_scene->updateNode(node);
    };
    // FEAT-065: marcatori, immagine e icone come sulla mappa (stesse azioni, stessi menu).
    {
        auto *view = static_cast<OutlineTreeView *>(m_outline);
        view->picture = [this](const MindNode *node) { return m_scene->nodePicture(node); };
        view->mapMarkerClicked = [this](const QModelIndex &index, int i) {
            if (MindNode *node = m_model->nodeFromIndex(index); node && i < node->markers.size()) {
                const QString marker = node->markers[i];
                setMarker(node, Markers::seriesOf(marker), Markers::next(marker));
            }
        };
        view->trailingIconClicked = [this](const QModelIndex &index, OutlineTreeView::TrailingIcon icon, const QPoint &screenPos) {
            MindNode *node = m_model->nodeFromIndex(index);
            if (!node) return;
            if (icon == OutlineTreeView::TrailingIcon::Files) showFilesMenu(node, screenPos);
            else followLink(node, icon == OutlineTreeView::TrailingIcon::TopicLink ? NodeItem::LinkKind::Topic : NodeItem::LinkKind::Web);
        };
        view->pictureResized = [this](const QModelIndex &index, const QSizeF &size) {
            MindNode *node = m_model->nodeFromIndex(index);
            if (!node || !m_document->setImageSize(node, size)) return;
            m_scene->updateNode(node);
            m_scene->autoLayout();
            m_document->flushPendingStep();  // misura e ridisposizione in un solo passo
            refreshStylePage();
        };
        view->pictureActivated = [this](const QModelIndex &index) {
            if (MindNode *node = m_model->nodeFromIndex(index); node && !node->formula.isEmpty()) editFormula(node);
        };
    }
    m_outline->setModel(m_model);
    auto *outlineDelegate = new OutlineDelegate(
        [this] { addOutlineSibling(); }, [this] { indentOutlineNode(); },
        [this] { outdentOutlineNode(); }, m_outline);
    // Ogni passo di una modifica fatta nell'outliner (testo, corsivo, barrato, sottolineato) è
    // un Ctrl+Z distinto, come sulla mappa (vedi MindMapScene::historyStepFinished più sopra).
    outlineDelegate->taskTyped = [this](const QModelIndex &index, int task) { setTypedTask(m_model->nodeFromIndex(index), task); };
    outlineDelegate->charStyleKey = [this](const QKeyEvent *event) { return charStyleKey(event); };  // FEAT-095
    outlineDelegate->pasteWhileEditing = [this](const QModelIndex &index) {
        return pasteWhileEditing(m_model->nodeFromIndex(index), true);
    };
    outlineDelegate->historyStepFinished = [this] {
        m_document->flushPendingStep();
    };
    m_outline->setItemDelegate(outlineDelegate);
    m_outline->setHeaderHidden(true);
    m_outline->setFrameShape(QFrame::NoFrame);
    m_outline->setRootIsDecorated(false);
    m_outline->setItemsExpandable(true);
    m_outline->setExpandsOnDoubleClick(false);
    m_outline->setWordWrap(true);
    m_outline->setTextElideMode(Qt::ElideNone);
    m_outline->setDragDropMode(QAbstractItemView::InternalMove);
    m_outline->setDefaultDropAction(Qt::MoveAction);
    m_outline->setDropIndicatorShown(false);
    m_outline->setAcceptDrops(true);
    m_outline->setDragEnabled(false);
    // Ctrl+clic aggiunge o toglie una voce, Maiusc+clic seleziona un intervallo (richiesta
    // 2026-10-13, come la selezione multipla della mappa). Prima: una voce alla volta (predefinito).
    m_outline->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_outline->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    QPalette outlinePalette = m_outline->palette();
    for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        outlinePalette.setBrush(group, QPalette::Highlight, Qt::transparent);
        outlinePalette.setBrush(group, QPalette::HighlightedText, QColor("#172033"));
    }
    m_outline->setPalette(outlinePalette);
    m_outline->installEventFilter(this);
    outlineLayout->addWidget(m_outline, 1);
    // Barra di scorrimento dell'outliner sul bordo destro della finestra invece che accanto alla
    // colonna centrale (richiesta 2026-10-13, foto d'esempio). La barra interna di QTreeView resta
    // quella che scorre davvero (rotellina, tastiera, trascinamento) ma non si vede; questa è una
    // seconda barra collegata nei due sensi a valore, intervallo e passi.
    // Stile: quello generale delle barre (#c4cad2, larghezza 10, nel foglio di stile della finestra).
    auto *outlineScroll = new QScrollBar(Qt::Vertical);
    QSizePolicy scrollPolicy = outlineScroll->sizePolicy();
    scrollPolicy.setRetainSizeWhenHidden(true);  // nascosta non sposta la colonna centrale
    outlineScroll->setSizePolicy(scrollPolicy);
    outlineScroll->setFixedWidth(10);
    m_outline->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QScrollBar *innerScroll = m_outline->verticalScrollBar();
    const auto syncOutlineScroll = [outlineScroll, innerScroll] {
        const QSignalBlocker blocker(outlineScroll);
        outlineScroll->setRange(innerScroll->minimum(), innerScroll->maximum());
        outlineScroll->setPageStep(innerScroll->pageStep());
        outlineScroll->setSingleStep(innerScroll->singleStep());
        outlineScroll->setValue(innerScroll->value());
        outlineScroll->setVisible(innerScroll->maximum() > innerScroll->minimum());  // come ScrollBarAsNeeded
    };
    connect(innerScroll, &QScrollBar::rangeChanged, outlineScroll, syncOutlineScroll);
    connect(innerScroll, &QScrollBar::valueChanged, outlineScroll, syncOutlineScroll);
    connect(outlineScroll, &QScrollBar::valueChanged, innerScroll, &QScrollBar::setValue);
    syncOutlineScroll();
    outlineColumns->addSpacing(22);
    outlineColumns->addWidget(outlineScroll);
    m_outline->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_outline, &QWidget::customContextMenuRequested, this, &MainWindow::showOutlineContextMenu);
    // m_outline è dichiarato QTreeView nell'header ma creato come OutlineTreeView poco sopra.
    static_cast<OutlineTreeView *>(m_outline)->zoomStep = [this](int direction) {
        const qreal factor = direction > 0 ? 1.15 : 1.0 / 1.15;  // stesso passo di Ctrl+rotellina sulla mappa
        applyOutlineZoom(qRound(m_outlineZoom * factor));
    };
    // FEAT-035 part B: the outliner's sheet holds zoomed values, so it applies text size and density
    // itself (before the zoom) and the appearance filter leaves it alone.
    m_outline->setProperty(UiTheme::kOwnSizes, true);
    applyOutlineZoom(100);
    connect(UiTheme::AppearanceNotifier::instance(), &UiTheme::AppearanceNotifier::changed, m_outline,
            [this] { applyOutlineZoom(m_outlineZoom); });
    // Fine modifica: testo e formati nel nodo principale. Come per le voci (OutlineDelegate::
    // setModelData) si conserva la formattazione fatta sulla mappa e si aggiungono corsivo,
    // barrato e sottolineato del campo (mergeOutlineFormats).
    rootEdit->editingFinished = [this, rootEdit] {
        MindNode *root = m_model->root();
        const QString title = rootEdit->text().trimmed();
        if (title.isEmpty()) return;
        const QString html = mergeOutlineFormats(root->title, root->titleHtml, *rootEdit->document(), NodeItem::titleFont(root),
                                                 kHeavyBold.bold, storedBoldWeights(root));
        m_document->setFormattedTitle(root, title, html);
    };
    rootEdit->returnPressed = [this] {
        m_outline->setCurrentIndex({});
        addOutlineSibling();
    };
    // FEAT-055 (2026-10-18): un Markdown di almeno due voci incollato nel titolo principale diventa
    // tutta la mappa (prima voce = argomento principale), in un passo di storico. Una voce sola si
    // incolla come testo. Le voci dell'outliner invece creano un ramo (pasteOutlineNode).
    rootEdit->pasteContent = [this] { return pasteWhileEditing(m_model->root(), false); };
    rootEdit->pasteText = [this, rootEdit](const QString &text) {
        const Document::PasteResult result = m_document->replaceMapWithMarkdown(text);
        if (result.tooLarge) {
            statusBar()->showMessage(tr("Text too long: you can paste at most %1 items at a time.")
                                         .arg(MarkdownImport::kMaxPastedItems), 4000);
            return true;
        }
        if (!result.first) return false;
        // Il campo ha il focus, quindi refreshOutline non lo ricarica: senza questo, uscendo dal
        // campo editingFinished riscriverebbe il titolo vecchio sul nuovo argomento principale.
        loadRootTitle(rootEdit, m_model->root());
        rootEdit->moveCursor(QTextCursor::End);
        m_scene->autoLayout();
        return true;
    };
    connect(m_outline->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current) {
        if (auto *node = m_model->nodeFromIndex(current)) updateInspector(node);
    });
    connect(m_model, &QAbstractItemModel::modelReset, m_outline, &QTreeView::expandAll);
    // Bug 2026-11-02: rows inserted later (paste, duplicate, undo of a delete) stayed closed with
    // their items hidden, unlike on the map. A new branch opens whole.
    connect(m_model, &QAbstractItemModel::rowsInserted, m_outline, [this](const QModelIndex &parent, int first, int last) {
        for (int row = first; row <= last; ++row) m_outline->expandRecursively(m_model->index(row, 0, parent));
    });

    // The side panel itself is built by ensureInspector(); these follow the map and do nothing
    // while it does not exist.
    connect(m_scene, &MindMapScene::imageSelectionChanged, this, [this] {
        // Richiesta dell'utente 2026-10-21: scegliendo l'adesivo (o l'immagine) di un nodo con la
        // colonna Marcatore/Adesivo aperta si passa alla pagina Stile con bordo, ombra e misura.
        if (m_scene->selectedImageNode() && isIconPanelShown()) {
            m_dockPages->setCurrentIndex(0);
            showPanelPage(m_panelContext.manualChoice(PanelContext::Page::Style));
            return;  // showPanelPage aggiorna gia' la pagina
        }
        refreshStylePage();
    });
    connect(m_model, &MindMapModel::backdropChanged, this, &MainWindow::applyBackdrop);  // FEAT-036
    connect(m_model, &MindMapModel::stylesChanged, this, [this] { refreshStylePage(); });
    connect(m_model, &MindMapModel::styleEdited, this, [this] { refreshStylePage(); });
    connect(m_model, &MindMapModel::structureChanged, this, [this] { refreshStylePage(); });

    // FEAT-061 / FEAT-072 C: left navigation panel (Ctrl+F, View > Navigation panel).
    m_navigationPanel = new NavigationPanel;
    m_navigationPanel->sheets = [this](bool allSheets) {
        QList<NavigationPanel::SheetData> result;
        const QVector<Document::Sheet> &sheets = m_document->sheets();
        for (int i = 0; i < sheets.size(); ++i) {
            const bool current = i == m_document->currentSheet();
            if (!allSheets && !current) continue;
            if (!current && !m_document->ensureSheetLoaded(i).ok) continue;  // unreadable sheet: skipped
            result.append({sheets[i].id, sheets[i].name, current, m_document->sheetContent(i)});
        }
        return result;
    };
    m_navigationPanel->activateTopic = [this](const QString &, const QString &nodeId) {
        // After the click, like followLink: another sheet rebuilds the scene.
        QTimer::singleShot(0, this, [this, nodeId] {
            if (!revealNode(nodeId)) statusBar()->showMessage(tr("The topic no longer exists"), 3000);
        });
    };
    m_navigationPanel->activateLabel = [this](const QString &label) { m_scene->setLabelHighlight(label); };
    // Find and replace: one step for all the sheets touched, the map laid out again.
    m_navigationPanel->replaceTitles = [this](const NavigationPanel::TitleChanges &changes) {
        commitPendingEdits();
        const int count = m_document->replaceTitles(changes, [this] { m_scene->autoLayout(); });
        if (count) {
            setDirty();
            refreshOutline();
            statusBar()->showMessage(tr("Replaced in %n topic(s)", nullptr, count), 3000);
        }
        return count;
    };
    // FEAT-060 Tags tab: a label renamed or removed on every sheet in one step; the highlight follows it.
    m_navigationPanel->renameLabel = [this](const QString &from, const QString &to) {
        commitPendingEdits();
        const int count = m_document->renameLabel(from, to, [this] { m_scene->autoLayout(); });
        if (!count) return 0;
        setDirty();
        const QString highlight = m_scene->labelHighlight();
        if (!highlight.isEmpty()) m_scene->setLabelHighlight(Labels::same(highlight, from) ? to.simplified() : highlight);
        refreshOutline();
        statusBar()->showMessage(to.isEmpty() ? tr("Tag removed from %n topic(s)", nullptr, count)
                                              : tr("Tag renamed on %n topic(s)", nullptr, count), 3000);
        return count;
    };
    m_navigationDock = new QDockWidget(tr("Navigation"), this);
    m_navigationDock->setObjectName(QStringLiteral("NavigationDock"));
    m_navigationDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_navigationDock->setTitleBarWidget(new QWidget(m_navigationDock));
    m_navigationDock->setWidget(m_navigationPanel);
    m_navigationDock->setMinimumWidth(kNavigationWidthPx);
    addDockWidget(Qt::LeftDockWidgetArea, m_navigationDock);
    connect(m_navigationPanel, &NavigationPanel::closeRequested, this, [this] {
        m_navigationDock->hide();
        m_view->setFocus();
    });
    connect(m_navigationDock, &QDockWidget::visibilityChanged, this, [this] {
        applyEditorFrame(m_inspectorDock && m_inspectorDock->isVisible());
    });
    // Results follow the map, rebuilt at most every 150 ms and only with the panel open.
    m_navigationRefresh = new QTimer(this);
    m_navigationRefresh->setSingleShot(true);
    m_navigationRefresh->setInterval(150);
    connect(m_navigationRefresh, &QTimer::timeout, this, [this] { if (m_navigationDock->isVisible()) m_navigationPanel->refresh(); });
    const auto refreshNavigation = [this] { if (m_navigationDock->isVisible()) m_navigationRefresh->start(); };
    connect(m_model, &QAbstractItemModel::dataChanged, this, refreshNavigation);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, refreshNavigation);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, refreshNavigation);
    connect(m_model, &QAbstractItemModel::rowsMoved, this, refreshNavigation);
    connect(m_model, &QAbstractItemModel::modelReset, this, refreshNavigation);
    m_navigationDock->hide();

    refreshOutline();
}

// Startup (2026-11-04): the side panel (Style, Pitch, Map; Marker and Sticker) is the costliest part of
// the window, ~1.1 s measured, mostly Qt style sheet polishing of its hundreds of controls. The Home
// does not show it, so it is built after the first frame (see eventFilter) or as soon as something needs it.
// Startup (2026-11-04): the Gantt view is the second costliest part of the window, ~300-550 ms of
// style sheet polishing measured. It is built after the side panel (see eventFilter) or when first
// needed, inside its page stack: a styled tree moved under a styled container is re-styled once more.
void MainWindow::ensureGanttView() {
    if (m_ganttView) return;
    m_ganttView = new GanttView(m_document, m_shortcuts, m_workspacePages);  // FEAT-075
    m_ganttView->setMenuStyle(UiTheme::qss(m_outlineAddMenu->styleSheet()));
    connect(m_ganttView, &GanttView::resourcesRequested, this, &MainWindow::openResources);
    connect(m_ganttView, &GanttView::webLinkRequested, this, [this](const QString &id) { followLink(nodeById(id), NodeItem::LinkKind::Web); });
    connect(m_ganttView, &GanttView::noteRequested, this, [this](const QString &id, const QRect &anchor) {
        if (MindNode *node = nodeById(id)) m_noteEditor->openFor(node->id, node->notes, node->notesHtml, anchor);
    });
    if (auto *critical = m_ganttGroup->findChild<QToolButton *>(QStringLiteral("ganttCriticalButton"))) {
        critical->setChecked(m_ganttView->criticalShown());
        connect(critical, &QAbstractButton::toggled, m_ganttView, &GanttView::setCriticalShown);
    }
    if (auto *baseline = m_ganttGroup->findChild<QToolButton *>(QStringLiteral("ganttBaselineButton"))) {
        connect(baseline, &QToolButton::clicked, this, [this, baseline] {
            baseline->setChecked(!m_ganttView->comparedBaseline().isEmpty());  // pressed only while comparing
            m_ganttView->showBaselineMenu(baseline);
        });
        connect(m_ganttView, &GanttView::baselineCompared, baseline, &QToolButton::setChecked);
    }
    if (auto *panel = m_ganttGroup->findChild<QToolButton *>(QStringLiteral("ganttPanelButton"))) {
        connect(panel, &QToolButton::clicked, m_ganttView, &GanttView::togglePanel);
        connect(m_ganttView, &GanttView::panelShownChanged, panel, &QToolButton::setChecked);
    }
    // Below the top bars and above the bottom groups, like the outliner (top 72): the header and the
    // scrollbars stay fully visible, not under the fade (user's photo, 2026-10-27).
    m_ganttView->layout()->setContentsMargins(16, 72, 16, 72);
    // Topics added, moved or deleted from the Gantt: the map is laid out in the same step.
    m_ganttView->afterMapChange = [this] {
        m_scene->autoLayout();
        m_document->flushPendingStep();
        refreshOutline();
        updateBottomBar();
    };
    for (const auto id : {"copyStyle", "pasteStyle"}) m_ganttView->addAction(m_actions[id]);  // FEAT-085
    m_ganttView->copyStyle = [this] { copyStyle(); };
    m_ganttView->pasteStyle = [this] { pasteStyle(); };
    m_ganttView->canPasteStyle = [this] { return m_copiedStyle.has_value(); };
    // FEAT-078: the Gantt has its own zoom (50-300 %), next to its time scale.
    connect(m_ganttView->chart(), &GanttChart::zoomChanged, this, [this](int percent) {
        // Not while a value is being typed: the field is set when it is confirmed.
        if (workspaceView() != WorkspaceView::Gantt || m_zoomCombo->lineEdit()->hasFocus()) return;
        const QSignalBlocker blocker(m_zoomCombo);
        m_zoomCombo->setCurrentText(QString::number(percent) + "%");
    });
    m_workspacePages->addWidget(m_ganttView);
    m_ganttView->setDark(NodeStyle::isDarkBackground(m_model->backdrop().color));  // as applyBackdrop()
}

void MainWindow::ensureInspector() {
    if (m_inspectorDock) return;
    m_inspectorDock = new QDockWidget(tr("Style and properties"), this);
    m_inspectorDock->setObjectName("InspectorDock");
    m_inspectorDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto *titleBar = new QWidget;
    titleBar->setObjectName("InspectorTitleBar");
    titleBar->setFixedHeight(80);
    auto *titleLayout = new QHBoxLayout(titleBar); m_inspectorTitleLayout = titleLayout;
    // 19 = centro della barra dell'editor (34px dal bordo della finestra) - metà dei controlli (16) + 1 di bordo del dock.
    titleLayout->setContentsMargins(16, 19, 8, 8);
    titleLayout->setAlignment(Qt::AlignTop);
    auto *titleLabel = new QLabel(tr("Style and properties"));
    titleLabel->setObjectName("InspectorTitle");
    titleLayout->addWidget(titleLabel);
    titleLabel->hide();
    titleLayout->addStretch();
    m_inspectorDock->setTitleBarWidget(titleBar);
    // FEAT-017: tre pagine create una volta (Stile, Pitch, Mappa). I controlli esistenti restano
    // nascosti in questa iterazione (decisione dell'utente): Stile ospita quelli del nodo, Mappa la
    // palette che agisce su tutta la mappa. Saranno resi visibili con FEAT-019.
    auto *panel = new QWidget; panel->setObjectName("InspectorPanel"); auto *form = new QFormLayout(panel);
    auto *mapPage = new QWidget; mapPage->setObjectName("InspectorMapPage"); auto *mapForm = new QFormLayout(mapPage);
    mapForm->setContentsMargins(0, 0, 0, 0);
    m_titleEdit = new QLineEdit; form->addRow(tr("Title"), m_titleEdit);
    m_notesEdit = new QPlainTextEdit; m_notesEdit->setMaximumHeight(100); form->addRow(tr("Notes"), m_notesEdit);
    auto *fill = new QPushButton(tr("Choose…")); connect(fill, &QPushButton::clicked, this, [this] { chooseColor("fill"); }); form->addRow(tr("Fill"), fill);
    auto *text = new QPushButton(tr("Choose…")); connect(text, &QPushButton::clicked, this, [this] { chooseColor("text"); }); form->addRow(tr("Text"), text);
    auto *border = new QPushButton(tr("Choose…")); connect(border, &QPushButton::clicked, this, [this] { chooseColor("border"); }); form->addRow(tr("Border/branch"), border);
    m_branchStyle = new QComboBox; m_branchStyle->addItem(tr("Solid line"), static_cast<int>(Qt::SolidLine)); m_branchStyle->addItem(tr("Dashed line"), static_cast<int>(Qt::DashLine)); m_branchStyle->addItem(tr("Dotted"), static_cast<int>(Qt::DotLine)); form->addRow(tr("Branch style"), m_branchStyle);
    m_branchWidth = new QDoubleSpinBox; m_branchWidth->setRange(1, 12); m_branchWidth->setSingleStep(0.5); m_branchWidth->setSuffix(" px"); form->addRow(tr("Branch thickness"), m_branchWidth);
    auto *palettes = new QWidget; auto *paletteRow = new QHBoxLayout(palettes); paletteRow->setContentsMargins(0, 0, 0, 0); paletteRow->setSpacing(4);
    const QList<QStringList> paletteColors{
        {"#ff6b6b", "#ff9f68", "#ffd166", "#8bd3b0", "#6edbd5", "#74b9ff", "#a78bfa"},
        {"#ef4444", "#f97316", "#eab308", "#84cc16", "#22c55e", "#14b8a6", "#0ea5e9"},
        {"#22c55e", "#14b8a6", "#06b6d4", "#3b82f6", "#6366f1", "#8b5cf6", "#d946ef"},
        {"#8b5cf6", "#d946ef", "#ec4899", "#f43f5e", "#f97316", "#eab308", "#6366f1"}
    };
    for (const auto &colors : paletteColors) {
        auto *swatch = new QPushButton; swatch->setFixedSize(48, 32);
        QStringList stops;
        for (int i = 0; i < colors.size(); ++i)
            stops << QString("stop:%1 %2").arg(QString::number(static_cast<double>(i) / (colors.size() - 1), 'f', 3), colors[i]);
        swatch->setStyleSheet(QString("border-radius:4px;border:2px solid white;background:qlineargradient(x1:0,y1:0,x2:1,y2:0,%1);").arg(stops.join(',')));
        swatch->setToolTip(tr("Palette of 7 colors in sequence"));
        connect(swatch, &QPushButton::clicked, this, [this, colors] {
            // FEAT-019: i colori dei rami principali sono ricavati dalla palette (cascata).
            m_model->setPalette(QString(), colors);
        }); paletteRow->addWidget(swatch);
    }
    mapForm->addRow(tr("Map palette"), palettes);
    auto *image = new QPushButton(tr("Load or change…")); connect(image, &QPushButton::clicked, this, &MainWindow::chooseImage); form->addRow(tr("Image"), image);
    m_imageWidth = new QDoubleSpinBox; m_imageWidth->setRange(24, 1600); m_imageWidth->setSuffix(" px"); form->addRow(tr("Width"), m_imageWidth);
    m_imageHeight = new QDoubleSpinBox; m_imageHeight->setRange(24, 1200); m_imageHeight->setSuffix(" px"); form->addRow(tr("Height"), m_imageHeight);
    auto *applyProps = new QPushButton(tr("Apply properties")); connect(applyProps, &QPushButton::clicked, this, &MainWindow::applyInspector); form->addRow(applyProps);
    // I controlli restano interni per conservare i collegamenti esistenti, ma non sono visibili.
    for (auto *control : panel->findChildren<QWidget *>()) control->hide();
    for (auto *control : mapPage->findChildren<QWidget *>()) control->hide();
    // FEAT-031: "Tema colore" in cima alla pagina Mappa, etichetta sopra e select a tutta riga come
    // nel riferimento (03-temi-colore-pagina-mappa.png); i vecchi campioni di palette restano nascosti.
    auto *themeRow = new QWidget(mapPage);
    auto *themeLayout = new QVBoxLayout(themeRow);
    themeLayout->setContentsMargins(0, 0, 0, 0);
    themeLayout->setSpacing(0);
    themeRow->setStyleSheet(UiTheme::qss(QStringLiteral(
        "QLabel { color:@text; font-size:13px; background:transparent; } QLabel#FieldCaption { color:@textMuted; font-size:12px; }")));
    // FEAT-072 B16: map type card at the top; the popup chooses the variant. The sheet
    // direction is in the central topic's Structure section (Style page).
    m_sheetVariant = new MapTypeSelect(themeRow);
    connect(m_sheetVariant, &MapTypeSelect::variantChosen, this, &MainWindow::chooseSheetVariant);
    themeLayout->addWidget(m_sheetVariant);
    // Below the card the content sits 20 px from the panel edge and groups are divided by lines.
    auto *mapBody = new QWidget(themeRow);
    auto *bodyLayout = new QVBoxLayout(mapBody);
    bodyLayout->setContentsMargins(8, 20, 8, 0);
    bodyLayout->setSpacing(12);
    themeLayout->addWidget(mapBody);
    auto *balanceRowWidget = new QWidget(mapBody);
    auto *balanceRow = new QHBoxLayout(balanceRowWidget);
    balanceRow->setContentsMargins(0, 0, 0, 0);
    balanceRow->setSpacing(8);
    auto *balanceLabel = new QLabel(tr("Balance map"), balanceRowWidget);
    m_balanceSwitch = new ToggleSwitch(balanceRowWidget);
    m_balanceSwitch->setObjectName(QStringLiteral("BalanceMapSwitch"));
    m_balanceSwitch->setAccessibleName(tr("Balance map"));
    // Lo switch non anticipa lo stato: chiede il cambio e poi rilegge quello del foglio.
    connect(m_balanceSwitch, &ToggleSwitch::toggled, this, [this](bool on) { setBalanced(on); refreshStylePage(); });
    balanceRow->addWidget(balanceLabel);
    balanceRow->addStretch();
    balanceRow->addWidget(m_balanceSwitch);
    auto *themeLabel = new QLabel(tr("Color theme"), mapBody);
    themeLabel->setObjectName(QStringLiteral("FieldCaption"));
    m_paletteSelect = new PaletteSelect(themeRow);
    m_paletteSelect->setMinimumWidth(180);
    // FEAT-032: "+" accanto alla select apre "Crea nuovo tema colore".
    auto *newPalette = new QToolButton(themeRow);
    newPalette->setObjectName(QStringLiteral("NewPaletteButton"));
    newPalette->setToolTip(tr("Create new color theme"));
    newPalette->setAccessibleName(tr("Create new color theme"));
    newPalette->setFixedSize(24, 24);
    newPalette->setCursor(Qt::PointingHandCursor);
    newPalette->setIcon(QIcon(QStringLiteral(":/assets/hugeicons/menu-plus.svg")));
    newPalette->setIconSize(QSize(16, 16));
    newPalette->setStyleSheet(UiTheme::qss(QStringLiteral(
        "QToolButton { background:@surface; border:1px solid @divider; border-radius:4px; }"
        "QToolButton:hover { background:@divider; } QToolButton:focus { border-color:@accent; }")));
    connect(newPalette, &QToolButton::clicked, this, &MainWindow::createPalette);
    auto *selectRow = new QHBoxLayout;
    selectRow->setSpacing(8);
    selectRow->addWidget(m_paletteSelect, 1);
    selectRow->addWidget(newPalette);
    bodyLayout->addWidget(panelDivider(mapBody));
    bodyLayout->addSpacing(8);
    bodyLayout->addWidget(themeLabel);
    bodyLayout->addLayout(selectRow);
    // FEAT-036: sei varianti (sfondo = un colore della palette) e il colore di sfondo libero.
    m_variantPicker = new VariantPicker(false, themeRow);
    connect(m_variantPicker, &VariantPicker::variantChosen, this, [this](int variant) { m_model->setBackdrop(variant); });
    bodyLayout->addWidget(m_variantPicker);
    auto *backgroundRow = new QHBoxLayout;
    backgroundRow->setSpacing(8);
    auto *backgroundLabel = new QLabel(tr("Background color"), mapBody);
    m_backgroundField = new ColorField(themeRow);
    m_backgroundField->setObjectName(QStringLiteral("BackgroundColorField"));
    m_backgroundField->setAccessibleName(tr("Background color"));
    m_backgroundField->themeColors = [this] { return m_model->activePalette; };
    connect(m_backgroundField, &ColorField::colorChosen, this, [this](const QColor &color) { m_model->setBackground(color); });
    backgroundRow->addWidget(backgroundLabel);
    backgroundRow->addStretch();
    backgroundRow->addWidget(m_backgroundField);
    bodyLayout->addSpacing(8);
    bodyLayout->addWidget(panelDivider(mapBody));
    bodyLayout->addSpacing(8);
    bodyLayout->addLayout(backgroundRow);
    bodyLayout->addSpacing(8);
    bodyLayout->addWidget(panelDivider(mapBody));
    bodyLayout->addSpacing(8);
    // FEAT-043 parte stile: font, spessore delle linee e Ramo colorato del foglio (un passo ciascuno).
    m_sheetStylePanel = new SheetStylePanel(themeRow);
    connect(m_sheetStylePanel, &SheetStylePanel::sheetStyleChosen, this, [this](const NodeStyle::SheetStyle &style) {
        // FEAT-043 Rami in posizione libera: accendendolo ogni ramo principale tiene il lato che ha
        // (nulla si sposta); spegnendolo gli spostamenti a mano si tolgono e, con il bilanciamento,
        // i lati tornano automatici. Nello stesso passo dello stile del foglio.
        if (style.freeBranches != m_model->sheetStyle.freeBranches) {
            for (const auto &child : m_model->root()->children) {
                if (child->floating) continue;
                if (style.freeBranches) {
                    if (child->side == MindNode::Side::Auto)
                        child->side = m_scene->sideOf(child.get()) < 0 ? MindNode::Side::Left : MindNode::Side::Right;
                } else if (m_model->balanced) {
                    child->side = MindNode::Side::Auto;
                }
            }
            if (!style.freeBranches) {
                const std::function<void(MindNode *)> clear = [&clear](MindNode *node) {
                    node->manualOffset = {};
                    for (const auto &child : node->children) clear(child.get());
                };
                clear(m_model->root());
            }
        }
        m_model->setSheetStyle(style);
        m_scene->autoLayout();  // il font cambia la misura dei nodi; compatta e posizione libera il layout
        refreshStylePage();     // "Bilancia mappa" non vale con i rami in posizione libera
    });
    m_sheetStylePanel->addMapStyleRow(balanceRowWidget);
    connect(m_sheetStylePanel->numberingFields(), &NumberingFields::numberingChosen, this, [this](const BranchNumbering &numbering) {
        m_model->setStyle({m_model->root()}, StyleProperty::Numbering, QVariant::fromValue(numbering));
    });
    connect(m_sheetStylePanel, &SheetStylePanel::resetMapRequested, this, [this] {
        if (!m_document->resetMapStyle()) return;
        m_scene->autoLayout();  // fonts and widths change the topics' size
        refreshStylePage();
        statusBar()->showMessage(tr("Map style reset"), 2500);
    });
    bodyLayout->addWidget(m_sheetStylePanel);
    mapForm->insertRow(0, themeRow);
    connect(m_paletteSelect, &PaletteSelect::paletteChosen, this, [this](const QString &id) {
        const auto palette = CustomPalettes::lookup(id);  // di base o personalizzata (FEAT-032)
        if (!palette) return;
        const bool custom = palette->group == NodeStyle::PaletteGroup::Personalizzata;
        m_model->setPalette(id, palette->colors, custom ? palette->name : QString());  // un passo di storico
    });
    form->setContentsMargins(0, 0, 0, 0);  // il padding lo dà la superficie del pannello
    form->setAlignment({});
    // FEAT-019: pagina Stile vera, in cima alla pagina; i vecchi controlli restano nascosti sotto.
    // Ogni scelta vale per tutti i nodi selezionati ed e' un solo passo di storico (styleEdited).
    m_stylePage = new StylePage(panel);
    form->insertRow(0, m_stylePage);
    // FEAT-026 fase C: sezione Struttura della pagina Stile (un solo nodo non centrale).
    connect(m_stylePage, &StylePage::structureChosen, this, [this](const QString &type, const QString &direction) {
        const QList<MindNode *> selected = m_scene->selectedNodes();
        if (selected.size() == 1) setStructure(selected.front(), type, direction);  // the central too (FEAT-072 B13)
    });
    connect(m_stylePage, &StylePage::styleChosen, this, [this](StyleProperty property, const QVariant &value) {
        m_model->setStyle(m_scene->selectedNodes(), property, value);
    });
    // FEAT-021: larghezza dei nodi selezionati, un passo di storico (styleEdited). Ogni nodo si ferma
    // fra il suo minimo e 3000 px (MindMapScene::manualWidthFor, come trascinando il bordo).
    const auto applyWidths = [this](const std::function<qreal(const MindNode *)> &widthFor) {
        QList<std::pair<MindNode *, qreal>> widths;
        for (MindNode *node : m_scene->selectedNodes()) widths.append({node, widthFor(node)});
        m_model->setCustomWidths(widths);
    };
    connect(m_stylePage, &StylePage::widthChosen, this, [this, applyWidths](qreal px) {
        applyWidths([this, px](const MindNode *node) { return m_scene->manualWidthFor(node, px); });
    });
    connect(m_stylePage, &StylePage::widthStepped, this, [this, applyWidths](qreal deltaPx) {
        applyWidths([this, deltaPx](const MindNode *node) {
            return m_scene->manualWidthFor(node, std::round(m_scene->nodeWidth(node)) + deltaPx);
        });
    });
    connect(m_stylePage, &StylePage::widthReset, this, [applyWidths] { applyWidths([](const MindNode *) { return 0.0; }); });
    // FEAT-072 B15: Apply to related: the same style to the siblings
    // of the topic (same parent, so same level), floating ones apart.
    connect(m_stylePage, &StylePage::styleActionRequested, this, [this] {
        const QList<MindNode *> selected = m_scene->selectedNodes();
        if (selected.size() != 1) return;
        MindNode *source = selected.front();
        if (!source->parent) return;
        QList<MindNode *> related;
        for (const auto &child : source->parent->children)
            if (child->floating == source->floating) related.append(child.get());
        m_model->copyStyle(source, related);
    });
    connect(m_stylePage, &StylePage::childrenStyleRequested, this, [this] {
        m_model->copyStyleToDescendants(m_scene->selectedNodes());
    });
    connect(m_stylePage, &StylePage::resetStyleRequested, this, [this] { m_model->resetStyle(m_scene->selectedNodes()); });
    // FEAT-040: con un'immagine scelta la pagina Stile mostra solo i suoi controlli.
    m_imageStylePage = new ImageStylePage(panel);
    m_imageStylePage->hide();
    form->insertRow(0, m_imageStylePage);
    connect(m_imageStylePage, &ImageStylePage::styleChosen, this, [this](const ImageStyle &style) {
        if (MindNode *node = m_scene->selectedImageNode()) m_document->setImageStyle(node, style);
        refreshStylePage();
    });
    connect(m_imageStylePage, &ImageStylePage::sizeChosen, this, [this](const QSizeF &size) { resizeSelectedImage(size); });
    connect(m_imageStylePage, &ImageStylePage::rotateRequested, this, [this] { rotateVisual(m_scene->selectedImageNode()); });
    connect(m_imageStylePage, &ImageStylePage::sizeResetRequested, this, [this] {
        const MindNode *node = m_scene->selectedImageNode();
        if (!node) return;
        // Con la rotazione di 90 o 270 gradi la misura reale si scambia.
        const auto turned = [node](QSizeF size) { return node->imageStyle.rotation % 180 ? size.transposed() : size; };
        if (!node->formula.isEmpty()) { resizeSelectedImage(turned(NodeItem::formulaInsertionSize(node))); return; }  // FEAT-064
        // Misura d'inserimento ricalcolata dall'originale: per le immagini incorporate il file
        // estratto dal documento, non la miniatura (FEAT-037).
        QString path = node->imagePath;
        if (!node->imageAssetId.isEmpty() && m_scene->originalImagePath)
            if (const QString original = m_scene->originalImagePath(node); !original.isEmpty()) path = original;
        resizeSelectedImage(turned(NodeItem::insertionImageSize(path)));
    });
    // FEAT-053: with only relationships selected the Style page shows their controls.
    m_relationStylePage = new RelationshipStylePage(panel);
    m_relationStylePage->hide();
    m_relationStylePage->themeColors = [this] { return m_model->activePalette; };
    form->insertRow(0, m_relationStylePage);
    connect(m_relationStylePage, &RelationshipStylePage::edited, this, [this](const std::function<void(Relationship &)> &change) {
        QList<Relationship> changed;
        for (const QString &id : m_scene->selectedRelationIds())
            if (const Relationship *relationship = m_document->relation(id)) { changed.append(*relationship); change(changed.last()); }
        m_document->updateRelations(changed);
        refreshStylePage();
    });
    // FEAT-052: with only boundaries selected the Style page shows theirs.
    m_boundaryStylePage = new BoundaryStylePage(panel);
    m_boundaryStylePage->hide();
    m_boundaryStylePage->themeColors = [this] { return m_model->activePalette; };
    form->insertRow(0, m_boundaryStylePage);
    connect(m_boundaryStylePage, &BoundaryStylePage::edited, this, [this](const std::function<void(Boundary &)> &change) {
        QList<Boundary> changed;
        for (const QString &id : m_scene->selectedBoundaryIds())
            if (const Boundary *boundary = m_document->boundary(id)) { changed.append(*boundary); change(changed.last()); }
        if (m_document->updateBoundaries(changed)) { m_scene->autoLayout(); m_document->flushPendingStep(); }  // the width changes the room
        refreshStylePage();
    });
    m_styleControls = {m_titleEdit, m_notesEdit, fill, text, border, m_branchStyle, m_branchWidth,
                       image, m_imageWidth, m_imageHeight, applyProps};
    // FEAT-063: the Pitch tab; every change is a history step of the document.
    m_pitchPage = new PitchPage;
    auto *pitchPage = m_pitchPage;
    m_pitchPage->setTopicValue = [this](Document::PitchField field, const QString &value) {
        if (MindNode *topic = pitchTopic(); topic && m_document->setPitchTopic(topic, field, value)) refreshPitchPage();
    };
    m_pitchPage->setSheetValues = [this](const QString &theme, const QString &aspect, bool clearTemplate) {
        if (m_document->setPitchSheet(theme, aspect, clearTemplate ? std::optional(QJsonObject()) : std::nullopt)) refreshPitchPage();
    };
    // FEAT-089: the template's base theme is stored too, the theme the sheet falls back to.
    m_pitchPage->setSheetTemplate = [this](const QJsonObject &pitchTemplate) {
        const auto value = Pitch::templateFromJson(pitchTemplate);
        const QString theme = value ? Pitch::themeKey(value->base) : m_model->pitchTheme;
        if (m_document->setPitchSheet(theme, m_model->pitchAspect, pitchTemplate)) refreshPitchPage();
    };
    m_sidePanel = new SidePanel;
    m_sidePanel->setObjectName("InspectorSidePanel");
    // Ordine fisso Stile, Pitch, Mappa: corrisponde a PanelContext::Page.
    m_sidePanel->addPage(tr("Style"), panel);
    m_sidePanel->addPage(tr("Pitch"), pitchPage);
    m_sidePanel->addPage(tr("Map"), mapPage);
    connect(m_sidePanel, &SidePanel::pageActivated, this, [this](int index) {
        m_panelContext.manualChoice(static_cast<PanelContext::Page>(index));  // nessun dato modificato
        if (static_cast<PanelContext::Page>(index) == PanelContext::Page::Pitch) showTip(QStringLiteral("pitch"));  // FEAT-093
    });
    showPanelPage(m_panelContext.current());
    // FEAT-051 tappa 2: seconda pagina della colonna, Marcatore e Adesivo.
    m_markerPage = new IconPage(IconLibrary::Kind::Marker);
    m_stickerPage = new IconPage(IconLibrary::Kind::Sticker);
    for (IconPage *page : {m_markerPage, m_stickerPage}) {
        page->chosen = [this, page](const IconLibrary::Item &item, const QString &collectionId) { chooseIcon(page, item.path, item.name, collectionId); };
        // Un file solo aggiunto va anche nel nodo selezionato (come prima con la raccolta).
        page->filesAdded = [this, page](const QStringList &paths, const QString &collectionId) {
            if (paths.size() == 1) chooseIcon(page, paths.front(), IconLibrary::displayName(paths.front()), collectionId);
            else statusBar()->showMessage(tr("%n items added to the collection.", nullptr, int(paths.size())), 4000);
        };
    }
    m_iconPanel = new SidePanel;
    m_iconPanel->setObjectName("IconSidePanel");
    m_iconPanel->addPage(tr("Marker"), m_markerPage);
    m_iconPanel->addPage(tr("Sticker"), m_stickerPage);
    m_dockPages = new QStackedWidget;
    m_dockPages->setObjectName("InspectorDockPages");
    m_dockPages->addWidget(m_sidePanel);
    m_dockPages->addWidget(m_iconPanel);
    connect(m_dockPages, &QStackedWidget::currentChanged, this, [this] { updatePanelToggles(); });
    connect(m_iconPanel, &SidePanel::contentMinimumWidthChanged, this, [this] { applyInspectorMinimumWidth(); });
    m_inspectorDock->setWidget(m_dockPages);
    m_inspectorWidth = inspectorMinimumWidth();
    m_inspectorDock->setFixedWidth(m_inspectorWidth);  // FEAT-072 B1: not resizable
    connect(m_sidePanel, &SidePanel::contentMinimumWidthChanged, this, [this] { applyInspectorMinimumWidth(); });
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);
    m_inspectorDock->setMaximumHeight(QWIDGETSIZE_MAX);
    m_inspectorDock->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    connect(m_inspectorDock, &QDockWidget::visibilityChanged, this, [this] { updatePanelToggles(); });
    connect(m_inspectorDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        // Pannello nascosto (anche da Home o outliner) durante l'animazione di apertura: si ferma.
        if (!visible && m_inspectorAnimating) stopInspectorAnimation();
        if (!visible) m_inspectorFollowsSelection = false;
        applyEditorFrame(visible);
    });
    m_inspectorDock->hide();
    // Built after the editor page: its window controls move into the panel title bar, and the
    // pages show the document that may already be open.
    if (m_panelWindowControls) m_inspectorTitleLayout->addWidget(m_panelWindowControls);
    updateInspector(m_selected);
    refreshStylePage();
    updatePanelContext();
    updatePanelToggles();
}


// FEAT-052: nothing under or beside a summary topic yet (Tab and Enter do nothing on it).
void MainWindow::addChild() { showCreatedNode(m_document->addChild(m_scene->selectedNode())); }
void MainWindow::addSibling() { showCreatedNode(m_document->addSibling(m_scene->selectedNode())); }

// A topic added at the edge of the view would appear beyond it: the map scrolls just enough to
// show it with the 24 px grid margin (as revealNode), and does not move when it is already visible.
void MainWindow::showCreatedNode(MindNode *created) {
    if (!created) return;
    m_scene->autoLayout();
    m_scene->selectNode(created);
    m_view->reveal(m_scene->nodeSceneRect(created), 24);
}
// (B10, 2026-10-13: con più nodi selezionati ne veniva eliminato uno solo; la regola dei rami
// annidati e' nel Document.)
// Immagine, adesivo o formula tolti dal nodo (un passo con la ridisposizione).
void MainWindow::removeVisual(MindNode *node) {
    if (!node || !m_document->removeVisual(node)) return;
    m_scene->updateNode(node);
    m_scene->autoLayout();
    m_document->flushPendingStep();
    m_scene->selectNode(node);
    refreshOutline();
    refreshStylePage();
}

void MainWindow::setBalanced(bool on) {
    QHash<const MindNode *, int> sides;
    for (const auto &child : m_model->root()->children) sides.insert(child.get(), m_scene->sideOf(child.get()));
    if (!m_document->setBalanced(on, sides)) return;
    m_scene->autoLayout();  // acceso: disposizione automatica; spento: la stessa di prima
    m_document->flushPendingStep();  // un solo passo, con il ridisposto
}

void MainWindow::setStructure(MindNode *node, const QString &typeKey, const QString &directionKey) {
    if (!m_document->setStructure(node, typeKey, directionKey)) return;
    m_scene->autoLayout();
    m_document->flushPendingStep();  // un passo, con il ridisposto
    refreshStylePage();  // anche "Bilancia mappa", che vale solo su due lati
}

void MainWindow::reorderNode(MindNode *node, MindNode *newParent, int row, int side) {
    if (!m_document->moveNode(node, newParent, row, side)) return;
    m_scene->autoLayout();
    m_document->flushPendingStep();  // spostamento e ridisposizione in un solo passo
    m_scene->selectNode(node);
    refreshOutline();
}

void MainWindow::rotateVisual(MindNode *node) {
    if (!node || !m_document->rotateImage(node)) return;
    m_scene->updateNode(node);
    m_scene->autoLayout();
    m_document->flushPendingStep();  // rotazione e ridisposizione in un solo passo
    refreshOutline();
    refreshStylePage();
}

void MainWindow::showImageMenu(MindNode *node, const QPoint &screenPos) {
    if (!node || node->imagePath.isEmpty()) return;
    QMenu menu(this);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(m_outlineAddMenu->styleSheet() + " QMenu::item{padding-left:12px;}");
    QAction *rotate = menu.addAction(QIcon(QStringLiteral(":/assets/hugeicons/menu-rotate.svg")), tr("Rotate 90°"));
    menu.addSeparator();
    QAction *remove = menu.addAction(node->imageSticker ? tr("Remove sticker") : tr("Remove image"));
    remove->setShortcut(QKeySequence(QKeySequence::Delete));
    remove->setShortcutVisibleInContextMenu(true);
    const QString id = node->id;
    QAction *chosen = menu.exec(screenPos);
    MindNode *current = nodeById(id);
    if (!chosen || !current) return;
    if (chosen == remove) removeVisual(current);
    else if (chosen == rotate) rotateVisual(current);
}

// Richiesta dell'utente 2026-10-21: dopo una cancellazione resta attivo un fratello (il seguente, se
// no il precedente) del primo nodo cancellato; senza fratelli il genitore. Si sceglie prima, fra i
// nodi che restano.
MindNode *MainWindow::nodeAfterRemoval(const QList<MindNode *> &nodes) const {
    const auto removed = [&nodes](const MindNode *node) {
        for (; node; node = node->parent) if (nodes.contains(node)) return true;
        return false;
    };
    for (MindNode *node : nodes) {
        if (!node || !node->parent || node->summaryTopic || removed(node->parent)) continue;  // FEAT-052: not among the children
        const auto &siblings = node->parent->children;
        const auto it = std::find_if(siblings.cbegin(), siblings.cend(), [node](const auto &child) { return child.get() == node; });
        for (auto next = it + 1; next != siblings.cend(); ++next) if (!removed(next->get())) return next->get();
        for (auto previous = it; previous != siblings.cbegin();) { --previous; if (!removed(previous->get())) return previous->get(); }
        return node->parent;
    }
    return nullptr;
}

void MainWindow::selectAfterRemoval(MindNode *node) {
    if (!node) { updateInspector(nullptr); return; }
    m_scene->selectNode(node);
    updateInspector(node);
    if (m_outline) m_outline->setCurrentIndex(m_model->indexFromNode(node));
}

void MainWindow::removeSelected() {
    // Canc con l'immagine (o adesivo, formula) scelta: si toglie solo quella, non il nodo; con il
    // mouse su un marcatore si toglie il marcatore (richiesta dell'utente 2026-10-20).
    if (MindNode *image = m_scene->selectedImageNode()) { removeVisual(image); return; }
    if (const auto [node, marker] = m_scene->hoveredMarker(); node) { setMarker(node, Markers::seriesOf(marker), QString()); return; }
    if (const QStringList relations = m_scene->selectedRelationIds(); !relations.isEmpty()) {
        m_document->removeRelations(relations);  // FEAT-053; selected topics stay (one thing per Delete)
        return;
    }
    if (const QStringList boundaries = m_scene->selectedBoundaryIds(); !boundaries.isEmpty()) {
        if (m_document->removeBoundaries(boundaries)) { m_scene->autoLayout(); m_document->flushPendingStep(); }  // FEAT-052; its topics stay
        return;
    }
    const QList<MindNode *> selected = m_scene->selectedNodes();
    MindNode *next = nodeAfterRemoval(selected);
    if (m_document->removeNodes(selected)) { m_scene->autoLayout(); selectAfterRemoval(next); }
}
void MainWindow::arrangeSelected(Document::Arrange arrange) {
    const QList<MindNode *> selected = m_scene->selectedNodes();
    if (selected.size() != 1 || !m_document->arrange(selected.first(), arrange)) return;
    m_document->flushPendingStep();
    m_scene->refreshStacking();
}

void MainWindow::editSelected(bool cursorAtEnd) {
    auto *item = qgraphicsitem_cast<NodeItem *>(m_scene->focusItem());
    if (const QList<QGraphicsItem *> selected = m_scene->selectedItems(); !item && selected.size() == 1)
        item = qgraphicsitem_cast<NodeItem *>(selected.front());  // the one selected topic, without the focus
    if (!item) return;
    if (cursorAtEnd) item->beginEditingAtEnd();
    else item->beginEditing();
}

// FEAT-035: scelta del modello nel modale comune scuro; un clic su un modello lo sceglie e chiude.
bool MainWindow::chooseTemplate() {
    // FEAT-026 fase D: la galleria delle varianti (prima una scelta fra sette modelli, che restano in Home).
    VariantGallery gallery(this, QString(), tr("Create"));
    if (gallery.exec() != QDialog::Accepted) return false;
    applyTemplate(1, gallery.chosenVariant());
    return true;
}

void MainWindow::chooseSheetVariant(const QString &variantId) {
    if (variantId == m_model->sheetStyle.variant) return;
    // Un passo: tipo del foglio (gli spostamenti a mano si perdono, come cambiando tipo) e stile.
    const MapVariants::Variant *variant = MapVariants::find(variantId);
    if (!variant) return;
    m_document->setStructure(m_model->root(), MapStructure::typeKey(variant->type),
                             MapStructure::directionKey(MapStructure::directions(variant->type).first()));
    MapVariants::apply(*m_model, variant->id, false);
    m_scene->autoLayout();
    m_document->flushPendingStep();
    refreshStylePage();
}

void MainWindow::applyTemplate(int templateIndex, const QString &variant) {
    const QStringList rootTitles{tr("Central Topic"), tr("Central Topic"), tr("New project"), tr("Central Topic"),
                                 tr("Brainstorming"), tr("SWOT analysis"), tr("Action plan")};
    m_model->clear(rootTitles.value(templateIndex, tr("Central Topic")));
    if (templateIndex != 3) applyDefaultTheme();  // FEAT-036; il modello notte ha la sua palette
    auto *root = m_model->root();
    if (templateIndex != 0) {
        // "Argomento principale". I tre nuovi modelli partono da pochi campi guida semi vuoti.
        QStringList titles;
        if (templateIndex == 2) titles = {tr("Research"), tr("Planning"), tr("Development"), tr("Launch")};
        else if (templateIndex == 4) titles = {tr("Idea 1"), tr("Idea 2"), tr("Idea 3")};
        else if (templateIndex == 5) titles = {tr("Strong points"), tr("Weaknesses"), tr("Opportunities"), tr("Threats")};
        else if (templateIndex == 6) titles = {tr("Goal"), tr("Tasks"), tr("Next step")};
        else for (int i = 1; i <= 4; ++i) titles.append(MindMapModel::defaultTitle(root, i));
        // FEAT-031: i modelli seguono le regole della palette (centrale senza sfondo ne' bordo).
        // Solo il modello scuro tiene il centrale scuro e il testo bianco dei rami, scelti a mano,
        // e una palette propria (decisione dell'utente 2026-10-16).
        if (templateIndex == 3) {
            m_model->paletteId = QStringLiteral("modello-scuro");
            m_model->activePalette = {"#5146e5", "#8b5cf6", "#06b6d4", "#ec4899", "#f43f5e", "#f59e0b"};
            root->style.fill = QColor("#18213d");
            root->style.text = QColor("#f4f7ff");
            root->style.border = QColor("#7787ff");
        }
        for (int i = 0; i < titles.size(); ++i) {
            auto *node = m_model->addChild(root, titles[i]);
            if (templateIndex == 3) node->style.text = QColor("#ffffff");
        }
        m_model->refreshStyles();
    }
    MapVariants::apply(*m_model, variant, false);
    // 2026-10-14: il documento nuovo non è più legato al file di prima: si rilascia il blocco.
    m_scene->autoLayout(); updateInspector(root); m_scene->selectNode(root); m_document->startUntitled();
}

void MainWindow::newMap() {
    if (!maybeSave()) return;
    // FEAT-072 Q1: from Home the template modal, from the editor (Ctrl+N) a Mind Map at once.
    if (m_pages->currentWidget() == m_homePage) {
        if (!chooseTemplate()) return;
    } else {
        applyTemplate(1, MapVariants::all().first().id);
    }
    showEditor();
}

// 2026-10-14 (revisione di robustezza e prestazioni, richiesta dell'utente). Il contenuto dei
// fogli è nella forma piatta dello storico: il salvataggio automatico non riconverte più la
// mappa in JSON (prima toJson() sul thread dell'interfaccia a ogni salvataggio).




void MainWindow::addSheet() {
    commitPendingEdits();  // il testo in modifica entra nel foglio che si sta lasciando
    // FEAT-072 Q1: "+" adds a Mind Map at once; the type changes from the Map page.
    const QString variant = MapVariants::all().first().id;
    m_document->addSheet([&variant](MindMapModel &sheet) { MapVariants::apply(sheet, variant, true); },
                         [this] { m_scene->autoLayout(); });
    onSheetShown();
}


void MainWindow::renameSheet() {
    commitPendingEdits();
    const int index = m_document->currentSheet();
    const QString name = m_document->sheets().value(index).name;
    bool ok = false;
    const QString renamed = ModalDialog::askText(this, tr("Rename sheet"), tr("Name"), tr("Rename"), &ok, name);
    if (ok) m_document->renameSheet(index, renamed);  // an empty name keeps the old one
}

void MainWindow::duplicateSheet() {
    commitPendingEdits();  // the text being typed goes into the copy too
    const int index = m_document->currentSheet();
    const QString name = m_document->sheets().value(index).name;
    const Document::Result result = m_document->duplicateSheet(index);
    if (!result.ok) { ModalDialog::inform(this, tr("Error"), result.error); return; }
    onSheetShown();
    statusBar()->showMessage(tr("Sheet \"%1\" duplicated").arg(name), 3000);
}

void MainWindow::deleteSheet() {
    if (m_document->sheets().size() < 2) return;
    commitPendingEdits();
    const int index = m_document->currentSheet();
    const QString name = m_document->sheets().value(index).name;
    if (ModalDialog::ask(this, tr("Delete sheet"),
                         tr("Delete \"%1\" and all its topics? This cannot be undone.").arg(name),
                         tr("Delete")) != ModalDialog::Choice::Primary) return;
    const Document::Result result = m_document->removeSheet(index);
    if (!result.ok) { ModalDialog::inform(this, tr("Error"), result.error); return; }
    onSheetShown();
    refreshLinkStates();  // FEAT-027: links to the deleted topics turn grey
    statusBar()->showMessage(tr("Sheet \"%1\" deleted").arg(name), 3000);
}

void MainWindow::switchSheet(int index) {
    if (index < 0 || index >= m_document->sheets().size() || index == m_document->currentSheet()) return;
    commitPendingEdits();  // bug: il testo scritto e non confermato andava perso con il nodo
    const Document::Result result = m_document->switchSheet(index);
    if (!result.ok) {
        statusBar()->showMessage(tr("Cannot open the sheet: %1").arg(result.error), 3000);
        syncSheetSelect();  // la select torna al foglio attivo
        return;
    }
    onSheetShown();
}

// Dopo un cambio di foglio (anche aggiunto): pagina Mappa, outliner e pannello sul nuovo foglio.
void MainWindow::onSheetShown() {
    if (m_pitchPage) m_pitchPage->cancelEditing();  // FEAT-089: a template draft belongs to its sheet
    m_panelContext.setActiveSheet(currentSheetId());  // FEAT-017: pagina Mappa, nessun passo di storico
    refreshOutline();
    updateBottomBar();
    updateInspector(m_model->root());
    m_scene->selectNode(m_model->root());
    updateHistoryActions();
    updateMapSubtitle();
}

// Select dei fogli allineata al Document (elenco, nomi, foglio attivo), senza segnali.
void MainWindow::syncSheetSelect() {
    m_panelContext.setActiveSheet(currentSheetId());
    if (!m_sheetCombo) return;
    const QSignalBlocker blocker(m_sheetCombo);
    m_sheetCombo->clear();
    for (const Document::Sheet &sheet : m_document->sheets()) {
        m_sheetCombo->addItem(sheet.name, sheet.id);
        m_sheetCombo->setItemData(m_sheetCombo->count() - 1, sheet.name, Qt::ToolTipRole);
    }
    const int current = m_document->currentSheet();
    m_sheetCombo->setCurrentIndex(current);
    if (current >= 0 && current < m_document->sheets().size()) m_sheetCombo->setToolTip(m_document->sheets()[current].name);
}




void MainWindow::openMap() {
    if (!maybeSave()) return;
    const auto path = m_pickOpenFile ? m_pickOpenFile()
                                     : QFileDialog::getOpenFileName(this, tr("Open map"), {}, tr("Mentis (*.mentis);;Markdown (*.md)"));
    if (!path.isEmpty()) readFile(path);
}
bool MainWindow::saveMap() {
    const QString file = m_document->filePath();
    return file.isEmpty() ? saveMapAs() : writeFile(file, BmapStorage::SaveReason::Explicit);
}
bool MainWindow::saveMapAs() {
    const auto path = m_pickSaveFile ? m_pickSaveFile()
                                     : QFileDialog::getSaveFileName(this, tr("Save map"), m_document->filePath(), tr("Mentis (*.mentis)"));
    return path.isEmpty() ? false : writeFile(path.endsWith(".mentis") ? path : path + ".mentis");
}

bool MainWindow::writeFile(const QString &path, BmapStorage::SaveReason reason) {
    commitPendingEdits();  // con Ctrl+S durante la modifica si salvava il titolo vecchio
    const Document::Result result = m_document->save(path, reason);
    if (!result.ok) {
        if (result.fileLocked || reason != BmapStorage::SaveReason::Automatic) {
            ModalDialog::inform(this, tr("Save error"), result.error);  // FEAT-035: modale scuro
        } else {
            statusBar()->showMessage(tr("Autosave failed: %1").arg(result.error), 5000);
        }
        return false;
    }
    rememberRecent(path);
    return true;
}

bool MainWindow::readFile(const QString &path) {
    commitPendingEdits();
    // P8 (2026-10-13): il controllo di integrita' completo (legge tutto il file, lento con molte
    // immagini su dischi vecchi) solo dopo una chiusura anomala o con un giornale rimasto.
    const bool integrityCheck = m_uncleanPreviousExit || QFileInfo::exists(path + "-journal") || QFileInfo::exists(path + "-wal");
    // Il Markdown non ha posizioni: la mappa si dispone prima di diventare lo stato iniziale.
    const Document::Result result = m_document->open(path, integrityCheck, [this](bool nativeMap) {
        if (!nativeMap) m_scene->autoLayout();
    });
    if (!result.ok) {
        ModalDialog::inform(this, result.fileLocked ? tr("File already open") : tr("Error"), result.error);
        return false;
    }
    if (!m_document->filePath().isEmpty()) rememberRecent(m_document->filePath());
    showEditor();
    return true;
}

void MainWindow::importMarkdown() {
    // BUG-014: l'importazione sostituisce il documento (e azzera storico e copia di recupero), quindi
    // chiede di salvare le modifiche come openMap; prima chiamava readFile senza chiedere.
    if (!maybeSave()) return;
    const auto path = m_pickMarkdownFile ? m_pickMarkdownFile()
                                         : QFileDialog::getOpenFileName(this, tr("Import Markdown"), {}, tr("Markdown (*.md *.markdown)"));
    if (!path.isEmpty()) readFile(path);
}
void MainWindow::exportMarkdown() {
    if (exportGanttFile(GanttOffice::Format::Markdown)) return;
    const auto path = m_pickExportFile ? m_pickExportFile() : QFileDialog::getSaveFileName(this, tr("Export Markdown"), exportName() + QStringLiteral(".md"), tr("Markdown (*.md)")); if (path.isEmpty()) return;
    // FEAT-054 (2026-10-18): cartella con il .md e assets/; il nome scelto da' il nome della cartella.
    const Document::ExportResult result = m_document->exportMarkdown(Document::exportMarkdownPath(path));
    if (!result.ok) ModalDialog::inform(this, tr("Error"), result.error);
    else if (!result.skipped.isEmpty())
        statusBar()->showMessage(tr("Exported. Attachments not found, skipped: %1").arg(result.skipped.join(QStringLiteral(", "))), 6000);
}

void MainWindow::exportImage() {
    commitPendingEdits();
    ensureGanttView();  // the dialog offers the Gantt tab
    ExportDialog dialog(this, exportName(), !exportBranchId().isEmpty(), exportPages(), m_scene->originalImagePath,
                        m_ganttView->exportSource(), workspaceView() == WorkspaceView::Gantt);  // FEAT-083: the Gantt tab
    if (m_agentImagePreset) dialog.applyPreset(*std::exchange(m_agentImagePreset, std::nullopt));  // FEAT-045
    dialog.exec();
    if (!dialog.outcome().isEmpty()) statusBar()->showMessage(dialog.outcome(), 6000);
}

// FEAT-054 stage 3: "Export > PDF…" and "Print…".
void MainWindow::exportPdfOrPrint(bool print) {
    commitPendingEdits();
    ensureGanttView();
    // FEAT-083: the Gantt tab, chosen when the Gantt view is open.
    const PageDialog::GanttChoice gantt{m_ganttView->exportSource(), workspaceView() == WorkspaceView::Gantt};
    PageDialog dialog(this, print ? PageDialog::Mode::Print : PageDialog::Mode::Pdf, exportName(), !exportBranchId().isEmpty(),
                      exportPages(), m_scene->originalImagePath, print ? std::nullopt : std::optional(pitchSource()), gantt);
    if (m_agentPagePreset) dialog.applyPreset(*std::exchange(m_agentPagePreset, std::nullopt));  // FEAT-045
    dialog.exec();
    if (!dialog.outcome().isEmpty()) statusBar()->showMessage(dialog.outcome(), 6000);
}

// FEAT-063 stage C (user's request 2026-10-27): "Export > Pitch…" saves the presentation as a .pptx
// at once, with the sheet's theme and ratio; its PDF is in "Export > PDF…" (Pitch).
void MainWindow::exportPresentation() {
    commitPendingEdits();
    const QList<Pitch::Slide> slides = Pitch::slides(*m_model);
    if (slides.isEmpty()) {
        statusBar()->showMessage(tr("No slides: every slide of this sheet is hidden"), 4000);
        return;
    }
    const QString name = exportName();
    QString path = m_pickExportFile ? m_pickExportFile()
                                    : QFileDialog::getSaveFileName(this, tr("Export Pitch"), name + QStringLiteral(".pptx"),
                                                                   tr("PowerPoint presentation (*.pptx)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".pptx");
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const PageDialog::PitchSource pitch = pitchSource();
    const QString error = PitchExport::writePptx(path, pitch.slides, pitch.look, pitch.ratio, name);
    QGuiApplication::restoreOverrideCursor();
    if (!error.isEmpty()) ModalDialog::inform(this, tr("Error"), tr("The presentation could not be exported: %1").arg(error));
    else statusBar()->showMessage(tr("Presentation exported to %1").arg(QDir::toNativeSeparators(path)), 6000);
}

// FEAT-054 stage 4: "Export > Word…" / "Excel…" save every sheet at once (only the save dialog).
void MainWindow::exportOffice(Document::OfficeFormat format) {
    commitPendingEdits();
    const bool word = format == Document::OfficeFormat::Word;
    if (exportGanttFile(word ? GanttOffice::Format::Word : GanttOffice::Format::Excel)) return;
    const QString suffix = word ? QStringLiteral(".docx") : QStringLiteral(".xlsx");
    const QString name = exportName(true);  // every sheet: the document's name
    QString path = m_pickExportFile ? m_pickExportFile()
                                    : QFileDialog::getSaveFileName(this, word ? tr("Export Word") : tr("Export Excel"), name + suffix,
                                                                   word ? tr("Word document (*.docx)") : tr("Excel workbook (*.xlsx)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += suffix;
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const Document::ExportResult result = m_document->exportOffice(path, format, name);
    QGuiApplication::restoreOverrideCursor();
    if (!result.ok) ModalDialog::inform(this, tr("Error"), tr("The file could not be exported: %1").arg(result.error));
    else if (!result.skipped.isEmpty())
        statusBar()->showMessage(tr("Exported. Images not found, skipped: %1").arg(result.skipped.join(QStringLiteral(", "))), 6000);
    else statusBar()->showMessage(tr("Exported to %1").arg(QDir::toNativeSeparators(path)), 6000);
}

// FEAT-083 stage C: with tasks on the active sheet's Gantt, Word…, Excel… and Markdown… ask first
// what to export (OfficeExportDialog); the mind map goes on as before (false).
bool MainWindow::exportGanttFile(GanttOffice::Format format) {
    commitPendingEdits();
    ensureGanttView();
    const GanttExportSource gantt = m_ganttView->exportSource();
    if (gantt.rows.isEmpty()) return false;
    OfficeExportDialog dialog(this, format, gantt.scale, workspaceView() == WorkspaceView::Gantt);
    const bool accepted = m_chooseOfficeExport ? m_chooseOfficeExport(dialog) : dialog.exec() == QDialog::Accepted;
    if (!accepted) return true;
    if (!dialog.ganttChosen()) return false;
    QString suffix, caption, filter;
    switch (format) {
    case GanttOffice::Format::Word:
        suffix = QStringLiteral(".docx"), caption = tr("Export Word"), filter = tr("Word document (*.docx)");
        break;
    case GanttOffice::Format::Excel:
        suffix = QStringLiteral(".xlsx"), caption = tr("Export Excel"), filter = tr("Excel workbook (*.xlsx)");
        break;
    case GanttOffice::Format::Markdown:
        suffix = QStringLiteral(".md"), caption = tr("Export Markdown"), filter = tr("Markdown (*.md)");
        break;
    }
    const QString name = exportName();  // the active sheet
    QString path = m_pickExportFile ? m_pickExportFile() : QFileDialog::getSaveFileName(this, caption, name + suffix, filter);
    if (path.isEmpty()) return true;
    if (QFileInfo(path).suffix().isEmpty()) path += suffix;
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const QString error = GanttOffice::write(format, path, gantt, dialog.content(), dialog.unit(), name);
    QGuiApplication::restoreOverrideCursor();
    if (!error.isEmpty()) ModalDialog::inform(this, tr("Error"), tr("The file could not be exported: %1").arg(error));
    else statusBar()->showMessage(tr("Exported to %1").arg(QDir::toNativeSeparators(path)), 6000);
    return true;
}

PageDialog::PitchSource MainWindow::pitchSource() const {
    return {Pitch::slides(*m_model), Pitch::sheetLook(*m_model),
            PitchExport::fileRatio(Pitch::aspectFromKey(m_model->pitchAspect))};
}

// The suggested name: "20261031T113911 Sheet 1" (MapExporter::defaultFileName, user 2026-10-31); the
// document's name for the exports of every sheet (Word, Excel).
QString MainWindow::exportName(bool wholeDocument) const {
    const QString document = m_document->filePath().isEmpty() ? QString() : QFileInfo(m_document->filePath()).completeBaseName();
    const QString sheet = m_document->sheets().value(m_document->currentSheet()).name;
    return MapExporter::defaultFileName(wholeDocument || sheet.isEmpty() ? document : sheet, QDateTime::currentDateTime());
}

QString MainWindow::exportBranchId() const {
    const QList<MindNode *> selected = m_scene->selectedNodes();
    return selected.size() == 1 ? selected.front()->id : QString();
}

ExportDialog::PagesFor MainWindow::exportPages() {
    const QString branchId = exportBranchId();
    // The active sheet from the model (it has the latest positions), the others from the document.
    return [this, branchId](ExportDialog::Scope scope) {
        QList<MapExporter::Page> pages;
        const int current = m_document->currentSheet();
        const QVector<Document::Sheet> &sheets = m_document->sheets();
        if (scope != ExportDialog::Scope::AllSheets) {
            pages.append({sheets.value(current).name, m_model->toFlatSnapshot(),
                          scope == ExportDialog::Scope::Branch ? branchId : QString()});
            return pages;
        }
        for (int i = 0; i < sheets.size(); ++i) {
            if (i == current) {
                pages.append({sheets[i].name, m_model->toFlatSnapshot(), {}});
                continue;
            }
            const Document::Result loaded = m_document->ensureSheetLoaded(i);
            // An unreadable sheet stays empty: MapExporter names it in its error.
            pages.append({sheets[i].name, loaded.ok ? m_document->sheetContent(i) : MapHistory::Snapshot(), {}});
        }
        return pages;
    };
}

// Richiesta dell'utente 2026-10-20 (rivista lo stesso giorno, "una via di mezzo"): un'immagine
// nuova (non un adesivo, che resta piccolo) e' larga al massimo quanto il posto del nodo, ma almeno kMinInsertWidthPx, in
// proporzione; piu' piccolo resta alla sua misura. Poi si cambia con le maniglie o dalla pagina
// Stile; "Ripristina dimensione" resta la misura reale del file (FEAT-040).
QSizeF MainWindow::fittedImageSize(const MindNode *node, const QString &path) const {
    const QSizeF natural = NodeItem::insertionImageSize(path);
    const qreal room = std::max<qreal>(std::floor(m_scene->imageRoomWidth(node)), kMinInsertWidthPx);
    if (natural.width() <= room) return natural;
    const qreal width = room;
    return {width, std::max<qreal>(1, std::round(natural.height() * width / natural.width()))};
}

void MainWindow::chooseImage() {
    if (!m_selected) return;
    // FEAT-039: solo i formati che Qt sa leggere qui, poi il controllo del contenuto.
    const QString path = m_pickImageFile ? m_pickImageFile()
                                         : QFileDialog::getOpenFileName(this, tr("Choose image"), {}, FileValidation::imageFilter());
    if (path.isEmpty()) return;
    setNodeImage(m_selected, path);
}

// Inserisci > Immagine..., and FEAT-094 Ctrl+V of an image: the file is checked, then shown on the node.
bool MainWindow::setNodeImage(MindNode *node, const QString &path) {
    if (!node) return false;
    const FileValidation::Check check = FileValidation::image(path);
    if (!acceptFile(path, check.ok, check.reason, check.large, check.largeReason)) return false;
    // FEAT-037: misura reale in pixel, ridotta in proporzione oltre 3.000 px (decisione 2026-10-16);
    // l'originale resta intero nel file. La stessa misura serve a "Ripristina dimensione" (FEAT-040).
    m_document->setImage(node, path, fittedImageSize(node, path));
    // Una larghezza scelta a mano piu' stretta del nuovo contenuto diventa quella necessaria.
    if (node->customWidth > 0) {
        const qreal needed = m_scene->nodeWidth(node);
        if (needed > node->customWidth) { node->customWidth = std::min(std::ceil(needed), kMaxManualNodeWidthPx); m_scene->updateNode(node); }
    }
    m_scene->autoLayout();
    m_scene->selectNode(node);
    return true;
}
void MainWindow::beginRelation() {
    // FEAT-052: a selected boundary can be the start too.
    const QStringList boundaries = m_scene->selectedBoundaryIds();
    auto *node = m_scene->selectedNode();
    if (!node && boundaries.size() != 1) { addFloatingNode(); return; }
    m_relationSource = node ? node->id : boundaries.front();
    m_cancelRelationAction->setEnabled(true);  // B4: da qui Esc annulla
    statusBar()->showMessage(tr("Relationship: now click the target node. Press Esc to cancel."));
}

void MainWindow::addFloatingNode() {
    auto *node = m_document->addFloatingTopic(m_view->mapToScene(m_view->viewport()->rect().center()), tr("Floating Topic"));
    m_scene->selectNode(node); updateInspector(node);
}

void MainWindow::startFloatingPlacement() {
    cancelFloatingPlacement();
    // Anteprima: l'argomento vero, disegnato con i colori che avra'. Si crea nel modello con i segnali
    // bloccati (nessun passo, nessuna vista avvisata), si disegna e si toglie: il modello torna com'era.
    QPixmap preview;
    QPointF offset;
    {
        const QSignalBlocker block(m_model);
        MindNode *node = m_model->addChild(m_model->root(), tr("Floating Topic"));
        node->floating = true;
        m_model->refreshStyles();
        NodeItem item(node);
        item.syncFromNode();
        preview = item.dragPreview(2.0);
        offset = item.boundingRect().topLeft();
        m_model->removeNode(node, false);
        m_model->refreshStyles();
    }
    m_placingFloating = new QGraphicsPixmapItem(preview);
    m_placingFloating->setOffset(offset);
    m_placingFloating->setOpacity(0.8);
    m_placingFloating->setZValue(1000);
    m_placingFloating->setAcceptedMouseButtons(Qt::NoButton);
    m_scene->addItem(m_placingFloating);
    m_placingFloating->setPos(m_view->mapToScene(m_view->viewport()->mapFromGlobal(QCursor::pos())));
    m_view->viewport()->setMouseTracking(true);
    m_view->setFocus();
    statusBar()->showMessage(tr("Click on the map to place the floating topic. Press Esc to cancel."));
}

void MainWindow::cancelFloatingPlacement() {
    if (!m_placingFloating) return;
    delete std::exchange(m_placingFloating, nullptr);
    statusBar()->clearMessage();
}

void MainWindow::addSummary() {
    const auto nodes = m_scene->selectedNodes(); if (nodes.isEmpty()) { statusBar()->showMessage(tr("Select a node to add the summary"), 2500); return; }
    // FEAT-052: one summary per parent and side, over the run of siblings, like the boundaries.
    const QList<MindNode *> created = m_document->addSummaries(nodes, [this](const MindNode *node) { return m_scene->sideOf(node); });
    if (created.isEmpty()) {
        const bool onlyCentral = std::all_of(nodes.cbegin(), nodes.cend(), [](const MindNode *node) { return !node->parent; });
        const bool table = std::any_of(nodes.cbegin(), nodes.cend(), [](const MindNode *node) {
            return node->parent && (MapStructure::typeOf(node->parent) == MapStructure::Type::TreeTable || MapStructure::insideTreeTable(node));
        });
        statusBar()->showMessage(onlyCentral ? tr("The central topic cannot have a summary")
                                 : table     ? tr("Tree tables cannot have summaries")
                                             : tr("These topics already have a summary"), 2500);
        return;
    }
    m_scene->autoLayout();
    m_document->flushPendingStep();
    m_scene->selectNode(created.front());
    editSelected();  // opens the new summary topic's title
}

void MainWindow::finishRelation(const QString &targetId) {
    const QString created = m_document->addRelation(std::exchange(m_relationSource, QString()), targetId);
    if (m_cancelRelationAction) m_cancelRelationAction->setEnabled(false);  // B4
    // FEAT-053: the new relationship is selected, with its label placeholder.
    if (!created.isEmpty()) m_scene->selectRelationAfterClick(created);
    statusBar()->showMessage(tr("Relationship created"), 2500);
}

void MainWindow::addBoundary() {
    const auto nodes = m_scene->selectedNodes(); if (nodes.isEmpty()) { statusBar()->showMessage(tr("Select one or more nodes (Ctrl+click)"), 2500); return; }
    // FEAT-052: one boundary per parent and side, over the run of siblings.
    const QStringList created = m_document->addBoundaries(nodes, [this](const MindNode *node) { return m_scene->sideOf(node); });
    if (created.isEmpty()) {
        const bool onlyCentral = std::all_of(nodes.cbegin(), nodes.cend(), [](const MindNode *node) { return !node->parent; });
        const bool table = std::any_of(nodes.cbegin(), nodes.cend(), [](const MindNode *node) { return MapStructure::insideTreeTable(node); });
        statusBar()->showMessage(onlyCentral ? tr("The central topic cannot have a boundary")
                                 : table     ? tr("Tree tables cannot have boundaries")
                                             : tr("These topics already have a boundary"), 2500);
        return;
    }
    m_scene->autoLayout();
    m_document->flushPendingStep();
    m_scene->selectBoundary(created.front());
}

// FEAT-064: formula del nodo nel modale con l'anteprima (anche per modificarla, doppio clic).
void MainWindow::insertFormula() { editFormula(m_selected); }

void MainWindow::editFormula(MindNode *node) {
    if (!node) return;
    const QString id = node->id;
    FormulaDialog dialog(this, node->formula, node->formulaBackground);
    if (dialog.exec() != QDialog::Accepted) return;
    MindNode *current = nodeById(id);
    if (!current) return;
    // Formula nuova o cambiata: la misura riparte da quella naturale; stessa formula: resta quella scelta.
    QSizeF size;
    if (dialog.code() != current->formula) {
        MindNode probe;
        probe.formula = dialog.code();
        probe.fontFamily = current->fontFamily; probe.fontSize = current->fontSize; probe.fontWeight = current->fontWeight;
        probe.parent = current->parent;
        size = NodeItem::formulaInsertionSize(&probe);
    }
    if (!m_document->setFormula(current, dialog.code(), dialog.background(), size)) return;
    m_scene->updateNode(current);
    m_scene->autoLayout();
    m_document->flushPendingStep();  // formula e ridisposizione in un solo passo
    m_scene->selectNode(current);
    refreshOutline();
}

// Tasto destro sulla formula: modifica, copia del codice o dell'immagine, rimozione.
void MainWindow::showFormulaMenu(MindNode *node, const QPoint &screenPos) {
    if (!node || node->formula.isEmpty()) return;
    QMenu menu(this);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(m_outlineAddMenu->styleSheet() + " QMenu::item{padding-left:12px;}");
    QAction *edit = menu.addAction(tr("Edit formula…"));
    QAction *copyCode = menu.addAction(tr("Copy LaTeX code"));
    QAction *copyImage = menu.addAction(tr("Copy as image"));
    QAction *rotate = menu.addAction(QIcon(QStringLiteral(":/assets/hugeicons/menu-rotate.svg")), tr("Rotate 90°"));
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Remove formula"));
    remove->setShortcut(QKeySequence(QKeySequence::Delete));
    remove->setShortcutVisibleInContextMenu(true);
    const QString id = node->id;
    QAction *chosen = menu.exec(screenPos);
    MindNode *current = nodeById(id);
    if (!chosen || !current) return;
    if (chosen == edit) editFormula(current);
    else if (chosen == rotate) rotateVisual(current);
    else if (chosen == copyCode) QGuiApplication::clipboard()->setText(current->formula);
    else if (chosen == copyImage) {
        // Come nel nodo (sfondo e colore), alla misura del nodo e a doppia risoluzione.
        const QColor text = FormulaText::textColorOn(current->formulaBackground, current->text);
        QImage image = FormulaText::boxImage(current->formula, NodeItem::titleFont(current), current->formulaBackground, text,
                                             current->imageSize, 2.0);
        image.setDevicePixelRatio(1.0);
        QGuiApplication::clipboard()->setImage(image);
    } else if (chosen == remove && m_document->setFormula(current, QString())) {
        m_scene->updateNode(current);
        m_scene->autoLayout();
        m_document->flushPendingStep();
        refreshOutline();
    }
}

bool MainWindow::acceptFile(const QString &path, bool ok, const QString &reason, bool large, const QString &largeReason) {
    const QString name = QFileInfo(path).fileName();
    if (!ok) {
        ModalDialog::inform(this, tr("File not inserted"), tr("\"%1\" was not inserted. %2").arg(name, reason));
        return false;
    }
    if (!large) return true;
    return ModalDialog::ask(this, tr("Large file"), tr("\"%1\": %2 Insert it anyway?").arg(name, largeReason), tr("Insert"))
           == ModalDialog::Choice::Primary;
}

// ---------------------------------------------------------------- FEAT-051: adesivi e marcatori

// Colonna del pannello: un bottone chiude la sua pagina se e' gia' quella aperta, altrimenti la
// mostra (aprendo la colonna se serve).
void MainWindow::toggleDockPage(int index) {
    ensureInspector();
    if (m_inspectorDock->isVisible() && m_dockPages->currentIndex() == index) { toggleInspector(); return; }
    m_dockPages->setCurrentIndex(index);
    if (!m_inspectorDock->isVisible()) toggleInspector();
    if (index == 1) updateIconPanelSelection();
    updatePanelToggles();
}

void MainWindow::updatePanelToggles() {
    const bool visible = m_inspectorDock && m_inspectorDock->isVisible() && m_dockPages;
    if (auto *panel = findChild<QToolButton *>(QStringLiteral("PanelToggle"))) panel->setChecked(visible && m_dockPages->currentIndex() == 0);
    if (auto *icons = findChild<QToolButton *>(QStringLiteral("IconPanelToggle"))) icons->setChecked(visible && m_dockPages->currentIndex() == 1);
}

IconPage *MainWindow::showIconPanel(int page) {
    ensureInspector();
    if (!isIconPanelShown()) toggleDockPage(1);
    m_iconPanel->setCurrentPage(page);
    updateIconPanelSelection();
    return iconPage(page);
}

IconPage *MainWindow::iconPage(int page) const { return page == 0 ? m_markerPage : m_stickerPage; }

bool MainWindow::isIconPanelShown() const {
    return m_inspectorDock && m_inspectorDock->isVisible() && m_dockPages && m_dockPages->currentIndex() == 1;
}

// Nodes the panel's choice goes to: the selected ones (map or outliner); none = nothing
// (user's request 2026-10-24: with several selected topics a click goes to all of them).
QList<MindNode *> MainWindow::iconTargetNodes() const {
    if (m_workspacePages && m_workspacePages->currentWidget() == m_outlinePage) return outlineSelectedNodes();
    return m_scene->selectedNodes();
}

// Markers lit in the panel: those every target node has.
void MainWindow::updateIconPanelSelection() {
    if (!m_markerPage) return;
    const QList<MindNode *> nodes = iconTargetNodes();
    QStringList common = nodes.isEmpty() ? QStringList() : nodes.front()->markers;
    for (const MindNode *node : nodes)
        common.removeIf([node](const QString &marker) { return !node->markers.contains(marker); });
    m_markerPage->setActiveMarkers(common);
}

// Adesivo: nel posto dell'immagine. Marcatore: si mette o, se c'e' gia', si toglie (uno per serie;
// per i propri l'immagine entra nel documento). With several nodes the marker is removed only when
// all of them have it, otherwise it goes to all; one step either way.
void MainWindow::chooseIcon(IconPage *page, const QString &path, const QString &name, const QString &collectionId) {
    const QList<MindNode *> nodes = iconTargetNodes();
    if (nodes.isEmpty()) return;
    if (page->kind() == IconLibrary::Kind::Sticker) { insertSticker(nodes, path); return; }
    QByteArray data;
    const QString marker = IconPage::markerFor({path, name}, collectionId, &data);
    if (marker.isEmpty() || !Markers::isValid(marker)) return;
    if (Markers::isCustom(marker)) m_model->addMarkerImage(Markers::customImage(marker), data);
    const bool everyHasIt = std::all_of(nodes.begin(), nodes.end(), [&marker](const MindNode *node) { return node->markers.contains(marker); });
    bool changed = false;
    for (MindNode *node : nodes)
        if (m_document->setMarker(node, Markers::seriesOf(marker), everyHasIt ? QString() : marker)) { m_scene->updateNode(node); changed = true; }
    if (!changed) return;
    m_scene->autoLayout();
    m_document->flushPendingStep();  // marcatori e ridisposizione in un solo passo
    refreshOutline();
    updateIconPanelSelection();
}

bool MainWindow::insertSticker(MindNode *node, const QString &path) {
    if (!insertSticker(QList<MindNode *>{node}, path)) return false;
    m_scene->selectNode(node);
    return true;
}

bool MainWindow::insertSticker(const QList<MindNode *> &nodes, const QString &path) {
    if (nodes.isEmpty() || nodes.contains(nullptr) || path.isEmpty()) return false;
    const FileValidation::Check check = FileValidation::image(path);  // FEAT-039
    if (!acceptFile(path, check.ok, check.reason, check.large, check.largeReason)) return false;
    const QSizeF size = Stickers::insertionSize(path);  // piccolo (decisione 2026-10-20)
    bool changed = false;
    for (MindNode *node : nodes)
        if (m_document->setSticker(node, path, size)) { m_scene->updateNode(node); changed = true; }
    if (!changed) return false;
    m_scene->autoLayout();
    m_document->flushPendingStep();  // adesivi e ridisposizione in un solo passo
    refreshOutline();
    return true;
}

namespace {
QIcon markerIcon(const QString &marker) {
    QPixmap pixmap(40, 40);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    Markers::paint(&painter, QRectF(1, 1, 18, 18), marker);
    painter.end();
    return QIcon(pixmap);
}
}  // namespace

void MainWindow::setMarker(MindNode *node, const QString &series, const QString &marker) {
    if (!node || !m_document->setMarker(node, series, marker)) return;
    m_scene->updateNode(node);
    m_scene->autoLayout();
    m_document->flushPendingStep();  // marcatore e ridisposizione in un solo passo
    refreshOutline();
    updateIconPanelSelection();
}

void MainWindow::showMarkerMenu(MindNode *node, const QString &marker, const QPoint &screenPos) {
    if (!node) return;
    QMenu menu(this);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(m_outlineAddMenu->styleSheet());
    const QString series = Markers::seriesOf(marker);
    const QString id = node->id;
    for (const Markers::Series &s : Markers::series()) {
        if (s.id != series) continue;
        for (const QString &value : s.values) {
            const QString option = series + QLatin1Char(':') + value;
            QAction *action = menu.addAction(markerIcon(option), Markers::label(option));
            action->setCheckable(true);
            action->setChecked(option == marker);
            connect(action, &QAction::triggered, this, [this, id, series, option] {
                if (MindNode *current = nodeById(id)) setMarker(current, series, option);
            });
        }
    }
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Remove marker"));
    remove->setShortcut(QKeySequence(QKeySequence::Delete));  // anche Canc con il mouse sul marcatore
    remove->setShortcutVisibleInContextMenu(true);
    connect(remove, &QAction::triggered, this, [this, id, series] {
        if (MindNode *current = nodeById(id)) setMarker(current, series, QString());
    });
    menu.exec(screenPos);
}

// ---------------------------------------------------------------- FEAT-049: file allegati

bool MainWindow::isRiskyFile(const QString &name) {
    static const QStringList risky{QStringLiteral("exe"), QStringLiteral("bat"), QStringLiteral("cmd"), QStringLiteral("com"),
                                   QStringLiteral("ps1"), QStringLiteral("psm1"), QStringLiteral("vbs"), QStringLiteral("vbe"),
                                   QStringLiteral("js"), QStringLiteral("jse"), QStringLiteral("wsf"), QStringLiteral("msi"),
                                   QStringLiteral("msp"), QStringLiteral("scr"), QStringLiteral("jar"), QStringLiteral("lnk"),
                                   QStringLiteral("reg"), QStringLiteral("hta"), QStringLiteral("cpl"), QStringLiteral("sh"),
                                   QStringLiteral("app"), QStringLiteral("dmg"), QStringLiteral("pkg"), QStringLiteral("appimage")};
    return risky.contains(QFileInfo(name).suffix().toLower());
}

bool MainWindow::attachFiles(MindNode *node, const QStringList &paths) {
    if (!node) return false;
    QStringList accepted;
    for (const QString &path : paths) {
        const FileValidation::Check check = FileValidation::anyFile(path);  // FEAT-039
        if (acceptFile(path, check.ok, check.reason, check.large, check.largeReason)) accepted.append(path);
    }
    if (!m_document->addFiles(node, accepted)) return false;
    m_scene->updateNode(node);
    m_scene->autoLayout();
    m_document->flushPendingStep();  // file e ridisposizione in un solo passo
    refreshOutline();
    return true;
}

void MainWindow::chooseFilesToAttach(MindNode *node) {
    if (!node) return;
    const QStringList paths = m_pickAttachFiles ? m_pickAttachFiles() : QFileDialog::getOpenFileNames(this, tr("Attach files"));
    if (!paths.isEmpty()) attachFiles(node, paths);
}

// File trascinati da Esplora risorse: sopra un nodo si allegano; un'immagine sola, su un nodo senza
// immagine, diventa l'immagine del nodo (come Inserisci > Immagine...).
void MainWindow::dropFiles(const QStringList &paths, const QPointF &scenePos) {
    MindNode *node = m_scene->nodeAt(scenePos);
    if (!node) {
        statusBar()->showMessage(tr("Drop the files on a topic to attach them."), 4000);
        return;
    }
    if (paths.size() == 1 && node->imagePath.isEmpty() && FileValidation::image(paths.front()).ok) {
        const FileValidation::Check check = FileValidation::image(paths.front());
        if (!acceptFile(paths.front(), check.ok, check.reason, check.large, check.largeReason)) return;
        m_document->setImage(node, paths.front(), fittedImageSize(node, paths.front()));
        m_scene->autoLayout();
        m_scene->selectNode(node);
        return;
    }
    if (attachFiles(node, paths)) m_scene->selectNode(node);
}

// Elenco della graffetta: per ogni file Apri, Salva con nome..., Rimuovi; in fondo Aggiungi file...
void MainWindow::showFilesMenu(MindNode *node, const QPoint &screenPos) {
    if (!node) return;
    QMenu menu(this);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(m_outlineAddMenu->styleSheet() + " QMenu::item{padding-left:12px;}");
    const QString id = node->id;
    struct Choice { int index; int action; };
    QHash<QAction *, Choice> choices;
    const auto addActions = [&](QMenu *target, int index) {
        choices.insert(target->addAction(tr("Open")), {index, 0});
        choices.insert(target->addAction(tr("Save as…")), {index, 1});
        choices.insert(target->addAction(tr("Remove", "attachment")), {index, 2});
    };
    for (int i = 0; i < node->files.size(); ++i) {
        const NodeFile &file = node->files[i];
        const QString label = QStringLiteral("%1  (%2)").arg(file.name, QLocale().formattedDataSize(file.size));
        if (node->files.size() == 1) {
            menu.addAction(label)->setEnabled(false);  // intestazione: il file e' uno solo
            addActions(&menu, i);
        } else {
            QMenu *sub = menu.addMenu(label);
            sub->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
            sub->setAttribute(Qt::WA_TranslucentBackground);
            sub->setStyleSheet(menu.styleSheet());
            addActions(sub, i);
        }
    }
    menu.addSeparator();
    QAction *add = menu.addAction(tr("Add files…"));
    QAction *chosen = menu.exec(screenPos);
    MindNode *current = nodeById(id);
    if (!chosen || !current) return;
    if (chosen == add) { chooseFilesToAttach(current); return; }
    const Choice choice = choices.value(chosen, {-1, -1});
    if (choice.index < 0 || choice.index >= current->files.size()) return;
    if (choice.action == 0) openNodeFile(current, choice.index);
    else if (choice.action == 1) saveNodeFileAs(current, choice.index);
    else if (m_document->removeFile(current, choice.index)) {
        m_scene->updateNode(current);
        m_scene->autoLayout();
        m_document->flushPendingStep();
        refreshOutline();
    }
}

QString MainWindow::openNodeFile(MindNode *node, int index) {
    if (!node || index < 0 || index >= node->files.size()) return {};
    const NodeFile file = node->files[index];
    if (isRiskyFile(file.name) &&
        ModalDialog::ask(this, tr("Open program or script?"),
                         tr("\"%1\" is a program or a script: it can change your computer. Open it only if you trust where it comes from.").arg(file.name),
                         tr("Open")) != ModalDialog::Choice::Primary)
        return {};
    const QString source = m_document->fileSource(node, index);
    if (source.isEmpty()) {
        ModalDialog::inform(this, tr("File not available"), tr("\"%1\" cannot be read from the document.").arg(file.name));
        return {};
    }
    // Copia temporanea in una cartella propria (nome originale, per l'applicazione che la apre).
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/Mentis-files/")
                           + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString copy = folder + QLatin1Char('/') + file.name;
    if (!QDir().mkpath(folder) || !QFile::copy(source, copy)) {
        ModalDialog::inform(this, tr("File not available"), tr("\"%1\" cannot be copied to open it.").arg(file.name));
        return {};
    }
    QFile::setPermissions(copy, QFile::permissions(copy) | QFileDevice::WriteOwner);
    m_openedFiles.insert(copy, {node->id, file.assetId});
    m_openedFilesWatcher->addPath(copy);
    const QUrl url = QUrl::fromLocalFile(copy);
    if (!(m_openUrl ? m_openUrl(url) : QDesktopServices::openUrl(url)))
        statusBar()->showMessage(tr("No application opens \"%1\".").arg(file.name), 4000);
    return copy;
}

// Una copia aperta e' cambiata: si chiede se aggiornare l'allegato (un passo di storico).
void MainWindow::checkOpenedFiles() {
    if (m_askingOpenedFile) return;
    m_askingOpenedFile = true;
    const QStringList copies = m_openedFiles.keys();
    for (const QString &copy : copies) {
        // Alcuni programmi sostituiscono il file salvando: l'osservatore lo perde e va rimesso.
        if (QFileInfo(copy).isFile() && !m_openedFilesWatcher->files().contains(copy)) m_openedFilesWatcher->addPath(copy);
        OpenedFile &opened = m_openedFiles[copy];
        if (!QFileInfo(copy).isFile()) continue;
        QFile content(copy);
        if (!content.open(QIODevice::ReadOnly)) continue;  // ancora bloccato dall'altra applicazione
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&content);
        const QString now = QString::fromLatin1(hash.result().toHex());
        if (now == opened.assetId) continue;
        MindNode *node = nodeById(opened.nodeId);
        int index = -1;
        if (node)
            for (int i = 0; i < node->files.size(); ++i)
                if (node->files[i].assetId == opened.assetId) { index = i; break; }
        if (index < 0) { opened.assetId = now; continue; }  // allegato tolto o gia' sostituito
        const QString name = node->files[index].name;
        if (ModalDialog::ask(this, tr("File changed"),
                             tr("\"%1\" was changed in another program. Update the attachment in the map?").arg(name),
                             tr("Update")) == ModalDialog::Choice::Primary) {
            if (MindNode *again = nodeById(opened.nodeId); again && index < again->files.size() && m_document->replaceFile(again, index, copy)) {
                m_scene->updateNode(again);
                m_document->flushPendingStep();
                refreshOutline();
            }
        }
        opened.assetId = now;  // la stessa versione non si richiede due volte
    }
    m_askingOpenedFile = false;
}

void MainWindow::saveNodeFileAs(MindNode *node, int index) {
    if (!node || index < 0 || index >= node->files.size()) return;
    const QString name = node->files[index].name;
    const QString target = m_pickAttachmentSave ? m_pickAttachmentSave(name) : QFileDialog::getSaveFileName(this, tr("Save file as"), name);
    if (target.isEmpty()) return;
    const QString source = m_document->fileSource(node, index);
    QFile::remove(target);  // la finestra ha gia' chiesto se sostituirlo
    if (source.isEmpty() || !QFile::copy(source, target))
        ModalDialog::inform(this, tr("File not saved"), tr("\"%1\" could not be saved.").arg(name));
}

void MainWindow::chooseColor(const QString &part) { if (!m_selected) return; QColor current = part == "fill" ? m_selected->fill : part == "text" ? m_selected->text : m_selected->border; const auto color = QColorDialog::getColor(current, this); if (!color.isValid()) return; m_model->setStyle({m_selected}, part == "fill" ? StyleProperty::FillColor : part == "text" ? StyleProperty::TextColor : StyleProperty::BorderColor, color); }

// L'abilitazione dei controlli la decide il contesto del pannello (showPanelPage, FEAT-017).
void MainWindow::updateInspector(MindNode *node) { m_selected = node; if (!node || !m_titleEdit) return; m_titleEdit->setText(node->title); m_notesEdit->setPlainText(node->notes); m_imageWidth->setValue(node->imageSize.width()); m_imageHeight->setValue(node->imageSize.height()); m_branchWidth->setValue(node->branchWidth); m_branchStyle->setCurrentIndex(m_branchStyle->findData(static_cast<int>(node->branchStyle))); }
// B10 (2026-10-13): se il titolo cambia si svuota anche titleHtml, altrimenti il nodo mostrerebbe
// ancora il vecchio testo formattato (stessa regola di MindMapModel::setData). Oggi l'inspector è nascosto.
void MainWindow::applyInspector() {
    if (!m_selected) return;
    m_document->setTitle(m_selected, m_titleEdit->text().trimmed());
    m_document->setNotes(m_selected, m_notesEdit->toPlainText());
    m_document->setImageSize(m_selected, {m_imageWidth->value(), m_imageHeight->value()});
    m_model->setStyle({m_selected}, StyleProperty::BranchWidth, m_branchWidth->value());
    m_model->setStyle({m_selected}, StyleProperty::BranchStyle, m_branchStyle->currentData().toInt());
    m_scene->updateNode(m_selected); m_scene->selectNode(m_selected); refreshOutline(); setDirty();
}
QRect MainWindow::outlinePictureRect(const MindNode *node) const {
    return static_cast<OutlineTreeView *>(m_outline)->pictureRect(m_model->indexFromNode(const_cast<MindNode *>(node)));
}

QRect MainWindow::outlineMarkerRect(const MindNode *node, int marker) const {
    if (!node || marker < 0 || marker >= node->markers.size()) return {};
    return static_cast<OutlineTreeView *>(m_outline)->mapMarkerRect(m_model->indexFromNode(const_cast<MindNode *>(node)), marker);
}

QRect MainWindow::outlineIconRect(const MindNode *node, int icon) const {
    const auto icons = static_cast<OutlineTreeView *>(m_outline)->trailingIcons(m_model->indexFromNode(const_cast<MindNode *>(node)));
    return icon >= 0 && icon < icons.size() ? icons[icon].second : QRect();
}

bool MainWindow::outlinePictureSelected(const MindNode *node) const {
    return static_cast<OutlineTreeView *>(m_outline)->isPictureSelected(m_model->indexFromNode(const_cast<MindNode *>(node)));
}

MindNode *MainWindow::outlineSelectedNode() const {
    return m_outline ? m_model->nodeFromIndex(m_outline->currentIndex()) : nullptr;
}

void MainWindow::refreshOutline() {
    if (m_refreshing || !m_outline || !m_outlineRootEdit) return;
    m_refreshing = true;
    if (!m_outlineRootEdit->hasFocus()) loadRootTitle(static_cast<OutlineRootTitleEdit *>(m_outlineRootEdit), m_model->root());
    m_outline->viewport()->update();
    m_refreshing = false;
}

void MainWindow::addOutlineSibling() {
    auto *selected = outlineSelectedNode();
    auto *created = selected ? m_document->addSibling(selected) : m_document->addChild(nullptr);
    // BUG-016: senza disposizione il ramo nuovo restava sopra gli altri sulla mappa; come dalla
    // mappa, si dispone nello stesso passo di storico (map-layout-contract.md, "Nodi nuovi").
    m_scene->autoLayout();
    const QModelIndex index = m_model->indexFromNode(created);
    m_outline->setCurrentIndex(index);
    m_outline->scrollTo(index);
    m_outline->edit(index);
}

void MainWindow::indentOutlineNode() {
    auto *node = outlineSelectedNode();
    if (!m_document->indentNode(node)) return;
    const QModelIndex index = m_model->indexFromNode(node);
    m_outline->setCurrentIndex(index);
    m_outline->expand(index.parent());
    m_scene->autoLayout();
}

void MainWindow::outdentOutlineNode() {
    auto *node = outlineSelectedNode();
    if (!m_document->outdentNode(node)) return;
    const QModelIndex index = m_model->indexFromNode(node);
    m_outline->setCurrentIndex(index);
    m_outline->scrollTo(index);
    m_scene->autoLayout();
}

namespace {
// Formato degli appunti per un ramo dell'outliner (JSON del sottoalbero, come nei file .mentis).
const QString kOutlineMimeType = QStringLiteral("application/x-mentis-subtree");

}

void MainWindow::showOutlineContextMenu(const QPoint &pos) {
    const QModelIndex index = m_outline->indexAt(pos);
    if (!index.isValid()) return;  // il menu si apre solo su una voce
    // FEAT-065: tasto destro su marcatore, immagine o icone dopo il titolo: i menu della mappa.
    {
        auto *view = static_cast<OutlineTreeView *>(m_outline);
        MindNode *node = m_model->nodeFromIndex(index);
        const QPoint screen = m_outline->viewport()->mapToGlobal(pos);
        if (node && view->mapMarkerAt(index, pos) >= 0) { showMarkerMenu(node, node->markers.value(view->mapMarkerAt(index, pos)), screen); return; }
        if (node && view->pictureRect(index).contains(pos)) {
            if (node->formula.isEmpty()) showImageMenu(node, screen);
            else showFormulaMenu(node, screen);
            return;
        }
        if (const int icon = view->trailingIconAt(index, pos); node && icon >= 0) {
            const OutlineTreeView::TrailingIcon kind = view->trailingIcons(index)[icon].first;
            if (kind == OutlineTreeView::TrailingIcon::Files) showFilesMenu(node, screen);
            else showLinkMenu(node, kind == OutlineTreeView::TrailingIcon::TopicLink ? NodeItem::LinkKind::Topic : NodeItem::LinkKind::Web, screen);
            return;
        }
    }
    // FEAT-060: tasto destro su una pillola: menu dell'etichetta (Rimuovi, Evidenzia).
    if (auto *view = static_cast<OutlineTreeView *>(m_outline); view->labelAt(index, pos) >= 0) {
        if (MindNode *node = m_model->nodeFromIndex(index))
            showLabelMenu(node, node->labels.value(view->labelAt(index, pos)), m_outline->viewport()->mapToGlobal(pos));
        return;
    }
    // Tasto destro su una voce di una selezione multipla (2026-10-13): la selezione resta e il
    // menu offre solo "Cancella", che le elimina tutte.
    const bool multiple = static_cast<OutlineTreeView *>(m_outline)->hasMultipleSelection() &&
                          m_outline->selectionModel()->isRowSelected(index.row(), index.parent());
    if (!multiple) m_outline->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);

    QMenu menu(m_outline);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    // Stesso stile del menu "+" (AGENTS.md, Menu a comparsa) più separatori e voci disattivate.
    menu.setStyleSheet(UiTheme::qss(m_outlineAddMenu->styleSheet() +
                       " QMenu::separator{height:1px;background:@divider;margin:4px 8px;}"
                       " QMenu::item:disabled{color:#6b7280;}"
                       // Voci senza icona: il testo resta a 12 px dal bordo (il padding 20 del menu + serve solo con le icone).
                       " QMenu::item{padding-left:12px;}"));
    if (multiple) {
        auto *action = menu.addAction(tr("Delete"));
        action->setShortcuts(m_shortcuts->shortcuts("outlineDelete"));
        action->setShortcutVisibleInContextMenu(true);
        connect(action, &QAction::triggered, this, &MainWindow::deleteOutlineNode);
        auto *paste = menu.addAction(tr("Paste style"));  // FEAT-085: on every selected item
        paste->setShortcuts(m_shortcuts->shortcuts("pasteStyle"));
        paste->setShortcutVisibleInContextMenu(true);
        paste->setEnabled(m_copiedStyle.has_value());
        connect(paste, &QAction::triggered, this, &MainWindow::pasteStyle);
        menu.exec(m_outline->viewport()->mapToGlobal(pos));
        return;
    }
    menu.addMenu(m_outlineAddMenu);
    menu.addSeparator();
    // FEAT-041: commandId = comando dell'area Outliner del registro (vuoto = nessun tasto).
    const auto add = [&](const QString &text, const char *commandId, void (MainWindow::*slot)(), bool enabled = true) {
        auto *action = menu.addAction(text);
        if (commandId) action->setShortcuts(m_shortcuts->shortcuts(commandId));  // solo da mostrare: i tasti li gestisce eventFilter
        action->setShortcutVisibleInContextMenu(true);
        action->setEnabled(enabled);
        connect(action, &QAction::triggered, this, slot);
    };
    const bool canPaste = canPasteBranch();
    add(tr("Copy"), "outlineCopy", &MainWindow::copyOutlineNode);
    add(tr("Cut"), "outlineCut", &MainWindow::cutOutlineNode);
    add(tr("Paste"), "outlinePaste", &MainWindow::pasteOutlineNode, canPaste);
    add(tr("Duplicate"), "outlineDuplicate", &MainWindow::duplicateOutlineNode);
    menu.addSeparator();
    // Rientro possibile solo se c'è un fratello sopra; riduzione solo sotto il primo livello.
    add(tr("Indent"), "outlineIndent", &MainWindow::indentOutlineNode, index.row() > 0);
    add(tr("Outdent"), "outlineOutdent", &MainWindow::outdentOutlineNode, index.parent().isValid());
    menu.addSeparator();
    // Canc e non Backspace: qui Backspace riduce il rientro (comportamento esistente).
    add(tr("Delete"), "outlineDelete", &MainWindow::deleteOutlineNode);
    add(tr("Clear text"), nullptr, &MainWindow::clearOutlineNodeText);
    menu.addSeparator();
    add(tr("Copy style"), "copyStyle", &MainWindow::copyStyle);  // FEAT-085
    add(tr("Paste style"), "pasteStyle", &MainWindow::pasteStyle, m_copiedStyle.has_value());
    menu.exec(m_outline->viewport()->mapToGlobal(pos));
}

// The selected items, or the current one (FEAT-042, 2026-11-05: Copy takes them all).
QList<MindNode *> MainWindow::outlineSelectedNodes() const {
    QList<MindNode *> selected;
    for (const QModelIndex &index : m_outline->selectionModel()->selectedRows())
        if (auto *node = m_model->nodeFromIndex(index)) selected.append(node);
    if (selected.isEmpty())
        if (auto *node = outlineSelectedNode()) selected.append(node);
    return selected;
}

void MainWindow::copyOutlineNode() { copyBranches(outlineSelectedNodes()); }

void MainWindow::cutOutlineNode() {
    if (!outlineSelectedNode()) return;
    copyOutlineNode();
    deleteOutlineNode();
}

// FEAT-042 (user, 2026-11-05): copied branches and Markdown text (FEAT-055) go under the selected
// item, as its last children, as on the map (before, after the item at its level). Without a
// selection, at the end of the first level.
void MainWindow::pasteOutlineNode() {
    auto *selected = outlineSelectedNode();
    if (pasteContentOnto(selected)) {  // FEAT-094: image, files or address on the selected item
        if (MindNode *current = outlineSelectedNode()) m_outline->setCurrentIndex(m_model->indexFromNode(current));
        return;
    }
    MindNode *parent = selected ? selected : m_model->root();
    MindNode *created = pasteBranchesAt(parent, static_cast<int>(parent->children.size()));
    if (!created) return;
    const QModelIndex index = m_model->indexFromNode(created);
    m_outline->setCurrentIndex(index);
    m_outline->scrollTo(index);
    m_scene->autoLayout();
}

void MainWindow::duplicateOutlineNode() {
    auto *created = m_document->duplicateNode(outlineSelectedNode());
    if (!created) return;
    const QModelIndex index = m_model->indexFromNode(created);
    m_outline->setCurrentIndex(index);
    m_outline->scrollTo(index);
    m_scene->autoLayout();
}

void MainWindow::deleteOutlineNode() {
    // FEAT-065: con l'immagine scelta nell'outliner Canc toglie solo lei, come sulla mappa.
    if (auto *view = static_cast<OutlineTreeView *>(m_outline); view->selectedPicture().isValid()) {
        MindNode *node = m_model->nodeFromIndex(view->selectedPicture());
        view->clearPictureSelection();
        if (node) removeVisual(node);
        return;
    }
    // Selezione multipla (2026-10-13): si eliminano tutte le voci selezionate, con la stessa regola
    // di removeSelected() per la mappa (una voce dentro un ramo già selezionato sparisce con il ramo).
    QList<MindNode *> selected;
    for (const QModelIndex &index : m_outline->selectionModel()->selectedRows())
        if (auto *node = m_model->nodeFromIndex(index)) selected.append(node);
    if (selected.size() <= 1) selected = {outlineSelectedNode()};
    MindNode *next = nodeAfterRemoval(selected);
    if (m_document->removeNodes(selected)) { m_scene->autoLayout(); selectAfterRemoval(next); }
}

void MainWindow::clearOutlineNodeText() {
    const QModelIndex index = m_outline->currentIndex();
    if (!index.isValid()) return;
    // (B10, 2026-10-13: il titolo veniva svuotato nel modello prima di aprire il campo, quindi
    // con Esc restava vuoto. Ora si svuota solo il campo: Esc ripristina il titolo.)
    m_outline->edit(index);  // la voce resta, pronta per scrivere il nuovo testo
    if (auto *editor = dynamic_cast<OutlineTitleEditor *>(m_outline->indexWidget(index))) editor->setText(QString());
    else m_model->setData(index, QString(), Qt::EditRole);  // campo non aperto: comportamento precedente
}

// Gruppi flottanti nel tono dello sfondo visibile: la mappa o la pagina dell'outliner (2026-10-19).
// The background of the visible page: the map's, or the outliner's and the Gantt's own light or dark
// page (never the map's colour; user's photos 2026-10-27).
QColor MainWindow::pageColor() const {
    switch (workspaceView()) {
    case WorkspaceView::Outline: return outlineColors().page;
    case WorkspaceView::Gantt: return m_ganttView->pageColor();
    case WorkspaceView::Map: break;
    }
    return m_model->backdrop().color;
}

// Floating groups, the fade under the top bars and the window frame in the tone of the visible page.
void MainWindow::applyFloatingTone() {
    const QColor page = pageColor();
    FloatingUi::setBackdrop(page);
    for (QWidget *child : findChildren<QWidget *>())
        if (auto *fade = dynamic_cast<EditorTopFade *>(child)) fade->setColor(page);
    updateWindowFrame(m_inspectorDock && m_inspectorDock->isVisible());
}

void MainWindow::setOutlineMode(bool enabled) {
    setWorkspaceView(enabled ? WorkspaceView::Outline : WorkspaceView::Map);
}

MainWindow::WorkspaceView MainWindow::workspaceView() const {
    if (m_workspacePages && m_workspacePages->currentWidget() == m_outlinePage) return WorkspaceView::Outline;
    if (m_workspacePages && m_ganttView && m_workspacePages->currentWidget() == m_ganttView) return WorkspaceView::Gantt;
    return WorkspaceView::Map;
}

void MainWindow::setWorkspaceView(WorkspaceView view) {
    if (view == WorkspaceView::Gantt) ensureGanttView();
    setFormatPainter(false);  // FEAT-085
    if (workspaceView() == WorkspaceView::Gantt) m_ganttView->commitPendingEdit();
    const bool outline = view == WorkspaceView::Outline, gantt = view == WorkspaceView::Gantt;
    if (outline) refreshOutline();
    m_workspacePages->setCurrentWidget(outline ? m_outlinePage : gantt ? static_cast<QWidget *>(m_ganttView) : m_view);
    applyFloatingTone();
    if (m_inspectorDock) m_inspectorDock->hide();
    for (auto *button : m_mapToolbarButtons) button->setVisible(view == WorkspaceView::Map);
    for (auto *button : m_outlineToolbarButtons) button->setVisible(outline);
    for (const auto &id : {"addChild", "addSibling", "edit", "editAtEnd", "remove", "image"}) m_actions[id]->setEnabled(view == WorkspaceView::Map);
    if (m_outlineToggle) m_outlineToggle->setText(view == WorkspaceView::Map ? tr("Outliner") : tr("Mind map"));
    if (m_ganttToggle) m_ganttToggle->setText(gantt ? tr("Outliner") : tr("Gantt"));
    // Ogni pagina ha il proprio zoom: il selettore mostra quello della pagina visibile; il Gantt ha
    // anche la sua scala dei tempi (FEAT-078).
    if (m_zoomCombo) {
        const QSignalBlocker blocker(m_zoomCombo);
        const int percent = outline ? m_outlineZoom : gantt ? m_ganttView->chart()->zoomPercent() : m_view->zoomPercent();
        m_zoomCombo->setCurrentText(QString::number(percent) + "%");
    }
    if (m_ganttScale) m_ganttScale->setVisible(gantt);
    if (m_ganttGroup) m_ganttGroup->setVisible(gantt);
    if (outline) m_outline->setFocus();
    else if (gantt) m_ganttView->focusContent();
    else m_view->setFocus();
    if (outline || gantt) showTip(outline ? QStringLiteral("outliner") : QStringLiteral("gantt"));  // FEAT-093
}

void MainWindow::applyOutlineZoom(int percent) {
    // 50-300%: sotto il 50% il testo dell'outliner (18px) diventa illeggibile.
    m_outlineZoom = qBound(50, percent, 300);
    const qreal z = m_outlineZoom / 100.0;
    const auto px = [z](int value) { return qRound(value * z); };
    // Hover: nessuno sfondo (prima #edf1f6, tolto su richiesta il 2026-10-12); compaiono solo i
    // 6 pallini della maniglia, disegnati dal delegate per la riga sotto il mouse.
    m_outline->setStyleSheet(QString(R"(
        QTreeView { font-size:%1px; color:%7; background:transparent; border:0; outline:0; padding:%2px 0; }
        QTreeView::item { min-height:%3px; border-radius:%4px; padding:%5px %6px; }
        QTreeView::item:hover { background:transparent; }
        QTreeView::item:selected,
        QTreeView::item:selected:active,
        QTreeView::item:selected:!active { background:transparent; color:%7; }
        QTreeView::branch { background:transparent; image:none; }
        QTreeView::branch:selected,
        QTreeView::branch:selected:active,
        QTreeView::branch:selected:!active { background:transparent; image:none; }
    )").arg(px(UiTheme::fontPx(18))).arg(px(UiTheme::space(8))).arg(px(UiTheme::heightPx(40))).arg(px(4)).arg(px(4))
                                    .arg(px(UiTheme::space(12))).arg(outlineColors().text.name()));
    m_outline->setIndentation(px(36));
    static_cast<OutlineTreeView *>(m_outline)->setZoom(z);  // creato come OutlineTreeView in createDocks()
    // Campo QPlainTextEdit (2026-10-13): a sinistra 2 px e sopra e sotto 1 px in più, i margini
    // interni del testo di QLineEdit, così titolo ed elenco restano nella stessa posizione
    // (verificato al pixel a zoom 100%). Selezione con il colore delle voci
    // (Impostazioni > Selezione) al posto di quello di sistema.
    m_outlineRootEdit->setStyleSheet(QString("QPlainTextEdit{font-size:%1px;font-weight:800;color:%6;background:transparent;border:0;"
                                             "padding:%5px %2px %5px %3px;selection-background-color:%4;selection-color:#202630;}"
                                             " QPlainTextEdit:focus{background:%7;border-radius:4px;}")
                                         .arg(px(28)).arg(px(8)).arg(px(8) + 2).arg(NodeItem::selectionStyle().textSelectionColor.name()).arg(px(8) + 1)
                                         .arg(outlineColors().text.name(), outlineColors().rootFocus.name()));
    m_outlineRootEdit->ensurePolished();  // il font del QSS serve subito al rendering delle formule
    updateRootFormula();
    if (m_zoomCombo && m_workspacePages && m_workspacePages->currentWidget() == m_outlinePage) {
        const QSignalBlocker blocker(m_zoomCombo);
        m_zoomCombo->setCurrentText(QString::number(m_outlineZoom) + "%");
    }
}

void MainWindow::updateBottomBar() {
    if (!m_nodeCountLabel || !m_model || !m_model->root()) return;
    const auto countNodes = [](const MindNode *root) {
        int count = 0;
        std::function<void(const MindNode *)> visit = [&](const MindNode *node) {
            if (!node) return;
            ++count;
            for (const auto &child : node->children) visit(child.get());
        };
        visit(root);
        return count;
    };
    m_nodeCountLabel->setText(tr("Topics: %1").arg(countNodes(m_model->root())));
}

void MainWindow::updateWindowControlsForPanel(bool inPanel) {
    // 2026-10-14: il corpo è passato in styleWindowControls, che vale per qualunque serie di
    // controlli (barra dell'editor o intestazione del pannello).
    styleWindowControls(m_windowControls, inPanel);
}

// Stile chiaro (barra dell'editor) o scuro (intestazione del pannello) di una serie di controlli
// della finestra. Prima era il corpo di updateWindowControlsForPanel, solo per m_windowControls.
void MainWindow::styleWindowControls(QWidget *controls, bool inPanel) {
    if (!controls) return;
    const QString lightStyle = "QToolButton{background:transparent;border:0;border-radius:4px;padding:0;} QToolButton:hover{background:#dce3ea;} QToolButton:pressed{background:#cbd4df;}";
    const QString lightCloseStyle = "QToolButton{background:transparent;border:0;border-radius:4px;padding:0;} QToolButton:hover{background:#e5484d;} QToolButton:pressed{background:#c9343a;}";
    const QString darkStyle = UiTheme::qss(QStringLiteral("QToolButton{background:transparent;border:0;border-radius:4px;padding:0;} QToolButton:hover{background:@divider;} QToolButton:pressed{background:@surfaceSunken;}"));
    const QString darkCloseStyle = "QToolButton{background:transparent;border:0;border-radius:4px;padding:0;} QToolButton:hover{background:#e5484d;} QToolButton:pressed{background:#c9343a;}";
    for (auto *button : controls->findChildren<QToolButton *>()) {
        const QString kind = button->property("controlKind").toString();
        const QString suffix = inPanel ? "-light.svg" : ".svg";
        button->setIcon(QIcon(":/assets/hugeicons/window-" + kind + suffix));
        const bool close = kind == "close";
        button->setStyleSheet(inPanel ? (close ? darkCloseStyle : darkStyle)
                                      : (close ? lightCloseStyle : lightStyle));
    }
}

// Finestra Impostazioni (2026-10-13). Le scelte valgono per l'app, non per la singola mappa:
// si salvano in QSettings e non entrano nello storico Ctrl+Z. Da FEAT-035 non c'e' OK/Annulla:
// la finestra salva ogni scelta appena fatta e qui si ridisegna cio' che serve.
void MainWindow::showSettings(bool shortcutsPage) {
    SettingsDialog dialog(this, m_shortcuts);
    if (m_agentSettingsPage >= 0) dialog.showPage(std::exchange(m_agentSettingsPage, -1));  // FEAT-045
    connect(&dialog, &SettingsDialog::selectionStyleChanged, this, [this] {
        m_scene->refreshSelectionStyle();
        // Colore del testo selezionato (2026-10-13): il foglio di stile del titolo principale lo
        // contiene, quindi va rifatto; i campi delle voci lo leggono a ogni apertura.
        applyOutlineZoom(m_outlineZoom);
    });
    // Profilo di prestazioni (2026-10-14): vale subito, tranne i passi di storico in RAM.
    connect(&dialog, &SettingsDialog::performanceModeChanged, this, &MainWindow::applyPerformanceProfile);
    connect(&dialog, &SettingsDialog::agentsChanged, this, [this] { m_agentHost->setListening(McpMapTools::Access::load().enabled); });
    // FEAT-046: la lingua vale dal prossimo avvio (decisione dell'utente 2026-10-18).
    connect(&dialog, &SettingsDialog::languageChanged, this, [this] {
        statusBar()->showMessage(tr("Language changed: restart Mentis to apply it."), 5000);
    });
    connect(&dialog, &SettingsDialog::checkForUpdatesRequested, this, [this, &dialog] { updatePrompt()->checkNow(&dialog); });
    connect(updatePrompt(), &UpdatePrompt::checkFinished, &dialog, &SettingsDialog::refreshLastUpdateCheck);
    bool tourRequested = false;  // FEAT-093: the tour starts once the dialog is closed
    connect(&dialog, &SettingsDialog::tourRequested, this, [&tourRequested] { tourRequested = true; });
    if (shortcutsPage) dialog.showShortcutsPage();  // FEAT-041: "Scorciatoie…" apre questa scheda
    dialog.exec();
    if (tourRequested) tour()->start();
}

ManualWindow *MainWindow::manualWindow() const { return m_manual.data(); }

// FEAT-093: the tours know the window only through these: the practice map, the controls of their
// steps, where a topic is on the screen, the view or panel a step needs.
GuidedTour *MainWindow::tour() {
    if (m_tour) return m_tour;
    GuidedTour::Host host;
    host.window = this;
    host.model = [this] { return m_model; };
    host.nodeRect = [this](const MindNode *node) {
        if (m_pages->currentWidget() != m_editorPage || workspaceView() != WorkspaceView::Map) return QRect();
        const QRect inView = m_view->mapFromScene(m_scene->nodeSceneRect(node)).boundingRect();
        return QRect(m_view->viewport()->mapTo(this, inView.topLeft()), inView.size());
    };
    host.selection = [this] {
        QList<const MindNode *> nodes;
        for (const MindNode *node : m_scene->selectedNodes()) nodes << node;
        return nodes;
    };
    host.reveal = [this](const MindNode *node) {
        // After the panel the step opens has taken its room from the map.
        const QString id = node->id;
        QTimer::singleShot(0, this, [this, id] {
            if (MindNode *shown = m_model->nodeById(id)) m_view->ensureVisible(m_scene->nodeSceneRect(shown), 96, 96);
        });
    };
    host.floatingBars = [this](const QString &kind) {
        QList<QRect> bars;
        if (m_pages->currentWidget() != m_editorPage) return bars;
        const auto add = [&](const QWidget *widget) {
            if (widget && widget->isVisible()) bars << QRect(mapFromGlobal(widget->mapToGlobal(QPoint())), widget->size());
        };
        // On the map: the topic bar with its two + knobs, and the bar of the selected text, kept by the
        // scene at the size of the screen. That bar belongs to the topic being written, the one with the
        // focus: only its items are looked at, not the whole map's, 5 times a second.
        if (m_topicTools && kind == QLatin1String("topic")) {
            add(m_topicTools->bar());
            add(m_topicTools->childKnob());
            add(m_topicTools->siblingKnob());
        }
        if (kind != QLatin1String("text")) return bars;
        const QGraphicsItem *writing = m_scene->focusItem() ? m_scene->focusItem()->topLevelItem() : nullptr;
        for (QGraphicsItem *item : writing ? writing->childItems() : QList<QGraphicsItem *>())
            if (auto *proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(item);
                proxy && proxy->isVisible() && proxy->flags().testFlag(QGraphicsItem::ItemIgnoresTransformations)) {
                const QRect inView = proxy->deviceTransform(m_view->viewportTransform()).mapRect(proxy->rect()).toAlignedRect();
                bars << QRect(m_view->viewport()->mapTo(this, inView.topLeft()), inView.size());
            }
        // In the outliner: the bar of the selected text.
        add(findChild<QWidget *>(QStringLiteral("OutlineFormatBar"), Qt::FindDirectChildrenOnly));
        return bars;
    };
    host.control =[this](const QString &name) -> QWidget * {
        const auto toolbarGroup = [](const QList<QWidget *> &groups, int index) { return index < groups.size() ? groups.at(index) : nullptr; };
        if (name == QLatin1String("newMap")) return m_homePage->findChild<QPushButton *>(QStringLiteral("accent"));
        if (name == QLatin1String("mainMenu"))
            for (auto *button : m_editorPage->findChildren<QToolButton *>())
                if (button->property("mainMenuButton").toBool()) return button;
        if (name == QLatin1String("topicBar")) return m_topicTools ? m_topicTools->bar() : nullptr;
        if (name == QLatin1String("childKnob")) return m_topicTools ? m_topicTools->childKnob() : nullptr;
        if (name == QLatin1String("siblingKnob")) return m_topicTools ? m_topicTools->siblingKnob() : nullptr;
        if (name == QLatin1String("help")) return findChild<QToolButton *>(QStringLiteral("HelpButton"));
        if (name == QLatin1String("addSheet")) return findChild<QToolButton *>(QStringLiteral("AddSheetButton"));
        if (name == QLatin1String("topicGroup")) return toolbarGroup(m_mapToolbarButtons, 0);
        if (name == QLatin1String("structureGroup")) return toolbarGroup(m_mapToolbarButtons, 1);
        if (name == QLatin1String("panel")) return findChild<QToolButton *>(QStringLiteral("PanelToggle"));
        if (name == QLatin1String("panelPages") || name == QLatin1String("sidePanel") || name == QLatin1String("iconPanel")) ensureInspector();
        if (name == QLatin1String("panelPages")) return m_sidePanel ? m_sidePanel->selector() : nullptr;
        if (name == QLatin1String("sidePanel")) return m_sidePanel;
        if (name == QLatin1String("iconPanel")) return m_iconPanel;
        if (name == QLatin1String("iconToggle")) return findChild<QToolButton *>(QStringLiteral("IconPanelToggle"));
        if (name == QLatin1String("present")) return findChild<QToolButton *>(QStringLiteral("PresentButton"));
        if (name == QLatin1String("focusGroup")) return m_focusButton ? m_focusButton->parentWidget() : nullptr;
        if (name == QLatin1String("sheets")) return m_sheetCombo ? m_sheetCombo->parentWidget() : nullptr;
        if (name == QLatin1String("outlineToggle")) return m_outlineToggle;
        if (name == QLatin1String("ganttToggle")) return m_ganttToggle;
        if (name == QLatin1String("save")) return m_saveStateButton;
        if (name == QLatin1String("outline")) return m_outlinePage;
        if (name == QLatin1String("outlineGroup")) return toolbarGroup(m_outlineToolbarButtons, 0);
        if (name.startsWith(QLatin1String("gantt")) && name != QLatin1String("ganttGroup")) ensureGanttView();
        if (name == QLatin1String("gantt")) return m_ganttView;
        if (name == QLatin1String("ganttActions")) return m_ganttView->actionsRow();
        if (name == QLatin1String("ganttGroup")) return m_ganttGroup;
        if (name == QLatin1String("ganttPanel")) return m_ganttView->panel();
        if (name == QLatin1String("ganttHeader")) return m_ganttView->table()->header();
        if (name == QLatin1String("ganttScale")) return m_ganttScale;
        return nullptr;
    };
    host.prepare = [this](const QString &state) {
        if (m_pages->currentWidget() != m_editorPage) return;
        if (state == QLatin1String("map") && workspaceView() != WorkspaceView::Map) setWorkspaceView(WorkspaceView::Map);
        if (state == QLatin1String("outline") && workspaceView() != WorkspaceView::Outline) setWorkspaceView(WorkspaceView::Outline);
        const bool gantt = state == QLatin1String("gantt") || state == QLatin1String("ganttPanel");
        if (gantt && workspaceView() != WorkspaceView::Gantt) setWorkspaceView(WorkspaceView::Gantt);
        if (state == QLatin1String("ganttPanel") && !m_ganttView->panel()->isVisible()) m_ganttView->togglePanel();
        if (state == QLatin1String("stylePanel") && !(m_inspectorDock && m_inspectorDock->isVisible() && m_dockPages->currentIndex() == 0)) toggleDockPage(0);
        if (state == QLatin1String("iconPanel") && !isIconPanelShown()) toggleDockPage(1);
    };
    host.startPracticeMap = [this] {
        if (!maybeSave()) return;
        applyTemplate(0, MapVariants::all().first().id);
        showEditor();
    };
    host.keepMap = [this] { saveMapAs(); };
    host.discardMap = [this] {
        // A new untitled map has no changes: the Home opens without asking.
        applyTemplate(1, MapVariants::all().first().id);
        showHome();
    };
    host.shortcutAllowed = [this](QObject *receiver) {
        // The keys the steps ask for (Tab, Enter, F2, Space), with Delete, Undo, Redo and Save.
        for (const char *id : {"addChild", "addSibling", "edit", "editAtEnd", "remove", "undo", "redo", "save"})
            if (receiver == m_actions.value(QLatin1String(id))) return true;
        return false;
    };
    m_tour = new GuidedTour(std::move(host), this);
    return m_tour;
}

void MainWindow::offerTourAtFirstStart() {
    if (!TourSettings::tourOffered()) tour()->offer();
}

void MainWindow::showTip(const QString &key) {
    // The outliner and the Gantt have their own tour, offered the first time they open.
    if (key == QLatin1String("outliner") || key == QLatin1String("gantt")) {
        tour()->offerArea(key == QLatin1String("gantt") ? GuidedTour::Kind::Gantt : GuidedTour::Kind::Outliner);
        return;
    }
    if (key == QLatin1String("pitch"))
        Tips::offer(this, key, m_sidePanel, tr("**Pitch** makes the slides from the map, branch by branch.\n- this tab decides how each topic "
                                              "looks\n- **Present** (`F5`) shows them"), QStringLiteral("pitch/slides.md"));
}

void MainWindow::showManual(const QString &page) {
    if (!m_manual) {
        m_manual = new ManualWindow(this);
        m_manual->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_manual->setDark(NodeStyle::isDarkBackground(m_model->backdrop().color));  // like the outliner and the Gantt
    m_manual->openPage(page);
    m_manual->show();
    m_manual->raise();
    m_manual->activateWindow();
}

QString MainWindow::helpPage() const {
    if (m_pages->currentWidget() == m_homePage) return QStringLiteral("getting-started/home.md");
    const WorkspaceView view = workspaceView();
    if (view == WorkspaceView::Gantt) {
        GanttTaskPanel *panel = m_ganttView ? m_ganttView->panel() : nullptr;
        if (panel && panel->isVisible())
            return panel->page() == GanttTaskPanel::Page::Gantt ? QStringLiteral("gantt/gantt-style.md") : QStringLiteral("gantt/tasks.md");
        return QStringLiteral("gantt/gantt-view.md");
    }
    if (m_inspectorDock && m_inspectorDock->isVisible() && m_dockPages) {
        if (m_dockPages->currentIndex() == 1)
            return m_iconPanel && m_iconPanel->currentPage() == 1 ? QStringLiteral("topic-contents/stickers.md")
                                                                  : QStringLiteral("topic-contents/markers.md");
        if (m_sidePanel) switch (static_cast<PanelContext::Page>(m_sidePanel->currentPage())) {
            case PanelContext::Page::Pitch: return QStringLiteral("pitch/slides.md");
            case PanelContext::Page::Map: return QStringLiteral("style/map-style.md");
            case PanelContext::Page::Style: return QStringLiteral("style/topic-style.md");
        }
    }
    if (view == WorkspaceView::Outline) return QStringLiteral("outliner/outliner.md");
    for (QGraphicsItem *item : m_scene->selectedItems()) {
        if (dynamic_cast<RelationshipItem *>(item)) return QStringLiteral("map/relationships.md");
        if (dynamic_cast<BoundaryItem *>(item)) return QStringLiteral("map/boundaries.md");
    }
    const QList<MindNode *> nodes = m_scene->selectedNodes();
    if (nodes.size() == 1 && nodes.front()->summaryTopic) return QStringLiteral("map/summaries.md");
    return QStringLiteral("map/topics.md");
}

UpdatePrompt *MainWindow::updatePrompt() {
    if (!m_updatePrompt) m_updatePrompt = new UpdatePrompt(this);
    return m_updatePrompt;
}


bool MainWindow::maybeSave(bool storePreview) {
    // 2026-10-14: il testo in modifica entra nel modello prima del controllo, e la modifica in
    // attesa del timer diventa un passo di storico (altrimenti lo stato modificato poteva essere vecchio).
    commitPendingEdits();
    m_document->flushPendingStep();
    if (!m_document->isDirty()) {
        // Si lascia il documento così com'è nel file: l'anteprima della Home lo mostra aggiornato.
        if (storePreview) storeDocumentPreview();
        return true;
    }
    // FEAT-035: modale comune scuro (prima QMessageBox di sistema). 2026-11-05: X instead of "Cancel",
    // "Don't save" on the left and "Save" on the right (user's request).
    const auto choice = ModalDialog::askWithClose(this, tr("Unsaved changes"), tr("Do you want to save your changes?"),
                                                  tr("Save"), tr("Don't save"));
    return choice == ModalDialog::Choice::Secondary || (choice == ModalDialog::Choice::Primary && saveMap());
}
void MainWindow::setDirty() { m_document->markChanged(); }

// Stato "modificato" del documento: asterisco nel titolo, nome del file e stato del salvataggio.
void MainWindow::onDirtyChanged(bool dirty) {
    setWindowModified(dirty);
    // P4 (2026-10-13): il titolo si reimposta solo se cambia (succede a ogni movimento del mouse).
    const QString file = m_document->filePath();
    const QString title = QString("Mentis%1[*]").arg(file.isEmpty() ? "" : " — " + QFileInfo(file).fileName());
    if (windowTitle() != title) setWindowTitle(title);
    updateMapSubtitle();
}





void MainWindow::undo() {
    const Document::StepResult result = m_document->undo();
    // L'ultima modifica e' in un altro foglio: la select dei fogli lampeggia e un messaggio dice dove
    // andare; tornati su quel foglio si continua ad annullare (2026-10-14, richiesta dell'utente).
    if (result.outcome == Document::StepOutcome::OtherSheet) pointToSheet(result.sheetId, false);
    else if (result.outcome == Document::StepOutcome::Failed) statusBar()->showMessage(tr("Cannot restore the change: %1").arg(result.error), 3000);
    else if (result.outcome == Document::StepOutcome::Applied) statusBar()->showMessage(tr("Change undone"), 1500);
}

void MainWindow::redo() {
    const Document::StepResult result = m_document->redo();
    if (result.outcome == Document::StepOutcome::OtherSheet) pointToSheet(result.sheetId, true);
    else if (result.outcome == Document::StepOutcome::Failed) statusBar()->showMessage(tr("Cannot restore the change: %1").arg(result.error), 3000);
    else if (result.outcome == Document::StepOutcome::Applied) statusBar()->showMessage(tr("Change redone"), 1500);
}

// Interfaccia dopo un passo di storico applicato (il Document ignora intanto le modifiche).
void MainWindow::onHistoryRestored(bool incremental) {
    m_relationSource.clear();
    refreshOutline();
    MindNode *keep = nullptr;
    if (incremental && !m_selectedBeforeRestore.isEmpty()) keep = nodeById(m_selectedBeforeRestore);
    if (incremental) {
        m_scene->updateNode(m_model->root());
        m_scene->refreshTableCells();  // FEAT-028
        m_scene->refreshAnnotations();
    }
    updateInspector(keep ? keep : m_model->root());
    if (!keep) m_scene->selectNode(m_model->root());
    // FEAT-021: la larghezza mostrata e' misurata dalla scena, aggiornata qui sopra dopo i segnali
    // del modello (il nodo centrale con updateNode): si rilegge a scena pronta.
    refreshStylePage();
}


// --- FEAT-027: collegamenti ad argomenti e pagine web ---


MindNode *MainWindow::nodeById(const QString &id) const {
    std::function<MindNode *(MindNode *)> find = [&](MindNode *node) -> MindNode * {
        if (node->id == id) return node;
        for (const auto &child : node->children) if (MindNode *found = find(child.get())) return found;
        return nullptr;
    };
    return id.isEmpty() ? nullptr : find(m_model->root());
}

void MainWindow::editTopicLink(MindNode *node) {
    if (!node) return;
    commitPendingEdits();
    m_document->flushPendingStep();
    // Tutti i fogli nell'albero: quelli non ancora letti si leggono ora dal file.
    QList<TopicLinkDialog::SheetContent> sheets;
    const QVector<Document::Sheet> &documentSheets = m_document->sheets();
    for (int i = 0; i < documentSheets.size(); ++i) {
        if (i != m_document->currentSheet() && !m_document->ensureSheetLoaded(i).ok) continue;
        sheets.append({documentSheets[i].id, documentSheets[i].name, m_document->sheetContent(i)});
    }
    const QString sourceId = node->id;
    TopicLinkDialog dialog(this, sheets, sourceId, node->topicLink);
    if (dialog.exec() != QDialog::Accepted) return;
    node = nodeById(sourceId);  // il modale non cambia la mappa, ma il puntatore si rilegge
    const QString targetId = dialog.targetId(), targetSheet = dialog.targetSheetId();
    if (!node || targetId.isEmpty()) return;
    QList<MindMapModel::LinkChange> changes{{node, targetId, {}}};
    if (dialog.alsoBack()) {
        if (targetSheet == currentSheetId()) {
            if (MindNode *target = nodeById(targetId)) changes.append({target, sourceId, {}});
        } else if (sheetIndexOf(targetSheet) >= 0) {
            // Inverso in un altro foglio: il suo nuovo stato entra nello stesso passo (decisione
            // 2026-10-16: Ctrl+Z toglie entrambi i collegamenti).
            MapHistory::Snapshot other = m_document->historySnapshot(targetSheet);
            QJsonObject target = other.value(targetId);
            target.insert(QStringLiteral("topicLink"), sourceId);
            other.insert(targetId, target);
            m_pendingLinkSheet = std::pair<QString, MapHistory::Snapshot>{targetSheet, other};
        }
    }
    // Se il nodo di partenza era gia' collegato a quell'argomento resta da registrare l'inverso.
    if (!m_model->setLinks(changes) && m_pendingLinkSheet) { setDirty(); commitLinkStep(); refreshLinkStates(); }
    m_pendingLinkSheet.reset();
    statusBar()->showMessage(tr("Link added"), 2000);
}

void MainWindow::editWebLink(MindNode *node) {
    if (!node) return;
    commitPendingEdits();
    const QString sourceId = node->id;
    WebLinkDialog dialog(this, node->webLink);
    if (dialog.exec() != QDialog::Accepted || dialog.address().isEmpty()) return;
    if (MindNode *source = nodeById(sourceId)) m_model->setLinks({{source, {}, dialog.address()}});
}

void MainWindow::commitLinkStep() { m_document->commitLinkStep(std::exchange(m_pendingLinkSheet, std::nullopt)); }

void MainWindow::refreshLinkStates() {
    // Insieme degli id costruito solo se qualche nodo ha un collegamento ad argomento.
    QSet<QString> ids;
    bool built = false;
    bool unread = false;
    m_scene->refreshTopicLinkStates([&](const QString &targetId) {
        if (!built) {
            built = true;
            std::function<void(const MindNode *)> visit = [&](const MindNode *node) {
                ids.insert(node->id);
                for (const auto &child : node->children) visit(child.get());
            };
            visit(m_model->root());
            for (int i = 0; i < m_document->sheets().size(); ++i) {
                if (i == m_document->currentSheet()) continue;
                if (!m_document->sheets()[i].loaded) { unread = true; continue; }
                const MapHistory::Snapshot content = m_document->sheetContent(i);
                for (auto it = content.cbegin(); it != content.cend(); ++it) ids.insert(it.key());
            }
            m_seenNodeIds.unite(ids);
        }
        if (ids.contains(targetId)) return true;
        // Mai visto e con fogli ancora da leggere: non si puo' dire (resta normale fino al clic).
        return unread && !m_seenNodeIds.contains(targetId);
    });
}

bool MainWindow::revealNode(const QString &nodeId) {
    MindNode *node = nodeById(nodeId);
    for (int i = 0; !node && i < m_document->sheets().size(); ++i) {
        if (i == m_document->currentSheet()) continue;
        if (!m_document->ensureSheetLoaded(i).ok) continue;
        const QString id = m_document->sheets()[i].id;
        const MapHistory::Snapshot content = m_document->sheetContent(i);
        if (!content.contains(nodeId)) continue;
        switchSheet(i);
        if (currentSheetId() != id) return false;  // foglio non aperto
        node = nodeById(nodeId);
    }
    if (!node) return false;
    m_scene->selectNode(node);
    m_view->ensureVisible(m_scene->nodeSceneRect(node), 24, 24);
    // Ramo chiuso: nell'Outliner si aprono le voci fino al nodo (sulla mappa con FEAT-016).
    const QModelIndex index = m_model->indexFromNode(node);
    for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent()) m_outline->expand(parent);
    if (index.isValid()) m_outline->scrollTo(index);
    updateInspector(node);
    return true;
}

void MainWindow::followLink(MindNode *node, NodeItem::LinkKind kind) {
    if (!node) return;
    if (kind == NodeItem::LinkKind::Web) {
        const QUrl url(node->webLink);
        const bool opened = m_openUrl ? m_openUrl(url) : QDesktopServices::openUrl(url);
        if (!opened) statusBar()->showMessage(tr("Cannot open %1").arg(node->webLink), 3000);
        return;
    }
    // Dopo il clic (2026-10-19): un argomento in un altro foglio ricrea la scena; farlo durante il
    // clic sul bottone lasciava alla fine del clic la scena nuova, che poteva togliere la selezione
    // dal nodo di arrivo (la prova backLinkInAnotherSheetIsOneStep falliva a caso).
    const LinkPlace from{currentSheetId(), node->id};
    const QString target = node->topicLink;
    QTimer::singleShot(0, this, [this, from, target] {
        if (!revealNode(target)) {
            refreshLinkStates();
            statusBar()->showMessage(tr("The linked topic no longer exists"), 3000);
            return;
        }
        m_linkBack.append(from);
        m_linkForward.clear();
        refreshLinkStates();
    });
}

void MainWindow::navigateLinkHistory(bool back) {
    QList<LinkPlace> &from = back ? m_linkBack : m_linkForward;
    QList<LinkPlace> &to = back ? m_linkForward : m_linkBack;
    const MindNode *selected = m_scene->selectedNode();
    const LinkPlace here{currentSheetId(), selected ? selected->id : m_model->root()->id};
    // Un nodo della cronologia eliminato nel frattempo si salta.
    while (!from.isEmpty()) {
        const LinkPlace place = from.takeLast();
        if (revealNode(place.nodeId)) { to.append(here); return; }
    }
    statusBar()->showMessage(back ? tr("No previous link") : tr("No next link"), 2000);
}

// FEAT-059: rettangolo (globale) accanto al quale si apre la nota: il nodo sulla mappa o la riga
// dell'outliner, secondo cosa si vede.
QRect MainWindow::noteAnchor(const MindNode *node) const {
    if (m_workspacePages && m_workspacePages->currentWidget() == m_outlinePage) {
        // Il titolo (fino all'ultima lettera) e le pillole, non tutta la riga: il riquadro delle
        // etichette si apre accanto al testo (2026-10-19, prima finiva in fondo a destra).
        const auto *view = static_cast<OutlineTreeView *>(m_outline);
        const QModelIndex index = m_model->indexFromNode(const_cast<MindNode *>(node));
        QRect area = view->titleRect(index);
        for (const QRect &pill : view->labelRects(index)) area = area.united(pill);
        return {m_outline->viewport()->mapToGlobal(area.topLeft()), area.size()};
    }
    const QRectF scene = m_scene->nodeSceneRect(node);
    const QRect view = m_view->mapFromScene(scene).boundingRect();
    return {m_view->viewport()->mapToGlobal(view.topLeft()), view.size()};
}

void MainWindow::openNote(MindNode *node) {
    if (!node) return;
    // Nell'outliner la nota si scrive sotto il titolo; sulla mappa nel riquadro accanto al nodo.
    if (m_workspacePages && m_workspacePages->currentWidget() == m_outlinePage) { openOutlineNote(node); return; }
    m_noteEditor->openFor(node->id, node->notes, node->notesHtml, noteAnchor(node));
}

void MainWindow::openOutlineNote(MindNode *node) {
    auto *view = static_cast<OutlineTreeView *>(m_outline);
    const QModelIndex index = m_model->indexFromNode(node);
    if (!index.isValid()) return;
    view->scrollTo(index);
    // Una nota nuova ha subito il suo riquadro (una riga), poi l'altezza segue il testo.
    const int lineHeight = QFontMetrics(view->noteFont()).lineSpacing() + 2 * view->notePaddingY();
    view->setNoteEditing(index, view->hasNoteText(index) ? qCeil(view->noteDocument(index)->size().height()) + 2 * view->notePaddingY() : lineHeight);
    view->doItemsLayout();
    m_outlineNoteEditor->openInline(node->id, node->notes, node->notesHtml, view->noteBoxRect(index), view->noteFont(),
                                    outlineColors().grip, outlineNoteBackground(), view->notePaddingX(), view->notePaddingY());
}

void MainWindow::placeOutlineNote() {
    if (!m_outlineNoteEditor || !m_outlineNoteEditor->isOpen()) return;
    auto *view = static_cast<OutlineTreeView *>(m_outline);
    if (MindNode *node = nodeById(m_outlineNoteEditor->nodeId())) {
        view->doItemsLayout();
        m_outlineNoteEditor->moveInline(view->noteBoxRect(m_model->indexFromNode(node)));
    }
}

// FEAT-059: fine della scrittura (riquadro della mappa o campo dell'outliner): un passo di storico
// con la nota e la ridisposizione della mappa.
void MainWindow::finishNote(const QString &id, const QString &plain, const QString &html) {
    if (auto *view = static_cast<OutlineTreeView *>(m_outline)) view->setNoteEditing({}, 0);
    MindNode *node = nodeById(id);
    if (node && m_document->setNote(node, plain, html)) {
        m_scene->updateNode(node);
        m_scene->autoLayout();
        m_document->flushPendingStep();
    }
    refreshOutline();
}

// FEAT-048: casella vuota sui nodi che non la hanno (un passo), poi ridisposizione (il nodo si allarga).
void MainWindow::addTaskBoxes(const QList<MindNode *> &nodes) {
    QList<MindNode *> without;
    for (MindNode *node : nodes)
        if (node && node->task == MindNode::Task::None) without.append(node);
    if (!m_document->setTask(without, MindNode::Task::Open)) return;
    m_scene->autoLayout();
    m_document->flushPendingStep();  // casella e ridisposizione in un solo passo
    refreshOutline();
}

// FEAT-048: the topic toolbar button and the "+" menus toggle: when every topic already has a
// checkbox it is removed from all of them, otherwise the topics without one get it. One step.
void MainWindow::toggleTaskBoxes(const QList<MindNode *> &nodes) {
    QList<MindNode *> with;
    bool anyWithout = false;
    for (MindNode *node : nodes) {
        if (!node) continue;
        if (node->task == MindNode::Task::None) anyWithout = true;
        else with.append(node);
    }
    if (anyWithout) { addTaskBoxes(nodes); return; }
    if (!m_document->setTask(with, MindNode::Task::None)) return;
    m_scene->autoLayout();
    m_document->flushPendingStep();
    refreshOutline();
}

// FEAT-068: "- [ ] " o "- [x] " scritto a inizio titolo (mappa o outliner): casella del nodo,
// un passo dopo quello del titolo.
void MainWindow::setTypedTask(MindNode *node, int task) {
    if (!node || !m_document->setTask({node}, task == 2 ? MindNode::Task::Done : MindNode::Task::Open)) return;
    m_scene->autoLayout();
    m_document->flushPendingStep();
    refreshOutline();
}

void MainWindow::showTaskMenu(MindNode *node, const QPoint &screenPos) {
    if (!node) return;
    QMenu menu(this);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(m_outlineAddMenu->styleSheet() + " QMenu::item{padding-left:12px;}");
    QAction *remove = menu.addAction(tr("Remove checkbox"));
    remove->setShortcuts(m_shortcuts->shortcuts(QStringLiteral("removeTask")));  // shown only (FEAT-041 stage B)
    remove->setShortcutVisibleInContextMenu(true);
    const QString id = node->id;
    if (menu.exec(screenPos) != remove) return;
    if (MindNode *current = nodeById(id); current && m_document->setTask({current}, MindNode::Task::None)) {
        m_scene->autoLayout();
        m_document->flushPendingStep();
        refreshOutline();
    }
}

// FEAT-042: branch commands of the topic menu and of Edit in the main menu. Copy and Cut take the
// whole selection when the topic is part of it (user, 2026-11-05).
QList<MindNode *> MainWindow::branchesWith(MindNode *node) const {
    const QList<MindNode *> selected = m_scene->selectedNodes();
    return selected.contains(node) ? selected : QList<MindNode *>{node};
}

void MainWindow::copyBranches(const QList<MindNode *> &nodes) {
    if (nodes.isEmpty()) return;
    auto *mime = new QMimeData;
    mime->setData(kOutlineMimeType, m_document->branchesData(nodes));
    mime->setText(m_document->branchesText(nodes));
    QApplication::clipboard()->setMimeData(mime);  // the clipboard owns mime
}

void MainWindow::copyBranch(MindNode *node) {
    if (node) copyBranches(branchesWith(node));
}

// The central topic is not cut: with others it stays out, alone nothing happens.
void MainWindow::cutBranch(MindNode *node) {
    if (!node) return;
    QList<MindNode *> nodes = branchesWith(node);
    nodes.removeAll(m_model->root());
    if (nodes.isEmpty()) return;
    copyBranches(nodes);
    MindNode *next = nodeAfterRemoval(nodes);
    if (m_document->removeNodes(nodes)) { m_scene->autoLayout(); selectAfterRemoval(next); }
}

// The copied branches (or text read as Markdown, FEAT-055) under parent from row, in one history
// step; the first new topic, or nullptr.
MindNode *MainWindow::pasteBranchesAt(MindNode *parent, int row) {
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!parent || !mime) return nullptr;
    if (mime->hasFormat(kOutlineMimeType)) return m_document->insertBranches(parent, row, Document::branchesFromData(mime->data(kOutlineMimeType)));
    if (!mime->hasText()) return nullptr;
    const Document::PasteResult result = m_document->pasteMarkdown(parent, row, mime->text());
    if (result.tooLarge) {
        statusBar()->showMessage(tr("Text too long: you can paste at most %1 items at a time.")
                                     .arg(MarkdownImport::kMaxPastedItems), 4000);
        return nullptr;
    }
    return result.first;
}

bool MainWindow::canPasteBranch() const {
    return ClipboardPaste::read(QApplication::clipboard()->mimeData(), kOutlineMimeType).kind != ClipboardPaste::Kind::None;
}

// FEAT-094: an image, files or a web address on the clipboard go onto the topic, like the Insert
// menu's entries; false for a branch or text, which the callers paste as topics as before.
bool MainWindow::pasteContentOnto(MindNode *node) {
    if (!node) return false;
    using ClipboardPaste::Kind;
    const ClipboardPaste::Content content = ClipboardPaste::read(QApplication::clipboard()->mimeData(), kOutlineMimeType);
    switch (content.kind) {
    case Kind::Image: {
        const QString path = ClipboardPaste::saveImage(content.image);
        if (path.isEmpty()) statusBar()->showMessage(tr("Cannot paste the image."), 4000);
        else setNodeImage(node, path);
        break;
    }
    case Kind::Files:
        // One image file is the topic's image (Insert > Image...); anything else is attached.
        if (content.files.size() == 1 && FileValidation::image(content.files.front()).ok) setNodeImage(node, content.files.front());
        else if (attachFiles(node, content.files)) m_scene->selectNode(node);
        break;
    case Kind::WebLink:
        commitPendingEdits();
        m_model->setLinks({{node, {}, content.address}});
        statusBar()->showMessage(tr("Link added"), 2000);
        break;
    default:
        return false;
    }
    refreshOutline();
    return true;
}

// FEAT-094: Ctrl+V while a title is typed. Text and addresses stay in the text; an image, files or a
// branch confirm the text and then go onto the topic, as from the + menu. Run on the next loop turn:
// confirming the text deletes the editor that is handling the key.
bool MainWindow::pasteWhileEditing(MindNode *node, bool outlinerItem) {
    using ClipboardPaste::Kind;
    const Kind kind = ClipboardPaste::read(QApplication::clipboard()->mimeData(), kOutlineMimeType).kind;
    if (!node || (kind != Kind::Image && kind != Kind::Files && kind != Kind::Branch)) return false;
    QTimer::singleShot(0, this, [this, id = node->id, kind, outlinerItem] {
        commitPendingEdits();
        MindNode *current = nodeById(id);
        if (!current) return;
        if (kind != Kind::Branch) pasteContentOnto(current);
        else if (outlinerItem) pasteOutlineNode();  // under the item, as Ctrl+V on the row
        else pasteIntoBranch(current);
    });
    return true;
}

// The copied branches go under the topic, as its last children.
void MainWindow::pasteIntoBranch(MindNode *node) {
    if (pasteContentOnto(node)) return;
    MindNode *created = node ? pasteBranchesAt(node, static_cast<int>(node->children.size())) : nullptr;
    if (!created) return;
    m_scene->autoLayout();
    m_scene->selectNode(created);
    updateInspector(created);
}

void MainWindow::duplicateBranch(MindNode *node) {
    if (!node || !node->parent) return;
    MindNode *created = m_document->duplicateNode(node);
    if (!created) return;
    m_scene->autoLayout();
    m_scene->selectNode(created);
    updateInspector(created);
}

// FEAT-072 F1: main menu order (live 2026-10-22), with the commands we have: Undo, Redo |
// New blank map, Home, Open | Save, Save as | Export > | Settings | File >, Edit >, Insert >,
// Tools >, View >, Help >. Not here (missing commands or Pro): Print, ZEN mode, Gantt, Pitch,
// Merge files, themes, account.
void MainWindow::buildMainMenu(QMenu *menu) {
    const auto submenu = [menu](QMenu *parent, const QString &title) {
        QMenu *sub = parent->addMenu(title);
        sub->setStyleSheet(menu->styleSheet());
        sub->setWindowFlags(menu->windowFlags());
        sub->setAttribute(Qt::WA_TranslucentBackground);
        return sub;
    };
    // Commands on the one selected topic: off without a single selection (checked when the menu opens).
    QList<std::pair<QAction *, bool>> onTopic;  // second: also valid on the central topic
    const auto topicAction = [&](QMenu *parent, const QString &text, void (MainWindow::*run)(MindNode *), bool onRoot) {
        QAction *action = parent->addAction(text);
        connect(action, &QAction::triggered, this, [this, run] {
            const QList<MindNode *> selected = m_scene->selectedNodes();
            if (selected.size() == 1) (this->*run)(selected.front());
        });
        onTopic.append({action, onRoot});
        return action;
    };
    menu->addAction(m_actions["undo"]);
    menu->addAction(m_actions["redo"]);
    menu->addSeparator();
    menu->addAction(m_actions["new"]);
    menu->addAction(m_actions["home"]);
    menu->addAction(m_actions["open"]);
    menu->addSeparator();
    menu->addAction(m_actions["save"]);
    menu->addAction(m_actions["saveAs"]);
    menu->addSeparator();
    // Import above Export, both on the first level (user's request 2026-10-27; it was in File).
    QMenu *importMenu = submenu(menu, tr("Import"));
    importMenu->addAction(tr("Markdown…"), m_actions["importMarkdown"], &QAction::trigger);
    // FEAT-054: "Export" is a submenu of the formats.
    QMenu *exportMenu = submenu(menu, tr("Export"));
    exportMenu->addAction(tr("Image…"), m_actions["exportImage"], &QAction::trigger);
    exportMenu->addAction(tr("PDF…"), m_actions["exportPdf"], &QAction::trigger);
    exportMenu->addAction(tr("Word…"), m_actions["exportWord"], &QAction::trigger);
    exportMenu->addAction(tr("Excel…"), m_actions["exportExcel"], &QAction::trigger);
    exportMenu->addAction(tr("Pitch…"), m_actions["exportPresentation"], &QAction::trigger);
    exportMenu->addAction(tr("Markdown…"), m_actions["exportMarkdown"], &QAction::trigger);
    menu->addSeparator();
    menu->addAction(m_actions["settings"]);
    menu->addSeparator();

    QMenu *file = submenu(menu, tr("File"));
    file->addAction(m_actions["new"]);
    file->addAction(tr("New from template…"), this, [this] { if (maybeSave() && chooseTemplate()) showEditor(); });
    file->addAction(m_actions["home"]);
    file->addAction(m_actions["open"]);
    QMenu *recent = submenu(file, tr("Open recent"));
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        for (const QString &path : QSettings().value("recentFiles").toStringList()) {
            if (!QFileInfo::exists(path)) continue;
            recent->addAction(QFileInfo(path).completeBaseName(), this, [this, path] { if (maybeSave()) readFile(path); })->setToolTip(path);
        }
        if (recent->isEmpty()) recent->addAction(tr("No recent maps"))->setEnabled(false);
    });
    file->addSeparator();
    file->addAction(m_actions["save"]);
    file->addAction(m_actions["saveAs"]);
    file->addSeparator();
    file->addAction(m_actions["print"]);

    QMenu *edit = submenu(menu, tr("Edit"));
    edit->addAction(m_actions["undo"]);
    edit->addAction(m_actions["redo"]);
    edit->addSeparator();
    topicAction(edit, tr("Cut"), &MainWindow::cutBranch, false);
    topicAction(edit, tr("Copy"), &MainWindow::copyBranch, true);
    QAction *paste = topicAction(edit, tr("Paste"), &MainWindow::pasteIntoBranch, true);
    topicAction(edit, tr("Duplicate"), &MainWindow::duplicateBranch, false);
    edit->addAction(m_actions["remove"]);
    edit->addSeparator();
    showMenuKey(topicAction(edit, tr("Reset style"), &MainWindow::resetBranchStyle, true), "resetStyle");
    // FEAT-085: entries of their own, greyed out as needed; the registered actions keep the keys (a key
    // after the tab is only shown, like the context menus' ones).
    QAction *copyStyleEntry = edit->addAction(tr("Copy style"), this, &MainWindow::copyStyle);
    QAction *pasteStyleEntry = edit->addAction(tr("Paste style"), this, &MainWindow::pasteStyle);
    connect(menu, &QMenu::aboutToShow, this, [this, copyStyleEntry, pasteStyleEntry] {
        const qsizetype targets = styleTargets().size();
        const auto keyText = [this](const char *id) {
            const QList<QKeySequence> keys = m_shortcuts->shortcuts(QLatin1String(id));
            return keys.isEmpty() ? QString() : QLatin1Char('\t') + keys.first().toString(QKeySequence::NativeText);
        };
        copyStyleEntry->setText(tr("Copy style") + keyText("copyStyle"));
        pasteStyleEntry->setText(tr("Paste style") + keyText("pasteStyle"));
        copyStyleEntry->setEnabled(targets == 1);
        pasteStyleEntry->setEnabled(targets > 0 && m_copiedStyle);
    });
    edit->addSeparator();
    edit->addAction(m_actions["goToCentral"]);
    edit->addSeparator();
    edit->addAction(m_actions["edit"]);

    addSelectMenu(menu);  // FEAT-056: first level, after Edit (user's request 2026-10-25)

    QMenu *insert = submenu(menu, tr("Insert"));
    insert->addAction(m_actions["addChild"]);
    insert->addAction(m_actions["addSibling"]);
    insert->addAction(m_actions["topicBefore"]);  // FEAT-086
    insert->addAction(m_actions["parentTopic"]);
    insert->addSeparator();
    insert->addAction(m_actions["relation"]);
    insert->addAction(m_actions["summary"]);
    insert->addAction(m_actions["boundary"]);
    insert->addSeparator();
    // The map "+" menu: floating topic, image, formula, link, note, sticker, marker...
    insert->addActions(m_mapInsertMenu->actions());
    insert->addSeparator();
    insert->addAction(m_actions["newSheet"]);
    edit->addSeparator();
    edit->addAction(tr("Rename sheet…"), this, &MainWindow::renameSheet);
    edit->addAction(tr("Duplicate sheet"), this, &MainWindow::duplicateSheet);
    QAction *deleteSheetAction = edit->addAction(tr("Delete sheet"), this, &MainWindow::deleteSheet);

    QMenu *tools = submenu(menu, tr("Tools"));
    tools->addAction(m_actions["layout"]);

    QMenu *view = submenu(menu, tr("View"));
    auto *modes = new QActionGroup(view);
    QAction *mindMap = view->addAction(tr("Mind map"), this, [this] { setOutlineMode(false); });
    QAction *outliner = view->addAction(tr("Outliner"), this, [this] { setOutlineMode(true); });
    QAction *gantt = view->addAction(tr("Gantt"), this, [this] { setWorkspaceView(WorkspaceView::Gantt); });  // FEAT-075
    for (QAction *mode : {mindMap, outliner, gantt}) { mode->setCheckable(true); modes->addAction(mode); }
    showMenuKey(gantt, "ganttView");
    showMenuKey(outliner, "toggleOutliner");
    view->addSeparator();
    view->addAction(m_actions["zoomIn"]);
    view->addAction(m_actions["zoomOut"]);
    view->addAction(m_actions["actualSize"]);
    view->addAction(m_actions["fit"]);
    view->addSeparator();
    view->addAction(m_actions["find"]);  // navigation panel (Ctrl+F)
    view->addAction(tr("Format panel"), this, &MainWindow::toggleInspector);
    view->addSeparator();
    view->addAction(m_actions["focusMode"]);

    QMenu *help = submenu(menu, tr("Help"));
    help->addAction(m_actions["help"]);
    // FEAT-093: the tour of each view.
    QMenu *tours = submenu(help, tr("Guided tour"));
    tours->addAction(tr("Mind map"), this, [this] { tour()->start(GuidedTour::Kind::Map); });
    tours->addAction(tr("Outliner"), this, [this] { tour()->start(GuidedTour::Kind::Outliner); });
    tours->addAction(tr("Gantt"), this, [this] { tour()->start(GuidedTour::Kind::Gantt); });
    help->addAction(m_actions["shortcuts"]);

    connect(menu, &QMenu::aboutToShow, this, [this, onTopic, paste, mindMap, outliner, gantt, deleteSheetAction] {
        deleteSheetAction->setEnabled(m_document->sheets().size() > 1);
        const QList<MindNode *> selected = m_scene->selectedNodes();
        const bool one = selected.size() == 1;
        for (const auto &[action, onRoot] : onTopic) action->setEnabled(one && (onRoot || selected.front()->parent));
        paste->setEnabled(one && canPasteBranch());
        const WorkspaceView shown = workspaceView();
        (shown == WorkspaceView::Outline ? outliner : shown == WorkspaceView::Gantt ? gantt : mindMap)->setChecked(true);
    });
}

// FEAT-056: Select >, from the selected topics; an entry with nothing to select is greyed out. Filled
// each time it opens: the main menu is built once.
void MainWindow::addSelectMenu(QMenu *parent) {
    QMenu *select = parent->addMenu(tr("Select"));
    select->setStyleSheet(parent->styleSheet());
    select->setWindowFlags(parent->windowFlags());
    select->setAttribute(Qt::WA_TranslucentBackground);
    connect(select, &QMenu::aboutToShow, this, [this, select] {
        select->clear();
        using Kind = SelectionCommands::Kind;
        const QList<MindNode *> selected = m_scene->selectedNodes();
        for (const auto &[text, kind, commandId] : {std::tuple{tr("Subtopics"), Kind::Subtopics, "selectSubtopics"},
                                         std::tuple{tr("Siblings"), Kind::Siblings, "selectSiblings"},
                                         std::tuple{tr("Same level"), Kind::SameLevel, "selectSameLevel"},
                                         std::tuple{tr("Floating topics"), Kind::Floating, "selectFloating"},
                                         std::tuple{tr("Relationships"), Kind::Relationships, "selectRelationships"},
                                         std::tuple{tr("Summaries"), Kind::Summaries, "selectSummaries"},
                                         std::tuple{tr("Boundaries"), Kind::Boundaries, "selectBoundaries"},
                                         std::tuple{tr("Images"), Kind::Images, "selectImages"},
                                         std::tuple{tr("Equations"), Kind::Equations, "selectEquations"}}) {
            QAction *action = select->addAction(text);
            showMenuKey(action, QString::fromLatin1(commandId));  // FEAT-041 stage B
            action->setEnabled(m_scene->selectTargets(SelectionCommands::targets(kind, *m_model, selected), false) > 0);
            // Computed again on click, from the selection and the map of that moment.
            connect(action, &QAction::triggered, this, [this, kind] {
                m_scene->selectTargets(SelectionCommands::targets(kind, *m_model, m_scene->selectedNodes()));
            });
        }
    });
}

void MainWindow::resetBranchStyle(MindNode *node) { if (node) m_model->resetStyle({node}); }

// FEAT-041 stage B: reference commands that Mentis also has (Settings > Shortcuts of the reference, read
// 2026-11-01), with their keys; the ones listed without a key get none and can be given one.
// Map commands act on the selected topics with the focus on the map (Q3); General ones everywhere.
void MainWindow::createTopicCommands() {
    using Area = ShortcutRegistry::Area;
    using Kind = SelectionCommands::Kind;
    const auto key = [](const char *text) { return QList<QKeySequence>{QKeySequence(QString::fromLatin1(text))}; };
    // Nothing on the Home page, nor while a field is being typed in: a shifted key (Ctrl+Shift+. arrives
    // as Ctrl+Shift+>) can pass the typing filter, and the title would lose it to the command.
    const auto general = [this](const QString &id, const QString &text, const QList<QKeySequence> &keys, const std::function<void()> &run) {
        makeAction(id, Area::General, text, keys, [this, run] {
            if (m_pages && m_pages->currentWidget() == m_editorPage && !typingInField()) run();
        });
        m_windowCommands.append(id);
    };
    const auto map = [this](const QString &id, const QString &text, const QList<QKeySequence> &keys, const std::function<void()> &run) {
        makeAction(id, Area::Map, text, keys, [this, run] { if (!typingInField()) run(); });
        m_mapCommands.append(id);
    };
    // On the one selected topic, like the Insert menu's entries.
    const auto one = [this](void (MainWindow::*run)(MindNode *)) {
        return [this, run] {
            const QList<MindNode *> selected = m_scene->selectedNodes();
            if (selected.size() == 1) (this->*run)(selected.front());
        };
    };

    general("newSheet", tr("New sheet"), key("Ctrl+Alt+N"), [this] { addSheet(); });
    general("prevSheet", tr("Previous sheet"), key("Ctrl+Shift+["), [this] { switchSheet(m_document->currentSheet() - 1); });
    general("nextSheet", tr("Next sheet"), key("Ctrl+Shift+]"), [this] { switchSheet(m_document->currentSheet() + 1); });
    general("toggleOutliner", tr("Switch between map and outliner"), key("Ctrl+Shift+M"),
            [this] { setOutlineMode(workspaceView() != WorkspaceView::Outline); });
    general("ganttView", tr("Gantt chart"), key("Ctrl+Alt+G"), [this] { setWorkspaceView(WorkspaceView::Gantt); });
    general("zoomIn", tr("Zoom in"), key("Ctrl++"), [this] { zoomShownView(10); });
    general("zoomOut", tr("Zoom out"), key("Ctrl+-"), [this] { zoomShownView(-10); });
    general("actualSize", tr("Actual size"), {}, [this] { zoomShownView(0); });
    general("panelStyle", tr("Format panel: Style"), key("Ctrl+]"), [this] { openPanelPage(PanelContext::Page::Style); });
    general("panelPitch", tr("Format panel: Pitch"), {}, [this] { openPanelPage(PanelContext::Page::Pitch); });
    general("panelMap", tr("Format panel: Map"), {}, [this] { openPanelPage(PanelContext::Page::Map); });
    // Toggles (user, 2026-11-05): the side panel of the view shown, the Format panel on the map, the
    // task panel in the Gantt (the outliner has none); open if closed, closed if open, like its button.
    general("toggleSidePanel", tr("Open/close side panel (Format on the map, tasks in the Gantt)"), key("Ctrl+Alt+S"), [this] {
        if (workspaceView() == WorkspaceView::Gantt) m_ganttView->togglePanel();
        else if (workspaceView() == WorkspaceView::Map) toggleDockPage(0);
    });
    // General, not Map: it must also close the panel while the focus is inside it.
    general("toggleIconPanel", tr("Open/close markers and stickers panel (map)"), key("Ctrl+Alt+M"), [this] {
        if (workspaceView() == WorkspaceView::Map) toggleDockPage(1);
    });
    // FEAT-095: the characters of the selected topics (map, outliner, Gantt); a selection being typed
    // takes them through the format bar instead (charStyleKey).
    using RichTitle::CharStyle;
    general("bold", tr("Bold"), key("Ctrl+B"), [this] { toggleCharStyle(CharStyle::Bold); });
    general("italic", tr("Italic"), key("Ctrl+I"), [this] { toggleCharStyle(CharStyle::Italic); });
    general("underline", tr("Underline"), key("Ctrl+U"), [this] { toggleCharStyle(CharStyle::Underline); });
    general("strikeOut", tr("Strikethrough"), key("Ctrl+Shift+X"), [this] { toggleCharStyle(CharStyle::StrikeOut); });

    // FEAT-086: reference keys (checked live 2026-11-01).
    map("moveUp", tr("Move topic up"), key("Alt+Up"), [this] { moveSelectedTopic(-1); });
    map("moveDown", tr("Move topic down"), key("Alt+Down"), [this] { moveSelectedTopic(1); });
    map("topicBefore", tr("Topic before"), key("Shift+Return"), [this] { addTopicBefore(); });
    map("parentTopic", tr("Parent topic"), key("Ctrl+Return"), [this] { addParentTopic(); });
    map("presetVeryImportant", tr("Style \"Very important\""), key("Shift+Alt+1"),
        [this] { setSelectedStyle(StyleProperty::Preset, int(NodePreset::MoltoImportante)); });
    map("presetImportant", tr("Style \"Important\""), key("Shift+Alt+2"),
        [this] { setSelectedStyle(StyleProperty::Preset, int(NodePreset::Importante)); });
    map("presetCrossedOut", tr("Style \"Strike out\""), key("Shift+Alt+3"),
        [this] { setSelectedStyle(StyleProperty::Preset, int(NodePreset::Depenna)); });
    map("branchToSheet", tr("New sheet from topic"), {}, one(&MainWindow::moveBranchToNewSheet));
    map("resetStyle", tr("Reset style"), key("Ctrl+Alt+0"), [this] { m_model->resetStyle(m_scene->selectedNodes()); });
    map("relation", tr("Relationship"), key("Ctrl+Shift+R"), [this] { beginRelation(); });
    map("boundary", tr("Boundary"), key("Ctrl+Shift+B"), [this] { addBoundary(); });
    map("summary", tr("Summary"), {}, [this] { addSummary(); });
    map("note", tr("Note"), key("Ctrl+Shift+N"), one(&MainWindow::openNote));
    map("labels", tr("Label"), key("Ctrl+Shift+L"), one(&MainWindow::openLabels));
    map("webLink", tr("Link to a web page"), key("Ctrl+K"), one(&MainWindow::editWebLink));
    map("topicLink", tr("Link to a topic"), {}, one(&MainWindow::editTopicLink));
    map("attachFiles", tr("Attach files"), {}, one(&MainWindow::chooseFilesToAttach));
    // BUG-042: Ctrl+C and Ctrl+X copy the selected branch, as the topic menu does; before, the map had
    // no copy key and Ctrl+V pasted whatever the clipboard held.
    // FEAT-042 (2026-11-05): all the selected branches.
    map("mapCopy", tr("Copy"), {QKeySequence(QKeySequence::Copy)}, [this] { copyBranches(m_scene->selectedNodes()); });
    map("mapCut", tr("Cut"), {QKeySequence(QKeySequence::Cut)}, [this] {
        if (const QList<MindNode *> selected = m_scene->selectedNodes(); !selected.isEmpty()) cutBranch(selected.front());
    });
    // FEAT-094: Ctrl+V on the selected topic (the Edit menu's Paste had no key on the map).
    map("mapPaste", tr("Paste"), {QKeySequence(QKeySequence::Paste)}, one(&MainWindow::pasteIntoBranch));
    map("markers", tr("Markers"), {}, [this] { showIconPanel(0); });
    map("stickers", tr("Stickers"), {}, [this] { showIconPanel(1); });
    map("formula", tr("LaTeX formula"), {}, [this] { insertFormula(); });
    map("removeMarkers", tr("Remove markers"), {}, [this] { removeMarkersOfSelected(); });
    map("removeVisual", tr("Remove sticker, image or formula"), {}, [this] { removeVisualOfSelected(); });
    map("removeLinks", tr("Remove links"), {}, [this] { removeLinksOfSelected(); });
    map("addTask", tr("Checkbox"), key("Ctrl+Alt+T"), [this] { toggleTaskBoxes(m_scene->selectedNodes()); });
    map("removeTask", tr("Remove checkbox"), {}, [this] { setTaskOfSelected(MindNode::Task::None); });
    map("taskDone", tr("Mark as done"), {}, [this] { setTaskOfSelected(MindNode::Task::Done); });
    map("taskNotDone", tr("Mark as not done"), {}, [this] { setTaskOfSelected(MindNode::Task::Open); });
    map("upperCase", tr("All uppercase"), {}, [this] { setSelectedStyle(StyleProperty::TextCase, int(TextCase::Maiuscolo)); });
    map("lowerCase", tr("All lowercase"), {}, [this] { setSelectedStyle(StyleProperty::TextCase, int(TextCase::Minuscolo)); });
    map("alignLeft", tr("Align text left"), {}, [this] { setSelectedStyle(StyleProperty::TextAlign, int(TextAlign::Sinistra)); });
    map("alignCenter", tr("Center text"), {}, [this] { setSelectedStyle(StyleProperty::TextAlign, int(TextAlign::Centro)); });
    map("alignRight", tr("Align text right"), {}, [this] { setSelectedStyle(StyleProperty::TextAlign, int(TextAlign::Destra)); });
    map("fontLarger", tr("Larger text"), key("Ctrl+Shift+."), [this] { stepFontSize(true); });
    map("fontSmaller", tr("Smaller text"), key("Ctrl+Shift+,"), [this] { stepFontSize(false); });
    map("numberingNone", tr("Numbering: none"), {}, [this] { setNumberingPreset(-1); });
    map("numbering123", tr("Numbering: 1.2.3"), {}, [this] { setNumberingPreset(0); });
    map("numberingABC", tr("Numbering: A.B.C"), {}, [this] { setNumberingPreset(1); });
    map("numberingabc", tr("Numbering: a.b.c"), {}, [this] { setNumberingPreset(2); });
    map("numberingRoman", tr("Numbering: I.II.III"), {}, [this] { setNumberingPreset(3); });
    map("goToCentral", tr("Go to central topic"), key("Ctrl+R"), [this] {
        m_scene->selectNode(m_model->root());
        updateInspector(m_model->root());
    });
    for (const auto &[id, text, kind] : {std::tuple{"selectSubtopics", tr("Select subtopics"), Kind::Subtopics},
                                         std::tuple{"selectSiblings", tr("Select siblings"), Kind::Siblings},
                                         std::tuple{"selectSameLevel", tr("Select topics of the same level"), Kind::SameLevel},
                                         std::tuple{"selectFloating", tr("Select floating topics"), Kind::Floating},
                                         std::tuple{"selectRelationships", tr("Select relationships"), Kind::Relationships},
                                         std::tuple{"selectSummaries", tr("Select summaries"), Kind::Summaries},
                                         std::tuple{"selectBoundaries", tr("Select boundaries"), Kind::Boundaries},
                                         std::tuple{"selectImages", tr("Select images"), Kind::Images},
                                         std::tuple{"selectEquations", tr("Select equations"), Kind::Equations}}) {
        map(QString::fromLatin1(id), text, {}, [this, kind] {
            m_scene->selectTargets(SelectionCommands::targets(kind, *m_model, m_scene->selectedNodes()));
        });
    }
    map("toggleCollapse", tr("Collapse or expand the branch"), key("Ctrl+/"), [this] { toggleCollapseOfSelected(); });
    map("toggleSubBranches", tr("Collapse or expand the sub-branches"), key("Ctrl+Alt+/"), [this] { toggleSubBranchesOfSelected(); });
}

void MainWindow::moveBranchToSheet(MindNode *node, int targetIndex) {
    if (!node || !node->parent) return;
    commitPendingEdits();
    if (!m_document->moveBranchToSheet(node, targetIndex)) {
        statusBar()->showMessage(tr("Cannot move the branch to that sheet."), 3000);
        return;
    }
    syncSheetSelect();
    onSheetShown();
}

void MainWindow::moveBranchToNewSheet(MindNode *node) { moveBranchToSheet(node, -1); }

void MainWindow::moveSelectedTopic(int direction) {
    const QList<MindNode *> selected = m_scene->selectedNodes();
    if (selected.size() != 1) return;
    MindNode *node = selected.front();
    if (!m_document->moveAmongSiblings(node, direction, [this](const MindNode *at) { return m_scene->sideOf(at); })) return;
    m_scene->autoLayout();
    m_document->flushPendingStep();  // the swap and the layout in one step
    m_scene->selectNode(node);
    refreshOutline();
}

void MainWindow::addTopicBefore() {
    const QList<MindNode *> selected = m_scene->selectedNodes();
    if (selected.size() != 1) return;
    showCreatedNode(m_document->addSiblingBefore(selected.front()));  // selected, not in editing, like Add sibling
}

void MainWindow::addParentTopic() {
    const QList<MindNode *> selected = m_scene->selectedNodes();
    if (selected.size() != 1) return;
    MindNode *created = m_document->insertParent(selected.front());
    if (!created) return;
    m_scene->autoLayout();
    m_document->flushPendingStep();
    m_scene->selectNode(created);
    m_view->reveal(m_scene->nodeSceneRect(created), 24);
    refreshOutline();
}

void MainWindow::zoomShownView(int deltaPercent) {
    switch (workspaceView()) {
    case WorkspaceView::Outline:
        applyOutlineZoom(deltaPercent ? m_outlineZoom + deltaPercent : 100);
        break;
    case WorkspaceView::Gantt: {
        GanttChart *chart = m_ganttView->chart();
        chart->setZoomPercent(deltaPercent ? chart->zoomPercent() + deltaPercent : 100);
        break;
    }
    case WorkspaceView::Map:
        m_view->setZoomPercent(deltaPercent ? m_view->zoomPercent() + deltaPercent : 100);
        break;
    }
}

void MainWindow::openPanelPage(PanelContext::Page page) {
    ensureInspector();
    if (!m_inspectorDock->isVisible()) toggleInspector();
    if (workspaceView() != WorkspaceView::Gantt) showPanelPage(page);  // the Gantt's panel has its own pages
}

// FEAT-095: on for every topic unless every character of all of them has it; the text there now
// changes (titleHtml), not the topic's style, so text typed later is normal. Formulas stay as they are.
int MainWindow::charStyleKey(const QKeyEvent *event) const {
    const std::pair<const char *, RichTitle::CharStyle> styles[] = {{"bold", RichTitle::CharStyle::Bold}, {"italic", RichTitle::CharStyle::Italic},
                                                                     {"underline", RichTitle::CharStyle::Underline}, {"strikeOut", RichTitle::CharStyle::StrikeOut}};
    for (const auto &[id, style] : styles)
        if (m_shortcuts->matches(QString::fromLatin1(id), event)) return int(style);
    return -1;
}

void MainWindow::toggleCharStyle(RichTitle::CharStyle style) {
    QList<MindNode *> nodes;
    for (MindNode *node : styleTargets())
        if (!node->title.isEmpty() && !FormulaText::containsMath(node->title)) nodes.append(node);
    if (nodes.isEmpty()) return;
    const auto base = [](const MindNode *node) {
        return RichTitle::BaseStyle{node->fontWeight, node->italic, node->underline, node->strikeOut};
    };
    bool all = true;
    for (const MindNode *node : std::as_const(nodes))
        all = all && RichTitle::hasEverywhere(node->title, node->titleHtml, style, base(node), NodeItem::boldWeightFor(node));
    m_document->flushPendingStep();  // one undo step of its own
    for (MindNode *node : std::as_const(nodes))
        m_document->setFormattedTitle(node, node->title,
                                      RichTitle::withEverywhere(node->title, node->titleHtml, style, !all, base(node), NodeItem::boldWeightFor(node)));
    m_document->flushPendingStep();
}

void MainWindow::setSelectedStyle(StyleProperty property, const QVariant &value) {
    const QList<MindNode *> nodes = m_scene->selectedNodes();
    if (!nodes.isEmpty()) m_model->setStyle(nodes, property, value);
}

// The size of the first selected topic, one step of the Style page's list, on all of them.
void MainWindow::stepFontSize(bool larger) {
    const QList<MindNode *> nodes = m_scene->selectedNodes();
    if (nodes.isEmpty()) return;
    const NodeStyle::Reading size = NodeStyle::read({nodes.front()}, StyleProperty::FontSize);
    if (const std::optional<qreal> next = NodeStyle::steppedFontSizePt(size.value.toDouble(), larger))
        m_model->setStyle(nodes, StyleProperty::FontSize, *next);
}

// The preset format with the separator, levels and first number the topics have (FEAT-024).
void MainWindow::setNumberingPreset(int preset) {
    const QList<MindNode *> nodes = m_scene->selectedNodes();
    if (nodes.isEmpty()) return;
    const NodeStyle::Reading current = NodeStyle::read(QList<const MindNode *>(nodes.cbegin(), nodes.cend()), StyleProperty::Numbering);
    BranchNumbering numbering = current.mixed ? BranchNumbering() : current.value.value<BranchNumbering>();
    numbering.format = preset < 0 ? QString() : Numbering::presetFormats(numbering.separator).value(preset);
    m_model->setStyle(nodes, StyleProperty::Numbering, QVariant::fromValue(numbering));
}

void MainWindow::setTaskOfSelected(MindNode::Task state) {
    if (!m_document->setTask(m_scene->selectedNodes(), state)) return;
    m_scene->autoLayout();
    m_document->flushPendingStep();  // checkboxes and layout in one step
    refreshOutline();
}

void MainWindow::removeMarkersOfSelected() {
    bool changed = false;
    for (MindNode *node : m_scene->selectedNodes()) {
        bool own = false;
        for (const QString &marker : QStringList(node->markers)) own |= m_document->setMarker(node, Markers::seriesOf(marker), QString());
        if (own) m_scene->updateNode(node);
        changed |= own;
    }
    if (!changed) return;
    m_scene->autoLayout();
    m_document->flushPendingStep();  // all the markers and the layout in one step
    refreshOutline();
    updateIconPanelSelection();
}

void MainWindow::removeVisualOfSelected() {
    bool changed = false;
    for (MindNode *node : m_scene->selectedNodes())
        if (m_document->removeVisual(node)) { changed = true; m_scene->updateNode(node); }
    if (!changed) return;
    m_scene->autoLayout();
    m_document->flushPendingStep();
    refreshOutline();
    refreshStylePage();
}

void MainWindow::removeLinksOfSelected() {
    QList<MindMapModel::LinkChange> changes;
    for (MindNode *node : m_scene->selectedNodes())
        if (!node->topicLink.isEmpty() || !node->webLink.isEmpty()) changes.append({node, QString(), QString()});
    if (!changes.isEmpty()) m_model->setLinks(changes);
}

namespace {
bool hasMapChildren(const MindNode *node) {
    return std::any_of(node->children.cbegin(), node->children.cend(), [](const auto &child) { return !child->floating; });
}
}  // namespace

// On several topics: they all close while one of them is open, otherwise they all open.
void MainWindow::toggleCollapseOfSelected() {
    QList<MindNode *> branches;
    for (MindNode *node : m_scene->selectedNodes())
        if (node->parent && hasMapChildren(node)) branches.append(node);
    if (branches.isEmpty()) return;
    const bool collapse = std::any_of(branches.cbegin(), branches.cend(), [](const MindNode *node) { return !node->collapsed; });
    bool changed = false;
    for (MindNode *node : branches) changed |= m_document->setCollapsed(node, collapse);
    if (!changed) return;
    m_scene->autoLayout();
    m_document->flushPendingStep();
    m_scene->selectTargets({branches, {}, {}});
    refreshOutline();
}

// The topic menu's "Collapse all sub-branches" while a branch below is open, otherwise "Expand all".
void MainWindow::toggleSubBranchesOfSelected() {
    const QList<MindNode *> selected = m_scene->selectedNodes();
    if (selected.size() != 1) return;
    MindNode *node = selected.front();
    bool openBelow = false;
    const std::function<void(const MindNode *)> scan = [&](const MindNode *at) {
        for (const auto &child : at->children) {
            if (child->floating || !hasMapChildren(child.get())) continue;
            if (!child->collapsed) openBelow = true;
            scan(child.get());
        }
    };
    scan(node);
    if (!m_document->setCollapsedBelow(node, openBelow)) return;
    m_scene->autoLayout();
    m_document->flushPendingStep();
    m_scene->selectNode(node);
    refreshOutline();
}

// FEAT-041 stage B (Q4): the command's key after a tab, like the context menus show it; the
// registered action handles the key. It follows the Shortcuts page.
void MainWindow::showMenuKey(QAction *entry, const QString &commandId) {
    const QString text = entry->text().section(QLatin1Char('\t'), 0, 0);
    const auto update = [this, entry, text, commandId] {
        const QList<QKeySequence> keys = m_shortcuts->shortcuts(commandId);
        entry->setText(keys.isEmpty() ? text : text + QLatin1Char('\t') + keys.first().toString(QKeySequence::NativeText));
    };
    update();
    connect(m_shortcuts, &ShortcutRegistry::changed, entry, [update, commandId](const QString &id) { if (id == commandId) update(); });
}

// FEAT-085: the selected topics of the page shown.
QList<MindNode *> MainWindow::styleTargets() const {
    if (workspaceView() != WorkspaceView::Gantt) return iconTargetNodes();  // map or outliner
    QList<MindNode *> topics;
    for (const QString &id : m_ganttView->selectedIds())
        if (MindNode *topic = nodeById(id)) topics.append(topic);
    return topics;
}

// The keys belong to a text being typed (a title, a Gantt cell, a note): no style command then.
bool MainWindow::typingInField() const {
    const QWidget *focus = QApplication::focusWidget();
    if (qobject_cast<const QLineEdit *>(focus) || qobject_cast<const QTextEdit *>(focus) ||
        qobject_cast<const QPlainTextEdit *>(focus) || qobject_cast<const QAbstractSpinBox *>(focus))
        return true;
    if (const auto *combo = qobject_cast<const QComboBox *>(focus); combo && combo->isEditable()) return true;
    const QGraphicsItem *item = m_scene->focusItem();
    return item && !qgraphicsitem_cast<const NodeItem *>(item);
}

void MainWindow::copyStyle() {
    if (typingInField()) return;
    const QList<MindNode *> targets = styleTargets();
    if (targets.size() == 1) copyStyleOf(targets.front());
}

void MainWindow::copyStyleOf(const MindNode *node) {
    if (!node) return;
    MindMapModel::TopicStyle style = m_model->topicStyle(node);
    // A Gantt bar without a colour of its own shows the sheet's default, not the branch colour of
    // the map: copied from the Gantt it stays without one, so the target's bar looks the same.
    if (workspaceView() == WorkspaceView::Gantt && !node->style.branchColor) style.style.branchColor.reset();
    m_copiedStyle = style;
}

void MainWindow::pasteStyle() {
    if (!typingInField()) pasteStyleOn(styleTargets());
}

void MainWindow::pasteStyleOn(const QList<MindNode *> &targets) {
    if (m_copiedStyle && !targets.isEmpty()) m_model->pasteStyle(*m_copiedStyle, targets);  // one step (styleEdited)
}

void MainWindow::setFormatPainter(bool on, bool sticky) {
    if (!m_topicTools || (m_formatPainter == on && m_formatPainterSticky == (on && sticky))) return;
    const bool wasOn = m_formatPainter;
    m_formatPainter = on;
    m_formatPainterSticky = on && sticky;
    if (!on) {
        m_formatPainterBandOrigin.reset();
        delete m_formatPainterBand;
    }
    m_topicTools->setFormatPainterOn(on);
    // An override cursor: the viewport's own is replaced by the items' cursors (link, note icons) and
    // put back by the view afterwards, so a brush set there could outlive the painter.
    if (on && !wasOn) QApplication::setOverrideCursor(QCursor(QIcon(QStringLiteral(":/assets/hugeicons/format-painter.svg")).pixmap(24, 24), 6, 21));
    else if (!on && wasOn) QApplication::restoreOverrideCursor();
}

// FEAT-042: right-click on a topic (text only, groups separated by lines, shortcuts on
// the right). A topic not yet selected becomes the selection; on a multiple selection the menu
// offers only Delete, like the Outliner.
void MainWindow::showNodeMenu(MindNode *node, const QPoint &screenPos) {
    if (!node) return;
    const QList<MindNode *> selected = m_scene->selectedNodes();
    const bool multiple = selected.size() > 1 && selected.contains(node);
    if (!multiple) {
        m_scene->selectNode(node);
        updateInspector(node);
    }
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("NodeMenu"));
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(TextEditMenu::styleSheet());
    const QString id = node->id;
    const auto add = [&](const QString &text, const char *commandId, std::function<void(MindNode *)> run, bool enabled = true) {
        QAction *action = menu.addAction(text);
        if (commandId) action->setShortcuts(m_shortcuts->shortcuts(commandId));  // shown only: the map handles the keys
        action->setShortcutVisibleInContextMenu(true);
        action->setEnabled(enabled);
        connect(action, &QAction::triggered, this, [this, id, run] { if (MindNode *current = nodeById(id)) run(current); });
    };
    if (multiple) {
        add(tr("Cut"), nullptr, [this](MindNode *current) { cutBranch(current); });  // FEAT-042: the whole selection
        add(tr("Copy"), nullptr, [this](MindNode *current) { copyBranch(current); });
        menu.addSeparator();
        add(tr("Delete"), "remove", [this](MindNode *) { removeSelected(); });
        add(tr("Paste style"), "pasteStyle", [this](MindNode *) { pasteStyle(); }, m_copiedStyle.has_value());  // FEAT-085
        menu.addSeparator();
        addSelectMenu(&menu);
        menu.exec(screenPos);
        return;
    }
    const bool isRoot = !node->parent;
    // FEAT-052: a summary topic has no row among the children: only the entries that work on it.
    const bool summary = node->summaryTopic;
    menu.addMenu(m_mapInsertMenu);
    menu.addSeparator();
    add(tr("Add child"), "addChild", [this](MindNode *) { addChild(); });
    if (!summary) {
        add(tr("Add sibling"), "addSibling", [this](MindNode *) { addSibling(); }, !isRoot);
        add(tr("Topic before"), "topicBefore", [this](MindNode *) { addTopicBefore(); }, !isRoot && !node->floating);  // FEAT-086
        add(tr("Parent topic"), "parentTopic", [this](MindNode *) { addParentTopic(); }, !isRoot && !node->floating);
    }
    add(tr("Edit text"), "edit", [this](MindNode *) { editSelected(); });
    menu.addSeparator();
    if (!summary) add(tr("Cut"), nullptr, [this](MindNode *current) { cutBranch(current); }, !isRoot);
    add(tr("Copy"), nullptr, [this](MindNode *current) { copyBranch(current); });
    add(tr("Paste"), nullptr, [this](MindNode *current) { pasteIntoBranch(current); }, canPasteBranch());
    if (!summary) add(tr("Duplicate"), nullptr, [this](MindNode *current) { duplicateBranch(current); }, !isRoot);
    menu.addSeparator();
    add(tr("Delete"), "remove", [this](MindNode *) { removeSelected(); }, !isRoot);
    menu.addSeparator();
    add(tr("Reset style"), "resetStyle", [this](MindNode *current) { resetBranchStyle(current); }, node->style != NodeStyleOverrides());
    add(tr("Copy style"), "copyStyle", [this](MindNode *current) { copyStyleOf(current); });  // FEAT-085
    add(tr("Paste style"), "pasteStyle", [this](MindNode *current) { pasteStyleOn({current}); }, m_copiedStyle.has_value());
    addSelectMenu(&menu);  // FEAT-056
    add(tr("Relationship"), "relation", [this](MindNode *) { beginRelation(); });
    if (!summary) {
        // FEAT-057: Arrange, greyed out where the branch cannot overlap another one.
        QMenu *arrange = menu.addMenu(tr("Arrange"));
        arrange->setStyleSheet(TextEditMenu::styleSheet());
        arrange->setWindowFlags(arrange->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        arrange->setAttribute(Qt::WA_TranslucentBackground);
        arrange->setEnabled(m_document->canArrange(node));
        using Arrange = Document::Arrange;
        for (const auto &[text, commandId, how] : {std::tuple{tr("Bring to front"), "bringToFront", Arrange::ToFront},
                                                   std::tuple{tr("Bring forward"), "bringForward", Arrange::Forward},
                                                   std::tuple{tr("Send backward"), "sendBackward", Arrange::Backward},
                                                   std::tuple{tr("Send to back"), "sendToBack", Arrange::ToBack}}) {
            QAction *action = arrange->addAction(text);
            action->setShortcuts(m_shortcuts->shortcuts(commandId));  // shown only: the map handles the keys
            action->setShortcutVisibleInContextMenu(true);
            connect(action, &QAction::triggered, this, [this, how] { arrangeSelected(how); });
        }
    }
    menu.addSeparator();
    const auto hasChildren = [](const MindNode *at) {
        return std::any_of(at->children.cbegin(), at->children.cend(), [](const auto &child) { return !child->floating; });
    };
    const auto relayout = [this](MindNode *current) {
        m_scene->autoLayout();
        m_document->flushPendingStep();
        m_scene->selectNode(current);
        refreshOutline();
    };
    if (hasChildren(node) && !isRoot) {
        add(node->collapsed ? tr("Expand branch") : tr("Collapse branch"), "toggleCollapse", [this, relayout](MindNode *current) {
            if (m_document->setCollapsed(current, !current->collapsed)) relayout(current);
        });
    }
    // "Collapse all sub-branches" while some branch below is open, "Expand all" once
    // something in the branch is closed.
    bool openBelow = false, closed = node->collapsed;
    const std::function<void(const MindNode *, bool)> scan = [&](const MindNode *at, bool self) {
        if (hasChildren(at)) {
            if (at->collapsed) closed = true;
            else if (!self) openBelow = true;
        }
        for (const auto &child : at->children) if (!child->floating) scan(child.get(), false);
    };
    scan(node, true);
    if (openBelow) {
        add(tr("Collapse all sub-branches"), "toggleSubBranches", [this, relayout](MindNode *current) {
            if (m_document->setCollapsedBelow(current, true)) relayout(current);
        });
    }
    if (closed) {
        add(tr("Expand all"), openBelow ? nullptr : "toggleSubBranches", [this, relayout](MindNode *current) {
            if (m_document->setCollapsedBelow(current, false)) relayout(current);
        });
    }
    if (summary) { menu.exec(screenPos); return; }
    // FEAT-029: Move to sheet > New sheet and the other sheets of the file (not for the central topic).
    QMenu *sheets = menu.addMenu(tr("Move to sheet"));
    sheets->setStyleSheet(TextEditMenu::styleSheet());
    sheets->setWindowFlags(sheets->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    sheets->setAttribute(Qt::WA_TranslucentBackground);
    sheets->setEnabled(!isRoot);
    const auto moveTo = [this, id](int index) { moveBranchToSheet(nodeById(id), index); };
    QAction *toNewSheet = sheets->addAction(tr("New sheet"));
    toNewSheet->setShortcuts(m_shortcuts->shortcuts(QStringLiteral("branchToSheet")));  // shown only
    toNewSheet->setShortcutVisibleInContextMenu(true);
    connect(toNewSheet, &QAction::triggered, this, [moveTo] { moveTo(-1); });
    if (m_document->sheets().size() > 1) sheets->addSeparator();
    for (int i = 0; i < m_document->sheets().size(); ++i) {
        if (i == m_document->currentSheet()) continue;
        connect(sheets->addAction(m_document->sheets()[i].name), &QAction::triggered, this, [moveTo, i] { moveTo(i); });
    }
    menu.exec(screenPos);
}

void MainWindow::openLabels(MindNode *node) {
    if (!node) return;
    m_labelEditor->openFor(node->id, node->labels, m_document->documentLabels(), noteAnchor(node));
}

// FEAT-060: un passo di storico con le etichette e la ridisposizione (le pillole occupano posto).
void MainWindow::finishLabels(const QString &id, const QStringList &labels) {
    MindNode *node = nodeById(id);
    if (!node || !m_document->setLabels(node, labels)) return;
    m_scene->updateNode(node);
    m_scene->autoLayout();
    m_document->flushPendingStep();
    if (!m_scene->labelHighlight().isEmpty()) m_scene->setLabelHighlight(m_scene->labelHighlight());
    refreshOutline();
}

void MainWindow::showLabelMenu(MindNode *node, const QString &label, const QPoint &screenPos) {
    if (!node) return;
    QMenu menu(this);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(m_outlineAddMenu->styleSheet() + " QMenu::item{padding-left:12px;}");
    QAction *remove = menu.addAction(tr("Remove", "label"));
    QAction *highlight = menu.addAction(tr("Highlight related topics"));
    const QString id = node->id;
    QAction *chosen = menu.exec(screenPos);
    MindNode *current = nodeById(id);
    if (!chosen || !current) return;
    if (chosen == highlight) {
        // Sulla mappa: nell'outliner si torna prima alla mappa.
        if (m_workspacePages && m_workspacePages->currentWidget() == m_outlinePage) setOutlineMode(false);
        m_scene->setLabelHighlight(label);
        return;
    }
    QStringList labels = current->labels;
    labels.removeIf([&label](const QString &own) { return Labels::same(own, label); });
    finishLabels(id, labels);
}

void MainWindow::showLinkMenu(MindNode *node, NodeItem::LinkKind kind, const QPoint &screenPos) {
    if (!node) return;
    QMenu menu(this);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    // Come il menu del tasto destro dell'Outliner (voci di solo testo).
    menu.setStyleSheet(m_outlineAddMenu->styleSheet() + " QMenu::item{padding-left:12px;}");
    QAction *editAction = menu.addAction(tr("Edit link…"));
    QAction *removeAction = menu.addAction(tr("Remove link"));
    const QString id = node->id;
    QAction *chosen = menu.exec(screenPos);
    MindNode *current = nodeById(id);
    if (!chosen || !current) return;
    if (chosen == editAction) kind == NodeItem::LinkKind::Topic ? editTopicLink(current) : editWebLink(current);
    else if (chosen == removeAction) {
        MindMapModel::LinkChange change{current, {}, {}};
        if (kind == NodeItem::LinkKind::Topic) change.topicLink = QString();
        else change.webLink = QString();
        m_model->setLinks({change});
    }
}

void MainWindow::updateHistoryActions() {
    if (m_actions.contains("undo")) m_actions["undo"]->setEnabled(m_document->canUndo());
    if (m_actions.contains("redo")) m_actions["redo"]->setEnabled(m_document->canRedo());
}

void MainWindow::updateRootFormula() {
    const QString text = m_outlineRootEdit->toPlainText();
    if (!FormulaText::containsMath(text)) {
        m_rootFormula = FormulaText();
        static_cast<OutlineRootTitleEdit *>(m_outlineRootEdit)->setFormulaHeight(0);
    } else {
        m_rootFormula.update(text, m_outlineRootEdit->font(), QColor("#202630"), qMax(300, m_outlineRootEdit->width() - 20));
        static_cast<OutlineRootTitleEdit *>(m_outlineRootEdit)->setFormulaHeight(qCeil(m_rootFormula.size().height()) + 20);
    }
    m_outlineRootEdit->viewport()->update();
}

void MainWindow::updateMapSubtitle() {
    if (!m_saveStateButton) return;
    const QString shortcut = m_actions.contains("save")
        ? m_actions["save"]->shortcut().toString(QKeySequence::NativeText) : QString();
    const QString actionHint = shortcut.isEmpty() ? tr("Click to save") : tr("Save (%1)").arg(shortcut);
    // P4 (2026-10-13): questa funzione gira a ogni setDirty(), cioè a ogni movimento del mouse
    // mentre si trascina un nodo. Prima ricreava ogni volta l'icona dall'SVG (misurato: 0,26 ms
    // contro 0,013 ms con l'icona riusata). Ora le due icone si creano una volta sola e
    // pulsante e tooltip si aggiornano solo quando lo stato cambia davvero.
    static const QIcon syncIcon(":/assets/hugeicons/database-sync.svg");
    static const QIcon savedIcon(":/assets/hugeicons/checkmark-badge-02.svg");
    const int state = m_document->isSaving() ? 0 : (!isWindowModified() && !m_document->filePath().isEmpty()) ? 1 : 2;
    const QString stateKey = QString::number(state) + actionHint;
    if (m_saveStateButton->property("saveStateKey").toString() == stateKey) return;
    m_saveStateButton->setProperty("saveStateKey", stateKey);
    if (state == 0) {
        m_saveStateButton->setIcon(syncIcon);
        m_saveStateButton->setToolTip(tr("Saving…\n%1").arg(actionHint));
    } else if (state == 1) {
        m_saveStateButton->setIcon(savedIcon);
        m_saveStateButton->setToolTip(tr("Map saved\n%1").arg(actionHint));
    } else {
        m_saveStateButton->setIcon(syncIcon);
        m_saveStateButton->setToolTip(tr("Changes to save\n%1").arg(actionHint));
    }
    FloatingUi::retint(m_saveStateButton);  // tono dei gruppi (sfondi scuri: icona chiara)
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    // + > Argomento mobile: l'anteprima segue il cursore, il clic sinistro la fissa (vedi
    // startFloatingPlacement), il destro o Esc annullano.
    // Startup: the first paint of the Home is flushed to the screen before the queued calls run,
    // so the window appears first; the side panel, the Gantt view and its panel's style follow,
    // one per event loop turn so that input in between is handled.
    if (watched == m_homePage && event->type() == QEvent::Paint && !m_homePainted) {
        m_homePainted = true;  // the filter stays: Home's margins move the window (BUG-041)
        QTimer::singleShot(0, this, &MainWindow::ensureInspector);
        QTimer::singleShot(0, this, &MainWindow::ensureGanttView);
        QTimer::singleShot(0, this, [this] { if (m_ganttView) m_ganttView->panel()->applyPendingStyle(); });
    }
    if (m_placingFloating && watched == m_view->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            m_placingFloating->setPos(m_view->mapToScene(static_cast<QMouseEvent *>(event)->position().toPoint()));
            return false;
        }
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const QPointF position = m_view->mapToScene(mouse->position().toPoint());
            cancelFloatingPlacement();
            if (mouse->button() == Qt::LeftButton) {
                MindNode *node = m_document->addFloatingTopic(position, tr("Floating Topic"));
                m_scene->autoLayout();
                m_document->flushPendingStep();  // un passo
                m_scene->selectNode(node);
                updateInspector(node);
                refreshOutline();
            }
            return true;
        }
    }
    // FEAT-085: with the format painter on, a click on a topic gives it the copied style, without
    // selecting or dragging it; a rectangle dragged from an empty place gives it to every topic it
    // touches (user's request 2026-11-01), the selection unchanged. A click on an empty place, the
    // right button or Esc turn the painter off.
    if (m_formatPainterBandOrigin && watched == m_view->viewport()) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QRect band = QRect(*m_formatPainterBandOrigin, event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease
                                                                 ? mouse->position().toPoint() : *m_formatPainterBandOrigin).normalized();
        if (event->type() == QEvent::MouseMove) {
            if (!m_formatPainterBand && (mouse->position().toPoint() - *m_formatPainterBandOrigin).manhattanLength() >= QApplication::startDragDistance())
                m_formatPainterBand = new QRubberBand(QRubberBand::Rectangle, m_view->viewport());
            if (m_formatPainterBand) {
                m_formatPainterBand->setGeometry(band);
                m_formatPainterBand->show();
            }
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            m_formatPainterBandOrigin.reset();
            QList<MindNode *> touched;
            if (m_formatPainterBand) {
                delete m_formatPainterBand;
                for (QGraphicsItem *item : m_scene->items(m_view->mapToScene(band), Qt::IntersectsItemShape))
                    if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item); nodeItem && nodeItem->isVisible() && !touched.contains(nodeItem->node()))
                        touched.append(nodeItem->node());
            }
            pasteStyleOn(touched);
            if (touched.isEmpty() || !m_formatPainterSticky) setFormatPainter(false);
            return true;
        }
    }
    if (m_formatPainterJustPasted && watched == m_view->viewport() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)) {
        m_formatPainterJustPasted = false;
        if (event->type() == QEvent::MouseButtonDblClick) return true;  // else it would edit the title
    }
    if (m_formatPainter && watched == m_view->viewport() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        MindNode *node = mouse->button() == Qt::LeftButton ? m_scene->nodeAt(m_view->mapToScene(mouse->position().toPoint())) : nullptr;
        if (!node && mouse->button() == Qt::LeftButton && event->type() == QEvent::MouseButtonPress) {
            m_formatPainterBandOrigin = mouse->position().toPoint();  // a rectangle, or a click that turns it off
            return true;
        }
        if (node) pasteStyleOn({node});
        if (!node || !m_formatPainterSticky) {
            setFormatPainter(false);
            m_formatPainterJustPasted = node != nullptr;
        }
        return true;
    }
    if (m_formatPainter && watched == m_view && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        setFormatPainter(false);
        return true;
    }
    // Frecce sulla mappa: dal nodo scelto al vicino in quella direzione (MindMapScene::neighbour). Non
    // mentre si scrive in un nodo (il fuoco e' sul campo, non su un nodo).
    if (watched == m_view && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        QGraphicsItem *focus = m_scene->focusItem();
        const bool editing = focus && !qgraphicsitem_cast<NodeItem *>(focus);
        const std::pair<const char *, Qt::Key> moves[] = {{"mapSelectLeft", Qt::Key_Left}, {"mapSelectRight", Qt::Key_Right},
                                                          {"mapSelectUp", Qt::Key_Up}, {"mapSelectDown", Qt::Key_Down}};
        for (const auto &[id, key] : moves) {
            if (editing || !m_shortcuts->matches(QString::fromLatin1(id), keyEvent)) continue;
            const QList<MindNode *> selected = m_scene->selectedNodes();
            MindNode *from = selected.size() == 1 ? selected.front() : (selected.isEmpty() ? m_model->root() : nullptr);
            if (MindNode *to = m_scene->neighbour(from, key)) {
                m_scene->selectNode(to);
                m_view->ensureVisible(m_scene->nodeSceneRect(to), 24, 24);
            } else if (selected.isEmpty()) {
                m_scene->selectNode(m_model->root());
            }
            return true;  // la vista non scorre con le frecce
        }
    }
    if (m_placingFloating && watched == m_view && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        cancelFloatingPlacement();
        return true;
    }
    // FEAT-060: fine di "Evidenzia argomenti collegati" (clic sulla mappa o Esc).
    if (m_labelEditor && !m_scene->labelHighlight().isEmpty() &&
        ((watched == m_view->viewport() && event->type() == QEvent::MouseButtonPress) ||
         (watched == m_view && event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape))) {
        m_scene->setLabelHighlight({});
        if (event->type() == QEvent::KeyPress) return true;
    }
    // Stato compatto del salvataggio nella barra (vedi updateMapSubtitle).
    if (watched == this && event->type() == QEvent::ModifiedChange) updateMapSubtitle();
    // Titolo della radice nell'outliner: se contiene formule e non è in modifica,
    // al posto del testo LaTeX grezzo si disegna il rendering.
    // QPlainTextEdit disegna il testo nel viewport, che è già rientrato del padding (8 + 2 px a
    // sinistra, come i 10 px di prima).
    if (m_outlineRootEdit && watched == m_outlineRootEdit->viewport() && event->type() == QEvent::Paint &&
        !m_outlineRootEdit->hasFocus() && !m_rootFormula.isEmpty()) {
        QPainter painter(m_outlineRootEdit->viewport());
        m_rootFormula.draw(&painter, QRectF(m_outlineRootEdit->viewport()->rect()), Qt::AlignLeft | Qt::AlignVCenter);
        return true;
    }
    // Il titolo principale perde il focus: la barra di formattazione sparisce.
    if (watched == m_outlineRootEdit && (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide) && m_rootFormatBarUpdate)
        QTimer::singleShot(0, this, [this] { if (m_rootFormatBarUpdate) m_rootFormatBarUpdate(); });
    if (watched == m_outline && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        // Più voci selezionate (2026-10-13): Canc le elimina tutte insieme, come sulla mappa
        // (QKeySequence::Delete); Esc torna alla sola voce corrente; frecce e Maiusc+frecce restano
        // alla vista. Gli altri tasti che scrivono, spostano o creano voci non fanno nulla, Backspace
        // compreso (con una voce riduce il rientro, AGENTS.md "Menu contestuale dell'outliner").
        if (static_cast<OutlineTreeView *>(m_outline)->hasMultipleSelection()) {
            if (m_shortcuts->matches("outlineDelete", keyEvent)) { deleteOutlineNode(); return true; }
            // FEAT-042 (2026-11-05): Copy and Cut take all the selected items.
            if (m_shortcuts->matches("outlineCopy", keyEvent)) { copyOutlineNode(); return true; }
            if (m_shortcuts->matches("outlineCut", keyEvent)) { cutOutlineNode(); return true; }
            if (keyEvent->key() == Qt::Key_Escape) {
                m_outline->selectionModel()->select(m_outline->currentIndex(), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                return true;
            }
            const bool navigation = keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down ||
                                    keyEvent->key() == Qt::Key_Home || keyEvent->key() == Qt::Key_End ||
                                    keyEvent->key() == Qt::Key_PageUp || keyEvent->key() == Qt::Key_PageDown ||
                                    keyEvent->matches(QKeySequence::SelectAll);
            if (!navigation) return true;
        }
        // FEAT-041: tasti dell'area Outliner dal registro (prima fissi qui). Scorciatoie mostrate nel
        // menu contestuale, valide anche senza aprirlo; intercettate prima di QTreeView, che con
        // Ctrl+C copierebbe solo il testo della riga.
        const std::pair<const char *, void (MainWindow::*)()> outlinerCommands[] = {
            {"outlineNewItem", &MainWindow::addOutlineSibling}, {"outlineIndent", &MainWindow::indentOutlineNode},
            {"outlineOutdent", &MainWindow::outdentOutlineNode}, {"outlineCopy", &MainWindow::copyOutlineNode},
            {"outlineCut", &MainWindow::cutOutlineNode}, {"outlinePaste", &MainWindow::pasteOutlineNode},
            {"outlineDuplicate", &MainWindow::duplicateOutlineNode}, {"outlineDelete", &MainWindow::deleteOutlineNode},
        };
        for (const auto &[id, slot] : outlinerCommands)
            if (m_shortcuts->matches(id, keyEvent)) { (this->*slot)(); return true; }
        const bool hasCommandModifier = keyEvent->modifiers().testFlag(Qt::ControlModifier) ||
                                        keyEvent->modifiers().testFlag(Qt::AltModifier) ||
                                        keyEvent->modifiers().testFlag(Qt::MetaModifier);
        const QModelIndex current = m_outline->currentIndex();
        if (current.isValid() && !hasCommandModifier && keyEvent->key() == Qt::Key_Space) {
            const QString appended = current.data(Qt::EditRole).toString() + ' ';
            // subito nel modello, quindi Esc non tornava al testo di prima e si perdeva la formattazione)
            m_outline->setCurrentIndex(current);
            m_outline->edit(current);
            // Il testo va solo nel campo aperto: diventa titolo quando si conferma, Esc lo scarta.
            if (auto *editor = dynamic_cast<OutlineTitleEditor *>(m_outline->indexWidget(current))) editor->setText(appended);
            else m_model->setData(current, appended, Qt::EditRole);  // campo non aperto: comportamento precedente
            QTimer::singleShot(0, m_outline, [this] {
                if (auto *editor = dynamic_cast<OutlineTitleEditor *>(m_outline->indexWidget(m_outline->currentIndex())))
                    editor->setCursorPosition(editor->text().size());
            });
            return true;
        }
        if (current.isValid() && !hasCommandModifier && !keyEvent->text().isEmpty() &&
            keyEvent->text().at(0).isPrint()) {
            m_outline->setCurrentIndex(current);
            m_outline->edit(current);
            if (auto *editor = dynamic_cast<OutlineTitleEditor *>(m_outline->indexWidget(current))) editor->setText(keyEvent->text());
            else m_model->setData(current, keyEvent->text(), Qt::EditRole);  // campo non aperto: comportamento precedente
            QTimer::singleShot(0, m_outline, [this] {
                if (auto *editor = dynamic_cast<OutlineTitleEditor *>(m_outline->indexWidget(m_outline->currentIndex())))
                    editor->setCursorPosition(editor->text().size());
            });
            return true;
        }
    }
    if (watched->property("windowDragArea").toBool()) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                isMaximized() ? showNormal() : showMaximized();
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && windowHandle()) {
                if (isMaximized()) showNormal();
                windowHandle()->startSystemMove();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // Riga dell'outliner in modifica: il testo scritto entra nel modello prima del controllo
    // "salvare le modifiche?". Il delegate conferma alla perdita del focus.
    // 2026-10-14: conferma diretta di tutte le modifiche aperte, anche quella nel nodo della mappa
    // (prima andava persa: la conferma arrivava dopo la chiusura) e del titolo dell'outliner.
    commitPendingEdits();
    m_document->flushPendingStep();
    const bool hadUnsavedChanges = m_document->isDirty();
    const QString file = m_document->filePath();
    // Closing (2026-11-04): a named file is saved without asking and an unchanged untitled map asks
    // nothing, so the window leaves the screen now; the closing save, the Home preview and the
    // teardown of the widgets then run out of sight. It comes back only to show a failed save.
    if (!file.isEmpty() || !hadUnsavedChanges) hide();
    // (B5/B6, 2026-10-13: un salvataggio fallito alla chiusura annullava sempre la chiusura, anche
    // senza modifiche, e la finestra non si chiudeva più: file di sola lettura, immagine sparita.)
    if (!file.isEmpty() && hadUnsavedChanges) {
        const Document::Result result = m_document->save(file, BmapStorage::SaveReason::Closing);
        if (!result.ok) {
            show();
            ModalDialog::inform(this, tr("Save error"), result.error);
            const auto choice = ModalDialog::ask(this, tr("Save failed"),
                tr("The latest changes have not been saved. Close anyway?"), tr("Close"));
            if (choice == ModalDialog::Choice::Primary) event->accept(); else event->ignore();
            return;
        }
        rememberRecent(file);
    }
    if (!file.isEmpty() && !hadUnsavedChanges) {
        // Senza modifiche serve solo il punto di ripristino "closing": se non si può scrivere
        // (es. file di sola lettura) si chiude lo stesso, perché non si perde nulla.
        // 2026-10-14: nessun foglio viene riscritto (sono già nel file): solo punto e anteprima.
        m_document->writeClosingPoint();
    }
    if (maybeSave(false)) {  // l'anteprima è già stata scritta con il salvataggio di chiusura
        hide();
        m_document->close();  // copia di lavoro di un documento senza nome e blocco del file
        event->accept();
    } else {
        show();
        event->ignore();
    }
}

// Conferma le modifiche di testo aperte (2026-10-14). Prima andavano perse cambiando foglio,
// aggiungendo un foglio o chiudendo la finestra, e Ctrl+S salvava il titolo vecchio.
void MainWindow::openResources() {
    commitPendingEdits();
    ResourcesDialog dialog(this, m_document->resources(), m_document->model()->gantt().calendar());
    if (dialog.exec() == QDialog::Accepted) m_document->setResources(dialog.resources(), dialog.calendar());
}

void MainWindow::commitPendingEdits() {
    if (m_scene) m_scene->commitEditing();
    if (m_ganttView) m_ganttView->commitPendingEdit();  // FEAT-075
    if (m_outline)
        if (auto *delegate = dynamic_cast<OutlineDelegate *>(m_outline->itemDelegate())) delegate->commitOpenEditor();
    if (m_outlineRootEdit && m_outlineRootEdit->hasFocus())
        if (auto *rootEdit = static_cast<OutlineRootTitleEdit *>(m_outlineRootEdit); rootEdit->editingFinished) rootEdit->editingFinished();
}

void MainWindow::pointToSheet(const QString &sheetId, bool redo) {
    const int index = sheetIndex(sheetId);
    if (index < 0) return;
    if (auto *select = dynamic_cast<FloatingSelect *>(m_sheetCombo)) select->flash();
    const QString name = m_document->sheets()[index].name;
    statusBar()->showMessage(redo ? tr("The change to redo is in the sheet \"%1\"").arg(name)
                                  : tr("The change to undo is in the sheet \"%1\"").arg(name), 3000);
}

// Anteprima del foglio attivo per la Home: stessa resa di recentMapPreview (480x272, margine 16,
// sfondo bianco), da una scena separata per non toccare selezione e hover della mappa aperta.
QByteArray MainWindow::documentPreviewJpeg() const {
    // The Home shows the view the map was left in: a small Gantt after the Gantt, the map after the
    // map or the outliner (user, 2026-11-02).
    if (workspaceView() == WorkspaceView::Gantt)
        if (const QByteArray gantt = ganttPreviewJpeg(); !gantt.isEmpty()) return gantt;
    // Contenuto già pronto nel foglio (il Document lo aggiorna a ogni passo): nessuna serializzazione in più.
    const int current = m_document->currentSheet();
    const QVector<Document::Sheet> &sheets = m_document->sheets();
    return MindMapScene::homePreviewJpeg(current >= 0 && current < sheets.size() && !sheets[current].content.isEmpty()
                                             ? sheets[current].content : m_model->toFlatSnapshot());
}

// The Gantt as exported (table and chart, the view's time unit), from its top left corner: the
// preview stays readable on a long plan, as the start of the page the user saw.
QByteArray MainWindow::ganttPreviewJpeg() const {
    bool reduced = false;
    QString error;
    const QImage gantt = GanttPages::toImage(m_ganttView->exportSource(), m_ganttView->scale(), 1, &reduced, &error);
    if (gantt.isNull()) return {};
    constexpr int kMargin = 16;
    const int width = MindMapScene::kPreviewWidth - 2 * kMargin;
    const int height = MindMapScene::kPreviewHeight - 2 * kMargin;
    // Half the exported size at most, so text and bars keep a recognisable size.
    const QImage shown = gantt.scaled(std::min(width, gantt.width() / 2), gantt.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage preview(MindMapScene::kPreviewWidth, MindMapScene::kPreviewHeight, QImage::Format_RGB32);
    preview.fill(gantt.pixelColor(0, 0));  // the export's own page around it: no card on another colour
    QPainter painter(&preview);
    painter.drawImage(QPoint(kMargin, kMargin), shown, QRect(0, 0, std::min(width, shown.width()), std::min(height, shown.height())));
    painter.end();
    return MindMapScene::previewJpeg(preview);
}

void MainWindow::storeDocumentPreview() {
    // Solo per un documento aperto nell'editor (il Document controlla nome e modifiche).
    if (!m_pages || m_pages->currentWidget() != m_editorPage) return;
    m_document->storePreview();
}

void MainWindow::applyPerformanceProfile() {
    m_document->setAutoSaveDelay(Performance::autoSaveDelayMs());
    if (m_view) m_view->setRenderHint(QPainter::SmoothPixmapTransform, Performance::smoothPixmaps());
    QPixmapCache::setCacheLimit(Performance::pixmapCacheKb());
    if (m_scene) m_scene->update();
}

// Pannello laterale (2026-10-14, richiesta dell'utente): aprendolo compare subito la
// cornice scura attorno all'editor su tutti i lati, poi il pannello scorre da destra a sinistra
// fino alla sua larghezza. Solo con il profilo Standard; con il Leggero compare di colpo come prima.
// La larghezza si anima con setFixedWidth sul dock: QMainWindow ridispone l'editor a ogni passo.
void MainWindow::toggleInspector() {
    ensureInspector();
    if (m_inspectorDock->isVisible()) {
        m_inspectorDock->hide();  // visibilityChanged ferma l'eventuale animazione
        return;
    }
    preparePanelForOpening();
    m_inspectorFollowsSelection = workspaceView() == WorkspaceView::Map && m_scene->selectedNode();
    if (!Performance::animationsEnabled()) {
        m_inspectorDock->show();
        if (QLayout *windowLayout = layout()) windowLayout->activate();  // the view narrows now, not later
        m_inspectorFollowsSelection = false;
        return;
    }
    constexpr int kFrame = 8;          // cornice scura (kEditorFrame nel lambda di visibilityChanged)
    constexpr int kHoldMs = 80;        // pausa: prima si vede la cornice, poi parte il pannello
    constexpr int kSlideMs = 260;      // scorrimento; in tutto circa un terzo di secondo
    m_inspectorAnimating = true;
    m_inspectorDock->setFixedWidth(kFrame);  // larghezza esplicita: vale anche sotto il minimo del contenuto
    m_inspectorDock->show();
    auto *animation = new QVariantAnimation(this);
    animation->setStartValue(kFrame);
    m_inspectorWidth = inspectorMinimumWidth();
    animation->setEndValue(m_inspectorWidth);
    animation->setDuration(kSlideMs);
    animation->setEasingCurve(QEasingCurve::OutCubic);  // parte svelto e rallenta all'arrivo
    connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        if (m_inspectorAnimating) m_inspectorDock->setFixedWidth(value.toInt());
    });
    connect(animation, &QVariantAnimation::finished, this, [this] { stopInspectorAnimation(); });
    m_inspectorAnimation = animation;
    QTimer::singleShot(kHoldMs, animation, [animation] { animation->start(QAbstractAnimation::DeleteWhenStopped); });
}

QStringList MainWindow::selectedNodeIds() const {
    QStringList ids;
    for (const MindNode *node : m_scene->selectedNodes()) ids.append(node->id);
    ids.append(m_scene->selectedRelationIds());  // FEAT-053: a selected relationship opens Style too
    ids.append(m_scene->selectedBoundaryIds());  // FEAT-052: so does a boundary
    return ids;
}

// FEAT-052: context menu of a boundary (it is already selected).
void MainWindow::showBoundaryMenu(const QString &id, const QPoint &screenPos) {
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("BoundaryMenu"));
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(TextEditMenu::styleSheet());
    QAction *edit = menu.addAction(tr("Edit title"));
    connect(edit, &QAction::triggered, this, [this, id] { if (auto *item = m_scene->boundaryItem(id)) item->beginEditing(); });
    QAction *remove = menu.addAction(tr("Delete"));
    remove->setShortcuts(m_shortcuts->shortcuts("remove"));
    remove->setShortcutVisibleInContextMenu(true);
    // Queued: the menu runs inside the item's own event, which must not be deleted under it.
    connect(remove, &QAction::triggered, this, [this] { QTimer::singleShot(0, this, [this] { removeSelected(); }); });
    menu.exec(screenPos);
}

// FEAT-053: context menu of a relationship (it is already selected).
void MainWindow::showRelationMenu(const QString &id, const QPoint &screenPos) {
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("RelationMenu"));
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(TextEditMenu::styleSheet());
    QAction *edit = menu.addAction(tr("Edit label"));
    connect(edit, &QAction::triggered, this, [this, id] { if (auto *item = m_scene->relationItem(id)) item->beginEditing(); });
    QAction *remove = menu.addAction(tr("Delete"));
    remove->setShortcuts(m_shortcuts->shortcuts("remove"));
    remove->setShortcutVisibleInContextMenu(true);
    // Queued: the menu runs inside the item's own event, which must not be deleted under it.
    connect(remove, &QAction::triggered, this, [this] { QTimer::singleShot(0, this, [this] { removeSelected(); }); });
    menu.exec(screenPos);
}

// FEAT-017: la selezione della scena decide la pagina contestuale. Nessun dato viene scritto e
// nessun passo di storico creato; cambiare pagina mostra soltanto un widget gia' esistente.
void MainWindow::updatePanelContext() {
    updateIconPanelSelection();  // FEAT-051: marcatori accesi del nodo selezionato
    if (!m_sidePanel) return;
    showPanelPage(m_panelContext.selectionChanged(selectedNodeIds()));
}

// Riaprendo il pannello la pagina si ricalcola e la scelta manuale precedente non resta.
void MainWindow::preparePanelForOpening() {
    if (!m_sidePanel) return;
    showPanelPage(m_panelContext.reopened(selectedNodeIds()));
}

void MainWindow::showPanelPage(PanelContext::Page page) {
    if (!m_sidePanel) return;
    m_sidePanel->setCurrentPage(static_cast<int>(page));
    // I vecchi controlli nascosti agiscono su un solo nodo; la pagina Stile vera (FEAT-019) vale
    // per uno o piu' nodi e decide da se' cosa abilitare (StylePage::setNodes).
    const bool enabled = m_panelContext.styleControlsEnabled();
    for (QWidget *control : std::as_const(m_styleControls)) control->setEnabled(enabled);
    refreshStylePage();
}

// FEAT-036: sfondo del foglio sulla mappa (scena e superficie dell'editor), linee delle annotazioni
// e outliner, chiaro o scuro neutro secondo lo sfondo.
void MainWindow::applyBackdrop() {
    const QColor background = m_model->backdrop().color;
    m_scene->setBackgroundBrush(background);
    updateWindowFrame(m_inspectorDock && m_inspectorDock->isVisible());
    if (m_view) m_view->setStyleSheet(m_view->styleSheet().replace(QRegularExpression(QStringLiteral("QGraphicsView \\{ background:#[0-9a-fA-F]{6};")),
                                                                   QStringLiteral("QGraphicsView { background:%1;").arg(background.name())));
    // Richiesta dell'utente 2026-10-19 (foto di riferimento): i controlli della finestra e il menu, che non
    // stanno in un gruppo chiaro, sono chiari sugli sfondi scuri e scuri su quelli chiari.
    const bool darkMap = NodeStyle::isDarkBackground(background);
    styleWindowControls(m_windowControls, darkMap);
    const QList<QToolButton *> buttons = findChildren<QToolButton *>();
    const auto main = std::find_if(buttons.cbegin(), buttons.cend(), [](QToolButton *b) { return b->property("mainMenuButton").toBool(); });
    if (QToolButton *menuButton = main != buttons.cend() ? *main : nullptr) {
        QPixmap icon = QIcon(QStringLiteral(":/assets/hugeicons/menu.svg")).pixmap(48, 48);
        if (darkMap) {
            QPainter tint(&icon);
            tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tint.fillRect(icon.rect(), UiTheme::color(UiTheme::Token::Text));
        }
        menuButton->setIcon(QIcon(icon));
        // Hover come i bottoni dei gruppi (FloatingUi::Tone): chiaro sugli sfondi scuri.
        menuButton->setStyleSheet(QStringLiteral("QToolButton{background:transparent;border:0;border-radius:6px;padding:0;}"
                                                 "QToolButton:hover{background:%1;} QToolButton::menu-indicator{image:none;}")
                                      .arg(darkMap ? QStringLiteral("rgba(255,255,255,26)") : QStringLiteral("rgba(15,23,42,15)")));
    }
    m_scene->refreshAnnotations();
    outlineColors() = NodeStyle::isDarkBackground(background) ? darkOutlineColors() : lightOutlineColors();
    if (m_ganttView) m_ganttView->setDark(NodeStyle::isDarkBackground(background));  // FEAT-075, like the outliner
    if (m_manual) m_manual->setDark(darkMap);  // FEAT-092, like the outliner
    applyFloatingTone();
    if (m_outlinePage) m_outlinePage->setStyleSheet(QStringLiteral("background:%1;").arg(outlineColors().page.name()));
    if (m_outline) {
        applyOutlineZoom(m_outlineZoom);
        m_outline->viewport()->update();
    }
    refreshStylePage();
}

// FEAT-036 (richiesta dell'utente 2026-10-19): a pannello chiuso la mappa arriva al bordo della
// finestra, senza il filo di 1 px; a pannello aperto la cornice scura resta com'era. La superficie
// dell'editor ha sempre lo sfondo della mappa (prima tornava bianca aprendo o chiudendo il pannello).
// 2026-10-19: senza fogli di stile (prima ~200 ms per apertura e chiusura del pannello): la
// superficie e la finestra si disegnano da se' (EditorSurface, MainWindow::paintEvent).
void MainWindow::applyEditorFrame(bool inspectorVisible) {
    // With a side panel open the editor becomes a white rounded card inside a dark frame (window
    // background); with both closed it fills the window again.
    constexpr int kEditorFrame = 8;
    const bool navigationVisible = m_navigationDock && m_navigationDock->isVisible();
    const bool framed = inspectorVisible || navigationVisible;
    const int frame = framed ? kEditorFrame : 0;
    // createDocks() gira prima della creazione di m_pages: il segnale può arrivare
    // durante il costruttore (m_inspectorDock->hide()), quando m_pages non esiste ancora.
    if (m_pages) m_pages->setContentsMargins(frame, frame, inspectorVisible ? 0 : frame, frame);
    updateWindowFrame(framed);  // FEAT-036: superficie dello sfondo della mappa e bordo
    // I controlli finestra seguono la barra dell'editor, spostata in basso dalla cornice.
    if (m_inspectorTitleLayout) {
        QMargins margins = m_inspectorTitleLayout->contentsMargins();
        margins.setTop(19 + (inspectorVisible ? kEditorFrame : 0));
        m_inspectorTitleLayout->setContentsMargins(margins);
    }
    if (!m_windowControls) return;
    // 2026-10-14 (richiesta dell'utente): il pannello ha i propri controlli fissi
    // (m_panelWindowControls); aprendolo spariscono solo quelli della barra dell'editor.
    m_windowControls->setVisible(!inspectorVisible);
}

void MainWindow::showNavigationPanel() {
    if (m_pages->currentWidget() != m_editorPage) return;
    commitPendingEdits();  // the search reads the saved form of the map
    if (!m_navigationDock->isVisible()) {
        m_navigationDock->show();
        resizeDocks({m_navigationDock}, {kNavigationWidthPx}, Qt::Horizontal);
    }
    m_navigationPanel->showTab(NavigationPanel::Tab::Outline);
}

void MainWindow::updateWindowFrame(bool panelVisible) {
    const QColor background = m_model ? pageColor() : NodeStyle::kMapBackground;
    if (auto *surface = dynamic_cast<EditorSurface *>(findChild<QWidget *>(QStringLiteral("editorSurface")))) surface->setLook(background, panelVisible ? 8 : 0);
    const bool mapEdge = !panelVisible && m_pages && m_pages->currentWidget() == m_editorPage;
    const QColor frame = mapEdge ? background : UiTheme::color(UiTheme::Token::SurfaceSunken);
    if (frame != m_frameColor || mapEdge != m_frameMapEdge) {
        m_frameColor = frame;
        m_frameMapEdge = mapEdge;
        update();
    }
}

// Sfondo della finestra: cornice scura con il filo di bordo, o lo sfondo della mappa fino al bordo.
void MainWindow::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    const QColor fill = m_frameColor.isValid() ? m_frameColor : UiTheme::color(UiTheme::Token::SurfaceSunken);
    // BUG-030: filling the screen, the whole window is the frame, without border or rounded corners:
    // outside the radius, and under the half-pixel border, the window's white background showed
    // (two white marks at the top in focus mode).
    if (isMaximized() || isFullScreen()) {
        painter.fillRect(rect(), fill);
        QMainWindow::paintEvent(event);
        return;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(fill);
    painter.setPen(m_frameMapEdge ? QPen(Qt::NoPen) : QPen(UiTheme::color(UiTheme::Token::Divider), 1));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    QMainWindow::paintEvent(event);
}

// FEAT-036: tema delle mappe nuove dalle Impostazioni (Editor), all'inizio Alba con sfondo bianco.
void MainWindow::applyDefaultTheme() {
    const CustomPalettes::DefaultTheme theme = CustomPalettes::defaultTheme();
    const bool custom = theme.palette.group == NodeStyle::PaletteGroup::Personalizzata;
    m_model->setTheme(theme.palette.id, theme.palette.colors, custom ? theme.palette.name : QString(), theme.variant);
}

// FEAT-032: "Crea nuovo tema colore". La palette va nelle impostazioni dell'app e si applica al
// foglio con un passo di storico.
void MainWindow::createPalette() {
    NewPaletteDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString name = dialog.paletteName();
    const QString id = CustomPalettes::add(name, dialog.colors());
    if (id.isEmpty()) return;
    m_model->setPalette(id, dialog.colors(), name);
}

// FEAT-040: misura dell'immagine scelta dalla pagina Stile (un passo di storico), poi ridisposizione.
void MainWindow::resizeSelectedImage(const QSizeF &size) {
    MindNode *node = m_scene->selectedImageNode();
    if (!node || !m_document->setImageSize(node, size)) { refreshStylePage(); return; }
    // Una larghezza scelta a mano piu' stretta del nuovo contenuto diventa quella necessaria.
    if (node->customWidth > 0) {
        const qreal needed = m_scene->nodeWidth(node);
        if (needed > node->customWidth) { node->customWidth = std::min(std::ceil(needed), kMaxManualNodeWidthPx); m_scene->updateNode(node); }
    }
    m_scene->autoLayout();
    m_document->flushPendingStep();
    refreshStylePage();
}

// The topic the Pitch tab works on: the selected one, the central topic when nothing is selected
// (user's request 2026-10-26), none with several selected.
MindNode *MainWindow::pitchTopic() const {
    const QList<MindNode *> selected = m_scene->selectedNodes();
    if (selected.isEmpty()) return m_model->root();
    return selected.size() == 1 ? selected.front() : nullptr;
}

void MainWindow::refreshPitchPage() {
    if (m_pitchPage) m_pitchPage->showTopic(m_model, pitchTopic());
}

// FEAT-063: the presentation of the active sheet, full screen on Mentis's screen. Esc selects the
// topic of the last slide shown.
void MainWindow::presentPitch() {
    // One presentation at a time: F5 held a moment (auto-repeat) or a second trigger opened another
    // window behind the first, which seemed to reopen after Esc (user's report 2026-10-26).
    if (m_pitchWindow) {
        m_pitchWindow->activateWindow();
        return;
    }
    commitPendingEdits();
    const QList<Pitch::Slide> slides = Pitch::slides(*m_model);
    if (slides.isEmpty()) {
        statusBar()->showMessage(tr("No slides: every slide of this sheet is hidden"), 4000);
        return;
    }
    auto *window = new PitchWindow(slides, Pitch::sheetLook(*m_model),
                                   Pitch::aspectFromKey(m_model->pitchAspect), screen());
    m_pitchWindow = window;  // QPointer: cleared when the window deletes itself on close
    // After the presentation is gone (queued): the map selects the last topic and takes the keyboard.
    connect(window, &PitchWindow::finished, this, [this](const QString &topicId) {
        m_pitchWindow = nullptr;
        if (MindNode *node = m_model->nodeById(topicId)) m_scene->selectNode(node);
        raise();
        activateWindow();
        if (m_view) m_view->setFocus();
    }, Qt::QueuedConnection);
    window->showFullScreen();
    window->activateWindow();
    window->setFocus();
}

void MainWindow::refreshStylePage() {
    refreshPitchPage();
    if (!m_stylePage) return;
    if (m_imageStylePage) {
        const MindNode *image = m_scene->selectedImageNode();
        m_imageStylePage->setNode(image);
        m_imageStylePage->setVisible(image);
        m_stylePage->setVisible(!image);
        if (image) {
            if (m_relationStylePage) m_relationStylePage->hide();
            if (m_boundaryStylePage) m_boundaryStylePage->hide();
            return;
        }
    }
    if (m_boundaryStylePage) {
        QList<Boundary> boundaries;
        if (m_scene->selectedNodes().isEmpty() && m_scene->selectedRelationIds().isEmpty())
            for (const QString &id : m_scene->selectedBoundaryIds())
                if (const Boundary *boundary = m_document->boundary(id)) boundaries.append(*boundary);
        const QColor ink = NodeStyle::annotationColor(Qt::black, m_model->backdrop());
        QColor line = ink, fill = ink;
        line.setAlphaF(Boundaries::kLineAlpha);
        fill.setAlphaF(Boundaries::kFillAlpha);
        m_boundaryStylePage->setBoundaries(boundaries, line, fill);
        m_boundaryStylePage->setVisible(!boundaries.isEmpty());
        if (!boundaries.isEmpty()) {
            if (m_relationStylePage) m_relationStylePage->hide();
            m_stylePage->setVisible(false);
            return;
        }
    }
    if (m_relationStylePage) {
        QList<Relationship> relationships;
        if (m_scene->selectedNodes().isEmpty())
            for (const QString &id : m_scene->selectedRelationIds())
                if (const Relationship *relationship = m_document->relation(id)) relationships.append(*relationship);
        const NodeStyle::Backdrop backdrop = m_model->backdrop();
        m_relationStylePage->setRelationships(relationships, NodeStyle::annotationColor(Relationships::kDefaultLineColor, backdrop),
                                              NodeStyle::annotationColor(QColor("#303640"), backdrop));
        m_relationStylePage->setVisible(!relationships.isEmpty());
        m_stylePage->setVisible(relationships.isEmpty());
        if (!relationships.isEmpty()) return;
    }
    QList<const MindNode *> nodes;
    QList<qreal> widths;  // FEAT-021: misura gia' fatta dalla scena, nessuna nuova impaginazione
    for (const MindNode *node : m_scene->selectedNodes()) {
        nodes.append(node);
        widths.append(m_scene->nodeWidth(node));
    }
    m_stylePage->setNodes(nodes, m_model->activePalette, widths);
    if (m_sheetVariant) {
        // The card names the sheet type: after a type change from the central topic's Structure
        // section the variant may belong to another type; then the first variant of the type is shown.
        QString shown = m_model->sheetStyle.variant;
        const MapStructure::Type type = MapStructure::typeFromKey(m_model->root()->structure);
        const MapVariants::Variant *variant = MapVariants::find(shown);
        if (!variant || variant->type != type) {
            const QList<const MapVariants::Variant *> ofType = MapVariants::ofType(type);
            shown = ofType.isEmpty() ? QString() : ofType.first()->id;
        }
        m_sheetVariant->setVariant(shown);
    }
    if (m_paletteSelect) m_paletteSelect->setPalette(m_model->paletteId, m_model->activePalette, m_model->paletteName);
    if (m_sheetStylePanel) {
        m_sheetStylePanel->setSheetStyle(m_model->sheetStyle, m_model->root()->text);
        m_sheetStylePanel->setResetMapEnabled(m_model->hasMapStyle());  // FEAT-025
        m_sheetStylePanel->numberingFields()->setNodes({m_model->root()});
    }
    if (m_balanceSwitch) {  // FEAT-013: stato del foglio attivo, senza passi di storico
        const QSignalBlocker blocker(m_balanceSwitch);
        // Con una direzione a un solo lato non c'e' nulla da bilanciare: visibile ma spento (FEAT-026).
        const bool twoSided = MapStructure::isTwoSided(MapStructure::directionOf(m_model->root()));
        m_balanceSwitch->setEnabled(twoSided && !m_model->sheetStyle.freeBranches);
        m_balanceSwitch->setChecked(twoSided && m_model->balanced);
    }
    // FEAT-059: "Mostra tutte le note" del foglio attivo.
    if (NodeItem::showAllNotes() != m_model->sheetStyle.showNotes) {
        NodeItem::showAllNotes() = m_model->sheetStyle.showNotes;
        m_scene->autoLayout();  // le note sotto i nodi fanno spazio (i vicini si spostano)
    }
    if (m_variantPicker) m_variantPicker->setTheme(m_model->paletteId, m_model->activePalette, m_model->paletteVariant);
    if (m_backgroundField) {
        NodeStyle::Reading reading;
        reading.empty = false;
        reading.value = m_model->backdrop().color;
        reading.allManual = m_model->background.has_value() || m_model->paletteVariant >= 0;
        m_backgroundField->setReading(reading);
    }
}

int MainWindow::inspectorMinimumWidth() const {
    // From the content: the dock's own hint is capped by its fixed width (FEAT-072 B1).
    if (!m_inspectorDock || !m_inspectorDock->widget()) return kInspectorMinimumWidth;
    const QMargins margins = m_inspectorDock->contentsMargins();
    return std::max(kInspectorMinimumWidth, m_inspectorDock->widget()->minimumSizeHint().width() + margins.left() + margins.right());
}

void MainWindow::applyInspectorMinimumWidth() {
    if (!m_inspectorDock || m_inspectorAnimating) return;  // a fine animazione si riapplica
    // FEAT-072 B1: fixed width (no separator to drag), as narrow as the content allows.
    m_inspectorWidth = inspectorMinimumWidth();
    m_inspectorDock->setFixedWidth(m_inspectorWidth);
}

void MainWindow::stopInspectorAnimation() {
    if (!m_inspectorAnimating) return;
    m_inspectorAnimating = false;
    if (m_inspectorAnimation) {
        QVariantAnimation *animation = m_inspectorAnimation;
        m_inspectorAnimation = nullptr;
        animation->disconnect(this);
        if (animation->state() == QAbstractAnimation::Running) animation->stop();  // DeleteWhenStopped
        else animation->deleteLater();  // ancora nella pausa iniziale
    }
    // FEAT-072 B1: fixed width, the content's minimum (the separator cannot change it).
    m_inspectorWidth = inspectorMinimumWidth();
    m_inspectorDock->setFixedWidth(m_inspectorWidth);
    if (QLayout *windowLayout = layout()) windowLayout->activate();  // last narrowing still followed
    m_inspectorFollowsSelection = false;
}

void MainWindow::keepSelectionBesidePanel() {
    if (!m_inspectorFollowsSelection) return;
    if (MindNode *node = m_scene->selectedNode()) m_view->reveal(m_scene->nodeSceneRect(node), 24);
}


// Durante la distruzione dei widget figli il campo di modifica dell'outliner perde il focus
// e il delegate confermerebbe il testo: setData() -> setDirty() girerebbe su una finestra già
// in parte distrutta (crash visto nei test). Si scollega il delegate prima che succeda.
MainWindow::~MainWindow() {
    // Without a parent until the side panel is built (ensureInspector), which may never happen.
    if (m_panelWindowControls && !m_panelWindowControls->parent()) {
        m_maximizeButtons.clear();
        delete m_panelWindowControls;
    }
    // FEAT-049: le copie temporanee dei file aperti non servono piu' (gia' salvate nel documento).
    for (auto it = m_openedFiles.cbegin(); it != m_openedFiles.cend(); ++it) QDir(QFileInfo(it.key()).path()).removeRecursively();
    // Il Document (figlio della finestra) aspetta il salvataggio automatico in corso quando viene distrutto.
    QSettings().setValue("session/running", false);  // P8: chiusura regolare
    if (m_outline && m_outline->itemDelegate()) m_outline->itemDelegate()->disconnect(m_outline);
    // FEAT-017: la scena viene distrutta dopo i widget; svuotandosi notifica un cambio di selezione
    // che arriverebbe al pannello laterale gia' distrutto (crash in chiusura con un nodo selezionato).
    disconnect(m_scene, nullptr, this, nullptr);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    updateCornerMask();
}

void MainWindow::updateCornerMask() {
    if (isMaximized() || isFullScreen() || m_systemCorners) {
        clearMask();
        return;
    }
    QPainterPath outline;
    outline.addRoundedRect(QRectF(rect()), 8.0, 8.0);
    setMask(QRegion(outline.toFillPolygon().toPolygon()));
}

