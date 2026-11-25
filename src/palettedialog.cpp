#include "palettedialog.h"
#include "help.h"

#include "custompalettes.h"
#include "mindmapmodel.h"
#include "stylechoices.h"
#include "stylecontrols.h"
#include "uitheme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

// Anteprima del tema: per ogni colore una colonna con il ramo principale, un secondo livello e un
// terzo livello, colorati da NodeStyle::resolveTree come sulla mappa (FEAT-031). I colori non
// ancora scelti sono colonne tratteggiate. Fondo bianco come la mappa.
class PalettePreview final : public QWidget {
public:
    explicit PalettePreview(QWidget *parent = nullptr) : QWidget(parent) { setFixedHeight(132); }
    void setColors(const QStringList &colors) { m_colors = colors; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        StyleChoices::drawPalettePreview(p, QRectF(rect()), m_colors, UiTheme::fontPx(12));  // FEAT-119: shared with the phone
    }

private:
    QStringList m_colors;
};

NewPaletteDialog::NewPaletteDialog(QWidget *parent) : ModalDialog(parent, tr("Create new color theme"), tr("Save"), 520) {
    Help::setPage(this, QStringLiteral("style/color-themes.md"));  // FEAT-092: F1 and "?" open it
    setObjectName(QStringLiteral("NewPaletteDialog"));
    for (int i = 0; i < CustomPalettes::kColorCount; ++i) m_colors.append(QString());
    auto *hint = new QLabel(tr("Choose 6 colors to generate your color theme."));
    hint->setWordWrap(true);
    contentLayout()->addWidget(hint);

    auto *swatches = new QHBoxLayout;
    swatches->setSpacing(8);
    for (int i = 0; i < CustomPalettes::kColorCount; ++i) {
        auto *swatch = new QToolButton;
        swatch->setObjectName(QStringLiteral("PaletteSwatch"));
        swatch->setFixedSize(56, 56);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setAccessibleName(tr("Color %1").arg(i + 1));
        swatch->setToolTip(tr("Color %1").arg(i + 1));
        connect(swatch, &QToolButton::clicked, this, [this, i, swatch] {
            const QRect anchor(swatch->mapToGlobal(QPoint(0, 0)), swatch->size());
            ColorPicker::exec(anchor, ColorPicker::Placement::Above, {QColor(m_colors[i]), {}, false},
                              [this, i](const QColor &color) { setColor(i, color); });
        });
        m_swatches.append(swatch);
        swatches->addWidget(swatch);
    }
    swatches->addStretch();
    contentLayout()->addLayout(swatches);

    auto *nameLabel = new QLabel(tr("Name"));
    m_name = new QLineEdit;
    m_name->setObjectName(QStringLiteral("PaletteName"));
    m_name->setPlaceholderText(CustomPalettes::nextDefaultName());
    m_name->setAccessibleName(tr("Name"));
    m_error = new QLabel(tr("A palette with this name already exists."));
    m_error->setObjectName(QStringLiteral("ModalError"));
    m_error->hide();
    connect(m_name, &QLineEdit::textChanged, this, &NewPaletteDialog::updateState);
    contentLayout()->addWidget(nameLabel);
    contentLayout()->addWidget(m_name);
    contentLayout()->addWidget(m_error);

    auto *previewLabel = new QLabel(tr("Preview"));
    m_preview = new PalettePreview;
    contentLayout()->addWidget(previewLabel);
    contentLayout()->addWidget(m_preview);
    updateState();
}

QString NewPaletteDialog::paletteName() const {
    const QString name = m_name->text().trimmed();
    return name.isEmpty() ? CustomPalettes::nextDefaultName() : name;
}

void NewPaletteDialog::setColor(int index, const QColor &color) {
    if (index < 0 || index >= m_colors.size() || !color.isValid()) return;
    m_colors[index] = color.name();  // colori opachi (il selettore e' aperto senza opacita')
    updateState();
}

void NewPaletteDialog::updateState() {
    for (int i = 0; i < m_swatches.size(); ++i) {
        const QString &color = m_colors[i];
        m_swatches[i]->setStyleSheet(UiTheme::qss(color.isEmpty()
            ? QStringLiteral("QToolButton { background:@surfaceSunken; border:1px solid @divider; border-radius:8px; }"
                             "QToolButton:hover, QToolButton:focus { border-color:@accent; }")
            : QStringLiteral("QToolButton { background:%1; border:1px solid @divider; border-radius:8px; }"
                             "QToolButton:hover, QToolButton:focus { border:2px solid @accent; }").arg(color)));
    }
    const bool complete = !m_colors.contains(QString());
    const bool taken = CustomPalettes::isNameTaken(m_name->text());
    m_error->setVisible(taken);
    acceptButton()->setEnabled(complete && !taken);
    m_preview->setColors(m_colors);
}
