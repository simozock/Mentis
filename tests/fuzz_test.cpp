// Fuzzing with seeded mutations (2026-11-09, user's request): valid inputs bent at random, the same
// on every run (fixed seed), through the targets of tests/fuzz/fuzztargets.h. A target that
// crashes fails the run; the input is in mentis-fuzz-<target>.bin in the temporary folder, to be
// replayed and turned into a test. MENTIS_FUZZ_ITERATIONS raises the count (default 300 each);
// MENTIS_FUZZ_SEED tries another sequence. MENTIS_FUZZ_REPLAY=<file> runs only that input through
// the target of MENTIS_FUZZ_REPLAY_TARGET (markdown, rich-text, map-json, document), as replay().
#include "fuzz/fuzztargets.h"
#include "mindmapmodel.h"
#include "relationship.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPair>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <functional>

class FuzzTest final : public QObject {
    Q_OBJECT

private slots:
    void replay();
    void corpus();
    void markdown();
    void richText();
    void mapJson();
    void document();

private:
    // Runs target on mutations of the seeds; the input of each run is kept until the next.
    void run(const QString &name, const QList<QByteArray> &seeds, const std::function<void(const QByteArray &)> &target,
             bool json = false, int count = 0);
};

namespace {

int iterations() {
    bool ok = false;
    const int value = qEnvironmentVariableIntValue("MENTIS_FUZZ_ITERATIONS", &ok);
    return ok && value > 0 ? value : 300;
}

quint32 seed() {
    bool ok = false;
    const int value = qEnvironmentVariableIntValue("MENTIS_FUZZ_SEED", &ok);
    return ok ? quint32(value) : 20261109u;
}

// Tokens that tend to break parsers: structure, extreme numbers, odd text.
const QList<QByteArray> &dictionary() {
    static const QList<QByteArray> tokens{
        "{", "}", "[", "]", ",", ":", "\"", "null", "true", "false", "-1", "0", "1e308", "-1e308", "1e-320",
        "2147483648", "-2147483649", "NaN", "\"children\"", "\"id\"", "\"title\"", "\"titleHtml\"", "\"notesHtml\"",
        "\"parentId\"", "\"rootId\"", "\"root\"", "\"files\"", "\"labels\"", "\"markers\"", "\"relations\"",
        "\"summaries\"", "\"boundaries\"", "\"gantt\"", "\"x\"", "\"customWidth\"", "\"fontSize\"", "\"structure\"",
        "<img src=\"//server/x.png\">", "<style>@import url(x.css);</style>", "<table background=\"a.png\">",
        "<p>", "</p>", "<b>", "<a href=\"file:///x\">", "&#0;", "\\u0000", "\xff\xfe", "\xe2\x80\xae", "\n",
        "    ", "\t", "# ", "- ", "* ", "1. ", "- [ ] ", "> ", "```", "$$", "\\frac{", "|", "../", "%00"};
    return tokens;
}

QByteArray mutate(QByteArray data, QRandomGenerator &random) {
    const int changes = 1 + int(random.bounded(4));
    for (int i = 0; i < changes; ++i) {
        const int at = data.isEmpty() ? 0 : int(random.bounded(quint32(data.size())));
        switch (random.bounded(6)) {
        case 0:  // a byte changed
            if (!data.isEmpty()) data[at] = char(random.bounded(256));
            break;
        case 1:  // a token inserted
            data.insert(at, dictionary().at(int(random.bounded(quint32(dictionary().size())))));
            break;
        case 2:  // a range removed
            data.remove(at, int(random.bounded(64)) + 1);
            break;
        case 3: {  // a range repeated
            const QByteArray piece = data.mid(at, int(random.bounded(128)) + 1);
            data.insert(at, piece.repeated(1 + int(random.bounded(8))));
            break;
        }
        case 4:  // cut short
            data.truncate(at);
            break;
        default: {  // a number replaced by an extreme one
            static const QList<QByteArray> numbers{"1e308", "-1e308", "999999999999", "-2147483648", "0", "-0.0"};
            const qsizetype digit = data.indexOf(QByteArray::number(int(random.bounded(10))), at);
            if (digit >= 0) data.replace(digit, 1, numbers.at(int(random.bounded(quint32(numbers.size())))));
            break;
        }
        }
        if (data.size() > 256 * 1024) data.truncate(256 * 1024);  // keeps each run quick
    }
    return data;
}

// A value of another kind, or an extreme one (structure-aware mutations of the map's JSON).
QJsonValue oddValue(QRandomGenerator &random, const QJsonValue &like) {
    switch (random.bounded(12)) {
    case 0: return QJsonValue();
    case 1: return QJsonValue(true);
    case 2: return QJsonValue(-1);
    case 3: return QJsonValue(1e308);
    case 4: return QJsonValue(-1e308);
    case 5: return QJsonValue(2147483648.0);
    case 6: return QJsonValue(QString());
    case 7: return QJsonValue(QString(int(random.bounded(5000)) + 1, QChar(0x202E)));
    case 8: return QJsonValue(QJsonArray{like, like});
    case 9: return QJsonValue(QJsonObject{{QStringLiteral("children"), QJsonArray{like}}});
    case 10: return like.isString() ? QJsonValue(like.toString() + like.toString()) : QJsonValue(QStringLiteral("x"));
    default: return QJsonValue(QJsonArray());
    }
}

// Walks down to one value at random and changes it: replaced, removed, its siblings repeated, or a
// key every node knows added with an odd value.
QJsonValue mutateJson(const QJsonValue &value, QRandomGenerator &random, int depth = 0) {
    static const QStringList keys{"id", "title", "titleHtml", "notes", "notesHtml", "children", "labels", "markers",
                                  "files", "webLink", "topicLink", "formula", "x", "y", "customWidth", "fontSize",
                                  "structure", "structureDirection", "side", "floating", "collapsed", "task",
                                  "imageAssetId", "image", "imageSize", "summaries", "boundaries", "relations",
                                  "gantt", "pitchLayout", "fill", "shape", "from", "to", "topics", "rootId", "row"};
    const bool here = depth > 12 || random.bounded(4) == 0;
    if (value.isObject()) {
        QJsonObject object = value.toObject();
        if (here || object.isEmpty()) {
            const QString key = random.bounded(2) && !object.isEmpty() ? object.keys().at(int(random.bounded(quint32(object.size()))))
                                                                       : keys.at(int(random.bounded(quint32(keys.size()))));
            if (random.bounded(5) == 0) object.remove(key);
            else object.insert(key, oddValue(random, object.value(key)));
            return object;
        }
        const QString key = object.keys().at(int(random.bounded(quint32(object.size()))));
        object.insert(key, mutateJson(object.value(key), random, depth + 1));
        return object;
    }
    if (value.isArray()) {
        QJsonArray array = value.toArray();
        if (here || array.isEmpty()) {
            if (!array.isEmpty() && random.bounded(2)) {
                const QJsonValue copy = array.at(int(random.bounded(quint32(array.size()))));
                for (int i = int(random.bounded(50)); i >= 0; --i) array.append(copy);  // also repeated ids
            } else {
                array.append(oddValue(random, array.isEmpty() ? QJsonValue() : array.first()));
            }
            return array;
        }
        const int at = int(random.bounded(quint32(array.size())));
        array.replace(at, mutateJson(array.at(at), random, depth + 1));
        return array;
    }
    return oddValue(random, value);
}

QByteArray seedMap() {
    MindMapModel model;
    MindNode *root = model.root();
    root->notes = QStringLiteral("Note");
    root->notesHtml = QStringLiteral("<p>Note <b>bold</b></p>");
    MindNode *a = model.addChild(root, QStringLiteral("Uno"));
    a->titleHtml = QStringLiteral("<p><i>Uno</i></p>");
    a->labels = {QStringLiteral("etichetta")};
    a->webLink = QStringLiteral("https://example.org");
    a->formula = QStringLiteral("\\frac{a}{b}");
    a->task = MindNode::Task::Open;
    MindNode *b = model.addChild(a, QStringLiteral("Due"));
    model.addChild(b, QStringLiteral("Tre"));
    MindNode *c = model.addChild(root, QStringLiteral("Quattro"));
    c->floating = true;
    c->collapsed = true;
    Relationship relation;
    relation.id = QStringLiteral("rel");
    relation.from = a->id;
    relation.to = c->id;
    model.relations.append(relation);
    return QJsonDocument(model.toJson()).toJson(QJsonDocument::Compact);
}

}  // namespace

void FuzzTest::run(const QString &name, const QList<QByteArray> &seeds, const std::function<void(const QByteArray &)> &target,
                   bool json, int count) {
    QRandomGenerator random(seed());
    QFile last(QDir::temp().filePath(QStringLiteral("mentis-fuzz-%1.bin").arg(name)));
    QElapsedTimer timer;
    timer.start();
    if (count <= 0) count = iterations();
    QList<QByteArray> pool = seeds;  // a mutation that still parses is mutated again later
    for (int i = 0; i < count; ++i) {
        QByteArray input;
        if (i < seeds.size()) input = seeds.at(i);
        else if (!json || random.bounded(8) == 0) input = mutate(pool.at(int(random.bounded(quint32(pool.size())))), random);
        else {
            QJsonValue value = QJsonDocument::fromJson(pool.at(int(random.bounded(quint32(pool.size()))))).object();
            for (int step = int(random.bounded(3)); step >= 0; --step) value = mutateJson(value, random);
            input = QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
            if (pool.size() < 64 && input.size() < 64 * 1024) pool.append(input);
        }
        if (last.open(QIODevice::WriteOnly | QIODevice::Truncate)) { last.write(input); last.close(); }
        target(input);
    }
    QFile::remove(last.fileName());  // nothing to replay
    qInfo().noquote() << name << count << "inputs in" << timer.elapsed() << "ms";
}

void FuzzTest::replay() {
    const QString file = qEnvironmentVariable("MENTIS_FUZZ_REPLAY");
    if (file.isEmpty()) QSKIP("MENTIS_FUZZ_REPLAY not set");
    QFile input(file);
    QVERIFY(input.open(QIODevice::ReadOnly));
    const QByteArray data = input.readAll();
    const QString target = qEnvironmentVariable("MENTIS_FUZZ_REPLAY_TARGET");
    QTemporaryDir folder;
    if (target == QStringLiteral("markdown")) FuzzTargets::markdown(data);
    else if (target == QStringLiteral("rich-text")) FuzzTargets::richText(data);
    else if (target == QStringLiteral("map-json")) FuzzTargets::mapJson(data);
    else if (target == QStringLiteral("document")) FuzzTargets::document(data, folder.path());
    else QFAIL("MENTIS_FUZZ_REPLAY_TARGET: markdown, rich-text, map-json or document");
}

// The examples in tests/fuzz/corpus, the libFuzzer seeds and what it found on CI (regressions), each
// through its target within two seconds.
void FuzzTest::corpus() {
    QTemporaryDir folder;
    const QList<QPair<QString, std::function<void(const QByteArray &)>>> targets{
        {QStringLiteral("markdown"), &FuzzTargets::markdown},
        {QStringLiteral("richText"), &FuzzTargets::richText},
        {QStringLiteral("mapJson"), &FuzzTargets::mapJson},
        {QStringLiteral("document"), [&folder](const QByteArray &data) { FuzzTargets::document(data, folder.path()); }}};
    int files = 0;
    for (const auto &[name, target] : targets) {
        const QDir dir(QStringLiteral(MENTIS_FUZZ_CORPUS) + QLatin1Char('/') + name);
        for (const QFileInfo &info : dir.entryInfoList(QDir::Files)) {
            QFile file(info.filePath());
            QVERIFY(file.open(QIODevice::ReadOnly));
            QElapsedTimer timer;
            timer.start();
            target(file.readAll());
            if (!qEnvironmentVariableIsSet("MENTIS_SANITIZED"))
                QVERIFY2(timer.elapsed() < 2000, qPrintable(QStringLiteral("%1: %2 ms").arg(info.fileName()).arg(timer.elapsed())));
            ++files;
        }
    }
    QVERIFY(files >= 4);
}

void FuzzTest::markdown() {
    run(QStringLiteral("markdown"),
        {"# Titolo\n\n- uno\n  - due\n    - tre\n- [x] fatto\n\n1. primo\n2. secondo\n\n> nota\n\n$$\\frac{a}{b}$$\n",
         "Testo **grassetto** e *corsivo* con [link](https://example.org) e `codice`.\n\n| a | b |\n|---|---|\n| 1 | 2 |\n"},
        &FuzzTargets::markdown);
}

void FuzzTest::richText() {
    run(QStringLiteral("rich-text"),
        {"<p>Uno <b>due</b> <i>tre</i> <a href=\"https://example.org\">link</a></p>",
         "<html><head><style>p { margin: 0 }</style></head><body><table><tr><td style=\"background-color:#ff0\">x</td></tr></table>"
         "<p><img src=\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==\"></p></body></html>"},
        &FuzzTargets::richText);
}

void FuzzTest::mapJson() { run(QStringLiteral("map-json"), {seedMap()}, &FuzzTargets::mapJson, true); }

void FuzzTest::document() {
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    // A file each time (SQLite, synchronous=FULL): about 90 ms, so a fifth of the runs.
    run(QStringLiteral("document"), {seedMap()}, [&folder](const QByteArray &data) { FuzzTargets::document(data, folder.path()); },
        true, std::max(20, iterations() / 5));
}

QTEST_MAIN(FuzzTest)
#include "fuzz_test.moc"
