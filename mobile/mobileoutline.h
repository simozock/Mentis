#pragma once

#include <QAbstractListModel>
#include <QQuickTextDocument>  // complete for the invokables' meta types
#include <QList>
#include <QPointer>
#include <QSet>
#include <QColor>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class MobileMap;
class QTextDocument;
struct MindNode;

// FEAT-121: the active sheet as the desktop's outliner shows it (FEAT-003), for the phone's list.
// One row per topic under the central topic, in tree order, without the branches folded in the
// outline; the central topic is the title above the rows (root*). The floating topics come last
// (they are the central topic's last children), the first one with the desktop's line above it.
//
// Folding belongs to the outline, not to the map (MindNode::collapsed is the map's, FEAT-016): as
// the desktop's tree, every branch is open when a sheet is shown, folding is no Undo step and is not
// saved. The selected topic's branch is opened when it lands inside a folded one (the desktop's
// expand after Indent and Find).
//
// FEAT-121 D: a branch may be entered (focusOn, as Workflowy's zoom): its topic is then the title
// above the rows (head*) and its branch the rows, depths counted from it; crumbs are the way back.
// As the folding it belongs to the outline: not saved, no Undo step, reset with another sheet.
//
// A view of the model: rows are read again when the map's content or selection changes; with the
// same topics in the same order only the changed rows' data changes (the delegates stay).
class MobileOutline : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MobileMap *map READ map WRITE setMap NOTIFY mapChanged)
    Q_PROPERTY(QString rootId READ rootId NOTIFY rootChanged)
    Q_PROPERTY(QString rootTitle READ rootTitle NOTIFY rootChanged)
    // False while the map is shown: the rows are not read again at each edit (the review's P2);
    // turned on, they are read at once.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // The desktop's dark outliner, with a dark map background (MainWindow: NodeStyle::isDarkBackground).
    Q_PROPERTY(bool dark READ dark NOTIFY rootChanged)
    // FEAT-121 A4: the outline's zoom in percent, 50 to 300 as the desktop's (applyOutlineZoom), apart
    // from the map's. Kept while the app runs (shared by every outline, as the desktop's m_outlineZoom),
    // 100 at each start; not saved.
    Q_PROPERTY(int zoomPercent READ zoomPercent WRITE setZoomPercent NOTIFY zoomPercentChanged)
    // FEAT-121 D: the topic above the rows, the central topic or the entered one, and its weights
    // (the central topic and the first level are heavy); crumbs, from the central topic to the
    // head's parent, are {id, title} maps (empty when no branch is entered).
    Q_PROPERTY(QString headId READ headId NOTIFY rootChanged)
    Q_PROPERTY(bool headHeavy READ headHeavy NOTIFY rootChanged)
    Q_PROPERTY(QVariantList crumbs READ crumbs NOTIFY rootChanged)
    Q_PROPERTY(bool focused READ focused NOTIFY rootChanged)
    // FEAT-121 D: the phone's compact rows show their contents under the titles (the map menu's Show
    // contents). Kept while the app runs (shared, as the zoom), off at each start; not saved.
    Q_PROPERTY(bool showContents READ showContents WRITE setShowContents NOTIFY showContentsChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        DepthRole,          // 0 for the head's children (the central or the entered topic)
        TitleRole,          // plain text (MindNode::title)
        HtmlRole,           // MindNode::titleHtml as stored: a change of formats reloads the row
        HasChildrenRole,
        FoldedRole,
        HiddenCountRole,    // every topic folded under it (the desktop's badge)
        FloatingStartRole,  // the first floating topic: the line above it
        CanIndentRole,      // a sibling above (MindMapModel::indentNode)
        CanOutdentRole,     // under the first level (MindMapModel::outdentNode)
        CanMoveUpRole,      // a sibling above that is not floating (moveInOutline)
        CanMoveDownRole,
        // FEAT-065 contents (A3), as the desktop's OutlineDelegate draws them.
        MarkersRole,        // before the checkbox, left of the title
        TaskRole,           // 0 none, 1 open, 2 done (MindNode::Task)
        BranchColorRole,    // a done checkbox's fill
        TickColorRole,      // its tick (NodeStyle::readableTextOn)
        TopicLinkRole,      // the trailing icons after the title
        WebLinkRole,
        FilesRole,          // the paper clip with files attached
        PictureWidthRole,   // image, sticker or formula under the title at the map's size; 0 = none
        PictureHeightRole,
        PictureKeyRole,     // changes when the picture does
        PictureOpacityRole, // 0..1 (ImageStyle::opacity)
        NoteKeyRole,        // "" without a note; otherwise changes with the note
        LabelsRole,
        HeavyRole,          // the first level: bold rows (depths count from an entered branch)
        ExpandedRole,       // FEAT-121 D: the row's contents shown although the rows are compact
    };
    Q_ENUM(Role)

    explicit MobileOutline(QObject *parent = nullptr);

    MobileMap *map() const { return m_map; }
    void setMap(MobileMap *map);
    QString rootId() const { return m_rootId; }
    QString rootTitle() const { return m_rootTitle; }
    bool active() const { return m_active; }
    void setActive(bool active);
    bool dark() const { return m_dark; }
    static constexpr int kMinZoomPercent = 50;   // below it the 18 px rows are not readable (desktop)
    static constexpr int kMaxZoomPercent = 300;
    int zoomPercent() const { return s_zoomPercent; }
    void setZoomPercent(int percent);
    QString headId() const { return m_headId; }
    bool headHeavy() const { return m_headHeavy; }
    QVariantList crumbs() const { return m_crumbs; }
    bool focused() const { return !m_focusPath.isEmpty(); }
    bool showContents() const { return s_showContents; }
    void setShowContents(bool on);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // The row of topic `id`, -1 when it is not shown.
    Q_INVOKABLE int rowOf(const QString &id) const;
    // Folds or opens the branch of topic `id` in the outline (the map is unchanged).
    Q_INVOKABLE void toggleFolded(const QString &id);
    // FEAT-121 D: enters the branch of topic `id` ("" or the central topic: the whole sheet again).
    Q_INVOKABLE void focusOn(const QString &id);
    // Up one level from the entered branch; false when none is entered (Back then leaves the outline).
    Q_INVOKABLE bool focusUp();
    // Shows or hides the contents of topic `id`'s row under its title while the rows are compact.
    Q_INVOKABLE void toggleContents(const QString &id);
    // The title of topic `id` (the central topic too) in a row's field: its text with the bold,
    // italic, strikethrough and underline of its formatted title, as the desktop's
    // OutlineDelegate::setEditorData and loadRootTitle (ui-text-editing.md, "Outliner: formattazione
    // condivisa"); font, size and colour stay on the map. The field is left unmodified, and keeps the
    // row weights it was loaded with: applyTitle reads its bold at those, also after an Outdent or
    // Indent has changed the row's level (the review's P1).
    Q_INVOKABLE void loadTitle(QQuickTextDocument *field, const QString &id) const;
    // The title of topic `id` when it holds a formula ($...$, FEAT-116), "" otherwise: the row's
    // field then loads it as plain text with its formulas as pictures (FormulaField.load).
    Q_INVOKABLE QString mathTitle(const QString &id) const;
    // The field's text and those four formats written into topic `id`, as the desktop's outliner
    // (OutlineFormats::mergeOutlineFormats keeps the other properties of the untouched characters),
    // with the word being composed (preedit) typed at `cursor`; the layout in the open history step.
    // False when the field was not edited, nothing changes, or the central topic would be left empty.
    // Afterwards the field counts as unmodified, so a second call writes nothing again. A title with
    // a formula is read with each picture as its code and saved as plain text, as the map's
    // (ui-text-editing.md: titles with formulas stay plain text).
    Q_INVOKABLE bool applyTitle(QQuickTextDocument *field, const QString &id, const QString &preedit, int cursor);
    // The keyboard bar's format buttons, the desktop's OutlineFormatBar: Bold, Italic,
    // Strikethrough, Underline, Highlight (FEAT-068). formatState: OutlineFormats::InlineFlag bits
    // of the format at the field's selection (OutlineFormatBar::sync); applyFormat sets one of them
    // (a flag) on the selection [start, end) or takes it off, bold at the row's weights (taking it
    // off leaves the row's own weight). The field then holds the change for applyTitle.
    // FEAT-124: the weight bold takes in the field of topic `id` (its row's, as applyFormat), for the
    // Markdown shortcuts typed there.
    Q_INVOKABLE int boldWeight(QQuickTextDocument *field, const QString &id) const;
    Q_INVOKABLE int formatState(QQuickTextDocument *field, const QString &id, int start, int end) const;
    Q_INVOKABLE void applyFormat(QQuickTextDocument *field, const QString &id, int start, int end, int flag, bool on);
    // FEAT-059/065: the note of topic `id` in a row's read-only field, as the desktop's noteDocument:
    // its formatted text (plain when it has none) with the formulas drawn (FEAT-064) in `color`.
    Q_INVOKABLE void loadNote(QQuickTextDocument *field, const QString &id, const QColor &color) const;

signals:
    void mapChanged();
    void rootChanged();
    void activeChanged();
    void zoomPercentChanged();
    void showContentsChanged();

private:
    // What a row shows, copied from the model: an Undo step makes the topics again, so no pointer
    // into the model is kept between two refreshes.
    struct Row {
        QString id, title, html;
        int depth = 0, hidden = 0;
        bool hasChildren = false, folded = false, floatingStart = false, heavy = false, expanded = false;
        bool canIndent = false, canOutdent = false, canMoveUp = false, canMoveDown = false;
        QStringList markers, labels;
        int task = 0, files = 0;
        QColor branch, tick;
        bool topicLink = false, webLink = false;
        QSizeF picture;
        QString pictureKey, noteKey;
        qreal pictureOpacity = 1;
        bool operator==(const Row &) const = default;
    };
    void refresh();
    void collect(const MindNode *node, int depth, QList<Row> &rows) const;
    // Opens the folded branches over the selected topic; a selection outside the entered branch goes
    // up to the nearest topic holding it.
    void openAroundSelection();
    // The entered topic, or its nearest ancestor still in the model (an Undo, a delete); the path is
    // read again from it. Null: no branch entered.
    const MindNode *focusNode();
    void setFocusPath(const MindNode *node);
    MindNode *node(const QString &id) const;
    // The row's weights: the central topic and the first level are bold (desktop's rowBoldWeights);
    // a loaded field's are the ones it was loaded with.
    bool heavy(const MindNode *node) const;
    bool heavy(const QTextDocument *field, const MindNode *node) const;

    QPointer<MobileMap> m_map;
    QList<Row> m_rows;
    QSet<QString> m_folded, m_expanded;
    QStringList m_focusPath;  // ids from the central topic's child down to the entered topic
    QString m_rootId, m_rootTitle, m_rootHtml;
    QString m_headId, m_headTitle, m_headHtml;
    bool m_headHeavy = true;
    QVariantList m_crumbs;
    bool m_dark = false;
    bool m_active = true;
    static inline int s_zoomPercent = 100;
    static inline bool s_showContents = false;
};
