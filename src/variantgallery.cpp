#include "variantgallery.h"
#include "help.h"

#include "mapvariants.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "structurepicker.h"
#include "stylecontrols.h"
#include "uitheme.h"

#include <QHash>
#include <functional>
#include <QLabel>
#include <QListWidget>
#include <QGridLayout>
#include <QGuiApplication>
#include <QPainterPath>
#include <QScreen>
#include <QScrollBar>
#include <QToolButton>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

// Mappa d'esempio: centrale, quattro rami principali e due sotto-argomenti nel primo.
QPixmap renderSample(const MapVariants::Variant &variant, bool mono) {
    MindMapModel model;
    model.clear(VariantGallery::tr("Central Topic"));
    if (mono) model.activePalette = QStringList(6, QStringLiteral("#8a939e"));
    MapVariants::apply(model, variant.id, true);
    MindNode *first = model.root()->children.front().get();
    for (int j = 1; j <= 2; ++j) model.addChild(first, MindMapModel::defaultTitle(first, j));
    // Senza testo (spazi): in miniatura non si leggerebbe, conta la forma, e i nodi piccoli lasciano
    // alla mappa tutta la miniatura.
    const std::function<void(MindNode *)> blank = [&blank](MindNode *node) {
        node->title = QStringLiteral(" ");
        for (const auto &child : node->children) blank(child.get());
    };
    blank(model.root());
    model.refreshStyles();
    MindMapScene scene(&model);
    scene.autoLayout();
    const QRectF source = scene.itemsBoundingRect().adjusted(-16, -16, 16, 16);
    // Disegnata al doppio per gli schermi ad alta densita'.
    const qreal ratio = 2;
    QPixmap pixmap(QSize(VariantGallery::kThumbnailWidthPx, VariantGallery::kThumbnailHeightPx) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::white);
    QPainter painter(&pixmap);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    // Centrata nella miniatura, con le proporzioni della mappa.
    const QSizeF fitted = source.size().scaled(QSizeF(VariantGallery::kThumbnailWidthPx, VariantGallery::kThumbnailHeightPx), Qt::KeepAspectRatio);
    QRectF target(QPointF(), fitted);
    target.moveCenter(QPointF(VariantGallery::kThumbnailWidthPx / 2.0, VariantGallery::kThumbnailHeightPx / 2.0));
    scene.setBackgroundBrush(Qt::white);
    scene.render(&painter, target, source, Qt::IgnoreAspectRatio);
    return pixmap;
}

}  // namespace

QPixmap VariantGallery::thumbnail(const QString &variantId, bool mono) {
    static QHash<QString, QPixmap> cache;
    const QString key = variantId + (mono ? QStringLiteral("/mono") : QString());
    if (const auto it = cache.constFind(key); it != cache.cend()) return *it;
    const MapVariants::Variant *variant = MapVariants::find(variantId);
    if (!variant) variant = &MapVariants::all().first();
    return *cache.insert(key, renderSample(*variant, mono));
}

VariantGallery::VariantGallery(QWidget *parent, const QString &current, const QString &acceptText)
    : ModalDialog(parent, tr("Choose a map type"), acceptText, 760) {
    Help::setPage(this, QStringLiteral("map/map-types.md"));  // FEAT-092: F1 and "?" open it
    setObjectName(QStringLiteral("VariantGallery"));
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumHeight(460);
    scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));
    auto *content = new QWidget;
    content->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 8, 0);
    layout->setSpacing(8);
    const QString listStyle = UiTheme::qss(QStringLiteral(
        "QListWidget{background:transparent;border:none;outline:none;}"
        "QListWidget::item{color:@text;font-size:12px;background:transparent;border:2px solid transparent;border-radius:8px;"
        "margin:4px;padding:4px;}"
        "QListWidget::item:hover{border-color:@divider;}"
        "QListWidget::item:selected{border-color:@text;background:transparent;color:@text;}"));
    // "Choose a template" (FEAT-072 F4): nothing chosen at first, Create disabled.
    m_chosen = MapVariants::find(current) ? current : QString();
    acceptButton()->setEnabled(!m_chosen.isEmpty());
    for (MapStructure::Type type : MapVariants::galleryTypes()) {
        auto *title = new QLabel(StructurePicker::typeName(type));
        title->setStyleSheet(UiTheme::qss(QStringLiteral("color:@text;font-size:13px;font-weight:600;padding-top:8px;")));
        layout->addWidget(title);
        auto *list = new QListWidget;
        list->setObjectName(QStringLiteral("VariantList-%1").arg(MapStructure::typeKey(type)));
        list->setViewMode(QListView::IconMode);
        list->setIconSize(kShownThumbnailPx);
        list->setMovement(QListView::Static);
        list->setResizeMode(QListView::Adjust);
        list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setStyleSheet(listStyle);
        const QList<const MapVariants::Variant *> variants = MapVariants::ofType(type);
        for (int i = 0; i < variants.size(); ++i) {
            // Stessa immagine anche da scelta: la scelta la dice il bordo, senza la velatura di Qt.
            QIcon icon(thumbnail(variants[i]->id, false));
            icon.addPixmap(thumbnail(variants[i]->id, false), QIcon::Selected);
            // Name under the thumbnail.
            const QString name = QStringLiteral("%1 %2").arg(StructurePicker::typeName(type)).arg(i + 1);
            auto *item = new QListWidgetItem(icon, name);
            item->setData(Qt::UserRole, variants[i]->id);
            item->setToolTip(name);
            item->setSizeHint(kShownThumbnailPx + QSize(20, 40));
            list->addItem(item);
            if (variants[i]->id == m_chosen) list->setCurrentItem(item);
        }
        // Four per row: the height holds every row (the gallery scrolls, not the list).
        const int rows = (variants.size() + 3) / 4;
        list->setFixedHeight(rows * (kShownThumbnailPx.height() + 48) + 4);
        connect(list, &QListWidget::itemClicked, this, [this, list](QListWidgetItem *item) {
            choose(list, item->data(Qt::UserRole).toString());
        });
        connect(list, &QListWidget::itemDoubleClicked, this, [this, list](QListWidgetItem *item) {
            choose(list, item->data(Qt::UserRole).toString());
            accept();
        });
        m_lists.append(list);
        layout->addWidget(list);
    }
    layout->addStretch();
    scroll->setWidget(content);
    contentLayout()->addWidget(scroll);
}

void VariantGallery::choose(QListWidget *list, const QString &id) {
    m_chosen = id;
    acceptButton()->setEnabled(true);
    // Una sola scelta in tutta la galleria.
    for (QListWidget *other : std::as_const(m_lists))
        if (other != list) other->clearSelection(), other->setCurrentItem(nullptr);
}

namespace {
// Popup sizes (FEAT-072 B16; the reference shows about 78x46 thumbnails in three columns). Surface, border,
// radius and padding are those of our other dark popups (AGENTS.md, "Selettore colori").
constexpr QSize kPopupThumbPx(78, 50);
constexpr int kPopupPaddingPx = 12;
constexpr int kPopupGapPx = 8;
constexpr int kPopupMaxHeightPx = 560;
constexpr QSize kCardThumbPx(77, 46);
constexpr int kCardHeightPx = 62;

QPixmap scaledThumbnail(const QString &id, QSize size) {
    const qreal ratio = qApp->devicePixelRatio();
    QPixmap pixmap = VariantGallery::thumbnail(id, true).scaled(size * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(ratio);
    return pixmap;
}

QString groupTitle(const QString &name, bool open) { return (open ? QStringLiteral("▾ ") : QStringLiteral("▸ ")) + name; }

class VariantPopup final : public QWidget {
public:
    VariantPopup(MapTypeSelect *owner, const QString &current)
        : QWidget(owner, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
        setObjectName(QStringLiteral("MapTypePopup"));
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(1, 1, 1, 1);
        auto *scroll = new QScrollArea(this);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; }"));
        auto *content = new QWidget;
        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(kPopupPaddingPx, kPopupPaddingPx, kPopupPaddingPx, kPopupPaddingPx);
        layout->setSpacing(kPopupGapPx);
        const QString tileStyle = UiTheme::qss(QStringLiteral(
            "QToolButton { background:#ffffff; border:2px solid transparent; border-radius:6px; padding:0; }"
            "QToolButton:hover { border-color:@divider; } QToolButton:checked { border-color:@text; }"));
        const QString headerStyle = UiTheme::qss(QStringLiteral(
            "QToolButton { color:@text; font-size:12px; font-weight:600; border:0; background:transparent; padding:0; text-align:left; }"));
        for (MapStructure::Type type : MapVariants::galleryTypes()) {
            const QString name = StructurePicker::typeName(type);
            auto *header = new QToolButton(content);
            header->setCheckable(true);
            header->setChecked(true);
            header->setStyleSheet(headerStyle);
            header->setText(groupTitle(name, true));
            auto *grid = new QWidget(content);
            auto *gridLayout = new QGridLayout(grid);
            gridLayout->setContentsMargins(0, 0, 0, 0);
            gridLayout->setSpacing(kPopupGapPx);
            const QList<const MapVariants::Variant *> variants = MapVariants::ofType(type);
            for (int i = 0; i < variants.size(); ++i) {
                const QString id = variants[i]->id;
                auto *tile = new QToolButton(grid);
                tile->setObjectName(QStringLiteral("MapTypeTile-%1").arg(id));
                tile->setCheckable(true);
                tile->setChecked(id == current);
                tile->setCursor(Qt::PointingHandCursor);
                tile->setFixedSize(kPopupThumbPx + QSize(4, 4));
                tile->setIconSize(kPopupThumbPx);
                tile->setIcon(QIcon(scaledThumbnail(id, kPopupThumbPx)));
                tile->setToolTip(QStringLiteral("%1 %2").arg(name).arg(i + 1));
                tile->setStyleSheet(tileStyle);
                connect(tile, &QToolButton::clicked, this, [this, owner, id] {
                    close();
                    emit owner->variantChosen(id);
                });
                gridLayout->addWidget(tile, i / 3, i % 3);
            }
            connect(header, &QToolButton::toggled, grid, [header, grid, name](bool open) {
                header->setText(groupTitle(name, open));
                grid->setVisible(open);
            });
            layout->addWidget(header);
            layout->addWidget(grid);
        }
        layout->addStretch();
        scroll->setWidget(content);
        outer->addWidget(scroll);
        const int width = 2 * kPopupPaddingPx + 3 * (kPopupThumbPx.width() + 4) + 2 * kPopupGapPx
                          + scroll->verticalScrollBar()->sizeHint().width() + 2;
        setFixedSize(width, std::min(kPopupMaxHeightPx, content->sizeHint().height() + 2));
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(UiTheme::color(UiTheme::Token::Divider), 1));
        painter.setBrush(UiTheme::color(UiTheme::Token::Surface));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    }
};
}  // namespace

MapTypeSelect::MapTypeSelect(QWidget *parent) : QToolButton(parent) {
    setObjectName(QStringLiteral("MapTypeSelect"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_Hover);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kCardHeightPx);
    setToolTip(tr("Change the map type"));
    setAccessibleName(tr("Map type"));
    connect(this, &QToolButton::clicked, this, &MapTypeSelect::openPopup);
}

QSize MapTypeSelect::sizeHint() const { return {kCardThumbPx.width() + 120, kCardHeightPx}; }

void MapTypeSelect::setVariant(const QString &variantId) {
    m_variant = MapVariants::find(variantId) ? variantId : MapVariants::all().first().id;
    update();
}

QWidget *MapTypeSelect::openPopup() {
    auto *popup = new VariantPopup(this, m_variant);
    // On the left of the panel, top aligned with the card, inside the screen.
    const QPoint topLeft = mapToGlobal(QPoint(0, 0));
    QPoint position(topLeft.x() - kPopupGapPx - popup->width(), topLeft.y());
    if (QScreen *screen = QGuiApplication::screenAt(mapToGlobal(rect().center()))) {
        const QRect area = screen->availableGeometry();
        position.setX(std::max(area.left(), position.x()));
        position.setY(std::clamp(position.y(), area.top(), std::max(area.top(), area.bottom() - popup->height())));
    }
    popup->move(position);
    popup->show();
    return popup;
}

void MapTypeSelect::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(hasFocus() ? UiTheme::color(UiTheme::Token::Accent) : UiTheme::color(UiTheme::Token::Divider), 1));
    painter.setBrush(UiTheme::color(underMouse() ? UiTheme::Token::Divider : UiTheme::Token::Surface));
    painter.drawRoundedRect(card, 8, 8);
    const QRectF thumb(8, (height() - kCardThumbPx.height()) / 2.0, kCardThumbPx.width(), kCardThumbPx.height());
    QPainterPath clip;
    clip.addRoundedRect(thumb, 4, 4);
    painter.fillPath(clip, Qt::white);
    painter.save();
    painter.setClipPath(clip);
    const QPixmap pixmap = scaledThumbnail(m_variant, kCardThumbPx);
    const QSizeF size = pixmap.deviceIndependentSize();
    painter.drawPixmap(QPointF(thumb.center().x() - size.width() / 2, thumb.center().y() - size.height() / 2), pixmap);
    painter.restore();
    const MapVariants::Variant *variant = MapVariants::find(m_variant);
    QFont font = painter.font();
    font.setPixelSize(UiTheme::fontPx(13));
    painter.setFont(font);
    painter.setPen(UiTheme::color(UiTheme::Token::Text));
    const qreal textLeft = thumb.right() + 12;
    const QRectF textBox(textLeft, 0, width() - textLeft - 32, height());
    const QString name = variant ? StructurePicker::typeName(variant->type) : QString();
    painter.drawText(textBox, Qt::AlignVCenter | Qt::AlignLeft, painter.fontMetrics().elidedText(name, Qt::ElideRight, int(textBox.width())));
    painter.end();
    paintDarkSelectChevron(this);
}
