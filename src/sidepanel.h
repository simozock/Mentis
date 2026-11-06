#pragma once

#include <QWidget>

class QScrollArea;
class QStackedWidget;
class SegmentedControl;

// Contenitore riutilizzabile del pannello laterale (FEAT-017): selettore a segmenti fisso in alto
// e, sotto, una sola superficie arrotondata che fa scorrere il contenuto della pagina attiva (ogni
// pagina con il suo scorrimento e la sua altezza). Le
// pagine sono create dal chiamante una sola volta e qui soltanto mostrate; il contenitore non
// conosce mappe, nodi o fogli.
class SidePanel final : public QWidget {
    Q_OBJECT

public:
    explicit SidePanel(QWidget *parent = nullptr);

    int addPage(const QString &label, QWidget *page);
    int count() const;
    int currentPage() const;
    QWidget *page(int index) const;
    // Cambio programmatico (contesto): non emette pageActivated.
    void setCurrentPage(int index);
    SegmentedControl *selector() const { return m_selector; }

signals:
    // Pagina scelta dall'utente con il selettore.
    void pageActivated(int index);
    // La larghezza minima del contenuto e' cambiata (BUG-010): chi ospita il pannello rilegge
    // minimumSizeHint().
    void contentMinimumWidthChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // BUG-010: le pagine scorrono solo in verticale, quindi il pannello non deve stringersi sotto
    // la pagina piu' larga (anche quelle non visibili, per non cambiare larghezza a ogni pagina).
    void updateContentMinimumWidth();
    SegmentedControl *m_selector = nullptr;
    // Una QScrollArea per pagina (bug 2026-10-21): con una sola area attorno alle pagine impilate
    // ogni pagina era alta quanto la piu' alta (QStackedWidget), quindi i Marcatori scorrevano come
    // gli Adesivi, e lo scorrimento passava da una pagina all'altra.
    QStackedWidget *m_pages = nullptr;
    QScrollArea *scrollOf(int index) const;
};
