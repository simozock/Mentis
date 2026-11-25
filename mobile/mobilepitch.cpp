#include "mobilepitch.h"

#include "document.h"
#include "mindmapmodel.h"
#include "mobilemap.h"
#include "performance.h"
#include "pitchchoices.h"
#include "pitchtemplate.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QUuid>
#include <QVariantAnimation>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

#include <algorithm>

MobilePitch::MobilePitch(QObject *parent) : QObject(parent) {}

void MobilePitch::setMap(MobileMap *map) {
    if (m_map == map) return;
    if (m_map) disconnect(m_map, nullptr, this, nullptr);
    m_map = map;
    m_editing = false;
    if (m_map) {
        // Another sheet or map shown: the draft is dropped (PitchPage::cancelEditing).
        for (auto signal : {&MobileMap::snapshotChanged, &MobileMap::opened})
            connect(m_map, signal, this, [this] { m_editing = false; });
        for (auto signal : {&MobileMap::selectionChanged, &MobileMap::contentChanged, &MobileMap::snapshotChanged, &MobileMap::opened})
            connect(m_map, signal, this, &MobilePitch::bump);
    }
    emit mapChanged();
    bump();
}

void MobilePitch::bump() {
    ++m_revision;
    emit changed();
}

MindMapModel *MobilePitch::model() const { return m_map && m_map->isOpen() ? m_map->document()->model() : nullptr; }

MindNode *MobilePitch::topic() const {
    // MainWindow::pitchTopic: none selected = the central topic; several = none.
    MindMapModel *model = this->model();
    if (!model) return nullptr;
    const QStringList selected = m_map->selectedIds();
    if (selected.isEmpty()) return model->root();
    return selected.size() == 1 ? model->nodeById(selected.front()) : nullptr;
}

QVariantMap MobilePitch::state() const {
    const MindMapModel *model = this->model();
    if (!model) return {{QStringLiteral("open"), false}};
    const bool hasTemplate = !model->pitchTemplate.isEmpty();
    QVariantMap state{{QStringLiteral("open"), true},
                      {QStringLiteral("aspect"), int(Pitch::aspectFromKey(model->pitchAspect))},
                      {QStringLiteral("theme"), hasTemplate ? -1 : int(Pitch::themeFromKey(model->pitchTheme))},
                      {QStringLiteral("hasTemplate"), hasTemplate}};
    if (const auto value = Pitch::sheetTemplate(*model)) state.insert(QStringLiteral("templateId"), value->id);
    const MindNode *node = topic();
    state.insert(QStringLiteral("topic"), node != nullptr);
    if (node) {
        state.insert(QStringLiteral("topicSlide"), int(Pitch::visibilityFromKey(node->pitchTopicSlide)));
        state.insert(QStringLiteral("listSlide"), int(Pitch::visibilityFromKey(node->pitchListSlide)));
        state.insert(QStringLiteral("delivery"), int(Pitch::deliveryFromKey(node->pitchDelivery)));
        state.insert(QStringLiteral("layout"), int(Pitch::layoutFromKey(node->pitchLayout)));
        state.insert(QStringLiteral("topicSlideNames"), PitchChoices::visibilityNames(Pitch::autoTopicSlide(node)));
        state.insert(QStringLiteral("listSlideNames"), PitchChoices::visibilityNames(Pitch::autoListSlide(node)));
    } else {
        // Several topics: the rows are disabled and show their names without a value.
        state.insert(QStringLiteral("topicSlideNames"), PitchChoices::visibilityNames(false));
        state.insert(QStringLiteral("listSlideNames"), PitchChoices::visibilityNames(false));
    }
    return state;
}

QStringList MobilePitch::aspectNames() const { return PitchChoices::aspectNames(); }
QStringList MobilePitch::deliveryNames() const { return PitchChoices::deliveryNames(); }
QStringList MobilePitch::layoutNames() const { return PitchChoices::layoutNames(); }

QVariantList MobilePitch::themes() const {
    QVariantList list;
    for (const Pitch::Theme theme : PitchChoices::themeOrder())
        list.append(QVariantMap{{QStringLiteral("theme"), int(theme)},
                                {QStringLiteral("name"), PitchChoices::themeName(theme)},
                                {QStringLiteral("illustrated"), int(theme) >= Pitch::kBasicThemeCount}});
    return list;
}

bool MobilePitch::chooseTopic(int field, int index) {
    MindNode *node = topic();
    if (!node || index < 0) return false;
    QString value;
    switch (field) {
    case TopicSlide:
    case ListSlide:
        if (index > int(Pitch::Visibility::Hidden)) return false;
        value = Pitch::visibilityKey(static_cast<Pitch::Visibility>(index));
        break;
    case Delivery:
        if (index > int(Pitch::Delivery::All)) return false;
        value = Pitch::deliveryKey(static_cast<Pitch::Delivery>(index));
        break;
    case Layout:
        if (index >= Pitch::kLayoutCount) return false;
        value = Pitch::layoutKey(static_cast<Pitch::Layout>(index));
        break;
    default: return false;
    }
    if (!m_map->document()->setPitchTopic(node, static_cast<Document::PitchField>(field), value)) return false;
    bump();
    return true;
}

bool MobilePitch::chooseAspect(int index) {
    MindMapModel *model = this->model();
    if (!model || index < 0 || index > int(Pitch::Aspect::TallClassic)) return false;
    if (!m_map->document()->setPitchSheet(model->pitchTheme, Pitch::aspectKey(static_cast<Pitch::Aspect>(index)))) return false;
    bump();
    return true;
}

bool MobilePitch::chooseTheme(int theme) {
    MindMapModel *model = this->model();
    if (!model || theme < 0 || theme >= Pitch::kThemeCount) return false;
    if (!m_map->document()->setPitchSheet(Pitch::themeKey(static_cast<Pitch::Theme>(theme)), model->pitchAspect, QJsonObject()))
        return false;
    bump();
    return true;
}

// Stage B: templates (FEAT-089 as the desktop's PitchPage and PitchTemplateEditor).

namespace {
QString pageText(const char *text) { return QCoreApplication::translate("PitchPage", text); }
QString editorText(const char *text) { return QCoreApplication::translate("PitchTemplateEditor", text); }
QVariantList named(const QStringList &names) {
    QVariantList list;
    for (int i = 0; i < names.size(); ++i) list.append(QVariantMap{{QStringLiteral("value"), i}, {QStringLiteral("name"), names[i]}});
    return list;
}
}  // namespace

QVariantList MobilePitch::templates() {
    const MindMapModel *model = this->model();
    m_cards = model ? PitchChoices::templateCards(*model) : QList<PitchChoices::TemplateCard>();
    QVariantList list;
    for (const PitchChoices::TemplateCard &card : std::as_const(m_cards))
        list.append(QVariantMap{{QStringLiteral("id"), card.value.id}, {QStringLiteral("name"), card.name},
                                {QStringLiteral("onlyHere"), card.onlyHere}});
    return list;
}

bool MobilePitch::applyTemplate(const Pitch::Template &value) {
    MindMapModel *model = this->model();
    if (!model) return false;
    if (!m_map->document()->setPitchSheet(Pitch::themeKey(value.base), model->pitchAspect, Pitch::toJson(value))) return false;
    bump();
    return true;
}

bool MobilePitch::chooseTemplate(const QString &id) {
    // The map's own copy stays; a library template is copied again (it may have changed).
    for (const PitchChoices::TemplateCard &card : std::as_const(m_cards))
        if (card.value.id == id) return !card.onlyHere && applyTemplate(card.value);
    return false;
}

void MobilePitch::startEditing(const Pitch::Template &value, bool inLibrary) {
    m_draft = value;
    m_savedName = value.name;
    m_inLibrary = inLibrary;
    m_editing = true;
    bump();
}

bool MobilePitch::newTemplate() {
    const MindMapModel *model = this->model();
    if (!model) return false;
    const auto current = Pitch::sheetTemplate(*model);
    startEditing(PitchTemplates::create(current ? current->base : Pitch::themeFromKey(model->pitchTheme)), false);
    return true;
}

bool MobilePitch::editTemplate() {
    const MindMapModel *model = this->model();
    const auto current = model ? Pitch::sheetTemplate(*model) : std::nullopt;
    if (!current) return false;
    startEditing(*current, PitchTemplates::find(current->id).has_value());
    return true;
}

QVariantMap MobilePitch::draft() const {
    const MindMapModel *model = this->model();
    if (!m_editing || !model) return {};
    const Pitch::Look base = Pitch::look(*model, m_draft.base);
    const QStringList colorNames = PitchChoices::templateColorNames();
    QVariantList colors;
    for (int i = 0; i < PitchChoices::kTemplateColorCount; ++i) {
        const QColor own = PitchChoices::templateColor(m_draft, i);
        colors.append(QVariantMap{{QStringLiteral("name"), colorNames[i]},
                                  {QStringLiteral("value"), own.isValid() ? own : PitchChoices::baseTemplateColor(base, i)},
                                  {QStringLiteral("own"), own.isValid()}});
    }
    const QStringList pictureNames = PitchChoices::templatePictureNames();
    QVariantList pictures;
    for (int i = 0; i < PitchChoices::kTemplatePictureCount; ++i)
        pictures.append(QVariantMap{{QStringLiteral("name"), pictureNames[i]},
                                    {QStringLiteral("chosen"), !PitchChoices::templatePicture(m_draft, i).isEmpty()}});
    return {{QStringLiteral("name"), m_draft.name},
            {QStringLiteral("base"), int(m_draft.base)},
            {QStringLiteral("colors"), colors},
            {QStringLiteral("titleFont"), m_draft.titleFamily},
            {QStringLiteral("itemFont"), m_draft.itemFamily},
            {QStringLiteral("itemStyle"), m_draft.itemStyle ? int(*m_draft.itemStyle) + 1 : 0},
            {QStringLiteral("motif"), m_draft.motif ? int(*m_draft.motif) + 1 : 0},
            {QStringLiteral("pictures"), pictures},
            {QStringLiteral("logoCorner"), int(m_draft.logoCorner)},
            {QStringLiteral("logoSize"), m_draft.logoSize / PitchChoices::kLogoSizeStep * PitchChoices::kLogoSizeStep},
            {QStringLiteral("footer"), m_draft.footer},
            {QStringLiteral("numbers"), m_draft.numbers},
            {QStringLiteral("inLibrary"), m_inLibrary}};
}

QVariantList MobilePitch::draftChoices(int field) const {
    switch (field) {
    case Base: {
        // The desktop's select: the themes in Pitch::Theme's order.
        QStringList names;
        for (int i = 0; i < Pitch::kThemeCount; ++i) names.append(PitchChoices::themeName(Pitch::Theme(i)));
        return named(names);
    }
    case TitleFont:
    case ItemFont: {
        // "Of the theme" (empty), then the fonts; one of the draft's not installed here by its name.
        QVariantList list{QVariantMap{{QStringLiteral("value"), QString()}, {QStringLiteral("name"), editorText("Of the theme")}}};
        const QStringList families = QFontDatabase::families();
        for (const QString &family : families)
            if (!family.startsWith(QLatin1Char('@')))  // "@": vertical fonts
                list.append(QVariantMap{{QStringLiteral("value"), family}, {QStringLiteral("name"), family}});
        const QString own = field == TitleFont ? m_draft.titleFamily : m_draft.itemFamily;
        if (!own.isEmpty() && !families.contains(own))
            list.append(QVariantMap{{QStringLiteral("value"), own}, {QStringLiteral("name"), editorText("%1 (not installed)").arg(own)}});
        return list;
    }
    case ItemStyle: return named(PitchChoices::itemStyleNames());
    case Motif: return named(PitchChoices::motifNames());
    case LogoCorner: return named(PitchChoices::cornerNames());
    case LogoSize: {
        QVariantList list;
        for (const int size : PitchChoices::logoSizes())
            list.append(QVariantMap{{QStringLiteral("value"), size}, {QStringLiteral("name"), QStringLiteral("%1%").arg(size)}});
        return list;
    }
    default: return {};
    }
}

bool MobilePitch::setDraft(int field, const QVariant &value) {
    if (!m_editing) return false;
    const Pitch::Template before = m_draft;
    const int index = value.toInt();
    switch (field) {
    case Name: m_draft.name = value.toString().trimmed(); break;  // empty: the old name comes back on Save
    case Base:
        if (index < 0 || index >= Pitch::kThemeCount) return false;
        m_draft.base = Pitch::Theme(index);
        break;
    case TitleFont: m_draft.titleFamily = value.toString(); break;
    case ItemFont: m_draft.itemFamily = value.toString(); break;
    case ItemStyle:
        if (index < 0 || index > Pitch::kItemStyleCount) return false;
        m_draft.itemStyle = index > 0 ? std::optional(Pitch::ItemStyle(index - 1)) : std::nullopt;
        break;
    case Motif:
        if (index < 0 || index > Pitch::kMotifCount) return false;
        m_draft.motif = index > 0 ? std::optional(Pitch::Motif(index - 1)) : std::nullopt;
        break;
    case LogoCorner:
        if (index < 0 || index > int(Pitch::Corner::BottomRight)) return false;
        m_draft.logoCorner = Pitch::Corner(index);
        break;
    case LogoSize:
        if (!PitchChoices::logoSizes().contains(index)) return false;
        m_draft.logoSize = index;
        break;
    case Footer: m_draft.footer = value.toString(); break;
    case Numbers: m_draft.numbers = value.toBool(); break;
    default: return false;
    }
    if (m_draft == before) return false;
    bump();
    return true;
}

bool MobilePitch::setDraftColor(int index, const QColor &color) {
    if (!m_editing || index < 0 || index >= PitchChoices::kTemplateColorCount || !color.isValid()) return false;
    PitchChoices::templateColor(m_draft, index) = color.toRgb();
    bump();
    return true;
}

void MobilePitch::resetDraftColors() {
    if (!m_editing) return;
    for (int i = 0; i < PitchChoices::kTemplateColorCount; ++i) PitchChoices::templateColor(m_draft, i) = QColor();
    bump();
}

QString MobilePitch::pickedCopy(const QUrl &url, QString *error) const {
    QString name;
    return m_map ? m_map->copyPicked(url, &name, error) : QString();
}

bool MobilePitch::setDraftPicture(int index, const QUrl &url) {
    if (!m_editing || !m_map || index < 0 || index >= PitchChoices::kTemplatePictureCount) return false;
    QString error;
    const QString copy = pickedCopy(url, &error);
    const bool logo = PitchChoices::templatePictureIsLogo(index);
    const QByteArray data = copy.isEmpty() ? QByteArray()
                                           : Pitch::loadPicture(copy, logo ? Pitch::kLogoMaxSide : Pitch::kPictureMaxSide, logo, &error);
    if (!copy.isEmpty()) QDir(QFileInfo(copy).absolutePath()).removeRecursively();
    if (data.isEmpty()) {
        m_map->setError(editorText("The picture could not be read: %1").arg(error));
        return false;
    }
    PitchChoices::templatePicture(m_draft, index) = data;
    bump();
    return true;
}

void MobilePitch::removeDraftPicture(int index) {
    if (!m_editing || index < 0 || index >= PitchChoices::kTemplatePictureCount) return;
    PitchChoices::templatePicture(m_draft, index) = QByteArray();
    bump();
}

Pitch::Template MobilePitch::draftResult() const {
    Pitch::Template value = m_draft;
    if (value.name.isEmpty()) value.name = m_savedName;
    return value;
}

bool MobilePitch::saveDraft() {
    if (!m_editing || !m_map) return false;
    const Pitch::Template value = draftResult();
    QString error;
    if (!PitchTemplates::save(value, &error)) {
        m_map->setError(pageText("The template could not be saved: %1").arg(error));
        return false;
    }
    m_editing = false;
    if (!applyTemplate(value)) bump();  // the sheet had this copy already: the page still closes the editor
    return true;
}

void MobilePitch::cancelDraft() {
    if (!m_editing) return;
    m_editing = false;
    bump();
}

bool MobilePitch::deleteDraft() {
    if (!m_editing || !m_inLibrary || !m_map) return false;
    // The maps that use it keep their copy.
    if (!PitchTemplates::remove(m_draft.id)) {
        m_map->setError(pageText("The template could not be deleted."));
        return false;
    }
    cancelDraft();
    return true;
}

bool MobilePitch::exportDraft() {
    if (!m_editing || !m_map) return false;
    const Pitch::Template value = draftResult();
    const QString name = PitchChoices::templateFileName(value.name);
    // In the folder Android shares from (MobileMap::openedFilesFolder), a folder of its own.
    const QString folder = QDir(m_map->openedFilesFolder()).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString path = QDir(folder).filePath(name);
    QString error;
    if (!QDir().mkpath(folder) || !PitchTemplates::writeFile(value, path, &error)) {
        m_map->setError(editorText("The template could not be exported: %1").arg(error));
        return false;
    }
    return m_map->shareCopy(path, name);
}

bool MobilePitch::importTemplate(const QUrl &url) {
    if (!m_map || !model()) return false;
    QString error;
    const QString copy = pickedCopy(url, &error);
    const auto value = copy.isEmpty() ? std::nullopt : PitchTemplates::readFile(copy, &error);
    if (!copy.isEmpty()) QDir(QFileInfo(copy).absolutePath()).removeRecursively();
    if (!value || !PitchTemplates::save(*value, &error)) {
        m_map->setError(pageText("The template could not be imported: %1").arg(error));
        return false;
    }
    applyTemplate(*value);
    return true;
}

Pitch::Slide MobilePitch::previewSlide() const {
    const MindMapModel *model = this->model();
    if (!model) return {};
    Pitch::Slide slide = Pitch::previewSlide(*model, topic());
    if (slide.number == 0) slide.number = 1;  // FEAT-089: the preview shows where the number goes
    return slide;
}

Pitch::Look MobilePitch::sheetLook() const {
    const MindMapModel *model = this->model();
    return model ? Pitch::sheetLook(*model) : Pitch::Look{};
}

Pitch::Look MobilePitch::previewLook() const {
    const MindMapModel *model = this->model();
    if (!model) return {};
    return m_editing ? Pitch::look(*model, m_draft) : Pitch::sheetLook(*model);
}

Pitch::Look MobilePitch::templateLook(const QString &id) const {
    const MindMapModel *model = this->model();
    if (!model) return {};
    for (const PitchChoices::TemplateCard &card : m_cards)
        if (card.value.id == id) return Pitch::look(*model, card.value);
    return Pitch::sheetLook(*model);
}

Pitch::Look MobilePitch::themeLook(Pitch::Theme theme) const {
    const MindMapModel *model = this->model();
    return model ? Pitch::look(*model, theme) : Pitch::Look{};
}

Pitch::Aspect MobilePitch::aspect() const {
    const MindMapModel *model = this->model();
    return model ? Pitch::aspectFromKey(model->pitchAspect) : Pitch::Aspect::Auto;
}

// The preview and the theme cards.

PitchSlideView::PitchSlideView(QQuickItem *parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }

void PitchSlideView::setPitch(MobilePitch *pitch) {
    if (m_pitch == pitch) return;
    if (m_pitch) disconnect(m_pitch, nullptr, this, nullptr);
    m_pitch = pitch;
    if (m_pitch) connect(m_pitch, &MobilePitch::changed, this, &PitchSlideView::refresh);
    emit pitchChanged();
    refresh();
}

void PitchSlideView::setTheme(int theme) {
    if (m_theme == theme) return;
    m_theme = theme;
    emit pitchChanged();
    refresh();
}

void PitchSlideView::setTemplateId(const QString &id) {
    if (m_templateId == id) return;
    m_templateId = id;
    emit pitchChanged();
    refresh();
}

void PitchSlideView::refresh() {
    // The preview: the sheet's ratio, Auto as the desktop's preview (16:9).
    const qreal ratio = m_theme < 0 && m_templateId.isEmpty() && m_pitch ? Pitch::aspectRatio(m_pitch->aspect(), 16.0 / 9.0) : 16.0 / 9.0;
    if (!qFuzzyCompare(ratio, m_ratio)) {
        m_ratio = ratio;
        emit ratioChanged();
    }
    update();
}

void PitchSlideView::paint(QPainter *painter) {
    if (!m_pitch || !m_pitch->model()) return;
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF bounds(0, 0, width(), height());
    if (!m_templateId.isEmpty()) {  // a Templates card shows its cover
        PitchChoices::drawThemeCard(painter, bounds, m_pitch->templateLook(m_templateId), true);
        return;
    }
    if (m_theme >= 0) {
        const auto theme = static_cast<Pitch::Theme>(std::clamp(m_theme, 0, Pitch::kThemeCount - 1));
        PitchChoices::drawThemeCard(painter, bounds, m_pitch->themeLook(theme), m_theme >= Pitch::kBasicThemeCount);
        return;
    }
    // PitchPreview::paintEvent: rounded 8, the look's ground, the slide fitted in the middle.
    constexpr qreal kRadius = 8;
    QPainterPath clip;
    clip.addRoundedRect(bounds, kRadius, kRadius);
    painter->setClipPath(clip);
    const Pitch::Look look = m_pitch->previewLook();
    painter->fillRect(bounds, look.background.isValid() ? look.background : QColor(Qt::black));
    const QSizeF slide = QSizeF(m_ratio, 1).scaled(bounds.size(), Qt::KeepAspectRatio);
    Pitch::paint(painter, QRectF(QPointF((width() - slide.width()) / 2, (height() - slide.height()) / 2), slide),
                 m_pitch->previewSlide(), look);
}

// The layout icons.

PitchLayoutIcon::PitchLayoutIcon(QQuickItem *parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }

void PitchLayoutIcon::setLayout(int layout) {
    if (m_layout == layout) return;
    m_layout = layout;
    emit changed();
    update();
}

void PitchLayoutIcon::setColor(const QColor &color) {
    if (m_color == color) return;
    m_color = color;
    emit changed();
    update();
}

void PitchLayoutIcon::paint(QPainter *painter) {
    const qreal scale = std::min(width(), height()) / 20;
    painter->translate((width() - 20 * scale) / 2, (height() - 20 * scale) / 2);
    painter->scale(scale, scale);
    PitchChoices::drawLayoutIcon(painter, static_cast<Pitch::Layout>(std::clamp(m_layout, 0, Pitch::kLayoutCount - 1)), m_color);
}

// The presentation.

PitchPresentation::PitchPresentation(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setOpaquePainting(true);
    setAntialiasing(true);
    m_animation = new QVariantAnimation(this);
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(1.0);
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        if (!m_before.isNull()) m_slideFade = value.toReal();
        else m_itemFade = value.toReal();
        update();
    });
    connect(m_animation, &QVariantAnimation::finished, this, [this] {
        m_before = QImage();
        m_slideFade = m_itemFade = 1;
        update();
    });
}

bool PitchPresentation::start(MobilePitch *pitch) {
    MindMapModel *model = pitch ? pitch->model() : nullptr;
    if (!model) return false;
    const QList<Pitch::Slide> slides = Pitch::slides(*model);
    if (slides.isEmpty()) {
        pitch->map()->setMessage(QCoreApplication::translate("MainWindow", "No slides: every slide of this sheet is hidden"));
        return false;
    }
    m_pitch = pitch;
    m_slides = slides;
    m_look = Pitch::sheetLook(*model);
    m_aspect = Pitch::aspectFromKey(model->pitchAspect);
    m_animation->stop();
    m_before = QImage();
    m_slide = m_shown = 0;
    m_itemFade = m_slideFade = 1;
    m_running = true;
    presentOnAndroid(true);
    emit runningChanged();
    emit stepChanged();
    update();
    return true;
}

QRectF PitchPresentation::slideRect() const {
    const qreal ratio = Pitch::aspectRatio(m_aspect, height() > 0 ? width() / height() : 0);
    const QSizeF size = QSizeF(ratio, 1).scaled(QSizeF(width(), height()), Qt::KeepAspectRatio);
    return {QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size};
}

void PitchPresentation::paintSlide(QPainter *painter, qreal newest) const {
    const QRectF bounds(0, 0, width(), height());
    if (m_slides.isEmpty()) {
        painter->fillRect(bounds, m_look.background.isValid() ? m_look.background : QColor(Qt::black));
        return;
    }
    // PitchWindow::paintSlide: the bands in the slide's ground; a slide without steps shows all its items.
    const Pitch::Slide &slide = m_slides[m_slide];
    painter->fillRect(bounds, Pitch::lookOn(m_look, slide).background);
    Pitch::paint(painter, slideRect(), slide, m_look, slide.steps() > 0 ? m_shown : -1, newest);
}

void PitchPresentation::paint(QPainter *painter) {
    painter->setRenderHint(QPainter::Antialiasing);
    paintSlide(painter, m_itemFade);
    if (!m_before.isNull()) {
        painter->setOpacity(1 - m_slideFade);
        painter->drawImage(QRectF(0, 0, width(), height()), m_before);
    }
}

void PitchPresentation::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    // A rotation: the slide takes the new shape at the same step; a fade under way ends.
    if (newGeometry.size() != oldGeometry.size()) {
        m_animation->stop();
        m_before = QImage();
        m_slideFade = m_itemFade = 1;
        update();
    }
}

void PitchPresentation::keyPressEvent(QKeyEvent *event) {
    // PitchWindow::keyPressEvent's keys; Esc is left to the popup, which ends the presentation.
    switch (event->key()) {
    case Qt::Key_Right: case Qt::Key_Down: case Qt::Key_Space: case Qt::Key_Return: case Qt::Key_Enter: case Qt::Key_PageDown:
        next();
        return;
    case Qt::Key_Left: case Qt::Key_Up: case Qt::Key_Backspace: case Qt::Key_PageUp:
        previous();
        return;
    case Qt::Key_Home:
        if (m_running && !m_slides.isEmpty()) goTo(0, false);
        return;
    case Qt::Key_End:
        if (m_running && !m_slides.isEmpty()) goTo(int(m_slides.size()) - 1, true);
        return;
    default: QQuickPaintedItem::keyPressEvent(event);
    }
}

void PitchPresentation::startItemFade() {
    if (!Performance::animationsEnabled() || !isVisible()) { update(); return; }
    m_animation->stop();
    m_before = QImage();
    m_itemFade = 0;
    m_animation->setDuration(kItemFadeMs);
    m_animation->start();
}

void PitchPresentation::next() {
    if (!m_running || m_slides.isEmpty()) return;
    if (m_shown < m_slides[m_slide].steps()) {
        ++m_shown;
        emit stepChanged();
        startItemFade();
        return;
    }
    if (m_slide + 1 < m_slides.size()) goTo(m_slide + 1, false);
}

void PitchPresentation::previous() {
    if (!m_running || m_slides.isEmpty()) return;
    if (m_shown > 0) {
        --m_shown;
        m_itemFade = 1;
        emit stepChanged();
        update();
        return;
    }
    if (m_slide > 0) goTo(m_slide - 1, true);
}

void PitchPresentation::goTo(int slide, bool allItems) {
    slide = std::clamp(slide, 0, int(m_slides.size()) - 1);
    m_animation->stop();
    m_before = QImage();
    const bool fade = isVisible() && Performance::animationsEnabled() && width() > 0 && height() > 0;
    if (fade) {
        // What is on screen now, faded out over the new slide (PitchWindow::goTo's grab).
        const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1;
        m_before = QImage((QSizeF(width(), height()) * ratio).toSize(), QImage::Format_ARGB32_Premultiplied);
        m_before.setDevicePixelRatio(ratio);
        QPainter painter(&m_before);
        painter.setRenderHint(QPainter::Antialiasing);
        paintSlide(&painter, m_itemFade);
    }
    m_slide = slide;
    m_shown = allItems ? m_slides[slide].steps() : 0;
    m_itemFade = 1;
    m_slideFade = fade ? 0 : 1;
    if (fade) {
        m_animation->setDuration(kSlideFadeMs);
        m_animation->start();
    }
    emit stepChanged();
    update();
}

void PitchPresentation::finish() {
    if (!m_running) return;
    m_animation->stop();
    m_before = QImage();
    const QString topic = m_slides.isEmpty() ? QString() : m_slides[m_slide].topicId;
    m_running = false;
    m_slides.clear();
    presentOnAndroid(false);
    // PitchWindow's Esc: the map selects the topic of the last slide shown.
    if (m_pitch && m_pitch->map() && !topic.isEmpty()) m_pitch->map()->select(topic);
    emit runningChanged();
    emit stepChanged();
    emit finished();
}

void PitchPresentation::presentOnAndroid(bool on) {
#ifdef Q_OS_ANDROID
    // Set on Android's main thread, as the activity's window and orientation must be.
    (void)QNativeInterface::QAndroidApplication::runOnAndroidMainThread([on] {
        // WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
        // (https://developer.android.com/reference/android/view/WindowManager.LayoutParams#FLAG_KEEP_SCREEN_ON).
        constexpr jint kKeepScreenOn = 0x00000080;
        // ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE: landscape, either way up by the sensor
        // (https://developer.android.com/reference/android/content/pm/ActivityInfo#SCREEN_ORIENTATION_SENSOR_LANDSCAPE).
        constexpr jint kSensorLandscape = 6;
        // The orientation the app asked before the presentation, given back at its end.
        static jint previousOrientation = -1;  // SCREEN_ORIENTATION_UNSPECIFIED
        QJniObject activity(QNativeInterface::QAndroidApplication::context().object());
        const QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
        if (window.isValid()) {
            if (on) window.callMethod<void>("addFlags", "(I)V", kKeepScreenOn);
            else window.callMethod<void>("clearFlags", "(I)V", kKeepScreenOn);
        }
        if (on) {
            previousOrientation = activity.callMethod<jint>("getRequestedOrientation", "()I");
            activity.callMethod<void>("setRequestedOrientation", "(I)V", kSensorLandscape);
        } else {
            activity.callMethod<void>("setRequestedOrientation", "(I)V", previousOrientation);
        }
    });
#else
    Q_UNUSED(on);
#endif
}
