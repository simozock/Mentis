#pragma once

#include <QString>

// Profili di utilizzo (2026-10-14, richiesta dell'utente): "Standard" per i PC attuali,
// "Leggero" per hardware datato o limitato. "Automatico" sceglie all'avvio in base ai core
// logici della CPU (QThread::idealThreadCount, https://doc.qt.io/qt-6/qthread.html#idealThreadCount).
// Valori e motivazioni in AGENTS.md, "Profili di prestazioni".
namespace Performance {
enum class Mode { Automatic, Standard, Light };
enum class Profile { Standard, Light };

Mode mode();                    // scelta salvata in QSettings ("performance/mode")
void setMode(Mode mode);
Profile profile();              // profilo effettivo (Automatico risolto)
QString profileName(Profile profile);

// Ritardo del salvataggio automatico dopo l'ultima modifica: su dischi lenti meno scritture.
int autoSaveDelayMs();
// Sotto questo zoom i nodi si disegnano senza testo (NodeItem::paint).
double detailZoomThreshold();
// Risoluzione delle immagini dei nodi rispetto alla misura mostrata (NodeItem, loadScaledPixmap).
double imageScaleFactor();
// Dimensione della QPixmapCache in KB (predefinito di Qt: 10240).
int pixmapCacheKb();
// Interpolazione bilineare delle immagini nella vista della mappa.
bool smoothPixmaps();
// Animazioni decorative (es. lampeggio della select dei fogli).
bool animationsEnabled();
// Anteprime della Home generate caricando la mappa quando il file non ne contiene una.
bool generateMissingPreviews();
// Passi di annullamento tenuti in RAM per finestra (gli altri restano nel giornale SQLite).
int historyCacheSteps();
}
