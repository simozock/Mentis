#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QStringList>
#include <QString>
#include <QUrl>

// FEAT-105: the Mentis folder, where the maps are kept on desktop and phone. Widget-free, shared by
// the desktop window and the phone's MapLibrary.
class MapFolder {
    Q_DECLARE_TR_FUNCTIONS(MapFolder)

public:
    // The folder chosen in Settings (QSettings "mentisFolder"); empty = the default location.
    static QString configuredPath();
    static void setConfiguredPath(const QString &path);
    // Desktop: Documents/Mentis. Android: Mentis at the root of the shared storage once the access
    // to all files is given (sharedStorageAllowed), Mentis in the app's own folder before.
    static QString defaultPath();
    // Where the maps go when the default location cannot be used. Android: the app's own folder,
    // or its internal storage when that is the default or missing. Desktop: the default location.
    static QString fallbackPath();
    // Android: the access to all files (Android 11 and later) or the storage permission is given.
    // Always false elsewhere.
    static bool sharedStorageAllowed();
    // The folder from the settings, with the default location as fallback.
    static MapFolder current();

    // folder: the folder to use (empty = fallback); fallback: where to go when it is unusable.
    explicit MapFolder(const QString &folder, const QString &fallback = QString());

    struct Ready {
        QString path;     // the folder to use now, empty when not even the fallback could be made
        QString warning;  // why the chosen folder was not used, for the user
    };
    // Makes the folder if it is missing and its parent exists; otherwise, or when it is not
    // writable, the fallback. Removes the .mentis.part files a cut copy left in the folder itself
    // (a subfolder's are removed when maps() lists it).
    Ready ensure() const;

    // True when the file is in the folder or in one of its subfolders.
    bool contains(const QString &filePath) const;

    struct BringIn {
        QString path;   // the map to open, empty on failure
        bool created = false;  // a new copy was written (remove it if it is not a map)
        QString error;
    };
    // The local path to open for this URL: a file of the folder as it is, anything else (another
    // local file, a content:// URI) copied into targetDirectory (empty = the folder) first. The same
    // original brought in again reuses its copy (found by the original's SHA-256); anything else gets
    // a free name ("Map (2).mentis").
    // The copy is written to a .part name and renamed at the end: a cut copy never looks like a map.
    BringIn bringIn(const QUrl &url, const QString &targetDirectory = QString()) const;
    // A copy removed (not a map) or a map saved at path: the original brought in again must not
    // reuse what is at path now.
    static void forgetCopy(const QString &path);

    struct Map {
        QString name;  // file name without .mentis
        QString path;
        QDateTime modified;
    };
    // The .mentis files of a directory (empty = the folder), the most recently changed first; the
    // directory's cut copies (.mentis.part) are removed.
    QList<Map> maps(const QString &directory = QString()) const;

    struct Folder {
        QString name;
        QString path;
        QString color;  // FEAT-107: "#rrggbb" from folderColors(), empty = no colour
    };
    // The subfolders of a directory (empty = the folder), by name; links and junctions left out.
    QList<Folder> folders(const QString &directory = QString()) const;
    // The folder and every subfolder, depth first, by name: the places "Move to…" offers.
    QStringList allFolders() const;

    // FEAT-105 decision 4: subfolders made, renamed and removed from the Home. Each returns the new
    // path (empty on failure, with the reason in error). A taken name gets a free one ("Exams (2)").
    QString createFolder(const QString &parent, const QString &name, QString *error) const;
    QString renameFolder(const QString &path, const QString &name, QString *error) const;
    // Moves a folder (its maps, subfolders and colour) into targetDirectory with a free name; already
    // there = its path. Refused into itself or one of its subfolders, or while a map in it is open;
    // source and target must be inside this folder (the folder itself does not move).
    QString moveFolder(const QString &path, const QString &targetDirectory, QString *error) const;
    // Only an empty folder: a map is never deleted by removing its folder.
    bool removeFolder(const QString &path, QString *error) const;
    // Empty but for its colour file (FEAT-107).
    static bool isEmptyFolder(const QString &path);

    // FEAT-107: the folders' fixed palette, and a folder's colour kept in a hidden file inside it
    // (".mentis-folder", JSON), so it follows the folder to another device. Empty = no colour; a
    // missing or unreadable file = no colour.
    static QStringList folderColors();
    // The colours' names, in the same order, for menus and screen readers.
    static QStringList folderColorNames();
    static QString folderColor(const QString &folderPath);
    static bool setFolderColor(const QString &folderPath, const QString &color, QString *error);

    struct Move {
        QString path;           // where the map is now (where it will go, when deferred)
        bool deferred = false;  // open in a Mentis window: moved once it is closed (decision 7)
        QString error;
    };
    // Moves a map into targetDirectory with a free name; the folder it is already in = nothing.
    Move moveMap(const QString &mapPath, const QString &targetDirectory) const;
    // The deferred moves whose maps are closed now are done: (old path, new path) pairs, for the
    // recent list. A map still open keeps waiting; one whose file or target vanished is forgotten.
    static QList<QPair<QString, QString>> runPendingMoves();

    struct MoveAll {
        QList<QPair<QString, QString>> moved;  // (old path, new path), for the recent list
        int deferred = 0;                      // open maps that move once they are closed
        QStringList errors;
    };
    // FEAT-105 decision 5: every map of the folder moved to target, subfolders kept (empty ones
    // too; one with other files only is left alone), free names, open maps deferred. Old subfolders left empty are removed; the folder
    // itself stays. A target inside the folder is skipped while walking it.
    MoveAll moveAllTo(const QString &target) const;
    // path after from was moved to to: the same path when it was not from or inside it.
    static QString moved(const QString &path, const QString &from, const QString &to);
    // True while a Mentis window holds the map's lock (Document::acquireLock).
    static bool isOpen(const QString &mapPath);

    // A file name for a map titled so: characters not allowed on Windows (the strictest system)
    // replaced, "Map" when nothing is left, ".mentis" added.
    static QString fileNameFor(const QString &title);
    // "fileName", or "Base (2).mentis", "Base (3).mentis"... the first that does not exist in directory,
    // neither as a map nor as a map's -journal, -wal, -shm or .lock.
    static QString freeName(const QString &directory, const QString &fileName);

    QString path() const { return m_folder; }

private:
    QString m_folder;
    QString m_fallback;
};
