#include "panelsize.h"

#include <QSettings>

#include <cmath>

namespace {
const QString kSideWidthKey = QStringLiteral("mobile/sidePanelWidth");
const QString kBottomShareKey = QStringLiteral("mobile/sidePanelBottomShare");
}  // namespace

PanelSize::PanelSize(QObject *parent)
    : QObject(parent),
      m_sideWidth(QSettings().value(kSideWidthKey, 0).toReal()),
      m_bottomShare(QSettings().value(kBottomShareKey, 0).toReal()) {}

void PanelSize::setSideWidth(qreal width) {
    width = std::round(width);
    if (qFuzzyCompare(1 + width, 1 + m_sideWidth)) return;
    m_sideWidth = width;
    QSettings().setValue(kSideWidthKey, width);
    emit changed();
}

void PanelSize::setBottomShare(qreal share) {
    if (qFuzzyCompare(1 + share, 1 + m_bottomShare)) return;
    m_bottomShare = share;
    QSettings().setValue(kBottomShareKey, share);
    emit changed();
}
