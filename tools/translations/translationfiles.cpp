#include "translationfiles.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace TranslationFiles {

namespace {

const QString kHintSeparator = QStringLiteral(" ‖ ");  // " ‖ ": in no text of the code

bool done(const Message &message) {
    if (message.forms.isEmpty()) return false;
    for (const QString &form : message.forms)
        if (form.isEmpty()) return false;
    return true;
}

// A JSON string with its quotes, UTF-8 left as it is.
QByteArray quoted(const QString &text) {
    const QByteArray array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return array.mid(1, array.size() - 2);
}

QString lookupKey(const QString &context, const QString &messageKey) { return context + QChar(1) + messageKey; }

}  // namespace

QString key(const Message &message) {
    return message.hint.isEmpty() ? message.source : message.source + kHintSeparator + message.hint;
}

bool readTs(const QString &path, QList<Message> *messages, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot open %1").arg(path);
        return false;
    }
    QXmlStreamReader xml(&file);
    QString context;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        if (xml.name() == QLatin1String("name")) {
            context = xml.readElementText();
        } else if (xml.name() == QLatin1String("message")) {
            Message message;
            message.context = context;
            message.plural = xml.attributes().value(QLatin1String("numerus")) == QLatin1String("yes");
            bool gone = false;
            bool unfinished = false;
            while (!(xml.isEndElement() && xml.name() == QLatin1String("message")) && !xml.atEnd()) {
                xml.readNext();
                if (!xml.isStartElement()) continue;
                if (xml.name() == QLatin1String("source")) {
                    message.source = xml.readElementText();
                } else if (xml.name() == QLatin1String("comment")) {
                    message.hint = xml.readElementText();
                } else if (xml.name() == QLatin1String("translation")) {
                    const QStringView type = xml.attributes().value(QLatin1String("type"));
                    gone = type == QLatin1String("obsolete") || type == QLatin1String("vanished");
                    unfinished = type == QLatin1String("unfinished");
                    if (!message.plural) {
                        message.forms = {xml.readElementText()};
                    } else {
                        while (!(xml.isEndElement() && xml.name() == QLatin1String("translation")) && !xml.atEnd()) {
                            xml.readNext();
                            if (xml.isStartElement() && xml.name() == QLatin1String("numerusform"))
                                message.forms.append(xml.readElementText());
                        }
                    }
                }
            }
            if (gone) continue;
            // A draft marked "needs editing" is not a translation yet.
            if (unfinished) message.forms = QStringList(qMax<qsizetype>(message.forms.size(), message.plural ? 2 : 1), QString());
            if (message.forms.isEmpty()) message.forms = QStringList(message.plural ? 2 : 1, QString());
            messages->append(message);
        }
    }
    if (xml.hasError()) {
        *error = QStringLiteral("%1: %2 (line %3)").arg(path, xml.errorString()).arg(xml.lineNumber());
        return false;
    }
    return true;
}

bool readJson(const QByteArray &json, QList<Message> *messages, QString *error) {
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError) {
        // The line helps a volunteer find a missing comma or quote.
        const int line = json.left(parse.offset).count('\n') + 1;
        *error = QStringLiteral("%1 at line %2").arg(parse.errorString()).arg(line);
        return false;
    }
    if (!document.isObject()) {
        *error = QStringLiteral("the file must be an object of contexts");
        return false;
    }
    const QJsonObject contexts = document.object();
    for (auto context = contexts.begin(); context != contexts.end(); ++context) {
        if (!context.value().isObject()) {
            *error = QStringLiteral("\"%1\" must be an object of texts").arg(context.key());
            return false;
        }
        const QJsonObject texts = context.value().toObject();
        for (auto text = texts.begin(); text != texts.end(); ++text) {
            Message message;
            message.context = context.key();
            const qsizetype separator = text.key().indexOf(kHintSeparator);
            message.source = separator < 0 ? text.key() : text.key().left(separator);
            message.hint = separator < 0 ? QString() : text.key().mid(separator + kHintSeparator.size());
            if (text.value().isString()) {
                message.forms = {text.value().toString()};
            } else if (text.value().isArray()) {
                message.plural = true;
                for (const QJsonValue &form : text.value().toArray()) {
                    if (!form.isString()) {
                        *error = QStringLiteral("\"%1\" in \"%2\": the plural forms must be texts").arg(text.key(), context.key());
                        return false;
                    }
                    message.forms.append(form.toString());
                }
            } else {
                *error = QStringLiteral("\"%1\" in \"%2\" must be a text or a list of texts").arg(text.key(), context.key());
                return false;
            }
            messages->append(message);
        }
    }
    return true;
}

bool readJsonFile(const QString &path, QList<Message> *messages, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot open %1").arg(path);
        return false;
    }
    if (readJson(file.readAll(), messages, error)) return true;
    *error = path + QStringLiteral(": ") + *error;
    return false;
}

QByteArray writeJson(const QList<Message> &messages) {
    QList<QString> order;
    QHash<QString, QList<const Message *>> byContext;
    for (const Message &message : messages) {
        if (!byContext.contains(message.context)) order.append(message.context);
        byContext[message.context].append(&message);
    }
    QByteArray out = "{\n";
    for (qsizetype c = 0; c < order.size(); ++c) {
        out += "  " + quoted(order[c]) + ": {\n";
        const QList<const Message *> &texts = byContext[order[c]];
        for (qsizetype t = 0; t < texts.size(); ++t) {
            const Message &message = *texts[t];
            out += "    " + quoted(key(message)) + ": ";
            if (message.plural) {
                QByteArrayList forms;
                for (const QString &form : message.forms) forms.append(quoted(form));
                out += "[" + forms.join(", ") + "]";
            } else {
                out += quoted(message.forms.value(0));
            }
            out += t + 1 < texts.size() ? ",\n" : "\n";
        }
        out += c + 1 < order.size() ? "  },\n" : "  }\n";
    }
    out += "}\n";
    return out;
}

QByteArray writeTs(const QList<Message> &messages, const QString &language) {
    QByteArray out;
    QXmlStreamWriter xml(&out);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeDTD(QStringLiteral("<!DOCTYPE TS>"));
    xml.writeStartElement(QStringLiteral("TS"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("2.1"));
    xml.writeAttribute(QStringLiteral("language"), language);
    QString context;
    bool open = false;
    for (const Message &message : messages) {
        if (!open || message.context != context) {
            if (open) xml.writeEndElement();
            xml.writeStartElement(QStringLiteral("context"));
            xml.writeTextElement(QStringLiteral("name"), message.context);
            context = message.context;
            open = true;
        }
        xml.writeStartElement(QStringLiteral("message"));
        if (message.plural) xml.writeAttribute(QStringLiteral("numerus"), QStringLiteral("yes"));
        xml.writeTextElement(QStringLiteral("source"), message.source);
        if (!message.hint.isEmpty()) xml.writeTextElement(QStringLiteral("comment"), message.hint);
        xml.writeStartElement(QStringLiteral("translation"));
        if (!done(message)) xml.writeAttribute(QStringLiteral("type"), QStringLiteral("unfinished"));
        if (message.plural) {
            for (const QString &form : message.forms) xml.writeTextElement(QStringLiteral("numerusform"), form);
        } else {
            xml.writeCharacters(message.forms.value(0));
        }
        xml.writeEndElement();  // translation
        xml.writeEndElement();  // message
    }
    if (open) xml.writeEndElement();
    xml.writeEndElement();  // TS
    xml.writeEndDocument();
    return out;
}

QList<Message> merge(const QList<Message> &code, const QList<Message> &translated) {
    QHash<QString, const Message *> known;
    for (const Message &message : translated) known.insert(lookupKey(message.context, key(message)), &message);
    QList<Message> result;
    for (Message message : code) {
        const Message *old = known.value(lookupKey(message.context, key(message)));
        if (old && old->plural == message.plural) message.forms = old->forms;
        else message.forms = QStringList(message.plural ? 2 : 1, QString());
        result.append(message);
    }
    return result;
}

Completeness completeness(const QList<Message> &messages) {
    Completeness result;
    for (const Message &message : messages) {
        ++result.total;
        if (done(message)) ++result.done;
    }
    return result;
}

bool offered(const Completeness &completeness, int thresholdPercent) {
    return completeness.total > 0 && completeness.done * 100 >= thresholdPercent * completeness.total;
}

}  // namespace TranslationFiles
