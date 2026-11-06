#include "stylecontrols.h"

#include <QStyleOptionComboBox>
#include <QStylePainter>

#include "branchpath.h"
#include "mapstructure.h"
#include "structurepicker.h"
#include "uitheme.h"
#include "mindmapmodel.h"
#include "nodeshape.h"
#include "nodesketch.h"
#include "segmentedcontrol.h"
#include "custompalettes.h"
#include "modaldialog.h"
#include "nodeitem.h"
#include "toggleswitch.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGridLayout>
#include <QSet>
#include <QIntValidator>
#include <QStandardItemModel>
#include <QFrame>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QRegularExpressionValidator>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <cmath>
#include <memory>
#include <numbers>

namespace {
// Colori dei menu a comparsa scuri (AGENTS.md, "Colori").
const QColor kSurface = UiTheme::color(UiTheme::Token::Surface);
const QColor kBorder = UiTheme::color(UiTheme::Token::Divider);
const QColor kText = UiTheme::color(UiTheme::Token::Text);
constexpr qreal kRadiusPx = 8;
const QString kRecentKey = QStringLiteral("colorPicker/recent");

// Riquadro a scacchi sotto i colori trasparenti, cosi' l'opacita' si vede.
void paintColorOver(QPainter &painter, const QRectF &rect, const QColor &color, qreal radius) {
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    painter.save();
    painter.setClipPath(path);
    if (color.alpha() < 255) {
        painter.fillRect(rect, QColor("#ffffff"));
        const qreal cell = 4;
        for (qreal y = rect.top(); y < rect.bottom(); y += cell)
            for (qreal x = rect.left(); x < rect.right(); x += cell)
                if ((static_cast<int>((x - rect.left()) / cell) + static_cast<int>((y - rect.top()) / cell)) % 2)
                    painter.fillRect(QRectF(x, y, cell, cell), QColor("#d0d0d0"));
    }
    painter.fillRect(rect, color);
    painter.restore();
}

class Swatch final : public QToolButton {
public:
    Swatch(const QColor &color, bool current, QWidget *parent) : QToolButton(parent), m_color(color), m_current(current) {
        setFixedSize(ColorPicker::kSwatchPx, ColorPicker::kSwatchPx);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setToolTip(color.alpha() < 255 ? color.name(QColor::HexArgb).toUpper() : color.name().toUpper());
        setAttribute(Qt::WA_Hover);
    }
    QColor color() const { return m_color; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
        paintColorOver(painter, box, m_color, 4);
        const bool highlighted = m_current || underMouse();
        painter.setPen(QPen(highlighted ? kText : kBorder, highlighted ? 2 : 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(box, 4, 4);
    }

private:
    QColor m_color;
    bool m_current;
};

class ColorPreview final : public QWidget {
public:
    explicit ColorPreview(QWidget *parent) : QWidget(parent) { setFixedSize(ColorPicker::kSwatchPx, ColorPicker::kSwatchPx); }
    void setColor(const QColor &color) { m_color = color; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (m_color.isValid()) paintColorOver(painter, box, m_color, 4);
        painter.setPen(QPen(kBorder, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(box, 4, 4);
        if (!m_color.isValid()) {
            painter.setPen(kText);
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("—"));
        }
    }

private:
    QColor m_color;
};

// Bottone del colore libero: cerchio con tutte le tinte (conico), come nel riferimento.
class RainbowButton final : public QToolButton {
public:
    explicit RainbowButton(QWidget *parent) : QToolButton(parent) {
        setFixedSize(ColorPicker::kSwatchPx, ColorPicker::kSwatchPx);
        setCheckable(true);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QObject::tr("Custom color"));
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF circle = QRectF(rect()).adjusted(3, 3, -3, -3);
        QConicalGradient hues(circle.center(), 0);
        for (int i = 0; i <= 6; ++i) hues.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 230, 255));
        painter.setPen(Qt::NoPen);
        painter.setBrush(hues);
        painter.drawEllipse(circle);
        if (isChecked() || underMouse()) {
            painter.setPen(QPen(kText, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QRectF(rect()).adjusted(1, 1, -1, -1));
        }
    }
};

// Quadrato saturazione (orizzontale) / luminosita' (verticale) per la tinta corrente.
class SaturationValueArea final : public QWidget {
public:
    std::function<void(int saturation, int value, bool released)> changed;
    explicit SaturationValueArea(QWidget *parent) : QWidget(parent) {
        setFixedSize(ColorPicker::kGridColumns * ColorPicker::kSwatchPx + (ColorPicker::kGridColumns - 1) * ColorPicker::kGapPx, 144);
        setCursor(Qt::CrossCursor);
    }
    void setHsv(int hue, int saturation, int value) { m_hue = hue; m_saturation = saturation; m_value = value; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(box, 4, 4);
        painter.setClipPath(path);
        painter.fillRect(box, QColor::fromHsv(std::max(0, m_hue), 255, 255));
        QLinearGradient white(box.topLeft(), box.topRight());
        white.setColorAt(0, Qt::white);
        white.setColorAt(1, QColor(255, 255, 255, 0));
        painter.fillRect(box, white);
        QLinearGradient black(box.topLeft(), box.bottomLeft());
        black.setColorAt(0, QColor(0, 0, 0, 0));
        black.setColorAt(1, Qt::black);
        painter.fillRect(box, black);
        painter.setClipping(false);
        const QPointF marker(box.left() + box.width() * m_saturation / 255.0, box.top() + box.height() * (255 - m_value) / 255.0);
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(marker, 5, 5);
    }
    void mousePressEvent(QMouseEvent *event) override { pick(event->position(), false); }
    void mouseMoveEvent(QMouseEvent *event) override { if (event->buttons() & Qt::LeftButton) pick(event->position(), false); }
    void mouseReleaseEvent(QMouseEvent *event) override { pick(event->position(), true); }

private:
    void pick(const QPointF &position, bool released) {
        m_saturation = std::clamp(qRound(position.x() / width() * 255), 0, 255);
        m_value = std::clamp(255 - qRound(position.y() / height() * 255), 0, 255);
        update();
        if (changed) changed(m_saturation, m_value, released);
    }
    int m_hue = 0, m_saturation = 0, m_value = 255;
};

class HueBar final : public QWidget {
public:
    std::function<void(int hue, bool released)> changed;
    explicit HueBar(QWidget *parent) : QWidget(parent) {
        setFixedSize(ColorPicker::kGridColumns * ColorPicker::kSwatchPx + (ColorPicker::kGridColumns - 1) * ColorPicker::kGapPx, 12);
        setCursor(Qt::PointingHandCursor);
    }
    void setHue(int hue) { m_hue = std::max(0, hue); update(); }
    int hue() const { return m_hue; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient hues(box.topLeft(), box.topRight());
        for (int i = 0; i <= 6; ++i) hues.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 255, 255));
        painter.setPen(Qt::NoPen);
        painter.setBrush(hues);
        painter.drawRoundedRect(box, 6, 6);
        const qreal x = box.left() + box.width() * m_hue / 359.0;
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(x, box.center().y()), 5, 5);
    }
    void mousePressEvent(QMouseEvent *event) override { pick(event->position().x(), false); }
    void mouseMoveEvent(QMouseEvent *event) override { if (event->buttons() & Qt::LeftButton) pick(event->position().x(), false); }
    void mouseReleaseEvent(QMouseEvent *event) override { pick(event->position().x(), true); }

private:
    void pick(qreal x, bool released) {
        m_hue = std::clamp(qRound(x / width() * 359), 0, 359);
        update();
        if (changed) changed(m_hue, released);
    }
    int m_hue = 0;
};

QFrame *separator(QWidget *parent) {
    auto *line = new QFrame(parent);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background:%1;border:0;").arg(kBorder.name()));
    return line;
}
}  // namespace

const QStringList &ColorPicker::gridColors() {
    static const QStringList colors{
        "#ffffff", "#eeeeee", "#d0d0d0", "#adadad", "#999999", "#666666", "#333333", "#111111", "#000000",
        "#fdd834", "#ff9595", "#8ede99", "#0ce6cf", "#8eddf9", "#50c3f7", "#7986cb", "#ba69c8", "#ffabd5",
        "#ffc009", "#ff5252", "#2cd551", "#00a99d", "#2abee0", "#05a8f4", "#4051b5", "#9c27b0", "#ea8aba",
        "#ff9f00", "#ff3c00", "#19a719", "#007c74", "#0096bf", "#0288d1", "#303e9f", "#7b1fa2", "#ce4289",
        "#ff6f00", "#e32c2d", "#15831c", "#025a5a", "#00526b", "#00579b", "#1a227e", "#4a148c", "#a34075"};
    return colors;
}

QStringList ColorPicker::recentColors() {
    return QSettings().value(kRecentKey).toStringList().mid(0, kMaxRecentColors);
}

void ColorPicker::rememberRecentColor(const QColor &color) {
    if (!color.isValid()) return;
    QStringList recent = recentColors();
    const QString name = color.name(QColor::HexArgb);
    recent.removeAll(name);
    recent.prepend(name);
    QSettings().setValue(kRecentKey, recent.mid(0, kMaxRecentColors));
}

ColorPicker *ColorPicker::openPicker() {
    return qobject_cast<ColorPicker *>(QApplication::activePopupWidget());
}

ColorPicker::ColorPicker(const Options &options, std::function<void(const QColor &)> onChosen)
    : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      m_options(options), m_onChosen(std::move(onChosen)), m_color(options.current) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet(UiTheme::qss(QStringLiteral(
        "QLabel { color:%1; font-size:12px; font-weight:600; }"
        // Codice e opacita' sono due QLineEdit. Prima l'opacita' era un QSpinBox: con il foglio di
        // stile e senza frecce il suo campo interno restava largo 1 px e il testo spariva (misurato).
        "QLineEdit { background:transparent; color:%1; border:1px solid %2; border-radius:4px;"
        " padding:0 8px; font-size:12px; selection-background-color:@surfaceRaised; }"
        "QLineEdit:focus { border-color:#7047eb; }"
        "QLineEdit:disabled { color:#6b7280; }"
        "QToolButton#SectionToggle { color:%1; border:0; background:transparent; font-size:12px; font-weight:600;"
        " text-align:left; padding:0; }").arg(kText.name(), kBorder.name())));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kPaddingPx, kPaddingPx, kPaddingPx, kPaddingPx);
    layout->setSpacing(8);

    // Griglia: un campione corrisponde al colore attuale se ha la stessa tinta (l'opacita' e' a parte).
    auto *grid = new QWidget(this);
    auto *gridLayout = new QGridLayout(grid);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(kGapPx);
    const QStringList &colors = gridColors();
    for (int i = 0; i < colors.size(); ++i) {
        const QColor color(colors[i]);
        auto *swatch = new Swatch(color, m_color.isValid() && m_color.rgb() == color.rgb(), grid);
        connect(swatch, &QToolButton::clicked, this, [this, color] { choose(withOpacity(color), true); });
        gridLayout->addWidget(swatch, i / kGridColumns, i % kGridColumns);
    }
    layout->addWidget(grid);

    // Codice, opacita', anteprima e colore libero.
    auto *fields = new QHBoxLayout;
    fields->setSpacing(kGapPx);
    m_hex = new QLineEdit(this);
    m_hex->setObjectName(QStringLiteral("HexField"));
    m_hex->setFixedHeight(kSwatchPx);
    m_hex->setPlaceholderText(QStringLiteral("#RRGGBB"));
    m_hex->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("#?[0-9A-Fa-f]{0,6}")), m_hex));
    m_hex->setToolTip(tr("Hex code; press Enter to apply"));
    connect(m_hex, &QLineEdit::returnPressed, this, [this] {
        QString text = m_hex->text().trimmed();
        if (!text.startsWith(QLatin1Char('#'))) text.prepend(QLatin1Char('#'));
        const QColor color(text);
        if (text.size() == 7 && color.isValid()) choose(withOpacity(color), true);
    });
    fields->addWidget(m_hex, 1);
    m_opacity = new QLineEdit(this);
    m_opacity->setObjectName(QStringLiteral("OpacityField"));
    m_opacity->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\s*\\d{0,3}\\s*%?\\s*")), m_opacity));
    m_opacity->setFixedSize(64, kSwatchPx);
    m_opacity->setAlignment(Qt::AlignCenter);
    m_opacity->setToolTip(tr("Opacity (0-100 %)"));
    m_opacity->setEnabled(m_options.allowOpacity);
    connect(m_opacity, &QLineEdit::editingFinished, this, [this] {
        setOpacityPercent(opacityPercent());  // normalizza il testo, es. "50" -> "50 %"
        if (m_color.isValid() && withOpacity(m_color) != m_color) choose(withOpacity(m_color), false);
    });
    fields->addWidget(m_opacity);
    auto *preview = new ColorPreview(this);
    m_preview = preview;
    fields->addWidget(preview);
    m_custom = new RainbowButton(this);
    fields->addWidget(m_custom);
    layout->addLayout(fields);

    // Colore libero: pannello nostro (non la finestra di sistema, diversa per ogni piattaforma).
    m_customPanel = new QWidget(this);
    auto *customLayout = new QVBoxLayout(m_customPanel);
    customLayout->setContentsMargins(0, 0, 0, 0);
    customLayout->setSpacing(8);
    auto *area = new SaturationValueArea(m_customPanel);
    auto *hueBar = new HueBar(m_customPanel);
    customLayout->addWidget(area);
    customLayout->addWidget(hueBar);
    m_customPanel->hide();
    layout->addWidget(m_customPanel);
    auto syncCustom = [this, area, hueBar] {
        const QColor base = m_color.isValid() ? m_color : QColor(Qt::white);
        area->setHsv(base.hsvHue(), base.hsvSaturation(), base.value());
        hueBar->setHue(base.hsvHue());
    };
    connect(m_custom, &QToolButton::toggled, this, [this, syncCustom](bool on) {
        if (on) syncCustom();
        m_customPanel->setVisible(on);
        adjustSize();
    });
    area->changed = [this, hueBar](int saturation, int value, bool released) {
        const QColor color = withOpacity(QColor::fromHsv(hueBar->hue(), saturation, value));
        if (released) choose(color, false); else showColor(color);
    };
    hueBar->changed = [this, area, hueBar](int hue, bool released) {
        const QColor base = m_color.isValid() ? m_color : QColor(Qt::white);
        const QColor color = withOpacity(QColor::fromHsv(hue, std::max(base.hsvSaturation(), 1), std::max(base.value(), 1)));
        area->setHsv(hue, color.hsvSaturation(), color.value());
        if (released) choose(color, false); else showColor(color);
    };

    // Tema attuale e colori recenti.
    layout->addWidget(separator(this));
    layout->addWidget(section(tr("Current theme"), swatchRow(m_options.themeColors, kGridColumns)));
    layout->addWidget(separator(this));
    const QStringList recent = recentColors();
    QWidget *recentContent = recent.isEmpty() ? static_cast<QWidget *>(new QLabel(tr("No recent colors"), this)) : swatchRow(recent, kGridColumns);
    if (recent.isEmpty()) recentContent->setStyleSheet(QStringLiteral("color:#6b7280; font-weight:400;"));
    layout->addWidget(section(tr("Recently used"), recentContent));

    // BUG-011: da un colore del tutto trasparente (riempimento o bordo "nessuno") il colore scelto
    // parte opaco, altrimenti resterebbe invisibile.
    setOpacityPercent(m_color.isValid() && m_color.alpha() > 0 ? qRound(m_color.alphaF() * 100) : 100);
    showColor(m_color);
    adjustSize();
}

QWidget *ColorPicker::swatchRow(const QStringList &colors, int columns) {
    auto *row = new QWidget(this);
    auto *rowLayout = new QGridLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(kGapPx);
    rowLayout->setAlignment(Qt::AlignLeft);
    int index = 0;
    for (const QString &name : colors) {
        const QColor color(name);
        if (!color.isValid()) continue;
        auto *swatch = new Swatch(color, m_color.isValid() && m_color == color, row);
        // I colori del tema e i recenti si applicano cosi' come sono, opacita' compresa.
        connect(swatch, &QToolButton::clicked, this, [this, color] { choose(m_options.allowOpacity ? color : QColor(color.rgb()), true); });
        rowLayout->addWidget(swatch, index / columns, index % columns);
        ++index;
    }
    return row;
}

QWidget *ColorPicker::section(const QString &title, QWidget *content) {
    auto *box = new QWidget(this);
    auto *boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(0, 0, 0, 0);
    boxLayout->setSpacing(8);
    auto *toggle = new QToolButton(box);
    toggle->setObjectName(QStringLiteral("SectionToggle"));
    toggle->setCheckable(true);
    toggle->setChecked(true);
    toggle->setFocusPolicy(Qt::NoFocus);
    auto label = [title](bool open) { return (open ? QStringLiteral("▾ ") : QStringLiteral("▸ ")) + title; };
    toggle->setText(label(true));
    connect(toggle, &QToolButton::toggled, this, [this, toggle, content, label](bool open) {
        toggle->setText(label(open));
        content->setVisible(open);
        adjustSize();
    });
    content->setParent(box);
    boxLayout->addWidget(toggle);
    boxLayout->addWidget(content);
    return box;
}

int ColorPicker::opacityPercent() const {
    QString digits = m_opacity->text();
    digits.remove(QLatin1Char('%'));
    bool ok = false;
    const int value = digits.trimmed().toInt(&ok);
    return ok ? std::clamp(value, 0, 100) : 100;
}

void ColorPicker::setOpacityPercent(int percent) {
    m_opacity->setText(QStringLiteral("%1 %").arg(std::clamp(percent, 0, 100)));
}

QColor ColorPicker::withOpacity(QColor color) const {
    if (!color.isValid()) return color;
    color.setAlphaF(m_options.allowOpacity ? opacityPercent() / 100.0 : 1.0);
    return color;
}

void ColorPicker::showColor(const QColor &color) {
    static_cast<ColorPreview *>(m_preview)->setColor(color);
    if (!m_hex->hasFocus()) m_hex->setText(color.isValid() ? color.name().toUpper() : QString());
}

void ColorPicker::choose(QColor color, bool close) {
    if (!color.isValid()) return;
    m_color = color;
    showColor(color);
    rememberRecentColor(color);
    if (m_onChosen) m_onChosen(color);
    if (close) this->close();
}

void ColorPicker::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(kBorder, 1));
    painter.setBrush(kSurface);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadiusPx, kRadiusPx);
}

void ColorPicker::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) { close(); event->accept(); return; }
    QWidget::keyPressEvent(event);
}

void ColorPicker::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    if (m_loop) m_loop->quit();
}

void ColorPicker::exec(const QRect &anchor, Placement placement, const Options &options,
                       const std::function<void(const QColor &)> &onChosen) {
    auto *picker = new ColorPicker(options, onChosen);
    constexpr int kDistancePx = 8;
    QScreen *screen = QGuiApplication::screenAt(anchor.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect();
    QPoint position;
    if (placement == Placement::Above) {
        position = {anchor.right() + 1 - picker->width(), anchor.top() - kDistancePx - picker->height()};
        if (screen && position.y() < area.top()) position.setY(anchor.bottom() + 1 + kDistancePx);
    } else {
        position = {anchor.left() - kDistancePx - picker->width(), anchor.top()};
        if (screen && position.x() < area.left()) position.setX(anchor.right() + 1 + kDistancePx);
    }
    if (screen) {
        position.setX(std::clamp(position.x(), area.left(), std::max(area.left(), area.right() - picker->width())));
        position.setY(std::clamp(position.y(), area.top(), std::max(area.top(), area.bottom() - picker->height())));
    }
    picker->move(position);
    QEventLoop loop;
    picker->m_loop = &loop;
    const QPointer<ColorPicker> guard(picker);
    picker->show();
    loop.exec();
    // Il selettore si cancella da solo alla chiusura (WA_DeleteOnClose); se esiste ancora non deve
    // piu' riferirsi al ciclo d'attesa, che sta per finire.
    if (guard) guard->m_loop = nullptr;
}

void LightComboItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    QStyleOptionViewItem readable(option);
    readable.palette.setColor(QPalette::Text, kText);
    readable.palette.setColor(QPalette::WindowText, kText);
    readable.palette.setColor(QPalette::ButtonText, kText);
    readable.palette.setColor(QPalette::HighlightedText, kText);
    QStyledItemDelegate::paint(painter, readable, index);
}

namespace {
// Disabled controls (docs/design/ui-foundations.md, "Controlli disattivati"): the field loses its
// border and its text drops to textMuted at 50% over the panel; the row's
// label turns textMuted. Enabled values are all white, set by hand or derived (user's decision
// 2026-11-05): before, derived values were grey and looked disabled.
QColor disabledFieldText() {
    const QColor muted = UiTheme::color(UiTheme::Token::TextMuted);
    const QColor panel = UiTheme::color(UiTheme::Token::Surface);
    return QColor((muted.red() + panel.red()) / 2, (muted.green() + panel.green()) / 2, (muted.blue() + panel.blue()) / 2);
}

// Keeps a row's label disabled while all of its fields are disabled, so the whole row reads as
// unavailable. Follows the explicit state (WA_ForceDisabled): a disabled page disables both anyway.
class LabelFollowsFields final : public QObject {
public:
    LabelFollowsFields(QLabel *label, const QList<QWidget *> &fields) : QObject(label), m_label(label) {
        for (QWidget *field : fields) {
            m_fields.append(field);
            field->installEventFilter(this);
        }
        sync();
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::EnabledChange) sync();
        return false;
    }

private:
    void sync() {
        const bool enabled = std::any_of(m_fields.cbegin(), m_fields.cend(), [](const QPointer<QWidget> &field) {
            return field && !field->testAttribute(Qt::WA_ForceDisabled);
        });
        m_label->setEnabled(enabled || m_fields.isEmpty());
    }

    QLabel *m_label;
    QList<QPointer<QWidget>> m_fields;
};

QLabel *rowLabel(const QString &text, const QList<QWidget *> &fields, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    new LabelFollowsFields(label, fields);
    return label;
}
// Side panel grid (FEAT-072 B4, live 2026-10-21): controls 24 px high, a narrow column of
// 72 px and a wide one of 148 px (with 8 px between them: 228 px of usable width).
constexpr int kFieldHeightPx = 24;
constexpr int kFieldWidthPx = 72;
constexpr int kWideFieldWidthPx = 148;
constexpr int kRowGapPx = 12;      // between rows: pitch 36
constexpr int kSectionGapPx = 20;  // above and below the section lines
constexpr int kSectionInsetPx = 8; // sections 20 px from the panel edge (12 of the panel + 8)

// Marks where the shown value comes from ("manual" = set by hand on every node). Since 2026-11-05
// it no longer changes the look: derived and manual values are both white.
void setManualLook(QWidget *widget, bool manual) { widget->setProperty("manual", manual); }
}  // namespace

namespace {
// FEAT-038: icona grande di ogni voce per il menu a griglia (QPixmap nei dati della voce).
constexpr int kGridIconRole = Qt::UserRole + 1;
// Icone delle select a icone (campo ed elenco), del menu a griglia e misura di ogni casella della
// griglia (stimate dalle foto di riferimento).
constexpr QSize kTypeIconSize(22, 14);
constexpr QSize kGridIconSize(36, 22);
constexpr QSize kGridTileSize(64, 40);
constexpr QSize kLabeledTileSize(104, 64);  // FEAT-026: miniatura 48x32 e nome 12 px sotto
constexpr QSize kStructureIconSize(48, 32);

// Menu a griglia (FEAT-038, foto attachments/FEAT-038/01-03: icone grandi in colonne,
// titolo del gruppo facoltativo e, in fondo, la voce "Nessuno" come bottone largo con un'icona
// piccola. La select resta la fonte di voci, dati e valore attuale: il popup legge le voci e
// alla scelta emette QComboBox::activated, come l'elenco classico.
struct GridMenu {
    int columns = 0;     // 0 = elenco classico a una colonna
    int noneIndex = -1;  // voce mostrata come bottone largo in fondo, -1 = nessuna
    QString title;       // titolo del gruppo sopra le icone, vuoto = nessuno
    bool labels = false; // FEAT-026: il nome sotto l'icona (caselle piu' grandi)
    // FEAT-030: further groups, each with its title above the item at that index.
    QList<std::pair<int, QString>> groups;
    // FEAT-080: colours of a panel other than the dark Style page; empty = the Style page's.
    std::optional<GridMenuColors> colors;
};

void openGridMenu(QComboBox *select, const GridMenu &menu);

// Select della pagina Stile: freccia Hugeicons ricolorata in chiaro (text, AGENTS.md "Icone":
// chiare su scuro). Con il foglio di stile Qt non disegna piu' la freccia predefinita.
class DarkSelect final : public QComboBox {
public:
    using QComboBox::QComboBox;
    GridMenu gridMenu;
    QPixmap shown;  // FEAT-072 B9: picture of the closed select instead of the current item

    void showPopup() override {
        if (gridMenu.columns > 0) openGridMenu(this, gridMenu);
        else QComboBox::showPopup();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        if (shown.isNull()) {
            QComboBox::paintEvent(event);
        } else {
            QStylePainter painter(this);
            QStyleOptionComboBox option;
            initStyleOption(&option);
            option.currentIcon = QIcon(shown);
            option.iconSize = shown.deviceIndependentSize().toSize();
            option.currentText.clear();
            painter.drawComplexControl(QStyle::CC_ComboBox, option);
            painter.drawControl(QStyle::CE_ComboBoxLabel, option);
        }
        paintDarkSelectChevron(this);
    }
};

}  // namespace

QString branchWidthName(int index) {
    switch (index) {
    // Contesto "branch width": gli stessi nomi inglesi sono anche pesi del carattere, tradotti diversamente.
    case 0: return StylePage::tr("Extra thin", "branch width");
    case 1: return StylePage::tr("Thin", "branch width");
    case 2: return StylePage::tr("Medium", "branch width");
    case 3: return StylePage::tr("Bold", "branch width");
    default: return StylePage::tr("Extra bold", "branch width");
    }
}

QComboBox *makeDarkSelect(QWidget *parent) {
    auto *select = new DarkSelect(parent);
    select->setItemDelegate(new LightComboItemDelegate(select));
    select->setStyleSheet(UiTheme::qss(QStringLiteral(
        "QComboBox { color:%1; background:@surface; border:1px solid %2; border-radius:4px; min-height:22px;"
        " max-height:22px; font-size:12px; padding:0 8px; }"
        "QComboBox:hover { background:@divider; } QComboBox:focus { border-color:@accent; }"
        "QComboBox:disabled { color:%3; background:@surface; border-color:transparent; }"
        " QComboBox::drop-down { border:0; width:24px; }"
        "QComboBox QAbstractItemView { background:@surface; color:%1; border:1px solid %2; border-radius:8px;"
        " padding:4px; selection-background-color:@surfaceRaised; outline:0; }")
                                     .arg(kText.name(), kBorder.name(), disabledFieldText().name())));
    return select;
}

void paintDarkSelectChevron(QWidget *select) {
    constexpr int kChevronPx = 16;
    static const QPixmap chevron = [] {
        QPixmap tinted = QIcon(QStringLiteral(":/assets/hugeicons/arrow-down.svg")).pixmap(kChevronPx * 2, kChevronPx * 2);
        QPainter tint(&tinted);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(tinted.rect(), kText);
        return tinted;
    }();
    QPainter painter(select);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setOpacity(select->isEnabled() ? 1.0 : 0.4);
    painter.drawPixmap(QRect(select->width() - 8 - kChevronPx, (select->height() - kChevronPx) / 2, kChevronPx, kChevronPx), chevron);
}

namespace {
// Casella del menu a griglia: icona grande al centro; hover e fuoco con lo sfondo delle voci dei
// menu (divider), voce attuale con il contorno chiaro come la palette attiva di PalettePicker.
// Con wide la casella e' il bottone "Nessuno": icona piccola e nome, su tutta la larghezza.
class GridTile final : public QToolButton {
public:
    GridTile(const QPixmap &icon, const QString &name, bool current, bool wide, QWidget *parent, bool labelBelow = false,
             const std::optional<GridMenuColors> &colors = std::nullopt)
        : QToolButton(parent), m_icon(icon), m_name(name), m_current(current), m_wide(wide), m_labelBelow(labelBelow), m_colors(colors) {
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(name);
        setAccessibleName(name);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool lit = underMouse() || hasFocus();
        if (m_wide || lit) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_colors ? m_colors->hover : UiTheme::color(m_wide && lit ? UiTheme::Token::SurfaceRaised : UiTheme::Token::Divider));
            painter.drawRoundedRect(box, 4, 4);
        }
        const QColor text = m_colors ? m_colors->text : kText;
        if (m_current) {
            painter.setPen(QPen(text, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 4, 4);
        }
        const QSizeF iconSize = m_icon.deviceIndependentSize();
        if (m_labelBelow) {  // FEAT-026: miniatura in alto, nome sotto
            painter.drawPixmap(QPointF((width() - iconSize.width()) / 2, 8), m_icon);
            QFont font = painter.font();
            font.setPixelSize(UiTheme::fontPx(12));
            painter.setFont(font);
            painter.setPen(text);
            painter.drawText(QRectF(4, 8 + iconSize.height() + 4, width() - 8, height() - iconSize.height() - 12),
                             Qt::AlignHCenter | Qt::AlignTop, QFontMetricsF(font).elidedText(m_name, Qt::ElideRight, width() - 8));
            return;
        }
        if (!m_wide) {
            painter.drawPixmap(QPointF((width() - iconSize.width()) / 2, (height() - iconSize.height()) / 2), m_icon);
            return;
        }
        QFont font = painter.font();
        font.setPixelSize(UiTheme::fontPx(12));
        painter.setFont(font);
        const qreal textWidth = QFontMetricsF(font).horizontalAdvance(m_name);
        const qreal left = (width() - iconSize.width() - 8 - textWidth) / 2;
        painter.drawPixmap(QPointF(left, (height() - iconSize.height()) / 2), m_icon);
        painter.setPen(text);
        painter.drawText(QRectF(left + iconSize.width() + 8, 0, textWidth + 1, height()), Qt::AlignLeft | Qt::AlignVCenter, m_name);
    }

private:
    QPixmap m_icon;
    QString m_name;
    bool m_current;
    bool m_wide;
    bool m_labelBelow;
    std::optional<GridMenuColors> m_colors;
};

// Popup del menu a griglia: sotto la select, allineato al suo bordo destro (il pannello sta a
// destra della finestra); Esc o clic fuori chiudono senza scegliere, frecce e Invio scelgono da
// tastiera. Taller than the screen (small displays, many items): it scrolls inside (fitHeight).
class GridMenuPopup final : public QWidget {
public:
    GridMenuPopup(QComboBox *select, const GridMenu &menu)
        : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint), m_select(select), m_columns(menu.columns),
          m_colors(menu.colors.value_or(GridMenuColors{kSurface, kBorder, kText, QColor()})) {
        setObjectName(QStringLiteral("GridMenuPopup"));
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        // Il clic fuori chiude solo il menu: Qt non lo ripete sulla select sotto il mouse, che lo
        // riaprirebbe (https://doc.qt.io/qt-6/qt.html#WidgetAttribute-enum, Qt::WA_NoMouseReplay).
        setAttribute(Qt::WA_NoMouseReplay);
        // The tiles sit in a scroll area, transparent over the popup's rounded box; its bar shows
        // only when fitHeight cuts the popup (the palette picker's thin bar, in the menu's colours).
        m_scroll = new QScrollArea(this);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setWidgetResizable(true);
        QColor handle = m_colors.text, handleHover = m_colors.text;
        handle.setAlphaF(0.35);
        handleHover.setAlphaF(0.55);
        m_scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; border:0; }"
                                               "QScrollBar:vertical { background:transparent; width:%1px; margin:%2px 4px %2px 0; }"
                                               "QScrollBar::handle:vertical { background:%3; min-height:24px; border-radius:3px; }"
                                               "QScrollBar::handle:vertical:hover { background:%4; }"
                                               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
                                               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }")
                                    .arg(kScrollBarPx)
                                    .arg(ColorPicker::kPaddingPx)
                                    .arg(handle.name(QColor::HexArgb), handleHover.name(QColor::HexArgb)));
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->addWidget(m_scroll);
        auto *content = new QWidget;
        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(ColorPicker::kPaddingPx, ColorPicker::kPaddingPx, ColorPicker::kPaddingPx, ColorPicker::kPaddingPx);
        layout->setSpacing(8);
        auto addTitle = [this, layout](const QString &text) {
            auto *title = new QLabel(text, this);
            title->setStyleSheet(QStringLiteral("color:%1; font-size:12px; font-weight:600;").arg(m_colors.text.name()));
            layout->addWidget(title);
        };
        if (!menu.title.isEmpty()) addTitle(menu.title);
        auto *grid = new QGridLayout;
        grid->setSpacing(ColorPicker::kGapPx);
        layout->addLayout(grid);
        int groupStart = 0;  // tiles placed before the current group's grid
        const int current = select->currentIndex();
        for (int i = 0; i < select->count(); ++i) {
            for (const auto &[start, text] : menu.groups) {
                if (start != i) continue;
                addTitle(text);
                grid = new QGridLayout;
                grid->setSpacing(ColorPicker::kGapPx);
                layout->addLayout(grid);
                groupStart = static_cast<int>(m_tiles.size());
            }
            const QString name = select->itemData(i, Qt::ToolTipRole).toString();
            if (i == menu.noneIndex) {
                auto *none = new GridTile(select->itemIcon(i).pixmap(kTypeIconSize), name, i == current, true, this, false, menu.colors);
                none->setFixedHeight(kFieldHeightPx);
                none->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);  // largo quanto la griglia
                connect(none, &QToolButton::clicked, this, [this, i] { choose(i); });
                layout->addWidget(none);
                m_tiles.append({none, i});
                continue;
            }
            auto *tile = new GridTile(select->itemData(i, kGridIconRole).value<QPixmap>(), name, i == current, false, this, menu.labels, menu.colors);
            tile->setFixedSize(menu.labels ? kLabeledTileSize : kGridTileSize);
            connect(tile, &QToolButton::clicked, this, [this, i] { choose(i); });
            const int position = static_cast<int>(m_tiles.size()) - groupStart - (menu.noneIndex >= 0 && menu.noneIndex < i ? 1 : 0);
            grid->addWidget(tile, position / m_columns, position % m_columns);
            m_tiles.append({tile, i});
        }
        // Il bottone "Nessuno" va in fondo anche se nella select non e' l'ultima voce.
        for (const auto &[tile, index] : std::as_const(m_tiles))
            if (index == menu.noneIndex) { layout->removeWidget(tile); layout->addWidget(tile); }
        m_scroll->setWidget(content);
        // Every tile in view; QScrollArea's own size hint stops at 24 lines of text.
        setFixedSize(content->sizeHint());
        for (const auto &[tile, index] : std::as_const(m_tiles))
            if (index == current) tile->setFocus();
        if (current < 0 && !m_tiles.isEmpty()) m_tiles.front().first->setFocus();
    }

    // At most height px tall: the tiles scroll, the bar beside them widens the popup.
    void fitHeight(int height) {
        if (height <= 0 || this->height() <= height) return;
        setFixedSize(width() + kScrollBarPx + 4, height);
    }

    void revealFocus() {
        if (QWidget *focused = focusWidget()) m_scroll->ensureWidgetVisible(focused, 0, ColorPicker::kPaddingPx);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(m_colors.border, 1));
        painter.setBrush(m_colors.surface);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadiusPx, kRadiusPx);
    }

    void keyPressEvent(QKeyEvent *event) override {
        const int focused = focusedPosition();
        int step = 0;
        switch (event->key()) {
        case Qt::Key_Escape: close(); return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            if (focused >= 0) choose(m_tiles[focused].second);
            return;
        case Qt::Key_Left: step = -1; break;
        case Qt::Key_Right: step = 1; break;
        case Qt::Key_Up: step = -m_columns; break;
        case Qt::Key_Down: step = m_columns; break;
        default: QWidget::keyPressEvent(event); return;
        }
        // Le caselle sono in ordine di lettura e il bottone "Nessuno" e' l'ultimo: spostarsi oltre
        // l'ultima riga porta al bottone, oltre il bordo si resta fermi.
        const int last = static_cast<int>(m_tiles.size()) - 1;
        const int target = focused < 0 ? 0 : std::clamp(focused + step, 0, last);
        m_tiles[target].first->setFocus();
        revealFocus();
    }

private:
    int focusedPosition() const {
        for (int i = 0; i < m_tiles.size(); ++i)
            if (m_tiles[i].first->hasFocus()) return i;
        return -1;
    }

    void choose(int index) {
        const QPointer<QComboBox> select = m_select;
        close();
        if (select) emit select->activated(index);
    }

    static constexpr int kScrollBarPx = 6;
    QPointer<QComboBox> m_select;
    QScrollArea *m_scroll;
    int m_columns;
    GridMenuColors m_colors;
    QList<std::pair<GridTile *, int>> m_tiles;  // casella e indice della voce, in ordine di lettura
};

void openGridMenu(QComboBox *select, const GridMenu &menu) {
    auto *popup = new GridMenuPopup(select, menu);
    constexpr int kDistancePx = 4;
    const QPoint below = select->mapToGlobal(QPoint(select->width(), select->height() + kDistancePx));
    QPoint position(below.x() - popup->width(), below.y());
    if (QScreen *screen = QGuiApplication::screenAt(select->mapToGlobal(select->rect().center()))) {
        const QRect area = screen->availableGeometry();
        // Below the select if it fits, else above; fitting neither, on the roomier side, scrolling.
        const int top = select->mapToGlobal(QPoint(0, 0)).y() - kDistancePx;
        const int spaceBelow = area.bottom() - below.y(), spaceAbove = top - area.top();
        if (popup->height() > spaceBelow && popup->height() > spaceAbove) popup->fitHeight(std::max(spaceBelow, spaceAbove));
        position.setX(below.x() - popup->width());
        if (popup->height() > spaceBelow) position.setY(top - popup->height());
        position.setX(std::clamp(position.x(), area.left(), std::max(area.left(), area.right() - popup->width())));
        position.setY(std::clamp(position.y(), area.top(), std::max(area.top(), area.bottom() - popup->height())));
    }
    popup->move(position);
    popup->show();
    popup->revealFocus();
}
}  // namespace

ColorField::ColorField(QWidget *parent) : QToolButton(parent) {
    setFixedSize(kFieldWidthPx, kFieldHeightPx);
    setFocusPolicy(Qt::TabFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    connect(this, &QToolButton::clicked, this, &ColorField::openPicker);
}

void ColorField::setReading(const NodeStyle::Reading &reading) {
    m_mixed = reading.empty || reading.mixed;
    m_manual = !m_mixed && reading.allManual;
    m_color = m_mixed ? QColor() : reading.value.value<QColor>();
    setToolTip(m_mixed ? tr("Mixed values") : (m_manual ? tr("Set manually") : tr("From the palette")));
    update();
}

void ColorField::openPicker() {
    ColorPicker::Options options;
    options.current = m_color;
    options.themeColors = themeColors ? themeColors() : QStringList();
    options.allowOpacity = allowOpacity;
    const QRect anchor(mapToGlobal(QPoint(0, 0)), size());
    ColorPicker::exec(anchor, ColorPicker::Placement::LeftOf, options, [this](const QColor &color) { emit colorChosen(color); });
}

void ColorField::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    // FEAT-072 B5: the whole field is the colour, without the hex code (it stays in the
    // picker). Mixed values: the field background with a dash.
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setOpacity(isEnabled() ? 1.0 : 0.4);
    if (m_mixed) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(mixedFill.isValid() ? mixedFill : UiTheme::color(UiTheme::Token::Surface));
        painter.drawRoundedRect(box, 4, 4);
        painter.setPen(mixedText.isValid() ? mixedText : kText);
        painter.drawText(box, Qt::AlignCenter, QStringLiteral("—"));
    } else {
        paintColorOver(painter, box, m_color, 4);
    }
    // Border: a dark colour stays visible on the dark panel; accent with focus, lighter on hover.
    const bool hovered = isEnabled() && underMouse();
    const QColor border = borderColor.isValid() ? borderColor : kBorder;
    const QColor hoverBorder = hoverBorderColor.isValid() ? hoverBorderColor : kText;
    painter.setPen(QPen(hasFocus() ? UiTheme::color(UiTheme::Token::Accent) : (hovered ? hoverBorder : border), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(box, 4, 4);
}

namespace {
constexpr QSize kPresetIconSize(176, 30);
using IconDraw = std::function<void(QPainter &, const QRectF &)>;

QPixmap typeIcon(const IconDraw &draw, QSize size = kTypeIconSize) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    draw(painter, QRectF(QPointF(0, 0), QSizeF(size)).adjusted(1.5, 1.5, -1.5, -1.5));
    return pixmap;
}

// Voce di una select a icone: icona piccola per il campo e l'elenco, grande per il menu a griglia,
// nome nel suggerimento e per l'accessibilita'.
void addTypeItem(QComboBox *select, int value, const QString &name, const IconDraw &draw) {
    select->addItem(typeIcon(draw), QString(), value);
    const int index = select->count() - 1;
    select->setItemData(index, name, Qt::ToolTipRole);
    select->setItemData(index, name, Qt::AccessibleTextRole);
    select->setItemData(index, typeIcon(draw, kGridIconSize), kGridIconRole);
}

}  // namespace

void addIconSelectItem(QComboBox *select, int value, const QString &name, const std::function<void(QPainter &, const QRectF &)> &draw) {
    select->setIconSize(kTypeIconSize);
    addTypeItem(select, value, name, draw);
}

void openIconGridMenu(QComboBox *select, int columns, const GridMenuColors &colors, int noneIndex) {
    GridMenu menu;
    menu.columns = columns;
    menu.noneIndex = noneIndex;
    menu.colors = colors;
    openGridMenu(select, menu);
}

namespace {
// FEAT-023: the line-end marks of an icon select (branch and relationship start or end); the icons
// are drawn by BranchPath, as on the map.
void addLineEndItems(QComboBox *select, bool atStart) {
    const QList<std::pair<BranchEnd, const char *>> ends{
        {BranchEnd::None, QT_TRANSLATE_NOOP("StylePage", "None")}, {BranchEnd::OpenArrow, QT_TRANSLATE_NOOP("StylePage", "Open arrow")},
        {BranchEnd::FilledArrow, QT_TRANSLATE_NOOP("StylePage", "Filled arrow")}, {BranchEnd::Dot, QT_TRANSLATE_NOOP("StylePage", "Dot")},
        {BranchEnd::Square, QT_TRANSLATE_NOOP("StylePage", "Square")}, {BranchEnd::Diamond, QT_TRANSLATE_NOOP("StylePage", "Diamond")}};
    select->setIconSize(kTypeIconSize);
    for (const auto &[value, name] : ends) {
        addTypeItem(select, static_cast<int>(value), QCoreApplication::translate("StylePage", name), [value, atStart](QPainter &painter, const QRectF &box) {
            const qreal y = box.center().y();
            const QPointF from(box.left() + 3, y), to(box.right() - 3, y);
            const QPointF tip = atStart ? from : to;
            painter.setPen(QPen(kText, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(from, to);
            const QPainterPath mark = BranchPath::endMark(value, tip, QPointF(atStart ? -1 : 1, 0), 2.5);
            if (!BranchPath::endIsStroke(value)) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(kText);
            }
            painter.drawPath(mark);
        });
    }
}

// "Nessuno": riquadro tenue barrato.
void drawNone(QPainter &painter, const QRectF &box) {
    painter.setPen(QPen(QColor("#6b7280"), 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(box, 3, 3);
    painter.drawLine(box.bottomLeft(), box.topRight());
}

// Line patterns as an icon select with a grid menu, like Border: a horizontal stroke in each
// pattern, the name in the tooltip. With branch the freehand lines (drawn with NodeSketch, scaled
// like the hand-drawn border icons) and "None" at the bottom.
void addLinePatternItems(QComboBox *select, bool branch) {
    struct Item { int value; QString name; Qt::PenStyle pen; bool freehand; };
    QList<Item> items{{Qt::SolidLine, StylePage::tr("Solid line"), Qt::SolidLine, false}};
    if (branch) items.append({NodeStyle::kFreehandSolidLine, StylePage::tr("Hand-drawn solid"), Qt::SolidLine, true});
    items.append({Qt::DashLine, StylePage::tr("Dashed line"), Qt::DashLine, false});
    if (branch) items.append({NodeStyle::kFreehandDashLine, StylePage::tr("Hand-drawn dashed"), Qt::DashLine, true});
    items.append({Qt::DotLine, StylePage::tr("Dotted"), Qt::DotLine, false});
    select->setIconSize(kTypeIconSize);
    for (const Item &item : items) {
        addTypeItem(select, item.value, item.name, [item](QPainter &painter, const QRectF &box) {
            const qreal scale = box.height() / 28;
            painter.translate(box.topLeft());
            painter.scale(scale, scale);
            const qreal width = box.width() / scale;
            const QLineF line(3, 14, width - 3, 14);
            constexpr quint64 kIconSeed = 7;  // fixed stroke for the icons
            const QPainterPath path = item.freehand ? NodeSketch::roughLine(line, kIconSeed) : [&line] {
                QPainterPath straight(line.p1());
                straight.lineTo(line.p2());
                return straight;
            }();
            painter.strokePath(path, QPen(kText, 2 / scale, item.pen, Qt::FlatCap, Qt::RoundJoin));
        });
    }
    auto *dark = static_cast<DarkSelect *>(select);
    if (branch) {
        addTypeItem(select, Qt::NoPen, StylePage::tr("None"), drawNone);
        dark->gridMenu = {2, select->count() - 1, QString()};
    } else {
        dark->gridMenu = {3, -1, QString()};
    }
}

// FEAT-072 B9: the selected topic as the Style card shows it: its title in its own colours
// (fill over the map white, border, text colour and weight), elided to the card.
QPixmap topicPreview(const MindNode *node) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(kPresetIconSize * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font = painter.font();
    font.setPixelSize(13);
    font.setWeight(QFont::Weight(std::clamp(node->fontWeight, 100, 900)));
    const QFontMetricsF metrics(font);
    const QString title = QString(node->title).replace(QLatin1Char('\n'), QLatin1Char(' '));
    const qreal textWidth = std::min<qreal>(metrics.horizontalAdvance(title), kPresetIconSize.width() - 34);
    const qreal width = std::max<qreal>(textWidth + 32, 48);
    const QRectF pill((kPresetIconSize.width() - width) / 2, 1, width, kPresetIconSize.height() - 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(NodeStyle::kMapBackground);
    painter.drawRoundedRect(pill, 6, 6);
    const bool border = node->borderWidth > 0 && node->border.alpha() > 0;
    painter.setPen(border ? QPen(node->border, 2) : QPen(Qt::NoPen));
    painter.setBrush(node->fill);
    painter.drawRoundedRect(pill.adjusted(1, 1, -1, -1), 5, 5);
    painter.setFont(font);
    painter.setPen(node->text);
    painter.drawText(pill, Qt::AlignCenter, metrics.elidedText(title, Qt::ElideRight, textWidth));
    return pixmap;
}
// Anteprima di uno stile predefinito (FEAT-020): pillola con il nome dello
// stile, disegnata come la mappa disegnerebbe il nodo (colore del ramo, colori del tema, livello).
// Predefinito senza riempimento pieno mostra anche il bordo, cosi' la pillola si vede.
QPixmap presetIcon(NodePreset preset, const QString &name, const QColor &color, int depth,
                   const NodeStyle::ImportantColors &important) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(kPresetIconSize * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF pill = QRectF(QPointF(0, 0), QSizeF(kPresetIconSize)).adjusted(1, 1, -1, -1);
    QColor fill = NodeStyle::kMapBackground;
    QColor border = color;
    QColor text = NodeStyle::darkenedText(color);
    bool strike = false;
    switch (preset) {
    case NodePreset::MoltoImportante:
    case NodePreset::Importante:
        fill = preset == NodePreset::MoltoImportante ? important.veryImportant : important.important;
        border = Qt::transparent;
        text = NodeStyle::readableTextOn(fill);
        break;
    case NodePreset::Depenna:
        strike = true;
        break;
    case NodePreset::Predefinito:
        if (depth == 1) {
            fill = color;
            border = Qt::transparent;
            text = NodeStyle::readableTextOn(color);
        } else if (depth == 2) {
            fill = color;
            fill.setAlphaF(0.2f);
        } else if (depth == 0) {
            text = QColor("#172033");
        }
        break;
    }
    if (fill.alpha() < 255) {  // tinta al 20%: si compone sul bianco della mappa
        painter.setPen(Qt::NoPen);
        painter.setBrush(NodeStyle::kMapBackground);
        painter.drawRoundedRect(pill, 6, 6);
    }
    painter.setPen(border.alpha() == 0 ? QPen(Qt::NoPen) : QPen(border, 2));
    painter.setBrush(fill);
    painter.drawRoundedRect(pill.adjusted(1, 1, -1, -1), 5, 5);
    QFont font = painter.font();
    font.setPixelSize(13);
    font.setWeight(QFont::DemiBold);
    font.setStrikeOut(strike);
    painter.setFont(font);
    painter.setPen(text);
    painter.drawText(pill, Qt::AlignCenter, name);
    return pixmap;
}

// Miniatura SVG statica di un tipo di mappa alla misura data (assets/structures, FEAT-026).
QPixmap structureIcon(const QString &path, QSize size) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap = QIcon(path).pixmap(size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}
}  // namespace

QComboBox *makeStructureSelect(const QList<std::pair<QString, QString>> &items, QWidget *parent) {
    auto *select = static_cast<DarkSelect *>(makeDarkSelect(parent));
    select->setFixedWidth(64);
    select->setIconSize(kTypeIconSize);
    select->setFocusPolicy(Qt::TabFocus);
    // Come Forma, Riempimento e Bordo (FEAT-038): menu a griglia, qui a due colonne con il nome sotto
    // la miniatura (richiesta dell'utente 2026-10-21, foto della select di riferimento).
    select->gridMenu = {2, -1, QString(), true};
    for (const auto &[key, name] : items) insertStructureItem(select, select->count(), key, name);
    return select;
}

void insertStructureItem(QComboBox *select, int index, const QString &key, const QString &name) {
    const QString path = QStringLiteral(":/assets/structures/%1.svg").arg(key.isEmpty() ? QStringLiteral("inherit") : key);
    select->insertItem(index, QIcon(structureIcon(path, kTypeIconSize)), QString(), key);
    select->setItemData(index, name, Qt::ToolTipRole);
    select->setItemData(index, name, Qt::AccessibleTextRole);
    select->setItemData(index, structureIcon(path, kStructureIconSize), kGridIconRole);
}

PixelField::PixelField(qreal minimum, qreal maximum, const QString &toolTip, QWidget *parent) : NumberField(parent) {
    setFixedWidth(kFieldWidthPx);
    setToolTip(toolTip);
    setRange(minimum, maximum);
    setDecimals(1);
    clearValue();
}

void PixelField::setReading(const NodeStyle::Reading &reading) {
    if (!reading.empty && !reading.mixed) setValue(reading.value.toDouble());
    else clearValue();
    setManualLook(this, !reading.empty && !reading.mixed && reading.allManual);
}

namespace {
// Foglio di stile delle pagine del pannello (Stile del nodo, FEAT-019; Stile dell'immagine, FEAT-040).
QString panelPageStyleSheet(const QString &pageName) {
    return UiTheme::qss(QStringLiteral("QWidget#%3 { background:transparent; }"
        "QLabel { color:%1; font-size:13px; background:transparent; }"
        "QToolButton#SectionToggle { color:%1; border:0; background:transparent; font-size:13px; font-weight:600;"
        " text-align:left; padding:0; }"
        "QLineEdit, QComboBox { color:%1; background:@surface; border:1px solid %2; border-radius:4px; min-height:22px;"
        " max-height:22px; font-size:12px; padding:0 8px; selection-background-color:@surfaceRaised; }"
        "QComboBox#PresetSelect { min-height:56px; max-height:56px; padding:0 4px; border-radius:8px; }"
        "QToolButton#FitButton { color:%1; background:@surface; border:1px solid %2; border-radius:4px; font-size:12px; }"
        "QToolButton#FitButton:hover { background:@divider; }"
        "QToolButton#FitButton:focus { border-color:@accent; }"
        "QLineEdit:focus, QComboBox:focus { border-color:@accent; }"
        "QLineEdit:hover, QComboBox:hover { background:@divider; }"
        "QLineEdit:disabled, QComboBox:disabled, QToolButton#FitButton:disabled { color:%4; background:@surface;"
        " border-color:transparent; }"
        "QLabel:disabled { color:@textMuted; } QLabel#FieldCaption:disabled { color:%4; }"
        "QComboBox::drop-down { border:0; width:24px; }"
        "QComboBox QAbstractItemView { background:@surface; color:%1; border:1px solid %2; border-radius:8px;"
        " padding:4px; selection-background-color:@surfaceRaised; outline:0; }")
                      .arg(kText.name(), kBorder.name(), pageName, disabledFieldText().name()));
}
}  // namespace

NumberingFields::NumberingFields(QWidget *parent) : QWidget(parent) {
    auto *boxLayout = new QVBoxLayout(this);
    boxLayout->setContentsMargins(0, 0, 0, 0);
    boxLayout->setSpacing(kRowGapPx);
    const auto select = [this](const QString &name, int width) {
        auto *combo = new DarkSelect(this);
        combo->setObjectName(name);
        if (width > 0) combo->setFixedWidth(width);
        combo->setFocusPolicy(Qt::TabFocus);
        combo->setPlaceholderText(QStringLiteral("—"));
        combo->view()->setItemDelegate(new LightComboItemDelegate(combo->view()));
        return combo;
    };
    const auto row = [this](const QString &label, QWidget *field) {
        auto *line = new QWidget(m_details);
        auto *lineLayout = new QHBoxLayout(line);
        lineLayout->setContentsMargins(0, 0, 0, 0);
        lineLayout->setSpacing(8);
        lineLayout->addWidget(rowLabel(label, {field}, line));
        lineLayout->addStretch();
        lineLayout->addWidget(field);
        return line;
    };
    // The presets fill Format; a format written by hand shows Custom (the select plus the fields).
    m_preset = select(QStringLiteral("NumberingSelect"), 0);
    m_preset->setAccessibleName(tr("Numbering"));
    m_preset->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_preset->addItem(tr("None"), -1);
    for (const QString &format : Numbering::presetFormats(u'.')) m_preset->addItem(format, int(m_preset->count() - 1));
    m_preset->addItem(tr("Custom"), -2);
    if (auto *items = qobject_cast<QStandardItemModel *>(m_preset->model()))
        items->item(m_preset->count() - 1)->setEnabled(false);  // shown, not chosen
    connect(m_preset, &QComboBox::activated, this, [this](int index) {
        const int preset = m_preset->itemData(index).toInt();
        if (preset == -2) return;
        BranchNumbering numbering = m_numbering;
        numbering.format = preset < 0 ? QString() : Numbering::presetFormats(numbering.separator).value(preset);
        emit numberingChosen(numbering);
    });
    boxLayout->addWidget(m_preset);

    m_details = new QWidget(this);
    auto *details = new QVBoxLayout(m_details);
    details->setContentsMargins(0, 0, 0, 0);
    details->setSpacing(kRowGapPx);
    m_format = new QLineEdit(m_details);
    m_format->setObjectName(QStringLiteral("NumberingFormatField"));
    m_format->setFixedWidth(kWideFieldWidthPx);
    m_format->setToolTip(tr("One symbol per level: 1, A, a, I or i, joined by the separator (e.g. I.A.1)"));
    connect(m_format, &QLineEdit::editingFinished, this, [this] {
        BranchNumbering numbering = m_numbering;
        numbering.format = m_format->text().trimmed();
        if (numbering.format != m_numbering.format && Numbering::isValid(numbering)) emit numberingChosen(numbering);
        else m_format->setText(m_numbering.format);  // not valid: back to the shown one
    });
    details->addWidget(row(tr("Format"), m_format));
    m_separator = select(QStringLiteral("NumberingSeparatorSelect"), kFieldWidthPx);
    m_separator->setAccessibleName(tr("Separator"));
    for (const QChar separator : Numbering::separators()) m_separator->addItem(QString(separator), QString(separator));
    connect(m_separator, &QComboBox::activated, this, [this](int index) {
        BranchNumbering numbering = m_numbering;
        numbering.separator = m_separator->itemData(index).toString().front();
        numbering.format.replace(m_numbering.separator, numbering.separator);
        emit numberingChosen(numbering);
    });
    details->addWidget(row(tr("Separator"), m_separator));
    m_trailingSeparator = new ToggleSwitch(m_details);
    m_trailingSeparator->setObjectName(QStringLiteral("NumberingTrailingSeparatorSwitch"));
    m_trailingSeparator->setAccessibleName(tr("Separator at the end"));
    connect(m_trailingSeparator, &ToggleSwitch::toggled, this, [this](bool on) {
        BranchNumbering numbering = m_numbering;
        numbering.trailingSeparator = on;
        emit numberingChosen(numbering);
    });
    details->addWidget(row(tr("Separator at the end"), m_trailingSeparator));
    m_start = new QLineEdit(m_details);
    m_start->setObjectName(QStringLiteral("NumberingStartField"));
    m_start->setFixedWidth(kFieldWidthPx);
    m_start->setValidator(new QIntValidator(1, Numbering::kMaxStart, m_start));
    m_start->setToolTip(tr("Number of the first topic of the first level"));
    connect(m_start, &QLineEdit::editingFinished, this, [this] {
        BranchNumbering numbering = m_numbering;
        numbering.start = m_start->text().toInt();
        if (numbering.start != m_numbering.start && Numbering::isValid(numbering)) emit numberingChosen(numbering);
        else m_start->setText(QString::number(m_numbering.start));
    });
    details->addWidget(row(tr("First number"), m_start));
    m_type = select(QStringLiteral("NumberingTypeSelect"), kWideFieldWidthPx);
    m_type->setAccessibleName(tr("Type"));
    m_type->addItem(tr("Hierarchical"), true);
    m_type->addItem(tr("By level"), false);
    connect(m_type, &QComboBox::activated, this, [this](int index) {
        BranchNumbering numbering = m_numbering;
        numbering.hierarchical = m_type->itemData(index).toBool();
        emit numberingChosen(numbering);
    });
    details->addWidget(row(tr("Type"), m_type));
    m_levels = select(QStringLiteral("NumberingLevelsSelect"), kFieldWidthPx);
    m_levels->setAccessibleName(tr("Levels"));
    m_levels->addItem(tr("All"), 0);
    for (int levels = 1; levels <= Numbering::kMaxLevels; ++levels) m_levels->addItem(QString::number(levels), levels);
    connect(m_levels, &QComboBox::activated, this, [this](int index) {
        BranchNumbering numbering = m_numbering;
        numbering.levels = m_levels->itemData(index).toInt();
        emit numberingChosen(numbering);
    });
    details->addWidget(row(tr("Levels"), m_levels));
    boxLayout->addWidget(m_details);
}

void NumberingFields::setNodes(const QList<const MindNode *> &nodes) {
    const NodeStyle::Reading reading = NodeStyle::read(nodes, StyleProperty::Numbering);
    m_numbering = BranchNumbering();
    for (const MindNode *node : nodes)
        if (node->style.numbering) { m_numbering = *node->style.numbering; break; }
    const bool set = !reading.empty && !reading.mixed && !m_numbering.format.isEmpty();
    {
        const QSignalBlocker blocker(m_preset);
        int index = -1;
        if (!reading.empty && !reading.mixed) {
            const int preset = m_numbering.format.isEmpty() ? -1 : Numbering::presetIndex(m_numbering);
            index = m_preset->findData(m_numbering.format.isEmpty() || preset >= 0 ? preset : -2);
        }
        m_preset->setCurrentIndex(index);
    }
    setManualLook(m_preset, set);
    m_preset->setEnabled(!nodes.isEmpty());
    m_details->setVisible(set);
    if (!set) return;
    const QSignalBlocker formatBlocker(m_format), separatorBlocker(m_separator), trailingBlocker(m_trailingSeparator),
        startBlocker(m_start), typeBlocker(m_type), levelsBlocker(m_levels);
    m_format->setText(m_numbering.format);
    m_separator->setCurrentIndex(m_separator->findData(QString(m_numbering.separator)));
    m_trailingSeparator->setChecked(m_numbering.trailingSeparator);
    m_start->setText(QString::number(m_numbering.start));
    m_type->setCurrentIndex(m_type->findData(m_numbering.hierarchical));
    m_levels->setCurrentIndex(m_levels->findData(m_numbering.levels));
    // The fields show the topic's own numbering.
    for (QWidget *field : std::initializer_list<QWidget *>{m_format, m_separator, m_start, m_type, m_levels})
        setManualLook(field, true);
}

StylePage::StylePage(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("StylePage"));
    // Stile limitato a questa pagina: la finestra ha regole chiare per QLineEdit che qui non valgono.
    setStyleSheet(panelPageStyleSheet(objectName()));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);  // sections bring their own divider and spacing (FEAT-072 B2)

    // FEAT-020: stile predefinito in cima: ogni voce e' l'anteprima con il suo nome, nel
    // colore del nodo e del tema; la select mostra lo stile attuale del nodo.
    m_preset = enumSelect(StyleProperty::Preset, 0);
    m_preset->setObjectName(QStringLiteral("PresetSelect"));
    m_preset->setIconSize(kPresetIconSize);
    m_preset->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_preset->setMinimumWidth(kPresetIconSize.width() + 40);
    m_presetNames = {{NodePreset::MoltoImportante, tr("Very important")}, {NodePreset::Importante, tr("Important")},
                     {NodePreset::Depenna, tr("Strike out")}, {NodePreset::Predefinito, tr("Default")}};
    for (const auto &[preset, name] : std::as_const(m_presetNames)) {
        m_preset->addItem(QString(), static_cast<int>(preset));
        m_preset->setItemData(m_preset->count() - 1, name, Qt::ToolTipRole);
        m_preset->setItemData(m_preset->count() - 1, name, Qt::AccessibleTextRole);
    }
    layout->addWidget(m_preset);

    auto themes = [this] { return m_themeColors; };
    m_fill = new ColorField(this);
    m_border = new ColorField(this);
    m_text = new ColorField(this);
    m_branchColor = new ColorField(this);
    for (ColorField *field : {m_fill, m_border, m_text, m_branchColor}) field->themeColors = themes;
    connect(m_fill, &ColorField::colorChosen, this, [this](const QColor &color) { emit styleChosen(StyleProperty::FillColor, color); });
    connect(m_border, &ColorField::colorChosen, this, [this](const QColor &color) { emit styleChosen(StyleProperty::BorderColor, color); });
    connect(m_text, &ColorField::colorChosen, this, [this](const QColor &color) { emit styleChosen(StyleProperty::TextColor, color); });
    connect(m_branchColor, &ColorField::colorChosen, this, [this](const QColor &color) { emit styleChosen(StyleProperty::BranchColor, color); });

    // Tipi di riempimento e di bordo: select con sola icona, il nome nel suggerimento.
    constexpr int kTypeSelectWidthPx = 64;  // padding 8 + icona 22 + freccia 24 + margine
    m_fillStyle = enumSelect(StyleProperty::FillStyle, kTypeSelectWidthPx);
    // FEAT-020 parte C: motivi e mano libera disegnati con lo stesso codice della mappa su un
    // riquadro piu' grande, ridotto in proporzione all'altezza dell'icona: cosi' si vedono tre o
    // quattro righe del motivo sia nell'icona piccola sia in quella del menu a griglia (foto 23).
    auto sketchIcon = [](const std::function<void(QPainter &, const QPainterPath &, qreal)> &draw) -> IconDraw {
        return [draw](QPainter &painter, const QRectF &box) {
            const qreal scale = box.height() / 28;
            painter.translate(box.topLeft());
            painter.scale(scale, scale);
            QPainterPath outline;
            // Rientro di 3 unita': le punte dei motivi restano dentro l'icona.
            outline.addRoundedRect(QRectF(QPointF(0, 0), box.size() / scale).adjusted(3, 3, -3, -3), 5, 5);
            draw(painter, outline, scale);
        };
    };
    constexpr quint64 kIconSeed = 7;  // tratto fisso per le icone
    addTypeItem(m_fillStyle, static_cast<int>(NodeFillStyle::Pieno), tr("Filled"), [](QPainter &painter, const QRectF &box) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(kText);
        painter.drawRoundedRect(box, 3, 3);
    });
    addTypeItem(m_fillStyle, static_cast<int>(NodeFillStyle::PienoManoLibera), tr("Hand-drawn fill"),
                sketchIcon([](QPainter &painter, const QPainterPath &outline, qreal) {
                    painter.fillPath(NodeSketch::roughArea(outline, kIconSeed), kText);
                }));
    // Ordine della foto 23: pennellate e tratteggio diagonali, poi orizzontali.
    const QList<std::pair<NodeFillStyle, QString>> patterns{
        {NodeFillStyle::PennellateDiagonali, tr("Diagonal strokes")}, {NodeFillStyle::TratteggioDiagonale, tr("Diagonal hatching")},
        {NodeFillStyle::PennellateOrizzontali, tr("Horizontal strokes")}, {NodeFillStyle::TratteggioOrizzontale, tr("Horizontal hatching")}};
    for (const auto &[style, name] : patterns) {
        addTypeItem(m_fillStyle, static_cast<int>(style), name, sketchIcon([style](QPainter &painter, const QPainterPath &outline, qreal scale) {
            const NodeSketch::Pattern pattern = NodeSketch::pattern(style, outline, kIconSeed);
            // Nell'icona piccola pennellate un po' piu' sottili: a 22x14 px le scaglie chiare
            // altrimenti spariscono e il motivo sembra un pieno.
            painter.setPen(QPen(kText, pattern.penWidth * (scale < 0.5 ? 0.75 : 1.0), Qt::SolidLine, Qt::FlatCap));
            painter.drawLines(pattern.lines);
        }));
    }
    addTypeItem(m_fillStyle, static_cast<int>(NodeFillStyle::Nessuno), tr("No fill"), drawNone);
    m_borderStyle = enumSelect(StyleProperty::BorderStyle, kTypeSelectWidthPx);
    // Ordine della foto 04: continuo, a mano libera, tratteggiato, tratteggiato a mano libera.
    for (const auto penStyle : {Qt::SolidLine, Qt::DashLine}) {
        const bool dashed = penStyle == Qt::DashLine;
        addTypeItem(m_borderStyle, static_cast<int>(dashed ? NodeBorderStyle::Tratteggiato : NodeBorderStyle::Continuo),
                    dashed ? tr("Dashed") : tr("Solid"), [penStyle](QPainter &painter, const QRectF &box) {
                        painter.setPen(QPen(kText, 1.5, penStyle));
                        painter.drawRoundedRect(box, 3, 3);
                    });
        addTypeItem(m_borderStyle, static_cast<int>(dashed ? NodeBorderStyle::TratteggiatoManoLibera : NodeBorderStyle::ContinuoManoLibera),
                    dashed ? tr("Hand-drawn dashed") : tr("Hand-drawn solid"),
                    sketchIcon([penStyle](QPainter &painter, const QPainterPath &outline, qreal scale) {
                        painter.strokePath(NodeSketch::roughOutline(outline, kIconSeed),
                                           QPen(kText, 1.5 / scale, penStyle, Qt::RoundCap, Qt::RoundJoin));
                    }));
    }
    addTypeItem(m_borderStyle, static_cast<int>(NodeBorderStyle::Nessuno), tr("No border"), drawNone);
    // FEAT-038: riempimento e bordo con il menu a griglia a due colonne e "Nessuno" in fondo.
    static_cast<DarkSelect *>(m_fillStyle)->gridMenu = {2, m_fillStyle->findData(static_cast<int>(NodeFillStyle::Nessuno)), QString()};
    static_cast<DarkSelect *>(m_borderStyle)->gridMenu = {2, m_borderStyle->findData(static_cast<int>(NodeBorderStyle::Nessuno)), QString()};
    for (QComboBox *select : {m_fillStyle, m_borderStyle}) select->setIconSize(kTypeIconSize);
    m_borderWidth = new PixelField(NodeStyle::kMinBorderWidthPx, NodeStyle::kMaxBorderWidthPx,
                                   tr("Border thickness (%1-%2 px). Up and down arrows: 1 px, with Shift 10 px")
                                       .arg(NodeStyle::kMinBorderWidthPx).arg(NodeStyle::kMaxBorderWidthPx), this);
    connect(m_borderWidth, &PixelField::valueChosen, this, [this](qreal width) { emit styleChosen(StyleProperty::BorderWidth, width); });

    // FEAT-020 parte B: forme Base, icona disegnata con lo stesso contorno della mappa.
    m_shape = enumSelect(StyleProperty::Shape, kTypeSelectWidthPx);
    m_shape->setIconSize(kTypeIconSize);
    // In the menu's order, which is not the order of the values (FEAT-030: the square brackets, added
    // with the fishbone, belong to the Puntuale group).
    using S = NodeShape;
    const QList<std::pair<NodeShape, QString>> nodeShapes{
        {S::RettangoloArrotondato, tr("Rounded rectangle")}, {S::Rettangolo, tr("Rectangle")}, {S::LineaSotto, tr("Line below")},
        {S::DoppiaLineaSotto, tr("Double line below")}, {S::Cerchio, tr("Circle")}, {S::Ellisse, tr("Ellipse")},
        {S::Rombo, tr("Diamond")}, {S::Parallelogramma, tr("Parallelogram")}, {S::Pillola, tr("Pill")},
        {S::Esagono, tr("Hexagon")}, {S::EsagonoVerticale, tr("Vertical hexagon")}, {S::Ottagono, tr("Octagon")},
        // FEAT-030 stage A: the Advanced group.
        {S::Cuore, tr("Heart")}, {S::Nuvola, tr("Cloud")}, {S::Stella, tr("Star")}, {S::Goccia, tr("Drop")}, {S::Gemma, tr("Gem")},
        {S::Scudo, tr("Shield")}, {S::FrecciaSinistra, tr("Left arrow")}, {S::FrecciaDestra, tr("Right arrow")},
        {S::Etichetta, tr("Tag")}, {S::Bandiera, tr("Flag")}, {S::Nota, tr("Folded note")}, {S::PilaDiSchede, tr("Card stack")},
        {S::SchedaConBordoLaterale, tr("Side-bar card")},
        // FEAT-030 stage B: the Puntuale group.
        {S::ParentesiAngolari, tr("Angle brackets")}, {S::DoppieParentesiAngolari, tr("Double angle brackets")},
        {S::ParentesiQuadre, tr("Square brackets")}, {S::ParentesiTonde, tr("Round brackets")},
        {S::ParentesiGraffe, tr("Curly brackets")}, {S::Angoli, tr("Corners")}, {S::Virgolette, tr("Quotation marks")},
        // FEAT-030 stage C: the flowchart group.
        {S::Decisione, tr("Decision")}, {S::InputManuale, tr("Manual input")}, {S::ProcessoPredefinito, tr("Predefined process")},
        {S::OperazioneManuale, tr("Manual operation")}, {S::Database, tr("Database")}, {S::Documento, tr("Document")},
        {S::DocumentiMultipli, tr("Multiple documents")}, {S::Ritardo, tr("Delay")}, {S::Dati, tr("Data")},
        {S::Connettore, tr("Connector")}, {S::ConnettoreFuoriPagina, tr("Off-page connector")}, {S::CerchioDoppio, tr("Double circle")},
        {S::Visualizzazione, tr("Display")}, {S::Trapezio, tr("Trapezoid")}, {S::TrapezioRovesciato, tr("Inverted trapezoid")}};
    for (const auto &[shape, name] : nodeShapes) {
        addTypeItem(m_shape, static_cast<int>(shape), name, [shape](QPainter &painter, const QRectF &box) {
            // Riquadro "contenuto" scelto perche' ogni forma stia nell'icona con le sue proporzioni.
            const QRectF content(0, 0, 36, 22);
            const QRectF outer = NodeShapes::outerRect(shape, content, 2);
            const qreal scale = std::min(box.width() / outer.width(), box.height() / outer.height());
            painter.translate(box.center());
            painter.scale(scale, scale);
            painter.translate(-outer.center());
            painter.setPen(QPen(kText, 1.5 / scale));
            painter.setBrush(Qt::NoBrush);
            if (NodeShapes::hasOpenBorder(shape))
                for (const QLineF &line : NodeShapes::borderLines(shape, outer, 2)) painter.drawLine(line);
            else
                painter.drawPath(NodeShapes::outline(shape, outer));
            for (const QPainterPath &line : NodeShapes::innerLines(shape, outer)) painter.drawPath(line);
            for (const QPainterPath &mark : NodeShapes::marks(shape, outer)) painter.drawPath(mark);
            painter.fillPath(NodeShapes::filledMarks(shape, outer), kText);
        });
    }
    // FEAT-038: tre colonne con il titolo del gruppo, come la foto 02.
    const auto itemOf = [this](NodeShape shape) { return m_shape->findData(static_cast<int>(shape)); };
    static_cast<DarkSelect *>(m_shape)->gridMenu = {3, -1, tr("Basic"), false,
                                                    {{itemOf(S::Cuore), tr("Advanced")}, {itemOf(S::ParentesiAngolari), tr("Punctual")},
                                                     {itemOf(S::Decisione), tr("Flowchart")}}};

    auto *shape = new QWidget(this);
    auto *shapeLayout = new QVBoxLayout(shape);
    shapeLayout->setContentsMargins(0, 0, 0, 0);
    shapeLayout->setSpacing(kRowGapPx);
    // FEAT-072 B10: the shape select sits in the section header.
    shapeLayout->addWidget(row(tr("Fill"), {m_fillStyle, m_fill}));
    shapeLayout->addWidget(row(tr("Border"), {m_borderStyle, m_border}));
    shapeLayout->addWidget(row(tr("Thickness (px)"), {m_borderWidth}));
    layout->addWidget(section(tr("Shape"), shape, m_shape));

    // FEAT-021: riga da sola fra Forma e Testo (riferimento FEAT-019/01): larghezza del riquadro del
    // testo in px e "Adatta", che torna alla larghezza automatica. Field and button 72 px each, like
    // "Lunghezza" row (FEAT-072 B11).
    // Intervallo del campo 0-9999: il valore scritto arriva sempre a MainWindow, che lo porta fra il
    // minimo di ogni nodo e 3000 px (sotto il minimo = minimo, oltre 3000 = 3000, come trascinando).
    m_width = new PixelField(0, 9999,
                             tr("Width of the text box (up to %1 px; below the node minimum the minimum is used). Up and down arrows: 1 px, with Shift 10 px").arg(static_cast<int>(kMaxManualNodeWidthPx)), this);
    m_width->setObjectName(QStringLiteral("NodeWidthField"));
    m_width->setFixedWidth(kFieldWidthPx);
    m_width->setStepsEachNode(true);
    connect(m_width, &PixelField::valueChosen, this, &StylePage::widthChosen);
    connect(m_width, &PixelField::valueStepped, this, &StylePage::widthStepped);
    m_fit = new QToolButton(this);
    m_fit->setObjectName(QStringLiteral("FitButton"));
    m_fit->setText(tr("Fit"));
    m_fit->setToolTip(tr("Automatic width, like double-clicking the node edge"));
    m_fit->setFixedSize(kFieldWidthPx, kFieldHeightPx);
    m_fit->setFocusPolicy(Qt::TabFocus);
    connect(m_fit, &QToolButton::clicked, this, &StylePage::widthReset);
    layout->addWidget(divided(row(tr("Width (px)"), {m_width, m_fit})));  // its own block (B11)

    layout->addWidget(section(tr("Text"), textSection()));

    // FEAT-026 fase C: Struttura fra Testo e Ramo (foto dell'utente 2026-10-21): select a
    // icona nell'intestazione, direzione sotto. Solo con un nodo non centrale scelto.
    m_structure = new StructurePicker(true, false, this);
    connect(m_structure, &StructurePicker::chosen, this, &StylePage::structureChosen);
    m_structureSection = section(tr("Structure"), m_structure, m_structure->typeSelect());
    layout->addWidget(m_structureSection);
    // FEAT-072 B13: with the central topic the same section sets the sheet type and its direction.
    m_rootStructure = new StructurePicker(false, false, this);
    m_rootStructure->setObjectName(QStringLiteral("RootStructurePicker"));
    connect(m_rootStructure, &StructurePicker::chosen, this, &StylePage::structureChosen);
    m_rootStructureSection = section(tr("Structure"), m_rootStructure, m_rootStructure->typeSelect());
    layout->addWidget(m_rootStructureSection);

    // FEAT-023 (foto dell'utente 2026-10-21): anche nessuna linea e le linee a mano libera.
    // FEAT-038: icon select with previews (user request 2026-10-25; was a list of names).
    m_branchStyle = enumSelect(StyleProperty::BranchStyle, 64);
    m_branchStyle->setObjectName(QStringLiteral("BranchStyleSelect"));
    m_branchStyle->setAccessibleName(tr("Line style"));
    addLinePatternItems(m_branchStyle, true);
    // FEAT-023: spessori con i nomi (richiesta dell'utente 2026-10-21; prima un campo in px).
    m_branchWidth = enumSelect(StyleProperty::BranchWidth, kWideFieldWidthPx);
    m_branchWidth->setObjectName(QStringLiteral("BranchWidthSelect"));
    for (int i = 0; i < NodeStyle::kNamedBranchWidths.size(); ++i)
        m_branchWidth->addItem(branchWidthName(i), NodeStyle::kNamedBranchWidths[i]);
    auto *branch = new QWidget(this);
    auto *branchLayout = new QVBoxLayout(branch);
    branchLayout->setContentsMargins(0, 0, 0, 0);
    branchLayout->setSpacing(kRowGapPx);
    // Grid (FEAT-072 B14): line pattern, then thickness with the colour next to it.
    branchLayout->addWidget(row(tr("Line"), {m_branchStyle}));
    branchLayout->addWidget(row(QString(), {m_branchWidth, m_branchColor}));
    // FEAT-023 (user's approval 2026-10-24): marks at the start (parent) and end (child) of the lines,
    // two icon selects side by side.
    for (auto [select, property] : {std::pair{&m_branchStart, StyleProperty::BranchStart}, std::pair{&m_branchEnd, StyleProperty::BranchEnd}}) {
        const bool atStart = property == StyleProperty::BranchStart;
        *select = enumSelect(property, kFieldWidthPx);
        (*select)->setObjectName(atStart ? QStringLiteral("BranchStartSelect") : QStringLiteral("BranchEndSelect"));
        (*select)->setAccessibleName(atStart ? tr("Line start") : tr("Line end"));
        addLineEndItems(*select, atStart);
    }
    branchLayout->addWidget(row(tr("Start / End"), {m_branchStart, m_branchEnd}));
    // FEAT-072: shape of the lines towards the children in the section header, as the Branch
    // select (seven shapes, two columns); the icons are drawn by BranchPath, as on the map.
    m_branchShape = enumSelect(StyleProperty::BranchShape, 64);
    m_branchShape->setObjectName(QStringLiteral("BranchShapeSelect"));
    m_branchShape->setIconSize(kTypeIconSize);
    const QList<std::pair<BranchShape, QString>> shapes{
        {BranchShape::RoundedBracket, tr("Rounded bracket")}, {BranchShape::Bracket, tr("Bracket")},
        {BranchShape::Straight, tr("Straight lines")}, {BranchShape::Curve, tr("Curves")},
        {BranchShape::SCurve, tr("S-curves")}, {BranchShape::Angled, tr("Angled lines")},
        {BranchShape::RoundedAngled, tr("Rounded angled lines")}};
    for (const auto &[value, name] : shapes) {
        addTypeItem(m_branchShape, static_cast<int>(value), name, [value](QPainter &painter, const QRectF &box) {
            // Drawn in a 48x32 space (stub and bar at their real 12 px) and scaled to the icon.
            const qreal scale = std::min(box.width() / 48, box.height() / 32);
            painter.translate(box.center() - QPointF(24, 16) * scale);
            painter.scale(scale, scale);
            painter.setPen(QPen(kText, 1.5 / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            const QPointF from(2, 16);
            for (const qreal y : {3.0, 16.0, 29.0}) {
                const QPointF to(46, y);
                const QPainterPath path = value == BranchShape::RoundedBracket || value == BranchShape::Bracket
                                              ? BranchPath::bracket(from, to, value == BranchShape::RoundedBracket)
                                              : BranchPath::fan(from, to, static_cast<int>(value));
                painter.drawPath(path);
            }
        });
    }
    static_cast<DarkSelect *>(m_branchShape)->gridMenu = {2, -1, QString()};
    layout->addWidget(section(tr("Branch"), branch, m_branchShape));
    m_numbering = new NumberingFields(this);
    connect(m_numbering, &NumberingFields::numberingChosen, this,
            [this](const BranchNumbering &numbering) { emit styleChosen(StyleProperty::Numbering, QVariant::fromValue(numbering)); });
    layout->addWidget(section(tr("Numbering"), m_numbering));  // FEAT-024: under Branch
    // FEAT-072 B15: full-width button at the bottom.
    auto *actionBox = new QWidget(this);
    auto *actionLayout = new QVBoxLayout(actionBox);
    actionLayout->setContentsMargins(20, 0, 20, 20);
    m_styleAction = new QToolButton(actionBox);
    m_styleAction->setText(tr("Apply to related"));
    m_styleAction->setToolTip(tr("Give this style to the sibling topics (same parent)"));
    m_styleAction->setObjectName(QStringLiteral("FitButton"));
    m_styleAction->setAccessibleName(tr("Style of the topic"));
    m_styleAction->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_styleAction->setFixedHeight(28);
    m_styleAction->setCursor(Qt::PointingHandCursor);
    connect(m_styleAction, &QToolButton::clicked, this, &StylePage::styleActionRequested);
    actionLayout->addWidget(m_styleAction);
    // Apply to children, under the button above and like it.
    actionLayout->setSpacing(8);
    m_childrenStyle = new QToolButton(actionBox);
    m_childrenStyle->setObjectName(QStringLiteral("FitButton"));
    m_childrenStyle->setText(tr("Apply to children"));
    m_childrenStyle->setToolTip(tr("Give this style to all the subtopics of the selected topics"));
    m_childrenStyle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_childrenStyle->setFixedHeight(28);
    m_childrenStyle->setCursor(Qt::PointingHandCursor);
    connect(m_childrenStyle, &QToolButton::clicked, this, &StylePage::childrenStyleRequested);
    actionLayout->addWidget(m_childrenStyle);
    m_resetStyle = new QToolButton(actionBox);
    m_resetStyle->setObjectName(QStringLiteral("FitButton"));
    m_resetStyle->setText(tr("Reset style"));
    m_resetStyle->setToolTip(tr("Remove the style chosen for the selected topics"));
    m_resetStyle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_resetStyle->setFixedHeight(28);
    m_resetStyle->setCursor(Qt::PointingHandCursor);
    connect(m_resetStyle, &QToolButton::clicked, this, &StylePage::resetStyleRequested);
    actionLayout->addWidget(m_resetStyle);
    layout->addWidget(divided(actionBox));
    layout->addStretch();
    setNodes({}, {});
}

QComboBox *StylePage::enumSelect(StyleProperty property, int width) {
    auto *select = new DarkSelect(this);
    if (width > 0) select->setFixedWidth(width);
    select->setFocusPolicy(Qt::TabFocus);
    select->setPlaceholderText(QStringLiteral("—"));
    select->view()->setItemDelegate(new LightComboItemDelegate(select->view()));
    connect(select, &QComboBox::activated, this, [this, select, property](int index) {
        emit styleChosen(property, select->itemData(index));
    });
    return select;
}

void StylePage::showEnum(QComboBox *select, const NodeStyle::Reading &reading) {
    {
        const QSignalBlocker blocker(select);
        select->setCurrentIndex(!reading.empty && !reading.mixed ? select->findData(reading.value.toInt()) : -1);
    }
    setManualLook(select, !reading.empty && !reading.mixed && reading.allManual);
}

void StylePage::updatePresetPreviews(const QColor &color, int depth, const QStringList &palette) {
    // Le anteprime si ridisegnano solo se cambia qualcosa che le riguarda.
    const PreviewKey key{color.rgba(), std::min(depth, 3), palette};
    if (key == m_previewKey) return;
    m_previewKey = key;
    const NodeStyle::ImportantColors important = NodeStyle::importantColors(palette);
    for (int i = 0; i < m_preset->count(); ++i) {
        const auto &[preset, name] = m_presetNames[i];
        // Stessa immagine anche per la voce evidenziata: Qt altrimenti la tinge col colore di selezione.
        const QPixmap pixmap = presetIcon(preset, name, color, key.depth, important);
        QIcon icon;
        for (const QIcon::Mode mode : {QIcon::Normal, QIcon::Active, QIcon::Selected}) icon.addPixmap(pixmap, mode);
        m_preset->setItemIcon(i, icon);
    }
}

QWidget *StylePage::row(const QString &label, const QList<QWidget *> &fields) {
    auto *line = new QWidget(this);
    auto *lineLayout = new QHBoxLayout(line);
    lineLayout->setContentsMargins(0, 0, 0, 0);
    lineLayout->setSpacing(8);
    if (!label.isEmpty()) lineLayout->addWidget(rowLabel(label, fields, line));
    lineLayout->addStretch();
    for (QWidget *field : fields) lineLayout->addWidget(field);
    return line;
}

QWidget *panelDivider(QWidget *parent) {
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("PanelDivider"));
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background:%1;border:0;").arg(kBorder.name()));
    return line;
}

QWidget *StylePage::divided(QWidget *content) {
    // FEAT-072 B2: full-width 1 px line, 20 px above and below the block.
    auto *box = new QWidget(this);
    auto *boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(kSectionInsetPx, kSectionGapPx, kSectionInsetPx, 0);
    boxLayout->setSpacing(kSectionGapPx);
    boxLayout->addWidget(panelDivider(box));
    content->setParent(box);
    boxLayout->addWidget(content);
    return box;
}

QWidget *StylePage::section(const QString &title, QWidget *content, QWidget *header) {
    auto *box = new QWidget(this);
    auto *boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(0, 0, 0, 0);
    boxLayout->setSpacing(kRowGapPx);
    QHBoxLayout *headerLayout = nullptr;
    if (header) {
        headerLayout = new QHBoxLayout;
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(8);
    }
    auto *toggle = new QToolButton(box);
    toggle->setObjectName(QStringLiteral("SectionToggle"));
    toggle->setCheckable(true);
    toggle->setChecked(true);
    toggle->setFocusPolicy(Qt::TabFocus);
    auto label = [title](bool open) { return (open ? QStringLiteral("▾ ") : QStringLiteral("▸ ")) + title; };
    toggle->setText(label(true));
    connect(toggle, &QToolButton::toggled, this, [toggle, content, label](bool open) {
        toggle->setText(label(open));
        content->setVisible(open);
    });
    content->setParent(box);
    if (headerLayout) {
        header->setParent(box);
        headerLayout->addWidget(toggle);
        headerLayout->addStretch();
        headerLayout->addWidget(header);
        boxLayout->addLayout(headerLayout);
    } else {
        boxLayout->addWidget(toggle);
    }
    boxLayout->addWidget(content);
    return divided(box);
}

void StylePage::setNodes(const QList<const MindNode *> &nodes, const QStringList &themeColors, const QList<qreal> &widthsPx) {
    m_themeColors = themeColors;
    const bool any = !nodes.isEmpty();
    // Anteprime degli stili nel colore del (primo) nodo selezionato.
    int depth = 0;
    if (any) for (const MindNode *node = nodes.front(); node->parent; node = node->parent) ++depth;
    updatePresetPreviews(any ? nodes.front()->branchColor : QColor(themeColors.value(0, QStringLiteral("#ff6b6b"))), depth, themeColors);
    showEnum(m_preset, NodeStyle::read(nodes, StyleProperty::Preset));
    static_cast<DarkSelect *>(m_preset)->shown = nodes.size() == 1 ? topicPreview(nodes.front()) : QPixmap();
    m_preset->update();
    // FEAT-072 B15: Apply to related on one topic other than the central one (it has no siblings).
    m_styleAction->setVisible(nodes.size() == 1 && nodes.front()->parent);
    m_styleAction->setEnabled(nodes.size() == 1 && nodes.front()->style != NodeStyleOverrides());
    m_resetStyle->setVisible(any);
    m_resetStyle->setEnabled(std::any_of(nodes.cbegin(), nodes.cend(), [](const MindNode *node) { return node->style != NodeStyleOverrides(); }));
    m_childrenStyle->setVisible(any);
    m_childrenStyle->setEnabled(std::any_of(nodes.cbegin(), nodes.cend(), [](const MindNode *node) {
        return std::any_of(node->children.cbegin(), node->children.cend(), [](const auto &child) { return !child->floating; });
    }));
    showEnum(m_fillStyle, NodeStyle::read(nodes, StyleProperty::FillStyle));
    showEnum(m_borderStyle, NodeStyle::read(nodes, StyleProperty::BorderStyle));
    showEnum(m_shape, NodeStyle::read(nodes, StyleProperty::Shape));
    m_borderWidth->setReading(NodeStyle::read(nodes, StyleProperty::BorderWidth));
    m_fill->setReading(NodeStyle::read(nodes, StyleProperty::FillColor));
    m_border->setReading(NodeStyle::read(nodes, StyleProperty::BorderColor));
    m_text->setReading(NodeStyle::read(nodes, StyleProperty::TextColor));
    m_branchColor->setReading(NodeStyle::read(nodes, StyleProperty::BranchColor));
    for (ColorField *field : {m_fill, m_border, m_text, m_branchColor}) field->setEnabled(any);
    for (QWidget *field : std::initializer_list<QWidget *>{m_preset, m_shape, m_fillStyle, m_borderStyle, m_borderWidth}) field->setEnabled(any);
    showEnum(m_branchStyle, NodeStyle::read(nodes, StyleProperty::BranchStyle));
    showEnum(m_branchStart, NodeStyle::read(nodes, StyleProperty::BranchStart));  // FEAT-023
    showEnum(m_branchEnd, NodeStyle::read(nodes, StyleProperty::BranchEnd));
    {   // FEAT-072: Auto shows the shape actually drawn (curves from the central topic, rounded bracket below).
        NodeStyle::Reading shape = NodeStyle::read(nodes, StyleProperty::BranchShape);
        if (!shape.empty && !shape.mixed && static_cast<BranchShape>(shape.value.toInt()) == BranchShape::Auto)
            shape.value = static_cast<int>(nodes.front()->parent ? BranchShape::RoundedBracket : BranchShape::Curve);
        showEnum(m_branchShape, shape);
        m_branchShape->setEnabled(any);
    }
    {   // Uno spessore in px fuori dall'elenco (file di prima) si mostra come numero.
        const NodeStyle::Reading width = NodeStyle::read(nodes, StyleProperty::BranchWidth);
        const QSignalBlocker blocker(m_branchWidth);
        for (int i = m_branchWidth->count() - 1; i >= NodeStyle::kNamedBranchWidths.size(); --i) m_branchWidth->removeItem(i);
        if (!width.empty && !width.mixed && m_branchWidth->findData(width.value.toDouble()) < 0)
            m_branchWidth->addItem(tr("%1 px").arg(width.value.toDouble()), width.value.toDouble());
        m_branchWidth->setCurrentIndex(!width.empty && !width.mixed ? m_branchWidth->findData(width.value.toDouble()) : -1);
        setManualLook(m_branchWidth, !width.empty && !width.mixed && width.allManual);
    }
    // Stile e spessore valgono per le linee che partono dal nodo: hanno senso anche per il nodo
    // centrale (linee verso i rami principali, BUG-006).
    m_branchStyle->setEnabled(any);
    m_branchWidth->setEnabled(any);
    m_branchStart->setEnabled(any);
    m_branchEnd->setEnabled(any);
    showText(nodes);
    m_numbering->setNodes(nodes);
    const bool branch = nodes.size() == 1 && nodes.front()->parent;  // FEAT-026
    m_structureSection->setVisible(branch);
    if (branch) {
        m_structure->setInTable(MapStructure::insideTreeTable(nodes.front()));  // FEAT-028: no Mind map there
        m_structure->setValue(nodes.front()->structure, nodes.front()->structureDirection);
    }
    const bool central = nodes.size() == 1 && !nodes.front()->parent;
    m_rootStructureSection->setVisible(central);
    if (central) m_rootStructure->setValue(nodes.front()->structure, nodes.front()->structureDirection);
    // FEAT-021: numero intero; chiaro solo se tutti i nodi hanno la larghezza scelta a mano.
    NodeStyle::Reading width;
    const bool widthsKnown = any && widthsPx.size() == nodes.size();
    bool anyManual = false;
    if (widthsKnown) {
        width.empty = false;
        width.value = std::round(widthsPx.front());
        width.allManual = true;
        for (int i = 0; i < nodes.size(); ++i) {
            width.mixed = width.mixed || std::round(widthsPx[i]) != width.value.toDouble();
            width.allManual = width.allManual && nodes[i]->customWidth > 0;
            anyManual = anyManual || nodes[i]->customWidth > 0;
        }
    }
    m_width->setReading(width);
    m_width->setEnabled(widthsKnown);
    m_fit->setEnabled(anyManual);  // tutti gia' automatici: nulla da adattare
}

namespace {
// Nomi dei pesi nella select (FEAT-022; nomi italiani dei pesi standard, valori CSS/OpenType).
QString weightName(int weight) {
    switch (weight) {
    case 100: return StylePage::tr("Thin");
    case 200: return StylePage::tr("Extra light");
    case 300: return StylePage::tr("Light");
    case 400: return StylePage::tr("Regular");
    case 500: return StylePage::tr("Medium");
    case 600: return StylePage::tr("Semibold");
    case 700: return StylePage::tr("Bold");
    case 800: return StylePage::tr("Extra bold");
    case 900: return StylePage::tr("Black");
    default: return QString::number(weight);
    }
}

// Icona Hugeicons ricolorata (scura nel file, chiara sul pannello: AGENTS.md "Icone"), di size
// px logici alla densita' dello schermo. (Prima si chiedevano 32 px e si fissava la densita' a 2:
// con un ridimensionamento dello schermo diverso da 200% Qt restituiva gia' un'immagine piu' grande
// e l'icona usciva di 24 px invece di 16, e fuori centro.)
QPixmap tintedIcon(const QString &path, const QColor &color, int size) {
    QPixmap pixmap = QIcon(path).pixmap(QSize(size, size), qApp->devicePixelRatio());
    QPainter tint(&pixmap);
    tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tint.fillRect(pixmap.rect(), color);
    tint.end();
    return pixmap;
}

// Bottone della sezione Testo (grassetto, corsivo, barrato, sottolineato, allineamento): icona
// chiara di kIconPx, sfondo divider al passaggio e surfaceRaised se acceso, come le voci dei menu.
class IconToggle final : public QToolButton {
public:
    static constexpr int kIconPx = 16;
    IconToggle(const QString &iconPath, const QString &name, QWidget *parent) : QToolButton(parent) {
        m_icon = tintedIcon(iconPath, kText, kIconPx);
        setCheckable(true);
        setFocusPolicy(Qt::TabFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(name);
        setAccessibleName(name);
        // Riempie lo spazio dentro il bordo del riquadro, con lo stesso margine sui quattro lati
        // (prima un'altezza fissa che non contava il bordo spingeva il bottone sul lato basso).
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (isChecked() || underMouse()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(UiTheme::color(isChecked() ? UiTheme::Token::SurfaceRaised : UiTheme::Token::Divider));
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 4, 4);
        }
        painter.setOpacity(isEnabled() ? 1.0 : 0.4);
        const QSizeF icon = m_icon.deviceIndependentSize();
        painter.drawPixmap(QPointF((width() - icon.width()) / 2, (height() - icon.height()) / 2), m_icon);
    }

private:
    QPixmap m_icon;
};

// Gruppo di bottoni in un riquadro col bordo dei campi (riferimento FEAT-019/01).
QFrame *toggleBar(const QList<QToolButton *> &buttons, QWidget *parent) {
    auto *bar = new QFrame(parent);
    bar->setObjectName(QStringLiteral("ToggleBar"));
    bar->setStyleSheet(QStringLiteral("QFrame#ToggleBar { border:1px solid %1; border-radius:4px; background:transparent; }").arg(kBorder.name()));
    bar->setFixedHeight(kFieldHeightPx);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    for (QToolButton *button : buttons) {
        button->setParent(bar);
        layout->addWidget(button);
    }
    return bar;
}

// Riga senza etichetta: i campi occupano tutta la larghezza, il primo si allarga.
QWidget *fullRow(const QList<QWidget *> &fields, QWidget *parent) {
    auto *line = new QWidget(parent);
    auto *layout = new QHBoxLayout(line);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    for (int i = 0; i < fields.size(); ++i) layout->addWidget(fields[i], i == 0 ? 1 : 0);
    return line;
}

qreal pxToPt(qreal px) { return px * 3 / 4; }
qreal ptToPx(qreal pt) { return pt * 4 / 3; }
}  // namespace

QWidget *StylePage::textSection() {
    auto *content = new QWidget(this);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kRowGapPx);

    // Font: elenco a una colonna con i nomi nel carattere dell'interfaccia; la prima voce torna
    // al font di sistema (toglie la scelta).
    m_fontFamily = enumSelect(StyleProperty::FontFamily, 0);
    m_fontFamily->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_fontFamily->setMinimumWidth(80);
    m_fontFamily->setMaxVisibleItems(12);
    // FEAT-072 B12: the default shows the family name (Atkinson Hyperlegible), once.
    m_fontFamily->addItem(NodeStyle::kDefaultMapFontFamily, QString());
    NodeItem::loadBundledFonts();  // the bundled fonts (Atkinson, Roboto...) must be in the list
    for (const QString &family : QFontDatabase::families())
        if (!family.startsWith(QLatin1Char('@')) && family != NodeStyle::kDefaultMapFontFamily)
            m_fontFamily->addItem(family, family);  // "@": font verticali
    m_fontFamily->setToolTip(tr("Font"));

    // Dimensione: select modificabile, valori della barra di formattazione o un numero 8-72.
    m_fontSize = new DarkSelect(this);
    m_fontSize->setEditable(true);
    m_fontSize->setInsertPolicy(QComboBox::NoInsert);
    m_fontSize->setFixedWidth(kFieldWidthPx);
    m_fontSize->setFocusPolicy(Qt::TabFocus);
    m_fontSize->setToolTip(tr("Text size (%1-%2 px)").arg(std::ceil(ptToPx(NodeStyle::kMinFontSizePt))).arg(ptToPx(NodeStyle::kMaxFontSizePt)));
    m_fontSize->view()->setItemDelegate(new LightComboItemDelegate(m_fontSize->view()));
    for (const int px : NodeStyle::kFontSizesPx) m_fontSize->addItem(QString::number(px), pxToPt(px));
    m_fontSize->lineEdit()->setValidator(new QIntValidator(1, static_cast<int>(ptToPx(NodeStyle::kMaxFontSizePt)), m_fontSize));
    m_fontSize->lineEdit()->setPlaceholderText(QStringLiteral("—"));
    connect(m_fontSize, &QComboBox::activated, this, [this](int index) {
        emit styleChosen(StyleProperty::FontSize, m_fontSize->itemData(index).toDouble());
    });
    connect(m_fontSize->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        bool ok = false;
        const qreal size = pxToPt(m_fontSize->currentText().toInt(&ok));
        // Fuori intervallo o vuoto: torna al valore di prima (FEAT-022, come gli spessori).
        if (ok && NodeStyle::isValidValue(StyleProperty::FontSize, size)) emit styleChosen(StyleProperty::FontSize, size);
        else m_fontSize->setEditText(m_fontSize->property("shownText").toString());
    });
    layout->addWidget(fullRow({m_fontFamily, m_fontSize}, content));

    // Peso e colore.
    m_fontWeight = enumSelect(StyleProperty::FontWeight, 0);
    m_fontWeight->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_fontWeight->setMinimumWidth(80);
    m_fontWeight->setToolTip(tr("Weight"));
    layout->addWidget(fullRow({m_fontWeight, m_text}, content));

    // Grassetto (peso 700), corsivo, barrato, sottolineato e maiuscole.
    m_bold = new IconToggle(QStringLiteral(":/assets/hugeicons/text-bold.svg"), tr("Bold"), content);
    m_italic = new IconToggle(QStringLiteral(":/assets/hugeicons/text-italic.svg"), tr("Italic"), content);
    m_strikeOut = new IconToggle(QStringLiteral(":/assets/hugeicons/text-strikethrough.svg"), tr("Strikethrough"), content);
    m_underline = new IconToggle(QStringLiteral(":/assets/hugeicons/text-underline.svg"), tr("Underline"), content);
    // Il bottone cambia valore al clic; il valore vero lo rimette showText() dopo la scelta.
    connect(m_bold, &QToolButton::clicked, this, [this](bool on) {
        emit styleChosen(StyleProperty::FontWeight, on ? int(QFont::Bold) : int(QFont::Normal));
    });
    connect(m_italic, &QToolButton::clicked, this, [this](bool on) { emit styleChosen(StyleProperty::Italic, on); });
    connect(m_strikeOut, &QToolButton::clicked, this, [this](bool on) { emit styleChosen(StyleProperty::StrikeOut, on); });
    connect(m_underline, &QToolButton::clicked, this, [this](bool on) { emit styleChosen(StyleProperty::Underline, on); });
    m_textCase = enumSelect(StyleProperty::TextCase, 64);
    const QList<std::pair<QString, QString>> cases{{QStringLiteral("Aa"), tr("No change")},
                                                   {QStringLiteral("AA"), tr("All uppercase")},
                                                   {QStringLiteral("aa"), tr("All lowercase")}};
    for (int i = 0; i < cases.size(); ++i) {
        m_textCase->addItem(cases[i].first, i);
        m_textCase->setItemData(i, cases[i].second, Qt::ToolTipRole);
        m_textCase->setItemData(i, cases[i].second, Qt::AccessibleTextRole);
    }
    m_textCase->setToolTip(tr("Letter case (map only)"));
    layout->addWidget(fullRow({toggleBar({m_bold, m_italic, m_strikeOut, m_underline}, content), m_textCase}, content));

    // Allineamento: quattro bottoni, uno acceso.
    const QList<std::pair<QString, QString>> aligns{{QStringLiteral("left"), tr("Align left")},
                                                    {QStringLiteral("center"), tr("Center")},
                                                    {QStringLiteral("right"), tr("Align right")},
                                                    {QStringLiteral("justify"), tr("Justify")}};
    QList<QToolButton *> alignButtons;
    for (int i = 0; i < aligns.size(); ++i) {
        auto *button = new IconToggle(QStringLiteral(":/assets/hugeicons/text-align-%1.svg").arg(aligns[i].first), aligns[i].second, content);
        connect(button, &QToolButton::clicked, this, [this, i] { emit styleChosen(StyleProperty::TextAlign, i); });
        m_align.append(button);
        alignButtons.append(button);
    }
    layout->addWidget(toggleBar(alignButtons, content));
    return content;
}

void StylePage::fillWeights(const QString &family, int current) {
    // Pesi che il font offre (stili non corsivi); il peso attuale c'e' sempre.
    NodeItem::loadBundledFonts();
    const QString shown = family.isEmpty() ? NodeStyle::kDefaultMapFontFamily : family;  // predefinito: Atkinson Hyperlegible
    if (shown != m_weightsFamily || m_fontWeight->findData(current) < 0) {
        const QSignalBlocker blocker(m_fontWeight);
        QSet<int> weights{QFont::Normal, QFont::Bold};
        for (const QString &style : QFontDatabase::styles(shown))
            if (!QFontDatabase::italic(shown, style)) weights.insert(QFontDatabase::weight(shown, style));
        if (current > 0) weights.insert(current);
        QList<int> sorted(weights.cbegin(), weights.cend());
        std::sort(sorted.begin(), sorted.end());
        m_fontWeight->clear();
        for (const int weight : std::as_const(sorted))
            if (weight >= 100 && weight <= 900 && weight % 100 == 0) m_fontWeight->addItem(weightName(weight), weight);
        m_weightsFamily = shown;
    }
}

void StylePage::showText(const QList<const MindNode *> &nodes) {
    const bool any = !nodes.isEmpty();
    const NodeStyle::Reading family = NodeStyle::read(nodes, StyleProperty::FontFamily);
    {
        // Font non installato: si mostra con il suo nome (il nodo usa quello di sistema).
        const QString name = family.value.toString();
        if (!family.mixed && !family.empty && m_fontFamily->findData(name) < 0) {
            const QSignalBlocker blocker(m_fontFamily);
            m_fontFamily->addItem(tr("%1 (not installed)").arg(name), name);
        }
    }
    showEnum(m_fontFamily, family);
    if (!family.mixed && !family.empty) {
        const QSignalBlocker blocker(m_fontFamily);
        m_fontFamily->setCurrentIndex(m_fontFamily->findData(family.value.toString()));
    }
    const NodeStyle::Reading weight = NodeStyle::read(nodes, StyleProperty::FontWeight);
    fillWeights(family.mixed || family.empty ? QString() : family.value.toString(), weight.mixed ? 0 : weight.value.toInt());
    showEnum(m_fontWeight, weight);

    const NodeStyle::Reading size = NodeStyle::read(nodes, StyleProperty::FontSize);
    {
        const QSignalBlocker blocker(m_fontSize);
        // Shown in px (FEAT-072 Q5): 13.5 pt = 18 px; old whole points may give a decimal.
        const QString text = size.empty || size.mixed ? QString() : QString::number(ptToPx(size.value.toDouble()), 'g', 4);
        m_fontSize->setCurrentIndex(-1);
        m_fontSize->setEditText(text);
        m_fontSize->setProperty("shownText", text);
    }
    setManualLook(m_fontSize, !size.empty && !size.mixed && size.allManual);
    setManualLook(m_fontSize->lineEdit(), !size.empty && !size.mixed && size.allManual);

    auto showToggle = [&nodes](QToolButton *button, StyleProperty property, auto isOn) {
        const NodeStyle::Reading reading = NodeStyle::read(nodes, property);
        const QSignalBlocker blocker(button);
        button->setChecked(!reading.empty && !reading.mixed && isOn(reading.value));  // misti: spento
    };
    showToggle(m_bold, StyleProperty::FontWeight, [](const QVariant &value) { return value.toInt() >= QFont::Bold; });
    showToggle(m_italic, StyleProperty::Italic, [](const QVariant &value) { return value.toBool(); });
    showToggle(m_strikeOut, StyleProperty::StrikeOut, [](const QVariant &value) { return value.toBool(); });
    showToggle(m_underline, StyleProperty::Underline, [](const QVariant &value) { return value.toBool(); });
    showEnum(m_textCase, NodeStyle::read(nodes, StyleProperty::TextCase));
    const NodeStyle::Reading align = NodeStyle::read(nodes, StyleProperty::TextAlign);
    for (int i = 0; i < m_align.size(); ++i) {
        const QSignalBlocker blocker(m_align[i]);
        m_align[i]->setChecked(!align.empty && !align.mixed && align.value.toInt() == i);
    }
    for (QWidget *field : std::initializer_list<QWidget *>{m_fontFamily, m_fontSize, m_fontWeight, m_textCase, m_bold, m_italic,
                                                           m_strikeOut, m_underline})
        field->setEnabled(any);
    for (QToolButton *button : std::as_const(m_align)) button->setEnabled(any);
}

namespace {
const QString kFavoritesKey = QStringLiteral("palettes/favorites");

// Stella a cinque punte nel riquadro.
QPolygonF starShape(const QRectF &box) {
    QPolygonF star;
    const QPointF center = box.center();
    const qreal outer = box.width() / 2, inner = outer * 0.45;
    for (int i = 0; i < 10; ++i) {
        const qreal radius = i % 2 == 0 ? outer : inner;
        const qreal angle = -std::numbers::pi / 2 + i * std::numbers::pi / 5;
        star << QPointF(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
    }
    return star;
}

// Striscia dei colori di una palette: rettangoli uguali uniti, angoli arrotondati alle estremita'.
void paintStrip(QPainter &painter, const QRectF &rect, const QStringList &colors) {
    if (colors.isEmpty()) return;
    QPainterPath clip;
    clip.addRoundedRect(rect, 4, 4);
    painter.save();
    painter.setClipPath(clip);
    const qreal width = rect.width() / colors.size();
    for (int i = 0; i < colors.size(); ++i)
        painter.fillRect(QRectF(rect.left() + i * width, rect.top(), width + 0.5, rect.height()), QColor(colors[i]));
    painter.restore();
    painter.setPen(QPen(kBorder, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
}

class PaletteTile final : public QToolButton {
public:
    std::function<void()> favoriteToggled;  // la stella e' stata premuta (il clic non sceglie la palette)

    PaletteTile(const NodeStyle::Palette &palette, bool current, QWidget *parent)
        : QToolButton(parent), m_palette(palette), m_current(current) {
        setFixedSize(PalettePicker::kTileWidthPx, PalettePicker::kTileHeightPx);
        setFocusPolicy(Qt::TabFocus);  // Tab sposta il fuoco, Invio applica (PalettePicker::keyPressEvent)
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(NodeStyle::displayName(palette));
    }
    QString paletteId() const { return m_palette.id; }
    static QRectF starRect() { return {4, 7, PalettePicker::kStarPx, PalettePicker::kStarPx}; }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && starRect().adjusted(-3, -3, 3, 3).contains(event->position())) {
            PalettePicker::setFavorite(m_palette.id, !PalettePicker::isFavorite(m_palette.id));
            update();
            event->accept();
            if (favoriteToggled) favoriteToggled();
            return;
        }
        QToolButton::mousePressEvent(event);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (underMouse() || hasFocus()) { painter.setPen(Qt::NoPen); painter.setBrush(UiTheme::color(UiTheme::Token::Divider)); painter.drawRoundedRect(box, 4, 4); }
        QFont font = painter.font();
        font.setPixelSize(UiTheme::fontPx(12));
        font.setWeight(m_current ? QFont::DemiBold : QFont::Normal);
        painter.setFont(font);
        // Stella: vuota al passaggio del mouse, gialla e sempre visibile se la palette e' preferita.
        const bool favorite = PalettePicker::isFavorite(m_palette.id);
        if (favorite || underMouse()) {
            painter.setPen(favorite ? QPen(QColor("#f5b301"), 1) : QPen(kText, 1.2));
            painter.setBrush(favorite ? QColor("#f5c518") : Qt::transparent);
            painter.drawPolygon(starShape(starRect()));
        }
        painter.setPen(kText);
        painter.drawText(QRectF(22, 4, width() - 26, 20), Qt::AlignLeft | Qt::AlignVCenter, NodeStyle::displayName(m_palette));
        const QRectF strip(4, 28, width() - 8, 24);
        if (m_current) {
            painter.setPen(QPen(kText, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(strip.adjusted(-2, -2, 2, 2), 6, 6);
        }
        paintStrip(painter, strip, m_palette.colors);
    }

private:
    NodeStyle::Palette m_palette;
    bool m_current;
};
}  // namespace

QStringList PalettePicker::favoriteIds() {
    QStringList ids;
    for (const QString &id : QSettings().value(kFavoritesKey).toStringList())
        if (CustomPalettes::lookup(id)) ids.append(id);  // una palette personalizzata rimossa sparisce
    return ids;
}

bool PalettePicker::isFavorite(const QString &id) { return QSettings().value(kFavoritesKey).toStringList().contains(id); }

void PalettePicker::setFavorite(const QString &id, bool favorite) {
    QSettings settings;
    QStringList ids = settings.value(kFavoritesKey).toStringList();
    ids.removeAll(id);
    if (favorite) ids.append(id);
    settings.setValue(kFavoritesKey, ids);
}

PalettePicker *PalettePicker::openPicker() { return qobject_cast<PalettePicker *>(QApplication::activePopupWidget()); }

PalettePicker::PalettePicker(const QString &currentId, std::function<void(const QString &)> onChosen, PalettePickerActions actions)
    : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      m_currentId(currentId), m_onChosen(std::move(onChosen)), m_actions(std::move(actions)) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet(QStringLiteral("QLabel { color:#6b7280; font-size:12px; }"
                                 "QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; border:0; }"
                                 "QScrollBar:vertical { background:transparent; width:6px; margin:0; }"
                                 "QScrollBar::handle:vertical { background:#4b5563; min-height:24px; border-radius:3px; }"
                                 "QScrollBar::handle:vertical:hover { background:#6b7280; }"
                                 "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
                                 "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(ColorPicker::kPaddingPx, ColorPicker::kPaddingPx, ColorPicker::kPaddingPx, ColorPicker::kPaddingPx);
    layout->setSpacing(8);
    m_selector = new SegmentedControl({tr("Favorites"), tr("Colorful"), tr("Classic"), tr("Custom")}, this);
    layout->addWidget(m_selector);
    m_pages = new QStackedWidget(this);
    QStringList colorful, classic, custom;
    for (const NodeStyle::Palette &palette : NodeStyle::basePalettes())
        (palette.group == NodeStyle::PaletteGroup::Colorato ? colorful : classic).append(palette.id);
    for (const NodeStyle::Palette &palette : CustomPalettes::all()) custom.append(palette.id);  // FEAT-032
    m_pages->addWidget(page(favoriteIds(), tr("Click the star of a palette to add it here.")));
    m_pages->addWidget(page(colorful, QString()));
    m_pages->addWidget(page(classic, QString()));
    m_pages->addWidget(page(custom, tr("The palettes you create with + will appear here.")));
    layout->addWidget(m_pages, 0, Qt::AlignHCenter);
    // FEAT-032: palette del foglio che questo computer non conosce: si puo' aggiungere alle proprie.
    if (m_actions.addCurrent) {
        auto *add = new QToolButton(this);
        add->setObjectName(QStringLiteral("AddCurrentPalette"));
        add->setText(tr("Add \"%1\" to my palettes").arg(m_actions.notInstalledName));
        add->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        add->setFixedHeight(kFieldHeightPx);
        add->setCursor(Qt::PointingHandCursor);
        add->setStyleSheet(UiTheme::qss(QStringLiteral(
            "QToolButton { color:@text; background:@surfaceRaised; border:1px solid @divider; border-radius:4px; font-size:12px; }"
            "QToolButton:hover { background:@divider; } QToolButton:focus { border-color:@accent; }")));
        connect(add, &QToolButton::clicked, this, [this] {
            m_pending = m_actions.addCurrent;
            close();
        });
        layout->addWidget(add);
    }
    connect(m_selector, &SegmentedControl::segmentActivated, this, &PalettePicker::showGroup);
    const auto current = CustomPalettes::lookup(currentId);
    int group = 1;
    if (current && current->group == NodeStyle::PaletteGroup::Classico) group = 2;
    if (current && current->group == NodeStyle::PaletteGroup::Personalizzata) group = 3;
    showGroup(group);
    adjustSize();
}

QWidget *PalettePicker::page(const QStringList &ids, const QString &emptyText) {
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    auto *grid = new QGridLayout(content);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(ColorPicker::kGapPx);
    grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    int index = 0;
    for (const QString &id : ids) {
        const auto palette = CustomPalettes::lookup(id);
        if (!palette) continue;
        auto *tile = new PaletteTile(*palette, id == m_currentId, content);
        tile->favoriteToggled = [this] { QTimer::singleShot(0, this, &PalettePicker::rebuildFavorites); };
        connect(tile, &QToolButton::clicked, this, [this, id] {
            if (m_onChosen) m_onChosen(id);
            close();
        });
        // FEAT-032: tasto destro su una palette personalizzata: "Rimuovi" (con conferma, dopo la
        // chiusura del popup).
        if (palette->group == NodeStyle::PaletteGroup::Personalizzata && m_actions.remove) {
            tile->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(tile, &QWidget::customContextMenuRequested, this, [this, id, tile](const QPoint &position) {
                QMenu menu(this);
                menu.setStyleSheet(UiTheme::qss(QStringLiteral(
                    "QMenu { background:@surface; color:@text; border:1px solid @divider; border-radius:8px; padding:4px; }"
                    "QMenu::item { padding:6px 16px; border-radius:4px; } QMenu::item:selected { background:@surfaceRaised; }")));
                const QAction *remove = menu.addAction(tr("Remove"));
                if (menu.exec(tile->mapToGlobal(position)) != remove) return;
                m_pending = [remove = m_actions.remove, id] { remove(id); };  // copia: il popup non esiste piu'
                close();
            });
        }
        grid->addWidget(tile, index / 2, index % 2);
        ++index;
    }
    if (index == 0 && !emptyText.isEmpty()) {
        auto *label = new QLabel(emptyText, content);
        label->setWordWrap(true);
        grid->addWidget(label, 0, 0, 1, 2);
    }
    scroll->setWidget(content);
    // Due colonne e fino a 6 righe visibili; oltre si scorre.
    const int rows = std::min(6, std::max(1, (index + 1) / 2));
    scroll->setFixedSize(2 * kTileWidthPx + ColorPicker::kGapPx + 12, rows * kTileHeightPx + (rows - 1) * ColorPicker::kGapPx);
    return scroll;
}

int PalettePicker::currentGroup() const { return m_pages->currentIndex(); }

void PalettePicker::rebuildFavorites() {
    // Il gruppo Preferito si rifa' fuori dal clic sulla stella (la casella premuta puo' esserne parte).
    const int shown = m_pages->currentIndex();
    QWidget *old = m_pages->widget(0);
    m_pages->insertWidget(0, page(favoriteIds(), tr("Click the star of a palette to add it here.")));
    m_pages->removeWidget(old);
    old->deleteLater();
    m_pages->setCurrentIndex(shown);
    // Le stelle dello stesso id negli altri gruppi si ridisegnano.
    for (auto *tile : findChildren<QToolButton *>()) tile->update();
    adjustSize();
}

void PalettePicker::showGroup(int index) {
    m_pages->setCurrentIndex(index);
    m_selector->setCurrentIndex(index);
    adjustSize();
}

void PalettePicker::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(kBorder, 1));
    painter.setBrush(kSurface);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadiusPx, kRadiusPx);
}

void PalettePicker::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) { close(); event->accept(); return; }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (auto *tile = dynamic_cast<PaletteTile *>(focusWidget())) { tile->click(); event->accept(); return; }
    }
    QWidget::keyPressEvent(event);
}

void PalettePicker::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    if (m_loop) m_loop->quit();
}

void PalettePicker::exec(const QRect &anchor, const QString &currentId, const std::function<void(const QString &)> &onChosen,
                         const PalettePickerActions &actions) {
    auto *picker = new PalettePicker(currentId, onChosen, actions);
    constexpr int kDistancePx = 8;
    QScreen *screen = QGuiApplication::screenAt(anchor.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    QPoint position(anchor.left() - kDistancePx - picker->width(), anchor.top());
    if (screen) {
        const QRect area = screen->availableGeometry();
        if (position.x() < area.left()) position.setX(anchor.right() + 1 + kDistancePx);
        position.setX(std::clamp(position.x(), area.left(), std::max(area.left(), area.right() - picker->width())));
        position.setY(std::clamp(position.y(), area.top(), std::max(area.top(), area.bottom() - picker->height())));
    }
    picker->move(position);
    QEventLoop loop;
    picker->m_loop = &loop;
    const QPointer<PalettePicker> guard(picker);
    picker->show();
    loop.exec();
    std::function<void()> pending;
    if (guard) {
        guard->m_loop = nullptr;
        pending = std::move(guard->m_pending);
    }
    if (pending) pending();  // popup chiuso: ora si possono aprire modali (FEAT-032)
}

PaletteSelect::PaletteSelect(QWidget *parent) : QToolButton(parent) {
    setFixedHeight(kFieldHeightPx);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::TabFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    connect(this, &QToolButton::clicked, this, [this] {
        const QRect anchor(mapToGlobal(QPoint(0, 0)), size());
        PalettePickerActions actions;
        // FEAT-032: "Rimuovi" chiede conferma, perche' non si annulla con Ctrl+Z (impostazione
        // dell'app, non del documento); i fogli che la usano conservano i colori.
        actions.remove = [this](const QString &id) {
            const auto palette = CustomPalettes::find(id);
            if (!palette) return;
            const auto choice = ModalDialog::ask(window(), tr("Remove palette"),
                tr("\"%1\" will be removed from your palettes. Sheets that use it keep their colors. This cannot be undone.")
                    .arg(palette->name), tr("Remove"));
            if (choice != ModalDialog::Choice::Primary) return;
            CustomPalettes::remove(id);
            update();  // il nome puo' diventare "(non installata)"
        };
        if (!m_savedName.isEmpty() && !CustomPalettes::lookup(m_id)) {
            actions.notInstalledName = m_savedName;
            actions.addCurrent = [this] {
                QString name = m_savedName;
                for (int number = 2; CustomPalettes::isNameTaken(name); ++number) name = QStringLiteral("%1 %2").arg(m_savedName).arg(number);
                CustomPalettes::add(name, m_colors, m_id);
                setToolTip(paletteName());
                update();
            };
        }
        PalettePicker::exec(anchor, m_id, [this](const QString &id) { emit paletteChosen(id); }, actions);
    });
}

QString PaletteSelect::paletteName() const {
    if (const auto palette = CustomPalettes::lookup(m_id)) return NodeStyle::displayName(*palette);
    if (!m_savedName.isEmpty()) return tr("%1 (not installed)").arg(m_savedName);
    return m_id.isEmpty() ? tr("Custom") : tr("Template palette");
}

void PaletteSelect::setPalette(const QString &id, const QStringList &colors, const QString &savedName) {
    m_id = id;
    m_colors = colors;
    m_savedName = savedName;
    setToolTip(paletteName());
    update();
}

void PaletteSelect::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(hasFocus() ? QColor("#7047eb") : kBorder, 1));
    painter.setBrush(underMouse() ? UiTheme::color(UiTheme::Token::Divider) : Qt::transparent);
    painter.drawRoundedRect(box, 4, 4);
    paintStrip(painter, QRectF(8, 8, 72, height() - 16), m_colors);
    QFont font = painter.font();
    font.setPixelSize(UiTheme::fontPx(12));
    painter.setFont(font);
    painter.setPen(kText);
    painter.drawText(QRectF(88, 0, width() - 88 - 32, height()), Qt::AlignVCenter | Qt::AlignLeft, paletteName());
    static const QPixmap chevron = [] {
        QPixmap tinted = QIcon(QStringLiteral(":/assets/hugeicons/arrow-down.svg")).pixmap(32, 32);
        QPainter tint(&tinted);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(tinted.rect(), kText);
        return tinted;
    }();
    painter.drawPixmap(QRect(width() - 24, (height() - 16) / 2, 16, 16), chevron);
}

// --- FEAT-040: pagina Stile dell'immagine ---

ImageStylePage::ImageStylePage(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ImageStylePage"));
    setStyleSheet(panelPageStyleSheet(objectName()) + UiTheme::qss(QStringLiteral(
        "QLabel#FieldCaption { color:@textMuted; font-size:12px; }"
        "QToolButton#ChainButton { background:transparent; border:1px solid transparent; border-radius:4px; }"
        "QToolButton#ChainButton:hover { background:@divider; }"
        "QToolButton#ChainButton:checked { background:@surfaceRaised; }"
        "QToolButton#ChainButton:focus { border-color:@accent; }"
        "QSlider::groove:horizontal { height:4px; background:@surfaceRaised; border-radius:2px; }"
        "QSlider::sub-page:horizontal { background:@text; border-radius:2px; }"
        "QSlider::handle:horizontal { background:@text; width:14px; height:14px; margin:-5px 0; border-radius:7px; }"
        "QSlider::handle:horizontal:focus { background:@accent; }"
        "QSlider::sub-page:horizontal:disabled, QSlider::handle:horizontal:disabled { background:@textMuted; }")));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    const auto row = [this](const QString &label, const QList<QWidget *> &fields) {
        auto *line = new QWidget(this);
        auto *lineLayout = new QHBoxLayout(line);
        lineLayout->setContentsMargins(0, 0, 0, 0);
        lineLayout->setSpacing(8);
        if (!label.isEmpty()) lineLayout->addWidget(rowLabel(label, fields, line));
        lineLayout->addStretch();
        for (QWidget *field : fields) lineLayout->addWidget(field);
        return line;
    };
    const auto button = [this](const QString &text) {
        auto *result = new QToolButton(this);
        result->setObjectName(QStringLiteral("FitButton"));  // stesso aspetto di "Adatta" (FEAT-021)
        result->setText(text);
        result->setAccessibleName(text);
        result->setFixedHeight(kFieldHeightPx);
        result->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        result->setFocusPolicy(Qt::TabFocus);
        return result;
    };

    // Dimensioni: larghezza, catena, altezza; sotto i nomi dei campi; poi "Ripristina dimensione".
    constexpr int kSizeFieldPx = 68;
    const int minWidth = static_cast<int>(NodeItem::kMinImageWidthPx);
    const int maxSide = static_cast<int>(NodeItem::kMaxImageWidthPx);
    m_width = new PixelField(minWidth, maxSide, tr("Image width (%1-%2 px)").arg(minWidth).arg(maxSide), this);
    m_height = new PixelField(1, maxSide, tr("Image height (1-%1 px)").arg(maxSide), this);
    for (PixelField *field : {m_width, m_height}) field->setFixedWidth(kSizeFieldPx);
    m_width->setAccessibleName(tr("Width"));
    m_height->setAccessibleName(tr("Height"));
    m_chain = new QToolButton(this);
    m_chain->setObjectName(QStringLiteral("ChainButton"));
    m_chain->setCheckable(true);
    m_chain->setChecked(true);  // decisione dell'utente: proporzioni mantenute di base
    m_chain->setIcon(QIcon(QStringLiteral(":/assets/hugeicons/menu-link.svg")));
    m_chain->setIconSize(QSize(16, 16));
    m_chain->setFixedSize(kFieldHeightPx, kFieldHeightPx);
    m_chain->setFocusPolicy(Qt::TabFocus);
    m_chain->setToolTip(tr("Keep proportions"));
    m_chain->setAccessibleName(tr("Keep proportions"));
    connect(m_width, &PixelField::valueChosen, this, &ImageStylePage::chooseWidth);
    connect(m_height, &PixelField::valueChosen, this, &ImageStylePage::chooseHeight);
    auto *captions = new QWidget(this);
    auto *captionLayout = new QHBoxLayout(captions);
    captionLayout->setContentsMargins(0, 0, 0, 0);
    captionLayout->setSpacing(8);
    captionLayout->addStretch();
    for (const QString &text : {tr("Width"), QString(), tr("Height")}) {
        auto *caption = new QLabel(text, captions);
        caption->setObjectName(QStringLiteral("FieldCaption"));
        caption->setAlignment(Qt::AlignCenter);
        caption->setFixedWidth(text.isEmpty() ? kFieldHeightPx : kSizeFieldPx);
        captionLayout->addWidget(caption);
    }
    m_resetSize = button(tr("Reset size"));
    connect(m_resetSize, &QToolButton::clicked, this, &ImageStylePage::sizeResetRequested);
    auto *size = new QWidget(this);
    auto *sizeLayout = new QVBoxLayout(size);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->setSpacing(4);
    sizeLayout->addWidget(row(tr("Dimensions (px)"), {m_width, m_chain, m_height}));
    sizeLayout->addWidget(captions);
    sizeLayout->addSpacing(4);
    sizeLayout->addWidget(m_resetSize);
    layout->addWidget(size);

    // Rotazione di 90 gradi in senso orario (richiesta dell'utente 2026-10-20): immagine, adesivo, formula.
    m_rotate = button(tr("Rotate 90°"));
    m_rotate->setIcon(QIcon(QStringLiteral(":/assets/hugeicons/menu-rotate.svg")));
    m_rotate->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_rotate, &QToolButton::clicked, this, &ImageStylePage::rotateRequested);
    layout->addWidget(m_rotate);

    // Bordo: tipo (select a icone, come i nodi), colore, spessore.
    constexpr int kTypeSelectWidthPx = 64;
    auto *border = new DarkSelect(this);
    border->setFixedWidth(kTypeSelectWidthPx);
    border->setFocusPolicy(Qt::TabFocus);
    border->setAccessibleName(tr("Border"));
    border->setPlaceholderText(QStringLiteral("—"));
    border->view()->setItemDelegate(new LightComboItemDelegate(border->view()));
    border->setIconSize(kTypeIconSize);
    const QList<std::tuple<ImageStyle::Border, QString, Qt::PenStyle>> borders{
        {ImageStyle::Border::Solid, tr("Solid"), Qt::SolidLine},
        {ImageStyle::Border::Dashed, tr("Dashed"), Qt::DashLine},
        {ImageStyle::Border::Dotted, tr("Dotted"), Qt::DotLine}};
    for (const auto &[type, name, penStyle] : borders) {
        const Qt::PenStyle pen = penStyle;
        addTypeItem(border, static_cast<int>(type), name, [pen](QPainter &painter, const QRectF &box) {
            painter.setPen(QPen(kText, 1.5, pen));
            painter.drawRect(box);
        });
    }
    addTypeItem(border, static_cast<int>(ImageStyle::Border::None), tr("No border"), drawNone);
    border->gridMenu = {2, border->findData(static_cast<int>(ImageStyle::Border::None)), QString()};
    m_border = border;
    connect(m_border, &QComboBox::activated, this, [this](int index) {
        const auto type = static_cast<ImageStyle::Border>(m_border->itemData(index).toInt());
        chooseStyle([type](ImageStyle &style) { style.border = type; });
    });
    m_borderWidth = new PixelField(NodeStyle::kMinBorderWidthPx, NodeStyle::kMaxBorderWidthPx,
                                   tr("Border thickness (%1-%2 px). Up and down arrows: 1 px, with Shift 10 px")
                                       .arg(NodeStyle::kMinBorderWidthPx).arg(NodeStyle::kMaxBorderWidthPx), this);
    m_borderWidth->setAccessibleName(tr("Thickness"));
    connect(m_borderWidth, &PixelField::valueChosen, this, [this](qreal width) {
        chooseStyle([width](ImageStyle &style) { style.borderWidth = width; });
    });
    m_borderColor = new ColorField(this);
    m_borderColor->setAccessibleName(tr("Border color"));
    connect(m_borderColor, &ColorField::colorChosen, this, [this](const QColor &color) {
        chooseStyle([color](ImageStyle &style) { style.borderColor = color; });
    });
    auto *borderBox = new QWidget(this);
    auto *borderLayout = new QVBoxLayout(borderBox);
    borderLayout->setContentsMargins(0, 0, 0, 0);
    borderLayout->setSpacing(8);
    borderLayout->addWidget(row(tr("Border"), {m_border, m_borderColor}));
    borderLayout->addWidget(row(tr("Thickness (px)"), {m_borderWidth}));
    layout->addWidget(borderBox);

    // Ombra: interruttore (FEAT-035).
    m_shadow = new ToggleSwitch(this);
    m_shadow->setAccessibleName(tr("Shadow"));
    connect(m_shadow, &ToggleSwitch::toggled, this, [this](bool on) {
        chooseStyle([on](ImageStyle &style) { style.shadow = on; });
    });
    layout->addWidget(row(tr("Shadow"), {m_shadow}));

    // FEAT-051: contorno bianco e scuro degli adesivi; la riga c'e' solo per un adesivo.
    m_outline = new ToggleSwitch(this);
    m_outline->setAccessibleName(tr("Sticker outline"));
    connect(m_outline, &ToggleSwitch::toggled, this, [this](bool on) {
        chooseStyle([on](ImageStyle &style) { style.outline = on; });
    });
    m_outlineRow = row(tr("Sticker outline"), {m_outline});
    layout->addWidget(m_outlineRow);

    // Opacita': cursore e campo in %. Trascinando il cursore la scelta arriva al rilascio (un passo
    // di storico); clic sulla barra e frecce della tastiera scelgono a ogni scatto.
    m_opacitySlider = new QSlider(Qt::Horizontal, this);
    m_opacitySlider->setRange(ImageStyle::kMinOpacity, ImageStyle::kMaxOpacity);
    m_opacitySlider->setAccessibleName(tr("Opacity"));
    m_opacitySlider->setFocusPolicy(Qt::TabFocus);
    m_opacityField = new QLineEdit(this);
    m_opacityField->setFixedWidth(64);
    m_opacityField->setAccessibleName(tr("Opacity"));
    m_opacityField->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\s*\\d{0,3}\\s*%?\\s*")), this));
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_opacityField->setText(QStringLiteral("%1%").arg(value));
        if (!m_opacitySlider->isSliderDown()) chooseStyle([value](ImageStyle &style) { style.opacity = value; });
    });
    connect(m_opacitySlider, &QSlider::sliderReleased, this, [this] {
        const int value = m_opacitySlider->value();
        chooseStyle([value](ImageStyle &style) { style.opacity = value; });
    });
    connect(m_opacityField, &QLineEdit::editingFinished, this, [this] {
        bool ok = false;
        const int value = m_opacityField->text().remove(QLatin1Char('%')).trimmed().toInt(&ok);
        if (!ok) { showOpacity(m_style.opacity); return; }
        const int clamped = std::clamp(value, ImageStyle::kMinOpacity, ImageStyle::kMaxOpacity);
        showOpacity(clamped);
        chooseStyle([clamped](ImageStyle &style) { style.opacity = clamped; });
    });
    auto *opacity = new QWidget(this);
    auto *opacityLayout = new QVBoxLayout(opacity);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->setSpacing(8);
    opacityLayout->addWidget(new QLabel(tr("Opacity"), opacity));
    auto *opacityRow = new QHBoxLayout;
    opacityRow->setSpacing(8);
    opacityRow->addWidget(m_opacitySlider, 1);
    opacityRow->addWidget(m_opacityField);
    opacityLayout->addLayout(opacityRow);
    layout->addWidget(opacity);

    m_resetStyle = button(tr("Reset style"));
    connect(m_resetStyle, &QToolButton::clicked, this, [this] {
        // Bordo, ombra, opacita'; un adesivo torna con il suo contorno.
        // La rotazione non e' uno stile: resta (la misura del nodo e' gia' quella ruotata).
        chooseStyle([sticker = m_sticker, rotation = m_style.rotation](ImageStyle &style) {
            style = ImageStyle();
            style.outline = sticker;
            style.rotation = rotation;
        });
    });
    layout->addWidget(m_resetStyle);
    layout->addStretch();
    setNode(nullptr);
}

void ImageStylePage::setNode(const MindNode *node) {
    const bool any = node && (!node->imagePath.isEmpty() || !node->formula.isEmpty());  // FEAT-064: anche la formula
    m_size = any ? node->imageSize : QSizeF();
    m_style = any ? node->imageStyle : ImageStyle();
    const auto reading = [any](const QVariant &value) {
        NodeStyle::Reading result;
        result.empty = !any;
        result.value = value;
        result.allManual = true;  // valori dell'immagine: sempre propri, testo chiaro
        return result;
    };
    m_width->setReading(reading(std::round(m_size.width())));
    m_height->setReading(reading(std::round(m_size.height())));
    m_borderWidth->setReading(reading(m_style.borderWidth));
    {
        const QSignalBlocker blocker(m_border);
        m_border->setCurrentIndex(any ? m_border->findData(static_cast<int>(m_style.border)) : -1);
    }
    setManualLook(m_border, any);
    m_borderColor->setReading(reading(m_style.borderColor));
    {
        const QSignalBlocker blocker(m_shadow);
        m_shadow->setChecked(m_style.shadow);
    }
    m_sticker = any && node->imageSticker && !node->imagePath.isEmpty();
    m_outlineRow->setVisible(m_sticker);
    {
        const QSignalBlocker blocker(m_outline);
        m_outline->setChecked(m_sticker && m_style.outline);
    }
    showOpacity(m_style.opacity);
    // Spessore e colore servono solo con un bordo.
    const bool bordered = any && m_style.border != ImageStyle::Border::None;
    m_borderWidth->setEnabled(bordered);
    m_borderColor->setEnabled(bordered);
    for (QWidget *field : std::initializer_list<QWidget *>{m_width, m_height, m_chain, m_resetSize, m_rotate, m_border, m_shadow, m_outline,
                                                          m_opacitySlider, m_opacityField, m_resetStyle})
        field->setEnabled(any);
}

void ImageStylePage::chooseStyle(const std::function<void(ImageStyle &)> &change) {
    ImageStyle style = m_style;
    change(style);
    if (style == m_style) return;
    emit styleChosen(style);
}

void ImageStylePage::chooseWidth(qreal width) {
    if (m_size.isEmpty()) return;
    qreal height = m_size.height();
    if (m_chain->isChecked())
        height = std::clamp<qreal>(std::round(width * m_size.height() / m_size.width()), 1, NodeItem::kMaxImageWidthPx);
    emit sizeChosen({width, height});
}

void ImageStylePage::chooseHeight(qreal height) {
    if (m_size.isEmpty()) return;
    qreal width = m_size.width();
    if (m_chain->isChecked())
        width = std::clamp(std::round(height * m_size.width() / m_size.height()), NodeItem::kMinImageWidthPx, NodeItem::kMaxImageWidthPx);
    emit sizeChosen({width, height});
}

void ImageStylePage::showOpacity(int percent) {
    const QSignalBlocker blocker(m_opacitySlider);
    m_opacitySlider->setValue(percent);
    m_opacityField->setText(QStringLiteral("%1%").arg(percent));
}

// --- FEAT-036: varianti del tema colore ---

namespace {
// Piccola mappa di una variante: centrale e quattro rami principali, con i colori calcolati dalle
// stesse regole della mappa.
QPixmap variantThumbnail(const QString &paletteId, const QStringList &palette, int variant, QSize size) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap(size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    const NodeStyle::Backdrop backdrop = NodeStyle::backdropFor(paletteId, palette, variant, std::nullopt);
    auto root = std::make_unique<MindNode>();
    for (int i = 0; i < 4; ++i) {
        auto main = std::make_unique<MindNode>();
        main->parent = root.get();
        root->children.push_back(std::move(main));
    }
    NodeStyle::resolveTree(root.get(), palette, backdrop);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box(QPointF(0, 0), QSizeF(size));
    painter.setPen(Qt::NoPen);
    painter.setBrush(backdrop.color);
    painter.drawRoundedRect(box, 4, 4);
    const QPointF center = box.center();
    const QRectF central(center.x() - 12, center.y() - 3, 24, 6);
    const qreal mainWidth = 18, mainHeight = 7;
    for (int i = 0; i < 4; ++i) {
        const MindNode *node = root->children[static_cast<size_t>(i)].get();
        const bool right = i % 2 == 0;
        const qreal y = center.y() + (i < 2 ? -9 : 9) - mainHeight / 2;
        const qreal x = right ? box.right() - 8 - mainWidth : box.left() + 8;
        const QRectF branch(x, y, mainWidth, mainHeight);
        painter.setPen(QPen(node->branchColor, 1));
        painter.drawLine(QPointF(right ? central.right() : central.left(), center.y()),
                         QPointF(right ? branch.left() : branch.right(), branch.center().y()));
        painter.setPen(Qt::NoPen);
        painter.setBrush(node->fill);
        painter.drawRoundedRect(branch, 2, 2);
    }
    painter.setBrush(root->text);  // il centrale e' solo testo: una barretta del suo colore
    painter.drawRoundedRect(central, 2, 2);
    return pixmap;
}

class VariantTile final : public QToolButton {
public:
    explicit VariantTile(QWidget *parent) : QToolButton(parent) {
        setFixedSize(VariantPicker::kTileWidthPx, VariantPicker::kTileHeightPx);
        setFocusPolicy(Qt::TabFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }
    void setLook(const QPixmap &thumbnail, bool current) { m_thumbnail = thumbnail; m_current = current; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF inner = QRectF(rect()).adjusted(3, 3, -3, -3);
        painter.drawPixmap(inner.toRect(), m_thumbnail);
        painter.setBrush(Qt::NoBrush);
        if (m_current || underMouse() || hasFocus()) {
            // Come la palette attiva del popup: contorno chiaro di 2 px; hover e fuoco piu' tenui.
            painter.setPen(QPen(m_current ? kText : kBorder, 2));
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 6, 6);
        } else {
            painter.setPen(QPen(kBorder, 1));
            painter.drawRoundedRect(inner.adjusted(-0.5, -0.5, 0.5, 0.5), 4, 4);
        }
    }

private:
    QPixmap m_thumbnail;
    bool m_current = false;
};
}  // namespace

VariantPicker::VariantPicker(bool withWhite, QWidget *parent) : QWidget(parent), m_withWhite(withWhite) {
    setObjectName(QStringLiteral("VariantPicker"));
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(ColorPicker::kGapPx);
    const int count = NodeStyle::kVariantCount + (withWhite ? 1 : 0);
    for (int i = 0; i < count; ++i) {
        auto *tile = new VariantTile(this);
        const int variant = withWhite ? i - 1 : i;
        const QString name = variant < 0 ? tr("White background") : tr("Background %1").arg(variant + 1);
        tile->setToolTip(name);
        tile->setAccessibleName(name);
        connect(tile, &QToolButton::clicked, this, [this, variant] { emit variantChosen(variant); });
        m_tiles.append(tile);
        grid->addWidget(tile, i / 3, i % 3);
    }
}

void VariantPicker::setTheme(const QString &paletteId, const QStringList &palette, int variant) {
    if (paletteId == m_paletteId && palette == m_palette && variant == m_variant) return;
    m_paletteId = paletteId;
    m_palette = palette;
    m_variant = variant;
    rebuild();
}

QToolButton *VariantPicker::tile(int variant) const { return m_tiles.value(m_withWhite ? variant + 1 : variant); }

void VariantPicker::rebuild() {
    for (int i = 0; i < m_tiles.size(); ++i) {
        const int variant = m_withWhite ? i - 1 : i;
        auto *tile = static_cast<VariantTile *>(m_tiles[i]);
        const bool exists = variant < m_palette.size();
        tile->setEnabled(exists);
        tile->setLook(exists ? variantThumbnail(m_paletteId, m_palette, variant, QSize(kTileWidthPx - 6, kTileHeightPx - 6)) : QPixmap(),
                      variant == m_variant && (variant >= 0 || m_withWhite));
    }
}

// --- FEAT-043 parte stile: controlli del foglio ---

SheetStylePanel::SheetStylePanel(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("SheetStylePanel"));
    setStyleSheet(panelPageStyleSheet(objectName()) + UiTheme::qss(QStringLiteral(
        "QLabel#FieldCaption { color:@textMuted; font-size:12px; } QLabel#GroupHeading { font-weight:600; }")));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kRowGapPx);
    const auto caption = [this](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("FieldCaption"));
        return label;
    };
    const auto select = [this](const QString &name) {
        auto *field = new DarkSelect(this);
        field->setObjectName(name);
        field->setFocusPolicy(Qt::TabFocus);
        field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        field->view()->setItemDelegate(new LightComboItemDelegate(field->view()));
        setManualLook(field, true);
        return field;
    };
    m_font = select(QStringLiteral("SheetFontSelect"));
    m_font->setMaxVisibleItems(12);
    m_font->setAccessibleName(tr("Global font"));
    m_font->addItem(tr("Default"), QString());  // FEAT-072 B19: "Default" global font
    NodeItem::loadBundledFonts();
    for (const QString &family : QFontDatabase::families())
        if (!family.startsWith(QLatin1Char('@'))) m_font->addItem(family, family);  // "@": font verticali
    connect(m_font, &QComboBox::activated, this, [this](int index) {
        const QString family = m_font->itemData(index).toString();
        choose([family](NodeStyle::SheetStyle &style) { style.fontFamily = family; });
    });
    m_width = select(QStringLiteral("SheetWidthSelect"));
    m_width->setAccessibleName(tr("Branch line width"));
    // FEAT-023 (foto dell'utente 2026-10-21): spessori con i nomi, poi le versioni affusolate
    // (valore negativo nella voce = affusolato).
    m_width->addItem(tr("Default"), 0.0);
    for (int i = 0; i < NodeStyle::kNamedBranchWidths.size(); ++i) m_width->addItem(branchWidthName(i), NodeStyle::kNamedBranchWidths[i]);
    m_width->insertSeparator(m_width->count());
    for (int i = 0; i < NodeStyle::kNamedBranchWidths.size(); ++i)
        m_width->addItem(tr("%1 - Tapered").arg(branchWidthName(i)), -NodeStyle::kNamedBranchWidths[i]);
    connect(m_width, &QComboBox::activated, this, [this](int index) {
        const qreal value = m_width->itemData(index).toDouble();
        choose([value](NodeStyle::SheetStyle &style) {
            style.branchWidth = std::abs(value);
            style.tapered = value < 0;
        });
    });
    m_colored = new ToggleSwitch(this);
    m_colored->setObjectName(QStringLiteral("ColoredBranchSwitch"));
    m_colored->setAccessibleName(tr("Colored branch"));
    connect(m_colored, &ToggleSwitch::toggled, this, [this](bool on) {
        choose([on](NodeStyle::SheetStyle &style) { style.coloredBranches = on; });
    });
    m_color = new ColorField(this);
    m_color->setObjectName(QStringLiteral("BranchLineColorField"));
    m_color->setAccessibleName(tr("Color"));
    connect(m_color, &ColorField::colorChosen, this, [this](const QColor &color) {
        choose([color](NodeStyle::SheetStyle &style) { style.lineColor = color; });
    });
    const auto row = [this](const QString &label, QWidget *field) {
        auto *line = new QWidget(this);
        auto *lineLayout = new QHBoxLayout(line);
        lineLayout->setContentsMargins(0, 0, 0, 0);
        lineLayout->addWidget(rowLabel(label, {field}, line));
        lineLayout->addStretch();
        lineLayout->addWidget(field);
        return line;
    };
    layout->addWidget(caption(tr("Global font")));
    layout->addWidget(m_font);
    layout->addSpacing(4);
    layout->addWidget(caption(tr("Branch line width")));
    layout->addWidget(m_width);
    layout->addSpacing(4);
    layout->addWidget(row(tr("Colored branch"), m_colored));
    layout->addWidget(row(tr("Color"), m_color));
    // FEAT-072 B20: headed groups separated by lines, as in the Map page.
    const auto group = [this, layout](const QString &title) {
        layout->addSpacing(kSectionGapPx - layout->spacing());
        layout->addWidget(panelDivider(this));
        layout->addSpacing(kSectionGapPx - layout->spacing());
        auto *heading = new QLabel(title, this);
        heading->setObjectName(QStringLiteral("GroupHeading"));
        layout->addWidget(heading);
        auto *rows = new QVBoxLayout;
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(layout->spacing());
        layout->addLayout(rows);
        return rows;
    };
    m_mapStyleLayout = group(tr("Map style"));
    QVBoxLayout *topicDisplay = group(tr("Topic display"));
    // FEAT-024: the central topic's numbering, i.e. the whole map's (the same as on its Style page).
    m_numbering = new NumberingFields(this);
    group(tr("Numbering"))->addWidget(m_numbering);
    QVBoxLayout *advanced = group(tr("Advanced"));
    // FEAT-059: note sotto i nodi.
    m_showNotes = new ToggleSwitch(this);
    m_showNotes->setObjectName(QStringLiteral("ShowNotesSwitch"));
    m_showNotes->setAccessibleName(tr("Show all notes"));
    connect(m_showNotes, &ToggleSwitch::toggled, this, [this](bool on) {
        choose([on](NodeStyle::SheetStyle &style) { style.showNotes = on; });
    });
    // FEAT-043 Justify (user's decision 2026-10-24), above the notes.
    m_justify = new ToggleSwitch(this);
    m_justify->setObjectName(QStringLiteral("JustifySwitch"));
    m_justify->setAccessibleName(tr("Justify topic alignment"));
    m_justify->setToolTip(tr("Topics of the same level get the same width"));
    connect(m_justify, &ToggleSwitch::toggled, this, [this](bool on) {
        choose([on](NodeStyle::SheetStyle &style) { style.justify = on; });
    });
    topicDisplay->addWidget(row(tr("Justify topic alignment"), m_justify));
    topicDisplay->addWidget(row(tr("Show all notes"), m_showNotes));
    // FEAT-043 parte layout (richiesta dell'utente 2026-10-21).
    m_compact = new ToggleSwitch(this);
    m_compact->setObjectName(QStringLiteral("CompactMapSwitch"));
    m_compact->setAccessibleName(tr("Compact map"));
    connect(m_compact, &ToggleSwitch::toggled, this, [this](bool on) {
        choose([on](NodeStyle::SheetStyle &style) { style.compact = on; });
    });
    m_freeBranches = new ToggleSwitch(this);
    m_freeBranches->setObjectName(QStringLiteral("FreeBranchesSwitch"));
    m_freeBranches->setAccessibleName(tr("Free branch positions"));
    m_freeBranches->setToolTip(tr("Branches stay where you move them: no automatic layout or balancing"));
    connect(m_freeBranches, &ToggleSwitch::toggled, this, [this](bool on) {
        choose([on](NodeStyle::SheetStyle &style) { style.freeBranches = on; });
    });
    m_mapStyleLayout->addWidget(row(tr("Compact map"), m_compact));
    advanced->addWidget(row(tr("Free branch positions"), m_freeBranches));
    // FEAT-025: last on the page, full width, like Reset style at the bottom of the Style page.
    layout->addSpacing(kSectionGapPx);
    layout->addWidget(panelDivider(this));
    layout->addSpacing(kSectionGapPx);
    m_resetMap = new QToolButton(this);
    m_resetMap->setObjectName(QStringLiteral("FitButton"));
    m_resetMap->setText(tr("Reset map style"));
    m_resetMap->setToolTip(tr("Remove the styles chosen on this sheet's topics, relationships and boundaries, and its background and line settings"));
    m_resetMap->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_resetMap->setFixedHeight(28);
    m_resetMap->setCursor(Qt::PointingHandCursor);
    connect(m_resetMap, &QToolButton::clicked, this, &SheetStylePanel::resetMapRequested);
    layout->addWidget(m_resetMap);
    setSheetStyle(NodeStyle::SheetStyle());
}

void SheetStylePanel::setResetMapEnabled(bool enabled) { m_resetMap->setEnabled(enabled); }

void SheetStylePanel::addMapStyleRow(QWidget *row) {
    row->setParent(this);
    m_mapStyleLayout->insertWidget(0, row);
}

void SheetStylePanel::setSheetStyle(const NodeStyle::SheetStyle &style, const QColor &automaticLineColor) {
    m_style = style;
    {
        const QSignalBlocker blocker(m_font);
        int index = m_font->findData(style.fontFamily);
        if (index < 0) {  // font del file non installato su questo computer
            m_font->addItem(tr("%1 (not installed)").arg(style.fontFamily), style.fontFamily);
            index = m_font->count() - 1;
        }
        m_font->setCurrentIndex(index);
    }
    {
        const QSignalBlocker blocker(m_width);
        // Un valore in px di prima, fuori dall'elenco, si aggiunge in fondo come numero.
        const qreal value = style.tapered ? -style.branchWidth : style.branchWidth;
        int index = m_width->findData(value);
        if (index < 0 && style.branchWidth > 0) {
            m_width->addItem(style.tapered ? tr("%1 px - Tapered").arg(style.branchWidth) : tr("%1 px").arg(style.branchWidth), value);
            index = m_width->count() - 1;
        }
        m_width->setCurrentIndex(std::max(0, index));
    }
    {
        const QSignalBlocker blocker(m_colored);
        m_colored->setChecked(style.coloredBranches);
    }
    {
        const QSignalBlocker blocker(m_showNotes);
        m_showNotes->setChecked(style.showNotes);
    }
    for (const auto &[toggle, on] : {std::pair{m_compact, style.compact}, std::pair{m_freeBranches, style.freeBranches},
                                     std::pair{m_justify, style.justify}}) {
        const QSignalBlocker blocker(toggle);
        toggle->setChecked(on);
    }
    NodeStyle::Reading reading;
    reading.empty = false;
    reading.value = style.lineColor.isValid() ? style.lineColor : automaticLineColor;
    reading.allManual = style.lineColor.isValid();
    m_color->setReading(reading);
    m_color->setEnabled(!style.coloredBranches);  // il Colore vale solo con Ramo colorato spento
}

void SheetStylePanel::choose(const std::function<void(NodeStyle::SheetStyle &)> &change) {
    NodeStyle::SheetStyle style = m_style;
    change(style);
    if (style == m_style) return;
    emit sheetStyleChosen(style);
}

// --- FEAT-053: relationship style ---

RelationshipStylePage::RelationshipStylePage(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("RelationshipStylePage"));
    setStyleSheet(panelPageStyleSheet(objectName()) + UiTheme::qss(QStringLiteral("QLabel#GroupHeading { font-weight:600; }")));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kRowGapPx);
    const auto heading = [this](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("GroupHeading"));
        return label;
    };
    const auto row = [this](const QString &label, const QList<QWidget *> &fields) {
        auto *line = new QWidget(this);
        auto *lineLayout = new QHBoxLayout(line);
        lineLayout->setContentsMargins(0, 0, 0, 0);
        lineLayout->setSpacing(8);
        lineLayout->addWidget(rowLabel(label, fields, line));
        lineLayout->addStretch();
        for (QWidget *field : fields) lineLayout->addWidget(field);
        return line;
    };
    const auto darkSelect = [this](const QString &toolTip) {
        QComboBox *combo = makeDarkSelect(this);
        combo->setFixedWidth(kWideFieldWidthPx);
        combo->setToolTip(toolTip);
        combo->setAccessibleName(toolTip);
        return combo;
    };
    // Each select applies its item's data to the relationships through `apply`.
    const auto onChosen = [this](QComboBox *combo, std::function<void(Relationship &, const QVariant &)> apply) {
        connect(combo, &QComboBox::activated, this, [this, combo, apply](int index) {
            const QVariant value = combo->itemData(index);
            emit edited([apply, value](Relationship &relationship) { apply(relationship, value); });
        });
    };

    layout->addWidget(heading(tr("Shape")));
    m_shape = darkSelect(tr("Relationship shape"));
    m_shape->addItem(tr("Curved"), static_cast<int>(Relationship::Shape::Curved));
    m_shape->addItem(tr("Straight"), static_cast<int>(Relationship::Shape::Straight));
    m_shape->addItem(tr("Elbow"), static_cast<int>(Relationship::Shape::Elbow));
    onChosen(m_shape, [](Relationship &relationship, const QVariant &value) {
        relationship.shape = static_cast<Relationship::Shape>(value.toInt());
    });
    layout->addWidget(row(tr("Shape"), {m_shape}));

    layout->addWidget(heading(tr("Line")));
    m_pattern = darkSelect(tr("Line style"));
    m_pattern->setFixedWidth(64);
    addLinePatternItems(m_pattern, false);
    onChosen(m_pattern, [](Relationship &relationship, const QVariant &value) { relationship.linePattern = value.toInt(); });
    layout->addWidget(row(tr("Style"), {m_pattern}));
    m_width = darkSelect(tr("Line width"));
    for (int i = 0; i < NodeStyle::kNamedBranchWidths.size(); ++i)
        m_width->addItem(branchWidthName(i), NodeStyle::kNamedBranchWidths[i]);
    onChosen(m_width, [](Relationship &relationship, const QVariant &value) { relationship.lineWidth = value.toDouble(); });
    layout->addWidget(row(tr("Width"), {m_width}));
    m_lineColor = new ColorField(this);
    m_lineColor->setToolTip(tr("Line color"));
    m_lineColor->themeColors = [this] { return themeColors ? themeColors() : QStringList(); };
    connect(m_lineColor, &ColorField::colorChosen, this, [this](const QColor &color) {
        emit edited([color](Relationship &relationship) { relationship.lineColor = color; });
    });
    layout->addWidget(row(tr("Color"), {m_lineColor}));
    // Start and end marks, the same selects as the branches' (user 2026-10-24).
    for (auto [select, atStart] : {std::pair{&m_startMark, true}, std::pair{&m_endMark, false}}) {
        *select = darkSelect(QString());
        (*select)->setFixedWidth(kFieldWidthPx);
        (*select)->setToolTip(atStart ? tr("Line start") : tr("Line end"));
        (*select)->setAccessibleName((*select)->toolTip());
        addLineEndItems(*select, atStart);
        onChosen(*select, [atStart](Relationship &relationship, const QVariant &value) {
            (atStart ? relationship.startMark : relationship.endMark) = static_cast<BranchEnd>(value.toInt());
        });
    }
    layout->addWidget(row(tr("Start / End"), {m_startMark, m_endMark}));

    layout->addWidget(heading(tr("Text")));
    m_fontFamily = darkSelect(tr("Font"));
    m_fontFamily->setMaxVisibleItems(12);
    m_fontFamily->addItem(NodeStyle::kDefaultMapFontFamily, QString());  // the sheet's font
    NodeItem::loadBundledFonts();
    for (const QString &family : QFontDatabase::families())
        if (!family.startsWith(QLatin1Char('@')) && family != NodeStyle::kDefaultMapFontFamily)
            m_fontFamily->addItem(family, family);
    onChosen(m_fontFamily, [](Relationship &relationship, const QVariant &value) { relationship.fontFamily = value.toString(); });
    layout->addWidget(row(tr("Font"), {m_fontFamily}));
    m_fontSize = darkSelect(tr("Text size"));
    m_fontSize->setFixedWidth(kFieldWidthPx);
    // The 13 px default is not among the usual sizes: it is added first.
    m_fontSize->addItem(QString::number(int(Relationships::kDefaultFontSize)), Relationships::kDefaultFontSize);
    for (const int px : NodeStyle::kFontSizesPx) m_fontSize->addItem(QString::number(px), qreal(px));
    onChosen(m_fontSize, [](Relationship &relationship, const QVariant &value) { relationship.fontSize = value.toDouble(); });
    m_textColor = new ColorField(this);
    m_textColor->setToolTip(tr("Text color"));
    m_textColor->themeColors = [this] { return themeColors ? themeColors() : QStringList(); };
    connect(m_textColor, &ColorField::colorChosen, this, [this](const QColor &color) {
        emit edited([color](Relationship &relationship) { relationship.textColor = color; });
    });
    layout->addWidget(row(tr("Size"), {m_fontSize, m_textColor}));
}

void RelationshipStylePage::setRelationships(const QList<Relationship> &relationships, const QColor &defaultLineColor,
                                             const QColor &defaultTextColor) {
    setEnabled(!relationships.isEmpty());
    // Common value of a property, or nothing (index -1) when the relationships differ.
    const auto show = [&relationships](QComboBox *combo, const std::function<QVariant(const Relationship &)> &valueOf) {
        int index = -1;
        if (!relationships.isEmpty()) {
            const QVariant first = valueOf(relationships.front());
            const bool same = std::all_of(relationships.cbegin(), relationships.cend(), [&](const Relationship &r) { return valueOf(r) == first; });
            if (same) index = combo->findData(first);
        }
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(index);
    };
    show(m_shape, [](const Relationship &r) { return QVariant(static_cast<int>(r.shape)); });
    show(m_pattern, [](const Relationship &r) { return QVariant(r.linePattern.value_or(Relationships::kDefaultLinePattern)); });
    show(m_width, [](const Relationship &r) { return QVariant(r.lineWidth.value_or(Relationships::kDefaultLineWidth)); });
    show(m_startMark, [](const Relationship &r) { return QVariant(static_cast<int>(r.startMark.value_or(Relationships::kDefaultStartMark))); });
    show(m_endMark, [](const Relationship &r) { return QVariant(static_cast<int>(r.endMark.value_or(Relationships::kDefaultEndMark))); });
    show(m_fontFamily, [](const Relationship &r) { return QVariant(r.fontFamily); });
    show(m_fontSize, [](const Relationship &r) { return QVariant(r.fontSize.value_or(Relationships::kDefaultFontSize)); });
    const auto colorReading = [&relationships](const std::function<std::optional<QColor>(const Relationship &)> &own, const QColor &fallback) {
        NodeStyle::Reading reading;
        reading.empty = relationships.isEmpty();
        if (reading.empty) return reading;
        const QColor first = own(relationships.front()).value_or(fallback);
        reading.mixed = std::any_of(relationships.cbegin(), relationships.cend(), [&](const Relationship &r) { return own(r).value_or(fallback) != first; });
        reading.value = first;
        reading.allManual = std::all_of(relationships.cbegin(), relationships.cend(), [&](const Relationship &r) { return own(r).has_value(); });
        return reading;
    };
    m_lineColor->setReading(colorReading([](const Relationship &r) { return r.lineColor; }, defaultLineColor));
    m_textColor->setReading(colorReading([](const Relationship &r) { return r.textColor; }, defaultTextColor));
}

// --- FEAT-052: boundary style ---

BoundaryStylePage::BoundaryStylePage(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("BoundaryStylePage"));
    setStyleSheet(panelPageStyleSheet(objectName()) + UiTheme::qss(QStringLiteral("QLabel#GroupHeading { font-weight:600; }")));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kRowGapPx);
    const auto heading = [this](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("GroupHeading"));
        return label;
    };
    const auto row = [this](const QString &label, const QList<QWidget *> &fields) {
        auto *line = new QWidget(this);
        auto *lineLayout = new QHBoxLayout(line);
        lineLayout->setContentsMargins(0, 0, 0, 0);
        lineLayout->setSpacing(8);
        lineLayout->addWidget(rowLabel(label, fields, line));
        lineLayout->addStretch();
        for (QWidget *field : fields) lineLayout->addWidget(field);
        return line;
    };
    const auto darkSelect = [this](const QString &toolTip) {
        QComboBox *combo = makeDarkSelect(this);
        combo->setFixedWidth(kWideFieldWidthPx);
        combo->setToolTip(toolTip);
        combo->setAccessibleName(toolTip);
        return combo;
    };
    // Each select applies its item's data to the boundaries through `apply`.
    const auto onChosen = [this](QComboBox *combo, std::function<void(Boundary &, const QVariant &)> apply) {
        connect(combo, &QComboBox::activated, this, [this, combo, apply](int index) {
            const QVariant value = combo->itemData(index);
            emit edited([apply, value](Boundary &boundary) { apply(boundary, value); });
        });
    };
    const auto colorField = [this](const QString &toolTip, std::function<void(Boundary &, const QColor &)> apply) {
        auto *field = new ColorField(this);
        field->setToolTip(toolTip);
        field->themeColors = [this] { return themeColors ? themeColors() : QStringList(); };
        connect(field, &ColorField::colorChosen, this, [this, apply](const QColor &color) {
            emit edited([apply, color](Boundary &boundary) { apply(boundary, color); });
        });
        return field;
    };

    layout->addWidget(heading(tr("Shape")));
    m_shape = darkSelect(tr("Boundary shape"));
    m_shape->addItem(tr("Rounded rectangle"), static_cast<int>(Boundary::Shape::RoundedRectangle));
    m_shape->addItem(tr("Rectangle"), static_cast<int>(Boundary::Shape::Rectangle));
    onChosen(m_shape, [](Boundary &boundary, const QVariant &value) { boundary.shape = static_cast<Boundary::Shape>(value.toInt()); });
    layout->addWidget(row(tr("Shape"), {m_shape}));
    m_fillColor = colorField(tr("Fill color"), [](Boundary &boundary, const QColor &color) { boundary.fillColor = color; });
    layout->addWidget(row(tr("Fill"), {m_fillColor}));

    layout->addWidget(heading(tr("Border")));
    m_pattern = darkSelect(tr("Line style"));
    m_pattern->setFixedWidth(64);
    addLinePatternItems(m_pattern, false);
    onChosen(m_pattern, [](Boundary &boundary, const QVariant &value) { boundary.linePattern = value.toInt(); });
    layout->addWidget(row(tr("Style"), {m_pattern}));
    m_width = darkSelect(tr("Line width"));
    for (int i = 0; i < NodeStyle::kNamedBranchWidths.size(); ++i)
        m_width->addItem(branchWidthName(i), NodeStyle::kNamedBranchWidths[i]);
    onChosen(m_width, [](Boundary &boundary, const QVariant &value) { boundary.lineWidth = value.toDouble(); });
    layout->addWidget(row(tr("Width"), {m_width}));
    m_lineColor = colorField(tr("Line color"), [](Boundary &boundary, const QColor &color) { boundary.lineColor = color; });
    layout->addWidget(row(tr("Color"), {m_lineColor}));
}

void BoundaryStylePage::setBoundaries(const QList<Boundary> &boundaries, const QColor &defaultLineColor, const QColor &defaultFillColor) {
    setEnabled(!boundaries.isEmpty());
    // Common value of a property, or nothing (index -1) when the boundaries differ.
    const auto show = [&boundaries](QComboBox *combo, const std::function<QVariant(const Boundary &)> &valueOf) {
        int index = -1;
        if (!boundaries.isEmpty()) {
            const QVariant first = valueOf(boundaries.front());
            const bool same = std::all_of(boundaries.cbegin(), boundaries.cend(), [&](const Boundary &b) { return valueOf(b) == first; });
            if (same) index = combo->findData(first);
        }
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(index);
    };
    show(m_shape, [](const Boundary &b) { return QVariant(static_cast<int>(b.shape)); });
    show(m_pattern, [](const Boundary &b) { return QVariant(b.linePattern.value_or(Boundaries::kDefaultLinePattern)); });
    show(m_width, [](const Boundary &b) { return QVariant(b.lineWidth.value_or(Boundaries::kDefaultLineWidth)); });
    const auto colorReading = [&boundaries](const std::function<std::optional<QColor>(const Boundary &)> &own, const QColor &fallback) {
        NodeStyle::Reading reading;
        reading.empty = boundaries.isEmpty();
        if (reading.empty) return reading;
        const QColor first = own(boundaries.front()).value_or(fallback);
        reading.mixed = std::any_of(boundaries.cbegin(), boundaries.cend(), [&](const Boundary &b) { return own(b).value_or(fallback) != first; });
        reading.value = first;
        reading.allManual = std::all_of(boundaries.cbegin(), boundaries.cend(), [&](const Boundary &b) { return own(b).has_value(); });
        return reading;
    };
    m_lineColor->setReading(colorReading([](const Boundary &b) { return b.lineColor; }, defaultLineColor));
    m_fillColor->setReading(colorReading([](const Boundary &b) { return b.fillColor; }, defaultFillColor));
}
