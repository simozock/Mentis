// FEAT-105: the Mentis folder (MapFolder): free names, reuse by content, cut copies, the fallback
// when the chosen folder is missing or not writable.
#include "mapfolder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class MapFolderTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
        QCoreApplication::setApplicationName(QStringLiteral("MentisMapFolderTest"));
    }
    void freeNameAndFileNameForTitle();
    void bringInCopiesReusesAndNeverOverwrites();
    void bringInLeavesNoPartOnFailure();
    void ensureRemovesCutCopies();
    void ensureFallsBackWhenTheFolderCannotBeReached();
    void containsSubfoldersOnly();
    void subfoldersAreMadeRenamedAndRemoved();
    void mapMovesWithAFreeName();
    void openMapMovesOnceClosed();
    void everyMapMovesToANewFolder();
    void aNewFolderInsideTheOldOneIsSkipped();
    void aDiscardedCopyIsNotReused();
    void aNameWithAnOrphanJournalIsTaken();
    void foldersOfOtherFilesStayWhenMapsMove();
    void linkedFoldersAreNotFollowed();
    void aDeferredMoveFollowsItsRenamedFolder();
    void aFolderKeepsItsColour();
    void aFolderMovesIntoAnother();
    void aDeferredMoveFollowsItsMovedFolder();

private:
    static void write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
    }
};

void MapFolderTest::freeNameAndFileNameForTitle() {
    QTemporaryDir dir;
    QCOMPARE(MapFolder::freeName(dir.path(), QStringLiteral("Map.mentis")), QStringLiteral("Map.mentis"));
    write(dir.filePath(QStringLiteral("Map.mentis")), "a");
    write(dir.filePath(QStringLiteral("Map (2).mentis")), "b");
    QCOMPARE(MapFolder::freeName(dir.path(), QStringLiteral("Map.mentis")), QStringLiteral("Map (3).mentis"));

    QCOMPARE(MapFolder::fileNameFor(QStringLiteral("Exam: a/b?")), QStringLiteral("Exam- a-b-.mentis"));
    QCOMPARE(MapFolder::fileNameFor(QStringLiteral("  Plan.  ")), QStringLiteral("Plan.mentis"));
    QCOMPARE(MapFolder::fileNameFor(QStringLiteral("back\\slash")), QStringLiteral("back-slash.mentis"));
    QCOMPARE(MapFolder::fileNameFor(QString()), QStringLiteral("Map.mentis"));
}

void MapFolderTest::bringInCopiesReusesAndNeverOverwrites() {
    QTemporaryDir outside, root;
    const QString original = outside.filePath(QStringLiteral("Exam.mentis"));
    write(original, "first map");
    const MapFolder folder(root.path());

    const MapFolder::BringIn first = folder.bringIn(QUrl::fromLocalFile(original));
    QVERIFY2(!first.path.isEmpty(), qPrintable(first.error));
    QVERIFY(first.created);
    QCOMPARE(QFileInfo(first.path).fileName(), QStringLiteral("Exam.mentis"));
    QVERIFY(folder.contains(first.path));
    QVERIFY(QFileInfo::exists(original));  // the original stays where it was

    // Same name, same bytes: the copy is reused.
    const MapFolder::BringIn again = folder.bringIn(QUrl::fromLocalFile(original));
    QCOMPARE(again.path, first.path);
    QVERIFY(!again.created);
    // Opened, the copy changes (Mentis writes into it): still the same copy for the same original.
    write(first.path, "first map, edited in Mentis");
    QCOMPARE(folder.bringIn(QUrl::fromLocalFile(original)).path, first.path);
    // A map of the folder opens as it is.
    QCOMPARE(folder.bringIn(QUrl::fromLocalFile(first.path)).path, first.path);

    // Same name, other bytes: a free name, the first copy untouched.
    write(original, "changed map");
    const MapFolder::BringIn second = folder.bringIn(QUrl::fromLocalFile(original));
    QCOMPARE(QFileInfo(second.path).fileName(), QStringLiteral("Exam (2).mentis"));
    QFile kept(first.path);
    QVERIFY(kept.open(QIODevice::ReadOnly));
    QCOMPARE(kept.readAll(), QByteArray("first map, edited in Mentis"));
    QCOMPARE(folder.maps().size(), 2);
}

void MapFolderTest::bringInLeavesNoPartOnFailure() {
    QTemporaryDir outside, root;
    const MapFolder folder(root.path());
    const MapFolder::BringIn missing = folder.bringIn(QUrl::fromLocalFile(outside.filePath(QStringLiteral("none.mentis"))));
    QVERIFY(missing.path.isEmpty());
    QVERIFY(!missing.error.isEmpty());
    QVERIFY(QDir(root.path()).entryList(QDir::Files).isEmpty());
}

void MapFolderTest::ensureRemovesCutCopies() {
    QTemporaryDir root;
    QVERIFY(QDir(root.path()).mkdir(QStringLiteral("Exams")));
    write(root.filePath(QStringLiteral("Cut.mentis.part")), "x");
    write(root.filePath(QStringLiteral("Exams/Cut.mentis.part")), "x");
    write(root.filePath(QStringLiteral("notes.part")), "x");  // not ours: kept
    const MapFolder::Ready ready = MapFolder(root.path()).ensure();
    QCOMPARE(ready.path, QDir::cleanPath(root.path()));
    QVERIFY(ready.warning.isEmpty());
    QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("Cut.mentis.part"))));
    // A subfolder's cut copy goes when the subfolder is listed: ensure() never walks the whole tree.
    QVERIFY(QFileInfo::exists(root.filePath(QStringLiteral("Exams/Cut.mentis.part"))));
    QVERIFY(MapFolder(root.path()).maps(root.filePath(QStringLiteral("Exams"))).isEmpty());
    QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("Exams/Cut.mentis.part"))));
    QVERIFY(QFileInfo::exists(root.filePath(QStringLiteral("notes.part"))));
}

void MapFolderTest::ensureFallsBackWhenTheFolderCannotBeReached() {
    QTemporaryDir root;
    const QString fallback = root.filePath(QStringLiteral("Default/Mentis"));
    // Deleted, parent still there: made again.
    const QString deleted = root.filePath(QStringLiteral("Mine"));
    MapFolder::Ready ready = MapFolder(deleted, fallback).ensure();
    QCOMPARE(ready.path, QDir::cleanPath(deleted));
    QVERIFY(ready.warning.isEmpty());
    // Parent gone (a removed disk): the fallback, with a message.
    const QString unreachable = root.filePath(QStringLiteral("Gone/Disk/Mentis"));
    ready = MapFolder(unreachable, fallback).ensure();
    QCOMPARE(ready.path, QDir::cleanPath(fallback));
    QVERIFY(!ready.warning.isEmpty());
    QVERIFY(!QFileInfo::exists(unreachable));
    // A file where the folder should be: not writable as a folder, the fallback.
    const QString notAFolder = root.filePath(QStringLiteral("file"));
    write(notAFolder, "x");
    ready = MapFolder(notAFolder, fallback).ensure();
    QCOMPARE(ready.path, QDir::cleanPath(fallback));
    QVERIFY(!ready.warning.isEmpty());
    // The default location with missing parents (first start): made, no message.
    const QString fresh = root.filePath(QStringLiteral("New/Documents/Mentis"));
    ready = MapFolder(QString(), fresh).ensure();
    QCOMPARE(ready.path, QDir::cleanPath(fresh));
    QVERIFY(ready.warning.isEmpty());
}

void MapFolderTest::containsSubfoldersOnly() {
    QTemporaryDir root, outside;
    QVERIFY(QDir(root.path()).mkdir(QStringLiteral("Exams")));
    write(root.filePath(QStringLiteral("Exams/A.mentis")), "a");
    write(outside.filePath(QStringLiteral("B.mentis")), "b");
    const MapFolder folder(root.path());
    QVERIFY(folder.contains(root.filePath(QStringLiteral("Exams/A.mentis"))));
    QVERIFY(!folder.contains(outside.filePath(QStringLiteral("B.mentis"))));
    QVERIFY(!folder.contains(root.path()));
    // A sibling folder whose name starts the same is not inside.
    QVERIFY(QDir(root.path() + QStringLiteral("-other")).mkpath(QStringLiteral(".")));
    write(root.path() + QStringLiteral("-other/C.mentis"), "c");
    QVERIFY(!folder.contains(root.path() + QStringLiteral("-other/C.mentis")));
    QDir(root.path() + QStringLiteral("-other")).removeRecursively();
}

void MapFolderTest::subfoldersAreMadeRenamedAndRemoved() {
    QTemporaryDir root;
    const MapFolder folder(root.path());
    QString error;
    const QString exams = folder.createFolder(QString(), QStringLiteral("Exams"), &error);
    QCOMPARE(QFileInfo(exams).fileName(), QStringLiteral("Exams"));
    QCOMPARE(QFileInfo(folder.createFolder(QString(), QStringLiteral("Exams"), &error)).fileName(), QStringLiteral("Exams (2)"));
    QVERIFY(folder.createFolder(QString(), QStringLiteral("  "), &error).isEmpty());
    QVERIFY(!error.isEmpty());
    const QString year = folder.createFolder(exams, QStringLiteral("2026"), &error);
    QCOMPARE(folder.folders().size(), 2);
    QCOMPARE(folder.allFolders(), QStringList({QDir::cleanPath(root.path()), exams, year, QDir::cleanPath(root.filePath(QStringLiteral("Exams (2)")))}));

    // Rename: a taken name gets a free one; a change of case only renames the same folder.
    QCOMPARE(QFileInfo(folder.renameFolder(root.filePath(QStringLiteral("Exams (2)")), QStringLiteral("Exams"), &error)).fileName(),
             QStringLiteral("Exams (3)"));
    const QString lower = folder.renameFolder(exams, QStringLiteral("exams"), &error);
    QCOMPARE(QFileInfo(lower).fileName(), QStringLiteral("exams"));
    QCOMPARE(QDir(root.path()).entryList({QStringLiteral("exams")}, QDir::Dirs).value(0), QStringLiteral("exams"));

    // Remove: only an empty folder.
    QVERIFY(!folder.removeFolder(lower, &error));  // it holds "2026"
    QVERIFY(QFileInfo(lower).isDir());
    QVERIFY(folder.removeFolder(QDir(lower).filePath(QStringLiteral("2026")), &error));
    write(QDir(lower).filePath(QStringLiteral("A.mentis")), "a");
    QVERIFY(!folder.removeFolder(lower, &error));
    QVERIFY(QFileInfo::exists(QDir(lower).filePath(QStringLiteral("A.mentis"))));
}

void MapFolderTest::mapMovesWithAFreeName() {
    QTemporaryDir root, outside;
    const MapFolder folder(root.path());
    QString error;
    const QString exams = folder.createFolder(QString(), QStringLiteral("Exams"), &error);
    write(root.filePath(QStringLiteral("Plan.mentis")), "root plan");
    write(QDir(exams).filePath(QStringLiteral("Plan.mentis")), "exam plan");
    // A copy brought in from outside follows its moves.
    const QString original = outside.filePath(QStringLiteral("Received.mentis"));
    write(original, "received");
    const QString copy = folder.bringIn(QUrl::fromLocalFile(original)).path;

    // The folder it is in: nothing happens.
    const MapFolder::Move same = folder.moveMap(root.filePath(QStringLiteral("Plan.mentis")), root.path());
    QVERIFY(same.error.isEmpty() && !same.deferred);
    QCOMPARE(QDir::cleanPath(QFileInfo(same.path).absolutePath()), QDir::cleanPath(root.path()));
    // A taken name in the target: a free one, the other map untouched.
    const MapFolder::Move move = folder.moveMap(root.filePath(QStringLiteral("Plan.mentis")), exams);
    QVERIFY2(move.error.isEmpty(), qPrintable(move.error));
    QCOMPARE(QFileInfo(move.path).fileName(), QStringLiteral("Plan (2).mentis"));
    QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("Plan.mentis"))));
    QFile kept(QDir(exams).filePath(QStringLiteral("Plan.mentis")));
    QVERIFY(kept.open(QIODevice::ReadOnly));
    QCOMPARE(kept.readAll(), QByteArray("exam plan"));
    kept.close();  // an open file would keep Windows from renaming its folder below

    // A journal left by a crash goes with its map.
    write(copy + QStringLiteral("-journal"), "rollback");
    const MapFolder::Move received = folder.moveMap(copy, exams);
    QVERIFY(QFileInfo::exists(received.path + QStringLiteral("-journal")));
    QVERIFY(!QFileInfo::exists(copy + QStringLiteral("-journal")));
    QVERIFY(QFile::remove(received.path + QStringLiteral("-journal")));
    QCOMPARE(folder.bringIn(QUrl::fromLocalFile(original)).path, received.path);
    // The folder renamed: the copy is found again under the new name.
    const QString renamed = folder.renameFolder(exams, QStringLiteral("Old exams"), &error);
    QCOMPARE(folder.bringIn(QUrl::fromLocalFile(original)).path, QDir(renamed).filePath(QStringLiteral("Received.mentis")));
    QVERIFY(!folder.moveMap(root.filePath(QStringLiteral("none.mentis")), renamed).error.isEmpty());

    QCOMPARE(MapFolder::moved(QStringLiteral("/a/b/c.mentis"), QStringLiteral("/a/b"), QStringLiteral("/a/x")), QStringLiteral("/a/x/c.mentis"));
    QCOMPARE(MapFolder::moved(QStringLiteral("/a/bc/c.mentis"), QStringLiteral("/a/b"), QStringLiteral("/a/x")), QStringLiteral("/a/bc/c.mentis"));
    QCOMPARE(MapFolder::moved(QStringLiteral("/a/b"), QStringLiteral("/a/b"), QStringLiteral("/a/x")), QStringLiteral("/a/x"));
}

void MapFolderTest::openMapMovesOnceClosed() {
    QTemporaryDir root;
    QSettings().remove(QStringLiteral("mentisFolderPendingMoves"));
    const MapFolder folder(root.path());
    QString error;
    const QString exams = folder.createFolder(QString(), QStringLiteral("Exams"), &error);
    const QString map = QDir::cleanPath(root.filePath(QStringLiteral("Open.mentis")));
    write(map, "open map");
    {
        // A Mentis window holds the map, as Document::acquireLock does.
        QLockFile lock(map + QStringLiteral(".lock"));
        lock.setStaleLockTime(0);
        QVERIFY(lock.tryLock(0));
        QVERIFY(MapFolder::isOpen(map));
        // Its folder cannot be renamed while it is open.
        QVERIFY(folder.renameFolder(root.path(), QStringLiteral("Other"), &error).isEmpty());
        const MapFolder::Move move = folder.moveMap(map, exams);
        QVERIFY(move.deferred);
        QVERIFY(QFileInfo::exists(map));
        QVERIFY(MapFolder::runPendingMoves().isEmpty());  // still open: it waits
        QVERIFY(QFileInfo::exists(map));
    }
    QVERIFY(!MapFolder::isOpen(map));
    const QList<QPair<QString, QString>> moved = MapFolder::runPendingMoves();
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.first().first, map);
    QCOMPARE(moved.first().second, QDir(exams).filePath(QStringLiteral("Open.mentis")));
    QVERIFY(QFileInfo::exists(moved.first().second));
    QVERIFY(MapFolder::runPendingMoves().isEmpty());  // done once
}

void MapFolderTest::everyMapMovesToANewFolder() {
    QTemporaryDir oldRoot, newRoot;
    QSettings().remove(QStringLiteral("mentisFolderPendingMoves"));
    const MapFolder folder(oldRoot.path());
    QString error;
    const QString exams = folder.createFolder(QString(), QStringLiteral("Exams"), &error);
    const QString empty = folder.createFolder(exams, QStringLiteral("Later"), &error);
    write(oldRoot.filePath(QStringLiteral("Plan.mentis")), "plan");
    write(QDir(exams).filePath(QStringLiteral("Physics.mentis")), "physics");
    write(QDir(exams).filePath(QStringLiteral("Open.mentis")), "open");
    write(newRoot.filePath(QStringLiteral("Plan.mentis")), "another plan");  // taken name: never overwritten
    const QString open = QDir::cleanPath(QDir(exams).filePath(QStringLiteral("Open.mentis")));

    QLockFile lock(open + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);
    QVERIFY(lock.tryLock(0));
    const MapFolder::MoveAll result = folder.moveAllTo(newRoot.path());
    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char(' '))));
    QCOMPARE(result.moved.size(), 2);
    QCOMPARE(result.deferred, 1);
    const QDir target(newRoot.path());
    QCOMPARE(QFile(target.filePath(QStringLiteral("Plan.mentis"))).size(), qint64(12));  // the one already there
    QVERIFY(target.exists(QStringLiteral("Plan (2).mentis")));
    QVERIFY(target.exists(QStringLiteral("Exams/Physics.mentis")));
    QVERIFY(target.exists(QStringLiteral("Exams/Later")));  // empty subfolders keep the structure
    QVERIFY(!QFileInfo::exists(empty));                       // and the old empty one is gone
    QVERIFY(QFileInfo::exists(open));                         // open: waits, its folder stays
    QVERIFY(QFileInfo::exists(oldRoot.path()));                // the old folder itself stays

    lock.unlock();
    const QList<QPair<QString, QString>> moved = MapFolder::runPendingMoves();
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.first().second, QDir::cleanPath(target.filePath(QStringLiteral("Exams/Open.mentis"))));
}

void MapFolderTest::aNewFolderInsideTheOldOneIsSkipped() {
    QTemporaryDir root;
    const MapFolder folder(root.path());
    QString error;
    const QString inner = folder.createFolder(QString(), QStringLiteral("New"), &error);
    write(root.filePath(QStringLiteral("Plan.mentis")), "plan");
    write(QDir(inner).filePath(QStringLiteral("Kept.mentis")), "kept");
    const MapFolder::MoveAll result = folder.moveAllTo(inner);
    QVERIFY(result.errors.isEmpty());
    QCOMPARE(result.moved.size(), 1);
    QVERIFY(QDir(inner).exists(QStringLiteral("Plan.mentis")));
    QVERIFY(QDir(inner).exists(QStringLiteral("Kept.mentis")));
    QVERIFY(!QDir(inner).exists(QStringLiteral("New")));  // never moved into itself
}

void MapFolderTest::aDiscardedCopyIsNotReused() {
    QTemporaryDir outside, other, root;
    const MapFolder folder(root.path());
    const QString bad = outside.filePath(QStringLiteral("notes.mentis"));
    write(bad, "not a map");
    const MapFolder::BringIn refused = folder.bringIn(QUrl::fromLocalFile(bad));
    QVERIFY(refused.created);
    QVERIFY(QFile::remove(refused.path));  // not a map: removed, as MainWindow and MapLibrary do
    MapFolder::forgetCopy(refused.path);

    const QString real = other.filePath(QStringLiteral("notes.mentis"));
    write(real, "a real map");
    const MapFolder::BringIn map = folder.bringIn(QUrl::fromLocalFile(real));
    QCOMPARE(map.path, refused.path);  // the free name taken again
    const MapFolder::BringIn again = folder.bringIn(QUrl::fromLocalFile(bad));
    QVERIFY(again.created);  // a new copy, never the real map
    QVERIFY(again.path != map.path);

    // A copy deleted by hand and its name taken by a map saved there (MainWindow::saveMapAs).
    QVERIFY(QFile::remove(again.path));
    MapFolder::forgetCopy(again.path);
    write(again.path, "saved map");
    QVERIFY(folder.bringIn(QUrl::fromLocalFile(bad)).path != again.path);
}

void MapFolderTest::aNameWithAnOrphanJournalIsTaken() {
    QTemporaryDir dir;
    write(dir.filePath(QStringLiteral("Map.mentis-journal")), "rollback of a map deleted by hand");
    QCOMPARE(MapFolder::freeName(dir.path(), QStringLiteral("Map.mentis")), QStringLiteral("Map (2).mentis"));
    write(dir.filePath(QStringLiteral("Map (2).mentis.lock")), "");
    QCOMPARE(MapFolder::freeName(dir.path(), QStringLiteral("Map.mentis")), QStringLiteral("Map (3).mentis"));
}

void MapFolderTest::foldersOfOtherFilesStayWhenMapsMove() {
    QTemporaryDir oldRoot, newRoot;
    QVERIFY(QDir(oldRoot.path()).mkpath(QStringLiteral("Photos/2026")));
    write(oldRoot.filePath(QStringLiteral("Photos/2026/beach.jpg")), "jpg");
    QVERIFY(QDir(oldRoot.path()).mkpath(QStringLiteral("Work/Empty")));
    write(oldRoot.filePath(QStringLiteral("Work/report.txt")), "txt");
    write(oldRoot.filePath(QStringLiteral("Work/Plan.mentis")), "plan");
    const MapFolder::MoveAll result = MapFolder(oldRoot.path()).moveAllTo(newRoot.path());
    QVERIFY(result.errors.isEmpty());
    QCOMPARE(result.moved.size(), 1);
    const QDir target(newRoot.path());
    QVERIFY(target.exists(QStringLiteral("Work/Plan.mentis")));
    QVERIFY(target.exists(QStringLiteral("Work/Empty")));   // an empty folder among maps moves along
    QVERIFY(!target.exists(QStringLiteral("Photos")));      // a folder of other files is not copied
    QVERIFY(QFileInfo::exists(oldRoot.filePath(QStringLiteral("Photos/2026/beach.jpg"))));
    QVERIFY(QFileInfo::exists(oldRoot.filePath(QStringLiteral("Work/report.txt"))));
}

void MapFolderTest::linkedFoldersAreNotFollowed() {
#ifdef Q_OS_WIN
    QSKIP("QFile::link makes a .lnk shortcut on Windows, not a link the file system follows.");
#else
    QTemporaryDir root, outside;
    write(outside.filePath(QStringLiteral("Elsewhere.mentis")), "not in the folder");
    QVERIFY(QDir(root.path()).mkdir(QStringLiteral("Exams")));
    QVERIFY(QFile::link(root.path(), root.filePath(QStringLiteral("Exams/up"))));  // a loop back up
    QVERIFY(QFile::link(outside.path(), root.filePath(QStringLiteral("out"))));
    const MapFolder folder(root.path());
    QCOMPARE(folder.allFolders(), QStringList({QDir::cleanPath(root.path()), QDir::cleanPath(root.filePath(QStringLiteral("Exams")))}));
    QTemporaryDir newRoot;
    QVERIFY(folder.moveAllTo(newRoot.path()).moved.isEmpty());
    QVERIFY(QFileInfo::exists(outside.filePath(QStringLiteral("Elsewhere.mentis"))));
#endif
}

void MapFolderTest::aDeferredMoveFollowsItsRenamedFolder() {
    QTemporaryDir root;
    QSettings().remove(QStringLiteral("mentisFolderPendingMoves"));
    const MapFolder folder(root.path());
    QString error;
    const QString exams = folder.createFolder(QString(), QStringLiteral("Exams"), &error);
    const QString map = QDir::cleanPath(root.filePath(QStringLiteral("Open.mentis")));
    write(map, "open map");
    QString renamed;
    {
        QLockFile lock(map + QStringLiteral(".lock"));
        lock.setStaleLockTime(0);
        QVERIFY(lock.tryLock(0));
        QVERIFY(folder.moveMap(map, exams).deferred);
        renamed = folder.renameFolder(exams, QStringLiteral("Done"), &error);
        QVERIFY2(!renamed.isEmpty(), qPrintable(error));
    }
    const QList<QPair<QString, QString>> moved = MapFolder::runPendingMoves();
    QCOMPARE(moved.size(), 1);
    QCOMPARE(moved.first().second, QDir(renamed).filePath(QStringLiteral("Open.mentis")));
}

void MapFolderTest::aFolderKeepsItsColour() {
    QTemporaryDir oldRoot, newRoot;
    const MapFolder folder(oldRoot.path());
    QString error;
    const QString exams = folder.createFolder(QString(), QStringLiteral("Exams"), &error);
    QCOMPARE(MapFolder::folderColor(exams), QString());  // no colour by default
    const QString red = MapFolder::folderColors().first();
    QVERIFY2(MapFolder::setFolderColor(exams, red.toUpper(), &error), qPrintable(error));
    QCOMPARE(MapFolder::folderColor(exams), red);
    QCOMPARE(folder.folders().first().color, red);
    QVERIFY(!MapFolder::setFolderColor(exams, QStringLiteral("#123456"), &error));  // not in the palette
    QCOMPARE(MapFolder::folderColor(exams), red);
    QVERIFY(folder.maps(exams).isEmpty());  // the colour file is not a map
    QVERIFY(MapFolder::isEmptyFolder(exams));

    // Renamed and moved with every map: the colour follows; an empty folder with a colour moves too.
    const QString renamed = folder.renameFolder(exams, QStringLiteral("Done"), &error);
    QCOMPARE(MapFolder::folderColor(renamed), red);
    write(QDir(renamed).filePath(QStringLiteral("Plan.mentis")), "plan");
    const QString empty = folder.createFolder(QString(), QStringLiteral("Later"), &error);
    QVERIFY(MapFolder::setFolderColor(empty, MapFolder::folderColors().at(4), &error));
    QVERIFY(folder.moveAllTo(newRoot.path()).errors.isEmpty());
    QCOMPARE(MapFolder::folderColor(newRoot.filePath(QStringLiteral("Done"))), red);
    QCOMPARE(MapFolder::folderColor(newRoot.filePath(QStringLiteral("Later"))), MapFolder::folderColors().at(4));
    QVERIFY(!QFileInfo::exists(renamed));
    QVERIFY(!QFileInfo::exists(empty));

    // A folder with only its colour is deleted, colour file and all; no colour removes the file.
    const QString later = QDir::cleanPath(newRoot.filePath(QStringLiteral("Later")));
    QVERIFY(MapFolder(newRoot.path()).removeFolder(later, &error));
    QVERIFY(!QFileInfo::exists(later));
    const QString done = newRoot.filePath(QStringLiteral("Done"));
    QVERIFY(MapFolder::setFolderColor(done, QString(), &error));
    QCOMPARE(MapFolder::folderColor(done), QString());
    QVERIFY(!QFileInfo::exists(QDir(done).filePath(QStringLiteral(".mentis-folder"))));

    write(QDir(done).filePath(QStringLiteral(".mentis-folder")), "{not json");  // unreadable: no colour
    QCOMPARE(MapFolder::folderColor(done), QString());

    // Changed twice: the second colour replaces the first (hidden on Windows).
    QVERIFY(MapFolder::setFolderColor(done, MapFolder::folderColors().at(1), &error));
    QVERIFY2(MapFolder::setFolderColor(done, MapFolder::folderColors().at(2), &error), qPrintable(error));
    QCOMPARE(MapFolder::folderColor(done), MapFolder::folderColors().at(2));

    // A coloured folder holding only empty folders moves with its colour.
    QTemporaryDir third;
    const MapFolder from(newRoot.path());
    const QString parent = from.createFolder(QString(), QStringLiteral("Year"), &error);
    from.createFolder(parent, QStringLiteral("Term"), &error);
    QVERIFY(MapFolder::setFolderColor(parent, red, &error));
    QVERIFY(from.moveAllTo(third.path()).errors.isEmpty());
    QCOMPARE(MapFolder::folderColor(third.filePath(QStringLiteral("Year"))), red);
    QVERIFY(QFileInfo(third.filePath(QStringLiteral("Year/Term"))).isDir());
    QVERIFY(!QFileInfo::exists(parent));
}

void MapFolderTest::aFolderMovesIntoAnother() {
    QTemporaryDir root;
    const MapFolder folder(root.path());
    QString error;
    const QString exams = folder.createFolder(QString(), QStringLiteral("Exams"), &error);
    const QString courses = folder.createFolder(QString(), QStringLiteral("Courses"), &error);
    const QString inner = folder.createFolder(exams, QStringLiteral("June"), &error);
    write(QDir(inner).filePath(QStringLiteral("Plan.mentis")), "plan");
    QVERIFY(MapFolder::setFolderColor(exams, MapFolder::folderColors().first(), &error));

    // Its maps, subfolders and colour go with it.
    const QString moved = folder.moveFolder(exams, courses, &error);
    QCOMPARE(moved, QDir::cleanPath(QDir(courses).filePath(QStringLiteral("Exams"))));
    QVERIFY(!QFileInfo::exists(exams));
    QVERIFY(QFileInfo::exists(QDir(moved).filePath(QStringLiteral("June/Plan.mentis"))));
    QCOMPARE(MapFolder::folderColor(moved), MapFolder::folderColors().first());
    // Already there: nothing.
    QCOMPARE(folder.moveFolder(moved, courses, &error), moved);
    // Not into itself or one of its subfolders.
    error.clear();
    QVERIFY(folder.moveFolder(courses, moved, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(folder.moveFolder(courses, courses, &error).isEmpty());
    // A taken name gets a free one.
    QVERIFY(QDir(root.path()).mkdir(QStringLiteral("Exams")));
    QCOMPARE(QFileInfo(folder.moveFolder(moved, root.path(), &error)).fileName(), QStringLiteral("Exams (2)"));
    // Not while a map in it is open.
    const QString map = QDir(root.filePath(QStringLiteral("Exams (2)"))).filePath(QStringLiteral("June/Plan.mentis"));
    QLockFile lock(map + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);
    QVERIFY(lock.tryLock(0));
    error.clear();
    QVERIFY(folder.moveFolder(root.filePath(QStringLiteral("Exams (2)")), courses, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(QFileInfo::exists(map));
    // Not the folder itself, nothing outside it; a trailing slash is the same folder.
    QVERIFY(folder.moveFolder(root.path(), courses, &error).isEmpty());
    QTemporaryDir outside;
    QVERIFY(folder.moveFolder(courses, outside.path(), &error).isEmpty());
    QVERIFY(QFileInfo(courses).isDir());
    QCOMPARE(folder.moveFolder(courses + QLatin1Char('/'), root.path(), &error), QDir::cleanPath(courses));
}

void MapFolderTest::aDeferredMoveFollowsItsMovedFolder() {
    QTemporaryDir root;
    QSettings().remove(QStringLiteral("mentisFolderPendingMoves"));
    const MapFolder folder(root.path());
    QString error;
    const QString exams = folder.createFolder(QString(), QStringLiteral("Exams"), &error);
    const QString archive = folder.createFolder(QString(), QStringLiteral("Archive"), &error);
    const QString inner = folder.createFolder(exams, QStringLiteral("June"), &error);
    const QString map = QDir::cleanPath(root.filePath(QStringLiteral("Open.mentis")));
    write(map, "open map");
    QString moved;
    {
        // Open, queued to go into a folder inside the one that moves: the queue follows the move.
        QLockFile lock(map + QStringLiteral(".lock"));
        lock.setStaleLockTime(0);
        QVERIFY(lock.tryLock(0));
        QVERIFY(folder.moveMap(map, inner).deferred);
        moved = folder.moveFolder(exams, archive, &error);
        QVERIFY2(!moved.isEmpty(), qPrintable(error));
    }
    const QList<QPair<QString, QString>> done = MapFolder::runPendingMoves();
    QCOMPARE(done.size(), 1);
    QCOMPARE(done.first().second, QDir(moved).filePath(QStringLiteral("June/Open.mentis")));
}

QTEST_GUILESS_MAIN(MapFolderTest)
#include "map_folder_test.moc"
