#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// The side panel's dragged size (user's request 2026-11-22), kept in the app's settings: the
// width docked at the side (landscape) and the share of the page's height docked at the bottom
// (portrait). 0 means the default (the desktop's 276 px, half the page); the panel clamps both.
class PanelSize : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal sideWidth READ sideWidth WRITE setSideWidth NOTIFY changed)
    Q_PROPERTY(qreal bottomShare READ bottomShare WRITE setBottomShare NOTIFY changed)

public:
    explicit PanelSize(QObject *parent = nullptr);

    qreal sideWidth() const { return m_sideWidth; }
    qreal bottomShare() const { return m_bottomShare; }
    void setSideWidth(qreal width);
    void setBottomShare(qreal share);

signals:
    void changed();

private:
    qreal m_sideWidth = 0;
    qreal m_bottomShare = 0;
};
