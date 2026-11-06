#include "palettedialog.h"
#include "help.h"

#include "custompalettes.h"
#include "mindmapmodel.h"
#include "stylecontrols.h"
#include "uitheme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <memory>

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
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(NodeStyle::kMapBackground);
        p.drawRoundedRect(QRectF(rect()), 8, 8);
        // Albero di prova: radice, un principale per colore, ognuno con un figlio e un nipote.
        auto root = std::make_unique<MindNode>();
        QList<const MindNode *> mains;
        QStringList palette;
        for (const QString &color : m_colors) palette.append(color.isEmpty() ? QStringLiteral("#ffffff") : color);
        for (int i = 0; i < CustomPalettes::kColorCount; ++i) {
            auto main = std::make_unique<MindNode>();
            main->parent = root.get();
            auto child = std::make_unique<MindNode>();
            child->parent = main.get();
            auto grandchild = std::make_unique<MindNode>();
            grandchild->parent = child.get();
            child->children.push_back(std::move(grandchild));
            main->children.push_back(std::move(child));
            mains.append(main.get());
            root->children.push_back(std::move(main));
        }
        NodeStyle::resolveTree(root.get(), palette);
        constexpr qreal kPad = 12, kGap = 8, kBoxHeight = 28, kRowGap = 10;
        const qreal column = (width() - 2 * kPad - (CustomPalettes::kColorCount - 1) * kGap) / CustomPalettes::kColorCount;
        QFont font = p.font();
        font.setPixelSize(UiTheme::fontPx(12));
        p.setFont(font);
        for (int i = 0; i < CustomPalettes::kColorCount; ++i) {
            const qreal x = kPad + i * (column + kGap);
            QRectF box(x, kPad, column, kBoxHeight);
            if (m_colors.value(i).isEmpty()) {
                p.setPen(QPen(QColor("#c4cad2"), 1, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(QRectF(x, kPad, column, height() - 2 * kPad).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
                continue;
            }
            const MindNode *node = mains[i];
            for (int level = 0; level < 3; ++level) {
                p.setPen(Qt::NoPen);
                p.setBrush(node->fill);  // trasparente dove il livello non ha riempimento
                p.drawRoundedRect(box, 6, 6);
                p.setPen(node->text);
                p.drawText(box, Qt::AlignCenter, QStringLiteral("Aa"));
                box.translate(0, kBoxHeight + kRowGap);
                if (!node->children.empty()) node = node->children.front().get();
            }
        }
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
