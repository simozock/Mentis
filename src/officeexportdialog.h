#pragma once
// FEAT-083 stage C ("The small window"): Word…, Excel… and Markdown… first ask what to export: the
// mind map (as before) or the active sheet's Gantt, with its Content and, for Word and Excel, the
// Time unit. Cancel or Export…; the save dialog comes after.
#include "ganttexport.h"
#include "ganttoffice.h"
#include "modaldialog.h"

class QLabel;
class SegmentedControl;

class OfficeExportDialog final : public ModalDialog {
    Q_OBJECT
public:
    // viewUnit: the Gantt view's time unit, the first choice; preferGantt opens on the Gantt tab.
    OfficeExportDialog(QWidget *parent, GanttOffice::Format format, GanttChart::Scale viewUnit, bool preferGantt);

    bool ganttChosen() const;
    GanttPages::Content content() const;
    GanttChart::Scale unit() const;

    void accept() override;  // remembers the Gantt's choices

private:
    void updateRows();

    SegmentedControl *m_kind = nullptr;     // Mind map / Gantt
    SegmentedControl *m_content = nullptr;  // Gantt / Gantt and costs / Costs
    SegmentedControl *m_unit = nullptr;     // Days / Weeks / Months; not for Markdown
    QLabel *m_mapNote = nullptr;
    QList<QWidget *> m_ganttRows;
};
