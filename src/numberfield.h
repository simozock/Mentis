#pragma once
// The app's number field (user, 2026-10-28): only the number, a − at the left and a + at the right
// inside the border; the unit goes in the field's label. A QLineEdit, not a QSpinBox: a styled
// QSpinBox without its buttons shrank its text to 1 px (ColorPicker). Used by the Gantt's task panel
// and the Style panel's pixel fields.
#include <QLineEdit>
#include <QTimer>

class NumberField : public QLineEdit {
    Q_OBJECT
public:
    static constexpr int kButtonPx = 20;  // each of − and +, the field's height

    explicit NumberField(QWidget *parent = nullptr);

    void setRange(double minimum, double maximum);
    void setDecimals(int decimals);  // 0 = whole numbers; more digits are rounded away
    void setStep(double step) { m_step = step; }  // Shift and Page Up/Down: ten steps
    double minimum() const { return m_minimum; }
    double maximum() const { return m_maximum; }

    // The value shown; clearValue() shows none (the placeholder, e.g. "—" for different values).
    void setValue(double value);
    void clearValue();
    bool hasValue() const { return m_hasValue; }
    double value() const { return m_value; }
    // With no value shown, − and + emit valueStepped (each item changes from its own value) instead
    // of choosing one value from the minimum.
    void setStepsEachItem(bool eachItem) { m_stepsEachItem = eachItem; }

    // Geometry for the tests (widget coordinates).
    QRect minusRect() const;
    QRect plusRect() const;

signals:
    // A value confirmed (Enter, leaving the field, −/+, arrows), within the range; not emitted when
    // the text was not changed. The field shows it until the owner sets another.
    void valueChosen(double value);
    void valueStepped(double delta);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QString format(double value) const;
    double rounded(double value) const;
    void updateValidator();
    void showValue();     // the value (or none) again, as the text
    void confirmText();   // Enter or leaving the field
    void step(double steps);
    int buttonAt(const QPoint &pos) const;  // -1 = −, 1 = +, 0 = none
    bool canStep(int direction) const;

    double m_minimum = 0, m_maximum = 99, m_step = 1, m_value = 0;
    int m_decimals = 0;
    bool m_hasValue = true;
    bool m_stepsEachItem = false;
    int m_hover = 0, m_pressed = 0;
    QTimer m_repeat;  // a held − or + repeats
};
