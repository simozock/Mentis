#pragma once

#include <QAbstractButton>
#include <QColor>

// Interruttore dell'interfaccia scura (FEAT-035, AGENTS.md "Interfaccia scura"): pillola 36x20 con
// pomello bianco di 16 px; spento surfaceRaised, acceso grigio chiaro kOnTrack (FEAT-072). Clic e Spazio lo cambiano (come ogni
// QAbstractButton); col focus da tastiera ha un anello accent. Il testo accessibile e' l'etichetta
// della riga (setAccessibleName).
class ToggleSwitch final : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);
    QSize sizeHint() const override;

    // Track when off; invalid = surfaceRaised (the dark interface). The light Gantt panel sets its own.
    QColor offTrack;

    static constexpr int kTrackWidthPx = 36;
    static constexpr int kTrackHeightPx = 20;
    static constexpr int kKnobPx = 16;
    static constexpr QRgb kOnTrack = 0xff878c92;  // measured live (FEAT-072 B7)

protected:
    void paintEvent(QPaintEvent *event) override;
};
