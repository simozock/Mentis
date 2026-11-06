#pragma once

#include <QDialog>

class QLabel;
class QPushButton;
class QVBoxLayout;

// Modale comune (FEAT-027; scuro da FEAT-035, AGENTS.md "Modali" e "Interfaccia scura"): finestra
// senza cornice grande quanto la finestra principale, con un velo rgba(15,23,42,0.24) e al centro una
// superficie scura (raggio 8, padding 24, ombra leggera disegnata a mano, nessun effetto grafico).
// Titolo in alto, contenuto (contentLayout), separatore e in basso a destra "Annulla" e il bottone
// principale. Esc o "Annulla" chiudono senza modifiche (reject); Invio preme il bottone principale.
class ModalDialog : public QDialog {
    Q_OBJECT
public:
    ModalDialog(QWidget *parent, const QString &title, const QString &acceptText, int surfaceWidthPx);

    QVBoxLayout *contentLayout() const { return m_content; }
    QPushButton *acceptButton() const { return m_accept; }
    QPushButton *cancelButton() const { return m_cancel; }
    // Bottone in piu' a sinistra di "Annulla" (es. "Non salvare"); il clic chiude con kSecondaryResult.
    QPushButton *addSecondaryButton(const QString &text);
    // FEAT-054 (approved exception, ui-foundations.md): the dialog lays out its own buttons in the
    // content; separator, "Cancel" and the main button go away and an X beside the title closes it (reject).
    void useOwnButtons();
    // Unsaved changes (2026-11-05, user's request): no "Cancel", an X beside the title closes (reject,
    // like Esc); the secondary button goes to the left edge of the row, the main one stays on the right.
    void useCloseButton();
    // Rettangolo della superficie, in coordinate del modale (per i test e il disegno).
    QRect surfaceRect() const;

    static constexpr int kRadiusPx = 8;
    static constexpr int kPaddingPx = 24;
    static constexpr int kSecondaryResult = 2;

    // FEAT-035: domande e messaggi al posto di QMessageBox e QInputDialog (scuri come il resto).
    enum class Choice { Primary, Secondary, Cancel };
    // Testo e bottoni; secondaryText vuoto = solo principale e Annulla; cancelText vuoto = "Annulla".
    static Choice ask(QWidget *parent, const QString &title, const QString &text, const QString &primaryText,
                      const QString &secondaryText = QString(), const QString &cancelText = QString());
    // Like ask with two answers, laid out by useCloseButton: X or Esc = Cancel.
    static Choice askWithClose(QWidget *parent, const QString &title, const QString &text, const QString &primaryText,
                               const QString &secondaryText);
    // Messaggio con il solo bottone OK.
    static void inform(QWidget *parent, const QString &title, const QString &text);
    // Una riga di testo; ok = false se annullato. text: the field's starting text, selected (FEAT-004 rename).
    static QString askText(QWidget *parent, const QString &title, const QString &label, const QString &acceptText, bool *ok,
                           const QString &text = QString());

protected:
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    // BUG-040: a press on the scrim or on the surface's empty parts moves the main window with the modal.
    void mousePressEvent(QMouseEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *m_surface = nullptr;
    QVBoxLayout *m_content = nullptr;
    QPushButton *m_accept = nullptr;
    QPushButton *m_cancel = nullptr;
    QWidget *m_separator = nullptr;
    QPushButton *m_secondary = nullptr;
    QWidget *m_close = nullptr;  // useOwnButtons, useCloseButton
    QWidget *m_help = nullptr;   // FEAT-092
};
