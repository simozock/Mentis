#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

class GanttPlan;

class MobileMap;

// FEAT-122 stage C2: the side panel's Gantt page, as the desktop's GanttTaskPanel Gantt page
// (FEAT-080): the sheet's shapes, fills, borders and colours of tasks, summaries and milestones, and
// the look of the relations. It reads the plan only while the page is shown (`active`); every choice
// is one Undo step (Document::editGantt, then MobileMap::finishEdit). Choices and pictures are the
// core's (GanttStyleChoices).
//
// `values`: barShape, summaryShape, milestoneShape, fill (by BarKind), border (by BarKind), color (the
// colour the bars show, by BarKind), colorChosen (the sheet has its own, by BarKind), borderColor and
// borderColorChosen (the same for the border, user 2026-11-22), linkLine, linkEnd, linkWidth, linkEndSize,
// linkColor (the arrows' colour) and linkColorChosen (the sheet has its own).
class MobileGanttStyle : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MobileMap *map READ map WRITE setMap NOTIFY mapChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)

public:
    // GanttStyleChoices::List, for QML.
    enum List { BarShape, SummaryShape, MilestoneShape, Fill, Border, LinkLine, LinkEnd, LinkWidth, LinkEndSize };
    Q_ENUM(List)

    explicit MobileGanttStyle(QObject *parent = nullptr);

    MobileMap *map() const { return m_map; }
    void setMap(MobileMap *map);
    bool active() const { return m_active; }
    void setActive(bool active);
    QVariantMap values() const { return m_values; }

    // [{value, name, group}] of a list, in the desktop's order.
    Q_INVOKABLE static QVariantList choices(int list);
    // The StyleIcon kind of a list's pictures (MobileStyle::IconKind).
    Q_INVOKABLE static int iconKind(int list);
    // A list's choice for the whole sheet; kind (GanttPlan::BarKind) for Fill and Border.
    Q_INVOKABLE bool choose(int list, int value, int kind = 0);
    // The sheet's colour of a kind of bars; clearColor goes back to the default.
    Q_INVOKABLE bool setColor(int kind, const QColor &color);
    Q_INVOKABLE bool clearColor(int kind);
    // The sheet's border colour of a kind of bars; clearBorderColor goes back to automatic.
    Q_INVOKABLE bool setBorderColor(int kind, const QColor &color);
    Q_INVOKABLE bool clearBorderColor(int kind);
    // The relations' colour; clearLinkColor goes back to the view's grey.
    Q_INVOKABLE bool setLinkColor(const QColor &color);
    Q_INVOKABLE bool clearLinkColor();

signals:
    void mapChanged();
    void activeChanged();
    void changed();

private:
    void reload();
    bool editSheet(const std::function<bool(GanttPlan &)> &change);  // one Undo step


    QPointer<MobileMap> m_map;
    bool m_active = false;
    QVariantMap m_values;
};
