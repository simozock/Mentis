#pragma once

#include "nodestyle.h"

#include <QHash>
#include <QLineF>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QWidget>
#include <functional>

class QLabel;
class QPushButton;
class QTimer;
class MindMapModel;
struct MindNode;

// FEAT-093: the guided tour with the mascot and the tips at the first use of an area.
namespace Mascot {
enum class Face { Smile, Hello, Look, Think, Cheer };
QPixmap face(Face face, int sizePx);  // from :/assets/mascot, for the device pixel ratio
}  // namespace Mascot

// Settings of the guide: the tour is offered once at the first start; the tips can be turned off.
namespace TourSettings {
bool tourOffered();
void setTourOffered();
bool tipsEnabled();
void setTipsEnabled(bool on);
bool tipSeen(const QString &key);
void setTipSeen(const QString &key);
}  // namespace TourSettings

// The veil of the tour: the window dimmed (the modal scrim) except rounded holes around the controls
// of the step, with accent arrows towards where to click. It takes the mouse everywhere but over the
// controls the step lets the user use, so that nothing else can be done while the tour runs. The
// window stays free: its buttons are always open and its drag areas still move it.
class TourVeil final : public QWidget {
public:
    explicit TourVeil(QWidget *window);
    void setHoles(const QList<QRect> &holes);  // window coordinates
    QList<QRect> holes() const { return m_holes; }
    // Where the mouse reaches the window, window coordinates; none = the whole window is blocked.
    void setOpenings(const QList<QRect> &openings);
    QList<QRect> openings() const { return m_openings; }
    // The window's widget under the veil at a point of the window (the frame's hit test).
    QWidget *below(const QPoint &pos) const;
    QRect hole() const;  // around all the holes; empty = none
    void setArrows(const QList<QLineF> &arrows);  // from the bubble to a control, window coordinates
    QList<QLineF> arrows() const { return m_arrows; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    // Taken, so that they reach neither the window under the veil nor the window itself.
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void updateMask();
    void passToDragArea(QMouseEvent *event);
    QList<QRect> m_holes;
    QList<QRect> m_openings;
    QList<QLineF> m_arrows;
};

// The bubble: the dark modal surface with the mascot beside the text and the buttons under it, and an
// arrow towards the control it is about. Placed beside a rectangle of its window.
class MascotBubble final : public QWidget {
    Q_OBJECT
public:
    explicit MascotBubble(QWidget *window);
    void setContent(Mascot::Face face, const QString &text);
    // Buttons from the right: the first is the primary one. Removes the old ones.
    QPushButton *addButton(const QString &text, bool primary);
    void clearButtons();
    // Window coordinates; empty = centred. Keeps off the avoided rectangles when it can.
    void placeBeside(const QRect &target, const QList<QRect> &avoid = {});
    // The text as given to setContent, in the bubble's markup (see toHtml).
    QString text() const;
    static QString toHtml(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QLabel *m_face = nullptr;
    QLabel *m_text = nullptr;
    QString m_plain;
    QWidget *m_buttons = nullptr;
    enum class Side { None, Above, Below, Left, Right } m_side = Side::None;
    int m_arrowAt = 0;  // along the side facing the target
};

// The tours: the map's on a practice map, some steps waiting for the user's action; the outliner's and
// the Gantt's on the open sheet, offered the first time those views open. Each step lights the
// controls it is about. The main window gives what it knows through Host; the tour owns its veil and
// bubble, children of the window, while it runs.
class GuidedTour final : public QObject {
    Q_OBJECT
public:
    enum class Kind { Map, Outliner, Gantt };
    struct Host {
        QWidget *window = nullptr;
        std::function<MindMapModel *()> model;
        std::function<QRect(const MindNode *)> nodeRect;   // a topic on the map, window coordinates
        std::function<QList<const MindNode *>()> selection;  // the selected topics on the map
        std::function<void(const MindNode *)> reveal;        // scrolls the map to show the topic
        // The small bars shown on a selected topic or text (window coordinates): never dimmed on the
        // steps about topics. "topic" = the topic bar and its + knobs, "text" = the bar of the
        // selected words (map and outliner).
        std::function<QList<QRect>(const QString &kind)> floatingBars;
        // The controls of the steps by name ("newMap", "topicBar", "sidePanel", "sheets", "views",
        // "ganttActions", ...): MainWindow::tour() lists them. Hidden or missing ones are left out.
        std::function<QWidget *(const QString &)> control;
        // Puts the window in the state of a step: "map", "outline", "gantt", "stylePanel",
        // "iconPanel", "ganttPanel".
        std::function<void(const QString &)> prepare;
        std::function<void()> startPracticeMap;
        std::function<void()> keepMap;     // the end: save the practice map with a name
        std::function<void()> discardMap;  // the end: back to the Home without it
        // The window's shortcuts that work during the tour (the editing keys its steps ask for);
        // the others are blocked while it runs.
        std::function<bool(QObject *)> shortcutAllowed;
    };

    explicit GuidedTour(Host host, QObject *parent = nullptr);
    ~GuidedTour() override;
    // The first start: the mascot asks whether to start; "Not now" leaves the tour in the Help menu.
    void offer();
    // The first time the outliner or the Gantt opens: the mascot offers the tour of that view. False
    // when tips are off, it was offered already or a tour runs.
    bool offerArea(Kind kind);
    void start(Kind kind = Kind::Map);
    void stop();
    bool isRunning() const { return m_bubble != nullptr; }
    Kind kind() const { return m_kind; }
    QString step() const;  // the id of the step shown ("home", "subtopic", ...); empty while offering
    int stepCount() const;
    MascotBubble *bubble() const { return m_bubble; }
    TourVeil *veil() const { return m_veil; }
    // Checks a waiting step and follows the controls (run by a timer while the tour is shown; tests).
    void refresh();
    void next();
    void back();

signals:
    void finished();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Wait { None, RootTitle, MoreNodes, Moved, Styled };
    struct Spec {
        const char *id;
        Mascot::Face face;
        QString text;
        // Control names, or "root", "lastTopic", "allTopics", "selectedTopic" on the map. The first
        // is the one the bubble points at.
        QStringList targets;
        QString prepare;
        Wait wait = Wait::None;
        QString linkText, link;  // an extra button opening a web page in the browser
        QStringList pointers;    // targets that also get an arrow: where to click
        QStringList bars;        // the floating bars ("topic", "text") that can be used on this step
    };
    struct Snapshot {
        int nodes = 0;
        QString rootTitle;
        QString layout;                               // parents, order, sides, manual moves
        QHash<QString, NodeStyleOverrides> styles;    // the styles chosen by hand
        NodeStyle::SheetStyle sheet;
        QString palette;
    };
    QList<Spec> specs() const;
    Snapshot snapshot() const;
    void show();
    bool waits() const;
    bool usable() const;  // the step's controls can be used, not only looked at
    bool done() const;
    QList<QRect> targetRects(const QString &name) const;
    void build();

    Host m_host;
    Kind m_kind = Kind::Map;
    QList<Spec> m_specs;  // of m_kind, translated when the tour starts
    bool m_offering = false;
    int m_step = 0;
    Snapshot m_entry;  // the map when the step began, for the waiting steps
    bool m_practice = false;
    bool m_revisited = false;  // reached with Back: already done, so it has Next and does not wait
    QPointer<TourVeil> m_veil;
    QPointer<MascotBubble> m_bubble;
    QTimer *m_timer = nullptr;
};

// A tip at the first use of an area: the mascot thinking, two sentences, Got it and Read more.
namespace Tips {
// Shown once per key, beside the anchor, while tips are on and no tour runs; true when shown.
bool offer(QWidget *window, const QString &key, QWidget *anchor, const QString &text, const QString &page);
// The tip on screen, if any (tests).
MascotBubble *current();
void setSuppressed(bool suppressed);  // the tour hides tips while it runs
}  // namespace Tips
