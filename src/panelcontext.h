#pragma once

#include <QString>
#include <QStringList>

// Contesto del pannello laterale (FEAT-017): sceglie la pagina da mostrare in base alla selezione
// e alle scelte dell'utente. Non conosce widget e non scrive dati del documento: riceve solo
// identificatori stabili dei nodi selezionati e del foglio attivo.
//
// Regole (scheda FEAT-017, "Comportamento dettagliato" e "Decisioni confermate"):
// - zero nodi selezionati -> Mappa; uno o piu' nodi -> Stile (il nodo centrale compreso);
// - una scelta manuale resta finche' la selezione non cambia;
// - riaprendo il pannello il contesto si ricalcola e la scelta manuale non viene ricordata.
class PanelContext {
public:
    // Ordine dei segmenti nel selettore: Stile, Pitch, Mappa.
    enum class Page { Style = 0, Pitch = 1, Map = 2 };

    // Selezione corrente (nell'ordine della scena). Se e' diversa dalla precedente la pagina
    // torna quella contestuale; se e' la stessa (segnale ripetuto) la pagina non cambia.
    Page selectionChanged(const QStringList &nodeIds);
    // Pagina scelta dall'utente: non cambia la selezione.
    Page manualChoice(Page page);
    // Pannello riaperto: pagina contestuale della selezione corrente.
    Page reopened(const QStringList &nodeIds);
    // Foglio attivo, per la pagina Mappa. Non cambia pagina ne' dati.
    void setActiveSheet(const QString &sheetId) { m_activeSheet = sheetId; }

    Page current() const { return m_current; }
    const QStringList &selection() const { return m_selection; }
    const QString &activeSheet() const { return m_activeSheet; }
    // I controlli di Stile agiscono su un solo nodo; con piu' nodi restano disabilitati finche'
    // FEAT-019 non introduce il comando batch.
    bool styleControlsEnabled() const { return m_selection.size() == 1; }

    static Page contextualPage(const QStringList &nodeIds) { return nodeIds.isEmpty() ? Page::Map : Page::Style; }

private:
    Page m_current = Page::Map;
    QStringList m_selection;
    QString m_activeSheet;
};
