#pragma once

#include <QFrame>
#include <QPointer>

class QLineEdit;
class QTextEdit;
class QToolButton;
class QWidget;

// FEAT-059: riquadro scuro per scrivere la nota di un nodo, accanto al nodo (o alla riga
// dell'outliner). Testo formattato base: grassetto, corsivo, sottolineato, barrato,
// elenchi puntati e numerati, titolo. Resta aperto finche' si clicca fuori o si preme Esc; alla
// chiusura emette finished con il testo semplice e l'HTML (un passo di storico, deciso da chi lo
// apre). Figlio della finestra principale: non e' una finestra a se'.
//
// Modo Inline (outliner, richiesta 2026-10-19): nessuna superficie scura; il campo sta
// esattamente sul riquadro grigio della nota sotto il titolo e la barra dei formati e' un piccolo
// gruppo chiaro flottante sopra il testo. L'altezza segue il testo (contentHeightChanged).
class NoteEditor final : public QFrame {
    Q_OBJECT
public:
    enum class Mode { Popup, Inline };
    explicit NoteEditor(QWidget *parent, Mode mode = Mode::Popup);
    ~NoteEditor() override;

    // Apre per il nodo con l'id dato, accanto ad anchor (coordinate globali): a destra, o a sinistra
    // se a destra non c'e' spazio. html vuoto = testo semplice.
    void openFor(const QString &nodeId, const QString &plain, const QString &html, const QRect &anchor);
    // Nuova posizione con il nodo che si e' spostato (scorrimento o zoom della mappa).
    void moveNextTo(const QRect &anchor);
    // Modo Inline: apre sul riquadro box (coordinate del genitore) con il font e i colori della riga.
    void openInline(const QString &nodeId, const QString &plain, const QString &html, const QRect &box,
                    const QFont &font, const QColor &text, const QColor &background, int paddingX, int paddingY);
    void moveInline(const QRect &box);
    Mode mode() const { return m_mode; }
    // Chiude e, se aperto, emette finished.
    void commitAndClose();
    QString nodeId() const { return m_nodeId; }
    bool isOpen() const { return isVisible(); }
    QTextEdit *textEdit() const { return m_text; }
    QToolButton *formatButton(const QString &name) const;  // "bold", "italic", ... (per i test)
    QLineEdit *linkField() const { return m_linkField; }
    // Collegamento sul testo selezionato (indirizzo vuoto = toglierlo); se non c'e' selezione si
    // inserisce l'indirizzo stesso come testo collegato.
    void applyLink(const QString &typed);
    static const QColor &linkColor();

    static constexpr int kWidthPx = 360;
    static constexpr int kMinHeightPx = 120;
    static constexpr int kMaxHeightPx = 400;

signals:
    void finished(const QString &nodeId, const QString &plain, const QString &html);
    void contentHeightChanged(int height);  // modo Inline: altezza del riquadro con il testo

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QToolButton *addButton(const QString &name, const QString &icon, const QString &tip, bool checkable);
    void updateButtons();
    void fitHeight();
    Mode m_mode = Mode::Popup;
    QWidget *m_buttonHost = nullptr;   // dove stanno i bottoni: il riquadro o la barra flottante
    QFrame *m_toolbarFrame = nullptr;  // modo Inline
    QColor m_background;
    int m_paddingX = 0, m_paddingY = 0, m_lastHeight = -1;
    QTextEdit *m_text = nullptr;
    QWidget *m_linkRow = nullptr;      // campo dell'indirizzo, sotto la barra
    QLineEdit *m_linkField = nullptr;
    QList<QToolButton *> m_buttons;
    QString m_nodeId;
    QRect m_anchor;
    bool m_typingShortcut = false;  // FEAT-068: tasto passato al campo dal filtro
};
