#pragma once

// Gruppi flottanti fissi dell'interfaccia (2026-10-14, richiesta dell'utente): barra in alto,
// stato di salvataggio, fogli in basso a sinistra, zoom e outliner in basso a destra.
// Aspetto e misure stanno solo qui: cambiando un valore cambiano tutti i gruppi.
// Riferimento: AGENTS.md, "Barra in alto dell'editor" e "Gruppi flottanti".

#include <QColor>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFrame>
#include <QPointer>
#include <QString>
#include <QWidget>
#include <functional>

class QListWidget;
class QListWidgetItem;
class QTimer;
class QToolButton;
class QVariantAnimation;

namespace FloatingUi {
// Misure del gruppo e dei controlli (AGENTS.md, "Raggi degli angoli" e "Barra in alto").
constexpr int kControlSize = 32;    // pulsanti quadrati e altezza delle select
constexpr int kIconSize = 20;       // icone dei pulsanti
constexpr int kChevronSize = 16;    // freccia delle select
constexpr int kPadding = 4;         // tra bordo del gruppo e controlli
constexpr int kSpacing = 2;         // tra due controlli dello stesso gruppo
constexpr qreal kGroupRadius = 10;  // eccezione approvata, vedi AGENTS.md
constexpr qreal kControlRadius = 6; // 10 - 4 di padding (raggi concentrici)
constexpr int kTextPaddingX = 8;    // testo di select, etichette e pulsanti di testo

// Foglio di stile dei controlli dentro un gruppo (pulsanti, pulsanti di testo, etichette,
// campi della select modificabile). Lo applica FloatingGroup a se stesso.
QString controlsStyleSheet();
// Pulsante a icona 32x32 con icona 20, senza focus da tastiera (come i pulsanti della barra).
void setupIconButton(QToolButton *button);

// Tono dei gruppi preso dallo sfondo visibile (mappa o outliner) (richiesta
// dell'utente 2026-10-19 con le foto): sfondo bianco = gruppo bianco come prima; sfondo colorato
// chiaro = tinta dello sfondo schiarita, testo scuro; sfondo scuro = sfondo appena schiarito, testo
// e icone chiari. Il bottone acceso (checked) e' invertito: scuro con icona bianca sui toni
// chiari, chiaro con icona scura su quelli scuri.
struct Tone {
    bool dark = false;
    QColor fill, border, text, muted, hover, pressed, checkedFill, checkedIcon;
};
const Tone &tone();
Tone toneFor(const QColor &background);
// Cambia il tono (ridisegna gruppi, fogli di stile e icone dei bottoni); niente se e' lo stesso.
void setBackdrop(const QColor &background);
// Icona del bottone nel tono attuale (da chiamare dopo un setIcon su un bottone di un gruppo).
void retint(QToolButton *button);
}

// Riquadro bianco semitrasparente con ombra leggera disegnata a mano (pochi rettangoli
// arrotondati, nessun buffer fuori schermo). Prima: ToolbarGroupFrame in mainwindow.cpp.
class FloatingGroup final : public QWidget {
public:
    // Spazio riservato all'ombra attorno al riquadro (sotto è maggiore: luce dall'alto).
    static constexpr int kShadowSide = 4;
    static constexpr int kShadowTop = 3;
    static constexpr int kShadowBottom = 5;
    static constexpr int kPadding = FloatingUi::kPadding;
    static constexpr qreal kRadius = FloatingUi::kGroupRadius;

    explicit FloatingGroup(QWidget *parent = nullptr);
    ~FloatingGroup() override;
    void addWidget(QWidget *widget);
    void applyTone();  // foglio di stile e icone dal tono attuale (FloatingUi::setBackdrop)

protected:
    void paintEvent(QPaintEvent *) override;
    // Bottoni aggiunti dal layout (non solo da addWidget): icona nel tono attuale, anche quello
    // iniziale (2026-10-19: senza, il bottone acceso del pannello restava con l'icona scura).
    void childEvent(QChildEvent *event) override;
};
// Nome usato finora nel codice e in AGENTS.md.
using ToolbarGroupFrame = FloatingGroup;

// Elenco a comparsa delle select flottanti, con lo stesso aspetto dell'elenco dei font della
// barra di formattazione (FontListPopup in nodeitem.cpp). Prima: SheetListPopup.
class FloatingListPopup final : public QFrame {
public:
    static constexpr int kRowHeight = 28;
    static constexpr int kMaxRows = 10;
    std::function<void(int)> chosen;  // indice della voce scelta
    // FEAT-004: set = the rows reorder by dragging; called after the row moved from -> to. The list
    // stays open with the new order and the current row (the active sheet) stays current.
    std::function<void(int, int)> moved;

    // Colours and text size of the list; themed() = the app's surface (floating selects, font list).
    // The Gantt's task panel passes its card's colours (light or dark with the Gantt page).
    struct Colors {
        QColor surface, border, text, hover, selected;
        int fontPixelSize = 14;
    };
    static Colors themed();

    FloatingListPopup(const QComboBox *source, int width, const Colors &colors = themed());

protected:
    void mousePressEvent(class QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *) override;

private:
    int dropGap(int y) const;  // insertion gap 0..count under the viewport y
    void updateDrag(const QPoint &viewportPos);
    void endDrag();
    QListWidget *m_list = nullptr;
    Colors m_colors;
    int m_dragRow = -1;  // row pressed, -1 = none
    QListWidgetItem *m_activeItem = nullptr;  // current row before the press (the active sheet)
    QPoint m_pressPos;
    bool m_dragging = false;
    bool m_swallowRelease = false;  // Esc cancelled a drag: the release must not pick the row
    QWidget *m_dropLine = nullptr;
    QTimer *m_autoScroll = nullptr;
    int m_autoScrollStep = 0;  // rows per tick while the pointer is near an edge
    QPoint m_lastDragPos;
};

// Select dei gruppi flottanti, disegnata a mano come i pulsanti dei gruppi: senza bordo, raggio 6,
// testo 14 px peso 600 a sinistra (troncato con i tre puntini), freccia a destra, elenco
// FloatingListPopup. Modificabile (setEditable) per lo zoom: il testo lo mostra allora il campo,
// trasparente e allineato al posto del testo disegnato. Prima: SheetComboBox.
class FloatingSelect final : public QComboBox {
public:
    explicit FloatingSelect(QWidget *parent = nullptr);

    // FEAT-004: set = the list reorders by dragging (FloatingListPopup::moved); the owner moves
    // its data and refills the select.
    std::function<void(int, int)> reordered;

    void showPopup() override;
    void hidePopup() override;
    // Lampeggio breve e controllato (due impulsi del colore d'accento): attira lo sguardo sulla
    // select senza bloccare nulla. Un lampeggio già in corso non riparte da capo.
    void flash();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    // Campo della select modificabile spostato o ridimensionato da QComboBox: torna al suo posto.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QRect textRect() const;
    QRect lineEditRect() const;  // posizione del campo della select modificabile
    void placeLineEdit();
    QPointer<FloatingListPopup> m_popup;
    QElapsedTimer m_popupClosed;
    bool m_pressed = false;
    qreal m_flash = 0;  // 0 = nessuna evidenziazione, 1 = massima
    QPointer<QVariantAnimation> m_flashAnimation;
};
