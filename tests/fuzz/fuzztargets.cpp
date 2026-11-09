#include "fuzztargets.h"

#include "bmapstorage.h"
#include "markdownimport.h"
#include "mindmapmodel.h"
#include "richresources.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextDocument>

namespace FuzzTargets {

void markdown(const QByteArray &data) {
    const QString text = QString::fromUtf8(data);
    MarkdownImport::parse(text, MarkdownImport::kMaxPastedItems);
    MindMapModel model;
    if (model.fromMarkdown(text)) model.toMarkdown(true);
}

void richText(const QByteArray &data) {
    const QString clean = RichResources::withoutExternal(QString::fromUtf8(data));
    QTextDocument document;
    document.setHtml(clean);
    document.idealWidth();  // the layout reads the images and the style sheets
}

void mapJson(const QByteArray &data) {
    const QJsonObject json = QJsonDocument::fromJson(data).object();
    MindMapModel model;
    if (!model.fromJson(json)) return;
    model.toJson();
    model.toMarkdown(true);
    MindMapModel again;
    again.fromFlatSnapshot(model.toFlatSnapshot());
}

void document(const QByteArray &data, const QString &folder) {
    const QJsonObject json = QJsonDocument::fromJson(data).object();
    const QString path = folder + QStringLiteral("/fuzz.mentis");
    QFile::remove(path);
    if (!BmapStorage::saveDocument(path, json, BmapStorage::SaveReason::Explicit)) return;
    MindMapModel model;
    if (BmapStorage::load(path, model)) model.toJson();
}

}  // namespace FuzzTargets
