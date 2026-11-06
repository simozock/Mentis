#pragma once
// FEAT-084: the frame's rows of the Image and PDF/Print windows: Frame (off when the window opens),
// then, when on, Style, Thickness (px), Colour and Corners. Added to the window's grid of rows.
#include "exportframe.h"

#include <QObject>

class ColorField;
class NumberField;
class QComboBox;
class QGridLayout;
class QLabel;
class QWidget;
class SegmentedControl;
class ToggleSwitch;

class ExportFrameControls final : public QObject {
    Q_OBJECT
public:
    // The rows go at the end of grid (label in column 0, control in column 1 on the right).
    ExportFrameControls(QGridLayout *grid, QWidget *parent);

    ExportFrame frame() const;  // style None when off
    void setFrame(const ExportFrame &frame);  // FEAT-045: an agent opens the window with a frame chosen
    ToggleSwitch *toggle() const { return m_on; }
    QComboBox *styleField() const { return m_style; }
    NumberField *thicknessField() const { return m_thickness; }
    ColorField *colorField() const { return m_color; }
    SegmentedControl *cornersField() const { return m_corners; }

signals:
    void changed();

private:
    void sync();  // the detail rows shown with the frame on; the ornaments' minimum

    ToggleSwitch *m_on = nullptr;
    QComboBox *m_style = nullptr;
    NumberField *m_thickness = nullptr;
    ColorField *m_color = nullptr;
    SegmentedControl *m_corners = nullptr;
    QColor m_chosen = ExportFrame::defaultColor();
    QList<QPair<QLabel *, QWidget *>> m_details;
};
