#include "labeleditor.h"

#include "mindmapmodel.h"
#include "toggleswitch.h"
#include "uitheme.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kRadiusPx = 8;
constexpr int kPillHeightPx = 24;
constexpr int kPillPaddingPx = 10;
constexpr int kPillGapPx = 6;
constexpr int kMaxSuggestionRows = 6;
}  // namespace

// Pillole dei suggerimenti, a capo sulla larghezza del riquadro; clic = aggiungi.
class LabelEditor::Suggestions final : public QWidget {
public:
    explicit Suggestions(LabelEditor *editor) : QWidget(editor), m_editor(editor) {
        setMouseTracking(true);
        QFont small = font();
        small.setPixelSize(UiTheme::fontPx(13));
        setFont(small);
    }
    void setLabels(const QStringList &labels) {
        m_labels = labels;
        updateGeometry();
        update();
    }
    const QStringList &labels() const { return m_labels; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        const QList<QRect> rects = layoutFor(width);
        return rects.isEmpty() ? 0 : rects.back().bottom() + 1;
    }
    QSize sizeHint() const override { return {kWidthPx - 32, heightForWidth(kWidthPx - 32)}; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QList<QRect> rects = layoutFor(width());
        for (int i = 0; i < rects.size(); ++i) {
            const QRectF pill = QRectF(rects[i]).adjusted(0.5, 0.5, -0.5, -0.5);
            painter.setPen(QPen(UiTheme::color(UiTheme::Token::Divider), 1));
            painter.setBrush(i == m_hover ? UiTheme::color(UiTheme::Token::SurfaceRaised) : UiTheme::color(UiTheme::Token::SurfaceSunken));
            painter.drawRoundedRect(pill, kPillHeightPx / 2.0, kPillHeightPx / 2.0);
            painter.setPen(UiTheme::color(UiTheme::Token::Text));
            const QRect text = rects[i].adjusted(kPillPaddingPx, 0, -kPillPaddingPx, 0);
            painter.drawText(text, Qt::AlignCenter, fontMetrics().elidedText(m_labels[i], Qt::ElideRight, text.width()));
        }
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        const int hover = indexAt(event->position().toPoint());
        setCursor(hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (hover != m_hover) { m_hover = hover; update(); }
    }
    void leaveEvent(QEvent *) override { m_hover = -1; update(); }
    void mouseReleaseEvent(QMouseEvent *event) override {
        const int index = indexAt(event->position().toPoint());
        if (index >= 0 && event->button() == Qt::LeftButton) m_editor->addSuggestion(m_labels[index]);
    }

private:
    int indexAt(const QPoint &point) const {
        const QList<QRect> rects = layoutFor(width());
        for (int i = 0; i < rects.size(); ++i)
            if (rects[i].contains(point)) return i;
        return -1;
    }
    QList<QRect> layoutFor(int width) const {
        QList<QRect> rects;
        int x = 0, y = 0, rows = 1;
        for (const QString &label : m_labels) {
            const int pill = std::min(width, fontMetrics().horizontalAdvance(label) + 2 * kPillPaddingPx);
            if (x > 0 && x + pill > width) {
                if (++rows > kMaxSuggestionRows) break;  // le altre si trovano scrivendo
                x = 0;
                y += kPillHeightPx + kPillGapPx;
            }
            rects.append(QRect(x, y, pill, kPillHeightPx));
            x += pill + kPillGapPx;
        }
        return rects;
    }
    LabelEditor *m_editor;
    QStringList m_labels;
    int m_hover = -1;
};

LabelEditor::LabelEditor(QWidget *parent) : QFrame(parent) {
    setObjectName(QStringLiteral("LabelEditor"));
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(kWidthPx);
    setStyleSheet(UiTheme::qss(QStringLiteral(
        "QFrame#LabelEditor { background:transparent; }"
        "QLabel { color:@text; font-size:13px; }"
        "QLabel#LabelEditorTitle { font-size:14px; font-weight:600; }"
        "QLabel#LabelEditorHint { color:@textMuted; font-size:12px; }"
        "QLineEdit#LabelField { background:@surfaceSunken; color:@text; border:1px solid @surfaceSunken; border-radius:4px;"
        " min-height:30px; padding:0 8px; font-size:14px; selection-background-color:@accent; }"
        "QLineEdit#LabelField:focus { border-color:@accent; }")));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 20);  // dentro la superficie disegnata (4 px per l'ombra)
    layout->setSpacing(8);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(tr("Labels"), this);
    title->setObjectName(QStringLiteral("LabelEditorTitle"));
    header->addWidget(title);
    header->addStretch();
    auto *sortText = new QLabel(tr("Auto sort"), this);
    m_autoSort = new ToggleSwitch(this);
    m_autoSort->setAccessibleName(sortText->text());
    m_autoSort->setToolTip(tr("Keep the labels of a topic in alphabetical order"));
    header->addWidget(sortText);
    header->addWidget(m_autoSort);
    layout->addLayout(header);
    m_field = new QLineEdit(this);
    m_field->setObjectName(QStringLiteral("LabelField"));
    m_field->setPlaceholderText(tr("Labels, separated by commas"));
    m_field->installEventFilter(this);
    connect(m_field, &QLineEdit::textChanged, this, &LabelEditor::refreshSuggestions);
    layout->addWidget(m_field);
    m_suggestionsTitle = new QLabel(tr("Labels in this document"), this);
    m_suggestionsTitle->setObjectName(QStringLiteral("LabelEditorHint"));
    layout->addSpacing(4);
    layout->addWidget(m_suggestionsTitle);
    m_suggestions = new Suggestions(this);
    layout->addWidget(m_suggestions);
    connect(m_autoSort, &ToggleSwitch::toggled, this, [](bool on) { Labels::setAutoSort(on); });
    hide();
}

LabelEditor::~LabelEditor() { qApp->removeEventFilter(this); }

QStringList LabelEditor::typedLabels() const {
    return Labels::normalized(m_field->text().split(QLatin1Char(',')), false);
}

void LabelEditor::addSuggestion(const QString &label) {
    QStringList labels = typedLabels();
    if (std::none_of(labels.cbegin(), labels.cend(), [&label](const QString &own) { return Labels::same(own, label); })) labels.append(label);
    m_field->setText(labels.join(QStringLiteral(", ")) + QStringLiteral(", "));
    m_field->setFocus();
    m_field->end(false);
}

QStringList LabelEditor::visibleSuggestions() const { return m_suggestions->labels(); }

// Suggerimenti: le etichette del documento non ancora nel campo; con una parola a meta' dopo
// l'ultima virgola restano quelle che la contengono.
void LabelEditor::refreshSuggestions() {
    const QStringList parts = m_field->text().split(QLatin1Char(','));
    const QString typing = parts.back().trimmed();
    const QStringList chosen = Labels::normalized(parts.mid(0, parts.size() - 1), false);
    QStringList shown;
    for (const QString &label : m_allSuggestions) {
        if (std::any_of(chosen.cbegin(), chosen.cend(), [&label](const QString &own) { return Labels::same(own, label); })) continue;
        if (!typing.isEmpty() && !label.contains(typing, Qt::CaseInsensitive)) continue;
        if (Labels::same(label, typing)) continue;
        shown.append(label);
    }
    m_suggestions->setLabels(shown);
    m_suggestionsTitle->setVisible(!shown.isEmpty());
    m_suggestions->setVisible(!shown.isEmpty());
    adjustSize();
    if (isVisible() && !m_anchor.isNull()) moveNextTo(m_anchor);
}

void LabelEditor::openFor(const QString &nodeId, const QStringList &labels, const QStringList &suggestions, const QRect &anchor) {
    if (isVisible()) commitAndClose();
    m_nodeId = nodeId;
    m_allSuggestions = suggestions;
    {
        const QSignalBlocker blocker(m_autoSort);
        m_autoSort->setChecked(Labels::autoSort());
    }
    m_field->setText(labels.isEmpty() ? QString() : labels.join(QStringLiteral(", ")) + QStringLiteral(", "));
    refreshSuggestions();
    adjustSize();
    moveNextTo(anchor);
    show();
    raise();
    m_field->setFocus();
    m_field->end(false);
    qApp->installEventFilter(this);  // clic fuori = chiusura
}

void LabelEditor::moveNextTo(const QRect &anchor) {
    m_anchor = anchor;
    QWidget *window = parentWidget();
    if (!window) return;
    constexpr int kGapPx = 8;
    // Sotto le pillole (o il nodo), allineato al loro bordo sinistro; sopra se sotto non c'e' spazio
    // (richiesta 2026-10-19: piu' ordinato che accanto). Il bordo dell'ombra (4 px) non conta.
    constexpr int kShadowPx = 4;
    const QRect local(window->mapFromGlobal(anchor.topLeft()), anchor.size());
    const int x = std::clamp(local.left() - kShadowPx, kGapPx, std::max(kGapPx, window->width() - width() - kGapPx));
    int y = local.bottom() + kGapPx - kShadowPx;
    if (y + height() > window->height() - kGapPx) y = local.top() - kGapPx + kShadowPx - height();
    move(x, std::clamp(y, kGapPx, std::max(kGapPx, window->height() - height() - kGapPx)));
}

void LabelEditor::commitAndClose() {
    if (!isVisible()) return;
    qApp->removeEventFilter(this);
    hide();
    const QStringList labels = typedLabels();
    const QString id = m_nodeId;
    m_nodeId.clear();
    emit finished(id, labels);
}

bool LabelEditor::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_field && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) { commitAndClose(); return true; }
    }
    if (event->type() == QEvent::MouseButtonPress && isVisible()) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        auto *widget = qobject_cast<QWidget *>(watched);
        const QPoint inside = mapFromGlobal(mouse->globalPosition().toPoint());
        if (widget && widget->window() == window() && !rect().contains(inside)) commitAndClose();
    }
    return QFrame::eventFilter(watched, event);
}

void LabelEditor::paintEvent(QPaintEvent *) {
    // Come il riquadro della nota: ombra leggera, poi la superficie scura.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
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
