#include "exportframecontrols.h"

#include "nodestyle.h"
#include "numberfield.h"
#include "segmentedcontrol.h"
#include "stylecontrols.h"
#include "toggleswitch.h"
#include "uitheme.h"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>

#include <algorithm>

namespace {

// The style as pictures, as the nodes' Border select: the grid menu of the Style page (user,
// 2026-10-31: simpler to choose); the name in the tooltip.
class FrameStyleSelect final : public QComboBox {
public:
    explicit FrameStyleSelect(QWidget *parent) : QComboBox(parent) {
        QComboBox *dark = makeDarkSelect();  // the dark selects' look
        setStyleSheet(dark->styleSheet());
        delete dark;
    }
    void showPopup() override {
        using UiTheme::Token;
        openIconGridMenu(this, 2, {UiTheme::color(Token::Surface), UiTheme::color(Token::Divider), UiTheme::color(Token::Text),
                                   UiTheme::color(Token::Divider)});
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        QComboBox::paintEvent(event);
        paintDarkSelectChevron(this);
    }
};

// A straight piece of the style across box, its motif as tall as most of the box: legible small.
void paintStyle(QPainter &painter, const QRectF &box, ExportFrame::Style style) {
    ExportFrame frame;
    frame.style = style;
    frame.color = UiTheme::color(UiTheme::Token::Text);
    const double height = ExportFrame::isOrnament(style) ? box.height() * 0.9 : box.height() * 0.6;
    frame.paintStrip(&painter, QRectF(box.left(), box.center().y() - height / 2, box.width(), height));
}

}  // namespace

ExportFrameControls::ExportFrameControls(QGridLayout *grid, QWidget *parent) : QObject(parent) {
    const auto addRow = [&](const QString &text, QWidget *control, bool detail) {
        const int row = grid->rowCount();
        auto *label = new QLabel(text, parent);
        grid->addWidget(label, row, 0);
        grid->addWidget(control, row, 1, Qt::AlignRight | Qt::AlignVCenter);
        control->setAccessibleName(text);
        if (detail) m_details.append({label, control});
    };
    m_on = new ToggleSwitch(parent);
    m_on->setObjectName(QStringLiteral("ExportFrame"));
    addRow(tr("Frame"), m_on, false);
    m_style = new FrameStyleSelect(parent);
    m_style->setObjectName(QStringLiteral("ExportFrameStyle"));
    m_style->setMinimumWidth(96);
    for (int s = int(ExportFrame::Style::Solid); s < ExportFrame::kStyles; ++s) {
        const auto style = ExportFrame::Style(s);
        addIconSelectItem(m_style, s, ExportFrame::name(style), [style](QPainter &painter, const QRectF &box) { paintStyle(painter, box, style); });
    }
    addRow(tr("Style"), m_style, true);
    m_thickness = new NumberField(parent);
    m_thickness->setObjectName(QStringLiteral("ExportFrameThickness"));
    m_thickness->setDecimals(0);
    m_thickness->setRange(ExportFrame::kMinThickness, ExportFrame::kMaxThickness);
    m_thickness->setValue(ExportFrame::kDefaultThickness);
    m_thickness->setFixedWidth(96);
    addRow(tr("Thickness (px)"), m_thickness, true);
    m_color = new ColorField(parent);
    m_color->setObjectName(QStringLiteral("ExportFrameColor"));
    m_color->allowOpacity = false;
    m_color->setFixedWidth(72);
    NodeStyle::Reading reading;
    reading.empty = false;
    reading.value = m_chosen;
    m_color->setReading(reading);
    addRow(tr("Colour"), m_color, true);
    m_corners = new SegmentedControl({tr("Square"), tr("Rounded")}, parent);
    m_corners->setObjectName(QStringLiteral("ExportFrameCorners"));
    m_corners->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    addRow(tr("Corners"), m_corners, true);

    connect(m_on, &ToggleSwitch::toggled, this, [this] {
        sync();
        emit changed();
    });
    // The grid menu only emits activated (the Style page applies it itself): the choice made current
    // here (user, 2026-10-31: the frame's style did not change).
    connect(m_style, &QComboBox::activated, m_style, &QComboBox::setCurrentIndex);
    connect(m_style, &QComboBox::currentIndexChanged, this, [this] {
        sync();
        emit changed();
    });
    connect(m_thickness, &NumberField::valueChosen, this, &ExportFrameControls::changed);
    connect(m_color, &ColorField::colorChosen, this, [this](const QColor &color) {
        m_chosen = color;
        NodeStyle::Reading reading;
        reading.empty = false;
        reading.value = color;
        m_color->setReading(reading);
        emit changed();
    });
    connect(m_corners, &SegmentedControl::segmentActivated, this, &ExportFrameControls::changed);
    sync();
}

void ExportFrameControls::sync() {
    const bool on = m_on->isChecked();
    for (const auto &[label, control] : std::as_const(m_details)) {
        label->setVisible(on);
        control->setVisible(on);
    }
    // An ornament needs room for its motif: at least kMinOrnament px.
    const bool ornament = ExportFrame::isOrnament(ExportFrame::Style(m_style->currentData().toInt()));
    m_thickness->setRange(ornament ? ExportFrame::kMinOrnament : ExportFrame::kMinThickness, ExportFrame::kMaxThickness);
    if (m_thickness->value() < m_thickness->minimum()) m_thickness->setValue(m_thickness->minimum());
}

void ExportFrameControls::setFrame(const ExportFrame &frame) {
    m_on->setChecked(frame.enabled());
    if (frame.enabled())
        if (const int index = m_style->findData(int(frame.style)); index >= 0) m_style->setCurrentIndex(index);
    m_thickness->setValue(frame.thickness);
    m_chosen = frame.color;
    NodeStyle::Reading reading;
    reading.empty = false;
    reading.value = frame.color;
    m_color->setReading(reading);
    m_corners->setCurrentIndex(frame.rounded ? 1 : 0);
    sync();
    emit changed();
}

ExportFrame ExportFrameControls::frame() const {
    ExportFrame frame;
    if (!m_on->isChecked()) return frame;
    frame.style = ExportFrame::Style(m_style->currentData().toInt());
    frame.thickness = m_thickness->value();
    frame.color = m_chosen;
    frame.rounded = m_corners->currentIndex() == 1;
    return frame;
}
