#pragma once

#include "modaldialog.h"

class QLabel;
class QPlainTextEdit;

// FEAT-064: modale comune per scrivere la formula LaTeX del nodo. Campo del codice (spaziatura
// fissa), sotto la formula disegnata mentre si scrive su un riquadro chiaro, e il messaggio d'errore
// del parser se il codice non e' valido (il bottone principale resta spento). Ctrl+Invio conferma.
class FormulaDialog final : public ModalDialog {
    Q_OBJECT
public:
    FormulaDialog(QWidget *parent, const QString &code, const QColor &background = Qt::white);

    QString code() const;
    QColor background() const { return m_background; }
    class ColorField *backgroundField() const { return m_backgroundField; }
    QPlainTextEdit *field() const { return m_field; }
    QLabel *errorLabel() const { return m_error; }
    bool isValid() const { return m_valid; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    class Preview;
    void refresh();
    QPlainTextEdit *m_field = nullptr;
    Preview *m_preview = nullptr;
    QLabel *m_error = nullptr;
    class ColorField *m_backgroundField = nullptr;
    QColor m_background = Qt::white;
    bool m_valid = false;
};
