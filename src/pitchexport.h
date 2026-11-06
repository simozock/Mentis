#pragma once

// FEAT-063 stage C (docs/features/FEAT-063-pitch-presentazione.md, "Tappa C"): the presentation as a
// PowerPoint file (.pptx) or a PDF. No widgets.

#include "exportframe.h"
#include "pitch.h"

#include <QByteArray>
#include <QList>
#include <QString>

class QPainter;

namespace PitchExport {

// The slide's ratio for a file: Auto has no screen, so it is 16:9.
qreal fileRatio(Pitch::Aspect aspect);

// The .pptx package (OOXML, ECMA-376): per slide a picture of all but the text, the titles and items
// as editable text boxes, the items "one at a time" in their own picture appearing with their text
// on click, the topic's notes as speaker notes. title: the document's title (its properties).
QByteArray pptx(const QList<Pitch::Slide> &slides, const Pitch::Look &look, qreal ratio, const QString &title);

// Writes the presentation to file; empty = written, else the error (no half file is left).
QString writePptx(const QString &file, const QList<Pitch::Slide> &slides, const Pitch::Look &look, qreal ratio, const QString &title);
// One page per slide, complete, drawn as vectors (selectable text).
// FEAT-084: frame on each page's edge, the slide inside it.
QString writePdf(const QString &file, const QList<Pitch::Slide> &slides, const Pitch::Look &look, qreal ratio, const QString &title,
                 const ExportFrame &frame = {});
// The slide on a page of rect, inside the frame (a white band and the frame around it when on).
void paintFramed(QPainter *painter, const QRectF &rect, const Pitch::Slide &slide, const Pitch::Look &look, const ExportFrame &frame);

}  // namespace PitchExport
