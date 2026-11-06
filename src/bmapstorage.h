#pragma once

#include <QString>
#include <QJsonObject>
#include <QList>
#include <QHash>
#include <QByteArray>

#include <optional>

class MindMapModel;

// Persistenza del documento Mentis. L'estensione resta .mentis, ma il file e'
// un database SQLite transazionale e predisposto per piu' mappe nello stesso documento.
class BmapStorage final {
public:
    struct MapInfo { QString id; QString title; int sortOrder = 0; };
    enum class SaveReason { Automatic, Explicit, Closing };

    // assetSourcePath (B11, 2026-10-13): file .mentis da cui la mappa è stata aperta, se diverso da
    // path ("Salva con nome"). Le immagini e gli allegati già incorporati vengono copiati da lì.
    static bool save(const QString &path, const MindMapModel &model, SaveReason reason,
                     QString *error = nullptr, const QString &assetSourcePath = QString());
    static bool saveDocument(const QString &path, const QJsonObject &document, SaveReason reason,
                             QString *error = nullptr, const QString &assetSourcePath = QString(),
                             const QString &mapId = QStringLiteral("main"),
                             const QString &mapTitle = QString(), int sortOrder = 0);
    // integrityCheck (P8, 2026-10-13): PRAGMA quick_check legge tutto il file; serve solo dopo
    // una chiusura anomala o con un giornale SQLite rimasto accanto al file.
    static bool load(const QString &path, MindMapModel &model, QString *error = nullptr,
                     bool integrityCheck = true, const QString &mapId = QStringLiteral("main"));
    static QList<MapInfo> maps(const QString &path, QString *error = nullptr);

    // Fogli del documento in forma "piatta", la stessa dello storico (MindMapModel::toFlatSnapshot):
    // una voce per nodo con "parent" e "row", più "@map" con rootId, relazioni e palette.
    using FlatMap = QHash<QString, QJsonObject>;
    struct SheetWrite {
        QString id;
        QString title;
        int sortOrder = 0;
        // false = il contenuto nel file è già aggiornato: si scrivono solo titolo e ordine.
        // Se il foglio nel file non esiste ancora, content viene scritto comunque.
        bool writeContent = true;
        FlatMap content;
    };
    // 2026-10-14: tutti i fogli in UNA transazione (prima una per foglio più una per eliminare i
    // fogli tolti: un errore a metà lasciava fogli vecchi e nuovi mescolati). I fogli non elencati
    // vengono eliminati. Salvataggi espliciti e di chiusura aggiungono un solo punto di ripristino
    // con l'intero documento. previewJpeg, se non vuota, sostituisce l'anteprima della Home.
    // FEAT-079: resources, if given, replaces the document's resources (metadata key "resources";
    // an empty object removes it) in the same transaction.
    static bool saveSheets(const QString &path, const QList<SheetWrite> &sheets, SaveReason reason,
                           QString *error = nullptr, const QString &assetSourcePath = QString(),
                           const QByteArray &previewJpeg = QByteArray(),
                           const std::optional<QJsonObject> &resources = std::nullopt);
    // FEAT-079: the document's resources (GanttResources::toJson); empty if absent or unreadable.
    static QJsonObject loadResources(const QString &path);
    // Anteprima della Home salvata nel documento (vuota se assente o se il file non si legge).
    static QByteArray loadPreview(const QString &path);
    static bool savePreview(const QString &path, const QByteArray &jpeg, QString *error = nullptr);
    // FEAT-037: copia nella cache locale dell'originale di un'immagine incorporata (la mappa di solito
    // disegna la miniatura da 512 px). Sola lettura; la copia si scrive una volta e poi si riusa.
    // Vuoto se il file o l'immagine non si leggono: chi chiama resta sulla miniatura.
    static QString originalAssetPath(const QString &path, const QString &assetId, const QString &fileName,
                                     QString *error = nullptr);
    static bool removeMapsExcept(const QString &path, const QStringList &mapIds, QString *error = nullptr);
    static bool isBmapDatabase(const QString &path);
};
