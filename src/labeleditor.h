#pragma once

#include <QFrame>
#include <QStringList>

class QLabel;
class QLineEdit;
class ToggleSwitch;

// FEAT-060: riquadro scuro "Etichette" sotto le pillole del nodo (sopra se manca spazio), come quello della nota (FEAT-059): interruttore "Ordinamento automatico", campo con le etichette separate da virgole e, sotto,
// le etichette del documento come pillole che si aggiungono con un clic. Si chiude con Invio, Esc o
// un clic fuori ed emette finished (un passo di storico, deciso da chi lo apre). Figlio della
// finestra principale.
class LabelEditor final : public QFrame {
    Q_OBJECT
public:
    explicit LabelEditor(QWidget *parent);
    ~LabelEditor() override;

    // Apre per il nodo con l'id dato sotto anchor (coordinate globali), o sopra se sotto non c'e'
    // spazio; suggestions = etichette
    // del documento.
    void openFor(const QString &nodeId, const QStringList &labels, const QStringList &suggestions, const QRect &anchor);
    void moveNextTo(const QRect &anchor);
    void commitAndClose();
    bool isOpen() const { return isVisible(); }
    QString nodeId() const { return m_nodeId; }
    QLineEdit *field() const { return m_field; }
    ToggleSwitch *autoSortSwitch() const { return m_autoSort; }
    // Etichette scritte nel campo (separate da virgole), gia' ripulite.
    QStringList typedLabels() const;
    // Aggiunge un suggerimento al campo (se non c'e' gia'), come il clic sulla sua pillola.
    void addSuggestion(const QString &label);
    QStringList visibleSuggestions() const;  // per i test

    static constexpr int kWidthPx = 320;

signals:
    void finished(const QString &nodeId, const QStringList &labels);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    class Suggestions;
    void refreshSuggestions();
    QLineEdit *m_field = nullptr;
    ToggleSwitch *m_autoSort = nullptr;
    QLabel *m_suggestionsTitle = nullptr;
    Suggestions *m_suggestions = nullptr;
    QStringList m_allSuggestions;
    QString m_nodeId;
    QRect m_anchor;
};
