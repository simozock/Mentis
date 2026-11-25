#pragma once

#include "stickeredit.h"

#include <QColor>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// FEAT-070 on the phone (FEAT-097): the sticker editor's state for the QML modal, on the desktop's
// StickerEdit (history, actions) and StickerCanvas (gestures, drawing; shown by StickerCanvasView).
// Saving does what the desktop's sticker panel does after its editor (IconPage::editSticker): a new
// sticker in the chosen collection (or a new one), then, for an own sticker, the original may go.
class MobileStickerEditor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(bool modified READ modified NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QVariantList colors READ colors NOTIFY changed)
    // StickerCanvas::Mode: 0 Select, 1 Frame, 2 Rectangle, 3 Ellipse, 4 Line, 5 Pen, 6 Polygon, 7 Star.
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY stateChanged)
    // The keyboard's Shift for the finger: add to the selection, keep the proportions, 45° steps,
    // squares and circles, upright polygons and stars.
    Q_PROPERTY(bool shift READ shift WRITE setShift NOTIFY shiftChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(bool pointEditing READ pointEditing NOTIFY stateChanged)
    Q_PROPERTY(bool chosenPoint READ chosenPoint NOTIFY stateChanged)
    Q_PROPERTY(bool penActive READ penActive NOTIFY stateChanged)
    Q_PROPERTY(qreal rotation READ rotation NOTIFY changed)
    Q_PROPERTY(QString labelText READ labelText NOTIFY changed)
    Q_PROPERTY(QStringList labelFamilies READ labelFamilies CONSTANT)
    Q_PROPERTY(QString labelFamily READ labelFamily NOTIFY changed)
    Q_PROPERTY(qreal labelSize READ labelSize NOTIFY changed)
    Q_PROPERTY(QColor labelColor READ labelColor NOTIFY changed)
    Q_PROPERTY(bool labelBold READ labelBold NOTIFY changed)
    Q_PROPERTY(bool fillNone READ fillNone NOTIFY styleChanged)
    Q_PROPERTY(QColor fillColor READ fillColor NOTIFY styleChanged)  // the last fill when None
    Q_PROPERTY(QColor strokeColor READ strokeColor NOTIFY styleChanged)  // invalid: no outline
    Q_PROPERTY(qreal strokeWidth READ strokeWidth NOTIFY styleChanged)
    Q_PROPERTY(int cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY toolOptionsChanged)
    Q_PROPERTY(int sides READ sides WRITE setSides NOTIFY toolOptionsChanged)
    Q_PROPERTY(int points READ points WRITE setPoints NOTIFY toolOptionsChanged)
    Q_PROPERTY(int margin READ margin WRITE setMargin NOTIFY toolOptionsChanged)
    // Where the copy can go: [{value: id, name}] of the visible sticker collections; the original's.
    Q_PROPERTY(QVariantList collections READ collections NOTIFY opened)
    Q_PROPERTY(QString collectionId READ collectionId NOTIFY opened)
    Q_PROPERTY(QString originalName READ originalName NOTIFY opened)

public:
    explicit MobileStickerEditor(QObject *parent = nullptr);

    // The sticker at path (a file of the library or a built-in resource) of collection collectionId;
    // false when the app cannot draw it.
    Q_INVOKABLE bool open(const QString &path, const QString &collectionId);
    Q_INVOKABLE static bool editable(const QString &path);  // an SVG sticker

    bool valid() const { return m_edit.isValid(); }
    bool modified() const { return m_edit.isModified(); }
    bool canUndo() const { return m_edit.canUndo(); }
    bool canRedo() const { return m_edit.canRedo(); }
    QVariantList colors() const;
    int mode() const { return int(m_edit.canvas().mode()); }
    void setMode(int mode);
    bool shift() const { return m_shift; }
    void setShift(bool shift);
    int selectionCount() const { return int(m_edit.canvas().selection().size()); }
    bool pointEditing() const { return !m_edit.canvas().editedShape().isEmpty(); }
    bool chosenPoint() const { return m_edit.canvas().hasChosenPoint(); }
    bool penActive() const { return m_edit.canvas().penActive(); }
    qreal rotation() const { return m_edit.document().transform().rotation; }
    QString labelText() const { return m_edit.labelText(); }
    QStringList labelFamilies() const { return m_families; }
    QString labelFamily() const { return m_edit.labelLook().family; }
    qreal labelSize() const { return m_edit.labelLook().sizePercent; }
    QColor labelColor() const { return m_edit.labelLook().color; }
    bool labelBold() const { return m_edit.labelLook().bold; }
    bool fillNone() const { return !m_edit.style().fill.isValid(); }
    QColor fillColor() const;
    QColor strokeColor() const { return m_edit.style().stroke; }
    qreal strokeWidth() const { return m_edit.style().stroke.isValid() ? m_edit.style().strokeWidth : 0; }
    int cornerRadius() const { return m_cornerRadius; }
    void setCornerRadius(int px);
    int sides() const { return m_sides; }
    void setSides(int sides);
    int points() const { return m_points; }
    void setPoints(int points);
    int margin() const { return m_margin; }
    void setMargin(int percent);
    QVariantList collections() const { return m_collections; }
    QString collectionId() const { return m_collectionId; }
    QString originalName() const;

    Q_INVOKABLE void setColor(int index, const QColor &color) { m_edit.setColor(index, color); }
    // A colour of the file by its value: the colour sheet may pick again (its custom panel), and the
    // colours' order can change after a pick (the default black).
    Q_INVOKABLE void replaceColor(const QColor &from, const QColor &to) { m_edit.replaceColor(from, to); }
    Q_INVOKABLE void undo() { m_edit.undo(); }
    Q_INVOKABLE void redo() { m_edit.redo(); }
    Q_INVOKABLE void rotateBy(qreal degrees) { m_edit.rotateBy(degrees); }
    Q_INVOKABLE void setRotation(qreal degrees) { m_edit.setRotation(degrees); }
    Q_INVOKABLE void flipHorizontally() { m_edit.flipHorizontally(); }
    Q_INVOKABLE void flipVertically() { m_edit.flipVertically(); }
    Q_INVOKABLE void fitToDrawing() { m_edit.fitToDrawing(); }
    Q_INVOKABLE void addMargin() { m_edit.addMargin(m_margin); }
    Q_INVOKABLE void typeLabelText(const QString &text) { m_edit.typeLabelText(text); }
    Q_INVOKABLE void setLabelFamily(const QString &family) { m_edit.setLabelFamily(family); }
    Q_INVOKABLE void setLabelSize(qreal percent) { m_edit.setLabelSizePercent(percent); }
    Q_INVOKABLE void setLabelColor(const QColor &color) { m_edit.setLabelColor(color); }
    Q_INVOKABLE void setLabelBold(bool bold) { m_edit.setLabelBold(bold); }
    Q_INVOKABLE void setFillNone(bool none) { m_edit.setFillNone(none); }
    Q_INVOKABLE void setFillColor(const QColor &color) { m_edit.setFillColor(color); }
    Q_INVOKABLE void setStrokeColor(const QColor &color) { m_edit.setStrokeColor(color); }
    Q_INVOKABLE void setStrokeWidth(qreal width) { m_edit.setStrokeWidth(width); }
    // SvgDocument::Order: 0 forward, 1 backward, 2 front, 3 back.
    Q_INVOKABLE void reorder(int order) { m_edit.reorderShapes(SvgDocument::Order(order)); }
    Q_INVOKABLE void removeSelection() { m_edit.removeShapes(m_edit.canvas().selection()); }
    // The desktop's double click and Esc on a shape's points, as buttons.
    Q_INVOKABLE void editSelectedPoints();
    Q_INVOKABLE void endPointEditing() { m_edit.canvas().editPoints(QString()); }
    Q_INVOKABLE void removeChosenPoint() { m_edit.canvas().removeChosenPoint(); }
    // The desktop's Enter, a click on the first point, Backspace and Esc while drawing with the pen.
    Q_INVOKABLE void finishPen(bool close) { m_edit.canvas().finishPen(close); }
    Q_INVOKABLE void removeLastPenPoint() { m_edit.canvas().removeLastPenPoint(); }
    Q_INVOKABLE void cancelPen() { m_edit.canvas().cancelPen(); }
    Q_INVOKABLE void settle() { m_edit.settle(); }

    Q_INVOKABLE static QString cleanName(const QString &name) { return StickerEdit::cleanName(name); }
    // {ok, reason, canDeleteOriginal}: the copy written into collectionId, or into a new collection
    // named newCollection when collectionId is empty.
    Q_INVOKABLE QVariantMap save(const QString &name, const QString &collectionId, const QString &newCollection);
    // After save: the own original removed from its collection.
    Q_INVOKABLE bool deleteOriginal();

    StickerCanvas &canvas() { return m_edit.canvas(); }

signals:
    void changed();
    void styleChanged();
    void selectionChanged();
    void stateChanged();
    void shiftChanged();
    void toolOptionsChanged();
    void opened();

private:
    void applyToolOptions();

    StickerEdit m_edit;
    QStringList m_families;
    QString m_path;
    QString m_collectionId;
    QVariantList m_collections;
    bool m_shift = false;
    int m_cornerRadius = 0;
    int m_sides = 5;
    int m_points = 5;
    int m_margin = 10;
};
