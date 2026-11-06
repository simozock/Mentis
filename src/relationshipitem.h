#pragma once

// FEAT-053: a relationship drawn on the map. Selectable; when selected it shows the
// two control handles (dragging one bends the line) and the two ends (dragging one onto another
// element reconnects it). Double click, or typing while selected, edits the label. The item never
// writes the document: it emits `edited` with the changed relationship.

#include "relationship.h"

#include <QFont>
#include <QGraphicsObject>
#include <QPen>

#include <functional>

class QGraphicsTextItem;

class RelationshipItem final : public QGraphicsObject {
    Q_OBJECT
public:
    enum { Type = UserType + 53 };
    int type() const override { return Type; }

    // Resolved look (defaults and the sheet's backdrop applied by the scene).
    struct Look {
        QPen pen;
        QFont font;
        QColor textColor;
        QColor backdrop;      // behind the label, so the line does not cross the text
        QString placeholder;  // shown while selected with an empty label
    };

    explicit RelationshipItem(const QString &id, QGraphicsItem *parent = nullptr);
    QString id() const { return m_relationship.id; }
    const Relationship &relationship() const { return m_relationship; }
    void setContent(const Relationship &relationship, const Relationships::Geometry &geometry, const Look &look);
    const Relationships::Geometry &geometry() const { return m_geometry; }

    // Geometry of a changed copy, for the live preview while a handle is dragged.
    std::function<Relationships::Geometry(const Relationship &)> geometryFor;
    // Id of the element under a scene point (empty on the background), to reconnect an end.
    std::function<QString(const QPointF &scenePos)> elementAt;

    // Label editing; initialText replaces the label (typing while selected), empty keeps it.
    void beginEditing(const QString &initialText = QString());
    bool isEditing() const { return m_editor != nullptr; }
    void commitEditing();

    // Scene positions of the parts, for tests: control handles and ends.
    QPointF handlePos(int index) const { return index == 0 ? m_geometry.fromControl : m_geometry.toControl; }
    QRectF labelRect() const { return m_labelRect; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

signals:
    void edited(const Relationship &relationship);
    void menuRequested(const QString &id, const QPoint &screenPos);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool sceneEventFilter(QGraphicsItem *watched, QEvent *event) override;

private:
    enum class Part { None, FromControl, ToControl, FromEnd, ToEnd };
    Part partAt(const QPointF &pos) const;
    bool showsHandles() const;
    void updateCachedShapes();
    void finishEditing(bool keep);

    Relationship m_relationship;
    Relationships::Geometry m_geometry;
    Look m_look;
    QRectF m_labelRect;
    QSizeF m_labelSize;  // measured for m_measuredText in m_measuredFont
    QString m_measuredText;
    QFont m_measuredFont;
    QRectF m_bounds;
    QPainterPath m_hitPath;
    // Drag of a handle or an end: the preview copy and, for an end, the mouse position.
    Part m_dragPart = Part::None;
    Relationship m_dragged;
    QPointF m_dragPoint;
    QGraphicsTextItem *m_editor = nullptr;
};
