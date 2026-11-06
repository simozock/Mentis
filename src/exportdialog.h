#pragma once

// FEAT-054 stage 2: "Export > Image…", like Excalidraw's export (attachments/FEAT-054/02):
// preview and file name on the left; Content, Background and Scale on the right; PNG, SVG and
// "Copy to clipboard" at the bottom. Closes with Esc or the X. The drawing is MapExporter's.
// The PDF has its own dialog since stage 3 (PageDialog).

#include "ganttexport.h"
#include "exportframe.h"
#include "mapexporter.h"
#include "modaldialog.h"

#include <functional>
#include <memory>
#include <optional>

class ExportFrameControls;
class ExportPreview;
class QLineEdit;
class QPushButton;
class SegmentedControl;
class ToggleSwitch;

class ExportDialog final : public ModalDialog {
    Q_OBJECT
public:
    enum class Scope { ActiveSheet, AllSheets, Branch };  // the order of the Content segments
    using PagesFor = std::function<QList<MapExporter::Page>(Scope scope)>;

    // documentName: the start of the file name; branchAvailable: one topic is selected.
    // gantt (FEAT-083): the active sheet's Gantt, a tab beside Mind map; preferGantt opens on it.
    ExportDialog(QWidget *parent, const QString &documentName, bool branchAvailable, PagesFor pages,
                 std::function<QString(const MindNode *)> originalImagePath, std::optional<GanttExportSource> gantt = {},
                 bool preferGantt = false);
    ~ExportDialog() override;

    // Save dialog for a format (QFileDialog by default); tests replace it. Empty = cancelled.
    std::function<QString(MapExporter::Format format, const QString &suggestedPath)> pickPath;
    // For the bottom bar once the dialog closed after an export or a copy; empty otherwise.
    QString outcome() const { return m_outcome; }
    // FEAT-045: what an agent chose before opening the window; unset = as the window opens.
    struct Preset {
        std::optional<Scope> scope;
        std::optional<bool> gantt;       // the Gantt tab instead of the map
        std::optional<bool> background;
        std::optional<int> scale;        // 1-3 (PNG)
        std::optional<ExportFrame> frame;
    };
    void applyPreset(const Preset &preset);

    static constexpr int kPreviewWidthPx = 440;
    static constexpr int kPreviewHeightPx = 250;

private:
    MapExporter::Options options() const;
    Scope scope() const;
    bool ganttChosen() const;
    GanttChart::Scale ganttUnit() const;
    GanttPages::Content ganttContent() const;
    void refreshPreview();
    void exportAs(MapExporter::Format format);
    void copyToClipboard();
    QString suggestedPath(MapExporter::Format format) const;
    void storeChoices() const;

    PagesFor m_pages;
    std::function<QString(const MindNode *)> m_originalImagePath;
    SegmentedControl *m_scope = nullptr;
    SegmentedControl *m_kind = nullptr;       // Mind map / Gantt (FEAT-083), with a Gantt only
    SegmentedControl *m_unit = nullptr;       // Days / Weeks / Months
    SegmentedControl *m_content = nullptr;    // Gantt / Gantt and costs / Costs
    ExportFrameControls *m_frame = nullptr;   // FEAT-084
    std::optional<GanttExportSource> m_gantt;
    std::unique_ptr<GanttPages> m_ganttPage;  // the preview's picture
    QList<QPair<QWidget *, QWidget *>> m_mapRows, m_ganttRows;  // label and control
    ToggleSwitch *m_background = nullptr;
    SegmentedControl *m_scale = nullptr;
    QLineEdit *m_name = nullptr;
    ExportPreview *m_preview = nullptr;
    QPushButton *m_copy = nullptr;
    std::unique_ptr<MapExporter::Rendering> m_rendering;  // the preview's page
    QString m_outcome;
};
