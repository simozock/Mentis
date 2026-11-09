#pragma once

#include "formulatext.h"
#include "bmapstorage.h"
#include "document.h"
#include "exportdialog.h"
#include "ganttoffice.h"
#include "maphistory.h"
#include "nodeitem.h"
#include "pagedialog.h"
#include "panelcontext.h"
#include "shortcuts.h"
#include "performance.h"
#include "windowframe.h"
#include "mcpmaptools.h"
#include "mcpserver.h"

namespace McpBridge { class Host; }

#include <QMainWindow>
#include <QPointer>
#include <QHash>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QVector>
#include <functional>
#include <memory>
#include <optional>

class GanttView;
class QAction;
class QColor;
class QComboBox;
class QDoubleSpinBox;
class QDockWidget;
class QGridLayout;
class QHBoxLayout;
class QKeyEvent;
class QKeySequence;
class QLockFile;
class QLineEdit;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QPixmap;
class QPushButton;
class QResizeEvent;
class QStackedWidget;
class QToolBar;
class QToolButton;
class QTimer;
class QVariantAnimation;
class QTreeView;
class MindMapModel;
class MindMapScene;
class MindMapView;
class SidePanel;
class StylePage;
class UpdatePrompt;
class ManualWindow;
namespace RichTitle { enum class CharStyle; }
class ImageStylePage;
class BoundaryStylePage;
class RelationshipStylePage;
class VariantPicker;
class NoteEditor;
class LabelEditor;
class SheetStylePanel;
class ColorField;
class PaletteSelect;
class OfficeExportDialog;
struct MindNode;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    // FEAT-090: the update question, once a day; main() calls it a few seconds after the window opens.
    UpdatePrompt *updatePrompt();
    // FEAT-092: the manual's window on a page (created at the first use), and the page of the place
    // the user is in: the Home, the view, the side panel's tab, what is selected.
    void showManual(const QString &page);
    QString helpPage() const;
    ManualWindow *manualWindow() const;
    // FEAT-093: the guided tour (created at the first use); main() offers it at the first start.
    class GuidedTour *tour();
    void offerTourAtFirstStart();
    void openAtStart(const QString &path);  // the map given on the command line ("Mentis <file>")
    void showTip(const QString &key);  // the first use of an area: "outliner" and "gantt" offer their tour, "pitch" a tip
    NoteEditor *noteEditor() const { return m_noteEditor; }  // FEAT-059 (per i test)
    NoteEditor *outlineNoteEditor() const { return m_outlineNoteEditor; }
    // FEAT-065: posti nell'outliner (coordinate del viewport), per le prove; vuoti se assenti.
    QRect outlinePictureRect(const MindNode *node) const;
    QRect outlineMarkerRect(const MindNode *node, int marker) const;
    QRect outlineIconRect(const MindNode *node, int icon) const;
    bool outlinePictureSelected(const MindNode *node) const;
    LabelEditor *labelEditor() const { return m_labelEditor; }  // FEAT-060 (per i test)
    // FEAT-027: apertura delle pagine web dei collegamenti (predefinita QDesktopServices::openUrl);
    // i test la sostituiscono per non aprire il browser.
    void setUrlOpener(std::function<bool(const QUrl &)> opener) { m_openUrl = std::move(opener); }
    // FEAT-037: scelta del file dell'immagine (predefinita QFileDialog); i test la sostituiscono.
    void setImageFilePicker(std::function<QString()> picker) { m_pickImageFile = std::move(picker); }
    // FEAT-049: scelta dei file da allegare e destinazione di "Salva con nome..." (per i test).
    void setAttachFilesPicker(std::function<QStringList()> picker) { m_pickAttachFiles = std::move(picker); }
    void setAttachmentSavePicker(std::function<QString(const QString &)> picker) { m_pickAttachmentSave = std::move(picker); }
    // FEAT-049: allega i file al nodo (controlli di FEAT-039, un passo di storico); vero se ne ha aggiunti.
    bool attachFiles(MindNode *node, const QStringList &paths);
    // Apre il file index del nodo con l'applicazione del sistema da una copia temporanea, osservata:
    // se cambia si chiede se aggiornare l'allegato. Vuoto se non si apre; altrimenti la copia.
    QString openNodeFile(MindNode *node, int index);
    // Controlla subito le copie aperte (lo fa da solo quando il sistema segnala una modifica).
    void checkOpenedFiles();
    // Tipi che si aprono solo dopo una conferma (programmi e script).
    static bool isRiskyFile(const QString &name);
    // BUG-014: scelta del file di "Importa Markdown…" (predefinita QFileDialog); i test la sostituiscono.
    void setMarkdownFilePicker(std::function<QString()> picker) { m_pickMarkdownFile = std::move(picker); }
    // FEAT-066: file di "Apri…" e di "Salva con nome…" (predefiniti QFileDialog); i test li sostituiscono.
    void setOpenFilePicker(std::function<QString()> picker) { m_pickOpenFile = std::move(picker); }
    void setSaveFilePicker(std::function<QString()> picker) { m_pickSaveFile = std::move(picker); }
    void setExportFilePicker(std::function<QString()> picker) { m_pickExportFile = std::move(picker); }
    // FEAT-083: the Word/Excel/Markdown window's choices in place of its exec (true = Export…).
    void setOfficeExportChooser(std::function<bool(OfficeExportDialog &)> chooser) { m_chooseOfficeExport = std::move(chooser); }
    // FEAT-037: inserisce l'immagine nel nodo selezionato (come Inserisci > Immagine…).
    void chooseImage();
    // FEAT-064: modale della formula del nodo (inserisce o modifica; un passo di storico).
    void editFormula(MindNode *node);
    // FEAT-051 tappa 2: pagine Marcatore (0) e Adesivo (1) nella colonna del pannello laterale.
    // Apre la colonna su quella pagina (resta aperta senza selezione: un clic allora non fa nulla).
    class IconPage *showIconPanel(int page);
    class IconPage *iconPage(int page) const;
    bool isIconPanelShown() const;
    bool insertSticker(MindNode *node, const QString &path);
    bool insertSticker(const QList<MindNode *> &nodes, const QString &path);  // one step for all
    // FEAT-013: accende o spegne il bilanciamento del foglio attivo (un passo), come lo switch.
    void setBalanced(bool on);
    // FEAT-015: rilascio di un trascinamento con il bilanciamento attivo (un passo, con il ridisposto).
    void reorderNode(MindNode *node, MindNode *newParent, int row, int side);
    // + > Argomento mobile (richiesta dell'utente 2026-10-21): l'argomento segue il
    // cursore e un clic sulla mappa lo fissa li' (un passo); Esc, tasto destro o cambio di foglio
    // annullano senza modifiche. Vero mentre si sta scegliendo il posto.
    void startFloatingPlacement();
    bool isPlacingFloating() const { return m_placingFloating != nullptr; }
    void rotateVisual(MindNode *node);  // 90 gradi in senso orario (immagine, adesivo, formula)
    QSizeF fittedImageSize(const MindNode *node, const QString &path) const;  // immagini: larga come il nodo, almeno 200
    static constexpr qreal kMinInsertWidthPx = 200;
    void setMarker(MindNode *node, const QString &series, const QString &marker);
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void updateCornerMask();  // rounded corners, none when filling the screen
    void paintEvent(QPaintEvent *event) override;  // cornice della finestra (updateWindowFrame)
    bool eventFilter(QObject *watched, QEvent *event) override;
    // FEAT-071: Snap, Snap layouts and resizing from the edges on Windows (WindowFrame).
    bool event(QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void changeEvent(QEvent *event) override;  // FEAT-087: leaving full screen ends focus mode
    void keyPressEvent(QKeyEvent *event) override;  // FEAT-087: an Esc nobody used exits focus mode
private:
    void installWindowFrame();
    // FEAT-045 phase 2: one tool of an agent on the open map (McpBridge::Host): the text being
    // typed is confirmed first, the requested sheet is shown, a change is one Undo step with a
    // message and the topic selected.
    McpServer::ToolResult runAgentTool(const QString &tool, const QJsonObject &arguments);
    McpServer::ToolResult saveForAgent(const McpMapTools::Access &access, const QJsonObject &arguments);
    bool confirmForAgent(const QString &kind, const QString &subject);
    McpServer::ToolResult openDialogForAgent(const QJsonObject &arguments);  // FEAT-045: open_dialog
    McpServer::ToolResult openMapForAgent(const QJsonObject &arguments);     // FEAT-045: open_map, after the user's yes
    QString agentMessage(const QString &tool, const QJsonObject &arguments, const QJsonObject &result) const;
    // FEAT-087: full screen on the current screen, Mentis unchanged; off = the window as it was.
    void setFocusMode(bool on);
    void syncFocusModeControls();
    void createActions();
    void createMenusAndToolbar();
    void createDocks();
    void ensureInspector();  // the side panel, built after the first frame or on first use
    void ensureGanttView();  // the Gantt view, likewise
    QWidget *createHomePage();
    QWidget *createEditorPage();
    void refreshRecentFiles();
    QPixmap recentMapPreview(const QString &path) const;
    QPixmap templatePreview(int templateIndex) const;
    QWidget *homeCard(const QPixmap &picture, const QString &name, const std::function<void()> &open);  // FEAT-072 F5
    void showHome();
    void showEditor();
    bool chooseTemplate();
    // variant (FEAT-026 fase D): tipo e variante della galleria; vuota = la mappa mentale di sempre.
    void applyTemplate(int templateIndex, const QString &variant = QString());
    void chooseSheetVariant(const QString &variantId);  // FEAT-072 B16: from the map type card
    void rememberRecent(const QString &path);
    // FEAT-041: azione con scorciatoie dal registro (predefinite o scelte nelle Impostazioni).
    QAction *makeAction(const QString &id, ShortcutRegistry::Area area, const QString &text,
                        const QList<QKeySequence> &defaults, const std::function<void()> &slot);
    void addChild();
    void addSibling();
    void removeSelected();
    void editSelected(bool cursorAtEnd = false);
    void arrangeSelected(Document::Arrange arrange);  // FEAT-057: on the selected topic
    void addSelectMenu(QMenu *parent);  // FEAT-056: Select > in the topic menu and in the main menu
    void newMap();
    void openMap();
    bool saveMap();
    bool saveMapAs();
    void importMarkdown();
    void exportMarkdown();
    void exportImage();  // FEAT-054 stage 2: PNG and SVG (ExportDialog)
    void exportPdfOrPrint(bool print);  // FEAT-054 stage 3 (PageDialog)
    void exportPresentation();  // FEAT-063 stage C: the .pptx
    void exportOffice(Document::OfficeFormat format);  // FEAT-054 stage 4: Word and Excel
    bool exportGanttFile(GanttOffice::Format format);  // FEAT-083 stage C: true = handled (Gantt or cancelled)
    PageDialog::PitchSource pitchSource() const;  // the active sheet's slides, theme and ratio
    QString exportName(bool wholeDocument = false) const;  // "20261031T113911 Sheet 1" (FEAT-054)
    QString exportBranchId() const;  // the one selected topic, empty otherwise
    ExportDialog::PagesFor exportPages();  // sheets for the export dialogs, by Content
    void beginRelation();
    void addFloatingNode();
    void cancelFloatingPlacement();
    MindNode *nodeAfterRemoval(const QList<MindNode *> &nodes) const;  // vedi removeSelected
    void selectAfterRemoval(MindNode *node);
    class MapTypeSelect *m_sheetVariant = nullptr;       // FEAT-072 B16: map type card of the Map page
    void setStructure(MindNode *node, const QString &typeKey, const QString &directionKey);
    class QGraphicsPixmapItem *m_placingFloating = nullptr;  // anteprima che segue il cursore
    void addSummary();
    void addBoundary();
    void insertFormula();
    void showFormulaMenu(MindNode *node, const QPoint &screenPos);     // FEAT-064
    void chooseColor(const QString &part);
    void updateInspector(MindNode *node);
    void applyInspector();
    void refreshOutline();
    MindNode *outlineSelectedNode() const;
    QList<MindNode *> outlineSelectedNodes() const;
    void addOutlineSibling();
    void indentOutlineNode();
    void outdentOutlineNode();
    // Menu contestuale dell'outliner (tasto destro su una voce) e i suoi comandi.
    void showOutlineContextMenu(const QPoint &pos);
    void copyOutlineNode();
    void cutOutlineNode();
    void pasteOutlineNode();
    void duplicateOutlineNode();
    void deleteOutlineNode();
    void clearOutlineNodeText();
    void setOutlineMode(bool enabled);
    // FEAT-075: the workspace shows the map, the outliner or the Gantt plan of the active sheet.
    enum class WorkspaceView { Map, Outline, Gantt };
    void setWorkspaceView(WorkspaceView view);
    WorkspaceView workspaceView() const;
    QColor pageColor() const;
    void updateBottomBar();
    void updateWindowControlsForPanel(bool inPanel);
    void styleWindowControls(QWidget *controls, bool inPanel);
    void showSettings(bool shortcutsPage = false);
    // storePreview (2026-10-14): senza modifiche da salvare aggiorna l'anteprima della Home nel file.
    bool maybeSave(bool storePreview = true);
    bool writeFile(const QString &path, BmapStorage::SaveReason reason = BmapStorage::SaveReason::Explicit);
    bool readFile(const QString &path);
    // Il modello e' cambiato: passo di storico e salvataggio automatico (FEAT-066: Document).
    void setDirty();
    // FEAT-066: aggiornamenti dell'interfaccia dai segnali del Document.
    void onDirtyChanged(bool dirty);
    void syncSheetSelect();
    void onSheetShown();
    void onHistoryRestored(bool incremental);
    // FEAT-027: collegamenti dei nodi.
    void editTopicLink(MindNode *node);
    void editWebLink(MindNode *node);
    void followLink(MindNode *node, NodeItem::LinkKind kind);
    void showLinkMenu(MindNode *node, NodeItem::LinkKind kind, const QPoint &screenPos);
    // Passo di storico per i collegamenti cambiati, con l'eventuale altro foglio in attesa.
    void commitLinkStep();
    // Icone grigie per gli argomenti che non esistono piu' nei fogli letti (vedi FEAT-027).
    void refreshLinkStates();
    // Porta al nodo (anche in un altro foglio, letto se serve): selezionato e in vista, voci chiuse
    // dell'Outliner aperte fino a lui. Falso se il nodo non c'e' in nessun foglio.
    bool revealNode(const QString &nodeId);
    // A topic just added on the map: laid out, selected and scrolled into sight (user, 2026-11-05).
    void showCreatedNode(MindNode *created);
    void navigateLinkHistory(bool back);
    MindNode *nodeById(const QString &id) const;
    int sheetIndexOf(const QString &sheetId) const { return m_document->sheetIndex(sheetId); }
    void undo();
    void redo();
    void updateHistoryActions();
    void addSheet();
    void deleteSheet();  // FEAT-004: the active sheet, after a confirmation
    void duplicateSheet();  // FEAT-004: the active sheet, copied right after it and shown
    void renameSheet();  // FEAT-004: the active sheet, from a dialog with its name
    void openResources();  // FEAT-079: the Resources modal of the Gantt page
    void switchSheet(int index);
    QString currentSheetId() const { return m_document->currentSheetId(); }
    int sheetIndex(const QString &id) const { return m_document->sheetIndex(id); }
    // Conferma subito le modifiche di testo aperte (nodo della mappa, voce o titolo dell'outliner).
    void commitPendingEdits();
    // Ctrl+Z / Ctrl+Y quando l'ultimo passo è di un altro foglio: lampeggia la select dei fogli.
    void pointToSheet(const QString &sheetId, bool redo);
    // Anteprima JPEG del foglio attivo per la Home (480x272).
    QByteArray documentPreviewJpeg() const;
    QByteArray ganttPreviewJpeg() const;  // the Home's preview after the Gantt view
    void storeDocumentPreview();
    void applyPerformanceProfile();
    // Pannello laterale con apertura animata (2026-10-14, solo profilo Standard).
    void toggleInspector();
    // FEAT-017: contesto del pannello laterale (pagina e controlli di Stile) dalla selezione.
    QStringList selectedNodeIds() const;
    void updatePanelContext();
    void preparePanelForOpening();
    void showPanelPage(PanelContext::Page page);
    // FEAT-019: mostra nella pagina Stile i valori dei nodi selezionati (non scrive nulla).
    void refreshStylePage();
    void refreshPitchPage();  // FEAT-063: the Pitch tab on pitchTopic()
    MindNode *pitchTopic() const;
    void presentPitch();      // FEAT-063: Present (the top-right button, F5)
    class PitchPage *m_pitchPage = nullptr;
    QPointer<class PitchWindow> m_pitchWindow;  // the open presentation, if any
    void resizeSelectedImage(const QSizeF &size);  // FEAT-040
    void createPalette();                          // FEAT-032
    void addTaskBoxes(const QList<MindNode *> &nodes);            // FEAT-048
    void toggleTaskBoxes(const QList<MindNode *> &nodes);         // FEAT-048
    // FEAT-059: apre la nota del nodo accanto al nodo sulla mappa (o alla riga dell'outliner).
    void openNote(MindNode *node);
    QRect noteAnchor(const MindNode *node) const;
    void showTaskMenu(MindNode *node, const QPoint &screenPos);  // FEAT-048
    void showRelationMenu(const QString &id, const QPoint &screenPos);  // FEAT-053
    void showBoundaryMenu(const QString &id, const QPoint &screenPos);  // FEAT-052
    // FEAT-053/052: the pending relationship ends on the topic or boundary `targetId`.
    void finishRelation(const QString &targetId);
    void showNodeMenu(MindNode *node, const QPoint &screenPos);  // FEAT-042
    void buildMainMenu(QMenu *menu);                              // FEAT-072 F1
    QList<MindNode *> branchesWith(MindNode *node) const;
    void copyBranches(const QList<MindNode *> &nodes);
    void copyBranch(MindNode *node);
    void cutBranch(MindNode *node);
    MindNode *pasteBranchesAt(MindNode *parent, int row);
    void pasteIntoBranch(MindNode *node);
    bool pasteContentOnto(MindNode *node);  // FEAT-094
    bool pasteWhileEditing(MindNode *node, bool outlinerItem);
    void duplicateBranch(MindNode *node);
    void resetBranchStyle(MindNode *node);
    // FEAT-041 stage B: reference commands that Mentis has, with their keys (createTopicCommands).
    void createTopicCommands();
    void moveBranchToSheet(MindNode *node, int targetIndex);  // FEAT-029, -1 = a new sheet
    void moveBranchToNewSheet(MindNode *node);
    void zoomShownView(int deltaPercent);                    // map, outliner or Gantt; 0 = 100 %
    void openPanelPage(PanelContext::Page page);             // the side panel opened on the page
    void toggleCharStyle(RichTitle::CharStyle style);        // FEAT-095: on the whole text of the topics
    int charStyleKey(const QKeyEvent *event) const;          // FEAT-095: RichTitle::CharStyle of the key, -1 none
    void setSelectedStyle(StyleProperty property, const QVariant &value);
    void stepFontSize(bool larger);
    void setNumberingPreset(int preset);                     // Numbering::presetFormats, -1 = none
    void setTaskOfSelected(MindNode::Task state);
    void removeMarkersOfSelected();
    void removeVisualOfSelected();
    void removeLinksOfSelected();
    void toggleCollapseOfSelected();
    void toggleSubBranchesOfSelected();
    void showMenuKey(QAction *entry, const QString &commandId);  // Q4: the key after a tab, shown only
    // FEAT-086: on the one selected topic.
    void moveSelectedTopic(int direction);
    void addTopicBefore();
    void addParentTopic();
    // FEAT-085: Copy style / Paste style on the selected topics of the page shown, and the format
    // painter of the topic bar (sticky = it stays on after a paste, until Esc or a click elsewhere).
    void copyStyle();
    void pasteStyle();
    void copyStyleOf(const MindNode *node);
    void pasteStyleOn(const QList<MindNode *> &targets);
    QList<MindNode *> styleTargets() const;
    bool typingInField() const;
    void setFormatPainter(bool on, bool sticky = false);
    bool canPasteBranch() const;
    void setTypedTask(MindNode *node, int task);                 // FEAT-068
    // FEAT-060: riquadro "Etichette" accanto al nodo (mappa o riga dell'outliner) e menu della pillola.
    void openLabels(MindNode *node);
    void finishLabels(const QString &id, const QStringList &labels);
    void showLabelMenu(MindNode *node, const QString &label, const QPoint &screenPos);
    void applyBackdrop();                          // FEAT-036: sfondo della mappa e outliner
    void applyFloatingTone();
    QColor m_frameColor;          // sfondo della finestra (updateWindowFrame)
    bool m_frameMapEdge = false;  // a pannello chiuso: sfondo della mappa, senza bordo                      // gruppi flottanti nel tono dello sfondo visibile
    void applyDefaultTheme();                      // FEAT-036: tema delle mappe nuove
    void updateWindowFrame(bool panelVisible);     // FEAT-036: bordo e superficie dell'editor
    void stopInspectorAnimation();

    // FEAT-066: fogli, storico, file e salvataggio; possiede il modello del foglio attivo.
    Document *m_document;
    ShortcutRegistry *m_shortcuts;  // FEAT-041
    MindMapModel *m_model;
    MindMapScene *m_scene;
    MindMapView *m_view;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_homePage;
    bool m_homePainted = false;  // the first paint of the Home starts the deferred setup
    QWidget *m_editorPage;
    QWidget *m_outlinePage;
    QStackedWidget *m_workspacePages = nullptr;
    QGridLayout *m_recentGrid;
    QHBoxLayout *m_topBarLayout = nullptr;
    QHBoxLayout *m_inspectorTitleLayout = nullptr;
    QWidget *m_windowControls = nullptr;
    QWidget *m_panelWindowControls = nullptr;  // controlli fissi nell'intestazione del pannello (2026-10-14)
    // FEAT-071: the Maximize buttons of the top bar and of the panel; m_hotMaximize = the one Windows
    // last reported under the mouse. m_systemCorners: Windows 11 rounds the window, no mask.
    QList<QPointer<QToolButton>> m_maximizeButtons;
    QPointer<QToolButton> m_hotMaximize;
    bool m_systemCorners = false;
    McpBridge::Host *m_agentHost = nullptr;  // FEAT-045 phase 2: the channel of "Mentis --mcp"
    // FEAT-045: what an agent chose for the window it opens next (used once).
    std::optional<ExportDialog::Preset> m_agentImagePreset;
    std::optional<PageDialog::Preset> m_agentPagePreset;
    int m_agentSettingsPage = -1;
    UpdatePrompt *m_updatePrompt = nullptr;  // FEAT-090, created at the first use
    QPointer<ManualWindow> m_manual;          // FEAT-092
    class GuidedTour *m_tour = nullptr;       // FEAT-093
    bool m_focusMode = false;           // FEAT-087
    bool m_focusWasMaximized = false;   // window state to go back to
    QToolButton *m_focusButton = nullptr;
    WindowFrame::Hooks m_frameHooks;  // built once: nativeEvent sees every message (WM_NCHITTEST at each mouse move)
    QDockWidget *m_outlineDock = nullptr;
    QDockWidget *m_inspectorDock = nullptr;  // built by ensureInspector()
    // FEAT-061 / FEAT-072 C: left navigation panel (Outline, Notes, Tags with search), Ctrl+F.
    QDockWidget *m_navigationDock = nullptr;
    class NavigationPanel *m_navigationPanel = nullptr;
    class QTimer *m_navigationRefresh = nullptr;
    void showNavigationPanel();
    // Dark frame around the editor card when a side panel (left or right) is open.
    void applyEditorFrame(bool inspectorVisible);
    QToolBar *m_editorToolbar;
    QToolBar *m_leftRail = nullptr;
    QTreeView *m_outline;
    // Titolo principale dell'outliner: OutlineRootTitleEdit in mainwindow.cpp (2026-10-13, formati e barra come le voci).
    QPlainTextEdit *m_outlineRootEdit = nullptr;  // nullptr finché createDocks non lo crea: eventFilter lo legge prima
    std::function<void()> m_rootFormatBarUpdate;  // posiziona o nasconde la barra del titolo principale
    QLineEdit *m_titleEdit = nullptr;
    QPlainTextEdit *m_notesEdit = nullptr;
    QDoubleSpinBox *m_imageWidth = nullptr;
    QDoubleSpinBox *m_imageHeight = nullptr;
    QDoubleSpinBox *m_branchWidth = nullptr;
    QComboBox *m_branchStyle = nullptr;
    QLabel *m_nodeCountLabel = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    QComboBox *m_sheetCombo = nullptr;
    class TopicTools *m_topicTools = nullptr;  // FEAT-072 F3
    std::optional<MindMapModel::TopicStyle> m_copiedStyle;  // FEAT-085: for the session, any sheet or file
    bool m_formatPainter = false;
    bool m_formatPainterSticky = false;
    bool m_formatPainterJustPasted = false;  // the double click that follows a one-shot paste is swallowed
    // A rectangle dragged from an empty place with the painter on: the topics it touches get the style.
    std::optional<QPoint> m_formatPainterBandOrigin;  // viewport pixels
    QPointer<class QRubberBand> m_formatPainterBand;
    QMenu *m_mainMenu = nullptr;
    // FEAT-072 F5: Home (sizes measured live).
    static constexpr int kHomeMarginPx = 8;
    static constexpr int kHomeSidebarWidthPx = 212;
    static constexpr int kHomeCardWidthPx = 160;
    static constexpr int kHomeCardHeightPx = 100;
    static constexpr int kHomeColumns = 5;
    static constexpr int kHomeMaxRecent = 12;
    class QStackedWidget *m_homeStack = nullptr;
    class QLineEdit *m_homeSearch = nullptr;
    QToolButton *m_homeRecentRow = nullptr;
    QToolButton *m_homeTemplatesRow = nullptr;        // hamburger menu (FEAT-072 F1)
    QMenu *m_mapInsertMenu = nullptr;   // map "+" menu, reused as the topic menu's "Insert" (FEAT-042)
    QMenu *m_outlineAddMenu = nullptr;  // menu "+" dell'outliner, riusato come sottomenu "Inserisci"
    int m_outlineZoom = 100;  // zoom della pagina outliner, separato da quello della mappa
    QLabel *m_mapSubtitle = nullptr;
    QToolButton *m_saveStateButton = nullptr;
    QPushButton *m_outlineToggle = nullptr;  // first view button: Outliner or Mind map
    QPushButton *m_ganttToggle = nullptr;    // second: Gantt or Outliner (FEAT-075)
    QComboBox *m_ganttScale = nullptr;       // Days / Weeks / Months, in place of the zoom
    QWidget *m_ganttGroup = nullptr;         // top bar group of the Gantt: Critical path
    GanttView *m_ganttView = nullptr;
    MindNode *m_selected = nullptr;
    QString m_relationSource;  // id of the start topic or boundary while a relationship is being drawn
    // B4: Esc sulla mappa annulla la relazione in attesa; attiva solo dopo "Relazione".
    QAction *m_cancelRelationAction = nullptr;
    // P8 (2026-10-13): vero se la sessione precedente non si è chiusa regolarmente (crash,
    // spegnimento): solo allora l'apertura dei file fa il controllo di integrità completo.
    bool m_uncleanPreviousExit = false;
    bool m_refreshing = false;
    // Selezione prima di applicare un passo di storico, per ritrovarla dopo (P3, 2026-10-13).
    QString m_selectedBeforeRestore;
    // FEAT-027: cronologia dei salti fatti con i collegamenti (solo sessione, non salvata): Alt+<- e
    // Alt+->. Ogni voce e' un nodo (id) in un foglio.
    struct LinkPlace { QString sheetId; QString nodeId; };
    QList<LinkPlace> m_linkBack, m_linkForward;
    // Collegamento inverso in un altro foglio: stato di quel foglio da registrare nello stesso passo.
    std::optional<Document::LinkSheetChange> m_pendingLinkSheet;
    // Id dei nodi visti nei fogli letti in questa sessione: un argomento mai visto, con fogli ancora
    // da leggere, non si puo' dire eliminato (decisione 2026-10-16).
    QSet<QString> m_seenNodeIds;
    std::function<bool(const QUrl &)> m_openUrl;
    bool m_depthLimitPending = false;  // BUG-046: the depth message is waiting to be shown
    std::function<QString()> m_pickImageFile;
    std::function<QStringList()> m_pickAttachFiles;                 // FEAT-049
    void showMarkerMenu(MindNode *node, const QString &marker, const QPoint &screenPos);
    void showImageMenu(MindNode *node, const QPoint &screenPos);  // Rimuovi immagine/adesivo
    void removeVisual(MindNode *node);
    std::function<QString(const QString &)> m_pickAttachmentSave;   // FEAT-049
    // FEAT-049: copie temporanee aperte con un'altra applicazione -> nodo e contenuto di partenza.
    struct OpenedFile { QString nodeId; QString assetId; };
    QHash<QString, OpenedFile> m_openedFiles;
    class QFileSystemWatcher *m_openedFilesWatcher = nullptr;
    QTimer *m_openedFilesCheck = nullptr;
    bool m_askingOpenedFile = false;
    void chooseFilesToAttach(MindNode *node);
    void showFilesMenu(MindNode *node, const QPoint &screenPos);
    void saveNodeFileAs(MindNode *node, int index);
    void dropFiles(const QStringList &paths, const QPointF &scenePos);
    // FEAT-039: false se il file e' rifiutato (modale con il motivo) o se l'utente non conferma un
    // file molto grande.
    bool acceptFile(const QString &path, bool ok, const QString &reason, bool large, const QString &largeReason);
    bool setNodeImage(MindNode *node, const QString &path);
    std::function<QString()> m_pickMarkdownFile;
    std::function<QString()> m_pickOpenFile;
    std::function<QString()> m_pickSaveFile;
    std::function<QString()> m_pickExportFile;
    std::function<bool(OfficeExportDialog &)> m_chooseOfficeExport;
    SidePanel *m_sidePanel = nullptr;
    // FEAT-051 tappa 2: la colonna del pannello ospita lo stile (0) o marcatori e adesivi (1).
    class QStackedWidget *m_dockPages = nullptr;
    SidePanel *m_iconPanel = nullptr;
    class IconPage *m_markerPage = nullptr;
    class IconPage *m_stickerPage = nullptr;
    void toggleDockPage(int index);
    void updatePanelToggles();
    QList<MindNode *> iconTargetNodes() const;
    void chooseIcon(class IconPage *page, const QString &path, const QString &name, const QString &collectionId);
    void updateIconPanelSelection();
    StylePage *m_stylePage = nullptr;  // FEAT-019: pagina Stile attiva con uno o piu' nodi
    ImageStylePage *m_imageStylePage = nullptr;
    RelationshipStylePage *m_relationStylePage = nullptr;  // FEAT-053
    BoundaryStylePage *m_boundaryStylePage = nullptr;  // FEAT-052
    VariantPicker *m_variantPicker = nullptr;     // FEAT-036: miniature sotto "Tema colore"
    ColorField *m_backgroundField = nullptr;      // FEAT-036: "Colore sfondo"
    class ToggleSwitch *m_balanceSwitch = nullptr;  // FEAT-013: "Bilancia mappa"
    SheetStylePanel *m_sheetStylePanel = nullptr; // FEAT-043 parte stile
    NoteEditor *m_noteEditor = nullptr;           // FEAT-059
    LabelEditor *m_labelEditor = nullptr;         // FEAT-060
    NoteEditor *m_outlineNoteEditor = nullptr;    // FEAT-059: nota scritta sul posto nell'outliner
    void openOutlineNote(MindNode *node);
    void placeOutlineNote();
    void finishNote(const QString &id, const QString &plain, const QString &html);  // FEAT-040: al posto di m_stylePage con un'immagine scelta
    PaletteSelect *m_paletteSelect = nullptr;  // FEAT-031: "Tema colore" della pagina Mappa
    PanelContext m_panelContext;
    QList<QWidget *> m_styleControls;  // controlli della pagina Stile, attivi con un solo nodo
    static constexpr int kNavigationWidthPx = 300;  // left panel when it opens (FEAT-072 C6, to be checked)
    static constexpr int kInspectorMinimumWidth = 256;  // floor even with narrower content (FEAT-072 B1: reference ~276 with scrollbar)
    // BUG-010: il pannello non scende sotto la larghezza del suo contenuto (pagina piu' larga).
    int inspectorMinimumWidth() const;
    void applyInspectorMinimumWidth();
    int m_inspectorWidth = kInspectorMinimumWidth;      // larghezza a cui arriva l'animazione
    bool m_inspectorAnimating = false;
    // True while the side panel opens: each narrowing of the map view keeps the selected node in
    // sight beside the panel, as if the map moved with it (user, 2026-11-05).
    bool m_inspectorFollowsSelection = false;
    void keepSelectionBesidePanel();
    QPointer<QVariantAnimation> m_inspectorAnimation;
    // Titolo della radice nell'outliner con formule: disegnato quando il campo non
    // è in modifica (in modifica si vede il testo LaTeX da correggere).
    FormulaText m_rootFormula;
    void updateRootFormula();
    void updateMapSubtitle();
    void applyOutlineZoom(int percent);
    QList<QWidget *> m_mapToolbarButtons;
    QList<QWidget *> m_outlineToolbarButtons;
    QHash<QString, QAction *> m_actions;
    QStringList m_mapCommands;     // FEAT-041 stage B: on the map view only (Q3)
    QStringList m_windowCommands;  // FEAT-041 stage B: on the window, every page
};

