#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// FEAT-097 stage A: the maps kept in the app's own storage, and the copy of a map chosen with the
// system file picker (MapFolder::bringIn, FEAT-105). SQLite needs a real path, and on Android the picker gives a content:// URI,
// which Qt's QFile reads through its Android content file engine (no Qt page documents it; checked
// on the emulator, FEAT-097), so the map is copied into the storage and opened there. Stage A only
// reads: nothing is written back.
class MapLibrary : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList maps READ maps NOTIFY mapsChanged)
    Q_PROPERTY(QString folder READ folder NOTIFY folderChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    // FEAT-107: the Home shows one folder level at a time, as the desktop's (FEAT-105 decision 6).
    Q_PROPERTY(QString shown READ shown NOTIFY shownChanged)
    Q_PROPERTY(QString shownName READ shownName NOTIFY shownChanged)
    Q_PROPERTY(bool atRoot READ atRoot NOTIFY shownChanged)
    Q_PROPERTY(QVariantList folders READ folders NOTIFY mapsChanged)
    Q_PROPERTY(QVariantList folderColors READ folderColors CONSTANT)

public:
    // folder: where the maps are kept; empty = the Mentis folder (MapFolder::current).
    explicit MapLibrary(QObject *parent = nullptr) : MapLibrary(QString(), parent) {}
    MapLibrary(const QString &folder, QObject *parent);

    QString folder() const { return m_folder; }
    // The .mentis files of the level shown, the most recently changed first: {name, path, url, modified}.
    QVariantList maps() const { return m_maps; }
    // The subfolders of the level shown, by name: {name, path, color} (color "" = none).
    QVariantList folders() const { return m_folders; }
    QString lastError() const { return m_lastError; }
    QString shown() const { return m_shown; }
    QString shownName() const;
    bool atRoot() const;
    // The fixed palette: {color, name}, "No colour" first with an empty color.
    static QVariantList folderColors();

    // FEAT-107: the level shown; a path outside the Mentis folder shows the Mentis folder.
    Q_INVOKABLE void enter(const QString &folder);
    Q_INVOKABLE void up();
    // Each true on success, otherwise false with the reason in lastError.
    Q_INVOKABLE bool createFolder(const QString &name);
    Q_INVOKABLE bool renameFolder(const QString &folder, const QString &name);
    Q_INVOKABLE bool setFolderColor(const QString &folder, const QString &color);
    Q_INVOKABLE bool removeFolder(const QString &folder);
    Q_INVOKABLE static bool isEmptyFolder(const QString &folder);
    Q_INVOKABLE static QString folderColor(const QString &folder);
    // Where "Move to…" can put a map or a folder: {label ("Mentis › Exams"), path, here (the folder
    // it is in)}; a folder's own subtree is left out.
    Q_INVOKABLE QVariantList moveTargets(const QString &entry) const;
    Q_INVOKABLE bool moveMap(const QString &map, const QString &folder);
    Q_INVOKABLE bool moveFolder(const QString &folder, const QString &target);

    // Also finishes the moves of maps that were open (MapFolder::runPendingMoves).
    Q_INVOKABLE void refresh();
    // FEAT-105: the Mentis folder read again (the access to the shared storage given): when it
    // changed, the maps of the old one are moved there (MapFolder::moveAllTo). The number moved.
    // Only for the library made without a folder.
    Q_INVOKABLE int reload();
    // The local path to open for this URL: a file of the storage as it is, anything else (another
    // local file, a content:// URI) copied into the storage first. A copy with the same name and
    // the same bytes is reused; a different one gets a free name. Empty on failure (lastError).
    // created: set when a new copy was written.
    QString localCopy(const QUrl &url, bool *created = nullptr);
    // Removes a copy made by localCopy that turned out not to be a map.
    bool discard(const QString &path);

signals:
    void mapsChanged();
    void folderChanged();
    void lastErrorChanged();
    void shownChanged();

private:
    void setError(const QString &error);

    QString m_folder;
    QString m_shown;  // the level shown, m_folder or one of its subfolders
    QVariantList m_folders;
    bool m_followsSettings = false;  // made without a folder: the Mentis folder
    QVariantList m_maps;
    QString m_lastError;
};
