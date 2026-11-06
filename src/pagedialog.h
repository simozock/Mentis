#pragma once

// FEAT-054 stage 3: "Export > PDF…" and "Print…", one dialog (attachments/FEAT-054/
// 03-07) in Mentis's colors: page preview with its number on the left; Mind map / Outliner, printer
// and copies (print only), Content, Page size, Orientation, Scale, Background and Monochrome on the
// right. The pages are MapExporter::PagePlan's; with Pitch (PDF only) the presentation's slides.

#include "exportdialog.h"
#include "exportframe.h"
#include "ganttexport.h"
#include "mapexporter.h"
#include "modaldialog.h"
#include "pitch.h"

#include <QHash>

#include <functional>
#include <memory>
#include <optional>

class ExportFrameControls;
class PagePreview;
class QComboBox;
class QLabel;
class QSpinBox;
class QToolButton;
class SegmentedControl;
class ToggleSwitch;

class PageDialog final : public ModalDialog {
    Q_OBJECT
public:
    enum class Mode { Pdf, Print };
    using Scope = ExportDialog::Scope;
    // FEAT-063 stage C (user's request 2026-10-27): the PDF of the Pitch presentation, the third
    // choice beside Mind map and Outliner. ratio: the slide's width / height.
    struct PitchSource {
        QList<Pitch::Slide> slides;
        Pitch::Look look;
        qreal ratio = 16.0 / 9.0;
    };

    // FEAT-083: the active sheet's Gantt, a tab after Outliner; preferred = open on it (the Gantt
    // view is shown).
    struct GanttChoice {
        GanttExportSource source;
        bool preferred = false;
    };

    // pitch: offered with Mode::Pdf only.
    PageDialog(QWidget *parent, Mode mode, const QString &documentName, bool branchAvailable, ExportDialog::PagesFor pages,
               std::function<QString(const MindNode *)> originalImagePath, std::optional<PitchSource> pitch = {},
               std::optional<GanttChoice> gantt = {});
    ~PageDialog() override;

    // PDF: the save dialog (QFileDialog by default); tests replace it. Empty = cancelled.
    std::function<QString(const QString &suggestedPath)> pickPath;
    // For the bottom bar once the dialog closed after an export or a print; empty otherwise.
    QString outcome() const { return m_outcome; }
    // FEAT-045: an agent's choices before the window opens. kind: 0 map, 1 outliner, then the Gantt and
    // the Pitch tabs when the window has them (ganttTab, pitchTab).
    struct Preset {
        std::optional<int> scope;        // ExportDialog::Scope
        std::optional<QString> kind;     // "map", "outliner", "gantt", "pitch"
        std::optional<bool> background;
        std::optional<ExportFrame> frame;
    };
    void applyPreset(const Preset &preset);
    const MapExporter::PageSource *plan() const { return m_plan.get(); }  // for tests

    static constexpr int kPreviewWidthPx = 300;
    static constexpr int kPreviewHeightPx = 380;

private:
    MapExporter::PageOptions options() const;
    Scope scope() const;
    bool pitchChosen() const;
    bool ganttChosen() const;
    GanttPages::Options ganttOptions() const;
    void setRowVisible(QWidget *control, bool visible);
    void syncControls();  // enables what applies to the choices
    void refreshPlan();
    void showPage(int index);
    void run();
    void storeChoices() const;
    QString settingsKey(const QString &name) const;

    Mode m_mode;
    QString m_documentName;
    ExportDialog::PagesFor m_pages;
    std::function<QString(const MindNode *)> m_originalImagePath;
    std::optional<PitchSource> m_pitch;
    std::optional<GanttChoice> m_gantt;
    int m_ganttIndex = -1;  // the tabs' indexes, -1 when absent
    int m_pitchIndex = -1;
    int m_lastKind = 0;
    SegmentedControl *m_ganttContent = nullptr;  // Gantt / Gantt and costs / Costs
    SegmentedControl *m_ganttUnit = nullptr;     // Days / Weeks / Months
    SegmentedControl *m_ganttFit = nullptr;      // Fit width / Actual size
    QLabel *m_ganttNote = nullptr;               // the unit fit width took, the pages in width
    QHash<QWidget *, QLabel *> m_rowLabels;
    ExportFrameControls *m_frame = nullptr;  // FEAT-084
    SegmentedControl *m_kind = nullptr;   // Mind map / Outliner (/ Pitch)
    QComboBox *m_printer = nullptr;
    QSpinBox *m_copies = nullptr;
    SegmentedControl *m_scope = nullptr;
    QComboBox *m_size = nullptr;
    SegmentedControl *m_orientation = nullptr;
    SegmentedControl *m_scale = nullptr;
    ToggleSwitch *m_background = nullptr;
    ToggleSwitch *m_monochrome = nullptr;
    PagePreview *m_preview = nullptr;
    QLabel *m_pageNumber = nullptr;
    QToolButton *m_previous = nullptr;
    QToolButton *m_next = nullptr;
    std::unique_ptr<MapExporter::PageSource> m_plan;
    int m_page = 0;
    QString m_outcome;
};
