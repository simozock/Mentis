#include "mobilegantt.h"

#include "document.h"
#include "ganttnames.h"
#include "mindmapmodel.h"
#include "mobilemap.h"
#include "nodestyle.h"

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QDateTime>
#include <QLineF>
#include <QLocale>
#include <QPainter>
#include <QSettings>
#include <QStyleHints>

#include <algorithm>
#include <functional>

namespace {

const QString kNameShareKey = QStringLiteral("gantt/mobileNameShare");  // user's request 2026-11-22

// Kept while the app runs, shared by every Gantt (as the outline's zoom), 100 % at each start.
int s_zoomPercent = 100;

// The desktop's dark Gantt (GanttColors::dark) on the phone's dark tokens (Theme.qml: darkSunken,
// darkText, darkTextMuted, darkSurface, the desktop's UiTheme dark values).
GanttPalette darkPalette() {
    const QColor sunken(0x1f, 0x23, 0x26), surface(0x2b, 0x2f, 0x33);
    return {sunken, QColor(Qt::white), QColor(0x74, 0x7a, 0x80), QColor(255, 255, 255, 18), QColor(255, 255, 255, 34),
            QColor(255, 255, 255, 8), QColor(255, 255, 255, 22), QColor(Qt::white), surface, surface, QColor(255, 255, 255, 18)};
}

QFont pixelFont(int pixels, bool bold = false) {
    QFont font = QGuiApplication::font();
    font.setPixelSize(pixels);
    font.setBold(bold);
    return font;
}

}  // namespace

MobileGantt::MobileGantt(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setOpaquePainting(true);
    setAntialiasing(true);
    m_canvas.horizontalOffset = [this] { return int(std::lround(m_contentX)); };
    m_canvas.verticalOffset = [this] { return m_contentY; };
    m_canvas.textFont = QGuiApplication::font();
    m_canvas.headerFont = pixelFont(12);  // the desktop's 12 px time header
    m_canvas.accent = QColor(0x70, 0x47, 0xeb);  // Theme.accent
    m_canvas.setPrinting(true);  // no card corners: the chart fills the view
    // Stage B3: a finger needs more room than the mouse: the link circles 20 px from the bar's ends
    // (clear of the end grip), radius 6, hit within 16 px (a 32 px target, the rows' height); an
    // arrow within 12 px.
    m_canvas.reach = {20, 6, 16, 12};
    m_canvas.setZoomPercent(s_zoomPercent);
    m_canvas.setCriticalShown(QSettings().value(QStringLiteral("gantt/critical"), true).toBool());
    m_nameShare = std::clamp(QSettings().value(kNameShareKey, kDefaultNameShare).toReal(), 0.0, 1.0);
    m_canvas.updateRange();
    readColumns();
}

void MobileGantt::setMap(MobileMap *map) {
    if (m_map == map) return;
    if (m_map) disconnect(m_map, nullptr, this, nullptr);
    m_map = map;
    m_folded.clear();
    m_openPending = true;
    if (m_map) {
        // Another sheet or map: every summary open again, the opening day in view (the desktop's).
        for (auto signal : {&MobileMap::snapshotChanged, &MobileMap::opened})
            connect(m_map, signal, this, [this] { reload(true); });
        connect(m_map, &MobileMap::contentChanged, this, &MobileGantt::refresh);
        connect(m_map, &MobileMap::selectionChanged, this, &MobileGantt::updateSelection);
    }
    emit mapChanged();
    refresh();
}

void MobileGantt::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    emit activeChanged();
    refresh();
}

void MobileGantt::reload(bool opened) {
    if (opened) {
        m_folded.clear();
        m_baselineId.clear();  // another sheet: the comparison stops (FEAT-082)
        cancelRowDrag();       // and a lifted row is let go
        m_openPending = true;
        emit scrollTopRequested();
    }
    refresh();
}

void MobileGantt::refresh() {
    if (!m_active) return;
    const MindMapModel *model = m_map && m_map->document() ? m_map->document()->model() : nullptr;
    const GanttPlan plan = model ? model->gantt() : GanttPlan();  // a new plan each time: kept by value
    const GanttSchedule schedule = plan.schedule();
    m_folded.removeIf([&plan](const QString &id) { return !plan.hasChildren(id); });
    m_rows = GanttCommands::rows(plan, m_folded);
    m_nameFormats.clear();
    for (const GanttCommands::Row &row : std::as_const(m_rows))
        if (const MindNode *topic = model ? model->nodeById(row.id) : nullptr; topic && !topic->titleHtml.isEmpty())
            if (auto formats = GanttNames::ranges(topic, row.summary); !formats.isEmpty()) m_nameFormats.insert(row.id, formats);
    QStringList ids;
    ids.reserve(m_rows.size());
    for (const GanttCommands::Row &row : std::as_const(m_rows)) ids.append(row.id);
    const GanttCommands::BarLooks bars = GanttCommands::barLooks(model ? model->root() : nullptr);
    const QStringList selected = m_map ? m_map->selectedIds() : QStringList();
    m_selected = QSet<QString>(selected.cbegin(), selected.cend());
    m_dark = model && NodeStyle::isDarkBackground(model->backdrop().color);
    m_canvas.setColors(palette());
    m_canvas.setLabelled(bars.labelled);
    m_canvas.setData(plan, schedule, ids, m_selected, bars.looks);
    loadCells(plan, schedule, model);
    loadBaselines(plan, schedule);
    updateHandles();
    const int shift = m_canvas.updateRange();
    emit dataChanged();
    emit actionsChanged();
    emit layoutChanged();
    if (m_openPending && width() > 0 && !plan.tasks().isEmpty()) {  // an empty plan waits for its first task
        m_openPending = false;
        requestScroll(m_canvas.offsetOfDay(m_canvas.openingDay().addDays(-GanttCanvas::kOpenDaysBefore)));
    } else if (shift != 0) {
        requestScroll(m_contentX + shift);  // the visible dates stay where they were
    }
    update();
}

// FEAT-082: the compared snapshot, dropped when gone (removed, Undo of its creation); the comparison
// worked out once per change of the plan, not at every paint.
void MobileGantt::loadBaselines(const GanttPlan &plan, const GanttSchedule &schedule) {
    const GanttBaseline *baseline = m_baselineId.isEmpty() ? nullptr : plan.baseline(m_baselineId);
    if (!baseline) m_baselineId.clear();
    m_comparison = baseline ? plan.compare(schedule, *baseline) : GanttBaselineComparison();
    m_canvas.setBaseline(baseline ? std::optional<GanttBaseline>(*baseline) : std::nullopt, m_comparison);
    m_baselines.clear();
    const QLocale locale;
    const QVector<GanttBaseline> &all = plan.baselines();
    for (auto it = all.crbegin(); it != all.crend(); ++it)
        m_baselines.append(QVariantMap{{QStringLiteral("id"), it->id},
                                       {QStringLiteral("name"), it->name},
                                       {QStringLiteral("taken"), locale.toString(it->taken, QLocale::ShortFormat)}});
    emit baselinesChanged();
}

QString MobileGantt::comparedName() const {
    const GanttBaseline *baseline = m_baselineId.isEmpty() ? nullptr : m_canvas.plan().baseline(m_baselineId);
    return baseline ? baseline->name : QString();
}

bool MobileGantt::editBaselines(const std::function<bool(GanttPlan &)> &change) {
    Document *document = m_map ? m_map->document() : nullptr;
    return document && document->editGantt(change) && finish();
}

QString MobileGantt::addBaseline(const QString &name) {
    QString id;
    editBaselines([&](GanttPlan &plan) {
        id = plan.addBaseline(name, QDateTime::currentDateTime());
        return !id.isEmpty();
    });
    return id;
}

bool MobileGantt::renameBaseline(const QString &id, const QString &name) {
    return editBaselines([&](GanttPlan &plan) { return plan.renameBaseline(id, name); });
}

bool MobileGantt::removeBaseline(const QString &id) {
    return editBaselines([&](GanttPlan &plan) { return plan.removeBaseline(id); });  // the comparison stops with it
}

void MobileGantt::compareBaseline(const QString &id) {
    if (id == m_baselineId || (!id.isEmpty() && !m_canvas.plan().baseline(id))) return;
    m_baselineId = id;
    refresh();
}

QRectF MobileGantt::baselineMarkRect(const QString &id) const {
    const QRectF mark = m_canvas.baselineMarkRect(id);
    return mark.isEmpty() ? QRectF() : mark.translated(tableWidth(), 0);
}

bool MobileGantt::isNew(const QString &id) const {
    return !m_baselineId.isEmpty() && m_comparison.tasks.value(id) == GanttBaselineComparison::Change::New;
}

void MobileGantt::updateSelection() {
    if (!m_active || !m_map) return;
    const QStringList selected = m_map->selectedIds();
    const QSet<QString> now(selected.cbegin(), selected.cend());
    emit actionsChanged();  // also the multiple selection mode starting or stopping
    if (now == m_selected) return;
    m_selected = now;  // only the tint changes: the plan is not read again
    m_canvas.setSelected(m_selected);
    updateHandles();
    update();
}

GanttPalette MobileGantt::palette() const { return m_dark ? darkPalette() : GanttPalette::light(); }

void MobileGantt::setCriticalShown(bool shown) {
    if (shown == criticalShown()) return;
    m_canvas.setCriticalShown(shown);
    QSettings().setValue(QStringLiteral("gantt/critical"), shown);
    emit criticalShownChanged();
    update();
}

void MobileGantt::setScale(int scale) {
    scale = std::clamp(scale, int(GanttCanvas::Scale::Days), int(GanttCanvas::Scale::Months));
    if (scale == this->scale()) return;
    const QDate left = m_canvas.dateAt(0);  // the date at the chart's left edge stays in view
    m_canvas.setScale(GanttCanvas::Scale(scale));
    m_canvas.updateRange();
    emit scaleChanged();
    emit layoutChanged();
    requestScroll(m_canvas.offsetOfDay(left));
    update();
}

void MobileGantt::zoomTo(int percent, qreal anchorX) {
    percent = std::clamp(percent, GanttCanvas::kMinZoom, GanttCanvas::kMaxZoom);
    if (percent == zoomPercent()) return;
    const qreal anchor = std::max<qreal>(0, anchorX - tableWidth());
    const double day = (anchor + m_contentX) / m_canvas.dayWidth();  // the date under the fingers
    m_canvas.setZoomPercent(percent);
    s_zoomPercent = percent;
    emit zoomPercentChanged();
    emit layoutChanged();
    requestScroll(day * m_canvas.dayWidth() - anchor);
    update();
}

void MobileGantt::scrollToToday() { requestScroll(m_canvas.offsetOfDay(GanttToday::date().addDays(-GanttCanvas::kOpenDaysBefore))); }

void MobileGantt::requestScroll(qreal x) {
    const qreal most = std::max<qreal>(0, contentWidth() - width());
    emit scrollRequested(std::clamp(x, qreal(0), most));
}

void MobileGantt::setContentX(qreal x) {
    if (qFuzzyCompare(x + 1, m_contentX + 1)) return;
    m_contentX = x;
    emit contentXChanged();
    update();
}

void MobileGantt::setContentY(qreal y) {
    if (qFuzzyCompare(y + 1, m_contentY + 1)) return;
    m_contentY = y;
    emit contentYChanged();
    update();
}

qreal MobileGantt::contentWidth() const { return tableWidth() + m_canvas.contentWidth(); }

qreal MobileGantt::contentHeight() const { return kHeaderHeight + qreal(m_rows.size()) * kRowHeight; }

void MobileGantt::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size()) return;
    layoutColumns();
    emit layoutChanged();
    if (m_openPending && m_active && newGeometry.width() > 0) refresh();
}

int MobileGantt::rowOf(const QString &id) const {
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).id == id) return i;
    return -1;
}

QRectF MobileGantt::barRect(const QString &id) const {
    const QRectF bar = m_canvas.barRect(id);
    return bar.isEmpty() ? QRectF() : bar.translated(tableWidth(), 0);
}

void MobileGantt::tapAt(qreal x, qreal y) {
    if (!m_map) return;
    if (y < kHeaderHeight) {  // stage F: the table's header opens the Columns modal
        if (x < tableWidth()) emit headerTapped();
        return;
    }
    const int row = m_canvas.rowAt(y);
    if (row < 0) {  // under the last row: nothing selected, the scale group back (stage B1)
        if (m_map->multiSelect()) m_map->stopMultiSelect();
        m_map->select(QString());
        return;
    }
    // Stage B3: an arrow (not under a bar) opens its menu.
    if (x >= tableWidth()) {
        const QPointF pos = chartPoint(x, y);
        if (const int d = m_canvas.barAt(pos).isEmpty() ? m_canvas.dependencyAt(pos) : -1; d >= 0) {
            const GanttDependency &link = m_canvas.plan().dependencies().at(d);
            emit dependencyTapped({{QStringLiteral("from"), link.from}, {QStringLiteral("to"), link.to},
                                   {QStringLiteral("type"), int(link.type)}, {QStringLiteral("lag"), link.lag}}, x, y);
            return;
        }
    }
    const GanttCommands::Row &shown = m_rows.at(row);
    if (m_map->extendSelection(shown.id)) return;  // Ctrl + tap, as the desktop's Ctrl+click
    if (x >= tableWidth() && !m_map->multiSelect()) {  // the first tap of a double tap that dates the task
        m_tapId = shown.id;
        m_tapPos = QPointF(x, y);
        m_tapTime.start();
    }
    // The fold arrow's place: the Name column's start up to its text, at least the 48 px target.
    const qreal nameLeft = columnLeft(int(GanttColumns::Field::Name));
    const qreal textLeft = nameLeft + kSidePadding + shown.depth * kIndent + kArrowWidth + 4;
    if (shown.summary && x >= nameLeft && x < std::max<qreal>(textLeft, nameLeft + 48) && x < nameLeft + nameWidth()) {
        if (!m_folded.remove(shown.id)) m_folded.insert(shown.id);
        refresh();
        return;
    }
    if (m_map->multiSelect()) m_map->toggleInSelection(shown.id);  // Select more tasks (the map's FEAT-111 mode)
    else m_map->selectTopic(shown.id);
}

QString MobileGantt::addTask() {
    Document *document = m_map ? m_map->document() : nullptr;
    MindNode *created = document ? GanttCommands::addTask(*document, nullptr, false) : nullptr;
    if (!created) return {};
    const QString id = created->id;
    m_map->finishEdit(id);
    return id;
}


QList<MindNode *> MobileGantt::selectedTopics() const {
    QList<MindNode *> topics;
    Document *document = m_map ? m_map->document() : nullptr;
    if (!document) return topics;
    for (const QString &id : m_map->selectedIds())
        if (MindNode *topic = document->model()->nodeById(id); topic && m_canvas.taskOf(id)) topics.append(topic);
    return topics;
}

bool MobileGantt::finish() {
    m_map->finishEdit(m_map->selectedId());  // the map's layout joins the step, the selection stays
    return true;
}

QVariantMap MobileGantt::actions() const {
    const QList<MindNode *> topics = selectedTopics();
    const MindNode *first = topics.isEmpty() ? nullptr : topics.first();
    const GanttPlan &plan = m_canvas.plan();
    const QStringList ids = m_map ? GanttCommands::chainIds(plan, m_map->selectedIds(), m_map->selectedIds()) : QStringList();
    const bool unlinks = GanttCommands::chainUnlinks(plan, ids);
    const bool single = topics.size() == 1 && !m_map->multiSelect();
    const auto siblingBefore = [](const MindNode *topic) {
        return topic->parent && !topic->parent->children.empty() && topic->parent->children.front().get() != topic;
    };
    const bool coloured = std::any_of(topics.cbegin(), topics.cend(), [](const MindNode *topic) {
        return NodeStyle::hasManualValue(topic, StyleProperty::BranchColor);
    });
    const bool dated = std::any_of(topics.cbegin(), topics.cend(), [&plan](const MindNode *topic) {
        const GanttTask *task = plan.task(topic->id);
        return task && task->start.isValid();
    });
    QColor color;
    if (first) {
        const GanttTask *task = plan.task(first->id);
        color = task && NodeStyle::hasManualValue(first, StyleProperty::BranchColor)
                    ? task->color : GanttCanvas::defaultBarColor(plan, plan.barKind(first->id));
    }
    return {{QStringLiteral("tasks"), int(topics.size())},
            {QStringLiteral("canIndent"), single && siblingBefore(first)},
            {QStringLiteral("canOutdent"), single && first->parent && first->parent->parent},
            {QStringLiteral("canChain"), unlinks || ids.size() > 1},
            {QStringLiteral("chainUnlinks"), unlinks},
            {QStringLiteral("hasColor"), coloured},
            {QStringLiteral("nameOnBar"), first && m_canvas.showsName(first->id)},
            {QStringLiteral("canRemoveDate"), dated},
            {QStringLiteral("color"), color}};
}

QString MobileGantt::addRow(bool child) {
    Document *document = m_map ? m_map->document() : nullptr;
    if (!document) return {};
    MindNode *anchor = m_map->multiSelect() ? nullptr : document->model()->nodeById(m_map->selectedId());
    if (anchor && !anchor->parent) anchor = nullptr;  // the central topic has no row: a main topic
    if (child && !anchor) return {};
    m_map->stopMultiSelect();
    MindNode *created = GanttCommands::addTask(*document, anchor, child);
    if (!created) return {};
    if (anchor && child) m_folded.remove(anchor->id);
    const QString id = created->id;
    m_map->finishEdit(id);
    return id;
}

bool MobileGantt::indentSelected() {
    const QList<MindNode *> topics = selectedTopics();
    if (topics.size() != 1 || !GanttCommands::indent(*m_map->document(), topics.first())) return false;
    if (MindNode *parent = topics.first()->parent) m_folded.remove(parent->id);  // its new summary stays open
    return finish();
}

bool MobileGantt::outdentSelected() {
    const QList<MindNode *> topics = selectedTopics();
    return topics.size() == 1 && GanttCommands::outdent(*m_map->document(), topics.first()) && finish();
}

bool MobileGantt::chainSelected() {
    if (!m_map || !m_map->document()) return false;
    const GanttPlan current = m_map->document()->model()->gantt();  // gantt() builds a new plan: keep it alive
    const QStringList ids = GanttCommands::chainIds(current, m_map->selectedIds(), m_map->selectedIds());
    const bool unlink = GanttCommands::chainUnlinks(current, ids);
    if (!unlink && ids.size() < 2) return false;
    return m_map->document()->editGantt([&](GanttPlan &plan) { return GanttCommands::chain(plan, ids, unlink); }) && finish();
}

bool MobileGantt::setColor(const QColor &color) {
    const QList<MindNode *> topics = selectedTopics();
    if (topics.isEmpty() || !color.isValid() || !m_map->document()->model()->setStyle(topics, StyleProperty::BranchColor, color))
        return false;
    m_map->document()->markChanged();
    return finish();
}

bool MobileGantt::clearColor() {
    QList<MindNode *> topics = selectedTopics();
    topics.removeIf([](const MindNode *topic) { return !NodeStyle::hasManualValue(topic, StyleProperty::BranchColor); });
    if (topics.isEmpty() || !m_map->document()->model()->clearStyle(topics, StyleProperty::BranchColor)) return false;
    m_map->document()->markChanged();
    return finish();
}

bool MobileGantt::setNameOnBar(bool shown) {
    const QList<MindNode *> topics = selectedTopics();
    return !topics.isEmpty() && GanttCommands::setNameOnBar(*m_map->document(), topics, shown) && finish();
}

bool MobileGantt::removeDates() {
    QStringList ids;
    for (const MindNode *topic : selectedTopics()) ids.append(topic->id);
    if (ids.isEmpty()) return false;
    return m_map->document()->editGantt([&ids](GanttPlan &plan) {
        bool changed = false;
        for (const QString &id : ids) changed = plan.setStart(id, QDate()) || changed;
        return changed;
    }) && finish();
}

// ---- stage B3: the gestures on the bar with handles

namespace {
constexpr double kGrip = 12;        // the end grip's reach each side of the bar's end
constexpr double kBadgeReach = 9;   // the 14 px badge grown to a 32 px target
}  // namespace

void MobileGantt::updateHandles() {
    QString id;
    if (m_map && !m_map->multiSelect() && m_selected.size() == 1) {
        id = m_map->selectedId();
        const GanttTask *task = m_canvas.taskOf(id);
        if (!task || m_canvas.schedule().tasks.value(id).summary || m_canvas.barRect(id).isEmpty()) id.clear();
    }
    GanttCanvas::Gesture &gesture = m_canvas.gesture();
    if (gesture.drag == GanttCanvas::Drag::None) gesture.hovered = id;  // the canvas draws the circles and badge
    if (id == m_handles) return;
    m_handles = id;
    emit handlesChanged();
}

// The second press of a double tap on the chart, in the same row near the first tap, on a task the
// schedule leaves without a date (a dated predecessor dates it, a summary takes its subtasks' dates).
bool MobileGantt::startDating(qreal x, qreal y) {
    const QString tapped = m_tapId;
    m_tapId.clear();
    if (tapped.isEmpty() || !m_tapTime.isValid() || x < tableWidth()
        || m_tapTime.elapsed() > QGuiApplication::styleHints()->mouseDoubleClickInterval()) return false;
    const int row = m_canvas.rowAt(y);
    if (row < 0 || m_rows.at(row).id != tapped || std::abs(x - m_tapPos.x()) > kRowHeight) return false;
    const GanttScheduledTask scheduled = m_canvas.schedule().tasks.value(tapped);
    if (!m_canvas.taskOf(tapped) || scheduled.dated || scheduled.summary) return false;
    m_dating = tapped;
    m_datingFrom = m_datingTo = m_canvas.dateAt(chartPoint(x, y).x());
    m_datingX = x;
    update();
    return true;
}

bool MobileGantt::pressAt(qreal x, qreal y) {
    if (startDating(x, y)) return true;
    if (m_handles.isEmpty() || x < tableWidth() || !m_canvas.taskOf(m_handles)) return false;
    const QPointF pos = chartPoint(x, y);
    const QString &id = m_handles;
    const QRectF bar = m_canvas.barRect(id);
    const bool milestone = m_canvas.isMilestone(id);
    bool atStart = false;
    GanttCanvas::Drag drag = GanttCanvas::Drag::None;
    if (!m_canvas.linkHandleAt(id, pos, &atStart).isEmpty()) drag = GanttCanvas::Drag::Link;
    else if (!milestone && m_canvas.progressBadge(id).adjusted(-kBadgeReach, -kBadgeReach, kBadgeReach, kBadgeReach).contains(pos))
        drag = GanttCanvas::Drag::Progress;
    // On a short bar (Weeks, Months) only the grip's outer side, so the bar still moves (as the desktop).
    else if (!milestone && std::abs(pos.x() - bar.right()) <= kGrip && std::abs(pos.y() - bar.center().y()) <= kRowHeight / 2.0
             && (bar.width() >= 3 * kGrip || pos.x() >= bar.right()))
        drag = GanttCanvas::Drag::Resize;
    else if (bar.adjusted(-GanttCanvas::kBarInset, -GanttCanvas::kBarInset, GanttCanvas::kBarInset, GanttCanvas::kBarInset).contains(pos))
        drag = GanttCanvas::Drag::Move;
    if (drag == GanttCanvas::Drag::None) return false;
    GanttCanvas::Gesture &gesture = m_canvas.gesture();
    gesture.press = gesture.pos = pos;
    gesture.drag = drag;
    gesture.id = id;
    gesture.fromStart = atStart;
    update();
    return true;
}

void MobileGantt::dragTo(qreal x, qreal y) {
    if (!m_dating.isEmpty()) {
        // A finger shakes: under the drag distance it is still the day pressed.
        const bool moved = std::abs(x - m_datingX) >= QGuiApplication::styleHints()->startDragDistance();
        const QDate day = moved ? m_canvas.dateAt(chartPoint(x, y).x()) : m_datingFrom;
        if (day != m_datingTo) {
            m_datingTo = day;
            update();
        }
        return;
    }
    GanttCanvas::Gesture &gesture = m_canvas.gesture();
    if (gesture.drag == GanttCanvas::Drag::None) return;
    gesture.pos = chartPoint(x, y);  // drawn as a preview, the plan unchanged
    update();
}

bool MobileGantt::releaseAt(qreal x, qreal y) {
    if (!m_dating.isEmpty()) {
        dragTo(x, y);
        const QString id = m_dating;
        const QDate start = std::min(m_datingFrom, m_datingTo), end = std::max(m_datingFrom, m_datingTo);
        const bool ranged = start != end;
        cancelGesture();
        Document *document = m_map ? m_map->document() : nullptr;
        // Start and duration in one step; without a drag the task keeps its duration.
        return document && document->editGantt([&](GanttPlan &plan) {
            bool changed = plan.setStart(id, start);
            if (const GanttTask *task = plan.task(id); ranged && task && !task->milestone)
                changed = plan.setDuration(id, GanttCommands::durationTo(plan, start, end)) || changed;
            return changed;
        }) && finish();
    }
    GanttCanvas::Gesture &gesture = m_canvas.gesture();
    if (gesture.drag == GanttCanvas::Drag::None) return false;
    const QPointF pos = chartPoint(x, y);
    // A finger shakes more than a mouse: under the drag distance it was a tap.
    const bool moved = QLineF(gesture.press, pos).length() >= QGuiApplication::styleHints()->startDragDistance();
    const std::function<bool(GanttPlan &)> change = moved ? m_canvas.gestureEdit(pos) : nullptr;
    cancelGesture();
    Document *document = m_map ? m_map->document() : nullptr;
    return change && document && document->editGantt(change) && finish();
}

void MobileGantt::cancelGesture() {
    m_dating.clear();
    GanttCanvas::Gesture &gesture = m_canvas.gesture();
    gesture.drag = GanttCanvas::Drag::None;
    gesture.id.clear();
    gesture.hovered = m_handles;
    update();
}

bool MobileGantt::liftRow(qreal x, qreal y) {
    if (!m_map || m_map->multiSelect() || x < 0 || x >= tableWidth() || y < kHeaderHeight) return false;
    const int row = m_canvas.rowAt(y);
    if (row < 0) return false;
    m_lifted = m_rows.at(row).id;
    m_liftGrab = y - m_canvas.rowTop(row);
    m_dragY = y;
    m_dropId.clear();
    m_map->selectTopic(m_lifted);
    emit rowDragChanged();
    update();
    return true;
}

// The row and the ones under it deeper than it: the lifted branch, where nothing drops.
bool MobileGantt::inLiftedBranch(int row) const {
    const int lifted = rowOf(m_lifted);
    if (lifted < 0 || row < lifted) return false;
    for (int i = lifted + 1; i <= row; ++i)
        if (m_rows.at(i).depth <= m_rows.at(lifted).depth) return false;
    return true;
}

void MobileGantt::dragRowTo(qreal, qreal y) {
    if (m_lifted.isEmpty()) return;
    m_dragY = y;
    const int row = y < kHeaderHeight ? -1 : m_canvas.rowAt(y);
    const QString before = m_dropId;
    const GanttCommands::DropPlace place = m_dropPlace;
    if (row < 0 || inLiftedBranch(row)) {
        m_dropId.clear();
    } else {
        // The desktop's table: the top and bottom 30% before and after, the middle inside.
        const qreal relative = (y - m_canvas.rowTop(row)) / kRowHeight;
        m_dropId = m_rows.at(row).id;
        m_dropPlace = relative < 0.3 ? GanttCommands::DropPlace::Before
                    : relative > 0.7 ? GanttCommands::DropPlace::After : GanttCommands::DropPlace::Inside;
    }
    if (m_dropId != before || m_dropPlace != place) emit rowDragChanged();
    update();
}

bool MobileGantt::dropRow() {
    const QString id = m_lifted, target = m_dropId;
    const GanttCommands::DropPlace place = m_dropPlace;
    cancelRowDrag();
    Document *document = m_map ? m_map->document() : nullptr;
    if (!document || target.isEmpty()) return false;
    MindNode *anchor = document->model()->nodeById(target);
    if (!GanttCommands::move(*document, document->model()->nodeById(id), anchor, place)) return false;
    if (place == GanttCommands::DropPlace::Inside) m_folded.remove(target);  // its new subtask in sight
    m_map->finishEdit(id);
    return true;
}

void MobileGantt::cancelRowDrag() {
    if (m_lifted.isEmpty()) return;
    m_lifted.clear();
    m_dropId.clear();
    emit rowDragChanged();
    update();
}

bool MobileGantt::setDependency(const QString &from, const QString &to, int type, int lag) {
    Document *document = m_map ? m_map->document() : nullptr;
    if (!document || type < 0 || type > int(GanttDependency::Type::StartFinish)) return false;
    GanttDependency link;
    link.from = from;
    link.to = to;
    link.type = GanttDependency::Type(type);
    link.lag = std::clamp(lag, -GanttPlan::kMaxLag, GanttPlan::kMaxLag);
    return document->editGantt([&](GanttPlan &plan) { return plan.updateDependency(link); }) && finish();
}

bool MobileGantt::removeDependency(const QString &from, const QString &to) {
    Document *document = m_map ? m_map->document() : nullptr;
    return document && document->editGantt([&](GanttPlan &plan) { return plan.removeDependency(from, to); }) && finish();
}

QStringList MobileGantt::dependencyTypes() const {
    QStringList names;  // in GanttDependency::Type's order
    for (int type = 0; type <= int(GanttDependency::Type::StartFinish); ++type) names.append(GanttCanvas::typeName(GanttDependency::Type(type)));
    return names;
}

QPointF MobileGantt::handlePoint(const QString &which) const {
    if (m_handles.isEmpty()) return {};
    const QRectF bar = m_canvas.barRect(m_handles);
    QPointF point;
    if (which == QLatin1String("end")) point = m_canvas.linkHandle(m_handles);
    else if (which == QLatin1String("start")) point = m_canvas.linkHandleAtStart(m_handles);
    else if (which == QLatin1String("progress")) point = m_canvas.progressHandle(m_handles);
    else if (which == QLatin1String("grip")) point = QPointF(bar.right() - 4, bar.center().y());
    else point = bar.center();
    return point + QPointF(tableWidth(), 0);
}

// The end grip of the bar with handles: two short lines inside its end, as a picked picture's handle,
// so the finger finds where the duration changes.
void MobileGantt::paintGrip(QPainter *painter) {
    const GanttCanvas::Gesture &gesture = m_canvas.gesture();
    if (m_handles.isEmpty() || m_canvas.isMilestone(m_handles) || gesture.drag != GanttCanvas::Drag::None) return;
    const QRectF bar = m_canvas.barRect(m_handles).translated(tableWidth(), 0);
    if (bar.isEmpty() || bar.right() < tableWidth()) return;
    painter->save();
    painter->setClipRect(QRectF(tableWidth(), kHeaderHeight, width() - tableWidth(), height() - kHeaderHeight));
    QColor color = palette().text;
    color.setAlpha(150);
    painter->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap));
    const double x = bar.right() - 6, half = std::min(5.0, bar.height() / 2 - 3);
    for (const double dx : {0.0, -3.0}) painter->drawLine(QPointF(x + dx, bar.center().y() - half), QPointF(x + dx, bar.center().y() + half));
    painter->restore();
}

void MobileGantt::paint(QPainter *painter) {
    const GanttPalette colors = palette();
    painter->fillRect(boundingRect(), colors.card);
    const qreal table = tableWidth();
    const QRect chart(0, 0, int(std::ceil(width() - table)), int(std::ceil(height())));
    painter->save();
    painter->translate(table, 0);
    painter->setClipRect(chart);
    m_canvas.paint(*painter, chart);
    painter->restore();
    paintNames(painter);
    paintGrip(painter);
    paintDating(painter);
    paintRowDrag(painter);
}

// The days a double tap is giving the task: a bar in the accent, half transparent, from the day
// pressed to the one under the finger.
void MobileGantt::paintDating(QPainter *painter) {
    const int row = rowOf(m_dating);
    if (row < 0) return;
    const QDate start = std::min(m_datingFrom, m_datingTo), end = std::max(m_datingFrom, m_datingTo);
    const qreal left = tableWidth() + m_canvas.xOf(start), right = tableWidth() + m_canvas.xOf(end) + m_canvas.dayWidth();
    const QRectF bar(left, m_canvas.rowTop(row) + GanttCanvas::kBarInset, right - left, kRowHeight - 2 * GanttCanvas::kBarInset);
    painter->save();
    painter->setClipRect(QRectF(tableWidth(), kHeaderHeight, width() - tableWidth(), height() - kHeaderHeight));
    painter->setRenderHint(QPainter::Antialiasing);
    QColor fill = m_canvas.accent;
    fill.setAlpha(110);
    painter->setPen(QPen(m_canvas.accent, 1.5));
    painter->setBrush(fill);
    painter->drawRoundedRect(bar, 4, 4);
    painter->restore();
}

// The lifted row's drop as the desktop's table (GanttTableView::paintEvent): over the middle of a row
// a 2 px accent frame, radius 4; before or after it a 2 px accent line from the row's name, the level
// the topic lands on. The lifted row's table part at 50% follows the finger.
void MobileGantt::paintRowDrag(QPainter *painter) {
    if (m_lifted.isEmpty()) return;
    const int lifted = rowOf(m_lifted);
    if (lifted < 0) return;
    const qreal table = tableWidth();
    painter->save();
    painter->setClipRect(QRectF(0, kHeaderHeight, width(), height() - kHeaderHeight));
    painter->setRenderHint(QPainter::Antialiasing);
    const QColor accent = m_canvas.accent;
    if (const int row = rowOf(m_dropId); row >= 0) {
        const qreal top = m_canvas.rowTop(row);
        if (m_dropPlace == GanttCommands::DropPlace::Inside) {
            painter->setPen(QPen(accent, 2));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(QRectF(0, top, table, kRowHeight).adjusted(1, 1, -1, -1), 4, 4);
        } else {
            const qreal left = columnLeft(int(GanttColumns::Field::Name)) + kSidePadding + m_rows.at(row).depth * kIndent;
            const qreal y = m_dropPlace == GanttCommands::DropPlace::Before ? top : top + kRowHeight;
            painter->fillRect(QRectF(left, y - 1, table - left, 2), accent);
        }
    }
    // The copy under the finger: the row's selected tint and its name, as in the table.
    const GanttPalette colors = palette();
    const GanttCommands::Row &shown = m_rows.at(lifted);
    const QRectF copy(0, m_dragY - m_liftGrab, table, kRowHeight);
    painter->setClipRect(copy.intersected(QRectF(0, kHeaderHeight, table, height() - kHeaderHeight)));
    painter->setOpacity(0.5);
    painter->fillRect(copy, colors.card);
    painter->fillRect(copy, colors.selected);
    const qreal textLeft = columnLeft(int(GanttColumns::Field::Name)) + kSidePadding + shown.depth * kIndent + kArrowWidth + 4;
    const qreal room = columnLeft(int(GanttColumns::Field::Name)) + nameWidth() - kCellPadding - textLeft;
    if (const GanttTask *task = m_canvas.taskOf(shown.id); task && room > 0) {
        const QFont font = pixelFont(14, shown.summary);
        painter->setFont(font);
        painter->setPen(colors.text);
        GanttNames::paint(painter, QRectF(textLeft, copy.top(), room, kRowHeight), m_canvas.shownName(shown.id, *task), nameFormats(shown.id));
    }
    painter->restore();
}

void MobileGantt::setNamePreview(const QString &id, const QString &name) {
    if (m_canvas.setNamePreview(id, name)) update();
    m_namePreviewId = id;
}

QString MobileGantt::shownName(const QString &id) const {
    const GanttTask *task = m_canvas.taskOf(id);
    return task ? m_canvas.shownName(id, *task) : QString();
}

QList<QTextLayout::FormatRange> MobileGantt::nameFormats(const QString &id) const {
    return !m_namePreviewId.isEmpty() && id == m_namePreviewId ? QList<QTextLayout::FormatRange>() : m_nameFormats.value(id);
}

void MobileGantt::paintNames(QPainter *painter) {
    const GanttPalette colors = palette();
    const qreal table = tableWidth();
    const qreal nameLeft = columnLeft(int(GanttColumns::Field::Name)), nameRight = nameLeft + nameWidth();
    painter->save();
    painter->setClipRect(QRectF(0, 0, table, height()));
    painter->fillRect(QRectF(0, 0, table, height()), colors.card);
    // Rows under the header: the selected tinted as in the chart, a line under each.
    painter->save();
    painter->setClipRect(QRectF(0, kHeaderHeight, table, height() - kHeaderHeight), Qt::IntersectClip);
    const QFont regular = pixelFont(14), bold = pixelFont(14, true);
    const QFont pillFont = GanttCanvas::newPillFont(QGuiApplication::font());
    const int first = std::max(0, m_canvas.rowAt(kHeaderHeight));
    for (int row = first; row < m_rows.size(); ++row) {
        const qreal top = m_canvas.rowTop(row);
        if (top >= height()) break;
        const GanttCommands::Row &shown = m_rows.at(row);
        const QRectF line(0, top, table, kRowHeight);
        if (m_selected.contains(shown.id)) painter->fillRect(line, colors.selected);
        const qreal arrowLeft = nameLeft + kSidePadding + shown.depth * kIndent;
        const qreal textLeft = arrowLeft + kArrowWidth + 4;
        if (shown.summary) {
            painter->setFont(regular);
            painter->setPen(colors.muted);
            painter->drawText(QRectF(arrowLeft, top, kArrowWidth, kRowHeight), Qt::AlignCenter,
                              m_folded.contains(shown.id) ? QStringLiteral("▸") : QStringLiteral("▾"));
        }
        const GanttTask *task = m_canvas.taskOf(shown.id);
        const QFont &font = shown.summary ? bold : regular;
        painter->setFont(font);
        painter->setPen(colors.text);
        qreal room = nameRight - textLeft - kCellPadding;
        // FEAT-082: a task not in the compared snapshot, the "New" pill at the column's end (the desktop's Name cell).
        if (isNew(shown.id)) {
            const QSizeF pill = GanttCanvas::newPillSize(pillFont);
            room -= pill.width() + 6;
            GanttCanvas::paintNewPill(painter, QRectF(QPointF(nameRight - kCellPadding - pill.width(), top + (kRowHeight - pill.height()) / 2), pill),
                                      pillFont, m_canvas.accent);
        }
        if (task && room > 0)
            GanttNames::paint(painter, QRectF(textLeft, top, room, kRowHeight), m_canvas.shownName(shown.id, *task), nameFormats(shown.id));  // FEAT-124
        paintCells(painter, row, top);
        painter->setPen(colors.grid);
        const qreal bottom = std::floor(top + kRowHeight) - 0.5;
        painter->drawLine(QPointF(0, bottom), QPointF(table, bottom));
    }
    painter->restore();
    // The header: each column's title in its lower half, as the desktop's.
    painter->fillRect(QRectF(0, 0, table, kHeaderHeight), colors.card);
    painter->setFont(pixelFont(12));
    painter->setPen(colors.muted);
    const QFontMetricsF headerMetrics(painter->font());
    for (int i = 0; i < m_fields.size(); ++i) {
        const GanttColumns::Field field = m_fields.at(i);
        const bool name = field == GanttColumns::Field::Name;
        const qreal left = m_lefts.at(i) + (name ? kSidePadding : kCellPadding);
        const qreal right = (name ? nameRight : m_lefts.at(i) + fieldWidth(field)) - kCellPadding;
        if (right <= left) continue;
        const Qt::Alignment align = (GanttColumns::alignedRight(field) ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter;
        painter->drawText(QRectF(left, kHeaderHeight / 2.0, right - left, kHeaderHeight / 2.0), align,
                          headerMetrics.elidedText(name ? tr("Name") : GanttColumns::title(field), Qt::ElideRight, right - left));
    }
    // FEAT-082: while comparing, the upper half shows the snapshot's grey sample and its name (the
    // desktop's legend band; Later and Earlier are told in the Baselines modal: no room here).
    if (const QString compared = comparedName(); !compared.isEmpty()) {
        const qreal middle = kHeaderHeight / 4.0;
        painter->setPen(Qt::NoPen);
        painter->setBrush(GanttCanvas::baselineColor());
        painter->drawRoundedRect(QRectF(kSidePadding, middle - 2, 16, 4), 2, 2);
        const qreal left = kSidePadding + 22, room = table - left - kCellPadding;
        painter->setPen(colors.muted);
        if (room > 0)
            painter->drawText(QRectF(left, 0, room, kHeaderHeight / 2.0), Qt::AlignLeft | Qt::AlignVCenter,
                              headerMetrics.elidedText(compared, Qt::ElideRight, room));
    }
    painter->setPen(colors.headerLine);
    painter->drawLine(QPointF(0, kHeaderHeight - 0.5), QPointF(table, kHeaderHeight - 0.5));
    painter->drawLine(QPointF(table - 0.5, 0), QPointF(table - 0.5, height()));  // the table's edge against the chart
    painter->restore();
}

// ---- stage F: the shown columns

void MobileGantt::loadCells(const GanttPlan &plan, const GanttSchedule &schedule, const MindMapModel *model) {
    const bool numbered = model && GanttColumns::numbered(model->root());
    const bool numberingChanged = numbered != m_numbered;
    m_numbered = numbered;
    m_fields = m_choice;
    if (!m_numbered) m_fields.removeAll(GanttColumns::Field::Number);
    m_cells.clear();
    m_numberWidth = 0;
    if (m_fields.size() > 1 && model) {
        // The topics by id in one walk (nodeById searches the tree once per row).
        QHash<QString, const MindNode *> topics;
        const std::function<void(const MindNode *)> collect = [&](const MindNode *topic) {
            for (const auto &child : topic->children) {
                topics.insert(child->id, child.get());
                collect(child.get());
            }
        };
        collect(model->root());
        static const QVector<GanttResource> none;
        const QVector<GanttResource> &people = m_map && m_map->document() ? m_map->document()->resources().people : none;
        const QFontMetricsF metrics(pixelFont(14));
        m_cells.reserve(m_rows.size());
        for (const GanttCommands::Row &row : std::as_const(m_rows)) {
            QStringList cells;
            const GanttTask *task = plan.task(row.id);
            const GanttScheduledTask scheduled = schedule.tasks.value(row.id);
            const MindNode *topic = topics.value(row.id);
            for (const GanttColumns::Field field : std::as_const(m_fields)) {
                cells.append(task && field != GanttColumns::Field::Name ? GanttColumns::cellText(field, plan, *task, scheduled, topic, people) : QString());
                if (field == GanttColumns::Field::Number) m_numberWidth = std::max(m_numberWidth, metrics.horizontalAdvance(cells.last()));
            }
            m_cells.append(cells);
        }
    }
    if (m_fields.contains(GanttColumns::Field::Number))
        m_numberWidth = std::ceil(std::max(m_numberWidth, QFontMetricsF(pixelFont(12)).horizontalAdvance(GanttColumns::title(GanttColumns::Field::Number))))
                        + 2 * kCellPadding;
    layoutColumns();
    if (numberingChanged) readColumns();  // No.'s row enabled or not
}

void MobileGantt::readColumns() {
    m_choice = GanttColumns::shownFields(true);
    m_widths.clear();
    for (const GanttColumns::Field field : GanttColumns::movable()) m_widths.insert(field, GanttColumns::width(field));
    const auto entry = [this](GanttColumns::Field field, bool movable) {
        const bool number = field == GanttColumns::Field::Number;
        return QVariantMap{{QStringLiteral("field"), int(field)}, {QStringLiteral("title"), GanttColumns::title(field)},
                           {QStringLiteral("shown"), m_choice.contains(field) && (!number || m_numbered)},
                           {QStringLiteral("enabled"), !number || m_numbered}, {QStringLiteral("movable"), movable}};
    };
    m_columns = {entry(GanttColumns::Field::Number, false)};
    for (const GanttColumns::Field field : GanttColumns::order()) m_columns.append(entry(field, true));
    emit columnsChanged();
}

qreal MobileGantt::fieldWidth(GanttColumns::Field field) const {
    if (field == GanttColumns::Field::Name) return m_nameWidth;
    if (field == GanttColumns::Field::Number) return m_numberWidth;
    return m_widths.value(field);
}

void MobileGantt::layoutColumns() {
    qreal others = 0;
    for (const GanttColumns::Field field : std::as_const(m_fields))
        if (field != GanttColumns::Field::Name) others += fieldWidth(field);
    const qreal room = width() - kMinChart;  // what the chart leaves to the table
    m_nameWidth = std::max<qreal>(kMinName, std::round(width() * m_nameShare));
    if (m_nameWidth + others > room) m_nameWidth = std::max<qreal>(kMinName, std::round(room - others));
    m_tableWidth = std::min(m_nameWidth + others, std::max(m_nameWidth, room));
    m_lefts.clear();
    qreal left = 0;
    for (const GanttColumns::Field field : std::as_const(m_fields)) {
        m_lefts.append(left);
        left += fieldWidth(field);
    }
}

void MobileGantt::paintCells(QPainter *painter, int row, qreal top) {
    if (row >= m_cells.size()) return;
    const GanttPalette colors = palette();
    const QStringList &cells = m_cells.at(row);
    const QFont font = pixelFont(14);
    const QFontMetricsF metrics(font);
    painter->setFont(font);
    for (int i = 0; i < m_fields.size() && i < cells.size(); ++i) {
        const GanttColumns::Field field = m_fields.at(i);
        if (field == GanttColumns::Field::Name || cells.at(i).isEmpty()) continue;
        const qreal left = m_lefts.at(i) + kCellPadding, room = fieldWidth(field) - 2 * kCellPadding;
        if (room <= 0 || left >= tableWidth()) continue;
        painter->setPen(field == GanttColumns::Field::Number ? colors.muted : colors.text);  // the map's numbers grey, as the desktop's
        const Qt::Alignment align = (GanttColumns::alignedRight(field) ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter;
        painter->drawText(QRectF(left, top, room, kRowHeight), align, metrics.elidedText(cells.at(i), Qt::ElideRight, room));
    }
}

QVariantList MobileGantt::columns() const { return m_columns; }

void MobileGantt::setColumnShown(int field, bool shown) {
    if (field < int(GanttColumns::Field::Number) || field > int(GanttColumns::Field::People) || field == int(GanttColumns::Field::Name)) return;
    if (field == int(GanttColumns::Field::Number) && !m_numbered) return;  // the number exists only on a numbered map
    if (m_choice.contains(GanttColumns::Field(field)) == shown) return;
    GanttColumns::setShown(GanttColumns::Field(field), shown);
    readColumns();
    refresh();
}

void MobileGantt::moveColumn(int from, int to) {
    QVector<GanttColumns::Field> order = GanttColumns::order();
    if (from < 0 || from >= order.size() || to < 0 || to >= order.size() || from == to) return;
    order.move(from, to);
    GanttColumns::setOrder(order);
    readColumns();
    refresh();
}

QVariantList MobileGantt::shownFields() const {
    QVariantList fields;
    for (const GanttColumns::Field field : m_fields) fields.append(int(field));
    return fields;
}

QString MobileGantt::cellText(int row, int field) const {
    const int column = m_fields.indexOf(GanttColumns::Field(field));
    return row >= 0 && row < m_cells.size() && column >= 0 ? m_cells.at(row).value(column) : QString();
}

qreal MobileGantt::nameRight() const { return columnLeft(int(GanttColumns::Field::Name)) + m_nameWidth; }

void MobileGantt::setNameWidth(qreal nameWidth, bool save) {
    if (width() <= 0) return;
    qreal others = 0;
    for (const GanttColumns::Field field : std::as_const(m_fields))
        if (field != GanttColumns::Field::Name) others += fieldWidth(field);
    // Never past the room the chart and the other columns leave (layoutColumns would narrow it back).
    const qreal widest = std::max<qreal>(kMinName, width() - kMinChart - others);
    m_nameShare = std::clamp<qreal>(nameWidth, kMinName, widest) / width();
    if (save) QSettings().setValue(kNameShareKey, m_nameShare);
    layoutColumns();
    emit layoutChanged();
    update();
}

void MobileGantt::setColumnWidth(int field, qreal columnWidth, bool save) {
    const auto column = GanttColumns::Field(field);
    if (width() <= 0 || !m_widths.contains(column) || !m_fields.contains(column)) return;
    qreal others = 0;
    for (const GanttColumns::Field shown : std::as_const(m_fields))
        if (shown != GanttColumns::Field::Name && shown != column) others += fieldWidth(shown);
    // Never past what the chart's 48 px and Name's 96 px leave, so the column's grip stays on screen.
    const qreal widest = std::max<qreal>(GanttColumns::kMinWidth, width() - kMinChart - kMinName - others);
    const qreal next = std::round(std::clamp<qreal>(columnWidth, GanttColumns::kMinWidth, widest));
    m_widths.insert(column, next);
    if (save) GanttColumns::setWidth(column, int(next));
    layoutColumns();
    emit layoutChanged();
    update();
}

qreal MobileGantt::columnWidth(int field) const {
    const auto column = GanttColumns::Field(field);
    return m_fields.contains(column) ? fieldWidth(column) : 0;
}

qreal MobileGantt::columnLeft(int field) const {
    const int column = m_fields.indexOf(GanttColumns::Field(field));
    return column >= 0 && column < m_lefts.size() ? m_lefts.at(column) : 0;
}

QPointF MobileGantt::arrowPoint(int dependency) const {
    const QPolygonF path = m_canvas.arrowPath(dependency);
    if (path.size() < 2) return {};
    int longest = 1;  // the middle of its longest segment, away from the bars
    for (int i = 2; i < path.size(); ++i)
        if (QLineF(path.at(i - 1), path.at(i)).length() > QLineF(path.at(longest - 1), path.at(longest)).length()) longest = i;
    return (path.at(longest - 1) + path.at(longest)) / 2 + QPointF(tableWidth(), 0);
}
