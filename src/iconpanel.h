#pragma once

// FEAT-051 tappa 2 (docs/features/FEAT-051-adesivi-e-marcatori.md): pagine "Marcatore" e "Adesivo"
// del pannello laterale. Le raccolte (IconLibrary) sono gruppi pieghevoli con una
// griglia; un clic sceglie (chosen), il tasto destro modifica, il trascinamento riordina elementi e
// raccolte e sposta gli elementi fra le raccolte. In cima: nuova raccolta e "..." (aggiungi da file,
// importa una cartella, ripristina le raccolte di base), poi la ricerca (tappa 3). La pagina non conosce mappe e nodi: chi la
// ospita decide che cosa fare della scelta (nulla, senza un nodo selezionato).

#include "iconlibrary.h"

#include <QWidget>
#include <functional>

class QAbstractButton;
class QComboBox;
class QLineEdit;
class QPushButton;
class QTimer;
class QToolButton;
class QVBoxLayout;
class StickerEditorDialog;

class IconPage final : public QWidget {
    Q_OBJECT
public:
    explicit IconPage(IconLibrary::Kind kind, QWidget *parent = nullptr);
    IconLibrary::Kind kind() const { return m_kind; }
    void reload();
    // Marcatori del nodo selezionato: le celle corrispondenti si mostrano accese.
    void setActiveMarkers(const QStringList &markers);

    // Scelta di un elemento della raccolta collectionId.
    std::function<void(const IconLibrary::Item &item, const QString &collectionId)> chosen;
    // File aggiunti a una raccolta (dopo i controlli); uno solo va anche nel nodo selezionato.
    std::function<void(const QStringList &paths, const QString &collectionId)> filesAdded;
    // Sostituibili nelle prove: scelta dei file, della cartella e di un nome (vuoto = annullato).
    std::function<QStringList()> pickFiles;
    std::function<QString()> pickFolder;
    std::function<QString(const QString &title, const QString &current)> askName;
    // FEAT-070, replaceable in tests: run the editor (true = saved) and ask whether to delete the original.
    std::function<bool(StickerEditorDialog &dialog)> runEditor;
    std::function<bool(const QString &name)> askDeleteOriginal;

    // FEAT-070: edit an SVG sticker and save it as a new one; the original can then be deleted.
    void editSticker(const QString &path, const QString &collectionId);
    void addFiles(const QString &collectionId);   // vuoto = la prima raccolta propria (creata se manca)
    void importFolder();
    void newCollection();
    void renameCollection(const QString &id);
    void removeCollection(const QString &id);
    void restoreBuiltIns();
    void renameItem(const QString &path);  // own items only (tappa 3)
    // Rilascio di un trascinamento: elemento prima della posizione index (-1 = in fondo), raccolta
    // prima di quella beforeId (vuoto = in fondo).
    void dropItem(const QString &path, const QString &collectionId, int index);
    void dropCollection(const QString &id, const QString &beforeId);
    void setCollapsed(const QString &id, bool collapsed);

    QStringList shownPaths() const { return m_shown; }        // elementi visibili, nell'ordine
    QStringList shownCollections() const;                    // raccolte visibili, nell'ordine
    QAbstractButton *cell(const QString &path) const;
    QComboBox *categoryFilter() const { return m_category; }
    QLineEdit *searchField() const { return m_search; }
    static constexpr int kSearchDelayMs = 150;  // after the last key, before the grid is rebuilt

    // Colonne secondo la larghezza del pannello (richiesta dell'utente 2026-10-20): almeno queste,
    // di piu' allargando il pannello. La pagina non chiede piu' larghezza di quella minima.
    static constexpr int kStickerCellPx = 56;
    static constexpr int kStickerColumns = 4;
    static constexpr int kMarkerCellPx = 30;
    static constexpr int kMarkerColumns = 7;
    static constexpr int kCellSpacingPx = 4;
    int columns() const { return m_columns; }
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void showItemMenu(const QString &path, const QString &collectionId, const QPoint &globalPos);
    void showCollectionMenu(const QString &id, const QPoint &globalPos);
    QString ensureOwnCollection();
    QStringList checkedFiles(const QStringList &paths);
    bool collectionMatches(const IconLibrary::Collection &collection, const QString &query) const;
    bool itemMatches(const IconLibrary::Item &item, const QString &query) const;
    int columnsFor(int width) const;
    int minimumPageWidth() const;
    IconLibrary::Kind m_kind;
    int m_columns = 0;
    QComboBox *m_category = nullptr;
    QLineEdit *m_search = nullptr;
    QTimer *m_searchTimer = nullptr;
    QWidget *m_content = nullptr;
    QVBoxLayout *m_sections = nullptr;
    QPushButton *m_newCollection = nullptr;
    QToolButton *m_more = nullptr;
    QList<IconLibrary::Collection> m_collections;
    QStringList m_shown;
    QStringList m_active;
    QHash<QString, QAbstractButton *> m_cells;
};
