#pragma once

// FEAT-026 fase C: scelta del tipo di mappa (struttura) e della sua direzione, per il foglio (pagina
// Mappa) o per un ramo (pagina Stile, con "Come il genitore"). Due select scure una sotto l'altra,
// con le miniature in assets/structures/. Non conosce mappe e nodi: mostra un valore e dice la scelta.

#include <QWidget>

#include "mapstructure.h"

class QComboBox;

class StructurePicker final : public QWidget {
    Q_OBJECT
public:
    // allowInherit: prima voce "Come il genitore" (tipo vuoto), per i rami. withLabel: riga "Struttura"
    // con la select a destra; senza, la select del tipo la mette chi ospita (intestazione di sezione)
    // e qui resta solo la direzione.
    StructurePicker(bool allowInherit, bool withLabel, QWidget *parent = nullptr);
    // Tipo vuoto = ereditato (solo con allowInherit); la direzione si mostra solo per un tipo scelto.
    void setValue(const QString &typeKey, const QString &directionKey);
    QComboBox *typeSelect() const { return m_type; }
    static QString typeName(MapStructure::Type type);  // nome mostrato (anche nella galleria)
    QComboBox *directionSelect() const { return m_direction; }
    // FEAT-028 stage 2: inside a tree table the Mind map type is not offered.
    void setInTable(bool inTable);

signals:
    // Scelta dell'utente (non emesso da setValue). Cambiando tipo la direzione e' la predefinita.
    void chosen(const QString &typeKey, const QString &directionKey);

private:
    void fillDirections(const QString &typeKey, const QString &directionKey);
    bool m_allowInherit;
    QComboBox *m_type = nullptr;
    QComboBox *m_direction = nullptr;
};
