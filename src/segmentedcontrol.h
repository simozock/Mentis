#pragma once

#include <QColor>
#include <QStringList>
#include <QWidget>

class QButtonGroup;
class QToolButton;

// Selettore a segmenti riutilizzabile (FEAT-017): un solo segmento attivo, etichette fornite dal
// chiamante, nessuna conoscenza di mappe o nodi. Il segmento attivo si distingue per sfondo e per
// testo in grassetto, non solo per il colore. Tastiera: il controllo si raggiunge con la catena
// del focus (Tab/Maiusc+Tab) e frecce sinistra/destra, Home e Fine cambiano segmento. Un clic
// sui segmenti non sposta il focus.
class SegmentedControl final : public QWidget {
    Q_OBJECT

public:
    explicit SegmentedControl(const QStringList &labels = {}, QWidget *parent = nullptr);

    int addSegment(const QString &label);
    int count() const;
    int currentIndex() const;
    // Cambio programmatico: non emette segmentActivated (non e' una scelta dell'utente).
    void setCurrentIndex(int index);
    // FEAT-054: a disabled segment is greyed out and skipped by the arrow keys.
    void setSegmentEnabled(int index, bool enabled);
    // FEAT-080: other colours than the dark side panel's (the Gantt's light or dark card).
    struct Colors {
        QColor track, border, active, hover, text, muted;
    };
    void setColors(const Colors &colors);

signals:
    // Scelta dell'utente con mouse o tastiera.
    void segmentActivated(int index);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void activate(int index);
    void fitHeight();

    QButtonGroup *m_group = nullptr;
    QList<QToolButton *> m_segments;
    QColor m_track{0x25, 0x2a, 0x31};
    QColor m_border{0x3a, 0x41, 0x4b};
    bool m_keyboardFocus = false;  // contorno di focus solo se il focus e' arrivato da tastiera
};
