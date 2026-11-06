// FEAT-091: the JSON translation files: reading, writing, the merge with the code's texts, the .ts
// for lrelease, the 80% rule; and the real files of translations/ (Italian complete, every file in
// step with the template).
#include "translationfiles.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

using namespace TranslationFiles;

namespace {

Message text(const QString &context, const QString &source, const QString &translation, const QString &hint = {}) {
    return Message{context, source, hint, false, {translation}};
}
Message plural(const QString &context, const QString &source, const QStringList &forms) {
    return Message{context, source, {}, true, forms};
}

const Message *find(const QList<Message> &messages, const QString &context, const QString &source, const QString &hint = {}) {
    for (const Message &message : messages)
        if (message.context == context && message.source == source && message.hint == hint) return &message;
    return nullptr;
}

}  // namespace

class TranslationFilesTest : public QObject {
    Q_OBJECT
private slots:
    void jsonRoundTrip() {
        const QList<Message> messages{
            text("GanttView", "Later", "In ritardo"),
            text("GanttView", "Bold", "Grassetto", "branch width"),
            plural("GanttView", "%n task(s) removed", {"%n attività tolta", "%n attività tolte"}),
            text("UpdatePrompt", "Later", "Più tardi"),
            text("UpdatePrompt", "Say \"hi\"\n<b>", ""),
        };
        const QByteArray json = writeJson(messages);
        QVERIFY(json.contains("\"Bold \xe2\x80\x96 branch width\": \"Grassetto\""));  // the hint after " ‖ "
        QVERIFY(json.contains("[\"%n attività tolta\", \"%n attività tolte\"]"));
        QVERIFY(json.indexOf("GanttView") < json.indexOf("UpdatePrompt"));  // the code's order
        QList<Message> back;
        QString error;
        QVERIFY2(readJson(json, &back, &error), qPrintable(error));
        QCOMPARE(back.size(), messages.size());
        QCOMPARE(find(back, "GanttView", "Later")->forms, QStringList{"In ritardo"});
        QCOMPARE(find(back, "UpdatePrompt", "Later")->forms, QStringList{"Più tardi"});  // same text, other window
        QCOMPARE(find(back, "GanttView", "Bold", "branch width")->forms, QStringList{"Grassetto"});
        QVERIFY(find(back, "GanttView", "%n task(s) removed")->plural);
        QCOMPARE(find(back, "UpdatePrompt", "Say \"hi\"\n<b>")->forms, QStringList{QString()});
    }

    void invalidJsonSaysWhere() {
        QList<Message> messages;
        QString error;
        QVERIFY(!readJson("{\n  \"A\": {\n    \"x\": \"y\"\n    \"z\": \"w\"\n  }\n}\n", &messages, &error));
        QVERIFY2(error.contains("line 4"), qPrintable(error));  // the missing comma
        QVERIFY(!readJson("{\"A\": {\"x\": 3}}", &messages, &error));
        QVERIFY(error.contains("\"x\""));
        QVERIFY(!readJson("[]", &messages, &error));
    }

    void mergeKeepsTranslationsAndFollowsTheCode() {
        const QList<Message> code{
            text("Main", "Save", ""),
            text("Main", "Open", ""),
            plural("Main", "%n file(s)", {""}),
        };
        const QList<Message> translated{
            text("Main", "Save", "Salva"),
            text("Main", "Gone", "Sparito"),
            text("Other", "Open", "Apri"),  // another window: not this one's translation
        };
        const QList<Message> merged = merge(code, translated);
        QCOMPARE(merged.size(), 3);
        QCOMPARE(merged[0].forms, QStringList{"Salva"});
        QCOMPARE(merged[1].forms, QStringList{QString()});
        QCOMPARE(merged[2].forms, (QStringList{QString(), QString()}));  // two empty forms to fill
        QVERIFY(!find(merged, "Main", "Gone"));
    }

    void completenessAndThreshold() {
        QList<Message> messages;
        for (int i = 0; i < 10; ++i) messages.append(text("C", QString::number(i), i < 8 ? "t" : ""));
        QCOMPARE(completeness(messages).done, 8);
        QVERIFY(offered(completeness(messages), 80));
        messages[7].forms = {QString()};
        QVERIFY(!offered(completeness(messages), 80));  // 70%
        // A plural with one empty form is not done.
        QCOMPARE(completeness({plural("C", "%n", {"uno", ""})}).done, 0);
        QVERIFY(!offered(Completeness{}, 80));
    }

    void tsForLrelease() {
        const QByteArray ts = writeTs({text("Main", "A & <b>", "A e <b>"), text("Main", "Empty", ""),
                                       text("Main", "Bold", "Grassetto", "branch width"),
                                       plural("Main", "%n day(s)", {"%n giorno", "%n giorni"})},
                                      "it");
        QVERIFY(ts.contains("language=\"it\""));
        QVERIFY(ts.contains("A &amp; &lt;b&gt;"));
        QVERIFY(ts.contains("<comment>branch width</comment>"));
        QVERIFY(ts.contains("<numerusform>%n giorni</numerusform>"));
        // Only the empty text is unfinished: lrelease -nounfinished leaves it in English.
        QCOMPARE(ts.count("type=\"unfinished\""), 1);
        QTemporaryDir dir;
        QFile file(dir.filePath("it.ts"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(ts);
        file.close();
        QList<Message> back;
        QString error;
        QVERIFY2(readTs(file.fileName(), &back, &error), qPrintable(error));
        QCOMPARE(back.size(), 4);
        QCOMPARE(find(back, "Main", "A & <b>")->forms, QStringList{"A e <b>"});
        QCOMPARE(find(back, "Main", "%n day(s)")->forms, (QStringList{"%n giorno", "%n giorni"}));
    }

    // The files of the repository: valid, the same texts as the template, Italian complete (the
    // rule of AGENTS.md: a text of the interface comes in English and Italian together).
    void repositoryFiles() {
        const QDir folder(QStringLiteral(MENTIS_TRANSLATIONS_DIR));
        QList<Message> templateMessages;
        QString error;
        QVERIFY2(readJsonFile(folder.filePath("template.json"), &templateMessages, &error), qPrintable(error));
        QVERIFY(templateMessages.size() > 100);
        QCOMPARE(completeness(templateMessages).done, 0);
        const QStringList languages = folder.entryList({"*.json"}, QDir::Files);
        QVERIFY(languages.contains("it.json"));
        for (const QString &name : languages) {
            QList<Message> messages;
            QVERIFY2(readJsonFile(folder.filePath(name), &messages, &error), qPrintable(error));
            QVERIFY2(messages.size() == templateMessages.size(), qPrintable(name + ": run update_translations"));
            if (name == QLatin1String("it.json")) {
                const Completeness italian = completeness(messages);
                QVERIFY2(italian.done == italian.total,
                         qPrintable(QStringLiteral("it.json: %1 of %2 texts translated").arg(italian.done).arg(italian.total)));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TranslationFilesTest)
#include "translation_files_test.moc"
