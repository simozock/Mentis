#pragma once

#include "maplibrary.h"
#include "mapsnapshot.h"
#include "mindmapmodel.h"  // MindMapModel::TopicStyle, kept by value
#include "reordertarget.h"

#include <QColor>
#include <QObject>
#include <QPointF>
#include <QQuickTextDocument>  // complete for the invokables' meta types
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>

class Document;
struct MindNode;

// FEAT-097: the map open in the mobile app, for QML. It owns the Document (FEAT-005: the same
// file, storage, sheets and history as the desktop) and the drawn form of the active sheet
// (MapSnapshot), made again when the file, the sheet or the content changes.
//
// Stage B1: every edit goes through Document, then the sheet is laid out again (the snapshot writes
// the positions into the model) and the history step is closed, so one Undo takes back the edit
// with its layout, as on the desktop (MainWindow::showCreatedNode, reorderNode). Document saves
// automatically after the profile's delay; close() and save() write what is still pending.
class MobileMap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY opened)
    Q_PROPERTY(QString title READ title NOTIFY opened)
    Q_PROPERTY(QStringList sheets READ sheets NOTIFY sheetsChanged)
    Q_PROPERTY(int currentSheet READ currentSheet NOTIFY sheetsChanged)
    Q_PROPERTY(int topicCount READ topicCount NOTIFY contentChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    // A passing note for the status line (an Undo step in another sheet); empty = none.
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    // Time of the last open (file read, measures and layout of the first sheet), ms; -1 = none yet.
    Q_PROPERTY(int openMs READ openMs NOTIFY opened)
    // The selected topic (MindNode::id); empty = none.
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedTitle READ selectedTitle NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedIsCentral READ selectedIsCentral NOTIFY selectionChanged)
    // The selected topic has branches to hide or show (not the central topic, as on the desktop).
    Q_PROPERTY(bool canCollapse READ canCollapse NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedCollapsed READ selectedCollapsed NOTIFY selectionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    // True while the page asks a topic's title: the step stays open, so a new topic and its title
    // are one Undo step (the desktop keeps it open while the title is typed, BUG-039). Set to false,
    // the step closes.
    Q_PROPERTY(bool editingTitle READ editingTitle WRITE setEditingTitle NOTIFY editingTitleChanged)
    // FEAT-085's format painter: armed after copying a style; sticky = it stays armed after a paste.
    Q_PROPERTY(bool formatPainter READ formatPainter NOTIFY formatPainterChanged)
    Q_PROPERTY(bool formatPainterSticky READ formatPainterSticky NOTIFY formatPainterChanged)
    // The image formats Qt reads here, for the picker (FileValidation::imageFilter).
    Q_PROPERTY(QStringList imageFilters READ imageFilters CONSTANT)
    // The selected topic's markers (MindNode::markers), for the active cells of the marker page.
    Q_PROPERTY(QStringList selectedMarkers READ selectedMarkers NOTIFY selectionChanged)
    // B3: the selected topic has a formatted title (MindNode::titleHtml, not a formula), which the
    // title field edits as rich text (loadTitle, renameFrom); otherwise it stays plain (rename).
    Q_PROPERTY(bool selectedTitleRich READ selectedTitleRich NOTIFY selectionChanged)
    // FEAT-106: the selected topic's picture (image, sticker or formula) is chosen, as the desktop's
    // click on it: Remove takes off only the picture, a drag moves it around the title.
    Q_PROPERTY(bool pictureSelected READ pictureSelected NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedHasNote READ selectedHasNote NOTIFY selectionChanged)
    // FEAT-108: the selected topic's checkbox (FEAT-048) and web link (FEAT-027; empty = none).
    Q_PROPERTY(bool selectedHasCheckbox READ selectedHasCheckbox NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedWebLink READ selectedWebLink NOTIFY selectionChanged)
    // FEAT-116: the selected topic's formula content (LaTeX code without $), empty without one.
    Q_PROPERTY(QString selectedFormula READ selectedFormula NOTIFY selectionChanged)
    // Its background (MindNode::formulaBackground), white without a formula.
    Q_PROPERTY(QColor selectedFormulaBackground READ selectedFormulaBackground NOTIFY selectionChanged)
    // FEAT-110: the selected relationship (Relationship::id; empty = none) and its label. A topic and
    // a relationship are never selected together.
    Q_PROPERTY(QString selectedRelationId READ selectedRelationId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedRelationLabel READ selectedRelationLabel NOTIFY selectionChanged)
    // FEAT-110: the Relationship tool armed from the selected topic (one tap on another topic makes
    // one relationship), kept on after each one when relationToolKept.
    Q_PROPERTY(bool relationTool READ relationTool NOTIFY relationToolChanged)
    Q_PROPERTY(bool relationToolKept READ relationToolKept NOTIFY relationToolChanged)
    // FEAT-115: the selected boundary (Boundary::id; empty = none) and its title. A boundary is
    // selected alone, as a relationship.
    Q_PROPERTY(QString selectedBoundaryId READ selectedBoundaryId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedBoundaryTitle READ selectedBoundaryTitle NOTIFY selectionChanged)
    // FEAT-111: the multiple selection mode (a tap adds or removes a topic) and the topics it holds,
    // in the order they were added; selectedId is the last one added that is still selected.
    Q_PROPERTY(bool multiSelect READ multiSelect NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedIds READ selectedIds NOTIFY selectionChanged)
    // FEAT-111: Delete in the mode removes something (a topic other than the central one).
    Q_PROPERTY(bool canRemoveSelection READ canRemoveSelection NOTIFY selectionChanged)
    // FEAT-111: a style has been copied (Copy style or the format painter), so Paste style works.
    Q_PROPERTY(bool canPasteStyle READ canPasteStyle NOTIFY copiedStyleChanged)
    // FEAT-114: the selected topic's tags (MindNode::labels) and the app setting that sorts them
    // (Labels::autoSort, the desktop's LabelEditor switch).
    Q_PROPERTY(QStringList selectedLabels READ selectedLabels NOTIFY selectionChanged)
    Q_PROPERTY(bool labelsAutoSort READ labelsAutoSort WRITE setLabelsAutoSort NOTIFY labelsAutoSortChanged)
    // FEAT-114: the selected topic's attached files (FEAT-049), each {name, size} (size already
    // written for people, QLocale::formattedDataSize), in the order they were added.
    Q_PROPERTY(QVariantList selectedFiles READ selectedFiles NOTIFY selectionChanged)
    // FEAT-114: the selected topic's link to another topic (FEAT-027: the target's MindNode::id,
    // empty = none), and whether Back returns to where a jump along a link started.
    Q_PROPERTY(QString selectedTopicLink READ selectedTopicLink NOTIFY selectionChanged)
    Q_PROPERTY(bool canFollowBack READ canFollowBack NOTIFY linkHistoryChanged)

public:
    explicit MobileMap(QObject *parent = nullptr);
    ~MobileMap() override;

    bool isOpen() const { return !m_path.isEmpty(); }
    QString title() const;
    QStringList sheets() const;
    int currentSheet() const;
    int topicCount() const { return m_snapshot ? int(m_snapshot->topics().size()) : 0; }
    QString lastError() const { return m_lastError; }
    QString message() const { return m_message; }
    int openMs() const { return m_openMs; }
    const MapSnapshot *snapshot() const { return m_snapshot.get(); }
    QString selectedId() const { return m_selectedId; }
    QString selectedTitle() const;
    bool selectedIsCentral() const;
    bool canCollapse() const;
    bool selectedCollapsed() const;
    bool canUndo() const;
    bool canRedo() const;
    bool isDirty() const;
    bool editingTitle() const { return m_editingTitle; }
    bool formatPainter() const { return m_formatPainter; }
    bool formatPainterSticky() const { return m_formatPainter && m_formatPainterSticky; }
    QStringList imageFilters() const;
    QStringList selectedMarkers() const;
    bool selectedTitleRich() const;
    bool pictureSelected() const;
    bool selectedHasNote() const;
    bool selectedHasCheckbox() const;
    QString selectedWebLink() const;
    QString selectedFormula() const;
    QColor selectedFormulaBackground() const;
    QString selectedRelationId() const { return m_selectedRelationId; }
    QString selectedRelationLabel() const;
    const MapSnapshot::Relation *selectedRelation() const;
    QString selectedBoundaryId() const { return m_selectedBoundaryId; }
    QString selectedBoundaryTitle() const;
    const MapSnapshot::BoundaryShape *selectedBoundary() const;
    bool relationTool() const { return !m_relationSource.isEmpty(); }
    bool relationToolKept() const { return relationTool() && m_relationKept; }
    bool multiSelect() const { return m_multiSelect; }
    QStringList selectedIds() const;
    bool canRemoveSelection() const;
    bool canPasteStyle() const { return m_copiedStyle.has_value(); }
    QStringList selectedLabels() const;
    bool labelsAutoSort() const;
    void setLabelsAutoSort(bool on);
    QVariantList selectedFiles() const;
    QString selectedTopicLink() const;
    bool canFollowBack() const { return !m_linkBack.isEmpty(); }
    // Where the picked images and files are copied until the map embeds them (the app's cache).
    QString pickedImagesFolder() const;
    void setEditingTitle(bool editing);
    // The drawn topic of the selection, or null.
    const MapSnapshot::Topic *selectedTopic() const;
    Document *document() const { return m_document.get(); }

    // Opens a local .mentis path; false with lastError when the file is not a map or cannot be read
    // (the message of Document::open, the same as on the desktop).
    Q_INVOKABLE bool open(const QString &path);
    // Opens a map chosen with the system picker or listed by the library, through its local copy
    // (MapLibrary::localCopy); a new copy that is not a map is removed again.
    Q_INVOKABLE bool openUrl(const QUrl &url, MapLibrary *library);
    Q_INVOKABLE bool switchSheet(int index);
    // FEAT-112: the desktop's sheet commands (FEAT-004), none of them a history step. Each first
    // ends the format painter, the relationship tool and the selection mode, as switchSheet.
    // addSheet adds the next "Sheet N" (a laid-out Mind Map) and shows it.
    Q_INVOKABLE void addSheet();
    // The active sheet's name; false when empty or unchanged (the name stays).
    Q_INVOKABLE bool renameSheet(const QString &name);
    // "<name> (copy)" right after the active sheet, shown.
    Q_INVOKABLE bool duplicateSheet();
    // The active sheet and its Undo steps; the previous one is shown. False with one sheet.
    Q_INVOKABLE bool deleteSheet();
    // Moves a sheet to position `to`; the active sheet stays the same.
    Q_INVOKABLE bool moveSheet(int from, int to);
    // FEAT-113: the topics the search finds (NodeSearch, the desktop's Outline tab, FEAT-061), in
    // tree order; allSheets: every sheet, each one's results after a row with its name. fields:
    // NodeSearch::Field bits. Each row: header (the sheet's name row), text, ranges (start and length
    // pairs to highlight), html (the text escaped, the found parts in the desktop's highlight colours),
    // sheet (index), id (MindNode::id). An empty query finds nothing.
    Q_INVOKABLE QVariantList search(const QString &query, bool allSheets, bool wholeWords, bool matchCase, int fields);
    // FEAT-113: shows a found topic as the desktop's revealNode: its sheet, the collapsed branches
    // over it opened (one Undo step), selected. Ends the tools as switchSheet; false when it is gone.
    Q_INVOKABLE bool reveal(int sheet, const QString &id);
    // Writes the pending changes and closes; false with lastError when the save fails (the map stays
    // open, so nothing is lost). An unchanged map writes nothing.
    Q_INVOKABLE bool close();
    // After a failed close, on the user's choice: closes without writing again. The file keeps
    // what was saved last.
    Q_INVOKABLE void closeWithoutSaving();
    // Writes the pending changes now (the app goes to the background, where Android may end it).
    Q_INVOKABLE bool save();

    // Selection: the topic at a map point (none when the point is on the background); on the
    // topic's picture the picture too (FEAT-106). FEAT-110: off the topics, the relationship whose
    // line is within `relationTolerance` map px, or whose label holds the point. FEAT-115: then the
    // boundary there (MapSnapshot::boundaryAt, the same tolerance along its border).
    void selectAt(const QPointF &mapPoint, qreal relationTolerance = 0);
    Q_INVOKABLE void select(const QString &id);
    // Edits of the selection; false when nothing changes. The new topic is selected.
    Q_INVOKABLE bool addChild();
    Q_INVOKABLE bool addSibling();
    Q_INVOKABLE bool rename(const QString &title);
    // B3, a formatted title (selectedTitleRich): loadTitle puts it in the field's document as the
    // desktop's InlineTitleEditor does (the topic's font holds); renameFrom applies the field's text
    // and formats, with the word being composed (preedit) typed at `cursor`, as editTitle. Nothing
    // changes until the field's text is edited.
    Q_INVOKABLE void loadTitle(QQuickTextDocument *field);
    Q_INVOKABLE bool renameFrom(QQuickTextDocument *field, const QString &preedit, int cursor);
    // B3, the selected topic's note (FEAT-059): loadNote puts it in the field's document (its
    // formatted form, or the plain text); saveNote writes the field back as the desktop's
    // NoteEditor does (empty = note removed), the layout in one Undo step; false when the field was
    // not edited since it was loaded or last written, or nothing changes.
    Q_INVOKABLE void loadNote(QQuickTextDocument *field);
    Q_INVOKABLE bool saveNote(QQuickTextDocument *field);
    // The selected topic with its branch (not the central topic), or only its picture while the
    // picture is selected (FEAT-106, as the desktop's Delete), the layout in one Undo step; or the
    // selected relationship (FEAT-110), or the selected boundary with the layout (FEAT-115), one
    // Undo step.
    Q_INVOKABLE bool removeSelected();
    // FEAT-115, MainWindow::addBoundary: boundaries over the selected topics (every one in the
    // mode), grouped by parent and side (Document::addBoundaries); the first new one is selected, the
    // layout in one Undo step. Refused with the desktop's reason in `message`.
    Q_INVOKABLE bool addBoundary();
    // FEAT-115: the selected boundary's title, as BoundaryItem::finishEditing (simplified; empty =
    // no tab), the layout in one Undo step; false when nothing changes.
    Q_INVOKABLE bool renameBoundary(const QString &title);
    // FEAT-115 step 2, MainWindow::addSummary: summaries over the selected topics, grouped as the
    // boundaries (Document::addSummaries); the multiple selection ends and the first new summary
    // topic is selected, the layout in one Undo step (kept open while editingTitle, so the title
    // typed next joins it). Refused with the desktop's reason in `message`. A summary topic is
    // removed with its summary by removeSelected (Document::removeNodes).
    Q_INVOKABLE bool addSummary();
    // FEAT-110: the selected relationship's label, as RelationshipItem::finishEditing (simplified;
    // empty = no label), one Undo step; false when nothing changes.
    Q_INVOKABLE bool renameRelation(const QString &label);
    // FEAT-110: the selected relationship's handle at the start (fromHandle) or at the target moved
    // to a map point, as RelationshipItem's handle drag (an offset from that element's centre), one
    // Undo step; false when nothing changes.
    bool bendRelation(bool fromHandle, const QPointF &control);
    // FEAT-110, as Copy style: startRelation arms the Relationship tool from the selected topic
    // (keep: it stays on after each relationship); stopRelation turns it off. Arming one of the two
    // tools turns the other off.
    Q_INVOKABLE void startRelation(bool keep);
    Q_INVOKABLE void stopRelation();
    // A tap while the tool is on: on another topic one relationship from the start (one Undo step,
    // the selection stays), then off unless kept; on the start nothing; on the background off.
    // FEAT-115: off the topics a boundary there (MapSnapshot::boundaryAt with `tolerance`) is the target too.
    void relateAt(const QPointF &mapPoint, qreal tolerance = 0);
    // FEAT-111: startMultiSelect turns the mode on with the selected topic as the first of the
    // selection, or empty with nothing selected (the format painter and the Relationship tool end); stopMultiSelect ends it keeping
    // only selectedId selected. toggleInSelection adds the topic or removes it (the mode ends with
    // the last one); multiSelectAt does it for the topic at a map point, and ends the mode on the
    // background. In the mode removeSelected, insertSticker, toggleMarker, insertImage, toggleCheckbox
    // and pasteStyle act on every selected topic in one Undo step; any other way of selecting another
    // topic ends the mode.
    Q_INVOKABLE void startMultiSelect();
    Q_INVOKABLE void stopMultiSelect();
    Q_INVOKABLE void toggleInSelection(const QString &id);
    void multiSelectAt(const QPointF &mapPoint);
    // FEAT-106: the selected topic's note and its icon taken off, the layout in one Undo step.
    Q_INVOKABLE bool removeNote();
    // FEAT-106 (FEAT-037's drag): the picture of topic `id` dropped at a map point goes to the zone
    // there (NodeGeometry::imageDropZoneAt), as on the desktop only for a topic with a title; the
    // layout in one Undo step, the picture stays selected. False when nothing changes.
    bool placePicture(const QString &id, const QPointF &mapPoint);
    // FEAT-106 (NodeItem's handles, FEAT-037): the selected picture at a new size (map px), a too
    // narrow hand width widened, the layout in one Undo step; the picture stays selected. False when
    // nothing changes.
    bool resizePicture(const QSizeF &size);
    // FEAT-106, the topic bar's Copy with the picture selected: the original image or sticker (not
    // a formula) on the system clipboard. Android's clipboard takes no images
    // through Qt: there the image is written into the app's files and shared by its content:// URI
    // (Qt's FileProvider), which other apps paste. Says so in `message`; false when nothing is copied.
    Q_INVOKABLE bool copyPicture();
    // FEAT-116, the desktop's FormulaDialog: the selected topic's formula set from the code (a new
    // or changed code at its natural size, FormulaText::boxSize; an image or sticker replaced), or
    // removed; the layout in one Undo step. False when nothing changes. copyFormulaCode puts the
    // code on the clipboard as text and says so in `message`.
    Q_INVOKABLE bool setFormula(const QString &code);
    Q_INVOKABLE bool removeFormula();
    Q_INVOKABLE bool copyFormulaCode();
    // The system's fixed-pitch family (QFontDatabase::FixedFont), for the formula's code field.
    Q_INVOKABLE static QString fixedFontFamily();
    // FEAT-106, a tap on a marker (MainWindow::showMarkerMenu): the values of its series
    // ({marker, label, checked}; none for an own marker, whose group has no values), then
    // setMarker puts a value on the selected topic and removeMarker takes the series off; one
    // Undo step with the layout each.
    Q_INVOKABLE QVariantList markerChoices(const QString &marker) const;
    Q_INVOKABLE bool setMarker(const QString &marker);
    Q_INVOKABLE bool removeMarker(const QString &marker);
    Q_INVOKABLE bool toggleCollapsed();
    // FEAT-108, as MainWindow::toggleTaskBoxes: an empty checkbox on the selected topics without one,
    // or off from all when every one has it; the layout in one Undo step.
    Q_INVOKABLE bool toggleCheckbox();
    // FEAT-108, a tap on the checkbox (Document::toggleTask): ticked or unticked, one Undo step.
    Q_INVOKABLE bool toggleTaskDone();
    // FEAT-108, the desktop's WebLinkDialog: the address as it is stored (NodeLinks::webAddress:
    // http, https or mailto, "https://" added), empty when it is not allowed.
    Q_INVOKABLE static QString webAddress(const QString &typed);
    // The selected topic's web link set from a typed address (false when it is not allowed or
    // unchanged) or removed; the layout in one Undo step.
    Q_INVOKABLE bool setWebLink(const QString &typed);
    Q_INVOKABLE bool removeWebLink();
    // A tap on the link's button, as MainWindow::followLink: the page in the system's browser
    // (QDesktopServices); a link that cannot be opened says so in `message`.
    Q_INVOKABLE bool openWebLink();
    // FEAT-114, the desktop's LabelEditor (FEAT-060): the tags of every sheet read so far
    // (Document::documentLabels); the field's rules (Labels::suggestions, Labels::withAdded); the
    // field's tags on the selected topic (cleaned, sorted when labelsAutoSort), the layout in one
    // Undo step; false when nothing changes.
    Q_INVOKABLE QStringList documentLabels() const;
    Q_INVOKABLE static QStringList labelSuggestions(const QString &field, const QStringList &all);
    Q_INVOKABLE static QString labelsWithAdded(const QString &field, const QString &label);
    Q_INVOKABLE bool setLabels(const QString &field);
    // FEAT-114, the desktop's attached files (FEAT-049). pickFile copies a picked file into
    // pickedImagesFolder and checks it as MainWindow::attachFiles (FileValidation::anyFile):
    // {path, ok, reason, large, largeReason}, the texts of pickImage. attachFiles adds the checked
    // copies to the selected topic, the layout in one Undo step (embedded at the next save).
    Q_INVOKABLE QVariantMap pickFile(const QUrl &url);
    Q_INVOKABLE bool attachFiles(const QStringList &paths);
    // The selected topic's file `index` taken off, the layout in one Undo step.
    Q_INVOKABLE bool removeFile(int index);
    // A program or a script (FileValidation::isRisky): the page asks before opening it.
    Q_INVOKABLE bool fileIsRisky(int index) const;
    // A copy of the file (FileValidation::diskFileName, in the app's files: Android shares only
    // them) opened in the app the system picks (openFile, read only: edits do not come back) or
    // handed to the system's share sheet (shareFile; elsewhere its folder is shown). A file that
    // cannot be read or opened is said in `message`; false then.
    Q_INVOKABLE bool openFile(int index);
    Q_INVOKABLE bool shareFile(int index);
    // Where those copies go; emptied when a map is opened (another app may still read one after
    // the map is closed).
    QString openedFilesFolder() const;
    // FEAT-114, the desktop's TopicLinkDialog and MainWindow::editTopicLink: the selected topic
    // linked to topic `id` of sheet `sheet` (a search result) and, with alsoBack, that topic to it
    // when it has no topic link yet (topicHasLink); both in one Undo step, also across sheets.
    // False for the topic itself or when nothing changes.
    Q_INVOKABLE bool topicHasLink(int sheet, const QString &id);
    Q_INVOKABLE bool setTopicLink(int sheet, const QString &id, bool alsoBack);
    Q_INVOKABLE bool removeTopicLink();
    // A tap on the chain, as MainWindow::followLink: the target shown as a search result (reveal);
    // where the jump started is kept for followBack (the desktop's Alt+Left), which goes back
    // skipping places deleted since. Any other selection of a topic forgets those places, so Back
    // returns along links only right after the jumps. A link whose topic is gone says so in
    // `message`.
    Q_INVOKABLE bool followTopicLink();
    Q_INVOKABLE bool followBack();
    // FEAT-051 as MainWindow::insertSticker: the sticker (a path of StickerLibrary) in the selected
    // topic's image place, at its insertion size, with the layout in one Undo step; false with
    // lastError when the file is not a readable picture.
    Q_INVOKABLE bool insertSticker(const QString &path);
    // B2 (FEAT-015's rules, ReorderTarget): where the topic `id` dropped at a map point would go,
    // on the drawn sheet. Kind::None also for the central topic; Float is returned as on the
    // desktop, and moveTopic does nothing with it (no floating topics on the phone).
    ReorderTarget moveTarget(const QString &id, const QPointF &mapPoint) const;
    // Moves the topic there with its branch, the layout in one Undo step, and selects it; false
    // when the drop has no effect.
    bool moveTopic(const QString &id, const QPointF &mapPoint);
    // FEAT-085 on the phone. startFormatPainter copies the selected topic's style and arms the
    // painter (sticky: it stays armed after each paste); stopFormatPainter disarms it.
    Q_INVOKABLE void startFormatPainter(bool sticky);
    Q_INVOKABLE void stopFormatPainter();
    // A tap while the painter is armed: on a topic its style is pasted (one Undo step, the
    // selection stays) and a one-shot painter disarms; on the background it disarms.
    void paintAt(const QPointF &mapPoint);
    // FEAT-037 on the phone, the topic bar's Image. pickImage copies a picked file (a content://
    // URI or a local file) into pickedImagesFolder and checks it as the desktop does:
    // {path, ok, reason, large, largeReason}; reason and largeReason already say the file's name.
    Q_INVOKABLE QVariantMap pickImage(const QUrl &url);
    // Puts a checked picture in the selected topics as MainWindow::setNodeImage (size fitted to the
    // topic, a too narrow hand width widened), with its layout in one Undo step.
    Q_INVOKABLE bool insertImage(const QString &path);
    // FEAT-051 on the phone, the marker page: as MainWindow::chooseIcon, the item's marker on the
    // selected topic (one per series), or off when the topic has it; one Undo step with the layout.
    Q_INVOKABLE bool toggleMarker(const QString &path, const QString &name, const QString &collectionId);
    // Add in the markers and stickers modal: pickLibraryFile copies a picked file and checks it as
    // IconPage::checkedFiles ({ok, path, reason, small, smallReason, name, title}: name prefills the
    // name, title asks it); addToLibrary puts it in the first own collection (made when missing),
    // under that name, and into the selected topic.
    Q_INVOKABLE QVariantMap pickLibraryFile(const QUrl &url, bool marker);
    Q_INVOKABLE bool addToLibrary(const QString &path, const QString &name, bool marker);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    // The desktop's keys on a hardware keyboard (FEAT-097, ShortcutCatalog), as MainWindow runs
    // them. selectNeighbour: the topic drawn that way (Qt::Key_Left, Right, Up, Down; MapNavigation);
    // with none selected from the central topic, which is selected when there is none that way;
    // false when nothing changes.
    Q_INVOKABLE bool selectNeighbour(int key);
    Q_INVOKABLE void selectCentral();
    // Alt+Up / Alt+Down (FEAT-086): the topic among its siblings, the layout in one Undo step.
    Q_INVOKABLE bool moveAmongSiblings(int direction);
    // Shift+Enter and Ctrl+Enter (FEAT-086): a sibling before the selected topic, a parent over it;
    // the new topic is selected.
    Q_INVOKABLE bool addTopicBefore();
    Q_INVOKABLE bool addParentTopic();
    // FEAT-085's keys: copyStyle keeps the selected topic's style (as the painter's copy),
    // pasteStyle puts it on the selected topic in one Undo step.
    Q_INVOKABLE void copyStyle();
    Q_INVOKABLE bool pasteStyle();
    // As the desktop's Ctrl+C and Ctrl+V on the map (MainWindow::copyBranches, pasteIntoBranch):
    // copyBranches puts the selected topics' branches on the clipboard (every selected topic in the
    // mode, FEAT-111); pasteBranches puts the copied branches, or text read as Markdown, under the
    // selected topic (the last one tapped in the mode) as its last children, one Undo step, and
    // selects the first new topic. The clipboard is read only on Paste: Android tells the user each
    // time an app reads it. Android's clipboard keeps only text, HTML and URLs, so the branches
    // themselves (markers, pictures, notes, style) are kept here too: Paste uses them while the
    // clipboard still holds the text of that copy.
    Q_INVOKABLE bool copyBranches();
    Q_INVOKABLE bool pasteBranches();

signals:
    void opened();
    void sheetsChanged();
    // A new sheet is shown (open, sheet switch, close): the canvas fits it.
    void snapshotChanged();
    // The same sheet drawn again after an edit or an Undo step: the view stays where it is.
    void contentChanged();
    void selectionChanged();
    void historyChanged();
    void lastErrorChanged();
    void messageChanged();
    void editingTitleChanged();
    void formatPainterChanged();
    void relationToolChanged();
    void copiedStyleChanged();
    void labelsAutoSortChanged();
    void linkHistoryChanged();

private:
    void setError(const QString &error);
    void setMessage(const QString &message);
    // newSheet: another sheet is shown (snapshotChanged, the canvas fits it).
    void rebuildSnapshot(bool newSheet, bool writePositions = false);
    // FEAT-112: before a sheet command, as switchSheet; after one that shows another sheet.
    void endSheetTools();
    void showOtherSheet();
    void closeDocument();
    // Lays the sheet out again after an edit, closes the history step and selects `selected`.
    void finishEdit(const QString &selected);
    bool applyStep(bool redo);
    MindNode *selectedNode() const;
    // The topics the commands act on: every selected one in the mode (FEAT-111), else the selected one.
    QList<MindNode *> targetNodes() const;
    void setCopiedStyle(const MindMapModel::TopicStyle &style);
    // A picked file (a content:// URI or a local file) copied into pickedImagesFolder under its own
    // name; empty with error on failure. name: the picked file's name.
    QString copyPicked(const QUrl &url, QString *name, QString *error) const;
    // FEAT-114: the selected topic's file `index` copied into openedFilesFolder; empty with the
    // reason in `message`.
    QString openedCopy(int index);
    // FEAT-114: the sheet holding topic `id` (the active one first; others read from the file when
    // needed), -1 when none does; and the selection of a found place, kept by the link history.
    int sheetOfTopic(const QString &id);
    bool revealFollowed(int sheet, const QString &id);
    void clearLinkHistory();
    // FEAT-114: the topics of the drawn sheet whose topic link leads nowhere (MainWindow::
    // refreshLinkStates: a sheet not read yet is not checked), for the grey chain.
    void markBrokenLinks();
    void setSelected(const QString &id);
    void setSelectedRelation(const QString &id);  // FEAT-110: clears the topic's selection
    void setSelectedBoundary(const QString &id);  // FEAT-115: the same
    // FEAT-108: after setLinks, the change and its layout in one Undo step, saved automatically.
    void linkEdited(const MindNode *node);

    std::unique_ptr<Document> m_document;
    std::unique_ptr<MapSnapshot> m_snapshot;
    QString m_path;
    QString m_lastError;
    QString m_message;
    QString m_selectedId;
    QString m_selectedRelationId;  // FEAT-110
    QString m_selectedBoundaryId;  // FEAT-115
    QString m_relationSource;      // FEAT-110: the armed tool's start topic; empty = off
    bool m_relationKept = false;
    QByteArray m_copiedBranches;    // Document::branchesData of the last copy
    QString m_copiedBranchesText;   // and its text, as put on the clipboard
    bool m_multiSelect = false;     // FEAT-111
    QStringList m_multiSelection;   // FEAT-111: in the order added
    int m_openMs = -1;
    bool m_editingTitle = false;
    bool m_formatPainter = false;
    bool m_formatPainterSticky = false;
    bool m_pictureSelected = false;  // FEAT-106; pictureSelected() checks the picture is still there
    // FEAT-114: where the jumps along links started ({sheet id, topic id}), the last one last;
    // m_following while a jump or a return selects its topic.
    QList<std::pair<QString, QString>> m_linkBack;
    bool m_following = false;
    // The copied style lives while the app runs (FEAT-085: the session), across maps and sheets.
    std::optional<MindMapModel::TopicStyle> m_copiedStyle;
};
