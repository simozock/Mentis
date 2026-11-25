#include "mobileganttstyle.h"

#include "document.h"
#include "ganttcanvas.h"
#include "ganttplan.h"
#include "ganttstylechoices.h"
#include "mindmapmodel.h"
#include "mobilemap.h"
#include "mobilestyle.h"

#include <algorithm>
#include <functional>

namespace {

bool validList(int list) { return list >= MobileGanttStyle::BarShape && list <= MobileGanttStyle::LinkEndSize; }
bool validKind(int kind) { return kind >= 0 && kind < GanttPlan::kBarKinds; }

// A value of the list's choices (an enum read from QML is any int).
bool listed(int list, int value) {
    const QList<StyleChoices::Choice> choices = GanttStyleChoices::choices(GanttStyleChoices::List(list));
    return std::any_of(choices.cbegin(), choices.cend(), [value](const StyleChoices::Choice &choice) { return choice.value == value; });
}

}  // namespace

MobileGanttStyle::MobileGanttStyle(QObject *parent) : QObject(parent) {}

void MobileGanttStyle::setMap(MobileMap *map) {
    if (m_map == map) return;
    if (m_map) disconnect(m_map, nullptr, this, nullptr);
    m_map = map;
    if (m_map)
        for (auto signal : {&MobileMap::contentChanged, &MobileMap::snapshotChanged, &MobileMap::opened})
            connect(m_map, signal, this, &MobileGanttStyle::reload);
    emit mapChanged();
    reload();
}

void MobileGanttStyle::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    emit activeChanged();
    reload();
}

void MobileGanttStyle::reload() {
    if (!m_active) return;  // read again when the page is shown
    m_values.clear();
    Document *document = m_map && m_map->isOpen() ? m_map->document() : nullptr;
    if (!document) {
        emit changed();
        return;
    }
    const GanttPlan plan = document->model()->gantt();  // a copy: gantt() is rebuilt each time (BUG-025)
    QVariantList fills, borders, colors, chosen, borderColors, borderChosen;
    for (int kind = 0; kind < GanttPlan::kBarKinds; ++kind) {
        fills.append(int(plan.fillStyle(GanttPlan::BarKind(kind))));
        borders.append(int(plan.borderStyle(GanttPlan::BarKind(kind))));
        colors.append(GanttCanvas::defaultBarColor(plan, GanttPlan::BarKind(kind)));
        chosen.append(plan.defaultColor(GanttPlan::BarKind(kind)).isValid());
        borderColors.append(GanttCanvas::defaultOutlineColor(plan, GanttPlan::BarKind(kind)));
        borderChosen.append(plan.defaultBorderColor(GanttPlan::BarKind(kind)).isValid());
    }
    m_values = {{QStringLiteral("barShape"), int(plan.barShape())},
                {QStringLiteral("summaryShape"), int(plan.summaryShape())},
                {QStringLiteral("milestoneShape"), int(plan.milestoneShape())},
                {QStringLiteral("fill"), fills},
                {QStringLiteral("border"), borders},
                {QStringLiteral("color"), colors},
                {QStringLiteral("colorChosen"), chosen},
                {QStringLiteral("borderColor"), borderColors},
                {QStringLiteral("borderColorChosen"), borderChosen},
                {QStringLiteral("linkLine"), int(plan.linkLine())},
                {QStringLiteral("linkEnd"), int(plan.linkEnd())},
                {QStringLiteral("linkWidth"), int(plan.linkWidth())},
                {QStringLiteral("linkEndSize"), int(plan.linkEndSize())},
                {QStringLiteral("linkColor"), GanttCanvas::linkColor(plan)},
                {QStringLiteral("linkColorChosen"), plan.linkColor().isValid()}};
    emit changed();
}

QVariantList MobileGanttStyle::choices(int list) {
    QVariantList result;
    if (!validList(list)) return result;
    for (const StyleChoices::Choice &choice : GanttStyleChoices::choices(GanttStyleChoices::List(list)))
        result.append(QVariantMap{{QStringLiteral("value"), choice.value}, {QStringLiteral("name"), choice.name}, {QStringLiteral("group"), QString()}});
    return result;
}

int MobileGanttStyle::iconKind(int list) { return validList(list) ? MobileStyle::GanttIcons + list : -1; }

bool MobileGanttStyle::choose(int list, int value, int kind) {
    Document *document = m_map && m_map->isOpen() ? m_map->document() : nullptr;
    if (!document || !validList(list) || !validKind(kind) || !listed(list, value)) return false;
    const auto bars = GanttPlan::BarKind(kind);
    std::function<bool(GanttPlan &)> change;
    switch (List(list)) {
    case BarShape: change = [value](GanttPlan &plan) { return plan.setBarShape(GanttPlan::BarShape(value)); }; break;
    case SummaryShape: change = [value](GanttPlan &plan) { return plan.setSummaryShape(GanttPlan::SummaryShape(value)); }; break;
    case MilestoneShape: change = [value](GanttPlan &plan) { return plan.setMilestoneShape(GanttPlan::MilestoneShape(value)); }; break;
    case Fill: change = [value, bars](GanttPlan &plan) { return plan.setFillStyle(bars, NodeFillStyle(value)); }; break;
    case Border: change = [value, bars](GanttPlan &plan) { return plan.setBorderStyle(bars, GanttPlan::BorderStyle(value)); }; break;
    case LinkLine: change = [value](GanttPlan &plan) { return plan.setLinkLine(GanttPlan::LinkLine(value)); }; break;
    case LinkEnd: change = [value](GanttPlan &plan) { return plan.setLinkEnd(BranchEnd(value)); }; break;
    case LinkWidth: change = [value](GanttPlan &plan) { return plan.setLinkWidth(GanttPlan::LinkWidth(value)); }; break;
    case LinkEndSize: change = [value](GanttPlan &plan) { return plan.setLinkEndSize(GanttPlan::LinkEndSize(value)); }; break;
    }
    if (!document->editGantt(change)) return false;
    m_map->finishEdit(m_map->selectedId());
    return true;
}

bool MobileGanttStyle::editSheet(const std::function<bool(GanttPlan &)> &change) {
    Document *document = m_map && m_map->isOpen() ? m_map->document() : nullptr;
    if (!document || !document->editGantt(change)) return false;
    m_map->finishEdit(m_map->selectedId());
    return true;
}

// The desktop's fields have no opacity.
bool MobileGanttStyle::setColor(int kind, const QColor &color) {
    if (!validKind(kind) || !color.isValid()) return false;
    return editSheet([&](GanttPlan &plan) { return plan.setDefaultColor(GanttPlan::BarKind(kind), QColor(color.rgb())); });
}

bool MobileGanttStyle::clearColor(int kind) {
    return validKind(kind) && editSheet([&](GanttPlan &plan) { return plan.setDefaultColor(GanttPlan::BarKind(kind), QColor()); });
}

bool MobileGanttStyle::setBorderColor(int kind, const QColor &color) {
    if (!validKind(kind) || !color.isValid()) return false;
    return editSheet([&](GanttPlan &plan) { return plan.setDefaultBorderColor(GanttPlan::BarKind(kind), QColor(color.rgb())); });
}

bool MobileGanttStyle::clearBorderColor(int kind) {
    return validKind(kind) && editSheet([&](GanttPlan &plan) { return plan.setDefaultBorderColor(GanttPlan::BarKind(kind), QColor()); });
}

bool MobileGanttStyle::setLinkColor(const QColor &color) {
    if (!color.isValid()) return false;
    return editSheet([&](GanttPlan &plan) { return plan.setLinkColor(QColor(color.rgb())); });
}

bool MobileGanttStyle::clearLinkColor() {
    return editSheet([](GanttPlan &plan) { return plan.setLinkColor(QColor()); });
}
