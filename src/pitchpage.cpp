#include "pitchpage.h"

#include "mindmapmodel.h"
#include "modaldialog.h"
#include "pitchtemplateeditor.h"
#include "stylecontrols.h"
#include "texteditmenu.h"
#include "uitheme.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr qreal kPreviewRadius = 8;
const QColor kIconInk(0xe5, 0xe9, 0xef);  // light strokes on the dark panel, like the menu-* icons

// The five layouts as small drawings (2x for sharp HiDPI), from the same shapes as the slides.
QIcon layoutIcon(Pitch::Layout layout) {
    QPixmap pixmap(40, 40);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(kIconInk, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const auto lines = [&p](qreal x0, qreal x1, std::initializer_list<qreal> ys) {
        for (qreal y : ys) p.drawLine(QPointF(x0, y), QPointF(x1, y));
    };
    switch (layout) {
    case Pitch::Layout::Bracket: {
        QPainterPath brace(QPointF(9, 5));
        brace.quadTo(7, 5, 7, 7); brace.lineTo(7, 9); brace.quadTo(7, 10, 5.5, 10);
        brace.quadTo(7, 10, 7, 11); brace.lineTo(7, 13); brace.quadTo(7, 15, 9, 15);
        p.drawPath(brace);
        p.drawLine(QPointF(2, 10), QPointF(4, 10));
        lines(11, 18, {6, 10, 14});
        break;
    }
    case Pitch::Layout::Bullets:
        for (qreal y : {5.0, 10.0, 15.0}) { p.drawPoint(QPointF(3.5, y)); p.drawLine(QPointF(7, y), QPointF(17, y)); }
        break;
    case Pitch::Layout::Right:
        lines(3, 17, {5});
        lines(7, 17, {10, 15});
        break;
    case Pitch::Layout::Fan: {
        for (qreal y : {5.0, 10.0, 15.0}) {
            QPainterPath branch(QPointF(3, 10));
            branch.cubicTo(QPointF(7, 10), QPointF(7, y), QPointF(10, y));
            p.drawPath(branch);
            p.drawLine(QPointF(12, y), QPointF(17, y));
        }
        break;
    }
    case Pitch::Layout::Org:
        p.drawLine(QPointF(7, 4), QPointF(13, 4));
        p.drawLine(QPointF(10, 4), QPointF(10, 9));
        p.drawLine(QPointF(4, 9), QPointF(16, 9));
        for (qreal x : {4.0, 10.0, 16.0}) p.drawLine(QPointF(x, 9), QPointF(x, 14));
        lines(2.5, 5.5, {16}); lines(8.5, 11.5, {16}); lines(14.5, 17.5, {16});
        break;
    case Pitch::Layout::Fishbone:
        p.drawLine(QPointF(2, 10), QPointF(14, 10));  // spine
        p.drawLine(QPointF(15, 7), QPointF(18, 10)); p.drawLine(QPointF(15, 13), QPointF(18, 10));  // head
        for (qreal x : {6.0, 11.0}) { p.drawLine(QPointF(x + 2, 10), QPointF(x, 4)); p.drawLine(QPointF(x + 2, 10), QPointF(x, 16)); }
        break;
    case Pitch::Layout::Timeline:
        p.drawLine(QPointF(2, 10), QPointF(18, 10));
        for (qreal x : {5.0, 10.0, 15.0}) p.drawEllipse(QPointF(x, 10), 1.3, 1.3);
        p.drawLine(QPointF(5, 8), QPointF(5, 5)); p.drawLine(QPointF(10, 12), QPointF(10, 15)); p.drawLine(QPointF(15, 8), QPointF(15, 5));
        break;
    case Pitch::Layout::Grid:
        for (qreal x : {2.5, 10.5})
            for (qreal y : {3.5, 11.0}) p.drawRoundedRect(QRectF(x, y, 7, 5.5), 1.2, 1.2);
        break;
    case Pitch::Layout::Numbered: {
        QFont font = p.font();
        font.setPixelSize(5);
        font.setBold(true);
        p.setFont(font);
        for (int i = 0; i < 3; ++i) {
            p.drawText(QRectF(1, 2.5 + i * 5, 5, 5), Qt::AlignCenter, QString::number(i + 1));
            p.drawLine(QPointF(7.5, 5 + i * 5), QPointF(17, 5 + i * 5));
        }
        break;
    }
    case Pitch::Layout::Circle:
        p.drawEllipse(QPointF(10, 10), 2.5, 2.5);
        for (int i = 0; i < 5; ++i) {
            const qreal angle = -std::numbers::pi / 2 + 2 * std::numbers::pi * i / 5;
            p.drawEllipse(QPointF(10 + 7 * std::cos(angle), 10 + 7 * std::sin(angle)), 1.4, 1.4);
        }
        break;
    }
    p.end();
    return QIcon(pixmap);
}

QWidget *row(QWidget *parent, const QString &text, QWidget *control) {
    auto *widget = new QWidget(parent);
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto *label = new QLabel(text, widget);
    control->setAccessibleName(text);
    layout->addWidget(label);
    layout->addStretch();
    layout->addWidget(control);
    return widget;
}

QLabel *caption(QWidget *parent, const QString &text) {
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("FieldCaption"));
    return label;
}

// A card of the Change theme menu: a small slide in the look.
QToolButton *themeCard(QWidget *parent, const QString &name, const Pitch::Look &look, bool cover) {
    QPixmap picture(QSize(112, 63) * 2);
    picture.setDevicePixelRatio(2);
    picture.fill(Qt::transparent);
    QPainter painter(&picture);
    Pitch::Slide sample;
    sample.title = QStringLiteral("Pitch");
    sample.cover = cover;
    sample.number = 1;
    Pitch::paint(&painter, QRectF(0, 0, 112, 63), sample, look);
    painter.end();
    auto *card = new QToolButton(parent);
    card->setCheckable(true);
    card->setIcon(QIcon(picture));
    card->setIconSize(QSize(112, 63));
    card->setText(name);
    card->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    return card;
}
}  // namespace

QString pitchThemeName(Pitch::Theme theme) {
    static const char *const names[Pitch::kThemeCount] = {
        QT_TRANSLATE_NOOP("PitchPage", "Dark"),        QT_TRANSLATE_NOOP("PitchPage", "Light"),
        QT_TRANSLATE_NOOP("PitchPage", "Sheet theme"), QT_TRANSLATE_NOOP("PitchPage", "Proposal"),
        QT_TRANSLATE_NOOP("PitchPage", "Playful"),     QT_TRANSLATE_NOOP("PitchPage", "Launch"),
        QT_TRANSLATE_NOOP("PitchPage", "Manifesto"),   QT_TRANSLATE_NOOP("PitchPage", "Halftone"),
        QT_TRANSLATE_NOOP("PitchPage", "Paper"),       QT_TRANSLATE_NOOP("PitchPage", "Night"),
        QT_TRANSLATE_NOOP("PitchPage", "Soft"),        QT_TRANSLATE_NOOP("PitchPage", "Portfolio"),
        QT_TRANSLATE_NOOP("PitchPage", "Editorial")};
    return QCoreApplication::translate("PitchPage", names[int(theme)]);
}

PitchPreview::PitchPreview(QWidget *parent) : QWidget(parent) {
    setFixedHeight(kMinHeightPx);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void PitchPreview::showSlide(const Pitch::Slide &slide, const Pitch::Look &look, qreal ratio) {
    m_slide = slide;
    m_look = look;
    m_ratio = ratio;
    fitHeight();
    update();
}

// As wide as the panel and at most 16:9 tall (user's photo 2026-10-26: it was
// taller); a tall slide sits in it with bands. Before, setFixedHeight also raised the minimum height,
// so the box only grew.
void PitchPreview::fitHeight() {
    if (width() > 0) setFixedHeight(std::max(kMinHeightPx, qRound(width() / std::max(16.0 / 9.0, m_ratio))));
}

void PitchPreview::resizeEvent(QResizeEvent *) { fitHeight(); }

void PitchPreview::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), kPreviewRadius, kPreviewRadius);
    painter.setClipPath(clip);
    painter.fillRect(rect(), m_look.background.isValid() ? m_look.background : QColor(Qt::black));
    const QSizeF slide = QSizeF(m_ratio, 1).scaled(QSizeF(size()), Qt::KeepAspectRatio);
    const QRectF target(QPointF((width() - slide.width()) / 2, (height() - slide.height()) / 2), slide);
    Pitch::paint(&painter, target, m_slide, m_look);
}

PitchPage::PitchPage(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("InspectorPitchPage"));
    setStyleSheet(UiTheme::qss(QStringLiteral(
        "QWidget#InspectorPitchPage { background:transparent; }"
        "QLabel { color:@text; font-size:13px; background:transparent; }"
        "QLabel#GroupHeading { font-weight:600; }"
        "QLabel#FieldCaption { color:@textMuted; font-size:12px; }"
        // Disabled look of the side panel (docs/design/ui-foundations.md, "Controlli disattivati").
        "QLabel:disabled { color:@textMuted; }"
        "QPushButton { background:@surfaceRaised; color:@text; border:1px solid @divider; border-radius:4px;"
        " min-height:26px; font-size:12px; font-weight:600; padding:0 8px; }"
        "QPushButton:hover { background:@divider; }"
        "QPushButton:disabled { color:@textMuted; }"
        "QLineEdit { color:@text; background:@surface; border:1px solid @divider; border-radius:4px; min-height:24px;"
        " max-height:24px; padding:0 6px; font-size:12px; }"
        "QLineEdit:focus { border-color:@accent; }"
        "QToolButton[pitchLayout=\"true\"] { background:transparent; border:0; border-radius:4px; }"
        "QToolButton[pitchLayout=\"true\"]:hover { background:@divider; }"
        "QToolButton[pitchLayout=\"true\"]:checked { background:@surfaceRaised; }"
        "QWidget#PitchLayoutTrack { background:@surface; border:1px solid @divider; border-radius:8px; }")));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 0);
    layout->setSpacing(12);

    auto *heading = new QLabel(tr("View in Pitch mode"), this);
    heading->setObjectName(QStringLiteral("GroupHeading"));
    layout->addWidget(heading);
    m_preview = new PitchPreview(this);
    m_preview->setObjectName(QStringLiteral("PitchPreview"));
    layout->addWidget(m_preview);
    m_theme = new QPushButton(tr("Change theme"), this);
    m_theme->setObjectName(QStringLiteral("PitchTheme"));
    connect(m_theme, &QPushButton::clicked, this, &PitchPage::chooseTheme);
    layout->addWidget(m_theme);
    m_editTemplate = new QPushButton(tr("Edit template"), this);
    m_editTemplate->setObjectName(QStringLiteral("PitchEditTemplate"));
    m_editTemplate->hide();
    connect(m_editTemplate, &QPushButton::clicked, this, &PitchPage::editSheetTemplate);
    layout->addWidget(m_editTemplate);
    m_aspect = makeDarkSelect(this);
    m_aspect->setObjectName(QStringLiteral("PitchAspect"));
    m_aspect->setFixedWidth(110);
    m_aspect->addItems({tr("Auto"), QStringLiteral("16:9"), QStringLiteral("4:3"), QStringLiteral("9:16"), QStringLiteral("3:4")});
    connect(m_aspect, &QComboBox::activated, this, [this](int index) {
        if (m_updating || !m_model || !setSheetValues) return;
        setSheetValues(m_model->pitchTheme, Pitch::aspectKey(static_cast<Pitch::Aspect>(index)), false);
    });
    QWidget *aspectRow = row(this, tr("Aspect ratio"), m_aspect);
    layout->addWidget(aspectRow);
    QWidget *sheetDivider = panelDivider(this);
    layout->addWidget(sheetDivider);
    m_sheetRows = {m_theme, aspectRow, sheetDivider};
    m_editor = new PitchTemplateEditor(this);
    m_editor->hide();
    m_editor->pickFile = [this](const QString &caption, const QString &filter, bool save, const QString &name) {
        if (pickFile) return pickFile(caption, filter, save, name);
        return save ? QFileDialog::getSaveFileName(this, caption, name, filter) : QFileDialog::getOpenFileName(this, caption, {}, filter);
    };
    connect(m_editor, &PitchTemplateEditor::draftChanged, this, &PitchPage::showPreview);
    connect(m_editor, &PitchTemplateEditor::saveRequested, this, &PitchPage::saveTemplate);
    connect(m_editor, &PitchTemplateEditor::cancelRequested, this, &PitchPage::stopEditing);
    connect(m_editor, &PitchTemplateEditor::deleteRequested, this, [this] {
        // The maps that use it keep their copy.
        if (!PitchTemplates::remove(m_editor->draft().id)) {
            ModalDialog::inform(this, tr("Error"), tr("The template could not be deleted."));
            return;
        }
        stopEditing();
    });
    layout->addWidget(m_editor);

    // Topic rows: values in the order of Pitch::Visibility / Delivery.
    const auto topicSelect = [this](const char *name, Document::PitchField field, const auto &toKey) {
        QComboBox *select = makeDarkSelect(this);
        select->setObjectName(QLatin1String(name));
        select->setFixedWidth(150);
        connect(select, &QComboBox::activated, this, [this, field, toKey](int index) {
            if (!m_updating && setTopicValue) setTopicValue(field, toKey(index));
        });
        return select;
    };
    m_topicSlide = topicSelect("PitchTopicSlide", Document::PitchField::TopicSlide,
                               [](int i) { return Pitch::visibilityKey(static_cast<Pitch::Visibility>(i)); });
    m_listSlide = topicSelect("PitchListSlide", Document::PitchField::ListSlide,
                              [](int i) { return Pitch::visibilityKey(static_cast<Pitch::Visibility>(i)); });
    m_delivery = topicSelect("PitchDelivery", Document::PitchField::Delivery,
                             [](int i) { return Pitch::deliveryKey(static_cast<Pitch::Delivery>(i)); });
    m_delivery->setFixedWidth(220);
    m_delivery->addItems({tr("One at a time - memorize"), tr("One at a time"), tr("All together")});
    QWidget *topicRow = row(this, tr("Topic slide"), m_topicSlide);
    QWidget *listRow = row(this, tr("List slide"), m_listSlide);
    layout->addWidget(topicRow);
    QWidget *topicDivider = panelDivider(this);
    layout->addWidget(topicDivider);
    layout->addWidget(listRow);
    QLabel *deliveryCaption = caption(this, tr("Delivery"));
    layout->addWidget(deliveryCaption);
    layout->addWidget(m_delivery);
    QLabel *layoutCaption = caption(this, tr("Layout"));
    layout->addWidget(layoutCaption);
    auto *track = new QWidget(this);
    track->setObjectName(QStringLiteral("PitchLayoutTrack"));
    track->setAttribute(Qt::WA_StyledBackground);
    // Two rows of five (user's decision 2026-10-26).
    auto *trackLayout = new QGridLayout(track);
    trackLayout->setContentsMargins(4, 4, 4, 4);
    trackLayout->setSpacing(2);
    auto *group = new QButtonGroup(this);
    const QStringList names{tr("Bracket"), tr("Bullet list"), tr("Aligned right"), tr("Fan"), tr("Org chart"),
                            tr("Fishbone"), tr("Timeline"), tr("Grid"), tr("Numbered list"), tr("Circle")};
    for (int i = 0; i < Pitch::kLayoutCount; ++i) {
        auto *button = new QToolButton(track);
        button->setProperty("pitchLayout", true);
        button->setObjectName(QStringLiteral("PitchLayout%1").arg(i));
        button->setCheckable(true);
        button->setIcon(layoutIcon(static_cast<Pitch::Layout>(i)));
        button->setIconSize(QSize(20, 20));
        button->setFixedHeight(26);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setToolTip(names[i]);
        button->setAccessibleName(names[i]);
        group->addButton(button, i);
        trackLayout->addWidget(button, i / 5, i % 5);
        m_layouts.append(button);
    }
    connect(group, &QButtonGroup::idClicked, this, [this](int index) {
        if (!m_updating && setTopicValue) setTopicValue(Document::PitchField::Layout, Pitch::layoutKey(static_cast<Pitch::Layout>(index)));
    });
    layout->addWidget(track);
    layout->addStretch();
    m_topicRows = {topicRow, topicDivider, listRow, deliveryCaption, m_delivery, layoutCaption, track};
}

void PitchPage::refill(QComboBox *select, const QStringList &labels, int current) {
    const QSignalBlocker blocker(select);
    if (select->count() != labels.size()) {
        select->clear();
        select->addItems(labels);
    } else {
        for (int i = 0; i < labels.size(); ++i) select->setItemText(i, labels[i]);
    }
    select->setCurrentIndex(current);
}

void PitchPage::showTopic(const MindMapModel *model, const MindNode *topic) {
    m_model = model;
    m_topic = topic;
    if (!model) return;
    m_updating = true;
    {
        const QSignalBlocker blocker(m_aspect);
        m_aspect->setCurrentIndex(int(Pitch::aspectFromKey(model->pitchAspect)));
    }
    showPreview();
    m_editTemplate->setVisible(!m_editing && !model->pitchTemplate.isEmpty());
    for (QWidget *widget : std::as_const(m_topicRows)) widget->setEnabled(topic != nullptr);
    if (topic) {
        // "Auto" says what it resolves to, e.g. "AUTO (Visible)".
        const auto autoLabel = [this](bool shown) { return tr("Auto (%1)").arg(shown ? tr("Visible") : tr("Hidden")); };
        refill(m_topicSlide, {autoLabel(Pitch::autoTopicSlide(topic)), tr("Visible"), tr("Hidden")},
               int(Pitch::visibilityFromKey(topic->pitchTopicSlide)));
        refill(m_listSlide, {autoLabel(Pitch::autoListSlide(topic)), tr("Visible"), tr("Hidden")},
               int(Pitch::visibilityFromKey(topic->pitchListSlide)));
        {
            const QSignalBlocker blocker(m_delivery);
            m_delivery->setCurrentIndex(int(Pitch::deliveryFromKey(topic->pitchDelivery)));
        }
        m_layouts[int(Pitch::layoutFromKey(topic->pitchLayout))]->setChecked(true);
    }
    m_updating = false;
}

void PitchPage::chooseTheme() {
    if (!m_model) return;
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("PitchThemeMenu"));
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(TextEditMenu::styleSheet());
    auto *cards = new QWidget(&menu);
    cards->setStyleSheet(UiTheme::qss(QStringLiteral(
        "QToolButton { color:@text; background:transparent; border:2px solid transparent; border-radius:8px; font-size:12px; padding:4px; }"
        "QToolButton:hover { background:@divider; } QToolButton:checked { border-color:@textMuted; }"
        "QLabel#PitchThemeGroup { color:@textMuted; font-size:12px; background:transparent; padding:0 4px; }")));
    auto *grid = new QGridLayout(cards);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setSpacing(8);
    // Stage B: the basic themes, then the illustrated ones, three to a row (FEAT-038 grid menus).
    using T = Pitch::Theme;
    const T themes[] = {T::Light, T::Dark, T::Sheet, T::Proposal, T::Paper, T::Playful, T::Launch,
                        T::Night, T::Manifesto, T::Halftone, T::Soft, T::Portfolio, T::Editorial};
    constexpr int kColumns = 3;
    const auto addHeading = [&](const QString &text, int row) {
        auto *heading = new QLabel(text, cards);
        heading->setObjectName(QStringLiteral("PitchThemeGroup"));
        grid->addWidget(heading, row, 0, 1, kColumns);
    };
    const auto sheetTemplate = Pitch::sheetTemplate(*m_model);
    const Pitch::Theme current = Pitch::themeFromKey(m_model->pitchTheme);
    int index = 0;
    for (const Pitch::Theme theme : themes) {
        const bool illustrated = index >= Pitch::kBasicThemeCount;
        if (index == 0) addHeading(tr("Basic"), 0);
        if (index == Pitch::kBasicThemeCount) addHeading(tr("Illustrated"), 2);
        // An illustrated theme shows its cover.
        QToolButton *card = themeCard(cards, pitchThemeName(theme), Pitch::look(*m_model, theme), illustrated);
        card->setObjectName(QStringLiteral("PitchThemeCard%1").arg(int(theme)));
        card->setChecked(!sheetTemplate && theme == current);
        // A theme replaces the sheet's template (MainWindow clears it).
        connect(card, &QToolButton::clicked, &menu, [this, &menu, theme] {
            menu.close();
            if (setSheetValues) setSheetValues(Pitch::themeKey(theme), m_model->pitchAspect, true);
        });
        const int position = illustrated ? index - Pitch::kBasicThemeCount : index;
        grid->addWidget(card, (illustrated ? 3 : 1) + position / kColumns, position % kColumns);
        ++index;
    }
    // FEAT-089: the library's templates (the sheet's first if the library lacks it), New, Import.
    QList<Pitch::Template> templates = PitchTemplates::all();
    const bool sheetOnly = sheetTemplate && std::none_of(templates.cbegin(), templates.cend(), [&](const Pitch::Template &t) {
        return t.id == sheetTemplate->id;
    });
    if (sheetOnly) templates.prepend(*sheetTemplate);
    const int templatesRow = 3 + (Pitch::kThemeCount - Pitch::kBasicThemeCount + kColumns - 1) / kColumns;
    addHeading(tr("Templates"), templatesRow);
    int place = 0;
    for (const Pitch::Template &value : std::as_const(templates)) {
        const bool onlyHere = sheetOnly && value.id == sheetTemplate->id;
        QToolButton *card = themeCard(cards, onlyHere ? tr("%1 (this map)").arg(value.name) : value.name, Pitch::look(*m_model, value), true);
        card->setObjectName(QStringLiteral("PitchTemplateCard-%1").arg(value.id));
        card->setToolTip(value.name);
        card->setChecked(sheetTemplate && sheetTemplate->id == value.id);
        connect(card, &QToolButton::clicked, &menu, [this, &menu, value, onlyHere] {
            menu.close();
            // The map's own copy stays; a library template is copied again (it may have changed).
            if (setSheetTemplate && !onlyHere) setSheetTemplate(Pitch::toJson(value));
        });
        grid->addWidget(card, templatesRow + 1 + place / kColumns, place % kColumns);
        ++place;
    }
    auto *create = new QToolButton(cards);
    create->setObjectName(QStringLiteral("PitchNewTemplate"));
    create->setText(tr("New template"));
    create->setIcon(QIcon(QStringLiteral(":/assets/hugeicons/menu-plus.svg")));
    create->setIconSize(QSize(24, 24));
    create->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    create->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(create, &QToolButton::clicked, &menu, [this, &menu] {
        menu.close();
        newTemplate();
    });
    grid->addWidget(create, templatesRow + 1 + place / kColumns, place % kColumns);
    ++place;
    auto *import = new QPushButton(tr("Import template..."), cards);
    import->setObjectName(QStringLiteral("PitchImportTemplate"));
    import->setStyleSheet(UiTheme::qss(QStringLiteral(
        "QPushButton { background:@surfaceRaised; color:@text; border:1px solid @divider; border-radius:4px; min-height:26px;"
        " font-size:12px; font-weight:600; padding:0 8px; } QPushButton:hover { background:@divider; }")));
    connect(import, &QPushButton::clicked, &menu, [this, &menu] {
        menu.close();
        const QString filter = tr("Pitch template (*.%1)").arg(QLatin1String(Pitch::kTemplateSuffix));
        const QString path = pickFile ? pickFile(tr("Import template"), filter, false, QString())
                                      : QFileDialog::getOpenFileName(this, tr("Import template"), {}, filter);
        if (!path.isEmpty()) importTemplate(path);
    });
    grid->addWidget(import, templatesRow + 1 + (place + kColumns - 1) / kColumns, 0, 1, kColumns);
    auto *action = new QWidgetAction(&menu);
    action->setDefaultWidget(cards);
    menu.addAction(action);
    menu.exec(m_theme->mapToGlobal(QPoint(0, m_theme->height() + 4)));
}

void PitchPage::showPreview() {
    if (!m_model) return;
    Pitch::Slide slide = Pitch::previewSlide(*m_model, m_topic);
    if (slide.number == 0) slide.number = 1;  // FEAT-089: the preview shows where the number goes
    const Pitch::Look look = m_editing ? Pitch::look(*m_model, m_editor->draft()) : Pitch::sheetLook(*m_model);
    m_preview->showSlide(slide, look, Pitch::aspectRatio(Pitch::aspectFromKey(m_model->pitchAspect), 16.0 / 9.0));
}

void PitchPage::startEditing(const Pitch::Template &value, bool inLibrary) {
    if (!m_model) return;
    m_editing = true;
    m_editor->edit(value, m_model, inLibrary);
    for (QWidget *widget : std::as_const(m_sheetRows)) widget->hide();
    for (QWidget *widget : std::as_const(m_topicRows)) widget->hide();
    m_editTemplate->hide();
    m_editor->show();
    showPreview();
}

void PitchPage::stopEditing() {
    m_editing = false;
    m_editor->hide();
    for (QWidget *widget : std::as_const(m_sheetRows)) widget->show();
    for (QWidget *widget : std::as_const(m_topicRows)) widget->show();
    showTopic(m_model, m_topic);
}

void PitchPage::newTemplate() {
    if (!m_model) return;
    const auto current = Pitch::sheetTemplate(*m_model);
    startEditing(PitchTemplates::create(current ? current->base : Pitch::themeFromKey(m_model->pitchTheme)), false);
}

void PitchPage::editSheetTemplate() {
    if (!m_model) return;
    if (const auto current = Pitch::sheetTemplate(*m_model)) startEditing(*current, PitchTemplates::find(current->id).has_value());
}

// Saved in the library, then applied to the sheet (one history step).
void PitchPage::saveTemplate() {
    const Pitch::Template value = m_editor->result();
    QString error;
    if (!PitchTemplates::save(value, &error)) {
        ModalDialog::inform(this, tr("Error"), tr("The template could not be saved: %1").arg(error));
        return;
    }
    stopEditing();
    if (setSheetTemplate) setSheetTemplate(Pitch::toJson(value));
}

bool PitchPage::importTemplate(const QString &path) {
    QString error;
    const auto value = PitchTemplates::readFile(path, &error);
    if (!value || !PitchTemplates::save(*value, &error)) {
        ModalDialog::inform(this, tr("Error"), tr("The template could not be imported: %1").arg(error));
        return false;
    }
    if (setSheetTemplate) setSheetTemplate(Pitch::toJson(*value));
    return true;
}
