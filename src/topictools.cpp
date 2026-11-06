#include "topictools.h"

#include "floatingui.h"
#include "mapstructure.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QGraphicsTextItem>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>

namespace {
constexpr int kToolSizePx = 28;  // buttons of the bar (the reference bar is about 29 px high)
constexpr int kToolIconPx = 16;

// Blue circle with a white "+", in the selection colour of the map.
class PlusKnob final : public QAbstractButton {
public:
    explicit PlusKnob(QWidget *parent) : QAbstractButton(parent) {
        setFixedSize(TopicTools::kKnobSizePx, TopicTools::kKnobSizePx);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor fill = NodeItem::selectionStyle().selectedColor;
        if (underMouse()) fill = fill.darker(115);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawEllipse(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
        painter.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap));
        const QPointF c = QRectF(rect()).center();
        constexpr qreal arm = 4;
        painter.drawLine(c - QPointF(arm, 0), c + QPointF(arm, 0));
        painter.drawLine(c - QPointF(0, arm), c + QPointF(0, arm));
    }
};
}  // namespace

TopicTools::TopicTools(MindMapView *view, MindMapScene *scene, QObject *parent)
    : QObject(parent), m_view(view), m_scene(scene) {
    QWidget *viewport = view->viewport();
    m_bar = new FloatingGroup(viewport);
    m_bar->setObjectName(QStringLiteral("TopicToolbar"));
    addTool(QStringLiteral(":/assets/hugeicons/relation.svg"), tr("Create a relationship between two topics"), &relation);
    addTool(QStringLiteral(":/assets/hugeicons/link-web.svg"), tr("Insert a web link in the topic"), &webLink);
    addTool(QStringLiteral(":/assets/hugeicons/image.svg"), tr("Insert an image in the topic"), &image);
    addTool(QStringLiteral(":/assets/hugeicons/note.svg"), tr("Add a note to the topic"), &note);
    addTool(QStringLiteral(":/assets/hugeicons/checkbox.svg"), tr("Add or remove the topic's checkbox"), &checkbox);
    addTool(QStringLiteral(":/assets/hugeicons/sticker.svg"), tr("Add a marker or a sticker to the topic"), &sticker);
    m_formatPainter = addTool(QStringLiteral(":/assets/hugeicons/format-painter.svg"), tr("Format painter (double click: keep it on)"), nullptr);
    m_formatPainter->setObjectName(QStringLiteral("FormatPainter"));
    m_formatPainter->setCheckable(true);
    connect(m_formatPainter, &QToolButton::clicked, this, [this](bool checked) {
        const QList<MindNode *> selected = m_scene->selectedNodes();
        if (checked && selected.size() == 1 && formatPainter) formatPainter(selected.front());
        else if (!checked && stopFormatPainter) stopFormatPainter();
        else setFormatPainterOn(false);
    });
    // The double click comes in place of the second press: no second clicked(), the painter stays on.
    m_formatPainter->installEventFilter(this);
    m_bar->adjustSize();
    m_bar->hide();

    m_childKnob = new PlusKnob(viewport);
    m_childKnob->setObjectName(QStringLiteral("AddChildKnob"));
    m_childKnob->setToolTip(tr("Add a subtopic"));
    m_siblingKnob = new PlusKnob(viewport);
    m_siblingKnob->setObjectName(QStringLiteral("AddSiblingKnob"));
    m_siblingKnob->setToolTip(tr("Add a topic after"));
    connect(m_childKnob, &QAbstractButton::clicked, this, [this] { if (addChild) addChild(); });
    connect(m_siblingKnob, &QAbstractButton::clicked, this, [this] { if (addSibling) addSibling(); });
    m_childKnob->hide();
    m_siblingKnob->hide();

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(0);
    connect(m_timer, &QTimer::timeout, this, &TopicTools::update);
    connect(scene, &QGraphicsScene::selectionChanged, this, &TopicTools::schedule);
    connect(scene, &QGraphicsScene::changed, this, &TopicTools::schedule);
    connect(scene, &QGraphicsScene::focusItemChanged, this, &TopicTools::schedule);
    connect(view, &MindMapView::zoomChanged, this, &TopicTools::schedule);
    connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, this, &TopicTools::schedule);
    connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this, &TopicTools::schedule);
    viewport->installEventFilter(this);  // resize and mouse release (the bar hides while dragging)
}

QWidget *TopicTools::bar() const { return m_bar; }
QAbstractButton *TopicTools::childKnob() const { return m_childKnob; }
QAbstractButton *TopicTools::siblingKnob() const { return m_siblingKnob; }
QAbstractButton *TopicTools::formatPainterButton() const { return m_formatPainter; }

void TopicTools::setFormatPainterOn(bool on) {
    const QSignalBlocker blocker(m_formatPainter);
    m_formatPainter->setChecked(on);
}

void TopicTools::schedule() { m_timer->start(); }

QToolButton *TopicTools::addTool(const QString &icon, const QString &tip, std::function<void(MindNode *)> *run) {
    auto *button = new QToolButton(m_bar);
    FloatingUi::setupIconButton(button);
    button->setIcon(QIcon(icon));
    button->setIconSize(QSize(kToolIconPx, kToolIconPx));
    button->setFixedSize(kToolSizePx, kToolSizePx);
    button->setToolTip(tip);
    if (run)  // nullptr: the caller connects the button
        connect(button, &QToolButton::clicked, this, [this, run] {
            const QList<MindNode *> selected = m_scene->selectedNodes();
            if (selected.size() == 1 && *run) (*run)(selected.front());
        });
    m_bar->addWidget(button);
    FloatingUi::retint(button);
    return button;
}

bool TopicTools::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_formatPainter && event->type() == QEvent::MouseButtonDblClick) {
        if (m_formatPainter->isChecked() && keepFormatPainter) keepFormatPainter();
        return true;
    }
    if (event->type() == QEvent::Resize || event->type() == QEvent::MouseButtonRelease) schedule();
    return QObject::eventFilter(watched, event);
}

void TopicTools::update() {
    const QList<MindNode *> selected = m_scene->selectedNodes();
    MindNode *node = selected.size() == 1 ? selected.front() : nullptr;
    // A title being edited keeps the bar hidden also while its format bar's font or size list has the
    // focus (user's report 2026-10-25: both bars and the list showed at once).
    const bool editing = qgraphicsitem_cast<QGraphicsTextItem *>(m_scene->focusItem()) != nullptr
                      || (node && m_scene->isEditing(node));
    const bool dragging = QApplication::mouseButtons() != Qt::NoButton;
    const QRectF sceneRect = node ? m_scene->nodeOutlineSceneRect(node) : QRectF();  // FEAT-028: around a table cell
    const bool show = node && !editing && !dragging && !sceneRect.isEmpty() && (!enabled || enabled());
    if (!show) {
        m_bar->hide();
        m_childKnob->hide();
        m_siblingKnob->hide();
        return;
    }
    const QRect box = m_view->mapFromScene(sceneRect).boundingRect();
    // Bar: centred, its visible frame kBarGapPx above the topic (below it if there is no room).
    m_bar->adjustSize();
    const int frameHeight = m_bar->height() - FloatingGroup::kShadowTop - FloatingGroup::kShadowBottom;
    int top = box.top() - kBarGapPx - frameHeight - FloatingGroup::kShadowTop;
    if (top < 0) top = box.bottom() + kSiblingKnobOffsetPx + kKnobSizePx / 2 + kBarGapPx - FloatingGroup::kShadowTop;
    m_bar->move(box.center().x() - m_bar->width() / 2, top);
    m_bar->show();
    m_bar->raise();
    // Knobs: child on the branch side (under the topic in org and tree charts), sibling under it
    // (beside it in those charts).
    const bool block = MapStructure::isBlockType(MapStructure::typeOf(node));
    const bool left = node->parent && m_scene->sideOf(node) < 0;
    QPoint side(left ? box.left() - kChildKnobOffsetPx : box.right() + kChildKnobOffsetPx, box.center().y());
    QPoint below(box.center().x(), box.bottom() + kSiblingKnobOffsetPx);
    const QPoint half(kKnobSizePx / 2, kKnobSizePx / 2);
    // Not over a collapse control (beside the topic, or under it in org and tree charts): just past
    // it, away from the topic.
    for (const int collapseSide : {-1, 1}) {
        const QRectF collapse = m_scene->collapseSceneRect(node, collapseSide);
        if (collapse.isEmpty()) continue;
        const QRect circle = m_view->mapFromScene(collapse).boundingRect();
        const int clear = kCollapseKnobGapPx + kKnobSizePx / 2;
        const QRect zone = circle.adjusted(-kCollapseKnobGapPx, -kCollapseKnobGapPx, kCollapseKnobGapPx, kCollapseKnobGapPx);
        if (zone.intersects(QRect(side - half, QSize(kKnobSizePx, kKnobSizePx))))
            side.setX(left ? circle.left() - clear : circle.right() + clear);
        if (zone.intersects(QRect(below - half, QSize(kKnobSizePx, kKnobSizePx)))) below.setY(circle.bottom() + clear);
    }
    const QPoint childCentre = block ? below : side;
    const QPoint siblingCentre = block ? side : below;
    m_childKnob->move(childCentre - half);
    m_childKnob->show();
    m_childKnob->raise();
    const bool sibling = node->parent && !node->floating && !node->summaryTopic;  // FEAT-052: a summary topic has no row
    m_siblingKnob->setVisible(sibling);
    if (sibling) { m_siblingKnob->move(siblingCentre - half); m_siblingKnob->raise(); }
}
