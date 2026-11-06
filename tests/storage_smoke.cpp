#include "bmapstorage.h"
#include "maphistory.h"
#include "mindmapmodel.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonObject>
#include <QImage>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <cstdio>

namespace {
int fail(const char *message) { std::fprintf(stderr, "%s\n", message); return 1; }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Mentis"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisStorageTest"));

    QTemporaryDir directory;
    if (!directory.isValid()) return fail("temporary directory");
    const QString imagePath = directory.filePath(QStringLiteral("image.png"));
    { QImage image(1024, 768, QImage::Format_RGB32); image.fill(Qt::red); if (!image.save(imagePath)) return fail("image fixture"); }

    MindMapModel source;
    source.root()->title = QStringLiteral("Documento SQLite");
    MindNode *child = source.addChild(source.root(), QStringLiteral("Nodo incorporato"));
    child->notes = QStringLiteral("Nota persistente");
    child->position = {123.5, -42.25};
    child->imagePath = imagePath;

    const QString documentPath = directory.filePath(QStringLiteral("roundtrip.mentis"));
    QString error;
    if (!BmapStorage::save(documentPath, source, BmapStorage::SaveReason::Automatic, &error))
        return fail(qPrintable(error));
    if (!BmapStorage::isBmapDatabase(documentPath)) return fail("SQLite signature");

    MindMapModel restored;
    if (!BmapStorage::load(documentPath, restored, &error)) return fail(qPrintable(error));
    if (restored.root()->title != source.root()->title || restored.root()->children.size() != 1)
        return fail("tree round-trip");
    const MindNode *restoredChild = restored.root()->children.front().get();
    if (restoredChild->title != child->title || restoredChild->notes != child->notes || restoredChild->position != child->position)
        return fail("node round-trip");
    QFile restoredImage(restoredChild->imagePath);
    if (!restoredImage.open(QIODevice::ReadOnly) || restoredImage.size() <= 0 || restoredChild->imageAssetId.isEmpty()) {
        std::fprintf(stderr, "thumbnail path=%s size=%lld asset=%s error=%s\n",
                     qPrintable(restoredChild->imagePath), static_cast<long long>(restoredImage.size()),
                     qPrintable(restoredChild->imageAssetId), qPrintable(error));
        return fail("embedded image thumbnail");
    }

    // A sticker with transparency must reopen transparent: its thumbnail cannot be JPEG.
    {
        const QString stickerPath = directory.filePath(QStringLiteral("sticker.png"));
        QImage sticker(64, 64, QImage::Format_ARGB32); sticker.fill(Qt::transparent);
        for (int y = 16; y < 48; ++y) for (int x = 16; x < 48; ++x) sticker.setPixelColor(x, y, Qt::blue);
        if (!sticker.save(stickerPath)) return fail("sticker fixture");
        MindMapModel withSticker;
        withSticker.addChild(withSticker.root(), QStringLiteral("Adesivo"))->imagePath = stickerPath;
        const QString stickerDocument = directory.filePath(QStringLiteral("sticker.mentis"));
        if (!BmapStorage::save(stickerDocument, withSticker, BmapStorage::SaveReason::Explicit, &error))
            return fail(qPrintable(error));
        MindMapModel reopened;
        if (!BmapStorage::load(stickerDocument, reopened, &error)) return fail(qPrintable(error));
        const QImage thumbnail(reopened.root()->children.front()->imagePath);
        if (thumbnail.isNull() || thumbnail.pixelColor(2, 2).alpha() != 0
            || thumbnail.pixelColor(thumbnail.width() / 2, thumbnail.height() / 2) != QColor(Qt::blue))
            return fail("transparent sticker thumbnail");
    }

    MindMapModel secondSheet;
    secondSheet.root()->title = QStringLiteral("Seconda mappa");
    secondSheet.addChild(secondSheet.root(), QStringLiteral("Nodo del secondo foglio"));
    if (!BmapStorage::saveDocument(documentPath, secondSheet.toJson(), BmapStorage::SaveReason::Automatic,
                                   &error, QString(), QStringLiteral("sheet-2"), QStringLiteral("Foglio 2"), 1))
        return fail(qPrintable(error));
    const auto maps = BmapStorage::maps(documentPath, &error);
    if (maps.size() != 2 || maps[0].id != QStringLiteral("main") || maps[1].title != QStringLiteral("Foglio 2"))
        return fail("map index round-trip");
    MindMapModel restoredSecondSheet;
    if (!BmapStorage::load(documentPath, restoredSecondSheet, &error, false, QStringLiteral("sheet-2")))
        return fail(qPrintable(error));
    if (restoredSecondSheet.root()->title != secondSheet.root()->title || restoredSecondSheet.root()->children.size() != 1)
        return fail("second map round-trip");

    if (!BmapStorage::save(documentPath, restored, BmapStorage::SaveReason::Explicit, &error))
        return fail(qPrintable(error));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("snapshot-test"));
        db.setDatabaseName(documentPath);
        if (!db.open()) return fail("snapshot database open");
        QSqlQuery query(db);
        if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM assets WHERE length(thumbnail)>0")) || !query.next() || query.value(0).toInt() != 1)
            return fail("stored thumbnail");
        if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM snapshots WHERE reason='explicit'")) || !query.next() || query.value(0).toInt() != 1)
            return fail("explicit snapshot");
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("snapshot-test"));

    MapHistory history(100);
    MapHistory::Snapshot snapshot;
    snapshot.insert(QStringLiteral("node"), QJsonObject{{QStringLiteral("value"), 0}});
    history.reset(snapshot);
    for (int i = 1; i <= 205; ++i) {
        snapshot[QStringLiteral("node")][QStringLiteral("value")] = i;
        if (!history.commit(snapshot)) return fail("history commit");
    }
    for (int i = 0; i < 105; ++i) history.undo();
    if (history.current().value(QStringLiteral("node")).value(QStringLiteral("value")).toInt() != 100)
        return fail("paged undo");
    for (int i = 0; i < 105; ++i) history.redo();
    if (history.current().value(QStringLiteral("node")).value(QStringLiteral("value")).toInt() != 205)
        return fail("paged redo");

    // 2026-10-14: storico unico per più fogli. Ogni passo sa a quale foglio appartiene; anche dopo
    // il passaggio dal giornale SQLite (oltre i 100 passi in RAM) il foglio resta quello giusto.
    {
        MapHistory sheets(4);
        MapHistory::Snapshot a, b;
        a.insert(QStringLiteral("node"), QJsonObject{{QStringLiteral("value"), 0}});
        b = a;
        sheets.setSheet(QStringLiteral("A"), a);
        sheets.setSheet(QStringLiteral("B"), b);
        for (int i = 1; i <= 6; ++i) { a[QStringLiteral("node")][QStringLiteral("value")] = i; sheets.commit(QStringLiteral("A"), a); }
        for (int i = 1; i <= 3; ++i) { b[QStringLiteral("node")][QStringLiteral("value")] = 10 * i; sheets.commit(QStringLiteral("B"), b); }
        if (sheets.undoSheet() != QStringLiteral("B")) return fail("multi-sheet undo sheet");
        for (int i = 0; i < 3; ++i) sheets.undo();
        if (sheets.current(QStringLiteral("B")).value(QStringLiteral("node")).value(QStringLiteral("value")).toInt() != 0)
            return fail("multi-sheet undo B");
        if (sheets.undoSheet() != QStringLiteral("A")) return fail("multi-sheet next undo is A");
        sheets.undo();
        if (sheets.current(QStringLiteral("A")).value(QStringLiteral("node")).value(QStringLiteral("value")).toInt() != 5)
            return fail("multi-sheet undo A");
        if (sheets.redoSheet() != QStringLiteral("A")) return fail("multi-sheet redo sheet");
        if (sheets.isAtSavedState()) return fail("multi-sheet saved state");
    }

    // 2026-10-14: tutti i fogli in una transazione, un solo punto di ripristino per salvataggio,
    // fogli eliminati, foglio non riscritto (writeContent = false) e anteprima nel file.
    {
        const QString sheetsPath = directory.filePath(QStringLiteral("sheets.mentis"));
        MindMapModel first; first.root()->title = QStringLiteral("Primo");
        first.addChild(first.root(), QStringLiteral("Uno"));
        MindMapModel second; second.root()->title = QStringLiteral("Secondo");
        MindMapModel third; third.root()->title = QStringLiteral("Terzo");
        QList<BmapStorage::SheetWrite> writes{
            {QStringLiteral("s1"), QStringLiteral("Foglio 1"), 0, true, first.toFlatSnapshot()},
            {QStringLiteral("s2"), QStringLiteral("Foglio 2"), 1, true, second.toFlatSnapshot()},
            {QStringLiteral("s3"), QStringLiteral("Foglio 3"), 2, true, third.toFlatSnapshot()}};
        if (!BmapStorage::saveSheets(sheetsPath, writes, BmapStorage::SaveReason::Explicit, &error, QString(), QByteArray("jpeg-bytes")))
            return fail(qPrintable(error));
        if (BmapStorage::loadPreview(sheetsPath) != QByteArray("jpeg-bytes")) return fail("embedded preview");
        // Secondo salvataggio esplicito identico: nessun nuovo punto di ripristino.
        if (!BmapStorage::saveSheets(sheetsPath, writes, BmapStorage::SaveReason::Explicit, &error)) return fail(qPrintable(error));
        // Foglio 3 eliminato; foglio 2 rinominato senza contenuto (già nel file).
        writes.removeLast();
        writes[1].writeContent = false;
        writes[1].content.clear();
        writes[1].title = QStringLiteral("Rinominato");
        if (!BmapStorage::saveSheets(sheetsPath, writes, BmapStorage::SaveReason::Automatic, &error)) return fail(qPrintable(error));
        const auto infos = BmapStorage::maps(sheetsPath, &error);
        if (infos.size() != 2 || infos[1].title != QStringLiteral("Rinominato")) return fail("sheet list after save");
        MindMapModel reloaded;
        if (!BmapStorage::load(sheetsPath, reloaded, &error, true, QStringLiteral("s2")) || reloaded.root()->title != QStringLiteral("Secondo"))
            return fail("sheet kept without rewrite");
        // Un foglio non caricato e assente dal file deve far fallire tutto il salvataggio (nessun file a metà).
        QList<BmapStorage::SheetWrite> broken = writes;
        broken.append({QStringLiteral("missing"), QStringLiteral("Mancante"), 2, false, {}});
        broken[0].content[broken[0].content.value(QStringLiteral("@map")).value(QStringLiteral("rootId")).toString()][QStringLiteral("title")] = QStringLiteral("Non salvato");
        if (BmapStorage::saveSheets(sheetsPath, broken, BmapStorage::SaveReason::Automatic, &error)) return fail("broken save accepted");
        if (!BmapStorage::load(sheetsPath, reloaded, &error, true, QStringLiteral("s1")) || reloaded.root()->title != QStringLiteral("Primo"))
            return fail("failed save was not rolled back");
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("sheets-test"));
        db.setDatabaseName(sheetsPath);
        if (!db.open()) return fail("sheets database open");
        {
            QSqlQuery query(db);
            if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM snapshots")) || !query.next() || query.value(0).toInt() != 1)
                return fail("one snapshot per explicit save");
        }
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(QStringLiteral("sheets-test"));
    }

    // 2026-10-14: eliminando un nodo spariscono le relazioni e i sommari che lo usano.
    {
        MindMapModel model;
        MindNode *a = model.addChild(model.root(), QStringLiteral("A"));
        MindNode *b = model.addChild(model.root(), QStringLiteral("B"));
        model.relations.append({QStringLiteral("r"), a->id, b->id});
        Summary summary{QStringLiteral("m"), {b->id}, std::make_shared<MindNode>()};
        summary.topic->id = QStringLiteral("mt");
        summary.topic->summaryTopic = true;
        model.summaries.append(summary);
        model.relations.append({QStringLiteral("t"), a->id, QStringLiteral("mt")});  // goes with the summary
        model.boundaries.append({QStringLiteral("g"), {a->id, b->id}});
        model.boundaries.append({QStringLiteral("h"), {b->id}});
        model.relations.append({QStringLiteral("s"), a->id, QStringLiteral("h")});
        model.removeNode(b);
        // FEAT-052: the emptied boundary goes, with the relationship ending on it.
        if (!model.relations.isEmpty() || !model.summaries.isEmpty() || model.boundaries.size() != 1 ||
            model.boundaries.front().topics != QStringList{a->id})
            return fail("annotations pruned");
    }

    // 2026-10-14: gli "a capo" dei titoli sopravvivono a esportazione e importazione Markdown.
    {
        MindMapModel model;
        model.root()->title = QStringLiteral("Radice");
        model.addChild(model.root(), QStringLiteral("Prima riga\nSeconda riga"));
        MindMapModel imported;
        if (!imported.fromMarkdown(model.toMarkdown(false), &error) || imported.root()->children.size() != 1 ||
            imported.root()->children.front()->title != QStringLiteral("Prima riga\nSeconda riga"))
            return fail("markdown line breaks");
    }

    // Fault injection di base: destinazioni non valide e file corrotti devono fallire con un
    // errore utile senza alterare dati estranei. I guasti durante una transazione sono coperti
    // sopra dal caso saveSheets con foglio mancante e successiva verifica del rollback.
    {
        MindMapModel model;
        const QByteArray marker("not-a-database");
        const QString corruptPath = directory.filePath(QStringLiteral("corrupt.mentis"));
        QFile corrupt(corruptPath);
        if (!corrupt.open(QIODevice::WriteOnly) || corrupt.write(marker) != marker.size())
            return fail("corrupt fixture");
        corrupt.close();

        MindMapModel untouched;
        untouched.root()->title = QStringLiteral("Da conservare");
        error.clear();
        if (BmapStorage::load(corruptPath, untouched, &error)) return fail("corrupt file accepted");
        if (error.isEmpty()) return fail("corrupt file without diagnostic");
        if (untouched.root()->title != QStringLiteral("Da conservare")) return fail("failed load changed model");
        QFile verify(corruptPath);
        if (!verify.open(QIODevice::ReadOnly) || verify.readAll() != marker) return fail("failed load changed source file");

        error.clear();
        if (BmapStorage::save(directory.path(), model, BmapStorage::SaveReason::Explicit, &error))
            return fail("directory accepted as document path");
        if (error.isEmpty()) return fail("invalid destination without diagnostic");
    }
    return 0;
}
