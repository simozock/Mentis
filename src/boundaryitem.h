#pragma once

// FEAT-052: a boundary drawn on the map: under the branches and topics, selectable
// with a click on its border, fill or title tab. Double click, typing while selected, Enter or F2
// edit the title. The item never writes the document: it emits `edited` with the changed boundary.

#include "boundary.h"

#include <QFont>
#include <QGraphicsObject>
#include <QPen>

class QGraphicsTextItem;

class BoundaryItem final : public QGraphicsObject {
    Q_OBJECT
public:
    enum { Type = UserType + 52 };
    int type() const override { return Type; }

    // Resolved look (defaults and the sheet's backdrop applied by the scene).
    struct Look {
        QPen pen;
        QColor fill;
        QColor titleText;
        QFont font;
        QString placeholder;  // in the tab while a title is written into an empty one
    };

    explicit BoundaryItem(const QString &id, QGraphicsItem *parent = nullptr);
    QString id() const { return m_boundary.id; }
    const Boundary &boundary() const { return m_boundary; }
    // covered: union of the covered topics' boxes in scene coordinates.
    void setContent(const Boundary &boundary, const QRectF &covered, const Look &look);
    QRectF strokeRect() const { return m_stroke; }
    QRectF titleRect() const { return m_titleRect; }  // empty when there is no tab
    QPainterPath outline() const;  // drawn outline, for the ends of relationships

    void beginEditing(const QString &initialText = QString());
    bool isEditing() const { return m_editor != nullptr; }
    void commitEditing();

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

signals:
    void edited(const Boundary &boundary);
    void menuRequested(const QString &id, const QPoint &screenPos);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool sceneEventFilter(QGraphicsItem *watched, QEvent *event) override;

private:
    void updateTitleRect();
    // fromSceneEvent: confirmed by a focus or selection change inside the scene's own mouse
    // handling; the change is then sent after that event (BUG-023).
    void finishEditing(bool keep, bool fromSceneEvent = false);

    Boundary m_boundary;
    Look m_look;
    QRectF m_stroke;
    QRectF m_titleRect;
    qreal m_titleWidth = 0;  // measured for m_measuredTitle in m_measuredFont
    QString m_measuredTitle;
    QFont m_measuredFont;
    QGraphicsTextItem *m_editor = nullptr;
};
