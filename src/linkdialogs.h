#pragma once

#include "modaldialog.h"

#include <QHash>
#include <QJsonObject>
#include <QList>

class QCheckBox;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

// FEAT-027: scelta dell'argomento da collegare. Albero di tutti i fogli con i loro nodi (forma
// piatta dello storico, MindMapModel::toFlatSnapshot) e ricerca: restano i nodi che contengono il
// testo, con i loro antenati. Il nodo di partenza non si puo' scegliere. La casella aggiunge il
// collegamento inverso; e' disattivata se il nodo scelto ha gia' un collegamento ad argomento.
class TopicLinkDialog final : public ModalDialog {
    Q_OBJECT
public:
    struct SheetContent {
        QString id;
        QString name;
        QHash<QString, QJsonObject> content;
    };
    TopicLinkDialog(QWidget *parent, const QList<SheetContent> &sheets, const QString &sourceId, const QString &currentTargetId);

    QString targetId() const;
    QString targetSheetId() const;
    bool alsoBack() const;
    QLineEdit *searchField() const { return m_search; }
    QTreeWidget *tree() const { return m_tree; }
    QCheckBox *backCheck() const { return m_back; }
    // Mostra solo i nodi che contengono text (maiuscole ignorate) e i loro antenati.
    void filter(const QString &text);
    // Seleziona il nodo con quell'id (per i test e per la modifica di un collegamento).
    bool selectNode(const QString &id);

private:
    void updateButtons();
    QLineEdit *m_search = nullptr;
    QTreeWidget *m_tree = nullptr;
    QCheckBox *m_back = nullptr;
    QList<QTreeWidgetItem *> m_nodeItems;  // tutti i nodi, nell'ordine dell'albero
};

// FEAT-027: indirizzo di una pagina web. Il bottone si attiva solo con un indirizzo valido
// (NodeLinks::webAddress); sotto il campo il motivo se lo schema non e' ammesso.
class WebLinkDialog final : public ModalDialog {
    Q_OBJECT
public:
    WebLinkDialog(QWidget *parent, const QString &currentAddress);
    QString address() const;  // indirizzo completo, vuoto se non valido
    QLineEdit *addressField() const { return m_address; }

private:
    QLineEdit *m_address = nullptr;
    QLabel *m_error = nullptr;
};
