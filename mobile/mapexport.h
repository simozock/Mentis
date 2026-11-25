#pragma once

#include <QObject>
#include <QVariantMap>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

class Document;
class MapLibrary;
struct GanttExportSource;

// FEAT-120 stage C: Export in the Home's map row menu, the desktop's Export menu on the phone. The
// map is opened on its own (its lock taken, nothing saved), the sheet chosen, the file written by
// the core as the desktop writes it (PitchExport, Document::exportOffice, Document::exportMarkdown)
// into exportFolder and handed to Android's Share. Markdown, a folder on the desktop (the .md and
// assets/), is shared as one .zip of that folder (user's decision 2026-11-14). Stage C2: Image and
// PDF with the options of the desktop's dialogs, the map drawn by the phone (MapScene::paint) into
// MapPages' files, as the desktop's MapExporter writes them. FEAT-122 stage E: a sheet's Gantt in
// every format (GanttPages, GanttOffice), its columns the desktop's default ones.
class MapExport : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MapLibrary *library READ library WRITE setLibrary NOTIFY libraryChanged)
    Q_PROPERTY(QStringList sheets READ sheets NOTIFY preparedChanged)
    Q_PROPERTY(bool prepared READ prepared NOTIFY preparedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    // The desktop's Export entries done on the phone (stage C1), in its order.
    enum Format { Word, Excel, PitchPowerPoint, PitchPdf, Markdown };
    Q_ENUM(Format)

    explicit MapExport(QObject *parent = nullptr);
    ~MapExport() override;
    MapLibrary *library() const { return m_library; }
    void setLibrary(MapLibrary *library);
    QStringList sheets() const;
    bool prepared() const;
    QString lastError() const { return m_lastError; }

    // Opens the map at url for exporting (its sheets' names in sheets); false with lastError.
    Q_INVOKABLE bool prepare(const QUrl &url);
    // Writes format of sheet (Word and Excel: every sheet, as the desktop) and opens Share; false
    // with the desktop's message in lastError. The file's path in lastFile (for tests).
    Q_INVOKABLE bool run(int format, int sheet);
    // C2, the desktop's Export image: {sheet (-1 = all sheets), svg, background, scale 1-3, frame}.
    // All sheets write one file per sheet (MapPages::pagePaths), shared as one .zip. FEAT-122 E:
    // {gantt: true, sheet, svg, scale, content (GanttPages::Content), unit (GanttCanvas::Scale), frame}.
    Q_INVOKABLE bool runImage(const QVariantMap &options);
    // C2, the desktop's Export PDF: {pitch, sheet (-1 = all), pageSize (MapPages::PageSize), landscape,
    // actualSize, background, monochrome, frame, outliner}; Pitch: the sheet's slides with the frame;
    // FEAT-121 C, outliner: the outline on paper (Fit to map gives A4, as MapPages). FEAT-122 E:
    // {gantt: true, sheet, pageSize, landscape, actualSize (else fit width), content, unit, monochrome, frame}.
    Q_INVOKABLE bool runPdf(const QVariantMap &options);
    // FEAT-122 E: whether the sheet's Gantt has tasks (its Gantt can be exported).
    Q_INVOKABLE bool hasGantt(int sheet);
    // What fit width did to the Gantt's PDF (PageDialog's note): another unit, the timeline over
    // pages in width; empty otherwise. Options as runPdf's.
    Q_INVOKABLE QString ganttNote(const QVariantMap &options);
    // OfficeExportDialog's Gantt: format Word, Excel or Markdown, the sheet's Gantt with content and
    // unit (Word, Excel), one file shared.
    Q_INVOKABLE bool runGanttFile(int format, int sheet, int content, int unit);
    // frame: {style (ExportFrame::Style), thickness, color, rounded}; the styles' names from Solid on.
    Q_INVOKABLE QStringList frameStyleNames() const;
    Q_INVOKABLE int currentSheet() const;
    // Closes the map (nothing saved).
    Q_INVOKABLE void finish();
    QString lastFile() const { return m_lastFile; }
    // Where the exports are written: in the app's files, which Android shares; emptied at each export.
    static QString exportFolder();

signals:
    void libraryChanged();
    void preparedChanged();
    void lastErrorChanged();

private:
    void setError(const QString &error);
    // The file written at path, or empty with lastError.
    QString write(int format, const QString &folder, const QString &name);
    // The export folder emptied and made, the name of one sheet's / every sheet's export; false with lastError.
    bool startExport(int sheet, QString *name);
    bool share(const QString &file);
    // The sheet shown and its Gantt's source; false with lastError when the sheet cannot be read.
    bool ganttSource(int sheet, GanttExportSource *source);
    MapLibrary *m_library = nullptr;
    std::unique_ptr<Document> m_document;
    QString m_copy;  // a copy localCopy made for this export, removed by finish
    QString m_lastError;
    QString m_lastFile;
};
