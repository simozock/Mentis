#pragma once

// FEAT-051 (docs/features/FEAT-051-adesivi-e-marcatori.md): marcatori, piccole icone di stato nel
// posto a sinistra del nodo (prima della casella di spunta). Un nodo ha al massimo un
// marcatore per serie; si salvano come "serie:valore" (MindNode::markers, chiave "markers").
// Le icone si disegnano con QPainter (nessun file): restano nitide a ogni zoom.
//
// Tappa 2 (2026-10-20): marcatori propri presi da un file (SVG o PNG), "custom:<gruppo>:<sha256>".
// Ogni gruppo proprio e' una serie (un marcatore per gruppo nel nodo). L'immagine sta nel documento
// (MindMapModel::markerImages) e qui in un registro per contenuto, cosi' il disegno non dipende dal
// file d'origine.

#include <QRectF>
#include <QString>
#include <QStringList>

class QPainter;

namespace Markers {

struct Series {
    QString id;           // "priority", "progress", "flag", "symbol"
    QStringList values;   // nell'ordine del clic che li fa scorrere
};
const QList<Series> &series();
QString seriesOf(const QString &marker);           // "priority:3" -> "priority"; "custom:g:sha" -> "custom:g"
bool isCustom(const QString &marker);
QString customImage(const QString &marker);        // sha256 dell'immagine di un marcatore proprio
QString customMarker(const QString &group, const QString &sha);
// Registro delle immagini dei marcatori propri, per contenuto (sha256 esadecimale).
void registerImage(const QString &sha, const QByteArray &data);
QByteArray image(const QString &sha);
// Nome dei marcatori propri (per suggerimenti e menu), dato da chi conosce la raccolta.
void setCustomLabel(const QString &sha, const QString &label);
bool isValid(const QString &marker);
// Valore dopo marker nella sua serie (l'ultimo torna al primo): clic sul marcatore del nodo.
QString next(const QString &marker);
// Elenco con marker al posto di quello della stessa serie (vuoto = tolto), nell'ordine delle serie.
QStringList with(const QStringList &markers, const QString &series, const QString &marker);
// Nome leggibile (per i suggerimenti), gia' tradotto.
QString label(const QString &marker, bool translated = true);  // false = English
QString seriesLabel(const QString &series, bool translated = true);  // false = English
// Icona del marcatore dentro rect (quadrato).
void paint(QPainter *painter, const QRectF &rect, const QString &marker);

}  // namespace Markers
