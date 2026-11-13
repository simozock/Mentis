#pragma once

// FEAT-051 tappa 2 (docs/features/FEAT-051-adesivi-e-marcatori.md): raccolte di adesivi e di
// marcatori, uguali per i due tipi. Una raccolta e' un elenco ordinato di elementi:
//   - adesivi: file della cartella dell'app o risorse di base (":/stickers/<raccolta>/...");
//   - marcatori: "serie:valore" per le serie disegnate, file per i gruppi propri.
// Le raccolte di base si rinominano, si riordinano e si nascondono (e si ripristinano), non si
// eliminano; quelle proprie si creano, rinominano, eliminano. Gli elementi si spostano fra le
// raccolte e si riordinano. Tutto vale per tutte le mappe: library.json e files/ nella cartella
// dati dell'app (AppDataLocation/stickers o /markers). Nomi in inglese come all'origine.

#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

namespace IconLibrary {

enum class Kind { Sticker, Marker };

struct Item {
    QString path;  // file, risorsa o (marcatori disegnati) "serie:valore"
    QString name;  // nome mostrato e cercato
};

struct Collection {
    QString id;          // "builtin:..." per quelle di base, altrimenti un uuid breve
    QString name;
    bool builtIn = false;
    bool hidden = false;
    QList<Item> items;
};

QString folder(Kind kind);
// Raccolte nell'ordine scelto, comprese le nascoste (hidden), con gli elementi gia' letti.
QList<Collection> collections(Kind kind);
Collection collection(Kind kind, const QString &id);
// English names (tappa 3: the search finds them in every language): a built-in collection's
// original name (empty for an own one), a built-in marker's label (an own item: its name).
QString englishName(Kind kind, const QString &collectionId);
QString englishName(Kind kind, const Item &item);
QString addCollection(Kind kind, const QString &name);  // nuova raccolta propria in fondo; id
bool renameCollection(Kind kind, const QString &id, const QString &name);
// Propria: eliminata con i suoi file; di base: nascosta. BUG-037: removed files (also by removeItem
// and the old file of renameItem) are deleted at the next start by removeDeletedFiles, because an
// open map may still show them before it has embedded its copy.
bool removeCollection(Kind kind, const QString &id);
void restoreBuiltIns(Kind kind);  // di nuovo visibili, con il nome e gli elementi d'origine
bool moveCollection(Kind kind, const QString &id, int toIndex);
// Copia il file nella cartella della libreria e lo mette nella raccolta (in fondo); percorso nuovo.
// name (tappa 3): the item's name, which becomes the file's name; empty = the file's own.
QString addFile(Kind kind, const QString &collectionId, const QString &path, const QString &name = QString());
// FEAT-070: a new file written from data, named fileName (made unique) in the library, at the end
// of the collection; new path, empty on failure.
QString addData(Kind kind, const QString &collectionId, const QString &fileName, const QByteArray &data);
// Sposta l'elemento (in qualsiasi raccolta) nella raccolta di arrivo alla posizione index (-1 = in fondo).
bool moveItem(Kind kind, const QString &path, const QString &toCollectionId, int index = -1);
bool removeItem(Kind kind, const QString &collectionId, const QString &path);
// FEAT-045: an own item gets a new name (a copy of its file, unique in the library; BUG-037); new path,
// empty for a built-in item, an empty name or a failure. Maps keep their embedded copies.
QString renameItem(Kind kind, const QString &path, const QString &name);
// BUG-037: deletes the files removed from the library in the previous sessions (called at start).
void removeDeletedFiles(Kind kind);
// Nuova raccolta con i file validi della cartella (il nome della cartella); id, vuoto se nessuno.
QString importFolder(Kind kind, const QString &folderPath, int *imported = nullptr);
// Nome mostrato di un file: senza estensione, "_" e "-" come spazi.
QString displayName(const QString &path);
// The marker an item of a marker collection gives a topic: "series:value" for a drawn one,
// "custom:<group>:<sha256>" for an own image, which is registered with Markers (its bytes in data,
// for the map's addMarkerImage). Empty when the file cannot be read.
QString markerFor(const Item &item, const QString &collectionId, QByteArray *data = nullptr);
QString customGroup(const QString &collectionId);  // without ":" (the marker's separator)

// Controlli dei file (uguali per marcatori e adesivi): SVG consigliato, PNG accettato; al massimo
// kMaxFileBytes; il contenuto deve leggersi come immagine (un SVG che non si disegna si rifiuta).
// small = immagine raster piu' piccola di quanto serve (sotto kMinMarkerPx o kMinStickerPx).
constexpr qint64 kMaxFileBytes = 2LL * 1024 * 1024;
constexpr int kMinMarkerPx = 64;
constexpr int kMinStickerPx = 256;
struct Check {
    bool ok = false;
    QString reason;     // gia' tradotto
    bool small = false;
    QString smallReason;
};
Check checkFile(Kind kind, const QString &path);
QString fileFilter();  // filtro della finestra di scelta: SVG, PNG e le altre immagini leggibili

}  // namespace IconLibrary
