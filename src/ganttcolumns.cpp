#include "ganttcolumns.h"

#include "ganttcommands.h"
#include "mindmapmodel.h"

#include <QCoreApplication>
#include <QLocale>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>

namespace {

const QString kShownKey = QStringLiteral("gantt/columns");
const QString kOrderKey = QStringLiteral("gantt/columnOrder");
const QString kNumberHiddenKey = QStringLiteral("gantt/numberHidden");
const QString kWidthsKey = QStringLiteral("gantt/columnWidths");  // column key -> px

QStringList shownKeys() {
    return QSettings().value(kShownKey, QStringList{QStringLiteral("start"), QStringLiteral("duration")}).toStringList();
}

}  // namespace

QVector<GanttColumns::Field> GanttColumns::movable() {
    return {Field::Start, Field::End, Field::Duration, Field::Progress, Field::Priority, Field::People};
}

QString GanttColumns::key(Field field) {
    switch (field) {
    case Field::Start: return QStringLiteral("start");
    case Field::End: return QStringLiteral("end");
    case Field::Duration: return QStringLiteral("duration");
    case Field::Progress: return QStringLiteral("progress");
    case Field::Priority: return QStringLiteral("priority");
    case Field::People: return QStringLiteral("person");  // FEAT-078's key, from when it was one person
    case Field::Number:
    case Field::Name: break;
    }
    return {};
}

QString GanttColumns::title(Field field) {
    const auto viewText = [](const char *text) { return QCoreApplication::translate("GanttView", text); };
    switch (field) {
    case Field::Number: return viewText("No.");
    case Field::Name: return viewText("Name");
    case Field::Start: return viewText("Start");
    case Field::End: return viewText("End");
    case Field::Duration: return viewText("Duration");
    case Field::Progress: return viewText("Progress");
    case Field::Priority: return viewText("Priority");
    case Field::People: return viewText("People");
    }
    return {};
}

int GanttColumns::defaultWidth(Field field) {
    switch (field) {
    case Field::Start:
    case Field::End: return 96;
    case Field::Duration: return 72;
    case Field::Progress: return 96;
    case Field::Priority: return 96;
    case Field::People: return 112;
    case Field::Number:
    case Field::Name: break;
    }
    return 0;
}

int GanttColumns::width(Field field) {
    const int fallback = defaultWidth(field);
    if (fallback == 0) return 0;
    const int saved = QSettings().value(kWidthsKey).toMap().value(key(field), fallback).toInt();
    return std::max(kMinWidth, saved);
}

void GanttColumns::setWidth(Field field, int width) {
    if (defaultWidth(field) == 0) return;
    QVariantMap widths = QSettings().value(kWidthsKey).toMap();
    widths.insert(key(field), std::max(kMinWidth, width));
    QSettings().setValue(kWidthsKey, widths);
}

QVector<GanttColumns::Field> GanttColumns::order() {
    QVector<Field> fields;
    const QVector<Field> all = movable();
    for (const QString &saved : QSettings().value(kOrderKey).toStringList())
        for (const Field field : all)
            if (key(field) == saved && !fields.contains(field)) fields.append(field);
    for (const Field field : all)
        if (!fields.contains(field)) fields.append(field);
    return fields;
}

void GanttColumns::setOrder(const QVector<Field> &order) {
    QStringList keys;
    for (const Field field : order)
        if (const QString k = key(field); !k.isEmpty() && !keys.contains(k)) keys.append(k);
    QSettings().setValue(kOrderKey, keys);
}

bool GanttColumns::isShown(Field field) {
    if (field == Field::Name) return true;
    if (field == Field::Number) return !QSettings().value(kNumberHiddenKey, false).toBool();
    return shownKeys().contains(key(field));
}

void GanttColumns::setShown(Field field, bool shown) {
    if (field == Field::Name) return;
    if (field == Field::Number) {
        QSettings().setValue(kNumberHiddenKey, !shown);
        return;
    }
    // In the default order, as the desktop writes it.
    QStringList keys;
    for (const Field column : movable())
        if (column == field ? shown : isShown(column)) keys.append(key(column));
    QSettings().setValue(kShownKey, keys);
}

QVector<GanttColumns::Field> GanttColumns::shownFields(bool numbered) {
    QVector<Field> fields;
    if (numbered && isShown(Field::Number)) fields.append(Field::Number);
    fields.append(Field::Name);
    const QStringList keys = shownKeys();  // one read for all of them
    for (const Field field : order())
        if (keys.contains(key(field))) fields.append(field);
    return fields;
}

bool GanttColumns::numbered(const MindNode *root) {
    if (!root) return false;
    for (const auto &child : root->children)
        if (!child->number.isEmpty() || numbered(child.get())) return true;
    return false;
}

QString GanttColumns::cellText(Field field, const GanttPlan &plan, const GanttTask &task, const GanttScheduledTask &scheduled,
                               const MindNode *topic, const QVector<GanttResource> &people) {
    const auto shortDate = [](const QDate &date) { return QLocale().toString(date, QLocale::ShortFormat); };
    QString text;
    switch (field) {
    case Field::Number: text = topic ? topic->number : QString(); break;
    case Field::Name: text = task.shownName(); break;
    case Field::Start: text = scheduled.dated ? shortDate(scheduled.start) : QString(); break;
    case Field::End: text = scheduled.dated ? shortDate(scheduled.end) : QString(); break;
    case Field::Duration:
        if (!scheduled.summary) text = QString::number(task.duration);
        else if (scheduled.dated) text = QString::number(plan.workingDaysBetween(scheduled.start, scheduled.end));
        break;
    case Field::Progress: text = QStringLiteral("%1%").arg(scheduled.progress); break;
    case Field::Priority: text = topic ? GanttCommands::priorityName(topic->ganttPriority) : QString(); break;
    case Field::People: {
        // FEAT-081: in the Resources list's order, removed people left out.
        QStringList names;
        if (topic && !topic->ganttPeople.isEmpty())
            for (const GanttResource &person : people)
                if (topic->ganttPeople.contains(person.id)) names.append(person.displayName().isEmpty() ? person.email : person.displayName());
        text = names.join(QStringLiteral(", "));
        break;
    }
    }
    return text.simplified();
}
