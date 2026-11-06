#pragma once

// FEAT-072 F3 (checked live 2026-10-22): tools of the one selected topic, drawn over the map.
// - A small floating bar 8 px above the topic, centred on it: Relationship, Web link, Image, Note,
//   Checkbox, Sticker (opens the Markers and stickers panel), Format painter (FEAT-085: click = copy
//   the topic's style and paste it on the next topic clicked, double click = keep it on; the reference also
//   has "Develop ideas" and AI: not here).
// - Blue "+" buttons (18 px, the selection colour): beside the topic on its branch side, 15 px from
//   the edge = add a child; under it, 17 px from the edge = add a sibling (not on the central topic,
//   a floating topic nor a summary topic, FEAT-052). In org and tree charts the child "+" is under the topic and the sibling
//   "+" beside it. A "+" over a collapse control (beside the topic, or under it in org and tree charts)
//   moves out past it, 4 px from it (2026-10-23: beside, the centre ends about 30 px out).
// Hidden with no or several selected topics, while a title is edited, while a mouse button is down
// and when `enabled` says so. Size on screen does not follow the zoom.

#include <QObject>
#include <QPointer>

#include <functional>

class FloatingGroup;
class MindMapScene;
class MindMapView;
class QAbstractButton;
class QTimer;
class QToolButton;
struct MindNode;

class TopicTools final : public QObject {
    Q_OBJECT
public:
    static constexpr int kBarGapPx = 8;          // between the bar and the topic
    static constexpr int kKnobSizePx = 18;
    static constexpr int kChildKnobOffsetPx = 15;   // knob centre from the side of the topic
    static constexpr int kSiblingKnobOffsetPx = 17; // knob centre from the bottom of the topic
    static constexpr int kCollapseKnobGapPx = 4;    // between the collapse circle and a knob beside it

    TopicTools(MindMapView *view, MindMapScene *scene, QObject *parent = nullptr);

    std::function<bool()> enabled;  // false = everything hidden (e.g. Outliner shown)
    std::function<void(MindNode *)> relation, webLink, image, note, checkbox, sticker;
    std::function<void()> addChild, addSibling;
    // FEAT-085: format painter turned on from the topic, kept on by a double click, turned off.
    std::function<void(MindNode *)> formatPainter;
    std::function<void()> keepFormatPainter, stopFormatPainter;
    void setFormatPainterOn(bool on);  // the button's checked state, without its callbacks

    void schedule();  // repositions on the next turn of the event loop (changes come in bursts)

    // For tests.
    QWidget *bar() const;
    QAbstractButton *childKnob() const;
    QAbstractButton *siblingKnob() const;
    QAbstractButton *formatPainterButton() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void update();
    QToolButton *addTool(const QString &icon, const QString &tip, std::function<void(MindNode *)> *run);

    MindMapView *m_view;
    MindMapScene *m_scene;
    FloatingGroup *m_bar = nullptr;
    QAbstractButton *m_childKnob = nullptr;
    QAbstractButton *m_siblingKnob = nullptr;
    QToolButton *m_formatPainter = nullptr;
    QTimer *m_timer = nullptr;
};
