#pragma once
// FEAT-083 stage C (docs/features/FEAT-083-gantt-esportazione.md, "Formats" and "Excel (stage C)"):
// a sheet's Gantt and its costs as a Word document, an Excel workbook laid out as the user's weekly
// planner template, and Markdown tables. No widgets: Word's pictures are GanttPages' PDF pages.
#include "ganttexport.h"

#include <QByteArray>
#include <QDate>
#include <QString>

namespace GanttOffice {

enum class Format { Word, Excel, Markdown };

// The chart as pictures, one per landscape A4 PDF page (fit width, the unit chosen), then the cost
// tables. Empty with *error set when there is nothing to export.
QByteArray docx(const GanttExportSource &source, GanttPages::Content content, GanttChart::Scale unit, const QString &title,
                QString *error = nullptr);
// Sheets Gantt (bars as conditional formatting), Costs, People and Per week, as content asks.
// today: the highlighted period at first.
QByteArray xlsx(const GanttExportSource &source, GanttPages::Content content, GanttChart::Scale unit, const QString &title,
                const QDate &today = GanttToday::date());
QString markdown(const GanttExportSource &source, GanttPages::Content content);

// Write the file; empty = written, else the error (no half file is left).
QString write(Format format, const QString &path, const GanttExportSource &source, GanttPages::Content content,
              GanttChart::Scale unit, const QString &title);

}  // namespace GanttOffice
