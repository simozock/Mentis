#include "stickereditor.h"
#include "help.h"

#include "nodeitem.h"
#include "segmentedcontrol.h"
#include "numberfield.h"
#include "stylecontrols.h"
#include "uitheme.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <memory>
#include <tuple>

namespace {
constexpr int kSwatchPx = 32;
constexpr int kSwatchColumns = 8;
constexpr int kNumberFieldPx = 96;  // as the Export dialog's
constexpr int kTextFieldPx = 200;

QString swatchStyle(const QColor &color) {
    return UiTheme::qss(QStringLiteral("QToolButton { background:%1; border:1px solid @divider; border-radius:8px; }"
                                       "QToolButton:hover, QToolButton:focus { border:2px solid @accent; }")
                            .arg(color.name()));
}
}  // namespace

// --- Preview -------------------------------------------------------------------------------------

StickerPreview::StickerPreview(StickerCanvas *canvas, QWidget *parent) : QWidget(parent), m_canvas(canvas) {
    setFixedSize(kSizePx, kSizePx);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);  // arrows, Delete and Esc for the selection and the points
    m_canvas->setDevicePixelRatio(devicePixelRatioF());
    m_canvas->setViewSize(size());
    m_canvas->updateRequested = [this] { update(); };
}

void StickerPreview::paintEvent(QPaintEvent *) {
    m_canvas->setDevicePixelRatio(devicePixelRatioF());  // the screen may have changed
    QPainter painter(this);
    m_canvas->paint(&painter, {UiTheme::color(UiTheme::Token::SurfaceRaised), UiTheme::color(UiTheme::Token::SurfaceSunken),
                               UiTheme::color(UiTheme::Token::Surface), UiTheme::color(UiTheme::Token::Accent)});
}

void StickerPreview::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) m_canvas->press(event->position(), event->modifiers() & Qt::ShiftModifier);
}

void StickerPreview::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) m_canvas->doubleClick(event->position());
}

void StickerPreview::mouseMoveEvent(QMouseEvent *event) {
    if (!m_canvas->dragging()) {
        const Qt::CursorShape shape = m_canvas->cursorAt(event->position());
        if (shape == Qt::ArrowCursor) unsetCursor();
        else setCursor(shape);
    }
    m_canvas->move(event->position(), event->modifiers() & Qt::ShiftModifier);
}

void StickerPreview::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) m_canvas->release(event->position(), event->modifiers() & Qt::ShiftModifier);
}

// Delete, Backspace, the arrows and Esc belong to the preview while it has the focus: the main
// window's shortcuts (Delete removes topics) must not take them.
bool StickerPreview::event(QEvent *event) {
    if (event->type() == QEvent::ShortcutOverride && m_canvas->claimsKey(static_cast<QKeyEvent *>(event)->key())) {
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

void StickerPreview::keyPressEvent(QKeyEvent *event) {
    if (!m_canvas->key(event->key(), event->modifiers() & Qt::ShiftModifier)) QWidget::keyPressEvent(event);
}

// --- Dialog --------------------------------------------------------------------------------------

namespace {
// Hugeicons (docs/design/ui-foundations.md, "Icone"), light stroke for the dark modal.
QIcon hugeicon(const char *name) { return QIcon(QStringLiteral(":/assets/hugeicons/editor-%1.svg").arg(QLatin1String(name))); }

QIcon toolIcon(StickerPreview::Mode mode) {
    using Mode = StickerPreview::Mode;
    switch (mode) {
    case Mode::Shapes: return hugeicon("select");
    case Mode::Frame: return hugeicon("crop");
    case Mode::Rectangle: return hugeicon("rectangle");
    case Mode::Ellipse: return hugeicon("ellipse");
    case Mode::Line: return hugeicon("line");
    case Mode::Pen: return hugeicon("pen");
    case Mode::Polygon: return hugeicon("polygon");
    case Mode::Star: return hugeicon("star");
    }
    return {};
}

enum class Glyph { RotateLeft, RotateRight, FlipH, FlipV, Forward, Backward, Front, Back, Delete, Bold, Undo, Redo };

QIcon glyphIcon(Glyph glyph) {
    switch (glyph) {
    case Glyph::RotateLeft: return hugeicon("rotate-left");
    case Glyph::RotateRight: return hugeicon("rotate-right");
    case Glyph::FlipH: return hugeicon("flip-horizontal");
    case Glyph::FlipV: return hugeicon("flip-vertical");
    case Glyph::Forward: return hugeicon("bring-forward");
    case Glyph::Backward: return hugeicon("send-backward");
    case Glyph::Front: return hugeicon("bring-to-front");
    case Glyph::Back: return hugeicon("send-to-back");
    case Glyph::Delete: return hugeicon("delete");
    case Glyph::Bold: return hugeicon("bold");
    case Glyph::Undo: return hugeicon("undo");
    case Glyph::Redo: return hugeicon("redo");
    }
    return {};
}

// A grid of label / control rows, as the Export dialog (labels left, controls right).
QGridLayout *rowsGrid() {
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(12);
    grid->setColumnStretch(1, 1);
    return grid;
}

void addRow(QGridLayout *grid, const QString &text, QWidget *control) {
    const int row = grid->rowCount();
    grid->addWidget(new QLabel(text), row, 0);
    grid->addWidget(control, row, 1, Qt::AlignRight | Qt::AlignVCenter);
    if (control->accessibleName().isEmpty()) control->setAccessibleName(text);
}

void addRow(QGridLayout *grid, const QString &text, const QList<QWidget *> &controls) {
    auto *box = new QWidget;
    auto *row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    for (QWidget *control : controls) row->addWidget(control);
    addRow(grid, text, box);
}

NumberField *numberField(double minimum, double maximum, double value, int decimals = 0) {
    auto *field = new NumberField;
    field->setRange(minimum, maximum);
    field->setDecimals(decimals);
    field->setValue(value);
    field->setFixedWidth(kNumberFieldPx);
    return field;
}

NodeStyle::Reading colorReading(const QColor &color) {
    NodeStyle::Reading reading;
    reading.empty = !color.isValid();  // "none": shown as mixed (the dashed field)
    reading.value = color;
    return reading;
}
}  // namespace

// The editor's buttons in the modal's colours (docs/design/ui-components.md, "Modali"): set on each
// button, as a property selector on the dialog did not reach the tool buttons.
static QString toolStyle() {
    return UiTheme::qss(QStringLiteral("QToolButton { background:@surfaceSunken; border:0; border-radius:6px; }"
                                       "QToolButton:hover { background:@divider; }"
                                       "QToolButton:checked { background:@accent; }"));
}

static QString iconButtonStyle() {
    return UiTheme::qss(QStringLiteral("QToolButton { background:@surfaceRaised; border:1px solid @divider; border-radius:4px; }"
                                       "QToolButton:hover { background:@divider; }"
                                       "QToolButton:checked { background:@accent; border-color:@accent; }"));
}

static QString secondaryStyle() {
    return UiTheme::qss(QStringLiteral("QPushButton { background:@surfaceRaised; color:@text; border:1px solid @divider; }"
                                       "QPushButton:hover { background:@divider; }"
                                       "QPushButton:disabled { color:@textMuted; }"));
}

static QLabel *sectionTitle(const QString &text) {
    auto *label = new QLabel(text);
    label->setStyleSheet(UiTheme::qss(QStringLiteral("QLabel { color:@textMuted; font-size:12px; font-weight:600; }")));
    return label;
}

QToolButton *StickerEditorDialog::iconButton(const QIcon &icon, const QString &tip, const std::function<void()> &action, bool checkable) {
    auto *button = new QToolButton;
    button->setStyleSheet(iconButtonStyle());
    button->setIcon(icon);
    button->setIconSize(QSize(20, 20));
    button->setFixedSize(kIconButtonPx, kIconButtonPx);
    button->setCheckable(checkable);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    button->setCursor(Qt::PointingHandCursor);
    if (action) connect(button, &QToolButton::clicked, this, action);
    return button;
}

QPushButton *StickerEditorDialog::textButton(const QString &text, const std::function<void()> &action) {
    auto *button = new QPushButton(text);
    button->setStyleSheet(secondaryStyle());
    button->setCursor(Qt::PointingHandCursor);
    connect(button, &QPushButton::clicked, this, action);
    return button;
}

StickerEditorDialog::StickerEditorDialog(QWidget *parent, const QString &path, const QList<IconLibrary::Collection> &collections,
                                         const QString &collectionId)
    : ModalDialog(parent, tr("Edit sticker"), tr("Save as new"), 860) {
    Help::setPage(this, QStringLiteral("topic-contents/sticker-editor.md"));  // FEAT-092: F1 and "?" open it
    setObjectName(QStringLiteral("StickerEditorDialog"));
    m_edit.load(path);
    m_preview = new StickerPreview(&m_edit.canvas());
    m_edit.changed = [this] {
        rebuildSwatches();
        syncControls();
        updateState();
    };
    m_edit.styleChanged = [this] { syncStyle(); };
    m_edit.selectionChanged = [this] { updateState(); };

    auto *columns = new QHBoxLayout;
    columns->setSpacing(32);

    // Left: the tools, their options, the preview, Undo and Redo.
    using Mode = StickerPreview::Mode;
    auto *left = new QVBoxLayout;
    left->setSpacing(12);
    left->addWidget(sectionTitle(tr("Tools")));
    auto *tools = new QHBoxLayout;
    tools->setSpacing(4);
    auto *toolGroup = new QButtonGroup(this);
    const QList<std::tuple<Mode, QString, QString>> toolList{
        {Mode::Shapes, tr("Select: move and resize the shapes; double click to edit the points"), QStringLiteral("StickerShapesMode")},
        {Mode::Frame, tr("Frame: drag the edges to crop"), QStringLiteral("StickerFrameMode")},
        {Mode::Rectangle, tr("Rectangle (Shift: square)"), QStringLiteral("StickerRectangleTool")},
        {Mode::Ellipse, tr("Ellipse (Shift: circle)"), QStringLiteral("StickerEllipseTool")},
        {Mode::Line, tr("Line (Shift: steps of 45°)"), QStringLiteral("StickerLineTool")},
        {Mode::Pen, tr("Pen: click for a corner, drag for a curve; double click or Enter ends, a click on the first point closes"),
         QStringLiteral("StickerPenTool")},
        {Mode::Polygon, tr("Polygon, from its centre (Shift: upright)"), QStringLiteral("StickerPolygonTool")},
        {Mode::Star, tr("Star, from its centre (Shift: upright)"), QStringLiteral("StickerStarTool")}};
    for (const auto &[mode, tip, name] : toolList) {
        auto *button = new QToolButton;
        button->setStyleSheet(toolStyle());
        button->setObjectName(name);
        button->setIcon(toolIcon(mode));
        button->setIconSize(QSize(22, 22));
        button->setFixedSize(kToolPx, kToolPx);
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        button->setCheckable(true);
        button->setChecked(mode == Mode::Shapes);
        button->setCursor(Qt::PointingHandCursor);
        toolGroup->addButton(button);
        connect(button, &QToolButton::toggled, this, [this, mode](bool on) {
            if (on) m_preview->setMode(mode);
            updateState();
        });
        tools->addWidget(button);
        if (mode == Mode::Frame) tools->addSpacing(12);  // selecting and cropping, then drawing
    }
    tools->addStretch();
    left->addLayout(tools);
    // The chosen tool's option, in a row of fixed height so the preview does not move.
    auto *optionsRow = new QWidget;
    optionsRow->setFixedHeight(kOptionsRowPx);
    auto *options = new QHBoxLayout(optionsRow);
    options->setContentsMargins(0, 0, 0, 0);
    options->setSpacing(8);
    auto option = [this, options](const QString &label, NumberField *field, const QString &name) {
        auto *caption = new QLabel(label);
        field->setObjectName(name);
        field->setAccessibleName(label);
        connect(field, &NumberField::valueChosen, this, &StickerEditorDialog::updateToolOptions);
        options->addWidget(caption);
        options->addWidget(field);
        m_optionLabels.append(caption);
        return field;
    };
    m_cornerRadius = option(tr("Corners (px)"), numberField(0, 128, 0), QStringLiteral("StickerCornerRadius"));
    m_sides = option(tr("Sides"), numberField(3, 12, 5), QStringLiteral("StickerSides"));
    m_points = option(tr("Points"), numberField(3, 12, 5), QStringLiteral("StickerPoints"));
    // Without options: what the tool does, in a word.
    m_toolHint = new QLabel;
    m_toolHint->setObjectName(QStringLiteral("StickerToolHint"));
    m_toolHint->setStyleSheet(UiTheme::qss(QStringLiteral("QLabel { color:@textMuted; font-size:12px; }")));
    options->addWidget(m_toolHint);
    options->addStretch();
    left->addWidget(optionsRow);
    left->addWidget(m_preview);
    auto *history = new QHBoxLayout;
    history->setSpacing(8);
    m_undo = iconButton(glyphIcon(Glyph::Undo), tr("Undo (%1)").arg(QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText)), [this] { undo(); });
    m_redo = iconButton(glyphIcon(Glyph::Redo), tr("Redo (%1)").arg(QKeySequence(QKeySequence::Redo).toString(QKeySequence::NativeText)), [this] { redo(); });
    history->addWidget(m_undo);
    history->addWidget(m_redo);
    history->addStretch();
    left->addLayout(history);
    left->addStretch();
    // As wide as the preview on every page: the right column does not move when the page changes.
    auto *leftColumn = new QWidget;
    leftColumn->setFixedWidth(StickerPreview::kSizePx);
    leftColumn->setLayout(left);
    left->setContentsMargins(0, 0, 0, 0);
    columns->addWidget(leftColumn, 0, Qt::AlignTop);

    // Right: three pages (Style, Frame, Text), then where the new sticker goes.
    auto *right = new QVBoxLayout;
    right->setSpacing(16);
    m_pageSelect = new SegmentedControl({tr("Style"), tr("Frame"), tr("Text")});
    m_pageSelect->setObjectName(QStringLiteral("StickerPages"));
    right->addWidget(m_pageSelect);
    auto *pages = new QStackedWidget;
    connect(m_pageSelect, &SegmentedControl::segmentActivated, pages, &QStackedWidget::setCurrentIndex);
    auto page = [pages](QGridLayout *grid) {
        auto *widget = new QWidget;
        auto *box = new QVBoxLayout(widget);
        box->setContentsMargins(0, 0, 0, 0);
        box->addLayout(grid);
        box->addStretch();
        pages->addWidget(widget);
    };

    // Style: the drawing's colours, the selected or next shape's fill and outline, the order.
    QGridLayout *style = rowsGrid();
    style->addWidget(sectionTitle(tr("Colors of the drawing")), 0, 0, 1, 2);
    m_swatchGrid = new QGridLayout;
    m_swatchGrid->setSpacing(8);
    auto *swatches = new QWidget;
    swatches->setLayout(m_swatchGrid);
    m_swatchGrid->setContentsMargins(0, 0, 0, 0);
    style->addWidget(swatches, 1, 0, 1, 2, Qt::AlignLeft);
    m_fillKind = new SegmentedControl({tr("Color"), tr("None")});
    m_fillKind->setObjectName(QStringLiteral("StickerFillKind"));
    m_fillKind->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_fillKind, &SegmentedControl::segmentActivated, this, [this](int index) { m_edit.setFillNone(index == 1); });
    m_fillColor = new ColorField;
    m_fillColor->setObjectName(QStringLiteral("StickerFillColor"));
    m_fillColor->allowOpacity = false;
    connect(m_fillColor, &ColorField::colorChosen, this, [this](const QColor &color) { m_edit.setFillColor(color); });
    addRow(style, tr("Fill"), {m_fillKind, m_fillColor});
    m_strokeColor = new ColorField;
    m_strokeColor->setObjectName(QStringLiteral("StickerStrokeColor"));
    m_strokeColor->allowOpacity = false;
    connect(m_strokeColor, &ColorField::colorChosen, this, [this](const QColor &color) { m_edit.setStrokeColor(color); });
    addRow(style, tr("Outline"), m_strokeColor);
    m_strokeWidth = numberField(0, 40, 0, 1);
    m_strokeWidth->setStep(0.5);
    m_strokeWidth->setObjectName(QStringLiteral("StickerStrokeWidth"));
    m_strokeWidth->setToolTip(tr("On the sticker at 256 px; 0: no outline"));
    connect(m_strokeWidth, &NumberField::valueChosen, this, [this](double width) { m_edit.setStrokeWidth(width); });
    addRow(style, tr("Outline width (px)"), m_strokeWidth);
    using Order = SvgDocument::Order;
    QList<QWidget *> order;
    for (const auto &[glyph, tip, value] : {std::tuple{Glyph::Forward, tr("Bring forward"), Order::Forward},
                                            std::tuple{Glyph::Backward, tr("Send backward"), Order::Backward},
                                            std::tuple{Glyph::Front, tr("Bring to front"), Order::Front},
                                            std::tuple{Glyph::Back, tr("Send to back"), Order::Back}}) {
        QToolButton *button = iconButton(glyphIcon(glyph), tip, [this, value] { m_edit.reorderShapes(value); });
        m_shapeButtons.append(button);
        order.append(button);
    }
    QToolButton *remove = iconButton(glyphIcon(Glyph::Delete), tr("Delete"), [this] { m_edit.removeShapes(m_preview->selection()); });
    m_shapeButtons.append(remove);
    order.append(remove);
    addRow(style, tr("Selected shapes"), order);
    page(style);

    // Frame: turns, flips, the angle, fitting and a margin. The edges are dragged with the Frame tool.
    QGridLayout *frame = rowsGrid();
    addRow(frame, tr("Rotate"), {iconButton(glyphIcon(Glyph::RotateLeft), tr("Rotate left"), [this] { rotateBy(-90); }),
                                 iconButton(glyphIcon(Glyph::RotateRight), tr("Rotate right"), [this] { rotateBy(90); })});
    addRow(frame, tr("Flip"), {iconButton(glyphIcon(Glyph::FlipH), tr("Flip horizontally"), [this] { flipHorizontally(); }),
                               iconButton(glyphIcon(Glyph::FlipV), tr("Flip vertically"), [this] { flipVertically(); })});
    m_angle = numberField(-180, 180, 0);
    m_angle->setObjectName(QStringLiteral("StickerAngle"));
    connect(m_angle, &NumberField::valueChosen, this, [this](double value) { setRotation(value); });
    addRow(frame, tr("Angle (°)"), m_angle);
    addRow(frame, tr("Fit to the drawing"), textButton(tr("Fit"), [this] { fitToDrawing(); }));
    m_margin = numberField(1, 50, 10);
    m_margin->setObjectName(QStringLiteral("StickerMargin"));
    m_margin->setToolTip(tr("A share of the longer side, on every side"));
    addRow(frame, tr("Margin (%)"), {m_margin, textButton(tr("Add"), [this] { addMargin(); })});
    page(frame);

    // Text: one label on top of the drawing, dragged on the preview.
    QGridLayout *text = rowsGrid();
    m_labelText = new QLineEdit;
    m_labelText->setObjectName(QStringLiteral("StickerLabelText"));
    m_labelText->setPlaceholderText(tr("A word or a number"));
    m_labelText->setFixedWidth(kTextFieldPx);
    // Typing on a large sticker: one step after a pause (each step rewrites the whole file).
    connect(m_labelText, &QLineEdit::textEdited, this, [this](const QString &value) {
        readLabelControls();
        m_edit.typeLabelText(value);
    });
    addRow(text, tr("Text"), m_labelText);
    m_labelFont = makeDarkSelect();
    m_labelFont->setObjectName(QStringLiteral("StickerLabelFont"));
    m_labelFont->addItems(StickerEdit::labelFamilies());
    m_labelFont->setFixedWidth(kTextFieldPx);
    connect(m_labelFont, &QComboBox::currentTextChanged, this,
            [this](const QString &family) { m_edit.setLabelFamily(family); });
    addRow(text, tr("Font"), m_labelFont);
    m_labelSize = numberField(5, 100, m_edit.labelLook().sizePercent);
    m_labelSize->setObjectName(QStringLiteral("StickerLabelSize"));
    m_labelSize->setToolTip(tr("A share of the longer side"));
    connect(m_labelSize, &NumberField::valueChosen, this, [this](double percent) { m_edit.setLabelSizePercent(percent); });
    addRow(text, tr("Size (%)"), m_labelSize);
    m_labelColor = new ColorField;
    m_labelColor->setObjectName(QStringLiteral("StickerLabelColor"));
    m_labelColor->allowOpacity = false;
    connect(m_labelColor, &ColorField::colorChosen, this,
            [this](const QColor &color) { m_edit.setLabelColor(color); });
    addRow(text, tr("Color"), m_labelColor);
    m_labelBold = iconButton(glyphIcon(Glyph::Bold), tr("Bold"), {}, true);
    m_labelBold->setObjectName(QStringLiteral("StickerLabelBold"));
    connect(m_labelBold, &QToolButton::toggled, this,
            [this](bool bold) { m_edit.setLabelBold(bold); });
    addRow(text, tr("Bold"), m_labelBold);
    page(text);
    right->addWidget(pages);

    // Where the new sticker goes.
    right->addWidget(sectionTitle(tr("Save as")));
    QGridLayout *save = rowsGrid();
    m_name = new QLineEdit(IconLibrary::displayName(path));
    m_name->setObjectName(QStringLiteral("StickerName"));
    m_name->setFixedWidth(kTextFieldPx);
    addRow(save, tr("Name"), m_name);
    m_collection = makeDarkSelect();
    m_collection->setObjectName(QStringLiteral("StickerCollection"));
    m_collection->setFixedWidth(kTextFieldPx);
    for (const IconLibrary::Collection &c : collections)
        if (!c.hidden) m_collection->addItem(c.name, c.id);
    m_collection->addItem(tr("New collection…"), QString());
    m_collection->setCurrentIndex(qMax(0, m_collection->findData(collectionId)));
    addRow(save, tr("Collection"), m_collection);
    m_newCollection = new QLineEdit;
    m_newCollection->setObjectName(QStringLiteral("StickerNewCollection"));
    m_newCollection->setPlaceholderText(tr("Name of the new collection"));
    m_newCollection->setFixedWidth(kTextFieldPx);
    addRow(save, tr("New collection"), m_newCollection);
    m_newCollectionLabel = qobject_cast<QLabel *>(save->itemAtPosition(save->rowCount() - 1, 0)->widget());
    right->addLayout(save);
    right->addStretch();
    columns->addLayout(right, 1);
    contentLayout()->addLayout(columns);

    connect(m_name, &QLineEdit::textChanged, this, &StickerEditorDialog::updateState);
    connect(m_newCollection, &QLineEdit::textChanged, this, &StickerEditorDialog::updateState);
    connect(m_collection, &QComboBox::currentIndexChanged, this, &StickerEditorDialog::updateState);
    rebuildSwatches();
    syncControls();
    syncStyle();
    updateToolOptions();
    updateState();
}

void StickerEditorDialog::updateToolOptions() {
    m_edit.setToolOptions(m_cornerRadius->value(), int(m_sides->value()), int(m_points->value()));
}

void StickerEditorDialog::syncStyle() {
    const SvgDocument::ShapeStyle style = m_edit.style();
    m_fillKind->setCurrentIndex(style.fill.isValid() ? 0 : 1);
    m_fillColor->setEnabled(style.fill.isValid());
    m_fillColor->setReading(colorReading(style.fill.isValid() ? style.fill : m_edit.lastFill()));
    m_strokeColor->setReading(colorReading(style.stroke));
    m_strokeWidth->setValue(style.stroke.isValid() ? style.strokeWidth : 0);
}

void StickerEditorDialog::addMargin() { m_edit.addMargin(m_margin->value()); }

void StickerEditorDialog::readLabelControls() {
    if (!m_edit.labelText().isEmpty()) return;  // an existing label keeps its own look
    StickerEdit::LabelLook look = m_edit.labelLook();
    look.family = m_labelFont->currentText();
    look.sizePercent = m_labelSize->value();
    look.bold = m_labelBold->isChecked();
    m_edit.setLabelLook(look);
}

void StickerEditorDialog::setLabelText(const QString &text) {
    readLabelControls();
    m_edit.setLabelText(text);
}

void StickerEditorDialog::accept() {
    m_edit.settle();  // what is shown is what is saved
    ModalDialog::accept();
}

void StickerEditorDialog::syncControls() {
    const QSignalBlocker text(m_labelText), font(m_labelFont), bold(m_labelBold);
    m_angle->setValue(m_edit.document().transform().rotation);
    const QString label = m_edit.labelText();
    if (m_labelText->text().trimmed() != label) m_labelText->setText(label);
    const StickerEdit::LabelLook look = m_edit.labelLook();
    if (!label.isEmpty()) {  // without a label the controls keep what was chosen for the next one
        const int family = m_labelFont->findText(look.family);
        if (family >= 0) m_labelFont->setCurrentIndex(family);
        m_labelSize->setValue(look.sizePercent);
        m_labelBold->setChecked(look.bold);
    }
    m_labelColor->setReading(colorReading(look.color));
}

void StickerEditorDialog::rebuildSwatches() {
    // Swatches are reused, never deleted while the dialog is open: the picker that changes a colour
    // runs its own event loop inside a swatch's click. Spare ones are hidden.
    const QList<QColor> colors = m_edit.colors();
    for (int i = 0; i < colors.size(); ++i) {
        if (i == m_swatches.size()) {
            auto *swatch = new QToolButton;
            swatch->setObjectName(QStringLiteral("StickerSwatch"));
            swatch->setFixedSize(kSwatchPx, kSwatchPx);
            swatch->setCursor(Qt::PointingHandCursor);
            swatch->setAccessibleName(tr("Color %1").arg(i + 1));
            connect(swatch, &QToolButton::clicked, this, [this, i, swatch] {
                const QColor color = m_edit.colors().value(i);
                if (!color.isValid()) return;
                const QRect anchor(swatch->mapToGlobal(QPoint(0, 0)), swatch->size());
                // The picker can stay open (free colour panel): each choice replaces the previous one,
                // followed by value because the colours' order can change (the default black).
                auto current = std::make_shared<QColor>(color);
                ColorPicker::exec(anchor, ColorPicker::Placement::Above, {color, {}, false}, [this, current](const QColor &chosen) {
                    QColor next = chosen;
                    next.setAlpha(current->alpha());
                    m_edit.replaceColor(*current, chosen);
                    *current = next;
                });
            });
            m_swatchGrid->addWidget(swatch, i / kSwatchColumns, i % kSwatchColumns);
            m_swatches.append(swatch);
        }
        QToolButton *swatch = m_swatches[i];
        swatch->setToolTip(colors[i].name());
        swatch->setStyleSheet(swatchStyle(colors[i]));
        swatch->show();
    }
    for (int i = int(colors.size()); i < m_swatches.size(); ++i) m_swatches[i]->hide();
}

void StickerEditorDialog::updateState() {
    // The options of the chosen tool only.
    using Mode = StickerPreview::Mode;
    const Mode mode = m_preview->mode();
    const QList<std::pair<QWidget *, bool>> shown{{m_cornerRadius, mode == Mode::Rectangle}, {m_sides, mode == Mode::Polygon}, {m_points, mode == Mode::Star}};
    for (int i = 0; i < shown.size(); ++i) {
        shown[i].first->setVisible(shown[i].second);
        m_optionLabels[i]->setVisible(shown[i].second);
    }
    QString hint;
    if (mode == Mode::Shapes) hint = tr("Click a shape to select it, double click to edit its points");
    else if (mode == Mode::Frame) hint = tr("Drag the frame's edges to crop the sticker");
    else if (mode == Mode::Ellipse || mode == Mode::Line) hint = tr("Drag on the preview; Shift keeps the proportions");
    else if (mode == Mode::Pen) hint = tr("Click for corners, drag for curves; Enter ends");
    m_toolHint->setText(hint);
    m_toolHint->setVisible(!hint.isEmpty());
    for (QToolButton *button : std::as_const(m_shapeButtons)) button->setEnabled(!m_preview->selection().isEmpty());
    m_undo->setEnabled(m_edit.canUndo());
    m_redo->setEnabled(m_edit.canRedo());
    const bool newCollection = m_collection->currentData().toString().isEmpty();
    m_newCollection->setVisible(newCollection);
    if (m_newCollectionLabel) m_newCollectionLabel->setVisible(newCollection);
    acceptButton()->setEnabled(m_edit.isValid() && !stickerName().isEmpty() && (!newCollection || !newCollectionName().isEmpty()));
}

QString StickerEditorDialog::stickerName() const { return StickerEdit::cleanName(m_name->text()); }

QString StickerEditorDialog::collectionId() const { return m_collection->currentData().toString(); }

QString StickerEditorDialog::newCollectionName() const { return m_newCollection->text().trimmed(); }

// Ctrl+Z / Ctrl+Y as keys of the editor, not shortcuts: they work whatever window is active and the
// main window's Undo cannot take them. A text field keeps its own Undo (it handles the key first).
bool StickerEditorDialog::event(QEvent *event) {
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)) {
            event->accept();
            return true;
        }
    }
    return ModalDialog::event(event);
}

void StickerEditorDialog::keyPressEvent(QKeyEvent *event) {
    if (event->matches(QKeySequence::Undo)) undo();
    else if (event->matches(QKeySequence::Redo)) redo();
    else ModalDialog::keyPressEvent(event);
}

void StickerEditorDialog::reject() {
    m_edit.settle();
    if (isModified()) {
        const bool discard = confirmDiscard ? confirmDiscard()
                                            : ModalDialog::ask(this, tr("Discard changes?"), tr("The changes to this sticker will be lost."),
                                                               tr("Discard")) == ModalDialog::Choice::Primary;
        if (!discard) return;
    }
    ModalDialog::reject();
}
