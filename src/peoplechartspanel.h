#pragma once

// FEAT-127: the people's charts of the Resources modal (desktop), read-only. Nobody selected: the team
// summary (planned cost per currency, person-days, days per person, cost by role); a person: their line, the load
// per period of the Gantt's scale (scrolling sideways) and the progress of their tasks. The figures
// come from GanttPeopleCharts; this widget only lays them out and paints the bars.

#include "ganttpeoplecharts.h"

#include <QWidget>

class QLabel;
class QScrollArea;

class PeopleChartsPanel final : public QWidget {
    Q_OBJECT
public:
    static constexpr int kWidthPx = 300;  // beside the people
    explicit PeopleChartsPanel(QWidget *parent);

    // person: an index in charts.people, -1 for the summary.
    void setCharts(const GanttPeopleCharts &charts, int person);
    // The wrapped empty text's height at the panel's width (ModalDialog sizes wrapped texts for the
    // whole surface when it opens).
    void fitTexts(int widthPx);
    const GanttPeopleCharts &charts() const { return m_charts; }
    int person() const { return m_person; }

    // For the tests.
    QWidget *summaryPage() const { return m_summary; }
    QWidget *personPage() const { return m_personPage; }
    QLabel *emptyLabel() const { return m_empty; }
    QLabel *costLabel() const { return m_cost; }
    QLabel *personDaysLabel() const { return m_personDays; }
    QLabel *nameLabel() const { return m_name; }
    QLabel *personLineLabel() const { return m_line; }
    QLabel *progressLabel() const { return m_progressText; }
    QScrollArea *loadArea() const { return m_loadArea; }
    QWidget *loadChart() const { return m_load; }

private:
    GanttPeopleCharts m_charts;
    int m_person = -1;
    QLabel *m_empty;
    QWidget *m_summary;
    QLabel *m_cost;
    QLabel *m_personDays;
    QWidget *m_dayBars;
    QLabel *m_rolesTitle;
    QWidget *m_roles;
    QWidget *m_personPage;
    QLabel *m_name;
    QLabel *m_line;
    QScrollArea *m_loadArea;
    QWidget *m_load;
    QWidget *m_meter;
    QLabel *m_progressText;
};
