#include "shortcutcatalog.h"

#include "shortcuts.h"

#include <QKeySequence>

void ShortcutCatalog::addAll(ShortcutRegistry &registry) {
    using Area = ShortcutRegistry::Area;
    const auto keys = [](std::initializer_list<QKeySequence> list) { return QList<QKeySequence>(list); };
    const auto key = [](const char *text) { return QList<QKeySequence>{QKeySequence(QString::fromLatin1(text))}; };
    registry.add(QStringLiteral("home"), Area::General, tr("Library"), keys({QKeySequence("Ctrl+Shift+H"), QKeySequence("Ctrl+H")}));
    registry.add(QStringLiteral("new"), Area::General, tr("New"), keys({QKeySequence(QKeySequence::New)}));
    registry.add(QStringLiteral("open"), Area::General, tr("Open…"), keys({QKeySequence(QKeySequence::Open)}));
    registry.add(QStringLiteral("save"), Area::General, tr("Save"), keys({QKeySequence(QKeySequence::Save)}));
    registry.add(QStringLiteral("saveAs"), Area::General, tr("Save as…"), keys({QKeySequence(QKeySequence::SaveAs)}));
    registry.add(QStringLiteral("importMarkdown"), Area::General, tr("Import Markdown…"), {});
    registry.add(QStringLiteral("exportMarkdown"), Area::General, tr("Export Markdown…"), {});
    registry.add(QStringLiteral("exportImage"), Area::General, tr("Export image…"), {});
    registry.add(QStringLiteral("exportPdf"), Area::General, tr("Export PDF…"), {});
    registry.add(QStringLiteral("exportPresentation"), Area::General, tr("Export Pitch…"), {});
    registry.add(QStringLiteral("exportWord"), Area::General, tr("Export Word…"), {});
    registry.add(QStringLiteral("exportExcel"), Area::General, tr("Export Excel…"), {});
    registry.add(QStringLiteral("print"), Area::General, tr("Print…"), {QKeySequence::Print});
    registry.add(QStringLiteral("present"), Area::General, tr("Present"), keys({QKeySequence(Qt::Key_F5), QKeySequence("Ctrl+Alt+P")}));
    registry.add(QStringLiteral("undo"), Area::General, tr("Undo"), keys({QKeySequence(QKeySequence::Undo)}));
    // Ctrl+Y e' la seconda predefinita di Ripristina (standard su Windows); prima era aggiunta a mano.
    registry.add(QStringLiteral("redo"), Area::General, tr("Redo"), keys({QKeySequence("Ctrl+Shift+Z"), QKeySequence("Ctrl+Y")}));
    registry.add(QStringLiteral("layout"), Area::General, tr("Auto layout"), keys({QKeySequence("Ctrl+L")}));
    registry.add(QStringLiteral("fit"), Area::General, tr("Fit to window"), keys({QKeySequence("Ctrl+0")}));
    registry.add(QStringLiteral("focusMode"), Area::General, tr("Focus mode"), keys({QKeySequence("Ctrl+Alt+F")}));
    registry.add(QStringLiteral("image"), Area::General, tr("Add image…"), keys({QKeySequence("Ctrl+Shift+I")}));
    registry.add(QStringLiteral("linkBack"), Area::General, tr("Back through links"), keys({QKeySequence("Alt+Left")}));
    registry.add(QStringLiteral("linkForward"), Area::General, tr("Forward through links"), keys({QKeySequence("Alt+Right")}));
    registry.add(QStringLiteral("shortcuts"), Area::General, tr("Shortcuts…"), {});
    // FEAT-092: the manual's page of the place the user is in (F1; on macOS Qt's Help key).
    registry.add(QStringLiteral("help"), Area::General, tr("User manual"), keys({QKeySequence(QKeySequence::HelpContents)}));
    registry.add(QStringLiteral("settings"), Area::General, tr("Settings…"), keys({QKeySequence("Ctrl+,"), QKeySequence("Ctrl+Shift+P")}));
    // FEAT-085: keys on the map, the outliner and the Gantt (added to those views with the Gantt view).
    registry.add(QStringLiteral("copyStyle"), Area::General, tr("Copy style"), keys({QKeySequence("Ctrl+Alt+C")}));
    registry.add(QStringLiteral("pasteStyle"), Area::General, tr("Paste style"), keys({QKeySequence("Ctrl+Alt+V")}));
    registry.add(QStringLiteral("find"), Area::General, tr("Find"), keys({QKeySequence(QKeySequence::Find)}));
    registry.add(QStringLiteral("addChild"), Area::Map, tr("Add child"), keys({QKeySequence(Qt::Key_Tab)}));
    registry.add(QStringLiteral("addSibling"), Area::Map, tr("Add sibling"), keys({QKeySequence(Qt::Key_Return)}));
    // F2 selects the whole title; Space opens it with the cursor at the end (2026-11-04, user request).
    registry.add(QStringLiteral("edit"), Area::Map, tr("Edit text"), keys({QKeySequence(Qt::Key_F2)}));
    registry.add(QStringLiteral("editAtEnd"), Area::Map, tr("Edit text at the end"), keys({QKeySequence(Qt::Key_Space)}));
    registry.add(QStringLiteral("remove"), Area::Map, tr("Remove"), keys({QKeySequence(QKeySequence::Delete)}));
    // FEAT-057: no default keys since FEAT-041 stage B: Ctrl+Shift+[ / ] go to the sheets and Ctrl+]
    // to the Style panel.
    registry.add(QStringLiteral("bringToFront"), Area::Map, tr("Bring to front"), {});
    registry.add(QStringLiteral("bringForward"), Area::Map, tr("Bring forward"), {});
    registry.add(QStringLiteral("sendBackward"), Area::Map, tr("Send backward"), {});
    registry.add(QStringLiteral("sendToBack"), Area::Map, tr("Send to back"), {});
    registry.add(QStringLiteral("newSheet"), Area::General, tr("New sheet"), key("Ctrl+Alt+N"));
    registry.add(QStringLiteral("prevSheet"), Area::General, tr("Previous sheet"), key("Ctrl+Shift+["));
    registry.add(QStringLiteral("nextSheet"), Area::General, tr("Next sheet"), key("Ctrl+Shift+]"));
    registry.add(QStringLiteral("toggleOutliner"), Area::General, tr("Switch between map and outliner"), key("Ctrl+Shift+M"));
    registry.add(QStringLiteral("ganttView"), Area::General, tr("Gantt chart"), key("Ctrl+Alt+G"));
    registry.add(QStringLiteral("zoomIn"), Area::General, tr("Zoom in"), key("Ctrl++"));
    registry.add(QStringLiteral("zoomOut"), Area::General, tr("Zoom out"), key("Ctrl+-"));
    registry.add(QStringLiteral("actualSize"), Area::General, tr("Actual size"), {});
    registry.add(QStringLiteral("panelStyle"), Area::General, tr("Format panel: Style"), key("Ctrl+]"));
    registry.add(QStringLiteral("panelPitch"), Area::General, tr("Format panel: Pitch"), {});
    registry.add(QStringLiteral("panelMap"), Area::General, tr("Format panel: Map"), {});
    registry.add(QStringLiteral("toggleSidePanel"), Area::General, tr("Open/close side panel (Format on the map, tasks in the Gantt)"), key("Ctrl+Alt+S"));
    registry.add(QStringLiteral("toggleIconPanel"), Area::General, tr("Open/close markers and stickers panel (map)"), key("Ctrl+Alt+M"));
    registry.add(QStringLiteral("bold"), Area::General, tr("Bold"), key("Ctrl+B"));
    registry.add(QStringLiteral("italic"), Area::General, tr("Italic"), key("Ctrl+I"));
    registry.add(QStringLiteral("underline"), Area::General, tr("Underline"), key("Ctrl+U"));
    registry.add(QStringLiteral("strikeOut"), Area::General, tr("Strikethrough"), key("Ctrl+Shift+X"));
    // FEAT-086: reference keys (checked live 2026-11-01).
    registry.add(QStringLiteral("moveUp"), Area::Map, tr("Move topic up"), key("Alt+Up"));
    registry.add(QStringLiteral("moveDown"), Area::Map, tr("Move topic down"), key("Alt+Down"));
    registry.add(QStringLiteral("topicBefore"), Area::Map, tr("Topic before"), key("Shift+Return"));
    registry.add(QStringLiteral("parentTopic"), Area::Map, tr("Parent topic"), key("Ctrl+Return"));
    registry.add(QStringLiteral("presetVeryImportant"), Area::Map, tr("Style \"Very important\""), key("Shift+Alt+1"));
    registry.add(QStringLiteral("presetImportant"), Area::Map, tr("Style \"Important\""), key("Shift+Alt+2"));
    registry.add(QStringLiteral("presetCrossedOut"), Area::Map, tr("Style \"Strike out\""), key("Shift+Alt+3"));
    registry.add(QStringLiteral("branchToSheet"), Area::Map, tr("New sheet from topic"), {});
    registry.add(QStringLiteral("resetStyle"), Area::Map, tr("Reset style"), key("Ctrl+Alt+0"));
    registry.add(QStringLiteral("relation"), Area::Map, tr("Relationship"), key("Ctrl+Shift+R"));
    registry.add(QStringLiteral("boundary"), Area::Map, tr("Boundary"), key("Ctrl+Shift+B"));
    registry.add(QStringLiteral("summary"), Area::Map, tr("Summary"), {});
    registry.add(QStringLiteral("note"), Area::Map, tr("Note"), key("Ctrl+Shift+N"));
    registry.add(QStringLiteral("labels"), Area::Map, tr("Label"), key("Ctrl+Shift+L"));
    registry.add(QStringLiteral("webLink"), Area::Map, tr("Link to a web page"), key("Ctrl+K"));
    registry.add(QStringLiteral("topicLink"), Area::Map, tr("Link to a topic"), {});
    registry.add(QStringLiteral("attachFiles"), Area::Map, tr("Attach files"), {});
    registry.add(QStringLiteral("mapCopy"), Area::Map, tr("Copy"), {QKeySequence(QKeySequence::Copy)});
    registry.add(QStringLiteral("mapCut"), Area::Map, tr("Cut"), {QKeySequence(QKeySequence::Cut)});
    registry.add(QStringLiteral("mapPaste"), Area::Map, tr("Paste"), {QKeySequence(QKeySequence::Paste)});
    registry.add(QStringLiteral("markers"), Area::Map, tr("Markers"), {});
    registry.add(QStringLiteral("stickers"), Area::Map, tr("Stickers"), {});
    registry.add(QStringLiteral("formula"), Area::Map, tr("LaTeX formula"), {});
    registry.add(QStringLiteral("removeMarkers"), Area::Map, tr("Remove markers"), {});
    registry.add(QStringLiteral("removeVisual"), Area::Map, tr("Remove sticker, image or formula"), {});
    registry.add(QStringLiteral("removeLinks"), Area::Map, tr("Remove links"), {});
    registry.add(QStringLiteral("addTask"), Area::Map, tr("Checkbox"), key("Ctrl+Alt+T"));
    registry.add(QStringLiteral("removeTask"), Area::Map, tr("Remove checkbox"), {});
    registry.add(QStringLiteral("taskDone"), Area::Map, tr("Mark as done"), {});
    registry.add(QStringLiteral("taskNotDone"), Area::Map, tr("Mark as not done"), {});
    registry.add(QStringLiteral("upperCase"), Area::Map, tr("All uppercase"), {});
    registry.add(QStringLiteral("lowerCase"), Area::Map, tr("All lowercase"), {});
    registry.add(QStringLiteral("alignLeft"), Area::Map, tr("Align text left"), {});
    registry.add(QStringLiteral("alignCenter"), Area::Map, tr("Center text"), {});
    registry.add(QStringLiteral("alignRight"), Area::Map, tr("Align text right"), {});
    registry.add(QStringLiteral("fontLarger"), Area::Map, tr("Larger text"), key("Ctrl+Shift+."));
    registry.add(QStringLiteral("fontSmaller"), Area::Map, tr("Smaller text"), key("Ctrl+Shift+,"));
    registry.add(QStringLiteral("numberingNone"), Area::Map, tr("Numbering: none"), {});
    registry.add(QStringLiteral("numbering123"), Area::Map, tr("Numbering: 1.2.3"), {});
    registry.add(QStringLiteral("numberingABC"), Area::Map, tr("Numbering: A.B.C"), {});
    registry.add(QStringLiteral("numberingabc"), Area::Map, tr("Numbering: a.b.c"), {});
    registry.add(QStringLiteral("numberingRoman"), Area::Map, tr("Numbering: I.II.III"), {});
    registry.add(QStringLiteral("goToCentral"), Area::Map, tr("Go to central topic"), key("Ctrl+R"));
    registry.add(QStringLiteral("selectSubtopics"), Area::Map, tr("Select subtopics"), {});
    registry.add(QStringLiteral("selectSiblings"), Area::Map, tr("Select siblings"), {});
    registry.add(QStringLiteral("selectSameLevel"), Area::Map, tr("Select topics of the same level"), {});
    registry.add(QStringLiteral("selectFloating"), Area::Map, tr("Select floating topics"), {});
    registry.add(QStringLiteral("selectRelationships"), Area::Map, tr("Select relationships"), {});
    registry.add(QStringLiteral("selectSummaries"), Area::Map, tr("Select summaries"), {});
    registry.add(QStringLiteral("selectBoundaries"), Area::Map, tr("Select boundaries"), {});
    registry.add(QStringLiteral("selectImages"), Area::Map, tr("Select images"), {});
    registry.add(QStringLiteral("selectEquations"), Area::Map, tr("Select equations"), {});
    registry.add(QStringLiteral("toggleCollapse"), Area::Map, tr("Collapse or expand the branch"), key("Ctrl+/"));
    registry.add(QStringLiteral("toggleSubBranches"), Area::Map, tr("Collapse or expand the sub-branches"), key("Ctrl+Alt+/"));
    // Arrows on the map: to the next topic that way (user's request 2026-10-21).
    registry.add(QStringLiteral("mapSelectLeft"), Area::Map, tr("Select the node to the left"), keys({QKeySequence(Qt::Key_Left)}));
    registry.add(QStringLiteral("mapSelectRight"), Area::Map, tr("Select the node to the right"), keys({QKeySequence(Qt::Key_Right)}));
    registry.add(QStringLiteral("mapSelectUp"), Area::Map, tr("Select the node above"), keys({QKeySequence(Qt::Key_Up)}));
    registry.add(QStringLiteral("mapSelectDown"), Area::Map, tr("Select the node below"), keys({QKeySequence(Qt::Key_Down)}));
    // Outliner keys.
    registry.add(QStringLiteral("outlineNewItem"), Area::Outliner, tr("New item"), keys({QKeySequence(Qt::Key_Return)}));
    registry.add(QStringLiteral("outlineIndent"), Area::Outliner, tr("Indent"), keys({QKeySequence(Qt::Key_Tab)}));
    registry.add(QStringLiteral("outlineOutdent"), Area::Outliner, tr("Outdent"), keys({QKeySequence("Shift+Tab"), QKeySequence(Qt::Key_Backspace)}));
    registry.add(QStringLiteral("outlineCopy"), Area::Outliner, tr("Copy"), keys({QKeySequence(QKeySequence::Copy)}));
    registry.add(QStringLiteral("outlineCut"), Area::Outliner, tr("Cut"), keys({QKeySequence(QKeySequence::Cut)}));
    registry.add(QStringLiteral("outlinePaste"), Area::Outliner, tr("Paste"), keys({QKeySequence(QKeySequence::Paste)}));
    registry.add(QStringLiteral("outlineDuplicate"), Area::Outliner, tr("Duplicate"), keys({QKeySequence("Ctrl+D")}));
    registry.add(QStringLiteral("outlineDelete"), Area::Outliner, tr("Delete"), keys({QKeySequence(Qt::Key_Delete)}));
    // FEAT-075: keys of the Gantt table, handled by GanttView.
    registry.add(QStringLiteral("ganttAddTask"), Area::Gantt, tr("Add task"), keys({QKeySequence(Qt::Key_Return)}));
    registry.add(QStringLiteral("ganttIndent"), Area::Gantt, tr("Make subtask"), keys({QKeySequence(Qt::Key_Tab)}));
    registry.add(QStringLiteral("ganttOutdent"), Area::Gantt, tr("Move up a level"), keys({QKeySequence("Shift+Tab")}));
    registry.add(QStringLiteral("ganttDelete"), Area::Gantt, tr("Delete tasks"), keys({QKeySequence(Qt::Key_Delete)}));
    registry.add(QStringLiteral("ganttChain"), Area::Gantt, tr("Link in a chain"), keys({QKeySequence("Ctrl+Shift+L")}));
}
