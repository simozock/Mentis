#pragma once

#include "formulatext.h"

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// FEAT-116: the live preview of the phone's formula modal, as the desktop's FormulaDialog::Preview:
// the code on the formula's background over a light box (a light checkerboard under a see-through
// background), scaled down to fit, the placeholder when empty. `valid` and `error` are the
// dialog's checks (FormulaText::syntaxErrors, then the parser's messages), computed when the code
// changes.
class FormulaPreview : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString code READ code WRITE setCode NOTIFY codeChanged)
    Q_PROPERTY(QColor background READ background WRITE setBackground NOTIFY backgroundChanged)
    Q_PROPERTY(QString placeholder READ placeholder WRITE setPlaceholder NOTIFY placeholderChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY checked)
    Q_PROPERTY(QString error READ error NOTIFY checked)
    // The formula's height plus the margins, at least the desktop's 96 px.
    Q_PROPERTY(qreal neededHeight READ neededHeight NOTIFY checked)

public:
    explicit FormulaPreview(QQuickItem *parent = nullptr);

    QString code() const { return m_code; }
    void setCode(const QString &code);
    QColor background() const { return m_background; }
    void setBackground(const QColor &background);
    QString placeholder() const { return m_placeholder; }
    void setPlaceholder(const QString &placeholder);
    bool valid() const { return m_valid; }
    QString error() const { return m_error; }
    qreal neededHeight() const;
    void paint(QPainter *painter) override;

signals:
    void codeChanged();
    void backgroundChanged();
    void placeholderChanged();
    void checked();

private:
    void refresh();

    QString m_code;
    QColor m_background = Qt::white;
    QString m_placeholder;
    FormulaText m_formula;
    bool m_valid = false;
    QString m_error;
};
