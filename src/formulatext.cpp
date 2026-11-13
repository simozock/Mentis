#include "formulatext.h"
#include "formularendering.h"
#include <cmath>
#include <QCoreApplication>
#include <QUrl>
#include <QRegularExpression>
#include <QTextImageFormat>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include "jkqtmathtext/jkqtmathtext.h"
#include "jkqtmathtext/jkqtmathtexttools.h"

#include <QFontDatabase>
#include <QHash>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>

namespace {
// Un solo motore condiviso: il parser e le tabelle dei font si inizializzano una
// volta per tutta l'applicazione.
JKQTMathText &engine() {
    static JKQTMathText *instance = [] {
        // Registra Fira Math prima di useFiraMath(): JKQTMathText legge l'elenco dei
        // font prima di aggiungerlo, quindi alla prima chiamata non lo troverebbe e
        // userebbe un font serif di sistema (diverso su ogni PC).
        initJKQTMathTextResources();
        QFontDatabase::addApplicationFont(QStringLiteral(":/JKQTMathText/fonts/FiraMath-Regular.otf"));
        auto *mathText = new JKQTMathText();
        // Fira Math (inclusa come risorsa) solo per le formule: il testo normale resta
        // nel font dell'applicazione, così lo stile dei nodi non cambia.
        mathText->useFiraMath(true, true);
        return mathText;
    }();
    return *instance;
}

// Converte un pezzo di testo normale nella sintassi del parser, proteggendo i
// caratteri che in LaTeX hanno un significato speciale.
QString escapeText(const QString &text) {
    QString out;
    out.reserve(text.size() + 8);
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '\\': out += QStringLiteral("\\backslash{}"); break;
        case '{': case '}': case '%': case '&': case '#': case '_': case '$':
            out += '\\'; out += c; break;
        case '^': out += QStringLiteral("\\circonflex{}"); break;
        case '~': out += QStringLiteral("\\~{}"); break;
        default: out += c;
        }
    }
    return out;
}

// Comandi che in LaTeX accettano argomenti senza graffe (es. \frac12, \frac{\pi}8,
// \sqrt2). JKQTMathText invece vuole sempre {...}: senza, segnala un errore e non
// disegna il comando. Valore = numero di argomenti obbligatori.
int mathCommandArity(const QString &name) {
    static const QHash<QString, int> arity = {
        {"frac", 2}, {"dfrac", 2}, {"tfrac", 2}, {"cfrac", 2},
        {"binom", 2}, {"dbinom", 2}, {"tbinom", 2},
        {"stackrel", 2}, {"overset", 2}, {"underset", 2},
        {"sqrt", 1}, {"hat", 1}, {"widehat", 1}, {"bar", 1}, {"vec", 1}, {"dot", 1},
        {"ddot", 1}, {"tilde", 1}, {"widetilde", 1}, {"overline", 1}, {"underline", 1},
        {"overbrace", 1}, {"underbrace", 1}, {"mathrm", 1}, {"mathbf", 1}, {"mathit", 1},
        {"mathcal", 1}, {"mathbb", 1}, {"mathsf", 1}, {"mathtt", 1}, {"mathfrak", 1},
        {"boldsymbol", 1}, {"text", 1}, {"textbf", 1}, {"textit", 1}, {"textrm", 1},
        {"mbox", 1}, {"operatorname", 1},
    };
    return arity.value(name, 0);
}

// Aggiunge le graffe mancanti attorno agli argomenti dei comandi elencati sopra,
// ricorsivamente: "\frac5{12}" -> "\frac{5}{12}", "\frac{\pi}8" -> "\frac{\pi}{8}".
// Un argomento senza graffe è, come in LaTeX, un solo carattere o un solo comando.
// Il resto della formula viene copiato invariato.
QString normalizeMath(const QString &math) {
    QString out;
    out.reserve(math.size() + 8);
    const int n = math.size();
    // Restituisce la fine (esclusa) del gruppo che inizia con open in posizione i.
    // Gruppo non chiuso: termina a fine formula (il parser segnalerà l'errore).
    auto groupEnd = [&](int i, QChar open, QChar close) {
        int depth = 0;
        for (int j = i; j < n; ++j) {
            if (math[j] == '\\') { ++j; continue; }
            if (math[j] == open) ++depth;
            else if (math[j] == close && --depth == 0) return j + 1;
        }
        return n;
    };
    int i = 0;
    while (i < n) {
        const QChar c = math[i];
        if (c != '\\') { out += c; ++i; continue; }
        int j = i + 1;
        while (j < n && math[j].isLetter()) ++j;
        if (j == i + 1) {
            // Simbolo di un carattere (\\, \{, \,, ...): copiato così com'è.
            out += math.mid(i, 2); i += 2; continue;
        }
        const QString name = math.mid(i + 1, j - i - 1);
        out += math.mid(i, j - i);
        i = j;
        const int args = mathCommandArity(name);
        if (args == 0) continue;
        auto skipSpaces = [&] { while (i < n && math[i].isSpace()) ++i; };
        // Argomento opzionale di \sqrt[n]{...}.
        if (name == QLatin1String("sqrt")) {
            skipSpaces();
            if (i < n && math[i] == '[') {
                const int end = groupEnd(i, '[', ']');
                const bool closed = math[end - 1] == ']' && end - 1 > i;
                out += '[' + normalizeMath(math.mid(i + 1, end - i - (closed ? 2 : 1))) + ']';
                i = end;
            }
        }
        for (int a = 0; a < args; ++a) {
            skipSpaces();
            if (i >= n) break;
            QString arg;
            if (math[i] == '{') {
                const int end = groupEnd(i, '{', '}');
                const bool closed = math[end - 1] == '}' && end - 1 > i;
                arg = math.mid(i + 1, end - i - (closed ? 2 : 1));
                i = end;
            } else if (math[i] == '\\') {
                int k = i + 1;
                while (k < n && math[k].isLetter()) ++k;
                if (k == i + 1) k = qMin(n, i + 2);
                arg = math.mid(i, k - i);
                i = k;
            } else {
                arg = math[i];
                ++i;
            }
            out += '{' + normalizeMath(arg) + '}';
        }
    }
    return out;
}

// Dispositivo raster minimo usato solo per le misure (metriche dei font a 96 dpi
// come lo schermo, a differenza di QPicture).
QImage &measureDevice() {
    static QImage device(1, 1, QImage::Format_ARGB32_Premultiplied);
    return device;
}
}

void FormulaText::update(const QString &text, const QFont &font, const QColor &color, qreal maxWidth) {
    if (!m_picture.isNull() && text == m_text && font == m_font && color == m_color && qFuzzyCompare(maxWidth, m_maxWidth)) return;
    m_text = text; m_font = font; m_color = color; m_maxWidth = maxWidth;

    JKQTMathText &mathText = engine();
    mathText.setFontRoman(font.family());
    // I font impostati da foglio di stile (es. outliner, 18px) hanno la dimensione in pixel.
    if (font.pixelSize() > 0) mathText.setFontSizePixels(font.pixelSize());
    else mathText.setFontSize(font.pointSizeF() > 0 ? font.pointSizeF() : 11.0);
    mathText.setFontColor(color);

    // Misura di un frammento già in sintassi del parser.
    auto measure = [&](const QString &markup) {
        mathText.parse(markup, JKQTMathText::DefaultParser, JKQTMathText::ParseOptions());
        QPainter painter(&measureDevice());
        return mathText.getSize(painter).width();
    };
    const QString bold = font.bold() ? QStringLiteral("\\textbf{%1}") : QStringLiteral("%1");

    // A capo automatico: le parole del testo e le formule intere sono "blocchi"
    // indivisibili; si riempie ogni riga fino a maxWidth e si va a capo con \\.
    struct Token { QString markup; bool spaceBefore; };
    QVector<Token> tokens;
    bool pendingSpace = false;
    for (const auto &segment : split(text)) {
        if (segment.math) {
            // sostituita per aggiungere le graffe agli argomenti scritti senza (\frac12).
            tokens.append({QStringLiteral("$%1$").arg(normalizeMath(segment.text)), pendingSpace});
            pendingSpace = false;
            continue;
        }
        QString word;
        for (const QChar c : segment.text) {
            if (c.isSpace()) {
                if (!word.isEmpty()) { tokens.append({bold.arg(escapeText(word)), pendingSpace}); word.clear(); }
                pendingSpace = true;
            } else {
                word += c;
            }
        }
        if (!word.isEmpty()) { tokens.append({bold.arg(escapeText(word)), pendingSpace}); pendingSpace = false; }
    }
    const qreal spaceWidth = QFontMetricsF(font).horizontalAdvance(' ');
    QStringList lines;
    QString line;
    qreal lineWidth = 0;
    for (const auto &token : tokens) {
        const qreal w = measure(token.markup);
        const qreal extra = (line.isEmpty() || !token.spaceBefore) ? 0 : spaceWidth;
        if (!line.isEmpty() && maxWidth > 0 && lineWidth + extra + w > maxWidth) {
            lines << line; line.clear(); lineWidth = 0;
        }
        if (!line.isEmpty() && token.spaceBefore) { line += QStringLiteral("\\ "); lineWidth += spaceWidth; }
        line += token.markup;
        lineWidth += w;
    }
    if (!line.isEmpty()) lines << line;

    mathText.parse(lines.join(QStringLiteral("\\\\")), JKQTMathText::DefaultParser, JKQTMathText::AllowLinebreaks);
    m_hadErrors = mathText.hadErrors();
    m_errors = mathText.getErrorList();
    {
        QPainter painter(&measureDevice());
        m_size = mathText.getSize(painter);
    }
    // Registra i comandi di disegno con origine (0,0) in alto a sinistra.
    m_picture = QPicture();
    QPainter painter(&m_picture);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    mathText.draw(painter, Qt::AlignLeft | Qt::AlignTop, QRectF(QPointF(0, 0), m_size));
    painter.end();
}

QStringList FormulaText::syntaxErrors(const QString &code) {
    QStringList errors;
    int depth = 0;
    bool closedTooMuch = false;
    for (int i = 0; i < code.size(); ++i) {
        const QChar c = code.at(i);
        if (c == QLatin1Char('\\')) { ++i; continue; }  // \{ e \} sono graffe scritte, non gruppi
        if (c == QLatin1Char('{')) ++depth;
        else if (c == QLatin1Char('}') && --depth < 0) { closedTooMuch = true; depth = 0; }
    }
    if (depth > 0) errors << QCoreApplication::translate("FormulaText", "a { is not closed by }");
    if (closedTooMuch) errors << QCoreApplication::translate("FormulaText", "a } closes nothing");
    const auto count = [&code](const QString &pattern) { return int(code.count(QRegularExpression(pattern))); };
    if (count(QStringLiteral(R"(\\left(?![a-zA-Z]))")) != count(QStringLiteral(R"(\\right(?![a-zA-Z]))")))
        errors << QCoreApplication::translate("FormulaText", "every \\left needs its \\right");
    if (count(QStringLiteral(R"(\\begin\{)")) != count(QStringLiteral(R"(\\end\{)")))
        errors << QCoreApplication::translate("FormulaText", "every \\begin{...} needs its \\end{...}");
    if (QRegularExpression(QStringLiteral(R"((?<!\\)[\^_]\s*$)")).match(code).hasMatch())
        errors << QCoreApplication::translate("FormulaText", "^ or _ at the end has nothing to raise or lower");
    return errors;
}

QSizeF FormulaText::boxSize(const QString &code, const QFont &font) {
    FormulaText formula;
    formula.updateBlock(code, font, Qt::black);
    if (formula.isEmpty()) return {};
    return formula.size() + QSizeF(2 * kBoxPaddingPx, 2 * kBoxPaddingPx);
}

QImage FormulaText::boxImage(const QString &code, const QFont &font, const QColor &background, const QColor &text,
                             const QSizeF &size, qreal ratio) {
    if (size.isEmpty()) return {};
    FormulaText formula;
    formula.updateBlock(code, font, text);
    QImage image((size * ratio).toSize(), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(QPointF(0, 0), size), 4, 4);
    if (!formula.isEmpty()) {
        // Margine e formula in proporzione: ingrandendo il riquadro cresce anche la formula.
        const QSizeF natural = formula.size() + QSizeF(2 * kBoxPaddingPx, 2 * kBoxPaddingPx);
        const qreal scale = std::min(size.width() / natural.width(), size.height() / natural.height());
        painter.translate(size.width() / 2, size.height() / 2);
        painter.scale(scale, scale);
        const QSizeF inner = formula.size();
        formula.draw(&painter, QRectF(-inner.width() / 2, -inner.height() / 2, inner.width(), inner.height()), Qt::AlignCenter);
    }
    painter.end();
    return image;
}

QImage FormulaText::toImage(qreal scale) const {
    if (m_picture.isNull() || m_size.isEmpty()) return {};
    QImage image((m_size * scale).toSize() + QSize(2, 2), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(scale, scale);
    painter.drawPicture(QPointF(0, 0), m_picture);
    painter.end();
    image.setDevicePixelRatio(scale);
    return image;
}

bool FormulaText::renderInDocument(QTextDocument *document, const QFont &font, const QColor &color) {
    // Stessa regola dei titoli (Pandoc): "$" seguito da un non spazio, chiuso da un non spazio non
    // seguito da una cifra; "\$" resta un dollaro.
    static const QRegularExpression math(QStringLiteral(R"((?<!\\)\$(?!\s)([^$\n]+?)(?<![\s\\])\$(?!\d))"));
    bool found = false;
    int counter = 0;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const QString text = block.text();
        QList<QRegularExpressionMatch> matches;
        for (auto it = math.globalMatch(text); it.hasNext();) matches.append(it.next());
        for (auto m = matches.crbegin(); m != matches.crend(); ++m) {  // dalla fine: le posizioni restano valide
            FormulaText formula;
            formula.updateBlock(m->captured(1), font, color);
            if (formula.isEmpty()) continue;
            const QImage image = formula.toImage(2.0);
            const QString name = QStringLiteral("formula://%1").arg(++counter);
            document->addResource(QTextDocument::ImageResource, QUrl(name), image);
            QTextCursor cursor(document);
            cursor.setPosition(block.position() + int(m->capturedStart(0)));
            cursor.setPosition(block.position() + int(m->capturedEnd(0)), QTextCursor::KeepAnchor);
            QTextImageFormat format;
            format.setName(name);
            format.setWidth(formula.size().width());
            format.setHeight(formula.size().height());
            format.setVerticalAlignment(QTextCharFormat::AlignMiddle);
            cursor.insertImage(format);
            found = true;
        }
    }
    return found;
}

void FormulaText::draw(QPainter *painter, const QRectF &rect, Qt::Alignment alignment) const {
    if (m_picture.isNull()) return;
    qreal x = rect.left();
    if (alignment & Qt::AlignHCenter) x = rect.center().x() - m_size.width() / 2.0;
    else if (alignment & Qt::AlignRight) x = rect.right() - m_size.width();
    const qreal y = rect.center().y() - m_size.height() / 2.0;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    painter->drawPicture(QPointF(x, y), m_picture);
    painter->restore();
}

namespace {
// FEAT-096: FormulaRendering's title formula, drawn by JKQTMathText.
class TitleFormulaText final : public FormulaRendering::TitleFormula {
public:
    FormulaText text;
    QSizeF size() const override { return text.size(); }
    void draw(QPainter *painter, const QRectF &rect, Qt::Alignment alignment) const override { text.draw(painter, rect, alignment); }
};
}  // namespace

void FormulaText::installRendering() {
    static const bool installed = [] {
        FormulaRendering::Hooks &hooks = FormulaRendering::hooks();
        hooks.layoutTitle = [](const QString &title, const QFont &font, const QColor &color, qreal maxWidth)
            -> std::unique_ptr<FormulaRendering::TitleFormula> {
            auto formula = std::make_unique<TitleFormulaText>();
            formula->text.update(title, font, color, maxWidth);
            if (formula->text.isEmpty()) return nullptr;
            return formula;
        };
        hooks.renderInDocument = [](QTextDocument *document, const QFont &font, const QColor &color) {
            renderInDocument(document, font, color);
        };
        hooks.boxImage = &FormulaText::boxImage;
        // FEAT-116: the shared engine (a QObject, its fonts and resources) starts here, on the GUI
        // thread: the phone may first draw a formula while building its scene on the render thread.
        engine();
        return true;
    }();
    Q_UNUSED(installed);
}
