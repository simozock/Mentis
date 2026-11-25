#pragma once

#include <QDialog>

class QAbstractButton;
class QLabel;
class QPixmap;
class QPushButton;
class QToolButton;
class QVBoxLayout;

// Modale comune (FEAT-027; scuro da FEAT-035, AGENTS.md "Modali" e "Interfaccia scura"): finestra
// senza cornice grande quanto la finestra principale, con un velo rgba(15,23,42,0.24) e al centro una
// superficie scura (raggio 8, padding 24, ombra leggera disegnata a mano, nessun effetto grafico).
// Title with an X at its right, content (contentLayout), separator and the main button at the bottom
// right. Esc or the X close without changes (reject); Enter presses the main button (no "Cancel"
// button since 2026-11-23, user's request); Ctrl+Enter presses it from any field, multi-line too.
class ModalDialog : public QDialog {
    Q_OBJECT
public:
    ModalDialog(QWidget *parent, const QString &title, const QString &acceptText, int surfaceWidthPx);

    QVBoxLayout *contentLayout() const { return m_content; }
    QPushButton *acceptButton() const { return m_accept; }
    QAbstractButton *closeButton() const;
    // A second answer at the left edge of the buttons row (e.g. "Don't save"); closes with kSecondaryResult.
    QPushButton *addSecondaryButton(const QString &text);
    // FEAT-132: a button at the left edge of the buttons row, styled as the secondary one, that does not close
    // the dialog (the owner connects it); e.g. "Symbols and formulas..." of the formula modal.
    QPushButton *addSideButton(const QString &text);
    // FEAT-054 (approved exception, ui-foundations.md): the dialog lays out its own buttons in the
    // content; the separator and the main button go away (the X stays).
    void useOwnButtons();
    // FEAT-127: a button in the title row, at the left of "?" and the X (28 x 28, styled as them).
    void addTitleButton(QAbstractButton *button);
    // FEAT-127: the surface's width changed while open (the Resources modal with its charts beside).
    void setSurfaceWidth(int surfaceWidthPx);
    // Rettangolo della superficie, in coordinate del modale (per i test e il disegno).
    QRect surfaceRect() const;

    static constexpr int kRadiusPx = 8;
    static constexpr int kPaddingPx = 24;
    static constexpr int kSecondaryResult = 2;

    // FEAT-035: domande e messaggi al posto di QMessageBox e QInputDialog (scuri come il resto).
    enum class Choice { Primary, Secondary, Cancel };
    // Text and buttons; empty secondaryText = only the main button. The X or Esc = Cancel.
    static Choice ask(QWidget *parent, const QString &title, const QString &text, const QString &primaryText,
                      const QString &secondaryText = QString());
    // Messaggio con il solo bottone OK.
    static void inform(QWidget *parent, const QString &title, const QString &text);
    // BUG-046: inform with the mascot (face, already at kMascotPx) at the left of the text, centred
    // vertically, as in the tour's bubble (docs/design/ui-components.md, "Modali").
    static void informWithMascot(QWidget *parent, const QPixmap &face, const QString &title, const QString &text);
    static constexpr int kMascotPx = 72;
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
    // The first enabled, visible, editable text or number field in the focus chain; nullptr if none.
    QWidget *firstInputField() const;

    QWidget *m_surface = nullptr;
    QVBoxLayout *m_content = nullptr;
    QPushButton *m_accept = nullptr;
    QWidget *m_separator = nullptr;
    QPushButton *m_secondary = nullptr;
    QToolButton *m_close = nullptr;
    QWidget *m_help = nullptr;   // FEAT-092
};
