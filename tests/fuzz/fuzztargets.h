#pragma once

#include <QByteArray>
#include <QString>

// Fuzzing (2026-11-09, user's request): the places where Mentis reads data someone else may have
// written. Each target takes any bytes and must neither crash nor hang; what it returns does not
// matter. They are run by tests/fuzz_test.cpp (seeded mutations, every build) and by the libFuzzer
// drivers in tests/fuzz/ (CI on Linux, clang). A QApplication must exist.
namespace FuzzTargets {

// Markdown imported or pasted: MarkdownImport::parse, then a whole map from it.
void markdown(const QByteArray &data);
// Rich text of a title or a note from a document: RichResources::withoutExternal, then laid out.
void richText(const QByteArray &data);
// A map as JSON (clipboard, history, file payloads): MindMapModel::fromJson, then written back.
void mapJson(const QByteArray &data);
// A .mentis built from that JSON and opened again (BmapStorage), in folder.
void document(const QByteArray &data, const QString &folder);

}  // namespace FuzzTargets
