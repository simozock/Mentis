// FEAT-066 tappa 1: il Document senza finestre. Fogli, storico unico con Ctrl+Z nel proprio foglio,
// stato modificato, salvataggio, apertura (fogli letti alla prima apertura, file bloccato, file
// rotto), Markdown e salvataggio automatico. Impostazioni e cartelle di prova separate.
#include "bmapstorage.h"
#include "document.h"
#include "iconlibrary.h"
#include "markdownimport.h"
#include "mindmapmodel.h"
#include "performance.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace {
QStringList childTitles(const MindNode *node) {
    QStringList titles;
    for (const auto &child : node->children) titles.append(child->title);
    return titles;
}
}  // namespace

class DocumentTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void startsUntitledAndClean();
    void changeBecomesAnUndoableStep();
    void undoStaysInItsSheet();
    void removeSheetKeepsTheOthers();
    void renameSheetIsSavedWithoutAHistoryStep();
    void moveSheetKeepsTheActiveOne();
    void duplicateSheetGetsFreshIds();
    void saveAndReopenReadsSheetsLazily();
    void lockedAndBrokenFilesLeaveTheDocumentAsItWas();
    void markdownIsLaidOutBeforeTheHistoryStarts();
    void autosaveWritesANamedDocument();
    void stickerDeletedFromTheLibraryStaysInTheMap();
    void resourcesAreTheDocumentsAndOneStep();
    void nodeOperationsAreUndoableSteps();
    void contentOperationsNotifyTheNodes();
    void annotationsAreUndoableSteps();
    void pastedMarkdownBecomesBranchesOrTheWholeMap();
    void exportAndCopiedBranch();
    void exportFolderWithAssets();
    void topicCommandsAreSteps();

private:
    QString path(const QString &name) const { return m_dir.filePath(name); }
    QTemporaryDir m_dir;
};

void DocumentTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisDocumentTest"));
    Performance::setMode(Performance::Mode::Standard);
    QVERIFY(m_dir.isValid());
}

void DocumentTest::startsUntitledAndClean() {
    Document document;
    document.startUntitled();
    QCOMPARE(document.sheets().size(), 1);
    QCOMPARE(document.sheets().front().name, QStringLiteral("Sheet 1"));
    QVERIFY(document.filePath().isEmpty());
    QVERIFY(!document.assetSourcePath().isEmpty());  // copia di lavoro
    QVERIFY(!document.isDirty());
    QVERIFY(!document.canUndo());
}

void DocumentTest::changeBecomesAnUndoableStep() {
    Document document;
    document.startUntitled();
    QSignalSpy dirty(&document, &Document::dirtyChanged);
    document.model()->addChild(document.model()->root(), QStringLiteral("Uno"));
    document.markChanged();
    QVERIFY(document.isDirty());
    QVERIFY(document.canUndo());  // gia' prima che il passo si chiuda
    QVERIFY(!dirty.isEmpty());
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(childTitles(document.model()->root()).isEmpty());
    QVERIFY(!document.isDirty());  // di nuovo allo stato iniziale
    QCOMPARE(document.redo().outcome, Document::StepOutcome::Applied);
    QCOMPARE(childTitles(document.model()->root()), QStringList{QStringLiteral("Uno")});
    QCOMPARE(document.redo().outcome, Document::StepOutcome::Nothing);
}

void DocumentTest::undoStaysInItsSheet() {
    Document document;
    document.startUntitled();
    document.model()->addChild(document.model()->root(), QStringLiteral("Primo"));
    document.markChanged();
    document.flushPendingStep();
    document.addSheet();
    QCOMPARE(document.sheets().size(), 2);
    QCOMPARE(document.currentSheet(), 1);
    QCOMPARE(document.sheets()[1].name, QStringLiteral("Sheet 2"));
    document.model()->addChild(document.model()->root(), QStringLiteral("Secondo"));
    document.markChanged();
    document.flushPendingStep();
    QVERIFY(document.switchSheet(0).ok);
    QCOMPARE(childTitles(document.model()->root()), QStringList{QStringLiteral("Primo")});
    const Document::StepResult other = document.undo();
    QCOMPARE(other.outcome, Document::StepOutcome::OtherSheet);
    QCOMPARE(other.sheetId, document.sheets()[1].id);
    QCOMPARE(childTitles(document.model()->root()), QStringList{QStringLiteral("Primo")});
    QVERIFY(document.switchSheet(1).ok);
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(childTitles(document.model()->root()).isEmpty());
    QVERIFY(!document.switchSheet(5).ok);
}

// FEAT-004: the active sheet is deleted, the previous one is shown with its Undo steps; the file
// loses the sheet and keeps a recovery point with it. The last sheet cannot be deleted.
// FEAT-004: a renamed sheet keeps its name across the autosave and a reopening, also one not opened
// yet; Ctrl+Z does not bring the old name back and an empty name changes nothing.
void DocumentTest::renameSheetIsSavedWithoutAHistoryStep() {
    const QString file = path(QStringLiteral("rinomina-foglio.mentis"));
    {
        Document document;
        document.startUntitled();
        document.addChild(nullptr, QStringLiteral("Primo"));
        document.flushPendingStep();
        document.addSheet();
        QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
    }
    Document document;
    QVERIFY(document.open(file, false).ok);
    QCOMPARE(document.currentSheet(), 0);
    QVERIFY(!document.sheets()[1].loaded);
    const bool couldUndo = document.canUndo();
    QVERIFY(!document.renameSheet(1, QStringLiteral("  \n ")));
    QVERIFY(!document.renameSheet(1, document.sheets()[1].name));
    QVERIFY(!document.isDirty());
    QSignalSpy sheets(&document, &Document::sheetsChanged);
    QVERIFY(document.renameSheet(1, QStringLiteral("  Capitolo\n2 ")));
    QCOMPARE(document.sheets()[1].name, QStringLiteral("Capitolo 2"));
    QCOMPARE(sheets.count(), 1);
    QVERIFY(document.isDirty());
    QCOMPARE(document.canUndo(), couldUndo);
    QTRY_VERIFY_WITH_TIMEOUT(!document.isDirty(), 5000);  // the autosave wrote it
    document.close();
    Document reopened;
    QVERIFY(reopened.open(file, false).ok);
    QCOMPARE(reopened.sheets()[1].name, QStringLiteral("Capitolo 2"));
}

// FEAT-004: moving a sheet keeps the active one, is not a history step and the order is saved.
void DocumentTest::moveSheetKeepsTheActiveOne() {
    const QString file = path(QStringLiteral("riordina-fogli.mentis"));
    Document document;
    document.startUntitled();
    document.addSheet();
    document.addSheet();
    QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
    const QString first = document.sheets()[0].id, second = document.sheets()[1].id, third = document.sheets()[2].id;
    QCOMPARE(document.currentSheet(), 2);
    const bool couldUndo = document.canUndo();
    QVERIFY(!document.moveSheet(1, 1));
    QVERIFY(!document.moveSheet(0, 3));
    QVERIFY(!document.isDirty());
    QVERIFY(document.moveSheet(2, 0));
    QCOMPARE(document.currentSheet(), 0);
    QCOMPARE(document.currentSheetId(), third);
    QVERIFY(document.moveSheet(1, 2));  // the old first sheet goes last
    QCOMPARE(document.currentSheetId(), third);
    QCOMPARE(document.sheets()[1].id, second);
    QCOMPARE(document.sheets()[2].id, first);
    QVERIFY(document.isDirty());
    QCOMPARE(document.canUndo(), couldUndo);
    QTRY_VERIFY_WITH_TIMEOUT(!document.isDirty(), 5000);  // the autosave wrote the order
    document.close();
    Document reopened;
    QVERIFY(reopened.open(file, false).ok);
    QCOMPARE(reopened.sheets()[0].id, third);
    QCOMPARE(reopened.sheets()[1].id, second);
    QCOMPARE(reopened.sheets()[2].id, first);
}

// FEAT-004: the copy goes right after the sheet and is shown, with the same topics under new ids;
// relationships and topic links inside the sheet follow the copy, a link to another sheet stays.
// Both sheets survive a save and a reopening (node ids are unique in the file).
void DocumentTest::duplicateSheetGetsFreshIds() {
    const QString file = path(QStringLiteral("duplica-foglio.mentis"));
    Document document;
    document.startUntitled();
    document.addSheet();
    const QString elsewhere = document.addChild(nullptr, QStringLiteral("Altrove"))->id;
    document.flushPendingStep();
    QVERIFY(document.switchSheet(0).ok);
    MindNode *a = document.addChild(nullptr, QStringLiteral("A"));
    MindNode *b = document.addChild(nullptr, QStringLiteral("B"));
    MindMapModel *model = document.model();
    a->topicLink = b->id;
    b->topicLink = elsewhere;
    model->relations.append(Relationship{QStringLiteral("rel-1"), a->id, b->id});
    document.markChanged();
    document.flushPendingStep();
    const QString aId = a->id, bId = b->id, rootId = model->root()->id;
    QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
    const bool couldUndo = document.canUndo();

    QVERIFY(document.duplicateSheet(0).ok);
    QCOMPARE(document.sheets().size(), 3);
    QCOMPARE(document.currentSheet(), 1);
    QCOMPARE(document.sheets()[1].name, document.sheets()[0].name + QStringLiteral(" (copy)"));
    QVERIFY(document.isDirty());
    QCOMPARE(document.canUndo(), couldUndo);
    const MindNode *root = model->root();
    QCOMPARE(childTitles(root), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
    const MindNode *copyA = root->children[0].get();
    const MindNode *copyB = root->children[1].get();
    QVERIFY(root->id != rootId && copyA->id != aId && copyB->id != bId);
    QCOMPARE(copyA->topicLink, copyB->id);
    QCOMPARE(copyB->topicLink, elsewhere);
    QCOMPARE(model->relations.size(), 1);
    QVERIFY(model->relations[0].id != QStringLiteral("rel-1"));
    QCOMPARE(model->relations[0].from, copyA->id);
    QCOMPARE(model->relations[0].to, copyB->id);

    QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
    const QString copyId = document.sheets()[1].id;
    document.close();
    Document reopened;
    QVERIFY(reopened.open(file, false).ok);
    QCOMPARE(reopened.sheets().size(), 3);
    QCOMPARE(reopened.sheets()[1].id, copyId);
    QCOMPARE(childTitles(reopened.model()->root()), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
    QCOMPARE(reopened.model()->root()->children[0]->id, aId);
    QVERIFY(reopened.switchSheet(1).ok);
    QCOMPARE(childTitles(reopened.model()->root()), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
    QVERIFY(reopened.model()->root()->children[0]->id != aId);
    reopened.close();
}

void DocumentTest::removeSheetKeepsTheOthers() {
    const QString file = path(QStringLiteral("elimina-foglio.mentis"));
    Document document;
    document.startUntitled();
    document.addChild(nullptr, QStringLiteral("Primo"));
    document.flushPendingStep();
    document.addSheet();
    document.addChild(nullptr, QStringLiteral("Secondo"));
    document.flushPendingStep();
    document.addSheet();
    document.addChild(nullptr, QStringLiteral("Terzo"));
    document.flushPendingStep();
    QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
    // Written by the automatic save: the file now differs from the explicit save's recovery point.
    document.addChild(nullptr, QStringLiteral("Dopo"));
    QTRY_VERIFY_WITH_TIMEOUT(!document.isDirty(), 5000);
    QVERIFY(document.switchSheet(1).ok);
    const QString removed = document.sheets()[1].id;
    QVERIFY(document.removeSheet(1).ok);
    QCOMPARE(document.sheets().size(), 2);
    QCOMPARE(document.currentSheet(), 0);
    QCOMPARE(childTitles(document.model()->root()), QStringList{QStringLiteral("Primo")});
    QVERIFY(document.isDirty());
    // The last step was in the third sheet: Undo points there, never to the deleted one.
    const Document::StepResult other = document.undo();
    QCOMPARE(other.outcome, Document::StepOutcome::OtherSheet);
    QCOMPARE(other.sheetId, document.sheets()[1].id);
    QVERIFY(document.switchSheet(1).ok);
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QCOMPARE(childTitles(document.model()->root()), QStringList{QStringLiteral("Terzo")});
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(childTitles(document.model()->root()).isEmpty());
    QVERIFY(document.switchSheet(0).ok);
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(childTitles(document.model()->root()).isEmpty());
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Nothing);
    QTRY_VERIFY_WITH_TIMEOUT(!document.isDirty(), 5000);  // automatic save
    const QList<BmapStorage::MapInfo> maps = BmapStorage::maps(file);
    QCOMPARE(maps.size(), 2);
    for (const BmapStorage::MapInfo &map : maps) QVERIFY(map.id != removed);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("removeSheetCheck"));
        db.setDatabaseName(file);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral("SELECT COUNT(*) FROM snapshots WHERE reason='before-large-deletion'")) && query.next());
        QCOMPARE(query.value(0).toInt(), 1);
    }
    QSqlDatabase::removeDatabase(QStringLiteral("removeSheetCheck"));
    QVERIFY(document.removeSheet(0).ok);
    QVERIFY(!document.removeSheet(0).ok);  // the last sheet stays
    QCOMPARE(document.sheets().size(), 1);
    document.close();
}

void DocumentTest::saveAndReopenReadsSheetsLazily() {
    const QString file = path(QStringLiteral("due-fogli.mentis"));
    {
        Document document;
        document.startUntitled();
        document.model()->addChild(document.model()->root(), QStringLiteral("A"));
        document.markChanged();
        document.addSheet();
        document.model()->addChild(document.model()->root(), QStringLiteral("B"));
        document.markChanged();
        QSignalSpy saving(&document, &Document::saveStateChanged);
        QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
        QVERIFY(!saving.isEmpty());
        QCOMPARE(document.filePath(), file);
        QVERIFY(!document.isDirty());
        QVERIFY(!document.isSaving());
        document.close();
    }
    Document reopened;
    reopened.startUntitled();
    QVERIFY(reopened.open(file, false).ok);
    QCOMPARE(reopened.sheets().size(), 2);
    QVERIFY(reopened.sheets()[0].loaded);
    QVERIFY(!reopened.sheets()[1].loaded);  // letto solo quando serve
    QCOMPARE(childTitles(reopened.model()->root()), QStringList{QStringLiteral("A")});
    QVERIFY(reopened.switchSheet(1).ok);
    QVERIFY(reopened.sheets()[1].loaded);
    QCOMPARE(childTitles(reopened.model()->root()), QStringList{QStringLiteral("B")});
    QVERIFY(!reopened.isDirty());
    QVERIFY(!reopened.canUndo());
    reopened.close();
}

void DocumentTest::lockedAndBrokenFilesLeaveTheDocumentAsItWas() {
    const QString file = path(QStringLiteral("bloccato.mentis"));
    Document owner;
    owner.startUntitled();
    QVERIFY(owner.save(file, BmapStorage::SaveReason::Explicit).ok);  // tiene il blocco del file
    Document other;
    other.startUntitled();
    other.model()->addChild(other.model()->root(), QStringLiteral("Mio"));
    other.markChanged();
    const Document::Result locked = other.open(file, false);
    QVERIFY(!locked.ok);
    QVERIFY(locked.fileLocked);
    QVERIFY(other.filePath().isEmpty());
    QCOMPARE(childTitles(other.model()->root()), QStringList{QStringLiteral("Mio")});
    const QString broken = path(QStringLiteral("rotto.mentis"));
    QFile brokenFile(broken);
    QVERIFY(brokenFile.open(QIODevice::WriteOnly));
    brokenFile.write("non sono un database");
    brokenFile.close();
    const Document::Result failed = other.open(broken, false);
    QVERIFY(!failed.ok);
    QVERIFY(!failed.fileLocked);
    QVERIFY(!failed.error.isEmpty());
    QCOMPARE(childTitles(other.model()->root()), QStringList{QStringLiteral("Mio")});
    owner.close();
}

void DocumentTest::markdownIsLaidOutBeforeTheHistoryStarts() {
    const QString file = path(QStringLiteral("appunti.md"));
    QFile markdown(file);
    QVERIFY(markdown.open(QIODevice::WriteOnly));
    markdown.write("- Biologia\n  - Cellula\n");
    markdown.close();
    Document document;
    document.startUntitled();
    bool calledWithNative = true;
    QVERIFY(document.open(file, false, [&](bool nativeMap) {
        calledWithNative = nativeMap;
        document.model()->root()->title = QStringLiteral("Disposta");  // come una disposizione automatica
    }).ok);
    QVERIFY(!calledWithNative);
    QVERIFY(document.filePath().isEmpty());  // un Markdown aperto e' un documento senza nome
    QVERIFY(!document.isDirty());            // la modifica di afterLoad fa parte dello stato iniziale
    QCOMPARE(document.sheetContent(0).value(document.model()->root()->id).value(QStringLiteral("title")).toString(),
             QStringLiteral("Disposta"));
}

void DocumentTest::autosaveWritesANamedDocument() {
    const QString file = path(QStringLiteral("automatico.mentis"));
    Document document;
    document.startUntitled();
    QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
    document.model()->addChild(document.model()->root(), QStringLiteral("Scritto da solo"));
    document.markChanged();
    QVERIFY(document.isDirty());
    QTRY_VERIFY_WITH_TIMEOUT(!document.isDirty(), 5000);  // 750 ms con il profilo Standard
    MindMapModel onDisk;
    QVERIFY(BmapStorage::load(file, onDisk));
    QCOMPARE(childTitles(onDisk.root()), QStringList{QStringLiteral("Scritto da solo")});
    document.close();
}

// BUG-037: a sticker deleted from its collection while a map still shows it, before the map has
// embedded it (no save yet, or an untitled map saved afterwards): the map saves and closes without
// errors and shows it when reopened; the file goes at the next start. An image deleted from the
// disk by hand before any save no longer blocks the saves (the picture is lost: nothing to read).
void DocumentTest::stickerDeletedFromTheLibraryStaysInTheMap() {
    const auto image = [this](const QString &name) {
        QImage picture(64, 64, QImage::Format_ARGB32);
        picture.fill(Qt::red);
        const QString file = path(name);
        return picture.save(file) ? file : QString();
    };
    const QString collection = IconLibrary::addCollection(IconLibrary::Kind::Sticker, QStringLiteral("Mine"));
    QVERIFY(!collection.isEmpty());
    for (const bool untitled : {false, true}) {
        const QString file = path(QStringLiteral("adesivo-tolto-%1.mentis").arg(untitled));
        const QString sticker = IconLibrary::addFile(IconLibrary::Kind::Sticker, collection,
                                                     image(QStringLiteral("adesivo-%1.png").arg(untitled)));
        QVERIFY(!sticker.isEmpty());
        {
            Document document;
            document.startUntitled();
            if (!untitled) QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
            QVERIFY(document.setSticker(document.addChild(nullptr, QStringLiteral("Con adesivo")), sticker, {64, 64}));
            document.flushPendingStep();
            QVERIFY(IconLibrary::removeItem(IconLibrary::Kind::Sticker, collection, sticker));
            const Document::Result saved = document.save(file, BmapStorage::SaveReason::Explicit);
            QVERIFY2(saved.ok, qPrintable(saved.error));
            QVERIFY(document.writeClosingPoint().ok);
            document.close();
        }
        IconLibrary::removeDeletedFiles(IconLibrary::Kind::Sticker);  // the next start
        QVERIFY(!QFileInfo::exists(sticker));
        Document reopened;
        const Document::Result opened = reopened.open(file, true);
        QVERIFY2(opened.ok, qPrintable(opened.error));
        QCOMPARE(reopened.model()->root()->children.size(), size_t(1));
        const MindNode *node = reopened.model()->root()->children.front().get();
        QVERIFY(node->imageSticker);
        QVERIFY(!QImage(node->imagePath).isNull());
        reopened.close();
    }
    const QString file = path(QStringLiteral("immagine-tolta.mentis"));
    Document document;
    document.startUntitled();
    QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
    const QString picture = image(QStringLiteral("tolta.png"));
    QVERIFY(document.setImage(document.addChild(nullptr, QStringLiteral("Con immagine")), picture, {64, 64}));
    document.flushPendingStep();
    QVERIFY(QFile::remove(picture));
    const Document::Result saved = document.save(file, BmapStorage::SaveReason::Explicit);
    QVERIFY2(saved.ok, qPrintable(saved.error));
    document.close();
    MindMapModel onDisk;
    QVERIFY(BmapStorage::load(file, onDisk));
    QCOMPARE(childTitles(onDisk.root()), QStringList{QStringLiteral("Con immagine")});
}

// FEAT-079: one list for every sheet, set with the active sheet's calendar in one step; saved in the
// file's metadata (not written while empty) and read back on opening.
void DocumentTest::resourcesAreTheDocumentsAndOneStep() {
    const QString file = path(QStringLiteral("risorse.mentis"));
    {
        Document document;
        document.startUntitled();
        QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
        QVERIFY(BmapStorage::loadResources(file).isEmpty());
        document.addSheet();
        document.flushPendingStep();
        QSignalSpy changed(&document, &Document::resourcesChanged);
        GanttResources resources;
        resources.people = {{{}, QStringLiteral("Anna"), QStringLiteral("Rossi"), QStringLiteral("anna@example.com"),
                             QStringLiteral("Developer"), 4500}};
        GanttCalendar calendar = document.model()->gantt().calendar();
        QVERIFY(!document.setResources({}, calendar));  // nothing changes
        calendar.workingDays = {1, 2, 3, 4, 5};
        calendar.minutesPerDay = 7 * 60 + 45;
        QVERIFY(document.setResources(resources, calendar));
        QCOMPARE(changed.size(), 1);
        QVERIFY(document.isDirty());
        QCOMPARE(document.resources().people.size(), 1);
        QCOMPARE(document.model()->gantt().calendar(), calendar);
        const QString id = document.resources().people[0].id;
        QVERIFY(!id.isEmpty());

        // One step: Undo takes back the list and the calendar together, Redo brings them again.
        QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
        QVERIFY(document.resources().people.isEmpty());
        QCOMPARE(document.model()->gantt().calendar(), GanttCalendar());
        QCOMPARE(document.redo().outcome, Document::StepOutcome::Applied);
        QCOMPARE(document.resources().people.value(0).id, id);
        QCOMPARE(document.model()->gantt().calendar().minutesPerDay, 7 * 60 + 45);

        // The list is the document's: the first sheet sees it, with its own calendar. A change made
        // there is that sheet's step.
        QVERIFY(document.switchSheet(0).ok);
        QCOMPARE(document.resources().people.size(), 1);
        QCOMPARE(document.model()->gantt().calendar(), GanttCalendar());
        GanttResources renamed = document.resources();
        renamed.people[0].role = QStringLiteral("Lead");
        QVERIFY(document.setResources(renamed, document.model()->gantt().calendar()));
        QVERIFY(document.switchSheet(1).ok);
        QCOMPARE(document.undo().outcome, Document::StepOutcome::OtherSheet);
        QCOMPARE(document.resources().people[0].role, QStringLiteral("Lead"));
        QVERIFY(document.save(file, BmapStorage::SaveReason::Explicit).ok);
        QVERIFY(!document.isDirty());
        document.close();
    }
    Document reopened;
    reopened.startUntitled();
    QVERIFY(reopened.open(file, false).ok);
    QCOMPARE(reopened.resources().people.size(), 1);
    QCOMPARE(reopened.resources().people[0].role, QStringLiteral("Lead"));
    QCOMPARE(reopened.resources().people[0].hourlyCostCents, 4500);
    QVERIFY(!reopened.canUndo());
    QVERIFY(reopened.switchSheet(1).ok);
    QCOMPARE(reopened.model()->gantt().calendar().minutesPerDay, 7 * 60 + 45);

    // Autosave writes a list changed later; an emptied list leaves no key.
    QVERIFY(reopened.setResources({}, reopened.model()->gantt().calendar()));
    QTRY_VERIFY_WITH_TIMEOUT(!reopened.isDirty(), 5000);
    QVERIFY(BmapStorage::loadResources(file).isEmpty());
    reopened.close();
    reopened.startUntitled();
    QVERIFY(reopened.resources().people.isEmpty());  // a new document starts without people
}

// FEAT-066 tappa 2: operazioni sui nodi, ognuna un passo di storico.
void DocumentTest::nodeOperationsAreUndoableSteps() {
    Document document;
    document.startUntitled();
    MindNode *root = document.model()->root();
    const auto step = [&] { document.flushPendingStep(); };
    MindNode *a = document.addChild(nullptr, QStringLiteral("A"));
    step();
    MindNode *b = document.addSibling(a);
    step();
    QCOMPARE(childTitles(root), (QStringList{QStringLiteral("A"), b->title}));
    document.addChild(a, QStringLiteral("A1"));
    step();
    QVERIFY(document.indentNode(b));  // B sotto A, dopo A1
    step();
    QCOMPARE(childTitles(root), QStringList{QStringLiteral("A")});
    QCOMPARE(childTitles(a).size(), 2);
    QVERIFY(document.outdentNode(b));
    step();
    QCOMPARE(childTitles(root).size(), 2);
    MindNode *copy = document.duplicateNode(a);
    step();
    QVERIFY(copy && copy->id != a->id);
    QCOMPARE(childTitles(copy), QStringList{QStringLiteral("A1")});
    QVERIFY(!document.duplicateNode(root));  // il centrale non si duplica
    // Eliminazione con un nodo dentro un ramo gia' da eliminare e il centrale (ignorato).
    QVERIFY(document.removeNodes({a, a->children.front().get(), root}));
    step();
    QCOMPARE(childTitles(root), (QStringList{copy->title, b->title}));
    QVERIFY(!document.removeNodes({root}));
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);  // torna A con A1
    QCOMPARE(childTitles(document.model()->root()).size(), 3);
    MindNode *floating = document.addFloatingTopic(QPointF(40, 50), QStringLiteral("Libero"));
    step();
    QVERIFY(floating->floating);
    QCOMPARE(floating->position, QPointF(40, 50));
    MindNode *parent = document.model()->root()->children.back().get();
    MindNode *first = document.insertBranches(parent, 0, {QJsonObject{{"title", "R1"}}, QJsonObject{{"title", "R2"}}});
    step();
    QCOMPARE(first->title, QStringLiteral("R1"));
    QCOMPARE(childTitles(parent), (QStringList{QStringLiteral("R1"), QStringLiteral("R2")}));
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);  // un solo passo per i due rami
    QVERIFY(childTitles(document.model()->root()->children.back().get()).isEmpty());
}

void DocumentTest::contentOperationsNotifyTheNodes() {
    Document document;
    document.startUntitled();
    MindNode *node = document.addChild(nullptr, QStringLiteral("Area"));
    document.flushPendingStep();
    QSignalSpy changed(&document, &Document::nodesChanged);
    node->titleHtml = QStringLiteral("<b>Area</b>");
    QVERIFY(document.setTitle(node, QStringLiteral("Superficie")));
    QVERIFY(node->titleHtml.isEmpty());  // testo semplice
    QVERIFY(!document.setTitle(node, QStringLiteral("Superficie")));  // uguale: nessun passo
    QVERIFY(document.setFormattedTitle(node, QStringLiteral("Superficie"), QStringLiteral("<i>Superficie</i>")));
    QVERIFY(document.setNotes(node, QStringLiteral("Misura in m2")));
    QVERIFY(document.setFormula(node, QStringLiteral(" \\pi r^2 ")));  // FEAT-064: contenuto del nodo, non nel titolo
    QCOMPARE(node->formula, QStringLiteral("\\pi r^2"));
    QCOMPARE(node->title, QStringLiteral("Superficie"));
    QVERIFY(!document.setFormula(node, QStringLiteral("\\pi r^2")));
    QVERIFY(document.setImage(node, path(QStringLiteral("foto.png")), QSizeF(320, 200)));
    QCOMPARE(node->imageOriginalName, QStringLiteral("foto.png"));
    QVERIFY(document.setImageSize(node, QSizeF(160, 100)));
    QCOMPARE(changed.size(), 6);
    QCOMPARE(changed.front().front().value<QList<MindNode *>>(), QList<MindNode *>{node});
    document.flushPendingStep();
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);  // modifiche ravvicinate: un passo
    QCOMPARE(document.model()->root()->children.front()->title, QStringLiteral("Area"));
}

// FEAT-066 tappa 3: relazioni, sommari e confini.
void DocumentTest::annotationsAreUndoableSteps() {
    Document document;
    document.startUntitled();
    MindNode *a = document.addChild(nullptr, QStringLiteral("A"));
    MindNode *b = document.addChild(nullptr, QStringLiteral("B"));
    document.flushPendingStep();
    QSignalSpy annotations(&document, &Document::annotationsChanged);
    QVERIFY(document.addRelation(a->id, a->id).isEmpty());
    QVERIFY(document.addRelation(a->id, QString()).isEmpty());
    QVERIFY(!document.addRelation(a->id, b->id).isEmpty());
    document.flushPendingStep();
    QCOMPARE(document.addSummaries({a}).size(), 1);
    QVERIFY(document.addSummaries({a}).isEmpty());  // the same run already has one
    QVERIFY(document.addSummaries({document.model()->root()}).isEmpty());  // not on the central topic
    document.flushPendingStep();
    QVERIFY(document.addBoundaries({}).isEmpty());
    QCOMPARE(document.addBoundaries({a, b, a}).size(), 1);  // siblings: one boundary (FEAT-052)
    document.flushPendingStep();
    QCOMPARE(annotations.size(), 3);
    QCOMPARE(document.model()->relations.size(), 1);
    QCOMPARE(document.model()->summaries.size(), 1);
    QCOMPARE(document.model()->summaries.front().topics, QStringList{a->id});
    QCOMPARE(document.model()->summaries.front().topic->title, QStringLiteral("Summary"));
    QCOMPARE(document.model()->boundaries.size(), 1);
    QCOMPARE(document.model()->boundaries.front().topics, (QStringList{a->id, b->id}));
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(document.model()->boundaries.isEmpty());
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(document.model()->summaries.isEmpty());
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(document.model()->relations.isEmpty());
    // Eliminando un nodo spariscono le sue relazioni, i suoi sommari e i confini rimasti vuoti.
    QCOMPARE(document.redo().outcome, Document::StepOutcome::Applied);
    QCOMPARE(document.redo().outcome, Document::StepOutcome::Applied);
    QVERIFY(document.removeNodes({document.model()->root()->children.front().get()}));
    QVERIFY(document.model()->relations.isEmpty());
    QVERIFY(document.model()->summaries.isEmpty());
}

// FEAT-066 tappa 4: Markdown incollato (rami o mappa intera), esportazione e ramo copiato.
void DocumentTest::pastedMarkdownBecomesBranchesOrTheWholeMap() {
    Document document;
    document.startUntitled();
    MindNode *a = document.addChild(nullptr, QStringLiteral("A"));
    document.flushPendingStep();
    const Document::PasteResult branches = document.pasteMarkdown(nullptr, 1, QStringLiteral("# X\nnota\n- X1\n# Y\n"));
    document.flushPendingStep();
    QVERIFY(!branches.tooLarge);
    QCOMPARE(branches.first->title, QStringLiteral("X"));
    QCOMPARE(childTitles(document.model()->root()), (QStringList{QStringLiteral("A"), QStringLiteral("X"), QStringLiteral("Y")}));
    QCOMPARE(branches.first->notes, QStringLiteral("nota"));
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);  // un passo per tutto l'incolla
    QCOMPARE(childTitles(document.model()->root()), QStringList{QStringLiteral("A")});
    QString huge;
    for (int i = 0; i <= MarkdownImport::kMaxPastedItems; ++i) huge += QStringLiteral("- v%1\n").arg(i);
    QVERIFY(document.pasteMarkdown(a, 0, huge).tooLarge);
    QVERIFY(document.replaceMapWithMarkdown(huge).tooLarge);
    QCOMPARE(childTitles(document.model()->root()), QStringList{QStringLiteral("A")});
    // Mappa intera solo con almeno due voci.
    QVERIFY(!document.replaceMapWithMarkdown(QStringLiteral("una parola")).first);
    const Document::PasteResult map = document.replaceMapWithMarkdown(QStringLiteral("# Fisica\n## Ottica\n"));
    document.flushPendingStep();
    QCOMPARE(map.first, document.model()->root());
    QCOMPARE(document.model()->root()->title, QStringLiteral("Fisica"));
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QCOMPARE(childTitles(document.model()->root()), QStringList{QStringLiteral("A")});
}

void DocumentTest::exportAndCopiedBranch() {
    Document document;
    document.startUntitled();
    MindNode *a = document.addChild(nullptr, QStringLiteral("A"));
    document.addChild(a, QStringLiteral("A1"));
    a->notes = QStringLiteral("Nota");
    const QString file = path(QStringLiteral("esportata.md"));
    QVERIFY(document.exportMarkdown(file).ok);
    QFile exported(file);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(exported.readAll());
    exported.close();
    QVERIFY(text.contains(QStringLiteral("\n  - A\n")));
    QVERIFY(text.contains(QStringLiteral("    Nota\n")));
    QVERIFY(text.contains(QStringLiteral("\n    - A1\n")));
    // Cartella impossibile da creare (al suo posto c'e' un file): errore.
    const Document::ExportResult failed = document.exportMarkdown(path(QStringLiteral("esportata.md/x.md")));
    QVERIFY(!failed.ok);
    QVERIFY(!failed.error.isEmpty());
    // Riletto, l'esportato da' lo stesso albero (andata e ritorno di FEAT-055).
    MindMapModel reopened;
    QVERIFY(reopened.fromMarkdown(text));
    QCOMPARE(childTitles(reopened.root()), QStringList{QStringLiteral("A")});
    QCOMPARE(reopened.root()->children.front()->notes, QStringLiteral("Nota"));
    // Copied branches (FEAT-042, 2026-11-05): with their children, in map order, a topic inside a
    // copied branch only once; the text has one title per line, indented with tabs.
    MindNode *b = document.addChild(nullptr, QStringLiteral("B"));
    const QList<QJsonObject> copied = Document::branchesFromData(document.branchesData({b, a->children.front().get(), a}));
    QCOMPARE(copied.size(), 2);
    QCOMPARE(copied.at(0).value(QStringLiteral("title")).toString(), QStringLiteral("A"));
    QCOMPARE(copied.at(0).value(QStringLiteral("children")).toArray().size(), 1);
    QCOMPARE(copied.at(1).value(QStringLiteral("title")).toString(), QStringLiteral("B"));
    QCOMPARE(document.branchesText({b, a}), QStringLiteral("A\n\tA1\nB"));
    QVERIFY(Document::branchesFromData(document.branchesData({nullptr})).isEmpty());
    QCOMPARE(Document::branchesFromData(QByteArray(R"({"title":"C"})")).size(), 1);  // one branch alone
}

// FEAT-054 (2026-10-18): cartella con il .md e assets/ (immagini con percorsi relativi,
// collegamenti web nel titolo).
void DocumentTest::exportFolderWithAssets() {
    QCOMPARE(Document::exportMarkdownPath(QStringLiteral("C:/Documenti/Biologia.md")),
             QStringLiteral("C:/Documenti/Biologia/Biologia.md"));
    QCOMPARE(Document::exportMarkdownPath(QStringLiteral("C:/Documenti/Biologia")),
             QStringLiteral("C:/Documenti/Biologia/Biologia.md"));
    // Immagine grande (oltre la miniatura di 512 px) e due immagini diverse con lo stesso nome.
    QDir().mkpath(path(QStringLiteral("sorgenti/altra")));
    const QString photo = path(QStringLiteral("sorgenti/cellula.png"));
    QImage big(1200, 800, QImage::Format_RGB32);
    big.fill(Qt::darkGreen);
    QVERIFY(big.save(photo));
    const QString sameName = path(QStringLiteral("sorgenti/altra/cellula.png"));
    QImage small(40, 30, QImage::Format_RGB32);
    small.fill(Qt::red);
    QVERIFY(small.save(sameName));

    Document document;
    document.startUntitled();
    MindNode *cell = document.addChild(nullptr, QStringLiteral("Cellula"));
    cell->webLink = QStringLiteral("https://it.wikipedia.org/wiki/Cellula");
    cell->notes = QStringLiteral("Nota della cellula");
    document.setImage(cell, photo, QSizeF(1200, 800));
    MindNode *again = document.addChild(nullptr, QStringLiteral("Ancora"));
    document.setImage(again, photo, QSizeF(1200, 800));  // stesso file: copiato una volta
    MindNode *other = document.addChild(nullptr, QStringLiteral("Altra"));
    document.setImage(other, sameName, QSizeF(40, 30));  // stesso nome, file diverso

    // Salvato e riaperto: le immagini vengono dal documento, alla qualita' originale.
    const QString bmap = path(QStringLiteral("con-allegati.mentis"));
    QVERIFY(document.save(bmap, BmapStorage::SaveReason::Explicit).ok);
    document.close();
    Document reopened;
    reopened.startUntitled();
    QVERIFY(reopened.open(bmap, false).ok);
    // Immagine aggiunta dopo e il cui file non esiste piu': si salta e si segnala.
    MindNode *lost = reopened.addChild(nullptr, QStringLiteral("Persa"));
    reopened.setImage(lost, path(QStringLiteral("sorgenti/non-esiste.png")), QSizeF(10, 10));
    const QString markdown = Document::exportMarkdownPath(path(QStringLiteral("export/Biologia.md")));
    const Document::ExportResult result = reopened.exportMarkdown(markdown);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.skipped, QStringList{QStringLiteral("Persa")});
    QFile file(markdown);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(file.readAll());
    QCOMPARE(text, QStringLiteral("- Central Topic\n"
                                  "  - [Cellula](https://it.wikipedia.org/wiki/Cellula)\n"
                                  "    Nota della cellula\n"
                                  "    ![Cellula](assets/cellula.png)\n"
                                  "  - Ancora\n"
                                  "    ![Ancora](assets/cellula.png)\n"
                                  "  - Altra\n"
                                  "    ![Altra](assets/cellula-2.png)\n"
                                  "  - Persa\n"));
    const QString assets = QFileInfo(markdown).dir().filePath(QStringLiteral("assets"));
    QCOMPARE(QImage(assets + QStringLiteral("/cellula.png")).size(), QSize(1200, 800));  // non la miniatura
    QCOMPARE(QImage(assets + QStringLiteral("/cellula-2.png")).size(), QSize(40, 30));
    QCOMPARE(QDir(assets).entryList(QDir::Files).size(), 2);
    // Una mappa senza allegati non crea la cartella assets.
    Document plain;
    plain.startUntitled();
    plain.addChild(nullptr, QStringLiteral("Solo testo"));
    const QString plainMarkdown = Document::exportMarkdownPath(path(QStringLiteral("export/Testo.md")));
    QVERIFY(plain.exportMarkdown(plainMarkdown).ok);
    QVERIFY(!QFileInfo::exists(QFileInfo(plainMarkdown).dir().filePath(QStringLiteral("assets"))));
    reopened.close();
}

// FEAT-086: move among the siblings of the same side, topic before and parent topic, each one step.
void DocumentTest::topicCommandsAreSteps() {
    Document document;
    document.startUntitled();
    const auto step = [&] { document.flushPendingStep(); };
    const auto root = [&] { return document.model()->root(); };
    for (const char *title : {"A", "B", "C", "D"}) document.addChild(nullptr, QString::fromLatin1(title));
    step();
    // A and B on the right, C and D on the left.
    const auto halves = [](const MindNode *node) { return node->title == QLatin1String("A") || node->title == QLatin1String("B") ? 1 : -1; };
    MindNode *a = root()->children[0].get();
    QVERIFY(document.moveAmongSiblings(a, 1, halves));
    step();
    QCOMPARE(childTitles(root()), (QStringList{"B", "A", "C", "D"}));
    QVERIFY(!document.moveAmongSiblings(a, 1, halves));  // the last of its side
    QVERIFY(!document.moveAmongSiblings(root()->children[2].get(), -1, halves));  // C: the first of its side
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QCOMPARE(childTitles(root()), (QStringList{"A", "B", "C", "D"}));
    // A and C on one side: they swap and B keeps its row.
    const auto alternate = [](const MindNode *node) { return node->title == QLatin1String("A") || node->title == QLatin1String("C") ? 1 : -1; };
    QVERIFY(document.moveAmongSiblings(root()->children[0].get(), 1, alternate));
    step();
    QCOMPARE(childTitles(root()), (QStringList{"C", "B", "A", "D"}));
    QVERIFY(document.moveAmongSiblings(root()->children[2].get(), -1, alternate));
    step();
    QCOMPARE(childTitles(root()), (QStringList{"A", "B", "C", "D"}));
    // Subtopics do not compare sides.
    MindNode *b = root()->children[1].get();
    MindNode *b1 = document.addChild(b, QStringLiteral("B1"));
    document.addChild(b, QStringLiteral("B2"));
    step();
    QVERIFY(document.moveAmongSiblings(b1, 1, alternate));
    step();
    QCOMPARE(childTitles(b), (QStringList{"B2", "B1"}));

    MindNode *before = document.addSiblingBefore(b1);
    step();
    QVERIFY(before);
    QCOMPARE(b->children[1].get(), before);
    QCOMPARE(b->children[2].get(), b1);
    QVERIFY(!document.addSiblingBefore(root()));
    MindNode *floating = document.addFloatingTopic(QPointF(40, 50), QStringLiteral("Free"));
    step();
    QVERIFY(!document.addSiblingBefore(floating));
    QVERIFY(!document.insertParent(floating));
    QVERIFY(!document.insertParent(root()));
    QVERIFY(!document.moveAmongSiblings(floating, -1, alternate));

    // Parent topic: it takes B's row and side, B under it, and B's boundary covers it.
    b->side = MindNode::Side::Left;
    const QStringList boundaries = document.addBoundaries({b});
    step();
    QCOMPARE(boundaries.size(), 1);
    MindNode *parent = document.insertParent(b);
    step();
    QVERIFY(parent);
    QCOMPARE(root()->children[1].get(), parent);
    QCOMPARE(childTitles(parent), QStringList{"B"});
    QCOMPARE(parent->side, MindNode::Side::Left);
    QCOMPARE(b->side, MindNode::Side::Auto);
    QCOMPARE(document.boundary(boundaries.front())->topics, QStringList{parent->id});
    QCOMPARE(document.undo().outcome, Document::StepOutcome::Applied);
    QCOMPARE(root()->children[1]->title, QStringLiteral("B"));
    QCOMPARE(document.boundary(boundaries.front())->topics, QStringList{root()->children[1]->id});
}

QTEST_MAIN(DocumentTest)
#include "document_test.moc"
