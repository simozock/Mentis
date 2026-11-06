#pragma once

#include "modaldialog.h"

#include <QStringList>

class QLineEdit;
class QToolButton;
class PalettePreview;

// FEAT-032: modale comune "Crea nuovo tema colore" (scuro, FEAT-035). Sei quadrati vuoti: il clic
// apre il selettore colori comune e il colore scelto riempie il quadrato; l'ordine e' quello dei
// rami principali. Campo del nome (vuoto = "Personalizzata N"), anteprima dei livelli con le regole
// di FEAT-031 (NodeStyle::resolveTree). "Salva" e' attivo con 6 colori e un nome non gia' usato.
// Il modale non salva nulla: chi lo apre legge colors() e paletteName() dopo exec() == Accepted.
class NewPaletteDialog final : public ModalDialog {
    Q_OBJECT
public:
    explicit NewPaletteDialog(QWidget *parent);

    QStringList colors() const { return m_colors; }  // sei voci, vuote quelle non ancora scelte
    QString paletteName() const;                      // nome scritto o quello predefinito
    QToolButton *swatch(int index) const { return m_swatches.value(index); }
    QLineEdit *nameField() const { return m_name; }
    QLabel *errorLabel() const { return m_error; }
    // Colore di un quadrato, come una scelta nel selettore (anche per i test).
    void setColor(int index, const QColor &color);

private:
    void updateState();

    QList<QToolButton *> m_swatches;
    QStringList m_colors;
    QLineEdit *m_name = nullptr;
    QLabel *m_error = nullptr;
    PalettePreview *m_preview = nullptr;
};
