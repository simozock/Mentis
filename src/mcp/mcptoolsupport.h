#pragma once

// FEAT-045: what the files of the agents' tools share (argument checks, paths, schemas). Internal to
// src/mcp: mcpmaptools.cpp defines it, mcpcontenttools.cpp uses it.

#include "mcpmaptools.h"

#include <QJsonObject>
#include <QStringList>
#include <optional>

struct MindNode;
class Document;

namespace McpMapTools::Support {

using ToolResult = McpServer::ToolResult;

// A string argument; nullopt with error set when it is missing (required), not a string or too long.
std::optional<QString> text(const QJsonObject &args, const QString &key, bool required, QString *error,
                            int maxLength = kMaxTextLength);
std::optional<int> index(const QJsonObject &args, QString *error);  // "index", whole and from 0
std::optional<QStringList> labels(const QJsonObject &args, QString *error);
std::optional<QString> existingFile(const QString &raw, QString *error);  // its canonical path, or nullopt
MindNode *topic(MindMapModel &model, const QJsonObject &args, const QString &key, QString *error);
// Several topics from an array of ids ("ids"); empty with error set if one is missing.
QList<MindNode *> topics(MindMapModel &model, const QJsonObject &args, const QString &key, QString *error);

QJsonObject stringProperty(const QString &description);
QJsonObject schema(const QJsonObject &properties, const QStringList &required);
QJsonObject idsProperty(const QString &description);
// FEAT-045: an operation that needs the user's explicit yes (delete a sheet, replace a file). True =
// go on; false with error set (the user said no, or nobody could ask and confirm was not true).
bool confirmed(const Access &access, const QJsonObject &args, const QString &kind, const QString &subject, QString *error);
extern const QJsonObject kConfirm;  // the "confirm" argument's schema
extern const QJsonObject kSheet;

// Stage A of phase 3 (mcpcontenttools.cpp): style and node contents, and the list of their values.
void appendContentEdits(QList<MapEdit> &edits);
McpServer::Tool optionsTool(std::function<Access()> access);
// Stage B (mcpelementtools.cpp): relationships, summaries, boundaries, floating topics.
void appendElementEdits(QList<MapEdit> &edits);
QJsonObject elementsJson(const MindMapModel &model);  // read_map's lists of them
// Stage C (mcpsheettools.cpp): sheets and the map's structure; the structure keys for list_options.
void appendSheetEdits(QList<MapEdit> &edits);
QJsonObject structureOptions();
// Stage D (mcpgantttools.cpp): the sheet's Gantt plan, dependencies and people.
void appendGanttEdits(QList<MapEdit> &edits);
QList<McpServer::Tool> currencyTools(std::function<Access()> access);  // the currencies saved on this computer
// Stage E (mcpexporttools.cpp): exports, Pitch, and replacing files after the user's yes.
void appendExportEdits(QList<MapEdit> &edits);
QJsonObject pitchOptions();
// A file to write from args[key]: absolute, one of suffixes, its folder existing; exists
// tells whether it is there already (replace it only after confirmed()).
std::optional<QString> outputPath(const QJsonObject &args, const QString &key, const QStringList &suffixes,
                                  bool *exists, QString *error);
bool fileInUse(const QString &path);  // a .mentis held by a Mentis window (its lock file)
// write(target) with the old target moved aside first and removed only when write succeeds;
// on failure the old file is put back.
// The sticker and marker library (mcplibrarytools.cpp), and a library marker as a topic's marker id
// with its image.
QList<McpServer::Tool> libraryTools(std::function<Access()> access);
std::optional<std::pair<QString, QByteArray>> libraryMarker(const QString &path);
bool inStickerLibrary(const QString &path);  // an own or built-in sticker of the library
// Colours and text (mcpthemetools.cpp): own palettes, the sheet's theme and style, labels renamed
// everywhere, find and replace; formatted titles and notes from Markdown.
QList<McpServer::Tool> paletteTools(std::function<Access()> access);
// FEAT-089 (mcppitchtemplatetools.cpp): the Pitch templates of the app's library.
QList<McpServer::Tool> pitchTemplateTools(std::function<Access()> access);
void appendThemeEdits(QList<MapEdit> &edits);
bool setMarkdownTitle(Document &document, MindNode *node, const QString &markdown);
bool setMarkdownNotes(Document &document, MindNode *node, const QString &markdown);
bool replaceWith(const QString &target, const std::function<bool(const QString &path, QString *error)> &write, QString *error);

}  // namespace McpMapTools::Support
