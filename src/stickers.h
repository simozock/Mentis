#pragma once

// FEAT-051 (docs/features/FEAT-051-adesivi-e-marcatori.md): adesivi (sticker). Un adesivo e'
// un'immagine del nodo (stesso posto, misura e maniglie): chi lo sceglie lo mette con
// Document::setSticker. Le raccolte (di base nelle risorse ":/stickers/", proprie nella cartella
// dati dell'app) sono in IconLibrary e si scelgono dal pannello laterale (IconPage).

#include <QImage>
#include <QSizeF>
#include <QString>

namespace Stickers {

// Misura d'inserimento: lato lungo 120 px (gli adesivi sono piccoli), proporzioni del file. Le
// immagini invece sono larghe come il nodo e almeno 200 px (MainWindow::fittedImageSize).
QSizeF insertionSize(const QString &path);
// Contorno (richiesta dell'utente 2026-10-20): anello bianco attorno al
// disegno e, fuori, un filo scuro; cosi' l'adesivo si stacca sia dagli sfondi scuri sia da quelli
// chiari. Stessa misura dell'immagine: il disegno si riduce quanto serve all'anello. Gli spessori
// seguono il lato (kOutlineWhite e kOutlineDark del lato lungo).
QImage outlined(const QImage &source);
constexpr double kOutlineWhite = 0.03;
constexpr double kOutlineDark = 0.012;

}  // namespace Stickers
