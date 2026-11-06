#include "formuladialog.h"
#include "help.h"

#include "formulatext.h"
#include "stylecontrols.h"
#include "uitheme.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

// Anteprima: riquadro chiaro (come un nodo della mappa) con la formula al centro.
class FormulaDialog::Preview final : public QWidget {
public:
    explicit Preview(QWidget *parent) : QWidget(parent) { setMinimumHeight(96); }
    void setCode(const QString &code, const QColor &background) {
        QFont font = this->font();
        font.setPixelSize(20);
        m_background = background;
        if (code.trimmed().isEmpty()) m_formula = FormulaText();
        else m_formula.updateBlock(code, font, FormulaText::textColorOn(background, QColor("#1f2937")));
        setMinimumHeight(std::max(96, int(m_formula.size().height()) + 32));
        update();
    }
    const FormulaText &formula() const { return m_formula; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        // Sfondo della formula sopra una scacchiera leggera (si vede anche lo sfondo trasparente).
        painter.setBrush(QColor("#ffffff"));
        painter.drawRoundedRect(QRectF(rect()), 8, 8);
        if (m_background.alpha() < 255) {
            painter.setBrush(QColor(15, 23, 42, 12));
            for (int y = 0; y < height(); y += 8)
                for (int x = (y / 8) % 2 * 8; x < width(); x += 16) painter.drawRect(x, y, 8, 8);
        }
        painter.setBrush(m_background);
        painter.drawRoundedRect(QRectF(rect()), 8, 8);
        if (m_formula.isEmpty()) {
            painter.setPen(QColor("#6b7280"));
            QFont hint = font();
            hint.setPixelSize(UiTheme::fontPx(13));
            painter.setFont(hint);
            painter.drawText(rect(), Qt::AlignCenter, FormulaDialog::tr("The formula appears here while you type"));
            return;
        }
        // Formula piu' larga del riquadro: ridotta per starci (la misura vera resta quella del nodo).
        const QSizeF size = m_formula.size();
        const qreal scale = std::min(1.0, (width() - 24) / std::max<qreal>(1, size.width()));
        painter.translate(rect().center());
        painter.scale(scale, scale);
        m_formula.draw(&painter, QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height()), Qt::AlignCenter);
    }

private:
    FormulaText m_formula;
    QColor m_background = Qt::white;
};

FormulaDialog::FormulaDialog(QWidget *parent, const QString &code, const QColor &background)
    : ModalDialog(parent, tr("LaTeX formula"), code.isEmpty() ? tr("Insert") : tr("Save"), 560), m_background(background) {
    Help::setPage(this, QStringLiteral("topic-contents/formulas.md"));  // FEAT-092: F1 and "?" open it
    auto *caption = new QLabel(tr("LaTeX code, for example \\frac{a}{b}, x^2, \\sqrt{x}, \\sum_{i=1}^{n} i"));
    caption->setWordWrap(true);
    m_field = new QPlainTextEdit;
    m_field->setObjectName(QStringLiteral("FormulaCode"));
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(UiTheme::fontPx(14));
    m_field->setFont(mono);
    m_field->setTabChangesFocus(true);
    m_field->setFixedHeight(88);
    m_field->setStyleSheet(UiTheme::qss(QStringLiteral(
        "QPlainTextEdit { background:@surfaceSunken; color:@text; border:1px solid @divider; border-radius:4px; padding:6px;"
        " selection-background-color:@accent; } QPlainTextEdit:focus { border-color:@accent; }")));
    m_field->setPlainText(code);
    m_field->installEventFilter(this);
    m_preview = new Preview(this);
    m_error = new QLabel;
    m_error->setObjectName(QStringLiteral("FormulaError"));
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color:#ff8a8a; font-size:13px;"));
    contentLayout()->addWidget(caption);
    contentLayout()->addWidget(m_field);
    contentLayout()->addSpacing(8);
    contentLayout()->addWidget(m_preview);
    contentLayout()->addWidget(m_error);
    // Sfondo della formula (bianco di base; opacita' 0 = trasparente), selettore colori comune.
    auto *backgroundRow = new QHBoxLayout;
    backgroundRow->addWidget(new QLabel(tr("Background")));
    backgroundRow->addStretch();
    m_backgroundField = new ColorField;
    m_backgroundField->setObjectName(QStringLiteral("FormulaBackground"));
    NodeStyle::Reading reading;
    reading.value = m_background;
    reading.allManual = true;
    m_backgroundField->setReading(reading);
    connect(m_backgroundField, &ColorField::colorChosen, this, [this](const QColor &color) {
        m_background = color;
        NodeStyle::Reading chosen;
        chosen.value = color;
        chosen.allManual = true;
        m_backgroundField->setReading(chosen);
        refresh();
    });
    backgroundRow->addWidget(m_backgroundField);
    contentLayout()->addLayout(backgroundRow);
    connect(m_field, &QPlainTextEdit::textChanged, this, &FormulaDialog::refresh);
    refresh();
    m_field->setFocus();
    m_field->moveCursor(QTextCursor::End);
}

QString FormulaDialog::code() const { return m_field->toPlainText().trimmed(); }

void FormulaDialog::refresh() {
    const QString text = code();
    m_preview->setCode(text, m_background);
    const FormulaText &formula = m_preview->formula();
    // Il parser accetta molti errori in silenzio: prima i controlli di scrittura, poi i suoi messaggi.
    QStringList errors = FormulaText::syntaxErrors(text);
    if (formula.hadErrors()) errors += formula.errors().isEmpty() ? QStringList{tr("check the code.")} : formula.errors();
    m_valid = !text.isEmpty() && !formula.isEmpty() && errors.isEmpty();
    m_error->setText(text.isEmpty() || errors.isEmpty() ? QString() : tr("The formula is not valid: %1").arg(errors.front()));
    m_error->setVisible(!m_error->text().isEmpty());
    acceptButton()->setEnabled(m_valid);
}

bool FormulaDialog::eventFilter(QObject *watched, QEvent *event) {
    // Invio va a capo nel codice; Ctrl+Invio conferma (come il bottone principale).
    if (watched == m_field && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && (key->modifiers() & Qt::ControlModifier)) {
            if (m_valid) accept();
            return true;
        }
    }
    return ModalDialog::eventFilter(watched, event);
}
