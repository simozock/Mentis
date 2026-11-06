#include "officeexportdialog.h"
#include "help.h"

#include "segmentedcontrol.h"
#include "uitheme.h"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <algorithm>

namespace {
const QString kContentKey = QStringLiteral("export/officeGanttContent");
const QString kUnitKey = QStringLiteral("export/officeGanttUnit");

QString titleFor(GanttOffice::Format format) {
    switch (format) {
    case GanttOffice::Format::Word: return OfficeExportDialog::tr("Export Word");
    case GanttOffice::Format::Excel: return OfficeExportDialog::tr("Export Excel");
    case GanttOffice::Format::Markdown: break;
    }
    return OfficeExportDialog::tr("Export Markdown");
}
}  // namespace

OfficeExportDialog::OfficeExportDialog(QWidget *parent, GanttOffice::Format format, GanttChart::Scale viewUnit, bool preferGantt)
    : ModalDialog(parent, titleFor(format), tr("Export…"), 520) {
    Help::setPage(this, QStringLiteral("files/export-documents.md"));  // FEAT-092: F1 and "?" open it
    setObjectName(QStringLiteral("OfficeExportDialog"));
    QSettings settings;
    m_kind = new SegmentedControl({tr("Mind map"), tr("Gantt")}, this);
    m_kind->setObjectName(QStringLiteral("OfficeExportKind"));
    m_kind->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_kind->setCurrentIndex(preferGantt ? 1 : 0);
    contentLayout()->addWidget(m_kind, 0, Qt::AlignLeft);

    // The map's export as before: Word and Excel take every sheet, Markdown the active one.
    m_mapNote = new QLabel(format == GanttOffice::Format::Markdown ? tr("The active sheet, its attachments in a folder beside it.")
                                                                   : tr("Every sheet of the document."),
                           this);
    m_mapNote->setObjectName(QStringLiteral("OfficeExportMapNote"));
    m_mapNote->setWordWrap(true);
    m_mapNote->setStyleSheet(UiTheme::qss(QStringLiteral("QLabel { color:@textMuted; }")));
    contentLayout()->addWidget(m_mapNote);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(16);
    grid->setColumnStretch(1, 1);
    const auto addRow = [this, grid](const QString &text, SegmentedControl *control) {
        const int row = grid->rowCount();
        auto *label = new QLabel(text, this);
        grid->addWidget(label, row, 0);
        grid->addWidget(control, row, 1, Qt::AlignRight | Qt::AlignVCenter);
        control->setAccessibleName(text);
        control->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_ganttRows << label << control;
    };
    m_content = new SegmentedControl({tr("Gantt"), tr("Gantt and costs"), tr("Costs")}, this);
    m_content->setObjectName(QStringLiteral("OfficeExportContent"));
    m_content->setCurrentIndex(std::clamp(settings.value(kContentKey, 0).toInt(), 0, 2));
    addRow(tr("Content"), m_content);
    if (format != GanttOffice::Format::Markdown) {  // Markdown has tables only
        m_unit = new SegmentedControl({tr("Days"), tr("Weeks"), tr("Months")}, this);
        m_unit->setObjectName(QStringLiteral("OfficeExportUnit"));
        m_unit->setCurrentIndex(std::clamp(settings.value(kUnitKey, int(viewUnit)).toInt(), 0, 2));
        addRow(tr("Time unit"), m_unit);
    }
    contentLayout()->addLayout(grid);
    connect(m_kind, &SegmentedControl::segmentActivated, this, [this] { updateRows(); });
    updateRows();
}

bool OfficeExportDialog::ganttChosen() const { return m_kind->currentIndex() == 1; }

GanttPages::Content OfficeExportDialog::content() const { return static_cast<GanttPages::Content>(m_content->currentIndex()); }

GanttChart::Scale OfficeExportDialog::unit() const {
    return m_unit ? static_cast<GanttChart::Scale>(m_unit->currentIndex()) : GanttChart::Scale::Days;
}

void OfficeExportDialog::accept() {
    if (ganttChosen()) {
        QSettings settings;
        settings.setValue(kContentKey, m_content->currentIndex());
        if (m_unit) settings.setValue(kUnitKey, m_unit->currentIndex());
    }
    ModalDialog::accept();
}

void OfficeExportDialog::updateRows() {
    const bool gantt = ganttChosen();
    m_mapNote->setVisible(!gantt);
    for (QWidget *widget : std::as_const(m_ganttRows)) widget->setVisible(gantt);
}
