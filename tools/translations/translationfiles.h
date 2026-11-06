#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// FEAT-091: the translations are one JSON file per language (translations/<code>.json), grouped by
// Qt's context; the code keeps tr(). These functions read and write those files, merge them with
// the texts lupdate finds in the code, write the .ts that lrelease compiles, and count what is done.
// Qt Core only: the build runs them through the MentisTranslations tool.
namespace TranslationFiles {

// One text of the interface. forms: the translation, or one entry per plural form; an empty entry
// is not translated.
struct Message {
    QString context;
    QString source;
    QString hint;  // the translator's hint, tr(source, hint)
    bool plural = false;
    QStringList forms;
};

// The JSON key: the English text, then " ‖ " and the hint when there is one.
QString key(const Message &message);

// lupdate's .ts (or an old translated one): every message with its translation; obsolete and
// vanished ones are left out, an unfinished one counts as not translated.
bool readTs(const QString &path, QList<Message> *messages, QString *error);
bool readJson(const QByteArray &json, QList<Message> *messages, QString *error);
bool readJsonFile(const QString &path, QList<Message> *messages, QString *error);

// Contexts and texts in the order of the list (lupdate's), two spaces of indent, UTF-8.
QByteArray writeJson(const QList<Message> &messages);
// A .ts for lrelease; a text not fully translated is "unfinished" (left out with -nounfinished).
QByteArray writeTs(const QList<Message> &messages, const QString &language);

// The texts of the code, in their order, with the translations of the same context and key from
// translated; a new text is empty (two forms when plural). Texts no longer in the code go.
QList<Message> merge(const QList<Message> &code, const QList<Message> &translated);

struct Completeness {
    int total = 0;
    int done = 0;
};
Completeness completeness(const QList<Message> &messages);
// At least threshold percent done (and something to translate).
bool offered(const Completeness &completeness, int thresholdPercent);

}  // namespace TranslationFiles
