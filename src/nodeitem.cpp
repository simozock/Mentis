#include "nodeitem.h"
#include "numbering.h"
#include "summary.h"
#include "uitheme.h"
#include "latexrenderer.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "stickers.h"
#include "nodeshape.h"
#include "nodesketch.h"
#include "richtitle.h"
#include "performance.h"
#include "stylecontrols.h"
#include "spellchecker.h"
#include "texteditmenu.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QDateTime>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsTextItem>
#include "markdownshortcuts.h"
#include "markers.h"
#include <QGraphicsSceneContextMenuEvent>
#include <QAbstractTextDocumentLayout>
#include <QKeyEvent>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QSet>
#include <functional>
#include <QImageReader>
#include <QGraphicsScene>
#include <QGraphicsProxyWidget>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QComboBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QMenu>
#include <QInputDialog>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QIcon>
#include <QPointer>
#include <QElapsedTimer>
#include <QStyleOptionGraphicsItem>
#include <QStyledItemDelegate>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QToolButton>
#include <QWidgetAction>
#include <QListWidget>
#include <QStandardItemModel>
#include <QScreen>
#include <QVBoxLayout>
#include <algorithm>

namespace {
// FEAT-027: icona Hugeicons di un collegamento nel colore dato, preparata una volta per colore
// (QPixmapCache, poche icone da 48 px) e disegnata a 16 px: resta nitida fino allo zoom 300%.
QPixmap tintedIconPixmap(const QString &path, const QColor &color) {
    const QString key = QStringLiteral("mentis-icon-%1-%2").arg(path).arg(color.rgba(), 8, 16, QLatin1Char('0'));
    QPixmap pixmap;
    if (QPixmapCache::find(key, &pixmap)) return pixmap;
    pixmap = QIcon(path).pixmap(QSize(48, 48));
    QPainter tint(&pixmap);
    tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tint.fillRect(pixmap.rect(), color);
    tint.end();
    QPixmapCache::insert(key, pixmap);
    return pixmap;
}

QPixmap linkIconPixmap(bool topic, const QColor &color) {
    return tintedIconPixmap(topic ? QStringLiteral(":/assets/hugeicons/link.svg") : QStringLiteral(":/assets/hugeicons/link-web.svg"), color);
}

bool hasNote(const MindNode *node) { return !node->notes.trimmed().isEmpty(); }  // FEAT-059
}  // namespace

namespace {
// Carica un'immagine già ridotta a una risoluzione adatta alla dimensione mostrata.
// - QImageReader::setScaledSize decodifica direttamente alla dimensione ridotta
//   (per JPEG evita di allocare l'immagine a piena risoluzione): una foto 4000x3000
//   occupa ~46 MB in RAM, la versione ridotta qualche centinaio di KB.
// - Il fattore 2 mantiene l'immagine nitida fino al 200% di zoom; oltre si vede
//   un leggero ammorbidimento, compromesso scelto a favore della memoria.
// - QPixmapCache condivide la stessa immagine tra più nodi e tra ricostruzioni
//   della scena, evitando di rileggere il file dal disco.
QPixmap loadScaledPixmap(const QString &path, const QSizeF &displaySize, qreal factor, bool outline = false) {
    const QFileInfo info(path);
    // 2026-10-14: fattore 2 con il profilo Standard, 1 con quello Leggero (metà dei pixel per lato,
    // un quarto della memoria): Performance::imageScaleFactor. FEAT-054: the export scale on an export scene.
    const QSize target = (displaySize * factor).toSize().expandedTo(QSize(1, 1));
    const QString key = QStringLiteral("bc-image:%1:%2x%3:%4").arg(info.absoluteFilePath())
                            .arg(target.width()).arg(target.height())
                            .arg(info.lastModified().toMSecsSinceEpoch())
                        + (outline ? QStringLiteral(":outline") : QString());  // FEAT-051
    QPixmap pixmap;
    if (QPixmapCache::find(key, &pixmap)) return pixmap;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize source = reader.size();
    // Non ingrandisce mai oltre la risoluzione originale del file.
    if (source.isValid() && (source.width() > target.width() || source.height() > target.height()))
        reader.setScaledSize(target);
    QImage image = reader.read();
    if (outline) image = Stickers::outlined(image);
    pixmap = QPixmap::fromImage(image);
    if (!pixmap.isNull()) QPixmapCache::insert(key, pixmap);
    return pixmap;
}
}

NodeItem::NodeItem(MindNode *node, QGraphicsItem *parent) : QGraphicsObject(parent), m_node(node) {
    setFlags(ItemIsSelectable | ItemSendsGeometryChanges | ItemIsFocusable);
    setFlag(ItemIsMovable, !summaryTopicOf(node));  // FEAT-052: a summary topic and its branch follow the bracket
    setAcceptHoverEvents(true);  // cursore a I sopra il titolo
    setPos(node->position);
    // Prima: m_image.load(node->imagePath) leggeva il file a piena risoluzione a ogni
    // creazione dell'item. Ora passa dalla cache con immagine ridotta (vedi loadImage).
    // Il pixmap viene decodificato solo al primo paint: i nodi fuori dalla viewport
    // non consumano I/O e memoria durante l'apertura del documento.
    m_loadedImagePath = node->imagePath;
    m_loadedImageSize = node->imageSize;
    m_hasImage = !node->imagePath.isEmpty() || !node->formula.isEmpty();  // B9: misure note subito, pixel caricati dopo
    updateFormula();
    m_geometryKey = geometryKey();  // P2
    // Bug 2026-11-02 (manual pictures): only syncFromNode laid these out, so a map just opened showed
    // no label pills, and no notes with "Show all notes", until the topic changed.
    updateNotePreview();  // FEAT-059
    updateLabelLayout();  // FEAT-060
}

QSizeF NodeItem::formulaInsertionSize(const MindNode *node) {
    return node ? FormulaText::boxSize(node->formula, titleFont(node)) : QSizeF();
}

bool NodeItem::stickerOutline() const { return m_node->imageSticker && m_node->imageStyle.outline; }

void NodeItem::loadImage() {
    m_loadedImagePath = m_node->imagePath;
    m_loadedImageSize = m_node->imageSize;
    // FEAT-064: la formula si disegna come un'immagine nel suo riquadro (doppia risoluzione: resta
    // nitida anche ingrandita con lo zoom della mappa).
    m_loadedFormulaKey = m_node->formula.isEmpty() ? QString()
        : m_node->formula + QChar(0x1f) + m_node->formulaBackground.name(QColor::HexArgb) + QChar(0x1f) + m_node->text.name();
    // Rotazione (richiesta dell'utente 2026-10-20): i pixel si leggono diritti, alla misura non
    // ruotata, poi si girano; imageSize e' gia' quella ruotata.
    m_loadedRotation = m_node->imageStyle.rotation;
    const QSizeF upright = m_loadedRotation % 180 ? m_node->imageSize.transposed() : m_node->imageSize;
    const auto rotated = [this](const QPixmap &pixmap) {
        return m_loadedRotation == 0 || pixmap.isNull() ? pixmap : pixmap.transformed(QTransform().rotate(m_loadedRotation), Qt::SmoothTransformation);
    };
    auto *mapScene = qobject_cast<MindMapScene *>(scene());
    const qreal factor = mapScene && mapScene->exportImageScale() > 0 ? mapScene->exportImageScale() : Performance::imageScaleFactor();
    if (!m_node->formula.isEmpty()) {
        const QColor text = FormulaText::textColorOn(m_node->formulaBackground, m_node->text);
        m_image = rotated(QPixmap::fromImage(FormulaText::boxImage(m_node->formula, titleFont(m_node), m_node->formulaBackground, text,
                                                                   upright, 2.0 * factor)));
        m_imageLoadPending = false;
        return;
    }
    // FEAT-037: la miniatura incorporata e' di 512 px al massimo; un'immagine mostrata piu' grande
    // si decodifica dall'originale (estratto dal file nella cache una sola volta, al primo disegno,
    // come gia' la miniatura), cosi' resta nitida. loadScaledPixmap non va mai oltre i suoi pixel.
    QString path = m_node->imagePath;
    const QSizeF shown = m_node->imageSize * factor;
    if (!m_node->imageAssetId.isEmpty() && (shown.width() > kThumbnailPx || shown.height() > kThumbnailPx))
        if (mapScene && mapScene->originalImagePath)
            if (const QString original = mapScene->originalImagePath(m_node); !original.isEmpty()) path = original;
    m_loadedOutline = stickerOutline();
    m_image = path.isEmpty() ? QPixmap() : rotated(loadScaledPixmap(path, upright, factor, m_loadedOutline));
    m_imageLoadPending = false;
}

namespace {
// FEAT-020: con bordi piu' spessi (fino a 6 px) il bordo esce di meta' spessore. Without a border
// nothing sticks out: the ring is "gap" px from the shape, 1 px (measured live, FEAT-072; before
// 1 px more). Il tratto a mano libera (parte C) si
// scosta ancora di NodeSketch::kBorderWobble: l'anello e l'area del nodo lo tengono dentro. Lo
// stesso per le punte dei motivi (NodeSketch::kPatternReach).
qreal borderOutside(const MindNode *node) {
    qreal outside = 0.0;
    if (node->borderStyle != NodeBorderStyle::Nessuno) {
        const qreal wobble = NodeSketch::isFreehand(node->borderStyle) ? NodeSketch::kBorderWobble : 0;
        // FEAT-030 stage B: round and curly brackets reach above and below the rectangle.
        outside = std::max<qreal>(1.0, NodeShapes::markOverhang(node->shape) + node->borderWidth / 2 + wobble);
    }
    // Le punte dei motivi escono dalla forma (foto 24): anche loro restano dentro l'anello.
    if (NodeStyle::isPatternFill(node->fillStyle) && !NodeShapes::isUnderline(node->shape))
        outside = std::max(outside, NodeSketch::kPatternReach);
    return outside;
}
}

NodeItem::GeometryKey NodeItem::geometryKey() const {
    // Il colore del testo conta solo per le formule, gia' disegnate nel loro colore da updateFormula();
    // testo semplice e formattato prendono il colore in paint(). Prima ogni cambio di colore del
    // testo rimisurava il nodo e ridisponeva la mappa (FEAT-020 parte C: un motivo scurisce il
    // testo di 1.000 nodi).
    const QRgb formulaColor = FormulaText::containsMath(m_node->title) ? m_node->text.rgba() : 0;
    return {{shownTitle(), m_node->titleHtml, formulaColor, !m_node->parent, layoutCustomWidth(), m_node->strikeOut,
             m_node->fontFamily, m_node->fontSize, m_node->fontWeight, static_cast<int>(m_node->textCase),
             static_cast<int>(m_node->textAlign), m_node->italic, m_node->underline,
             int(!m_node->topicLink.isEmpty()) + int(!m_node->webLink.isEmpty()) + int(hasNote(m_node) && !showAllNotes())
                 + int(!m_node->files.isEmpty()),
             hasSideImage() ? m_node->imageSize.width() : 0.0, int(m_node->task != MindNode::Task::None) + int(m_node->markers.size())},
            m_node->imagePath, m_node->imageSize,
            static_cast<int>(m_node->imagePlacement), borderOutside(m_node), static_cast<int>(m_node->shape),
            m_node->borderWidth, showAllNotes() && hasNote(m_node),
            showAllNotes() ? m_node->notes : QString(), showAllNotes() ? m_node->notesHtml : QString(), m_node->labels,
            m_node->formula};
}

QString NodeItem::shownTitle() const { return Numbering::withNumber(m_node->number, m_node->title); }

void NodeItem::prependNumber(QTextDocument &document) const {
    if (m_node->number.isEmpty()) return;
    // FEAT-024: in the format of the title's first character, like the rest of its first word.
    QTextCursor cursor(&document);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    const QTextCharFormat format = cursor.charFormat();
    cursor.setPosition(0);
    cursor.insertText(m_node->title.isEmpty() ? m_node->number : m_node->number + u' ', format);
}

void NodeItem::setStackZ(qreal z) {
    if (z == m_stackZ) return;
    m_stackZ = z;
    if (!m_editor) setZValue(restingZ());
}

qreal NodeItem::restingZ() const { return m_stackZ - (m_cell && m_cell->holdsBranch ? kHoldingCellDepth : 0); }

void NodeItem::syncFromNode() {
    // prepareGeometryChange è obbligatorio prima di cambiare boundingRect (dimensione
    // immagine, allegato, testo vuoto), altrimenti la scena conserva l'area vecchia.
    // P2 (2026-10-13): misura e impaginazione si rifanno solo se cambia qualcosa da cui
    // dipendono (vedi GeometryKey); colori, note e posizione si aggiornano sempre sotto.
    // FEAT-020 parte B: forma, bordo, immagine e allegato spostano solo il riquadro, quindi il testo
    // si reimpagina solo se cambia TextLayoutKey (con 1.000 nodi circa 10 ms in meno al cambio di forma).
    // borderOutside sposta solo l'anello e l'area di disegno, non la misura usata dal layout
    // (normalBoundingRect, shapeRect): un bordo a mano libera o piu' spesso non ridispone la mappa.
    const GeometryKey key = geometryKey();
    GeometryKey sameRing = key;
    sameRing.borderOutside = m_geometryKey.borderOutside;
    m_lastSyncChangedGeometry = !(sameRing == m_geometryKey);
    if (!(key == m_geometryKey)) {
        // boundingRect() reads the node, which has already changed, so Qt only repaints the new area:
        // the old one is repainted here (bug 2026-10-24: part of an oval central topic stayed on screen
        // after switching to a rectangle until the next click).
        if (scene()) scene()->update(mapRectToScene(m_syncedBounds));
        prepareGeometryChange();
        if (!(key.text == m_geometryKey.text)) updateFormula();
        m_geometryKey = key;
    }
    const QString formulaKey = m_node->formula.isEmpty() ? QString()
        : m_node->formula + QChar(0x1f) + m_node->formulaBackground.name(QColor::HexArgb) + QChar(0x1f) + m_node->text.name();
    if (m_loadedImagePath != m_node->imagePath || m_loadedImageSize != m_node->imageSize || m_loadedFormulaKey != formulaKey
        || m_loadedOutline != stickerOutline() || m_loadedRotation != m_node->imageStyle.rotation) {
        m_loadedImagePath = m_node->imagePath;
        m_loadedImageSize = m_node->imageSize;
        m_image = QPixmap();
        m_imageLoadPending = true;
        m_hasImage = !m_node->imagePath.isEmpty() || !m_node->formula.isEmpty();  // B9 (prepareGeometryChange è già sopra)
        if (!m_hasImage) setImageSelectedState(false);  // FEAT-040: immagine tolta (anche con Ctrl+Z)
    }
    if (pos() != m_node->position) setPos(m_node->position);
    updateNotePreview();  // FEAT-059
    updateLabelLayout();  // FEAT-060
    m_syncedBounds = boundingRect();
    update();
}

qreal NodeItem::layoutCustomWidth() const { return std::max(m_node->customWidth, m_justifiedWidth); }

void NodeItem::setJustifiedWidth(qreal width) {
    if (std::abs(width - m_justifiedWidth) < 0.01) return;
    if (m_justifiedWidth <= 0) rememberNaturalWidth();  // only nodes that get justified pay for it
    m_justifiedWidth = width;
    syncFromNode();
}

void NodeItem::rememberNaturalWidth() {
    m_naturalWidth = textRect().width();
    m_naturalKey = m_geometryKey;
}

qreal NodeItem::naturalWidth() {
    if (m_justifiedWidth <= 0) return textRect().width();
    // The remembered width holds while the geometry without justification is the same; otherwise
    // the node is laid out once without it, measured, and laid out again with it.
    const qreal justified = std::exchange(m_justifiedWidth, 0.0);
    const bool fresh = m_naturalKey && geometryKey() == *m_naturalKey;
    if (!fresh) {
        syncFromNode();
        rememberNaturalWidth();
    }
    m_justifiedWidth = justified;
    if (!fresh) syncFromNode();
    return m_naturalWidth;
}

namespace {
constexpr qreal kLabelHeightPx = 20;
constexpr qreal kLabelPaddingPx = 8;
constexpr qreal kLabelGapPx = 4;
constexpr qreal kLabelTopGapPx = 4;      // sotto la forma
constexpr qreal kLabelMinWidthPx = 160;  // larghezza minima a cui vanno a capo
constexpr int kLabelFontPx = 12;

QFont labelFont() {
    QFont font;
    font.setPixelSize(kLabelFontPx);
    return font;
}
}  // namespace

// FEAT-060 (prova dell'utente 2026-10-19: testo un po' basso): centrato sull'altezza delle
// maiuscole, non su ascent + descent del font (che spinge in basso le lettere).
void drawLabelText(QPainter *p, const QRectF &pill, const QString &label, qreal padding) {
    const QFontMetricsF metrics(p->font());
    const QString text = metrics.elidedText(label, Qt::ElideRight, pill.width() - 2 * padding);
    const qreal baseline = pill.center().y() + metrics.capHeight() / 2;
    p->drawText(QPointF(pill.center().x() - metrics.horizontalAdvance(text) / 2, baseline), text);
}

// FEAT-060: pillole delle etichette in righe centrate, a capo alla larghezza del nodo.
void NodeItem::updateLabelLayout() {
    LabelLayout next;
    next.labels = m_node->labels;
    next.width = std::max(shapeRect().width(), kLabelMinWidthPx);
    if (next == m_labelLayout) return;
    // Senza etichette ne' prima ne' dopo la geometria non cambia (solo la larghezza di riserva).
    if (!next.labels.isEmpty() || !m_labelLayout.labels.isEmpty()) prepareGeometryChange();
    if (!next.labels.isEmpty()) {
        const QFontMetricsF metrics(labelFont());
        QList<QList<QRectF>> rows(1);
        qreal x = 0;
        for (const QString &label : next.labels) {
            const qreal width = std::min(next.width, std::ceil(metrics.horizontalAdvance(label)) + 2 * kLabelPaddingPx);
            if (x > 0 && x + width > next.width) { rows.append(QList<QRectF>()); x = 0; }
            rows.back().append(QRectF(x, (rows.size() - 1) * (kLabelHeightPx + kLabelGapPx), width, kLabelHeightPx));
            x += width + kLabelGapPx;
        }
        for (const QList<QRectF> &row : rows) {
            const qreal rowWidth = row.back().right() - row.front().left();
            const qreal shift = (next.width - rowWidth) / 2;  // ogni riga centrata
            for (const QRectF &pill : row) next.pills.append(pill.translated(shift, 0));
        }
        next.size = {next.width, rows.size() * kLabelHeightPx + (rows.size() - 1) * kLabelGapPx};
    }
    m_labelLayout = next;
}

qreal NodeItem::labelsBlockHeight() const {
    return m_labelLayout.pills.isEmpty() ? 0 : kLabelTopGapPx + m_labelLayout.size.height();
}

QList<QRectF> NodeItem::labelRects() const {
    QList<QRectF> rects;
    if (m_labelLayout.pills.isEmpty()) return rects;
    const QRectF shape = shapeRect();
    const QPointF origin(shape.center().x() - m_labelLayout.width / 2, shape.bottom() + kLabelTopGapPx);
    for (const QRectF &pill : m_labelLayout.pills) rects.append(pill.translated(origin));
    return rects;
}

int NodeItem::labelAt(const QPointF &position) const {
    const QList<QRectF> rects = labelRects();
    for (int i = 0; i < rects.size(); ++i)
        if (rects[i].contains(position)) return i;
    return -1;
}

bool &NodeItem::showAllNotes() {
    static bool shown = false;
    return shown;
}

namespace {
constexpr qreal kNotePreviewPaddingPx = 8;
constexpr qreal kNoteSideMarginPx = 16;
constexpr qreal kNotePreviewMinTextPx = 128;
constexpr qreal kNotePreviewMaxTextPx = 328;  // riquadro largo al massimo 344 px
constexpr int kNotePreviewMaxLines = 6;
constexpr int kNotePreviewFontPx = 12;
}  // namespace

// FEAT-059 (rivisto dopo la prova, 2026-10-19: con "Mostra tutte le note" la nota sta
// DENTRO il nodo, sotto immagine e testo (posto "sotto" del contratto), al posto dell'icona; il nodo
// si allunga e la mappa si ridispone. Larga quanto il contenuto, fra 128 e 328 px di testo.
void NodeItem::updateNotePreview() {
    NotePreviewKey key;
    key.shown = showAllNotes() && hasNote(m_node);
    if (key.shown) {
        key.notes = m_node->notes;
        key.notesHtml = m_node->notesHtml;
        key.color = m_node->text.rgba();
        const qreal inner = contentLayout(false).content.width() - taskZoneWidth() - linkZoneWidth();
        key.width = std::clamp(inner - 2 * kNoteSideMarginPx - 2 * kNotePreviewPaddingPx, kNotePreviewMinTextPx, kNotePreviewMaxTextPx);
    }
    if (key == m_notePreviewKey) return;
    prepareGeometryChange();
    m_notePreviewKey = key;
    m_notePreview.reset();
    m_notePreviewHeight = 0;
    if (!key.shown) return;
    m_notePreview = std::make_unique<QTextDocument>();
    QFont font = scene() ? scene()->font() : QFont();
    font.setPixelSize(kNotePreviewFontPx);
    m_notePreview->setDefaultFont(font);
    m_notePreview->setDocumentMargin(0);
    if (key.notesHtml.isEmpty()) m_notePreview->setPlainText(key.notes);
    else m_notePreview->setHtml(key.notesHtml);
    FormulaText::renderInDocument(m_notePreview.get(), font, m_node->text);  // FEAT-064: $...$ nella nota
    m_notePreview->setTextWidth(key.width);
    const qreal maxText = QFontMetricsF(font).lineSpacing() * kNotePreviewMaxLines;
    m_notePreviewHeight = std::min(m_notePreview->size().height(), maxText) + 2 * kNotePreviewPaddingPx;
}

QRectF NodeItem::notePreviewRect() const { return m_notePreview ? contentLayout().note : QRectF(); }

NodeItem::SelectionStyle &NodeItem::selectionStyle() {
    static SelectionStyle style;
    return style;
}

namespace {
// Quanto l'anello di selezione esce oltre i 4 px di margine già compresi in normalBoundingRect().
// Con i valori predefiniti (distanza 1 + bordo 1 + spessore 2 = 4 px) è zero: il riquadro del nodo non cambia.
// Il bordo del nodo (penna da 2 px centrata sul riquadro) esce di 1 px dal riquadro. Prima la
// distanza dell'anello si misurava dal riquadro, quindi con distanza 1 il bordo copriva lo spazio
// e l'anello sembrava attaccato al nodo (foto dell'utente, 2026-10-13).
qreal selectionOverflow(const MindNode *node) {
    const auto &style = NodeItem::selectionStyle();
    return std::max<qreal>(0, std::ceil(style.gap + borderOutside(node) + style.width) - 4);
}
}

void NodeItem::selectionStyleChanged() {
    prepareGeometryChange();
    update();
}

QPainterPath NodeItem::shape() const {
    QPainterPath path;
    // Come prima (forma predefinita = boundingRect), ma senza il margine extra dell'anello.
    path.addRect(m_draggingImage ? boundingRect() : normalBoundingRect());
    // FEAT-028: the whole cell is clickable. It contains the node, so the rectangles overlap: with the
    // default odd-even rule the node itself became a hole (user's report 2026-10-25).
    if (m_cell) {
        path.setFillRule(Qt::WindingFill);
        path.addRect(m_cell->rect);
        if (!m_cell->frame.isNull()) path.addRect(m_cell->frame);
    }
    for (const QRectF &pill : labelRects()) path.addRect(pill);  // FEAT-060: clic sulle pillole
    for (const int side : {-1, 1}) if (const QRectF r = collapseRect(side); !r.isEmpty()) path.addRect(r);  // FEAT-016
    return path;
}

QRectF NodeItem::boundingRect() const {
    if (!m_draggingImage) {
        const qreal extra = selectionOverflow(m_node);
        QRectF bounds = normalBoundingRect().adjusted(-extra, -extra, extra, extra);
        if (m_cell) bounds = bounds.united(m_cell->rect.united(m_cell->frame).adjusted(-1, -1, 1, 1));  // FEAT-028: the cell with its border
        for (const QRectF &pill : labelRects()) bounds = bounds.united(pill.adjusted(-1, -1, 1, 1));  // FEAT-060
        // FEAT-016: sempre compreso (anche quando non si vede), cosi' selezionare non cambia l'area.
        for (const int side : {-1, 1})
            if ((side < 0 ? m_collapseLeft : m_collapseRight).shown) bounds = bounds.united(collapseRect(side, true).adjusted(-2, -2, 2, 2));
        return bounds;
    }
    const qreal reachX = m_node->imageSize.width() / 2 + 190;
    const qreal reachY = m_node->imageSize.height() / 2 + 150;
    return normalBoundingRect().united(QRectF(-reachX, -reachY, reachX * 2, reachY * 2));
}

namespace {
// Larghezza massima di un nodo con formule: oltre si va a capo.
constexpr qreal kMaxFormulaNodeWidth = 440;
// Nodi di solo testo: larghezza su misura del titolo, poi a capo oltre kMaxTextNodeWidth.
// Margini interni per ruolo (NodeStyle::textPadding, padX/padY). BUG-003 (2026-10-15): nessuna misura
// minima fissa (prima 96x40, 128x48 per la radice): il riquadro automatico e' il contenuto piu' il
// padding; solo un titolo vuoto resta largo quanto un carattere (m_minContentWidth).
constexpr qreal kMaxTextNodeWidth = 280;
// FEAT-072: the central topic wraps beyond 330 px (measured live 2026-10-23: 329).
constexpr qreal kMaxCentralNodeWidth = 330;
// Testo allineato a sinistra (richiesta 2026-10-13; prima centrato): distanza tra il bordo
// sinistro del riquadro e il testo. Di norma il padding; si riduce solo quando il blocco di testo e'
// piu' largo del riquadro meno due padding (righe impaginate con meno margine o formule larghe),
// così resta lo stesso spazio ai due lati e il testo non tocca il bordo destro.
qreal textLeftInset(qreal boxWidth, qreal blockWidth, qreal padding) { return std::min(padding, (boxWidth - blockWidth) / 2); }

QFont nodeFont(const MindNode *node) {
    QFont font = QApplication::font();
    // FEAT-022: testo del nodo (predefiniti nella cascata, vedi NodeStyle::kRootFontSizePt). Senza un
    // font scelto il predefinito delle mappe, incluso nell'app; un font non installato resta quello predefinito.
    const QString family = node->fontFamily.isEmpty() ? NodeStyle::kDefaultMapFontFamily : node->fontFamily;
    font.setFamilies({NodeItem::isFontInstalled(family) ? family : NodeStyle::kDefaultMapFontFamily});
    font.setPointSizeF(node->fontSize);
    font.setWeight(static_cast<QFont::Weight>(node->fontWeight));
    font.setItalic(node->italic);
    font.setUnderline(node->underline);
    font.setStrikeOut(node->strikeOut);  // FEAT-020: stile Depenna, o scelto a mano (FEAT-022)
    // Maiuscole/minuscole solo nel disegno: il titolo resta come scritto
    // (https://doc.qt.io/qt-6/qfont.html#Capitalization-enum).
    font.setCapitalization(node->textCase == TextCase::Maiuscolo   ? QFont::AllUppercase
                           : node->textCase == TextCase::Minuscolo ? QFont::AllLowercase
                                                                   : QFont::MixedCase);
    return font;
}

// FEAT-022: allineamento del titolo per QTextOption e QTextDocument.
Qt::Alignment textAlignment(const MindNode *node) {
    switch (node->textAlign) {
    case TextAlign::Centro: return Qt::AlignHCenter;
    case TextAlign::Destra: return Qt::AlignRight;
    case TextAlign::Giustificato: return Qt::AlignJustify;
    case TextAlign::Sinistra: break;
    }
    return Qt::AlignLeft;
}

// Elenco dei font come finestra a comparsa vera (fuori dalla scena), richiesta 2026-10-12:
// su Windows l'elenco del QComboBox, incorporato nella scena insieme alla barra
// (QGraphicsProxyWidget), si apriva e spariva subito. Altezza massima di dieci righe con
// scorrimento; scrivendo si va al font con quel nome (ricerca incrementale di
// QAbstractItemView::keyboardSearch: lettere veloci formano un prefisso, una lettera dopo
// una pausa riparte da quella iniziale, una lettera sbagliata lascia il font più vicino).
// Elenco dei font del sistema condiviso da tutte le barre di formattazione: costruito alla
// prima modifica e poi riusato (QComboBox::setModel non prende possesso del modello).
QStandardItemModel *sharedFontModel() {
    static QStandardItemModel *model = [] {
        auto *result = new QStandardItemModel(qApp);
        for (const QString &family : QFontDatabase::families()) {
            auto *item = new QStandardItem(family);
            item->setData(family, Qt::UserRole);  // stesso ruolo di QComboBox::addItem(text, data)
            item->setData(UiTheme::color(UiTheme::Token::Text), Qt::ForegroundRole);
            result->appendRow(item);
        }
        return result;
    }();
    return model;
}

class FontListPopup final : public QFrame {
public:
    std::function<void(int)> chosen;  // indice della voce nella select dei font

    FontListPopup(const QComboBox *source, int width) : QFrame(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        // Bug 2026-10-14: un clic fuori chiude solo l'elenco; senza questo attributo Qt ripeteva la
        // pressione sul widget sotto il mouse (https://doc.qt.io/qt-6/qt.html#WidgetAttribute-enum,
        // Qt::WA_NoMouseReplay). Il clic successivo fuori chiude la barra e la modifica.
        setAttribute(Qt::WA_NoMouseReplay);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(4, 4, 4, 4);
        m_list = new QListWidget(this);
        m_list->setFrameShape(QFrame::NoFrame);
        m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // Righe tutte uguali: Qt misura una sola riga invece di caricare ogni font per
        // calcolarne l'altezza; i font si caricano solo per le righe visibili (PC vecchi).
        m_list->setUniformItemSizes(true);
        m_list->setStyleSheet(UiTheme::qss(R"(
            QListWidget{background:transparent;color:@text;border:0;outline:0;}
            QListWidget::item{color:@text;min-height:28px;padding:0 8px;border-radius:4px;}
            QListWidget::item:hover{background:@divider;}
            QListWidget::item:selected{color:@text;background:@surfaceRaised;}
            QScrollBar:vertical{background:transparent;width:6px;margin:0;}
            QScrollBar::handle:vertical{background:#c4cad2;min-height:24px;border-radius:3px;}
            QScrollBar::handle:vertical:hover{background:#9aa2ac;}
            QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
            QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}
        )"));
        for (int i = 0; i < source->count(); ++i) {
            const QString family = source->itemData(i).toString();
            // I font solo bitmap (Fixedsys, Terminal, MS Sans Serif...) non sono disegnabili da
            // DirectWrite: Qt li segnala come errore e il testo non cambierebbe.
            // P8 (2026-10-13): il controllo su tutti i font si fa una volta sola per sessione,
            // non a ogni apertura dell'elenco.
            static const QSet<QString> scalableFamilies = [] {
                QSet<QString> result;
                for (const QString &name : QFontDatabase::families()) if (QFontDatabase::isScalable(name)) result.insert(name);
                return result;
            }();
            if (!scalableFamilies.contains(family)) continue;
            auto *item = new QListWidgetItem(source->itemText(i), m_list);
            item->setData(Qt::UserRole, i);
            item->setData(Qt::FontRole, QFont(family));
            if (i == source->currentIndex()) m_list->setCurrentItem(item);
        }
        layout->addWidget(m_list);
        setFixedSize(width, 10 * 28 + 8 + 16);  // dieci righe, padding della lista e del pannello
        if (m_list->currentItem()) m_list->scrollToItem(m_list->currentItem(), QAbstractItemView::PositionAtCenter);
        const auto pick = [this](QListWidgetItem *item) {
            if (!item) return;
            const int index = item->data(Qt::UserRole).toInt();
            const auto callback = chosen;
            close();
            if (callback) callback(index);
        };
        QObject::connect(m_list, &QListWidget::itemClicked, this, pick);
        QObject::connect(m_list, &QListWidget::itemActivated, this, pick);  // Invio
        m_list->installEventFilter(this);
        m_list->setFocus();
    }

protected:
    // Con un Qt::Popup aperto Qt manda al popup tutti i clic, anche quelli fuori: chi è fuori
    // lo chiude (comportamento già di QWidget::mousePressEvent per i popup, qui esplicito).
    void mousePressEvent(QMouseEvent *event) override {
        if (!rect().contains(event->position().toPoint())) { close(); return; }
        QFrame::mousePressEvent(event);
    }
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == m_list && event->type() == QEvent::KeyPress &&
            static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            close();
            return true;
        }
        return QFrame::eventFilter(watched, event);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(UiTheme::color(UiTheme::Token::Divider), 1));
        painter.setBrush(UiTheme::color(UiTheme::Token::Surface));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    }

private:
    QListWidget *m_list = nullptr;
};

class RoundedComboBox final : public QComboBox {
public:
    using QComboBox::QComboBox;

    void setPopupMaximumHeight(int height) { m_popupMaximumHeight = height; }

    // Se impostata, sostituisce l'elenco standard di Qt (usata dalla select dei font).
    std::function<void()> customPopup;
    // Chiamata quando l'elenco standard si chiude (scelta, Esc o clic fuori), vedi hidePopup.
    std::function<void()> popupHidden;

    void hidePopup() override {
        QComboBox::hidePopup();
        if (popupHidden) popupHidden();
    }

    void showPopup() override {
        if (customPopup) { customPopup(); return; }
        QComboBox::showPopup();
        // Il popup del QComboBox è una finestra separata su Windows: il border-radius
        // del foglio di stile non ne ritaglia gli angoli. La maschera viene applicata
        // dopo che Qt ha calcolato la dimensione definitiva della finestra.
        QTimer::singleShot(0, this, [this] {
            QWidget *popup = view() ? view()->window() : nullptr;
            if (!popup) return;
            if (m_popupMaximumHeight > 0) {
                popup->setMaximumHeight(m_popupMaximumHeight);
                if (popup->height() > m_popupMaximumHeight)
                    popup->resize(popup->width(), m_popupMaximumHeight);
            }
            QPainterPath outline;
            outline.addRoundedRect(QRectF(popup->rect()), 8, 8);
            popup->setMask(QRegion(outline.toFillPolygon().toPolygon()));
        });
    }

private:
    int m_popupMaximumHeight = 0;
};

class TextFormatBar final : public QWidget {
    // User request 2026-10-25: as high as the selected topic's bar (TopicTools), with its icon size
    // (before: 32 px controls and 20 px icons, like the top bar).
    static constexpr int kControlPx = 28;
    static constexpr int kIconPx = 16;
    // Senza Q_OBJECT tr() userebbe il contesto "QWidget" e non troverebbe la traduzione (ADR-003).
    Q_DECLARE_TR_FUNCTIONS(TextFormatBar)
public:
    explicit TextFormatBar(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("TextFormatBar");
        // Bug 2026-10-13 (foto dell'utente): nel QGraphicsProxyWidget la barra è una finestra a sé
        // e Qt ne riempiva tutto il rettangolo prima del foglio di stile, quindi fuori dal
        // border-radius gli angoli restavano bianchi e squadrati sopra i nodi vicini. Con
        // WA_TranslucentBackground il rettangolo non si riempie più
        // (https://doc.qt.io/qt-6/qt.html#WidgetAttribute-enum), ma su Windows sparisce anche lo
        // sfondo del foglio di stile (seconda foto dell'utente). Lo sfondo bianco arrotondato e il
        // bordo ora li disegna paintEvent(), con gli stessi valori di prima.
        setAttribute(Qt::WA_TranslucentBackground);
        setStyleSheet(UiTheme::qss(R"(
            QToolButton { border:0; border-radius:6px; padding:0; background:transparent; }
            QToolButton:hover, QToolButton:checked { background:rgba(15,23,42,15); }
            QToolButton:pressed { background:rgba(15,23,42,28); }
            QComboBox { min-width:44px; border:1px solid rgba(15,23,42,15); border-radius:6px; padding:0 6px; background:#ffffff; color:#202630; }
            QComboBox::drop-down { border:0; width:18px; }
            QComboBox QAbstractItemView { background:@surface; color:@text; border:1px solid @divider; border-radius:8px; padding:4px; outline:0; selection-background-color:@surfaceRaised; selection-color:@text; }
            QComboBox QAbstractItemView::item { color:@text; min-height:28px; padding:0 8px; border-radius:4px; }
            QComboBox QAbstractItemView::item:selected { color:@text; background:@surfaceRaised; }
        )"));
        auto *row = new QHBoxLayout(this);
        // Come ToolbarGroupFrame: 4 px attorno ai bottoni, 2 px tra loro; la barra prende la
        // misura del contenuto e non stira i bottoni.
        row->setContentsMargins(4, 4, 4, 4);
        row->setSpacing(2);
        row->setSizeConstraint(QLayout::SetFixedSize);

        m_font = new RoundedComboBox;
        m_font->setFocusPolicy(Qt::NoFocus);
        m_font->setToolTip(tr("Font"));
        m_font->setMinimumWidth(104);
        m_font->setFixedHeight(kControlPx);  // stessa altezza dei bottoni
        m_font->setMaximumWidth(132);
        m_font->setMaxVisibleItems(10);
        static_cast<RoundedComboBox *>(m_font)->setPopupMaximumHeight(304);  // dieci righe da 28px più bordo e padding
        m_font->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_font->view()->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
        // Ora l'elenco si costruisce una volta sola per tutta l'app e ogni barra lo condivide.
        // Niente QFont per voce: il nome nel proprio font lo disegna solo l'elenco a comparsa
        // (FontListPopup), e solo per le righe visibili. La larghezza della select non dipende
        // più dalla scansione di tutte le voci.
        m_font->setModel(sharedFontModel());
        m_font->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_font->setMinimumContentsLength(10);
        stylePopup(m_font);
        static_cast<RoundedComboBox *>(m_font)->customPopup = [this] { showFontList(); };
        m_size = new RoundedComboBox;
        m_size->setFocusPolicy(Qt::NoFocus);
        m_size->setFixedHeight(kControlPx);
        for (const int size : {8, 10, 12, 14, 18, 24, 36, 48, 60}) {
            m_size->addItem(QString::number(size), size);
            m_size->setItemData(m_size->count() - 1, UiTheme::color(UiTheme::Token::Text), Qt::ForegroundRole);
        }
        // Su Windows la vista popup del QComboBox può ignorare il colore QSS delle voci
        // non selezionate; la palette esplicita evita testo nero sul menu scuro.
        QPalette sizePalette = m_size->view()->palette();
        sizePalette.setColor(QPalette::Base, UiTheme::color(UiTheme::Token::Surface));
        sizePalette.setColor(QPalette::Text, UiTheme::color(UiTheme::Token::Text));
        sizePalette.setColor(QPalette::Highlight, UiTheme::color(UiTheme::Token::SurfaceRaised));
        sizePalette.setColor(QPalette::HighlightedText, UiTheme::color(UiTheme::Token::Text));
        m_size->view()->setPalette(sizePalette);
        // Applicato direttamente alla vista perché, essendo un popup top-level, su
        // Windows non eredita in modo affidabile i selettori discendenti del QComboBox.
        m_size->view()->setStyleSheet(UiTheme::qss(R"(
            QAbstractItemView { background:@surface; color:@text; border:1px solid @divider; padding:4px; outline:0; }
            QAbstractItemView::item { color:@text; min-height:28px; padding:0 8px; border-radius:4px; }
            QAbstractItemView::item:selected { color:@text; background:@surfaceRaised; }
        )"));
        m_size->view()->setItemDelegate(new LightComboItemDelegate(m_size->view()));
        static_cast<RoundedComboBox *>(m_size)->popupHidden = [this] { if (popupClosed) popupClosed(); };
        m_bold = button(":/assets/hugeicons/text-bold.svg", tr("Bold"), true);
        m_italic = button(":/assets/hugeicons/text-italic.svg", tr("Italic"), true);
        m_strike = button(":/assets/hugeicons/text-strikethrough.svg", tr("Strikethrough"), true);
        m_underline = button(":/assets/hugeicons/text-underline.svg", tr("Underline"), true);
        m_highlight = button(":/assets/hugeicons/text-highlight.svg", tr("Highlight"), true);  // FEAT-068
        m_color = new QToolButton;
        m_color->setFocusPolicy(Qt::NoFocus);
        m_color->setToolTip(tr("Text color"));
        m_color->setFixedSize(kControlPx, kControlPx);

        row->addWidget(m_font); row->addWidget(m_size); row->addSpacing(4);
        row->addWidget(m_bold); row->addWidget(m_italic); row->addWidget(m_strike); row->addWidget(m_underline);
        row->addWidget(m_highlight);
        row->addSpacing(4); row->addWidget(m_color);

        connect(m_font, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            QTextCharFormat format; format.setFontFamilies({m_font->itemData(index).toString()}); apply(format);
        });
        connect(m_size, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            QTextCharFormat format; format.setFontPointSize(m_size->itemData(index).toInt()); apply(format);
        });
        connect(m_bold, &QToolButton::clicked, this, [this](bool on) {
            QTextCharFormat format; format.setFontWeight(on ? QFont::Bold : QFont::Normal); apply(format);
        });
        connect(m_italic, &QToolButton::clicked, this, [this](bool on) {
            QTextCharFormat format; format.setFontItalic(on); apply(format);
        });
        connect(m_strike, &QToolButton::clicked, this, [this](bool on) {
            QTextCharFormat format; format.setFontStrikeOut(on); apply(format);
        });
        connect(m_underline, &QToolButton::clicked, this, [this](bool on) {
            QTextCharFormat format; format.setFontUnderline(on); apply(format);
        });
        connect(m_highlight, &QToolButton::clicked, this, [this](bool on) { if (applyHighlight) applyHighlight(on); });
        connect(m_color, &QToolButton::clicked, this, [this] { showColorMenu(); });
        setColor(QColor("#202630"));
    }

    std::function<void(const QTextCharFormat &)> applyFormat;
    std::function<void(bool)> applyHighlight;  // FEAT-068: accende o toglie l'evidenziato
    int boldWeight = QFont::Bold;  // FEAT-022: peso da cui un tratto conta come grassetto (NodeItem::boldWeightFor)
    int defaultPointSize = 11;     // FEAT-022: dimensione del nodo, per i tratti senza dimensione propria
    // Bug 2026-10-14: un elenco della barra (font, dimensione, colore) chiuso con un clic fuori
    // lasciava il campo di testo senza focus. La modifica restava aperta e un altro clic fuori non
    // la concludeva più (la conferma parte dalla perdita del focus). Chiamata alla chiusura di
    // ogni elenco: il nodo ridà il focus al campo, così il clic successivo fuori chiude tutto.
    std::function<void()> popupClosed;
    // Colori della palette del foglio per "Tema attuale" del selettore (li fornisce il nodo).
    std::function<QStringList()> themeColors;

    // Apre l'elenco dei font sotto la select (sopra se in basso manca spazio). La barra sta in
    // un QGraphicsProxyWidget: la posizione sullo schermo passa da scena e vista.
    void showFontList() {
        // Bug 2026-10-13 (foto dell'utente): l'elenco restava aperto dopo un clic fuori. Un clic
        // fuori da un Qt::Popup lo chiude e Qt ripete la pressione sul widget sotto il mouse
        // (https://doc.qt.io/qt-6/qt.html#WidgetAttribute-enum, Qt::WA_NoMouseReplay): se il clic
        // cade sulla select, la ripetizione lo riapriva subito. Ora, come la select standard, un
        // clic sulla select con l'elenco appena chiuso lo lascia chiuso.
        if (m_fontPopup) { m_fontPopup->close(); return; }
        if (m_fontPopupClosed.isValid() && m_fontPopupClosed.elapsed() < 250) return;
        QGraphicsProxyWidget *proxy = graphicsProxyWidget();
        QGraphicsView *view = proxy && proxy->scene() ? proxy->scene()->views().value(0) : nullptr;
        if (!view) return;
        const QPolygonF inScene = proxy->mapToScene(proxy->subWidgetRect(m_font));
        const QRect inViewport = view->mapFromScene(inScene).boundingRect();
        const QRect global(view->viewport()->mapToGlobal(inViewport.topLeft()), inViewport.size());
        auto *popup = new FontListPopup(m_font, std::max(240, global.width()));
        m_fontPopup = popup;
        QObject::connect(popup, &QObject::destroyed, this, [this] {
            m_fontPopupClosed.start();
            if (popupClosed) popupClosed();
        });
        popup->chosen = [this](int index) {
            m_font->setCurrentIndex(index);
            emit m_font->activated(index);
        };
        QPoint at = global.bottomLeft() + QPoint(0, 4);
        if (const QScreen *screen = view->screen()) {
            const QRect available = screen->availableGeometry();
            if (at.y() + popup->height() > available.bottom()) at.setY(global.top() - 4 - popup->height());
            at.setX(std::clamp(at.x(), available.left(), std::max(available.left(), available.right() - popup->width())));
        }
        popup->move(at);
        popup->show();
    }

    // FEAT-095: Ctrl+B and the others on the selection, as a click on the button.
    void toggle(RichTitle::CharStyle style) {
        switch (style) {
        case RichTitle::CharStyle::Bold: m_bold->click(); break;
        case RichTitle::CharStyle::Italic: m_italic->click(); break;
        case RichTitle::CharStyle::Underline: m_underline->click(); break;
        case RichTitle::CharStyle::StrikeOut: m_strike->click(); break;
        }
    }

    void sync(const QTextCharFormat &format) {
        const QSignalBlocker b1(m_size), b2(m_bold), b3(m_italic), b4(m_strike), b5(m_underline), b6(m_highlight);
        const int pointSize = qRound(format.fontPointSize() > 0 ? format.fontPointSize() : defaultPointSize);
        int index = m_size->findData(pointSize);
        if (index < 0) {
            m_size->addItem(QString::number(pointSize), pointSize);
            index = m_size->count() - 1;
            m_size->setItemData(index, UiTheme::color(UiTheme::Token::Text), Qt::ForegroundRole);
        }
        m_size->setCurrentIndex(index);
        const QString family = format.font().families().value(0);
        const int familyIndex = m_font->findData(family);
        if (familyIndex >= 0) m_font->setCurrentIndex(familyIndex);
        m_bold->setChecked(format.fontWeight() >= boldWeight);
        m_italic->setChecked(format.fontItalic());
        m_strike->setChecked(format.fontStrikeOut());
        m_underline->setChecked(format.fontUnderline());
        m_highlight->setChecked(MarkdownShortcuts::isHighlight(format));
        if (format.foreground().style() != Qt::NoBrush && !MarkdownShortcuts::isHighlight(format)) setColor(format.foreground().color());
    }

private:
    QToolButton *button(const QString &icon, const QString &tip, bool checkable = false) {
        auto *result = new QToolButton;
        result->setIcon(QIcon(icon)); result->setIconSize({kIconPx, kIconPx}); result->setToolTip(tip);
        result->setFixedSize(kControlPx, kControlPx);
        result->setCheckable(checkable); result->setFocusPolicy(Qt::NoFocus);
        return result;
    }
    void apply(const QTextCharFormat &format) { if (applyFormat) applyFormat(format); }
    void stylePopup(QComboBox *combo) {
        QPalette palette = combo->view()->palette();
        palette.setColor(QPalette::Base, UiTheme::color(UiTheme::Token::Surface));
        palette.setColor(QPalette::Text, UiTheme::color(UiTheme::Token::Text));
        palette.setColor(QPalette::Highlight, UiTheme::color(UiTheme::Token::SurfaceRaised));
        palette.setColor(QPalette::HighlightedText, UiTheme::color(UiTheme::Token::Text));
        combo->view()->setPalette(palette);
        combo->view()->setStyleSheet(UiTheme::qss(R"(
            QAbstractItemView{background:@surface;color:@text;border:1px solid @divider;padding:4px;outline:0;}
            QAbstractItemView::item{color:@text;min-height:28px;padding:0 8px;border-radius:4px;}
            QAbstractItemView::item:selected{color:@text;background:@surfaceRaised;}
            QScrollBar:vertical{background:transparent;width:6px;margin:4px 0;}
            QScrollBar::handle:vertical{background:#c4cad2;min-height:24px;border-radius:3px;}
            QScrollBar::handle:vertical:hover{background:#9aa2ac;}
            QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
            QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}
        )"));
        combo->view()->setItemDelegate(new LightComboItemDelegate(combo->view()));
    }
    void showColorMenu() {
        // FEAT-019: selettore colori unico dell'app (prima un menu proprio con 33 colori).
        ColorPicker::Options options;
        options.current = m_currentColor;
        options.themeColors = themeColors ? themeColors() : QStringList();
        const QRect anchor(m_color->mapToGlobal(QPoint(0, 0)), m_color->size());
        ColorPicker::exec(anchor, ColorPicker::Placement::Above, options, [this](const QColor &color) {
            QTextCharFormat format;
            format.setForeground(color);
            apply(format);
            setColor(color);
        });
        // Selettore chiuso anche senza scegliere un colore: il focus torna al testo (vedi popupClosed).
        if (popupClosed) popupClosed();
    }
protected:
    // Sfondo della barra: bianco, raggio 10, bordo 1 px rgba(15,23,42,13), come nel foglio di
    // stile di prima (AGENTS.md "Formattazione della selezione nella mappa"). Il bordo sta a
    // mezzo pixel dal bordo del widget per restare nitido con l'antialiasing.
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(15, 23, 42, 13), 1));
        painter.setBrush(QColor("#ffffff"));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
    }

private:
    void setColor(const QColor &color) {
        m_currentColor = color;
        m_color->setStyleSheet(UiTheme::qss(QString("QToolButton{background:%1;border:6px solid #ffffff;border-radius:%2px;} QToolButton:hover{border-color:rgba(15,23,42,15);}").arg(color.name(QColor::HexArgb)).arg(kControlPx / 2)));
    }
    QComboBox *m_font = nullptr;
    QPointer<QWidget> m_fontPopup;      // elenco dei font aperto (vedi showFontList)
    QElapsedTimer m_fontPopupClosed;    // da quando l'elenco si è chiuso
    QComboBox *m_size = nullptr;
    QToolButton *m_bold = nullptr;
    QToolButton *m_italic = nullptr;
    QToolButton *m_strike = nullptr;
    QToolButton *m_underline = nullptr;
    QToolButton *m_highlight = nullptr;  // FEAT-068
    QToolButton *m_color = nullptr;
    QColor m_currentColor;  // colore mostrato dal bottone, punto di partenza del selettore
};

// Barra di formattazione condivisa da tutti i nodi (vedi startInlineEditor).
QPointer<QGraphicsProxyWidget> g_formatProxy;

// Campo di testo della modifica nel nodo. Invio (senza Maiusc) e Tab confermano, Esc annulla,
// perdere il focus conferma; Maiusc+Invio va a capo. Invio, Tab ed Esc vengono "prenotati"
// con ShortcutOverride, altrimenti scatterebbero le azioni della finestra (nuovo fratello,
// nuovo figlio) mentre si scrive.
class InlineTitleEditor final : public QGraphicsTextItem {
public:
    explicit InlineTitleEditor(QGraphicsItem *parent) : QGraphicsTextItem(parent) {
        // Lampeggio del cursore gestito qui, perché il disegno è personalizzato (vedi paint).
        const int flash = QApplication::cursorFlashTime();
        if (flash > 0) {
            m_blink.setInterval(flash / 2);
            QObject::connect(&m_blink, &QTimer::timeout, this, [this] { m_caretOn = !m_caretOn; update(); });
            m_blink.start();
        }
    }
    std::function<void(bool)> finished;  // true = conferma il testo
    std::function<void()> selectionChanged;
    // La barra e i relativi popup fanno parte della stessa sessione di modifica:
    // spostare lì il focus non deve confermare e distruggere prematuramente l'editor.
    std::function<bool()> keepEditing;
    bool formattingChanged = false;
    int boldWeight = QFont::Bold;  // FEAT-068: peso del grassetto di "**testo**"
    int pendingTask = 0;           // FEAT-068: "- [ ] " a inizio titolo, casella alla conferma
    std::function<void(RichTitle::CharStyle)> toggleStyle;  // FEAT-095: on the selection
    RichTitle::PlainFirstTyping plainTyping;  // FEAT-095

    // Disegno del testo senza il riquadro tratteggiato di "focus" di QGraphicsTextItem e con
    // la selezione azzurra chiara dell'outliner (#b3d4fc) al posto del blu di sistema.
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override {
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette.setColor(QPalette::Text, defaultTextColor());
        const QTextCursor cursor = textCursor();
        if (hasFocus() && m_caretOn) context.cursorPosition = cursor.position();
        if (cursor.hasSelection()) {
            QAbstractTextDocumentLayout::Selection selection;
            selection.cursor = cursor;
            // Colore da Impostazioni > Selezione (2026-10-13), predefinito sempre #b3d4fc.
            selection.format.setBackground(NodeItem::selectionStyle().textSelectionColor);
            selection.format.setForeground(defaultTextColor());
            context.selections << selection;
        }
        context.clip = boundingRect();
        painter->save();
        painter->setClipRect(boundingRect());
        document()->documentLayout()->draw(painter, context);
        painter->restore();
    }

protected:
    // FEAT-042: our menu (Cut, Copy, Paste, Select all) instead of the system one; each command is
    // the same key the field already handles.
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override {
        event->accept();
        // FEAT-044: the document is drawn at the item's origin, so item coordinates are layout ones.
        TextEditMenu::Spelling spelling;
        spelling.word = SpellCheck::misspelledAt(document(), document()->documentLayout()->hitTest(event->pos(), Qt::FuzzyHit));
        spelling.replace = [this, word = spelling.word](const QString &replacement) {
            QTextCursor cursor = textCursor();
            TextEditMenu::replaceWord(cursor, word, replacement);
            setTextCursor(cursor);
            if (selectionChanged) selectionChanged();
        };
        TextEditMenu::exec(event->widget(), event->screenPos(), textCursor().hasSelection(), [this](QKeySequence::StandardKey key) {
            const QKeyCombination combination = QKeySequence(key)[0];
            QKeyEvent press(QEvent::KeyPress, combination.key(), combination.keyboardModifiers());
            keyPressEvent(&press);
        }, spelling);
    }
    bool sceneEvent(QEvent *event) override {
        if (event->type() == QEvent::ShortcutOverride) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape ||
                key == Qt::Key_Tab || key == Qt::Key_Backtab) {
                event->accept();
                return true;
            }
        }
        return QGraphicsTextItem::sceneEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        const int key = event->key();
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !(event->modifiers() & Qt::ShiftModifier)) {
            if (finished) finished(true);
            return;
        }
        if (key == Qt::Key_Escape) { if (finished) finished(false); return; }
        if (key == Qt::Key_Tab || key == Qt::Key_Backtab) { if (finished) finished(true); return; }
        if (event->matches(QKeySequence::Paste)) {  // FEAT-094: also the text menu's Paste
            auto *map = qobject_cast<MindMapScene *>(scene());
            auto *owner = static_cast<NodeItem *>(parentItem());
            if (map && owner && map->pasteWhileEditing && map->pasteWhileEditing(owner->node())) return;
        }
        if (auto *map = qobject_cast<MindMapScene *>(scene()); map && map->charStyleKey) {
            if (const int style = map->charStyleKey(event); style >= 0) {
                // Without a selection nothing changes (FEAT-095 non-goal).
                if (textCursor().hasSelection() && toggleStyle) toggleStyle(RichTitle::CharStyle(style));
                return;
            }
        }
        if (QTextCursor cursor = textCursor(); plainTyping.type(event, cursor)) setTextCursor(cursor);
        else QGraphicsTextItem::keyPressEvent(event);
        // FEAT-068: scorciatoie Markdown sul carattere appena scritto.
        if (event->text().size() == 1 && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
            QTextCursor cursor = textCursor();
            MarkdownShortcuts::Options options;
            options.boldWeight = boldWeight;
            const MarkdownShortcuts::Result result = MarkdownShortcuts::afterTyping(cursor, event->text().front(), options);
            if (result.changed) {
                setTextCursor(cursor);
                if (result.task) pendingTask = result.task;
                else formattingChanged = true;
            }
        }
        restartBlink();
        if (selectionChanged) selectionChanged();
    }
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override { plainTyping.active = false; QGraphicsTextItem::mousePressEvent(event); restartBlink(); if (selectionChanged) selectionChanged(); }
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override { QGraphicsTextItem::mouseMoveEvent(event); update(); if (selectionChanged) selectionChanged(); }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override { QGraphicsTextItem::mouseReleaseEvent(event); if (selectionChanged) selectionChanged(); }
    void focusOutEvent(QFocusEvent *event) override {
        QGraphicsTextItem::focusOutEvent(event);
        if (event->reason() == Qt::PopupFocusReason) return;  // a menu (FEAT-042) does not end the editing
        // Conferma rimandata al giro successivo del ciclo eventi: se il focus si perde perché
        // la finestra si sta chiudendo, item e finestra vengono distrutti prima e la conferma
        // non parte (prima: crash in MainWindow::setDirty durante la distruzione).
        QTimer::singleShot(0, this, [this] {
            if (!finished || hasFocus()) return;
            if (keepEditing && keepEditing()) return;
            finished(true);
        });
    }

private:
    // Dopo un tasto o un clic il cursore resta visibile, poi riprende a lampeggiare.
    void restartBlink() {
        m_caretOn = true;
        if (m_blink.interval() > 0) m_blink.start();
        update();
    }
    QTimer m_blink;
    bool m_caretOn = true;
};
}

void NodeItem::updateFormula() {
    // FEAT-022: font e metriche del nodo preparati una volta per misura (prima il font si
    // ricostruiva e il suo motore si cercava tre o quattro volte per nodo).
    const QFont font = nodeFont(m_node);
    const QFontMetricsF metrics(font);
    // BUG-003: larghezza minima del contenuto = un carattere medio del font del nodo, cosi' un
    // titolo vuoto non diventa un riquadro di soli margini. Si misura qui, al cambio dei dati.
    m_minContentWidth = metrics.averageCharWidth();
    // Il rendering si prepara solo per i titoli che contengono $...$: i nodi di solo
    // testo non pagano nessun costo aggiuntivo.
    if (FormulaText::containsMath(m_node->title))
        m_formula.update(shownTitle(), font, m_node->text,
                         layoutCustomWidth() > 0 ? std::max<qreal>(1, manualTextWidth() - 2 * padX())
                                                 : kMaxFormulaNodeWidth - 22);
    else
        m_formula = FormulaText();
    if (!m_node->titleHtml.isEmpty() && m_formula.isEmpty()) {
        if (!m_richDocument) m_richDocument = new QTextDocument(this);
        m_richDocument->setDocumentMargin(0);
        m_richDocument->setDefaultFont(font);
        m_richDocument->setHtml(RichTitle::withoutDocumentFont(m_node->titleHtml));  // FEAT-022: vale il font del nodo
        prependNumber(*m_richDocument);
        QTextOption richOption = m_richDocument->defaultTextOption();
        richOption.setAlignment(textAlignment(m_node));
        // (richiesta 2026-10-13: una parola più larga del nodo usciva dal riquadro e veniva
        // tagliata. Ora va a capo anche dentro la parola, ma solo quando non c'è altro modo:
        // https://doc.qt.io/qt-6/qtextoption.html#WrapMode-enum)
        richOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        m_richDocument->setDefaultTextOption(richOption);
        m_richDocument->setTextWidth(-1);
        const qreal naturalWidth = std::ceil(m_richDocument->idealWidth());
        m_richDocument->setTextWidth(layoutCustomWidth() > 0 ? std::max<qreal>(1, manualTextWidth() - 2 * padX())
                                                             : std::min(naturalWidth, maxTextNodeWidth() - 2 * padX()));
        m_textSize = m_richDocument->size();
        return;
    }
    if (m_richDocument) { m_richDocument->deleteLater(); m_richDocument = nullptr; }
    // Misura del testo semplice per il riquadro su misura (vedi textRect()). La
    // larghezza utile per il testo è quella usata in paint(): riquadro meno 11 px per lato.
    const QString plain = shownTitle().replace(QStringLiteral("\\$"), QStringLiteral("$"));
    const qreal singleLine = metrics.horizontalAdvance(plain);
    // Un titolo con "a capo" (Maiusc+Invio, nella mappa o nell'outliner) non è mai una riga
    // sola: prima veniva misurato alto una riga e l'ultima riga usciva dal riquadro.
    // Larghezza scelta trascinando i lati: il testo va a capo a quella larghezza meno i margini.
    if (layoutCustomWidth() > 0)
        m_textSize = plainTextLayout(plain, font, plainWrapWidth(manualTextWidth()), nullptr);
    else if (!plain.contains(QLatin1Char('\n')) && singleLine + 2 * padX() <= maxTextNodeWidth())
        m_textSize = {singleLine, metrics.height()};
    else
        // (richiesta 2026-10-13: con Qt::TextWordWrap una parola più larga del nodo non va a
        // capo; Qt::TextWrapAnywhere invece spezzerebbe tutte le parole. La misura ora usa la
        // stessa impaginazione del disegno, vedi plainTextLayout(): per i testi con spazi il
        // risultato è identico a prima.)
        // The usable width is the box limit minus the padding of the role (FEAT-072: was a fixed 22,
        // the old padding, so a title of the central topic was measured on one line and drawn on two).
        m_textSize = plainTextLayout(plain, font, plainWrapWidth(maxTextNodeWidth()), nullptr);
    updatePlainLayout(font, metrics.height());  // P5
}

// P5 (2026-10-13): stessa impaginazione di QPainter::drawText(rect, Qt::AlignCenter |
// Qt::TextWordWrap, testo), che prima veniva rifatta a ogni paint(). Riproduce qt_format_text()
// di Qt (src/gui/painting/qpainter.cpp): righe larghe quanto il riquadro meno 11 px per lato,
// interlinea (leading) tra le righe, ogni riga portata al pixel intero (qCeil) e alta
// ascent + descent. La centratura orizzontale riga per riga si fa in paint().
// Verificato pixel per pixel contro drawText (test scratch, Qt 6.4.2).
void NodeItem::updatePlainLayout(const QFont &font, qreal fontHeight) {
    if (!m_formula.isEmpty() || m_richDocument) { m_plainLayout.reset(); return; }
    const QString plain = shownTitle().replace(QStringLiteral("\\$"), QStringLiteral("$"));
    const qreal width = plainWrapWidth(textBlockWidth(m_textSize.width(), maxTextNodeWidth()));
    m_plainLayout = std::make_unique<QTextLayout>();
    const QSizeF size = plainTextLayout(plain, font, width, m_plainLayout.get());
    m_plainLayoutSize = {width, size.height()};
    // FEAT-022: giustificato. Le righe si allargano fino alla larghezza utile del riquadro (meno
    // 16 px per lato): con la larghezza automatica e' la riga piu' lunga, con quella scelta a mano
    // e' il limite dell'a capo. Gli a capo non cambiano: ogni riga ci stava gia'.
    if (m_node->textAlign == TextAlign::Giustificato && m_plainLayout->lineCount() > 1) {
        const qreal usable = textBlockWidth(m_textSize.width(), maxTextNodeWidth()) - 2 * padX();
        plainTextLayout(plain, font, std::max(size.width(), std::min(usable, width)), m_plainLayout.get(), Qt::AlignJustify);
    }
    m_plainLayoutFontHeight = fontHeight;
    m_metricsDevice = nullptr;  // impaginazione nuova: il confronto col dispositivo va rifatto
}

// Impaginazione del titolo semplice (P5), usata sia per misurarlo sia per disegnarlo: stessi
// passi di qt_format_text() come descritto sopra. L'unica differenza da drawText(...,
// Qt::TextWordWrap) è l'a capo dentro una parola più larga della riga (richiesta 2026-10-13):
// WrapAtWordBoundaryOrAnywhere spezza la parola solo se non c'è un punto migliore.
// Restituisce la larghezza della riga più lunga e l'altezza totale; layout può essere nullptr
// quando serve solo la misura.
QSizeF NodeItem::plainTextLayout(const QString &plain, const QFont &font, qreal width, QTextLayout *layout,
                                 Qt::Alignment alignment) {
    QTextLayout local;
    QTextLayout *target = layout ? layout : &local;
    target->setText(QString(plain).replace(QLatin1Char('\n'), QChar::LineSeparator));
    target->setFont(font);
    // Come qt_format_text: allineamento a sinistra nel layout, centro e destra fatti a mano in
    // paint(); il giustificato lo fa il layout (FEAT-022).
    QTextOption option(alignment);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    target->setTextOption(option);
    const qreal leading = QFontMetricsF(font).leading();
    qreal height = -leading;
    qreal widest = 0;
    target->beginLayout();
    for (QTextLine line = target->createLine(); line.isValid(); line = target->createLine()) {
        line.setLineWidth(width);
        height += leading;
        height = std::ceil(height);
        line.setPosition(QPointF(0, height));
        height += line.ascent() + line.descent();
        widest = std::max(widest, line.naturalTextWidth());
    }
    target->endLayout();
    return {widest, std::max<qreal>(0, height)};
}

QRectF NodeItem::textRect() const { return contentLayout().box; }

bool NodeItem::hasSideImage() const {
    return m_hasImage && !m_node->title.trimmed().isEmpty()
           && (m_node->imagePlacement == MindNode::ImagePlacement::Left || m_node->imagePlacement == MindNode::ImagePlacement::Right);
}

qreal NodeItem::textBlockWidth(qreal content, qreal maxWidth) const {
    // Larghezza scelta dall'utente: il blocco riempie lo spazio lasciato da immagine affiancata e
    // bottoni; si allarga solo se il contenuto non può andare a capo più stretto (una formula),
    // così nulla esce dal bordo.
    if (layoutCustomWidth() > 0) return std::max(manualTextWidth(), std::ceil(content) + 2 * padX());
    return std::clamp(std::max(content, m_minContentWidth) + 2 * padX(), 0.0, maxWidth);
}

NodeItem::ContentLayout NodeItem::contentLayout(bool withNote) const {
    // Blocco del testo: durante la modifica segue il testo che si sta scrivendo; con formule cresce
    // per contenerle; un titolo vuoto con immagine non ha blocco (solo l'immagine).
    QSizeF block;
    bool hasText = true;
    if (m_editor) block = {textBlockWidth(m_editorSize.width(), std::max(maxTextNodeWidth(), kMaxFormulaNodeWidth)), m_editorSize.height() + 2 * padY()};
    else if (m_node->title.trimmed().isEmpty() && m_hasImage) hasText = false;
    else if (!m_formula.isEmpty()) block = {textBlockWidth(m_formula.size().width(), kMaxFormulaNodeWidth), m_formula.size().height() + 2 * padY()};
    else block = {textBlockWidth(m_textSize.width(), maxTextNodeWidth()), m_textSize.height() + 2 * padY()};
    // BUG-020 (foto dell'utente): un nodo senza testo e senza immagine e' un quadrato alto
    // quanto un nodo di una riga, cosi' resta visibile e cliccabile; con la larghezza a mano vale quella.
    if (!m_editor && hasText && m_node->title.trimmed().isEmpty() && !m_hasImage && layoutCustomWidth() <= 0)
        block.setWidth(block.height());
    // FEAT-037: immagine dentro il nodo, 16/8 px di margine come il testo, 8 px dal testo; il blocco
    // del testo ha gia' i suoi margini, che fanno da spazio verso l'immagine sopra o sotto. Bottoni
    // dei collegamenti (FEAT-027) sempre a destra di tutto.
    const QSizeF image = m_hasImage ? m_node->imageSize : QSizeF();
    const bool side = hasText && hasSideImage();
    qreal inner = 0, height = 0;
    if (!m_hasImage) { inner = block.width(); height = block.height(); }
    else if (!hasText) { inner = image.width() + 2 * padX(); height = image.height() + 2 * padY(); }
    else if (side) { inner = image.width() + kImageGapPx + block.width(); height = std::max(block.height(), image.height() + 2 * padY()); }
    else { inner = std::max(block.width(), image.width() + 2 * padX()); height = block.height() + image.height() + padY(); }
    const qreal links = linkZoneWidth();
    const qreal task = taskZoneWidth();  // FEAT-048: colonna della casella, speculare ai collegamenti
    if (layoutCustomWidth() > 0) inner = std::max(inner, layoutCustomWidth() - links - task);
    // FEAT-059: nota mostrata dentro il nodo, sotto il contenuto (8 px sotto, margini laterali 16).
    qreal noteWidth = 0, noteHeight = 0;
    if (withNote && m_notePreview) {
        noteWidth = m_notePreviewKey.width + 2 * kNotePreviewPaddingPx;
        noteHeight = m_notePreviewHeight;
        inner = std::max(inner, noteWidth + 2 * padX());
    }
    const qreal extra = noteHeight > 0 ? noteHeight + padY() : 0;
    ContentLayout layout;
    layout.box = {-(task + inner + links) / 2, -(height + extra) / 2, task + inner + links, height + extra};
    const QRectF &box = layout.box;
    layout.content = {box.left(), box.top(), box.width(), height};
    const QRectF &content = layout.content;
    const qreal left0 = box.left() + task;  // inizio del contenuto (immagine e testo)
    const qreal centerX = left0 + inner / 2;
    if (noteHeight > 0) layout.note = {centerX - noteWidth / 2, content.bottom(), noteWidth, noteHeight};
    if (!m_hasImage) {
        layout.text = {left0, content.top(), inner, height};
    } else if (!hasText) {
        layout.image = {centerX - image.width() / 2, content.center().y() - image.height() / 2, image.width(), image.height()};
    } else if (side) {
        // Il margine sinistro del blocco del testo (16 px) copre lo spazio di 8 px dall'immagine.
        const qreal textWidth = inner - image.width() - kImageGapPx;
        const bool left = m_node->imagePlacement == MindNode::ImagePlacement::Left;
        const qreal imageX = left ? left0 + padX() : left0 + textWidth - padX() + kImageGapPx;
        layout.image = {imageX, content.center().y() - image.height() / 2, image.width(), image.height()};
        layout.text = {left ? layout.image.right() + kImageGapPx - padX() : left0, content.center().y() - block.height() / 2,
                       textWidth, block.height()};
    } else {
        const bool above = m_node->imagePlacement == MindNode::ImagePlacement::Above;
        const qreal imageY = above ? content.top() + padY() : content.top() + block.height();
        layout.image = {centerX - image.width() / 2, imageY, image.width(), image.height()};
        layout.text = {centerX - block.width() / 2, above ? layout.image.bottom() : content.top(), block.width(), block.height()};
    }
    return layout;
}

qreal NodeItem::linkZoneWidth() const {
    // Colonna a destra: nota (FEAT-059) e collegamenti (FEAT-027).
    const int count = int(!m_node->topicLink.isEmpty()) + int(!m_node->webLink.isEmpty()) + int(hasNote(m_node) && !showAllNotes())
                      + int(!m_node->files.isEmpty());  // FEAT-049
    return count == 0 ? 0 : kLinkTextGapPx + count * kLinkButtonPx + (count - 1) * kLinkIconGapPx;
}

qreal NodeItem::manualTextWidth() const {
    // FEAT-037: un'immagine a sinistra o a destra toglie la sua larghezza (piu' lo spazio) al testo.
    const qreal sideImage = hasSideImage() ? m_node->imageSize.width() + kImageGapPx : 0;
    return std::max<qreal>(1, layoutCustomWidth() - linkZoneWidth() - taskZoneWidth() - sideImage);
}

qreal NodeItem::taskZoneWidth() const {
    // Come i collegamenti: 8 px dal contenuto e un bottone di 22 (piu' 4 fra due bottoni); il margine
    // di 16 px del testo fa il resto. FEAT-051: prima i marcatori, poi la casella.
    const int count = int(m_node->markers.size()) + int(m_node->task != MindNode::Task::None);
    return count == 0 ? 0 : kLinkTextGapPx + count * kLinkButtonPx + (count - 1) * kLinkIconGapPx;
}

QList<QRectF> NodeItem::markerRects() const {
    QList<QRectF> rects;
    if (m_node->markers.isEmpty()) return rects;
    const QRectF box = textRect();
    if (box.isEmpty()) return rects;
    const qreal y = contentLayout().content.center().y() - kLinkButtonPx / 2;
    for (int i = 0; i < m_node->markers.size(); ++i)
        rects.append({box.left() + padX() + i * (kLinkButtonPx + kLinkIconGapPx), y, kLinkButtonPx, kLinkButtonPx});
    return rects;
}

int NodeItem::markerAt(const QPointF &position) const {
    if (m_editor) return -1;
    const QList<QRectF> rects = markerRects();
    for (int i = 0; i < rects.size(); ++i)
        if (rects[i].contains(position)) return i;
    return -1;
}

QRectF NodeItem::taskRect() const {
    if (m_node->task == MindNode::Task::None) return {};
    const QRectF box = textRect();
    if (box.isEmpty()) return {};
    const qreal markers = m_node->markers.size() * (kLinkButtonPx + kLinkIconGapPx);  // FEAT-051: dopo i marcatori
    return {box.left() + padX() + markers, contentLayout().content.center().y() - kLinkButtonPx / 2, kLinkButtonPx, kLinkButtonPx};
}

QRectF NodeItem::textArea() const { return contentLayout().text; }

QRectF NodeItem::linkIconRect(LinkKind kind) const {
    const bool topic = !m_node->topicLink.isEmpty(), web = !m_node->webLink.isEmpty();
    if ((kind == LinkKind::Topic && !topic) || (kind == LinkKind::Web && !web)) return {};
    const QRectF box = textRect();
    if (box.isEmpty()) return {};
    // Da destra: margine del testo, la graffetta dei file (FEAT-049), poi l'icona web, poi (piu' a
    // sinistra) quella dell'argomento.
    qreal right = box.right() - padX() - (m_node->files.isEmpty() ? 0 : kLinkButtonPx + kLinkIconGapPx);
    if (kind == LinkKind::Topic && web) right -= kLinkButtonPx + kLinkIconGapPx;
    const qreal centerY = contentLayout().content.center().y();  // FEAT-059: non sulla nota mostrata
    return {right - kLinkButtonPx, centerY - kLinkButtonPx / 2, kLinkButtonPx, kLinkButtonPx};
}

QRectF NodeItem::noteRect() const {
    if (!hasNote(m_node) || showAllNotes()) return {};  // nota mostrata: niente icona
    const QRectF box = textRect();
    if (box.isEmpty()) return {};
    // Prima dei collegamenti: da destra, margine, poi i bottoni dei collegamenti presenti, poi la nota.
    const int links = int(!m_node->topicLink.isEmpty()) + int(!m_node->webLink.isEmpty()) + int(!m_node->files.isEmpty());
    const qreal right = box.right() - padX() - links * (kLinkButtonPx + kLinkIconGapPx);
    return {right - kLinkButtonPx, contentLayout().content.center().y() - kLinkButtonPx / 2, kLinkButtonPx, kLinkButtonPx};
}

QRectF NodeItem::formulaRect() const { return m_node->formula.isEmpty() ? QRectF() : imageRect(); }

QRectF NodeItem::filesRect() const {
    if (m_node->files.isEmpty()) return {};
    const QRectF box = textRect();
    if (box.isEmpty()) return {};
    const qreal right = box.right() - padX();
    return {right - kLinkButtonPx, contentLayout().content.center().y() - kLinkButtonPx / 2, kLinkButtonPx, kLinkButtonPx};
}

std::optional<NodeItem::LinkKind> NodeItem::linkAt(const QPointF &position) const {
    if (m_editor) return std::nullopt;
    for (const LinkKind kind : {LinkKind::Topic, LinkKind::Web}) {
        const QRectF button = linkIconRect(kind);
        if (!button.isEmpty() && button.contains(position)) return kind;
    }
    return std::nullopt;
}

void NodeItem::setTopicLinkBroken(bool broken) {
    if (m_topicLinkBroken == broken) return;
    m_topicLinkBroken = broken;
    update();
}

NodeItem::~NodeItem() {
    // A topic deleted while its text is being edited (CI, Qt 6.8): the format bar, its child, closes
    // its lists while it is destroyed and popupClosed gave the focus back to the dying editor.
    if (m_formatBar) {
        if (auto *bar = static_cast<TextFormatBar *>(m_formatBar->widget())) {
            bar->applyFormat = nullptr; bar->applyHighlight = nullptr; bar->popupClosed = nullptr; bar->themeColors = nullptr;
        }
    }
}

// Like the selected topic's bar (TopicTools, user request 2026-10-25): centred on the topic's shape,
// its frame 8 px above it, the same at every zoom. The proxy ignores the view's scale, so its anchor
// is the shape's top centre and the offset in screen pixels goes in its own transform (before, the
// pixel offset was in topic coordinates: off-centre away from 100%, and measured from the text).
void NodeItem::placeFormatBar() {
    constexpr qreal kBarGapPx = 8;  // TopicTools::kBarGapPx
    const QSizeF bar = m_formatBar->widget()->size();
    const QRectF shape = shapeRect();
    m_formatBar->setPos(shape.center().x(), shape.top());
    m_formatBar->setTransform(QTransform::fromTranslate(-bar.width() / 2, -bar.height() - kBarGapPx));
}

qreal NodeItem::cornerRadius() const { return m_node->summaryTopic ? Summaries::kTopicRadiusPx : -1; }

qreal NodeItem::padX() const { return NodeStyle::textPadding(m_node).x(); }

qreal NodeItem::padY() const { return NodeStyle::textPadding(m_node).y(); }

// Area the plain title is drawn in: the padding minus 5 px across and 2 px down (with the old 16x8
// padding it was 11x6), so antialiased edges are not cut.
qreal NodeItem::textClipX() const { return std::max<qreal>(0, padX() - 5); }

qreal NodeItem::textClipY() const { return std::max<qreal>(0, padY() - 2); }

qreal NodeItem::maxTextNodeWidth() const { return m_node && !m_node->parent ? kMaxCentralNodeWidth : kMaxTextNodeWidth; }

qreal NodeItem::plainWrapWidth(qreal boxWidth) const {
    // Automatica: righe larghe quanto il riquadro meno il padding, con 10 px di gioco (P5, come
    // drawText prima: con il padding di 16 era riquadro - 22). Scelta dall'utente: meno il padding
    // intero, così il testo ha sempre lo stesso margine e una riga lunga non si avvicina al bordo.
    return std::max<qreal>(1, boxWidth - (layoutCustomWidth() > 0 ? 2 * padX() : 2 * padX() - 10));
}

int NodeItem::resizeSideAt(const QPointF &position) const {
    if (m_editor || m_draggingImage) return 0;
    const QRectF box = shapeRect();
    if (box.isEmpty() || position.y() < box.top() || position.y() > box.bottom()) return 0;
    // Zona sensibile: 4 px fuori dal bordo (il margine di normalBoundingRect) e 5 dentro.
    if (position.x() >= box.left() - 4 && position.x() <= box.left() + 5) return -1;
    if (position.x() >= box.right() - 5 && position.x() <= box.right() + 4) return 1;
    return 0;
}

qreal NodeItem::minimumCustomWidth() const {
    // Si impagina il titolo a larghezza 1: ogni riga contiene un solo carattere (Qt mette sempre
    // almeno un carattere per riga), e la riga più larga è il carattere più largo.
    const QFont font = nodeFont(m_node);
    qreal content = 0;
    if (!m_formula.isEmpty()) {
        FormulaText narrow;
        narrow.update(shownTitle(), font, m_node->text, 1);
        content = narrow.size().width();
    } else if (m_richDocument) {
        QTextDocument narrow;
        narrow.setDocumentMargin(0);
        narrow.setDefaultFont(font);
        narrow.setHtml(RichTitle::withoutDocumentFont(m_node->titleHtml));
        prependNumber(narrow);
        narrow.setDefaultTextOption(m_richDocument->defaultTextOption());
        narrow.setTextWidth(1);
        content = narrow.idealWidth();
    } else {
        const QString plain = shownTitle().replace(QStringLiteral("\\$"), QStringLiteral("$"));
        content = plainTextLayout(plain, font, 1, nullptr).width();
    }
    // FEAT-027: le icone restano; FEAT-037: l'immagine non si stringe trascinando il bordo.
    const qreal text = std::ceil(content) + 2 * padX();
    qreal inner = text;
    if (m_hasImage) {
        const qreal image = m_node->imageSize.width();
        if (m_node->title.trimmed().isEmpty()) inner = image + 2 * padX();
        else if (hasSideImage()) inner = image + kImageGapPx + text;
        else inner = std::max(text, image + 2 * padX());
    }
    return inner + linkZoneWidth() + taskZoneWidth();
}

qreal NodeItem::anchorHalfWidth() const {
    const QRectF shape = shapeRect();
    return shape.isEmpty() ? 105.0 : shape.width() / 2;
}

QRectF NodeItem::shapeRect() const {
    const QRectF content = textRect();
    return NodeShapes::outerRect(m_node->shape, content, m_node->borderWidth, std::max<qreal>(0, content.width() - 2 * padX()));
}

QPointF NodeItem::branchAnchor(bool right) const {
    const QRectF shape = shapeRect();
    if (shape.isEmpty()) return {right ? 105.0 : -105.0, 0};
    return NodeShapes::branchAnchor(m_node->shape, shape, m_node->borderWidth, right);
}

qreal NodeItem::layoutHeight() const {
    // FEAT-060: il layout tratta il nodo come centrato sulla sua altezza: lo spazio delle etichette
    // sotto la forma si conta sopra e sotto, cosi' non coprono il nodo seguente.
    return normalBoundingRect().height() + 2 * labelsBlockHeight();
}

QRectF NodeItem::imageRect() const {
    // FEAT-037: l'immagine sta dentro il nodo (prima fuori dalla forma, a 10 px dal riquadro).
    return m_hasImage ? contentLayout().image : QRectF();
}

QRectF NodeItem::normalBoundingRect() const {
    QRectF bounds = shapeRect();
    if (!imageRect().isEmpty()) bounds = bounds.isEmpty() ? imageRect() : bounds.united(imageRect());
    if (bounds.isEmpty()) bounds = {-105, -34, 210, 68};
    return bounds.adjusted(-4, -4, 4, 4);
}

QList<QRectF> NodeItem::imageDropZones() const {
    // Nell'ordine di MindNode::ImagePlacement. Come nella foto 3 (dell'utente, 2026-10-17) il nodo
    // stesso si divide: a sinistra e a destra due strisce larghe un quarto, alte quanto il nodo; in
    // mezzo la meta' alta (Sopra) e quella bassa (Sotto). Prima erano quattro riquadri attorno al
    // nodo con il nome della posizione.
    const QRectF box = textRect();
    const qreal side = box.width() / 4;
    const qreal middle = box.width() - 2 * side;
    const qreal half = box.height() / 2;
    return {{box.left() + side, box.top(), middle, half}, {box.left() + side, box.top() + half, middle, half},
            {box.left(), box.top(), side, box.height()}, {box.right() - side, box.top(), side, box.height()}};
}

int NodeItem::imageDropZoneAt(const QPointF &position) const {
    const QList<QRectF> zones = imageDropZones();
    for (int i = 0; i < zones.size(); ++i) if (zones[i].contains(position)) return i;
    return -1;
}

int NodeItem::imageHandleAt(const QPointF &position) const {
    if (!m_imageSelected || !isSelected()) return 0;
    const QRectF image = imageRect();
    if (image.isEmpty()) return 0;
    const QPointF corners[] = {image.topLeft(), image.topRight(), image.bottomRight(), image.bottomLeft()};
    for (int i = 0; i < 4; ++i)
        if (QLineF(corners[i], position).length() <= kImageHandlePx) return i + 1;
    return 0;
}

const NodeItem::Sketch &NodeItem::sketchFor(const QRectF &shapeBox) {
    const SketchKey key{shapeBox, static_cast<int>(m_node->shape), static_cast<int>(m_node->fillStyle),
                        static_cast<int>(m_node->borderStyle), m_node->borderWidth};
    if (m_sketch.valid && m_sketch.key == key) return m_sketch;
    // Seme dall'id del nodo: lo stesso tratto a ogni ridisegno e dopo la riapertura del file.
    const quint64 seed = NodeSketch::seedFor(m_node->id);
    const QPainterPath outline = NodeShapes::outline(m_node->shape, shapeBox, cornerRadius());
    m_sketch = {key, true, {}, {}, {}, {}};
    // Il bordo si tiene gia' tracciato (contorno del tratto): paint() lo riempie soltanto, invece di
    // ricalcolare il tratto a ogni ridisegno.
    auto stroked = [](const QPainterPath &path, qreal width, Qt::PenStyle style) {
        QPainterPathStroker stroker;
        stroker.setWidth(width);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        stroker.setDashPattern(style);
        // Curve gia' spezzate in segmenti: il rasterizzatore non deve farlo a ogni ridisegno.
        QPainterPath flat;
        flat.setFillRule(Qt::WindingFill);
        for (const QPolygonF &polygon : stroker.createStroke(path).toSubpathPolygons()) flat.addPolygon(polygon);
        return flat;
    };
    if (m_node->fillStyle == NodeFillStyle::PienoManoLibera) m_sketch.area = NodeSketch::roughArea(outline, seed);
    else if (NodeStyle::isPatternFill(m_node->fillStyle))
        m_sketch.pattern = NodeSketch::pattern(m_node->fillStyle, outline, seed);
    if (NodeSketch::isFreehand(m_node->borderStyle)) {
        QPainterPath line;
        if (NodeShapes::hasOpenBorder(m_node->shape)) {
            quint64 lineSeed = seed;
            for (const QLineF &underline : NodeShapes::borderLines(m_node->shape, shapeBox, m_node->borderWidth))
                line.addPath(NodeSketch::roughLine(underline, lineSeed++));
            for (const QPainterPath &mark : NodeShapes::marks(m_node->shape, shapeBox))  // FEAT-030 stage B
                line.addPath(NodeSketch::roughPath(mark, lineSeed++));
        } else {
            line = NodeSketch::roughOutline(outline, seed);
            // Il riempimento pieno segue la stessa linea del bordo, cosi' gli angoli della forma
            // regolare non escono dal tratto irregolare.
            m_sketch.borderArea = line;
            m_sketch.borderArea.closeSubpath();
            // FEAT-030: the lines inside the shape are freehand too.
            quint64 innerSeed = seed + 1;
            for (const QPainterPath &inner : NodeShapes::innerLines(m_node->shape, shapeBox, cornerRadius())) {
                // Closed lines (a fold, a card, a circle) go round; open ones (bars, an arc) do not.
                const bool closed = inner.elementCount() > 2 && QPointF(inner.elementAt(0)) == QPointF(inner.elementAt(inner.elementCount() - 1));
                line.addPath(closed ? NodeSketch::roughOutline(inner, innerSeed++) : NodeSketch::roughPath(inner, innerSeed++));
            }
        }
        m_sketch.border = stroked(line, m_node->borderWidth, NodeStyle::borderPenStyle(m_node->borderStyle));
    }
    return m_sketch;
}

void NodeItem::setCollapseControls(const CollapseControl &left, const CollapseControl &right) {
    if (left == m_collapseLeft && right == m_collapseRight) return;
    prepareGeometryChange();
    m_collapseLeft = left;
    m_collapseRight = right;
    update();
}

QRectF NodeItem::collapseRect(int side) const { return collapseRect(side, false); }

QRectF NodeItem::collapseRect(int side, bool evenIfHidden) const {
    const CollapseControl &control = side < 0 ? m_collapseLeft : m_collapseRight;
    // Vuoto solo sul nodo selezionato; con i figli nascosti sempre (FEAT-016, "Regole UI").
    if (!control.shown || (!evenIfHidden && control.hidden == 0 && !isSelected())) return {};
    const bool onCell = m_cell && !m_cell->holdsBranch;
    const QRectF shape = onCell ? m_cell->rect : shapeRect();  // FEAT-028: on the cell's edge
    if (control.vertical) {
        const qreal centreX = shape.center().x();
        if (control.hidden == 0) {
            const qreal centreY = side > 0 ? shape.bottom() + control.reach : shape.top() - control.reach;
            return {centreX - kCollapseOpenSizePx / 2, centreY - kCollapseOpenSizePx / 2, kCollapseOpenSizePx, kCollapseOpenSizePx};
        }
        QFont font;
        font.setPixelSize(12);
        const qreal width = std::max(kCollapseSizePx, QFontMetricsF(font).horizontalAdvance(QString::number(control.hidden)) + 8);
        const qreal y = side > 0 ? shape.bottom() + kCollapseGapPx : shape.top() - kCollapseGapPx - kCollapseSizePx;
        return {centreX - width / 2, y, width, kCollapseSizePx};
    }
    if (control.hidden == 0) {
        // Open branch: a small circle on the point where the branches meet (FEAT-072,
        // live 2026-10-22: about 11 px, 12 px from the side; before 18 px, 4 px from the side).
        // FEAT-028: in a table the children's cells start at the edge, so the circle sits on it.
        const qreal offset = onCell ? 0 : kCollapseOpenOffsetPx;
        const qreal centreX = side > 0 ? shape.right() + offset : shape.left() - offset;
        return {centreX - kCollapseOpenSizePx / 2, shape.center().y() - kCollapseOpenSizePx / 2, kCollapseOpenSizePx, kCollapseOpenSizePx};
    }
    qreal width = kCollapseSizePx;
    if (control.hidden > 0) {
        QFont font;
        font.setPixelSize(12);
        width = std::max(width, QFontMetricsF(font).horizontalAdvance(QString::number(control.hidden)) + 8);
    }
    const qreal x = side > 0 ? shape.right() + kCollapseGapPx : shape.left() - kCollapseGapPx - width;
    return {x, shape.center().y() - kCollapseSizePx / 2, width, kCollapseSizePx};
}

void NodeItem::setTableCell(const std::optional<TableCell> &cell) {
    if (m_cell == cell) return;
    prepareGeometryChange();
    m_cell = cell;
    if (!m_editor) setZValue(restingZ());
    update();
}

void NodeItem::paintCollapseControls(QPainter *p) {
    for (const int side : {-1, 1}) {
        const QRectF rect = collapseRect(side);
        if (rect.isEmpty()) continue;
        const CollapseControl &control = side < 0 ? m_collapseLeft : m_collapseRight;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        // Fondo della mappa: i rami dei figli passano sotto il controllo.
        p->setBrush(scene() ? scene()->backgroundBrush() : QBrush(Qt::white));
        p->setPen(QPen(control.color, 1.5));
        if (control.hidden > 0 && control.vertical) {
            const qreal edge = side > 0 ? shapeRect().bottom() : shapeRect().top();
            p->drawLine(QPointF(rect.center().x(), edge), QPointF(rect.center().x(), side > 0 ? rect.top() : rect.bottom()));
        } else if (control.hidden > 0) {  // ramo chiuso: un tratto lega il nodo al numero (foto di FEAT-016)
            const QRectF shape = m_cell && !m_cell->holdsBranch ? m_cell->rect : shapeRect();
            const qreal edge = side > 0 ? shape.right() : shape.left();
            p->drawLine(QPointF(edge, rect.center().y()), QPointF(side > 0 ? rect.left() : rect.right(), rect.center().y()));
        }
        p->drawRoundedRect(rect.adjusted(0.75, 0.75, -0.75, -0.75), rect.height() / 2 - 0.75, rect.height() / 2 - 0.75);
        if (control.hidden > 0) {
            QFont font = p->font();
            font.setPixelSize(12);
            p->setFont(font);
            p->drawText(rect, Qt::AlignCenter, QString::number(control.hidden));
        }
        p->restore();
    }
}

void NodeItem::paint(QPainter *p, const QStyleOptionGraphicsItem *option, QWidget *) {
    paintCollapseControls(p);  // FEAT-016: fuori dalla forma, quindi l'ordine non conta
    if (m_imageLoadPending) {
        loadImage();
        // FEAT-039 (prima B9 toglieva lo spazio): file mancante o rovinato, l'immagine tiene il suo
        // posto e si disegna un segnaposto "Immagine non disponibile".
        m_imageMissing = m_image.isNull() && m_hasImage;
    }
    p->setRenderHint(QPainter::Antialiasing);
    // (2026-10-13: la selezione ora è l'anello esterno disegnato qui sotto; il bordo del nodo
    // resta lo stesso anche da selezionato, come nelle foto d'esempio)
    // FEAT-020: tipo e spessore del bordo; "Nessuno" non disegna il bordo, "Nessun riempimento"
    // lascia vedere lo sfondo della mappa.
    QPen pen(m_node->border, m_node->borderWidth, NodeStyle::borderPenStyle(m_node->borderStyle));
    if (m_node->border.alpha() == 0) pen = Qt::NoPen;
    const QBrush brush = m_node->fill.alpha() == 0 ? QBrush(Qt::NoBrush) : QBrush(m_node->fill);
    // FEAT-027: il testo si dispone nel riquadro senza la colonna delle icone dei collegamenti.
    const QRectF textBox = textArea();
    const QRectF shapeBox = shapeRect();
    // FEAT-020 parte B: forma (riempimento e bordo) e, per le linee sotto, le linee nel colore e
    // nello spessore del bordo. Il contorno si ricava ogni volta: costa poco (pochi punti).
    // Parte C: motivi e tratti a mano libera dalla cache (sketchFor), solo per quei tipi.
    auto drawShape = [&] {
        if (m_cell) {  // FEAT-028: the cell instead of the shape; the pen on the edges, shared with the neighbours
            if (!m_cell->frame.isNull()) {  // stage 2: the outer table's cell around this table
                p->setPen(QPen(m_cell->frameBorder, kTableBorderPx, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
                p->setBrush(m_cell->frameFill);
                p->drawRect(m_cell->frame);
            }
            p->setPen(QPen(m_cell->border, kTableBorderPx, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
            p->setBrush(m_cell->fill);
            p->drawRect(m_cell->rect);
            // The ring inside the cell, over its fill: the neighbouring cells would cover it outside
            // (the reference draws it on the edge).
            const auto &style = selectionStyle();
            const bool selected = option->state & QStyle::State_Selected;
            if ((selected || (m_hovered && style.hoverEnabled)) && !m_draggingImage) {
                const qreal inset = style.gap + kTableBorderPx / 2 + style.width / 2;
                p->setPen(QPen(selected ? style.selectedColor : style.hoverColor, style.width));
                p->setBrush(Qt::NoBrush);
                p->drawRect(m_cell->rect.adjusted(inset, inset, -inset, -inset));
            }
            return;
        }
        if (shapeBox.isEmpty()) return;
        // FEAT-030 stage B: the Puntuale shapes are open too, but their fill (any style) covers the rectangle.
        const bool underline = NodeShapes::isUnderline(m_node->shape);
        const bool open = NodeShapes::hasOpenBorder(m_node->shape);
        const bool freehandBorder = pen.style() != Qt::NoPen && NodeSketch::isFreehand(m_node->borderStyle);
        const bool sketchedFill = brush.style() != Qt::NoBrush && !underline
                                  && (m_node->fillStyle == NodeFillStyle::PienoManoLibera || NodeStyle::isPatternFill(m_node->fillStyle));
        const QPainterPath filledMarks = pen.style() == Qt::NoPen ? QPainterPath() : NodeShapes::filledMarks(m_node->shape, shapeBox);
        const Sketch *sketch = freehandBorder || sketchedFill ? &sketchFor(shapeBox) : nullptr;
        if (sketchedFill) {
            if (m_node->fillStyle == NodeFillStyle::PienoManoLibera) {
                p->fillPath(sketch->area, brush);
            } else {
                const QColor color = NodeSketch::opaqueOver(m_node->fill, scene() ? scene()->backgroundBrush().color() : NodeStyle::kMapBackground);  // FEAT-036
                p->setPen(QPen(color, sketch->pattern.penWidth, Qt::SolidLine, Qt::FlatCap));
                p->drawLines(sketch->pattern.lines);
            }
        }
        if (freehandBorder) {
            if (!sketchedFill && !underline && brush.style() != Qt::NoBrush)
                p->fillPath(open ? NodeShapes::outline(m_node->shape, shapeBox) : sketch->borderArea, brush);
            p->fillPath(sketch->border, pen.color());
            p->fillPath(filledMarks, pen.color());
            return;
        }
        p->setPen(open ? QPen(Qt::NoPen) : pen);
        p->setBrush(sketchedFill ? QBrush(Qt::NoBrush) : brush);
        p->drawPath(NodeShapes::outline(m_node->shape, shapeBox, cornerRadius()));
        if (pen.style() == Qt::NoPen) return;
        p->setPen(pen);
        for (const QLineF &line : NodeShapes::borderLines(m_node->shape, shapeBox, m_node->borderWidth)) p->drawLine(line);
        p->setBrush(Qt::NoBrush);
        for (const QPainterPath &line : NodeShapes::innerLines(m_node->shape, shapeBox, cornerRadius())) p->drawPath(line);
        for (const QPainterPath &mark : NodeShapes::marks(m_node->shape, shapeBox)) p->drawPath(mark);
        p->fillPath(filledMarks, pen.color());
    };
    // Anello di selezione o di hover (richiesta 2026-10-13). Gira attorno a tutto il nodo
    // (testo, immagine, allegato), a "gap" px dal riquadro, con raggio 8 + distanza dal bordo
    // (raggi concentrici, AGENTS.md "Raggi degli angoli"). Resta anche durante la modifica del
    // testo; non si disegna mentre si trascina un'immagine.
    {
        const auto &style = selectionStyle();
        const bool selected = option->state & QStyle::State_Selected;
        const bool hovered = m_hovered && style.hoverEnabled && !selected;
        if ((selected || hovered) && !m_draggingImage && !m_cell) {  // FEAT-028: a cell draws it with drawShape
            const qreal inset = style.gap + borderOutside(m_node) + style.width / 2;
            const QRectF ring = normalBoundingRect().adjusted(4 - inset, 4 - inset, inset - 4, inset - 4);
            p->save();
            p->setPen(QPen(selected ? style.selectedColor : style.hoverColor, style.width));
            p->setBrush(Qt::NoBrush);
            // FEAT-020 parte B: l'anello segue il contorno della forma (stessa forma, allargata);
            // con un'immagine o con le forme rettangolari resta il rettangolo arrotondato attorno
            // a tutto il nodo.
            // FEAT-037: l'immagine sta dentro la forma, quindi l'anello segue la forma anche con lei.
            // FEAT-030: around a connector's mark and its text below, the rounded rectangle too.
            const bool roundedRing = m_node->shape == NodeShape::RettangoloArrotondato || NodeShapes::hasOpenBorder(m_node->shape)
                                     || NodeShapes::hasTextBelow(m_node->shape) || shapeBox.isEmpty();
            if (roundedRing) p->drawRoundedRect(ring, kSelectionRadius, kSelectionRadius);
            else p->drawPath(NodeShapes::outline(m_node->shape, shapeBox.adjusted(-inset, -inset, inset, inset), cornerRadius()));
            p->restore();
        }
    }
    // P6 (2026-10-13): con lo zoom basso solo riquadro, bordo e immagine, senza testo e formule
    // (tecnica dell'esempio "40000 Chips" di Qt, QStyleOptionGraphicsItem::levelOfDetailFromTransform).
    // FEAT-054: an export (and its small preview) always draws the text.
    const auto *mapScene = qobject_cast<const MindMapScene *>(scene());
    if (QStyleOptionGraphicsItem::levelOfDetailFromTransform(p->worldTransform()) < Performance::detailZoomThreshold()
        && !(mapScene && mapScene->exportImageScale() > 0) && !m_editor && !m_draggingImage) {
        drawShape();
        if (!m_image.isNull() && m_hasImage) p->drawPixmap(imageRect(), m_image, m_image.rect());
        return;
    }
    if (!shapeBox.isEmpty()) {
        drawShape();
        p->setPen(m_node->text);
        p->setFont(nodeFont(m_node));  // FEAT-022
        // Le formule possono stare in mezzo al testo e sono vere formule (frazioni,
        // radici, integrali, matrici...). \$ resta un dollaro letterale.
        // In modifica il testo lo mostra il campo sopra il riquadro.
        if (m_editor || textBox.isEmpty()) {
        } else if (!m_formula.isEmpty()) {
            // FEAT-022: centro e destra anche per le formule (giustificato = sinistra).
            const qreal inset = textLeftInset(textBox.width(), m_formula.size().width(), padX());
            const Qt::Alignment horizontal = textAlignment(m_node) == Qt::AlignJustify ? Qt::AlignLeft : textAlignment(m_node);
            m_formula.draw(p, textBox.adjusted(inset, 6, horizontal == Qt::AlignLeft ? -11 : -inset, -6), horizontal | Qt::AlignVCenter);
        }
        else if (m_richDocument) {
            const QRectF target = textBox.adjusted(padX(), padY(), -padX(), -padY());
            const QSizeF size = m_richDocument->size();
            p->save();
            p->setClipRect(target);
            // (richiesta 2026-10-13: testo a sinistra; il documento è già allineato a sinistra al suo interno)
            p->translate(target.left(), target.center().y() - size.height() / 2);
            // (bug 2026-10-12: drawContents usa la palette dell'app per il testo senza colore
            // proprio; con Windows in modalità scura è bianca e il titolo formattato diventava
            // bianco. Ora il colore predefinito è quello del nodo, come per il titolo semplice.)
            QAbstractTextDocumentLayout::PaintContext context;
            context.palette.setColor(QPalette::Text, m_node->text);
            context.clip = QRectF(QPointF(), size);
            m_richDocument->documentLayout()->draw(p, context);
            p->restore();
        } else if (m_plainLayout && std::abs(plainWrapWidth(textBox.width()) - m_plainLayoutSize.width()) < 0.01 &&
                   [&] {
                       if (p->device() != m_metricsDevice) {
                           m_metricsDevice = p->device();
                           m_metricsDeviceMatches = QFontMetricsF(p->font()).height() == m_plainLayoutFontHeight;
                       }
                       return m_metricsDeviceMatches;
                   }()) {
            // P5: testo già impaginato, qui solo disegnato come fa qt_format_text(). Le due
            // condizioni sopra fanno ripiegare su drawText se riquadro o font del dispositivo
            // (es. una stampante con altri DPI) non sono quelli usati per l'impaginazione.
            const QRectF target = textBox.adjusted(textClipX(), textClipY(), -textClipX(), -textClipY());
            // Come drawText: si smette di disegnare righe quando il riquadro è pieno.
            int lineCount = 0;
            qreal height = 0, width = 0;
            while (lineCount < m_plainLayout->lineCount()) {
                const QTextLine line = m_plainLayout->lineAt(lineCount++);
                height = line.y() + line.ascent() + line.descent();
                width = std::max(width, line.naturalTextWidth());
                if (height >= target.height()) break;
            }
            const qreal yoff = (target.height() - height) / 2;
            // Richiesta 2026-10-13: righe allineate a sinistra (prima centrate una per una).
            // FEAT-022: centro e destra spostano il blocco dentro il riquadro, che puo' essere piu'
            // largo del testo (larghezza scelta a mano o minima), con lo stesso margine.
            const qreal inset = textLeftInset(textBox.width(), width, padX());
            qreal left = textBox.left() + inset;
            if (m_node->textAlign == TextAlign::Centro) left = textBox.center().x() - width / 2;
            else if (m_node->textAlign == TextAlign::Destra) left = textBox.right() - inset - width;
            // Senza Qt::TextDontClip drawText taglia ai bordi il testo che non ci sta (una parola
            // lunghissima senza spazi). Il ritaglio serve solo in quel caso.
            const bool clip = !target.contains(QRectF(left, target.y() + yoff, width, height));
            if (clip) { p->save(); p->setClipRect(target, Qt::IntersectClip); }
            // FEAT-022: centro e destra spostano ogni riga dentro il blocco largo quanto la piu' lunga.
            const qreal factor = m_node->textAlign == TextAlign::Centro ? 0.5 : m_node->textAlign == TextAlign::Destra ? 1.0 : 0.0;
            for (int i = 0; i < lineCount; ++i) {
                const QTextLine line = m_plainLayout->lineAt(i);
                line.draw(p, QPointF(left + (width - line.naturalTextWidth()) * factor, target.y() + yoff));
            }
            if (clip) p->restore();
        } else {
            // Ripiego (dispositivo con altri DPI): stesso a capo del titolo impaginato.
            QTextOption option(textAlignment(m_node) | Qt::AlignVCenter);
            option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            p->drawText(textBox.adjusted(textClipX(), textClipY(), -textClipX(), -textClipY()),
                        shownTitle().replace(QStringLiteral("\\$"), QStringLiteral("$")), option);
        }
        // FEAT-027: bottoni dei collegamenti come i gruppi flottanti (AGENTS.md: fondo bianco
        // semitrasparente, bordo 1 px rgba(15,23,42,13), qui raggio 4); al passaggio del mouse
        // rgba(15,23,42,15) sopra il fondo. Icona #27303b (leggibile anche sui nodi scuri),
        // grigia #6b7280 se l'argomento non c'e' piu'.
        for (const LinkKind kind : {LinkKind::Topic, LinkKind::Web}) {
            const QRectF button = linkIconRect(kind);
            if (button.isEmpty()) continue;
            const QRectF frame = button.adjusted(0.5, 0.5, -0.5, -0.5);
            p->setPen(QPen(QColor(15, 23, 42, 13), 1));
            p->setBrush(QColor(255, 255, 255, 214));
            p->drawRoundedRect(frame, kLinkButtonRadiusPx, kLinkButtonRadiusPx);
            if (m_hoveredLink == kind) {
                p->setPen(Qt::NoPen);
                p->setBrush(QColor(15, 23, 42, 15));
                p->drawRoundedRect(frame, kLinkButtonRadiusPx, kLinkButtonRadiusPx);
            }
            const QColor color = kind == LinkKind::Topic && m_topicLinkBroken ? QColor("#6b7280") : QColor("#27303b");
            const QPixmap pixmap = linkIconPixmap(kind == LinkKind::Topic, color);
            const qreal inset = (kLinkButtonPx - kLinkIconPx) / 2;
            p->drawPixmap(button.adjusted(inset, inset, -inset, -inset), pixmap, QRectF(pixmap.rect()));
        }
        // FEAT-059: bottone della nota, con lo stesso aspetto dei collegamenti.
        if (const QRectF button = noteRect(); !button.isEmpty()) {
            const QRectF frame = button.adjusted(0.5, 0.5, -0.5, -0.5);
            p->setPen(QPen(QColor(15, 23, 42, 13), 1));
            p->setBrush(QColor(255, 255, 255, 214));
            p->drawRoundedRect(frame, kLinkButtonRadiusPx, kLinkButtonRadiusPx);
            if (m_hoveredNote) {
                p->setPen(Qt::NoPen);
                p->setBrush(QColor(15, 23, 42, 15));
                p->drawRoundedRect(frame, kLinkButtonRadiusPx, kLinkButtonRadiusPx);
            }
            const QPixmap pixmap = tintedIconPixmap(QStringLiteral(":/assets/hugeicons/note.svg"), QColor("#27303b"));
            const qreal inset = (kLinkButtonPx - kLinkIconPx) / 2;
            p->drawPixmap(button.adjusted(inset, inset, -inset, -inset), pixmap, QRectF(pixmap.rect()));
        }
        // FEAT-049: graffetta dei file, come i collegamenti; con due o piu' file il numero in un
        // piccolo tondo scuro in basso a destra.
        if (const QRectF button = filesRect(); !button.isEmpty()) {
            const QRectF frame = button.adjusted(0.5, 0.5, -0.5, -0.5);
            p->setPen(QPen(QColor(15, 23, 42, 13), 1));
            p->setBrush(QColor(255, 255, 255, 214));
            p->drawRoundedRect(frame, kLinkButtonRadiusPx, kLinkButtonRadiusPx);
            if (m_hoveredFiles) {
                p->setPen(Qt::NoPen);
                p->setBrush(QColor(15, 23, 42, 15));
                p->drawRoundedRect(frame, kLinkButtonRadiusPx, kLinkButtonRadiusPx);
            }
            const QPixmap pixmap = tintedIconPixmap(QStringLiteral(":/assets/hugeicons/attachment.svg"), QColor("#27303b"));
            const qreal inset = (kLinkButtonPx - kLinkIconPx) / 2;
            p->drawPixmap(button.adjusted(inset, inset, -inset, -inset), pixmap, QRectF(pixmap.rect()));
            if (m_node->files.size() > 1) {
                QFont small;
                small.setPixelSize(9);
                small.setWeight(QFont::DemiBold);
                const QString count = m_node->files.size() > 9 ? QStringLiteral("9+") : QString::number(m_node->files.size());
                const qreal width = std::max<qreal>(12, QFontMetricsF(small).horizontalAdvance(count) + 4);
                const QRectF badge(button.right() - width + 4, button.bottom() - 8, width, 12);
                p->setPen(QPen(Qt::white, 1));
                p->setBrush(QColor("#27303b"));
                p->drawRoundedRect(badge, 6, 6);
                p->setFont(small);
                p->drawText(badge, Qt::AlignCenter, count);
            }
        }
        // FEAT-048: casella di spunta. Vuota: bordo nel colore del testo; spuntata: piena del colore
        // del ramo con la spunta chiara o scura secondo il contrasto. Il testo non cambia.
        // FEAT-051: marcatori (icone disegnate), con l'hover come i bottoni.
        const QList<QRectF> markers = markerRects();
        for (int i = 0; i < markers.size(); ++i) {
            if (i == m_hoveredMarker) {
                p->setPen(Qt::NoPen);
                p->setBrush(QColor(15, 23, 42, 15));
                p->drawRoundedRect(markers[i], kLinkButtonRadiusPx, kLinkButtonRadiusPx);
            }
            const qreal inset = (kLinkButtonPx - 18) / 2;
            Markers::paint(p, markers[i].adjusted(inset, inset, -inset, -inset), m_node->markers.value(i));
        }
        if (const QRectF area = taskRect(); !area.isEmpty()) {
            if (m_hoveredTask) {
                p->setPen(Qt::NoPen);
                p->setBrush(QColor(15, 23, 42, 15));
                p->drawRoundedRect(area, kLinkButtonRadiusPx, kLinkButtonRadiusPx);
            }
            const qreal inset = (kLinkButtonPx - kTaskBoxPx) / 2;
            const QRectF check = area.adjusted(inset, inset, -inset, -inset);
            if (m_node->task == MindNode::Task::Done) {
                p->setPen(Qt::NoPen);
                p->setBrush(m_node->branchColor);
                p->drawRoundedRect(check, 4, 4);
                QPainterPath tick;
                tick.moveTo(check.left() + 3.5, check.center().y() + 0.5);
                tick.lineTo(check.left() + 6.5, check.bottom() - 4);
                tick.lineTo(check.right() - 3.5, check.top() + 4.5);
                p->setPen(QPen(NodeStyle::readableTextOn(m_node->branchColor), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p->setBrush(Qt::NoBrush);
                p->drawPath(tick);
            } else {
                p->setPen(QPen(m_node->text, 1.5));
                p->setBrush(Qt::NoBrush);
                p->drawRoundedRect(check.adjusted(0.75, 0.75, -0.75, -0.75), 4, 4);
            }
        }
    }
    // FEAT-060: etichette sotto la forma: pillole chiare su sfondo chiaro, invertite su
    // uno sfondo scuro della mappa con un contrasto simile.
    if (const QList<QRectF> pills = labelRects(); !pills.isEmpty()) {
        const QColor background = scene() ? scene()->backgroundBrush().color() : NodeStyle::kMapBackground;
        const bool dark = NodeStyle::isDarkBackground(background);
        p->save();
        p->setFont(labelFont());
        for (int i = 0; i < pills.size(); ++i) {
            const QRectF pill = pills[i];
            const bool hot = i == m_hoveredLabel;  // hover: bordo piu' marcato
            p->setPen(QPen(dark ? QColor(255, 255, 255, hot ? 220 : 110) : QColor(15, 23, 42, hot ? 110 : 40), 1));
            p->setBrush(dark ? QColor(255, 255, 255, 20) : QColor(255, 255, 255, 230));
            p->drawRoundedRect(pill.adjusted(0.5, 0.5, -0.5, -0.5), kLabelHeightPx / 2, kLabelHeightPx / 2);
            p->setPen(dark ? QColor(255, 255, 255, 220) : QColor("#6b7280"));
            drawLabelText(p, pill, m_node->labels.value(i), kLabelPaddingPx);
        }
        p->restore();
    }
    // FEAT-059: nota dentro il nodo (Mostra tutte le note): fondo leggermente piu' chiaro o piu' scuro
    // del nodo, testo del colore del testo del nodo, al massimo 6 righe.
    if (const QRectF note = notePreviewRect(); !note.isEmpty() && m_notePreview) {
        p->save();
        const bool lightText = NodeStyle::readableTextOn(m_node->text) != QColor(Qt::white);  // testo chiaro
        p->setPen(Qt::NoPen);
        p->setBrush(lightText ? QColor(255, 255, 255, 46) : QColor(15, 23, 42, 13));
        p->drawRoundedRect(note, 6, 6);
        const QRectF text = note.adjusted(kNotePreviewPaddingPx, kNotePreviewPaddingPx, -kNotePreviewPaddingPx, -kNotePreviewPaddingPx);
        p->translate(text.topLeft());
        p->setClipRect(QRectF(QPointF(0, 0), text.size()));
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette.setColor(QPalette::Text, m_node->text);
        context.clip = QRectF(QPointF(0, 0), text.size());
        m_notePreview->documentLayout()->draw(p, context);
        p->restore();
        if (m_notePreview->size().height() > text.height() + 1) {  // testo tagliato
            p->setPen(m_node->text);
            p->drawText(QRectF(note.right() - 24, note.bottom() - 18, 18, 14), Qt::AlignRight | Qt::AlignBottom, QStringLiteral("…"));
        }
    }
    if (m_imageMissing && !m_draggingImage && !imageRect().isEmpty()) {
        const QRectF image = imageRect();
        p->save();
        p->setPen(QPen(QColor("#c4cad2"), 1, Qt::DashLine));
        p->setBrush(QColor(15, 23, 42, 8));
        p->drawRoundedRect(image.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        QFont font = p->font();
        font.setPixelSize(12);
        p->setFont(font);
        p->setPen(QColor("#6b7280"));
        p->drawText(image.adjusted(4, 4, -4, -4), Qt::AlignCenter | Qt::TextWordWrap, tr("Image not available"));
        p->restore();
    }
    if (!m_image.isNull() && !m_draggingImage) {
        // FEAT-040: ombra leggera come quella dei modali (4 rettangoli, alfa 10/8/6/4, 2 px in basso),
        // poi l'immagine con la sua opacita' e il bordo dentro il suo rettangolo (la misura non cambia).
        const QRectF image = imageRect();
        const ImageStyle &look = m_node->imageStyle;
        if (look.shadow) {
            p->save();
            p->setPen(Qt::NoPen);
            const int alphas[] = {10, 8, 6, 4};
            for (int i = 0; i < 4; ++i) {
                const qreal grow = i + 1;
                p->setBrush(QColor(15, 23, 42, alphas[i]));
                p->drawRoundedRect(image.adjusted(-grow, -grow + 2, grow, grow + 2), grow, grow);
            }
            p->restore();
        }
        const qreal opacity = p->opacity();
        p->setOpacity(opacity * look.opacity / 100.0);
        p->drawPixmap(image, m_image, m_image.rect());
        p->setOpacity(opacity);
        if (look.border != ImageStyle::Border::None) {
            const Qt::PenStyle style = look.border == ImageStyle::Border::Dashed ? Qt::DashLine
                                       : look.border == ImageStyle::Border::Dotted ? Qt::DotLine : Qt::SolidLine;
            const qreal half = look.borderWidth / 2;
            p->setPen(QPen(look.borderColor, look.borderWidth, style, Qt::FlatCap, Qt::MiterJoin));
            p->setBrush(Qt::NoBrush);
            p->drawRect(image.adjusted(half, half, -half, -half));
        }
    }
    // FEAT-037: immagine scelta con un clic: contorno e maniglie agli angoli nel colore della
    // selezione (Impostazioni > Selezione), fondo bianco come i controlli chiari.
    if (m_imageSelected && isSelected() && !m_draggingImage && !imageRect().isEmpty()) {
        const QRectF image = imageRect();
        const QColor accent = selectionStyle().selectedColor;
        p->setPen(QPen(accent, 1));
        p->setBrush(Qt::NoBrush);
        p->drawRect(image);
        p->setBrush(Qt::white);
        for (const QPointF &corner : {image.topLeft(), image.topRight(), image.bottomRight(), image.bottomLeft()})
            p->drawRect(QRectF(corner.x() - kImageHandlePx / 2, corner.y() - kImageHandlePx / 2, kImageHandlePx, kImageHandlePx));
    }
    if (m_draggingImage) {
        // Zone dentro il nodo: velo chiaro del colore della selezione con il bordo, piu' intenso
        // sotto il cursore (dove l'immagine cadrebbe).
        const QList<QRectF> zones = imageDropZones();
        const int activeZone = imageDropZoneAt(m_imageDragPosition);
        const QColor accent = selectionStyle().selectedColor;
        p->setPen(QPen(accent, 1));
        for (int i = 0; i < zones.size(); ++i) {
            QColor fill = accent;
            fill.setAlpha(i == activeZone ? 110 : 36);
            p->setBrush(fill);
            p->drawRect(zones[i].adjusted(0.5, 0.5, -0.5, -0.5));
        }
        p->save();
        p->setOpacity(0.55);
        const QSizeF size = m_node->imageSize;
        p->drawPixmap(QRectF(m_imageDragPosition.x() - size.width() / 2, m_imageDragPosition.y() - size.height() / 2, size.width(), size.height()), m_image, m_image.rect());
        p->restore();
    }
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    m_ignoreDragUntilRelease = false;  // un rilascio perso (es. focus a un'altra finestra) non blocca i trascinamenti dopo
    if (m_reordering) {  // FEAT-015: rilascio perso, come sopra; l'anteprima sparisce senza modifiche
        m_reordering = false;
        setDragFade(false);
        emit reorderFinished(this, {}, false);
    }
    // FEAT-027: l'icona di un collegamento non seleziona e non sposta il nodo; il clic vale al rilascio.
    if (event->button() == Qt::LeftButton) {
        if (const std::optional<LinkKind> link = linkAt(event->pos())) {
            m_pressedLink = link;
            event->accept();
            return;
        }
        if (const int marker = markerAt(event->pos()); marker >= 0) {  // FEAT-051: vale al rilascio
            m_pressedMarker = marker;
            event->accept();
            return;
        }
        // FEAT-048: la casella non seleziona il testo e non sposta il nodo; vale al rilascio.
        if (!m_editor && taskRect().contains(event->pos())) {
            m_pressedTask = true;
            event->accept();
            return;
        }
        if (!m_editor && labelAt(event->pos()) >= 0) {  // FEAT-060: pillola di un'etichetta
            m_pressedLabel = labelAt(event->pos());
            event->accept();
            return;
        }
        if (!m_editor && (noteRect().contains(event->pos()) || notePreviewRect().contains(event->pos()))) {  // FEAT-059
            m_pressedNote = true;
            event->accept();
            return;
        }
        for (const int side : {-1, 1}) {  // FEAT-016: vale al rilascio
            if (collapseRect(side).contains(event->pos())) {
                m_pressedCollapse = side;
                event->accept();
                return;
            }
        }
        if (!m_editor && filesRect().contains(event->pos())) {  // FEAT-049
            m_pressedFiles = true;
            event->accept();
            return;
        }
    }
    // FEAT-037: maniglie dell'immagine scelta (ridimensiona con le proporzioni bloccate).
    if (event->button() == Qt::LeftButton) {
        if (const int corner = imageHandleAt(event->pos())) {
            m_imageResizeCorner = corner;
            m_imageResizeStartSize = m_node->imageSize;
            m_imageResizeStartX = event->scenePos().x();
            event->accept();
            return;
        }
    }
    // Clic sull'immagine: la sceglie (maniglie al rilascio); trascinandola si sposta (zone).
    if (m_hasImage && imageRect().contains(event->pos()) && event->button() == Qt::LeftButton) {
        if (!isSelected()) {
            if (scene() && !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) scene()->clearSelection();
            setSelected(true);
        }
        m_imagePressed = true;
        m_imagePressPos = event->pos();
        event->accept();
        return;
    }
    setImageSelectedState(false);
    // Clic sul lato sinistro o destro: inizia il ridimensionamento (richiesta 2026-10-13).
    if (event->button() == Qt::LeftButton) {
        if (const int side = resizeSideAt(event->pos())) {
            if (!isSelected()) {
                if (scene() && !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) scene()->clearSelection();
                setSelected(true);
            }
            m_resizeSide = side;
            // Anche qui: il rilascio dopo un doppio clic sul bordo (FEAT-018) passa dal percorso
            // normale e con una posizione vecchia sembrerebbe un trascinamento del nodo.
            m_pressOnText = false;
            m_pressScreenPos = event->screenPos();
            m_resizeStartX = event->scenePos().x();
            m_resizeStartWidth = textRect().width();
            m_resizeMinWidth = std::min(minimumCustomWidth(), kMaxManualNodeWidthPx);
            m_resizeStartPos = pos();
            m_resizeStartCustomWidth = m_node->customWidth;
            event->accept();
            return;
        }
    }
    // Clic sul nodo: solo selezione (gestita da QGraphicsItem) e, trascinando, spostamento.
    m_pressOnText = false;
    m_pressScreenPos = event->screenPos();
    QGraphicsObject::mousePressEvent(event);
}

void NodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
    if (m_imageResizeCorner) {
        // FEAT-037: proporzioni bloccate; il nodo resta centrato, quindi l'angolo segue il mouse
        // se la larghezza cresce del doppio dello spostamento orizzontale.
        const qreal direction = m_imageResizeCorner == 2 || m_imageResizeCorner == 3 ? 1.0 : -1.0;
        const qreal width = std::clamp(std::round(m_imageResizeStartSize.width() + 2 * direction * (event->scenePos().x() - m_imageResizeStartX)),
                                       kMinImageWidthPx, kMaxImageWidthPx);
        const QSizeF size(width, std::round(width * m_imageResizeStartSize.height() / std::max<qreal>(1, m_imageResizeStartSize.width())));
        if (size != m_node->imageSize) {
            m_node->imageSize = size;
            syncFromNode();
            emit editingResized(this);  // la scena riallinea i rami del nodo
        }
        event->accept();
        return;
    }
    if (m_imagePressed && !m_draggingImage) {
        // Bug 2026-10-17 (foto dell'utente): sotto la soglia il movimento passava al nodo, che
        // diventava trasparente come durante uno spostamento e restava cosi' dopo il rilascio
        // dell'immagine. Il clic sull'immagine non muove mai il nodo.
        event->accept();
        if ((event->pos() - m_imagePressPos).manhattanLength() < QApplication::startDragDistance()) return;
        prepareGeometryChange();
        m_draggingImage = true;
        setImageSelectedState(false);
        setFlag(ItemIsMovable, false);
    }
    if (m_draggingImage) {
        m_imageDragPosition = event->pos();
        update();
        event->accept();
        return;
    }
    // Bug 2026-10-20 (foto dell'utente): premuto su un bottone del nodo (marcatore, casella, nota,
    // graffetta, pillola, collegamento) il nodo non e' selezionato e il trascinamento restava a
    // meta': il nodo diventava trasparente e lo spostamento finiva sugli altri nodi selezionati.
    // Come per l'immagine, da un bottone il nodo non si sposta; il clic vale al rilascio.
    if (m_pressedMarker >= 0 || m_pressedTask || m_pressedNote || m_pressedFiles || m_pressedLabel >= 0 || m_pressedLink || m_pressedCollapse) {
        event->accept();
        return;
    }
    // BUG-004 (seconda segnalazione, foto dell'utente): la mano si muove di qualche pixel durante
    // il secondo clic; senza questo il nodo veniva trascinato e lo spostamento salvato come manuale.
    if (m_ignoreDragUntilRelease) { event->accept(); return; }
    if (m_resizeSide) {
        // Nuova larghezza, al minimo un carattere per riga. Il lato opposto resta fermo: il
        // centro del nodo si sposta di metà della differenza.
        const qreal delta = (event->scenePos().x() - m_resizeStartX) * m_resizeSide;
        const qreal width = std::clamp(std::round(m_resizeStartWidth + delta), m_resizeMinWidth, kMaxManualNodeWidthPx);
        if (width != m_node->customWidth) {
            m_node->customWidth = width;
            syncFromNode();
            const qreal actual = textRect().width();
            setPos(m_resizeStartPos + QPointF((actual - m_resizeStartWidth) / 2 * m_resizeSide, 0));
        }
        event->accept();
        return;
    }
    // Bug 2026-10-20 (segnalato dall'utente): Qt sposta l'elemento a ogni pixel, quindi un clic con
    // la mano che si muove di 1-2 px spostava il nodo (e, dal centrale, tutta la mappa). Sotto la
    // soglia di trascinamento del sistema il nodo resta fermo; oltre, Qt lo porta subito alla
    // distanza giusta dal punto premuto.
    if (!m_dragFaded && (event->screenPos() - m_pressScreenPos).manhattanLength() < QApplication::startDragDistance()) {
        event->accept();
        return;
    }
    // FEAT-015: con il bilanciamento attivo il nodo resta al suo posto; la scena mostra l'anteprima e
    // la destinazione sotto il mouse.
    if (!m_reordering && !m_dragFaded && (event->buttons() & Qt::LeftButton) && (flags() & ItemIsMovable) && reorderDrag && reorderDrag(this)) {
        m_reordering = true;
        setDragFade(true);
    }
    if (m_reordering) {
        emit reorderMoved(this, event->scenePos());
        event->accept();
        return;
    }
    // Primo movimento con il tasto sinistro premuto: inizia il trascinamento del nodo.
    if (!m_dragFaded && (event->buttons() & Qt::LeftButton) && (flags() & ItemIsMovable)) setDragFade(true);
    QGraphicsObject::mouseMoveEvent(event);
}

bool NodeItem::cancelReorder() {
    if (!m_reordering) return false;
    m_reordering = false;
    setDragFade(false);
    ungrabMouse();  // il rilascio non arriva piu' al nodo: nessuno spostamento e nessun passo
    emit reorderFinished(this, {}, false);
    return true;
}

bool NodeItem::cancelResize() {
    if (m_imageResizeCorner) {  // FEAT-037: Esc rimette la misura di partenza dell'immagine
        m_imageResizeCorner = 0;
        m_node->imageSize = m_imageResizeStartSize;
        syncFromNode();
        emit editingResized(this);
        ungrabMouse();
        return true;
    }
    if (!m_resizeSide) return false;
    m_resizeSide = 0;
    m_node->customWidth = m_resizeStartCustomWidth;
    syncFromNode();
    setPos(m_resizeStartPos);
    // Senza il mouse il rilascio successivo non arriva al nodo, quindi non diventa un passo di
    // storico ne' uno spostamento del nodo.
    ungrabMouse();
    return true;
}

void NodeItem::setDragFade(bool active) {
    // L'opacità usata mentre si trascina è NodeItem::kDragOpacity (nodeitem.h).
    // Si muovono insieme tutti i nodi selezionati, quindi diventano tutti trasparenti.
    // All'inizio: questo nodo e quelli selezionati. Alla fine: tutti quelli ancora
    // trasparenti, anche se nel frattempo la selezione è cambiata.
    QList<QGraphicsItem *> targets{this};
    if (scene()) for (auto *item : active ? scene()->selectedItems() : scene()->items()) if (item != this) targets.append(item);
    for (auto *item : targets) {
        if (auto *node = qgraphicsitem_cast<NodeItem *>(item); node && (active || node->m_dragFaded)) {
            node->m_dragFaded = active;
            node->setOpacity(active ? kDragOpacity : 1.0);
            emit node->dragFadeChanged(node, active);
        }
    }
}

void NodeItem::setImageSelectedState(bool selected) {
    if (m_imageSelected == selected) return;
    m_imageSelected = selected;
    update();
    emit imageSelectionChanged(this);
}

void NodeItem::selectImage() {
    if (!m_hasImage) return;
    if (!isSelected()) {
        if (scene()) scene()->clearSelection();
        setSelected(true);
    }
    setImageSelectedState(true);
}

QPixmap NodeItem::picture() {
    if (!m_hasImage) return {};
    if (m_imageLoadPending || (m_image.isNull() && !m_loadedImagePath.isEmpty())) loadImage();
    return m_image;
}

QPixmap NodeItem::dragPreview(qreal scale) const {
    const QRectF bounds = boundingRect();
    QPixmap pixmap((bounds.size() * scale).toSize());
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
    painter.translate(-bounds.topLeft());
    QStyleOptionGraphicsItem option;  // senza State_Selected: niente anello di selezione
    auto *self = const_cast<NodeItem *>(this);
    const bool hovered = std::exchange(self->m_hovered, false);  // niente anello di passaggio
    self->paint(&painter, &option, nullptr);
    self->m_hovered = hovered;
    return pixmap;
}

qreal NodeItem::imageRoomWidth() const {
    return textRect().width() - taskZoneWidth() - linkZoneWidth() - 2 * padX();
}

QSizeF NodeItem::insertionImageSize(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSizeF size = reader.size();
    if (reader.transformation() & QImageIOHandler::TransformationRotate90) size.transpose();
    if (size.isEmpty()) size = {180, 120};
    if (size.width() > kMaxImageWidthPx) size = size.scaled(kMaxImageWidthPx, size.height(), Qt::KeepAspectRatio);
    return {std::round(size.width()), std::round(size.height())};
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged && m_node) {
        m_node->position = value.toPointF();
        emit moved(this);
    }
    if (change == ItemSelectedHasChanged && value.toBool()) emit activated(this);
    if (change == ItemSelectedHasChanged && !value.toBool()) setImageSelectedState(false);  // FEAT-037
    return QGraphicsObject::itemChange(change, value);
}

void NodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
    // FEAT-018: doppio clic nella zona del bordo (la stessa del cursore a doppia freccia) =
    // ritorno alla larghezza automatica, non modifica del titolo. Il centro del nodo (pos())
    // non cambia, quindi il risultato e' lo stesso dai due lati. Su un nodo gia' automatico
    // non succede nulla e non si crea un passo di storico.
    if (event->button() == Qt::LeftButton && formulaRect().contains(event->pos())) {  // FEAT-064
        event->accept();
        m_ignoreDragUntilRelease = true;
        emit formulaActivated(this);
        return;
    }
    if (event->button() == Qt::LeftButton && resizeSideAt(event->pos())) {
        event->accept();
        m_ignoreDragUntilRelease = true;
        if (m_node->customWidth > 0) {
            m_node->customWidth = 0;
            syncFromNode();
            emit resizeFinished(this);
        }
        return;
    }
    beginEditing();
    event->accept();
}


void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    if (m_pressedLabel >= 0) {  // FEAT-060: clic su una pillola = riquadro delle etichette
        const int pressed = m_pressedLabel;
        m_pressedLabel = -1;
        event->accept();
        if (labelAt(event->pos()) == pressed) emit labelActivated(this);
        return;
    }
    if (m_pressedCollapse) {  // FEAT-016: clic sul controllo = chiude o riapre i rami
        const int side = std::exchange(m_pressedCollapse, 0);
        event->accept();
        if (collapseRect(side).contains(event->pos())) emit collapseToggled(this);
        return;
    }
    if (m_pressedFiles) {  // FEAT-049: clic sulla graffetta = elenco dei file
        m_pressedFiles = false;
        event->accept();
        if (filesRect().contains(event->pos())) emit filesActivated(this, event->screenPos());
        return;
    }
    if (m_pressedNote) {  // FEAT-059: clic sull'icona della nota
        m_pressedNote = false;
        event->accept();
        if (noteRect().contains(event->pos()) || notePreviewRect().contains(event->pos())) emit noteActivated(this);
        return;
    }
    if (m_pressedMarker >= 0) {  // FEAT-051: clic sul marcatore = valore seguente
        const int pressed = std::exchange(m_pressedMarker, -1);
        event->accept();
        if (markerAt(event->pos()) == pressed) emit markerClicked(this, m_node->markers.value(pressed));
        return;
    }
    if (m_pressedTask) {  // FEAT-048: clic sulla casella
        m_pressedTask = false;
        event->accept();
        if (taskRect().contains(event->pos())) emit taskToggled(this);
        return;
    }
    if (m_pressedLink) {  // FEAT-027: clic su un'icona di collegamento
        const LinkKind kind = *m_pressedLink;
        m_pressedLink.reset();
        event->accept();
        if (linkAt(event->pos()) == kind) emit linkActivated(this, kind);
        return;
    }
    if (m_imageResizeCorner) {
        m_imageResizeCorner = 0;
        event->accept();
        if (m_node->imageSize != m_imageResizeStartSize) {
            // Una larghezza scelta a mano piu' stretta del contenuto diventa quella necessaria.
            if (m_node->customWidth > 0 && m_node->customWidth < naturalWidth()) {
                m_node->customWidth = std::min(std::ceil(naturalWidth()), kMaxManualNodeWidthPx);
                syncFromNode();
            }
            emit resizeFinished(this);  // ridisposizione e un passo di storico
        }
        return;
    }
    if (m_imagePressed && !m_draggingImage) {  // clic senza trascinare: immagine scelta
        m_imagePressed = false;
        setImageSelectedState(true);
        event->accept();
        return;
    }
    m_imagePressed = false;
    if (m_draggingImage) {
        const int zone = imageDropZoneAt(event->pos());
        prepareGeometryChange();
        m_draggingImage = false;
        setFlag(ItemIsMovable, !summaryTopicOf(m_node));
        if (zone >= 0 && !m_node->title.trimmed().isEmpty()) {
            m_node->imagePlacement = static_cast<MindNode::ImagePlacement>(zone);
            emit imagePlacementChanged(this);
        }
        update();
        event->accept();
        return;
    }
    if (m_ignoreDragUntilRelease) {
        m_ignoreDragUntilRelease = false;
        event->accept();
        return;
    }
    if (m_resizeSide) {
        m_resizeSide = 0;
        event->accept();
        if (std::abs(textRect().width() - m_resizeStartWidth) >= 0.5) emit resizeFinished(this);
        return;
    }
    if (m_reordering) {  // FEAT-015
        m_reordering = false;
        m_wasDragged = false;
        m_pressOnText = false;
        setDragFade(false);
        event->accept();
        emit reorderFinished(this, event->scenePos(), true);
        return;
    }
    const bool dragged = m_dragFaded ||
        (event->screenPos() - m_pressScreenPos).manhattanLength() >= QApplication::startDragDistance();
    const bool editOnRelease = m_pressOnText && !dragged && event->button() == Qt::LeftButton;
    m_wasDragged = dragged;  // B7
    m_pressOnText = false;
    if (m_dragFaded) setDragFade(false);
    QGraphicsObject::mouseReleaseEvent(event);
    emit dragFinished(this);
    if (editOnRelease) beginEditingAt(event->pos());
}

void NodeItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event) {
    // Sui lati sinistro e destro il cursore a doppia freccia orizzontale (cursor: ew-resize)
    // indica che il nodo si può allargare o stringere (richiesta 2026-10-13).
    // FEAT-027: sulle icone dei collegamenti la mano e il suggerimento con la destinazione.
    const std::optional<LinkKind> link = linkAt(event->pos());
    if (link != m_hoveredLink) { m_hoveredLink = link; update(); }
    const int onMarker = markerAt(event->pos());  // FEAT-051
    if (onMarker != m_hoveredMarker) { m_hoveredMarker = onMarker; update(); }
    const bool onTask = !m_editor && taskRect().contains(event->pos());  // FEAT-048
    if (onTask != m_hoveredTask) { m_hoveredTask = onTask; update(); }
    const bool onNote = !m_editor && noteRect().contains(event->pos());  // FEAT-059
    if (onNote != m_hoveredNote) { m_hoveredNote = onNote; update(); }
    const bool onFiles = !m_editor && filesRect().contains(event->pos());  // FEAT-049
    if (onFiles != m_hoveredFiles) { m_hoveredFiles = onFiles; update(); }
    // FEAT-060 (prova dell'utente 2026-10-19): sulla pillola si accende solo il suo bordo, non
    // l'anello di hover del nodo (le pillole stanno fuori dalla forma).
    const int onLabel = m_editor ? -1 : labelAt(event->pos());
    if (onLabel != m_hoveredLabel || m_hovered != (onLabel < 0)) {
        m_hoveredLabel = onLabel;
        m_hovered = onLabel < 0;
        update();
    }
    if (const int corner = imageHandleAt(event->pos())) setCursor(corner % 2 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    else if (resizeSideAt(event->pos())) setCursor(Qt::SizeHorCursor);
    else if (link || onTask || onNote || onFiles || onMarker >= 0 || labelAt(event->pos()) >= 0) setCursor(Qt::PointingHandCursor);
    else unsetCursor();
    QString tip;
    if (link == LinkKind::Web) tip = m_node->webLink;
    else if (link == LinkKind::Topic)
        tip = m_topicLinkBroken ? tr("The linked topic no longer exists") : tr("Go to the linked topic");
    else if (onTask) tip = m_node->task == MindNode::Task::Done ? tr("Mark as not done") : tr("Mark as done");
    else if (onMarker >= 0) tip = Markers::label(m_node->markers.value(onMarker));  // FEAT-051
    else if (onFiles) {  // FEAT-049: i nomi dei file
        QStringList names;
        for (const NodeFile &file : m_node->files) names.append(file.name);
        tip = names.join(QLatin1Char('\n'));
    }
    else if (onNote) {  // FEAT-059: le prime parole della nota
        const QString plain = m_node->notes.simplified();
        tip = plain.size() > 120 ? plain.left(120) + QStringLiteral("…") : plain;
    }
    if (toolTip() != tip) setToolTip(tip);
    QGraphicsObject::hoverMoveEvent(event);
}

// FEAT-002 (2026-11-04, user request): typing on the selected topic opens the title field and the
// key replaces the whole title live, as in XMind. Shortcuts (Space, F2, single-key actions) arrive
// earlier as ShortcutOverride, so only plain printable keys reach this point.
void NodeItem::keyPressEvent(QKeyEvent *event) {
    const QString text = event->text();
    // AltGr arrives as Ctrl+Alt on Windows (@, #, [ on the Italian layout): it still writes.
    const Qt::KeyboardModifiers mods = event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool altGr = mods == (Qt::ControlModifier | Qt::AltModifier);
    const bool printable = !text.isEmpty() && text.at(0).isPrint() && (!mods || altGr);
    if (!m_editor && isSelected() && printable && scene() && scene()->selectedItems().size() == 1) {
        beginEditing();  // the whole title is selected: the key replaces it
        if (m_editor) {
            scene()->sendEvent(m_editor, event);
            event->accept();
            return;
        }
    }
    QGraphicsObject::keyPressEvent(event);
}

void NodeItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
    // FEAT-027: tasto destro su un'icona: menu del collegamento (fuori dalle icone resta quello di prima).
    if (const std::optional<LinkKind> link = linkAt(event->pos())) {
        event->accept();
        emit linkMenuRequested(this, *link, event->screenPos());
        return;
    }
    if (const int label = labelAt(event->pos()); label >= 0) {  // FEAT-060: menu dell'etichetta
        event->accept();
        emit labelMenuRequested(this, m_node->labels.value(label), event->screenPos());
        return;
    }
    if (formulaRect().contains(event->pos())) {  // FEAT-064: menu della formula
        event->accept();
        emit formulaMenuRequested(this, event->screenPos());
        return;
    }
    if (!m_editor && !imageRect().isEmpty() && imageRect().contains(event->pos())) {  // immagine o adesivo
        event->accept();
        emit imageMenuRequested(this, event->screenPos());
        return;
    }
    if (filesRect().contains(event->pos())) {  // FEAT-049: lo stesso elenco del clic
        event->accept();
        emit filesActivated(this, event->screenPos());
        return;
    }
    if (const int marker = markerAt(event->pos()); marker >= 0) {  // FEAT-051: menu del marcatore
        event->accept();
        emit markerMenuRequested(this, m_node->markers.value(marker), event->screenPos());
        return;
    }
    if (taskRect().contains(event->pos())) {  // FEAT-048: menu della casella
        event->accept();
        emit taskMenuRequested(this, event->screenPos());
        return;
    }
    if (!m_editor) {  // FEAT-042: the topic's menu
        event->accept();
        emit nodeMenuRequested(this, event->screenPos());
        return;
    }
    QGraphicsObject::contextMenuEvent(event);
}

void NodeItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event) {
    m_hoveredLabel = labelAt(event->pos());  // FEAT-060: entrando da una pillola il nodo resta spento
    m_hovered = m_hoveredLabel < 0;
    update();
    QGraphicsObject::hoverEnterEvent(event);
}

void NodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event) {
    m_hovered = false;
    m_hoveredLink.reset();
    m_hoveredTask = false;
    m_hoveredNote = false;
    m_hoveredFiles = false;
    m_hoveredMarker = -1;
    m_hoveredLabel = -1;
    update();
    unsetCursor();
    setToolTip(QString());
    QGraphicsObject::hoverLeaveEvent(event);
}

namespace {
// Tiene ferma la mappa mentre si apre la modifica (bug 2026-10-12). Impostare la selezione o il
// cursore nel campo dopo startInlineEditor() fa chiamare di nuovo a Qt ensureVisible(), che fa
// scorrere la vista se il nodo è vicino al bordo: all'uscita dal blocco gli scorrimenti tornano
// ai valori di prima (startInlineEditor() lo fa già per il solo focus).
class KeepViewsStill {
public:
    explicit KeepViewsStill(QGraphicsScene *scene) {
        if (!scene) return;
        for (QGraphicsView *view : scene->views())
            m_saved.append({view, view->horizontalScrollBar()->value(), view->verticalScrollBar()->value()});
    }
    ~KeepViewsStill() {
        for (const Saved &saved : std::as_const(m_saved)) {
            if (!saved.view) continue;
            saved.view->horizontalScrollBar()->setValue(saved.x);
            saved.view->verticalScrollBar()->setValue(saved.y);
        }
    }
private:
    struct Saved { QPointer<QGraphicsView> view; int x; int y; };
    QList<Saved> m_saved;
};
}

void NodeItem::beginEditing() {
    const KeepViewsStill keepStill(scene());
    startInlineEditor();
    if (!m_editor) return;
    QTextCursor cursor(m_editor->document());
    cursor.select(QTextCursor::Document);  // da F2 / doppio clic: scrivere sostituisce tutto
    m_editor->setTextCursor(cursor);
    // startInlineEditor() aggiorna la barra prima che beginEditing() selezioni il
    // documento: va notificata anche questa selezione iniziale.
    static_cast<InlineTitleEditor *>(m_editor)->selectionChanged();
}

void NodeItem::beginEditingAtEnd() {
    const KeepViewsStill keepStill(scene());
    startInlineEditor();
    if (!m_editor) return;
    QTextCursor cursor(m_editor->document());
    cursor.movePosition(QTextCursor::End);
    m_editor->setTextCursor(cursor);
    static_cast<InlineTitleEditor *>(m_editor)->selectionChanged();
}

void NodeItem::beginEditingAt(const QPointF &position) {
    const KeepViewsStill keepStill(scene());
    startInlineEditor();
    if (!m_editor) return;
    // Cursore nel punto cliccato, come in un editor di testo.
    const int at = m_editor->document()->documentLayout()->hitTest(m_editor->mapFromParent(position), Qt::FuzzyHit);
    QTextCursor cursor(m_editor->document());
    cursor.setPosition(qMax(0, at));
    m_editor->setTextCursor(cursor);
    static_cast<InlineTitleEditor *>(m_editor)->selectionChanged();
}

void NodeItem::startInlineEditor() {
    if (m_editor) return;
    // QGraphicsTextItem::setFocus() chiama internamente ensureVisible(): quando il nodo
    // è decentrato, Qt sposta la vista per mostrare cursore e widget figli. Conserviamo
    // il centro visibile per aprire l'editor senza muovere il canvas.
    struct ViewScrollPosition {
        QPointer<QGraphicsView> view;
        int horizontal = 0;
        int vertical = 0;
    };
    QList<ViewScrollPosition> viewPositions;
    if (scene()) {
        for (QGraphicsView *view : scene()->views())
            viewPositions.append({view, view->horizontalScrollBar()->value(), view->verticalScrollBar()->value()});
    }
    const auto restoreViewPositions = [viewPositions] {
        for (const auto &entry : viewPositions) {
            if (!entry.view) continue;
            entry.view->horizontalScrollBar()->setValue(entry.horizontal);
            entry.view->verticalScrollBar()->setValue(entry.vertical);
        }
    };
    auto *editor = new InlineTitleEditor(this);
    editor->boldWeight = boldWeightFor(m_node);  // FEAT-068: come il bottone B della barra
    editor->setFont(nodeFont(m_node));
    editor->setDefaultTextColor(m_node->text);
    editor->document()->setDocumentMargin(0);
    QTextOption option = editor->document()->defaultTextOption();
    option.setAlignment(textAlignment(m_node));  // come il titolo disegnato (FEAT-022; prima sempre a sinistra)
    // Come il titolo disegnato: anche una parola più lunga del nodo va a capo (2026-10-13).
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    editor->document()->setDefaultTextOption(option);
    // Le formule restano sorgente semplice; per gli altri titoli si ripristina la
    // formattazione parziale salvata nel file.
    if (!m_node->titleHtml.isEmpty() && !FormulaText::containsMath(m_node->title))
        editor->setHtml(RichTitle::withoutDocumentFont(m_node->titleHtml));  // FEAT-022: vale il font del nodo
    else editor->setPlainText(m_node->title);
    editor->plainTyping.active = !m_node->titleHtml.isEmpty();  // FEAT-095
    SpellCheck::attach(editor->document());  // FEAT-044: only while the title is edited
    editor->setTextInteractionFlags(Qt::TextEditorInteraction);
    editor->setCursor(Qt::IBeamCursor);
    editor->finished = [this](bool commit) { finishEditing(commit); };
    m_halfBeforeEditing = anchorHalfWidth();  // FEAT-034: misura senza il campo
    m_editor = editor;
    // Bug 2026-10-13 (foto dell'utente): la barra di formattazione è figlia del nodo, quindi il suo
    // setZValue(20) vale solo tra i figli del nodo; i nodi aggiunti dopo nella scena (stesso z 0)
    // venivano disegnati sopra la barra. Durante la modifica il nodo sale sopra gli altri nodi
    // (https://doc.qt.io/qt-6/qgraphicsitem.html#sorting). Si ripristina in finishEditing().
    setZValue(kEditingZValue);
    // Ora la barra si crea una volta per scena e si sposta sul nodo in modifica
    // (setParentItem costa pochissimo); viene ricreata solo se è stata distrutta, per esempio
    // con la scena ricostruita o con il nodo eliminato durante la modifica.
    QGraphicsProxyWidget *formatProxy = g_formatProxy;
    if (!formatProxy || formatProxy->scene() != scene()) {
        delete formatProxy;
        formatProxy = new QGraphicsProxyWidget(this);
        formatProxy->setWidget(new TextFormatBar);
        formatProxy->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        formatProxy->setZValue(20);
        g_formatProxy = formatProxy;
    } else {
        formatProxy->setParentItem(this);
    }
    formatProxy->hide();
    auto *formatBar = static_cast<TextFormatBar *>(formatProxy->widget());
    m_formatBar = formatProxy;
    formatBar->boldWeight = boldWeightFor(m_node);
    formatBar->defaultPointSize = qRound(m_node->fontSize);
    formatBar->applyFormat = [this, editor, formatBar](const QTextCharFormat &requested) {
        QTextCursor cursor = editor->textCursor();
        if (!cursor.hasSelection()) return;
        // FEAT-022: grassetto e normale relativi al peso del nodo (vedi boldWeightFor).
        QTextCharFormat format = requested;
        if (requested.hasProperty(QTextFormat::FontWeight))
            format.setFontWeight(requested.fontWeight() >= QFont::Bold ? boldWeightFor(m_node) : m_node->fontWeight);
        cursor.mergeCharFormat(format);
        editor->formattingChanged = true;
        editor->setTextCursor(cursor);
        formatBar->sync(cursor.charFormat());
        editor->setFocus(Qt::OtherFocusReason);
    };
    editor->toggleStyle = [formatBar](RichTitle::CharStyle style) { formatBar->toggle(style); };
    formatBar->applyHighlight = [editor, formatBar](bool on) {
        QTextCursor cursor = editor->textCursor();
        if (!cursor.hasSelection()) return;
        MarkdownShortcuts::setHighlight(cursor, on);
        editor->formattingChanged = true;
        editor->setTextCursor(cursor);
        formatBar->sync(cursor.charFormat());
        editor->setFocus(Qt::OtherFocusReason);
    };
    // FEAT-019: "Tema attuale" del selettore colori = palette del foglio di questo nodo.
    formatBar->themeColors = [this] {
        const auto *mapScene = qobject_cast<const MindMapScene *>(scene());
        return mapScene && mapScene->model() ? mapScene->model()->activePalette : QStringList();
    };
    // Elenco della barra chiuso (bug 2026-10-14, vedi TextFormatBar::popupClosed): il focus torna
    // al campo con la stessa selezione, senza spostare la mappa (setFocus può chiamare ensureVisible).
    formatBar->popupClosed = [this, editor] {
        if (m_editor != editor || editor->hasFocus()) return;
        const KeepViewsStill keepStill(scene());
        editor->setFocus(Qt::OtherFocusReason);
    };
    editor->selectionChanged = [this, editor, formatBar, formatProxy] {
        const QTextCursor cursor = editor->textCursor();
        formatProxy->setVisible(cursor.hasSelection());
        if (!cursor.hasSelection()) return;
        formatBar->sync(cursor.charFormat());
        formatBar->adjustSize();
        placeFormatBar();
    };
    editor->keepEditing = [formatBar, formatProxy] {
        if (QApplication::activePopupWidget() || formatBar->underMouse()) return true;
        // Elenco di una select della barra aperto (anche come popup incorporato nella scena).
        for (QComboBox *combo : formatBar->findChildren<QComboBox *>())
            if (combo->view() && combo->view()->isVisible()) return true;
        // Focus passato alla barra o a un popup figlio del suo proxy.
        QGraphicsScene *scene = formatProxy->scene();
        for (QGraphicsItem *item = scene ? scene->focusItem() : nullptr; item; item = item->parentItem())
            if (item == formatProxy) return true;
        return false;
    };
    connect(editor->document(), &QTextDocument::contentsChanged, this, [this] { layoutEditor(); });
    setFlag(ItemIsMovable, false);  // trascinare nel testo seleziona, non sposta il nodo
    unsetCursor();
    layoutEditor();
    editor->setFocus(Qt::MouseFocusReason);
    editor->selectionChanged();
    restoreViewPositions();
    // Alcuni stili Windows rimandano ensureVisible al giro successivo dell'event loop.
    QTimer::singleShot(0, this, restoreViewPositions);
}

// FEAT-034 (decisioni dell'utente 2026-10-16): mentre la misura cambia il lato verso il genitore
// resta fermo; il nodo si allarga solo dal lato libero (il centrale e i nodi liberi dai due lati).
void NodeItem::keepParentSide(qreal oldHalf) {
    const qreal newHalf = anchorHalfWidth();
    if (m_growSide != 0 && std::abs(newHalf - oldHalf) > 0.01) setPos(pos() + QPointF(m_growSide * (newHalf - oldHalf), 0));
}

void NodeItem::layoutEditor() {
    if (!m_editor) return;
    const qreal oldHalf = m_halfBeforeEditing >= 0 ? std::exchange(m_halfBeforeEditing, -1.0) : anchorHalfWidth();
    // Larghezza su misura del testo, fino al massimo del nodo; oltre si va a capo.
    QTextDocument *document = m_editor->document();
    m_editor->setTextWidth(-1);
    // Con la larghezza scelta trascinando i lati il campo va a capo dove va a capo il titolo.
    const qreal maxWidth = layoutCustomWidth() > 0 ? std::max<qreal>(1, manualTextWidth() - 2 * padX())
                                                   : maxTextNodeWidth() - 2 * padX();
    // Nessun margine extra: con AlignHCenter un campo più largo del testo sposterebbe le
    // lettere rispetto al titolo disegnato.
    const qreal width = layoutCustomWidth() > 0 ? maxWidth : std::min(std::ceil(document->idealWidth()), maxWidth);
    m_editor->setTextWidth(width);
    prepareGeometryChange();
    m_editorSize = document->size();
    keepParentSide(oldHalf);
    // Testo a sinistra (2026-10-13): il campo parte dove parte il titolo disegnato, anche quando
    // il nodo è più largo del testo (larghezza minima).
    const QRectF area = textArea();  // FEAT-027: senza la colonna delle icone
    // Bug 2026-10-20 (foto dell'utente): in verticale al centro del posto del titolo, non del nodo:
    // con un'immagine, un adesivo o una formula sopra o sotto il campo finiva sopra l'immagine.
    m_editor->setPos(area.left() + textLeftInset(area.width(), m_editorSize.width(), padX()), area.center().y() - m_editorSize.height() / 2);
    if (m_formatBar && m_formatBar->isVisible()) placeFormatBar();  // the topic grew or shrank
    update();
    emit editingResized(this);
}

void NodeItem::finishEditing(bool commit) {
    if (!m_editor) return;
    auto *editor = static_cast<InlineTitleEditor *>(m_editor);
    editor->finished = nullptr;  // nascondere il campo toglie il focus: niente seconda chiamata
    editor->selectionChanged = nullptr;
    editor->keepEditing = nullptr;
    const QString text = editor->toPlainText().trimmed();
    const QString html = editor->toHtml();
    const bool formattingChanged = editor->formattingChanged;
    const int pendingTask = commit ? editor->pendingTask : 0;  // FEAT-068
    const bool editorHadFocus = editor->hasFocus();
    const qreal halfWhileEditing = anchorHalfWidth();
    prepareGeometryChange();
    m_editor = nullptr;
    keepParentSide(halfWhileEditing);  // FEAT-034: il layout dopo la conferma tiene lo stesso lato fermo
    // La barra resta nella scena, nascosta e staccata dal nodo, per la prossima modifica.
    if (m_formatBar) {
        m_formatBar->hide();
        if (auto *bar = static_cast<TextFormatBar *>(m_formatBar->widget())) { bar->applyFormat = nullptr; bar->popupClosed = nullptr; bar->themeColors = nullptr; }
        m_formatBar->setParentItem(nullptr);
        m_formatBar = nullptr;
    }
    editor->hide();
    editor->deleteLater();
    // Bug 2026-11-04: the hidden field took the scene's focus with it, so Space, F2 and typing on the
    // still selected topic did nothing after Enter. Ended from the keyboard, the topic gets it back;
    // a click elsewhere keeps its own focus.
    if (editorHadFocus && isSelected()) setFocus(Qt::OtherFocusReason);
    setZValue(restingZ());  // back to the branch's place in the stacking order (FEAT-057)
    setFlag(ItemIsMovable, !summaryTopicOf(m_node));
    update();
    emit editingResized(this);
    // Testo invariato: resta il titolo di prima. BUG-020 (2026-10-20): anche vuoto si conferma, come
    // Il nodo resta senza testo con la sua misura minima (contentLayout).
    const bool textChanged = text != m_node->title;
    if (commit && (textChanged || formattingChanged)) {
        const bool keepRichText = !m_node->titleHtml.isEmpty() || formattingChanged;
        // Ora ogni passo di annullamento del campo di testo diventa un passo di storico distinto.
        // QTextDocument registra già ogni formattazione come passo a sé e unisce la scrittura
        // continua in un passo solo (verificato con Qt 6.4: "ciao mondo bello" scritto di
        // seguito è un passo). Annullando il documento all'indietro si ricavano gli stati
        // intermedi, poi si applicano in ordine cronologico. Il campo viene comunque distrutto.
        struct Step { QString text, html; };
        QList<Step> steps{{text, html}};
        QTextDocument *document = editor->document();
        while (document->isUndoAvailable()) {
            document->undo();
            steps.prepend({document->toPlainText().trimmed(), document->toHtml()});
        }
        // steps[0] è il titolo all'apertura del campo: non è una modifica.
        Step previous = steps.value(0);
        const QPointer<NodeItem> self(this);  // per sicurezza: ogni titleEdited aggiorna scena e modello
        for (qsizetype i = 1; i < steps.size(); ++i) {
            const Step &step = steps[i];
            const bool last = i == steps.size() - 1;
            // Gli stati intermedi vuoti (titolo cancellato e poi riscritto) o uguali al
            // precedente non sono passi utili; l'ultimo stato si applica sempre.
            if (!last && (step.text.isEmpty() || (step.text == previous.text && step.html == previous.html))) continue;
            emit titleEdited(this, step.text,
                             !step.text.isEmpty() && !FormulaText::containsMath(step.text) && keepRichText ? step.html : QString());
            if (!self) return;
            if (!last) emit historyStepFinished();
            previous = step;
        }
    }
    if (pendingTask) emit taskTyped(this, pendingTask);  // FEAT-068: "- [ ] " a inizio titolo
}

QFont NodeItem::titleFont(const MindNode *node) { return nodeFont(node); }

int NodeItem::boldWeightFor(const MindNode *node) { return node->fontWeight >= QFont::Bold ? QFont::Black : QFont::Bold; }

void NodeItem::loadBundledFonts() {
    // Registered once, before the font list is read.
    static const bool loaded = [] {
        // FEAT-072: the fonts of the mind map variants (all SIL OFL 1.1, see each LEGGIMI.txt).
        for (const char *path : {":/assets/fonts/roboto/Roboto[wdth,wght].ttf", ":/assets/fonts/montserrat/Montserrat[wght].ttf",
                                 ":/assets/fonts/arvo/Arvo-Regular.ttf", ":/assets/fonts/arvo/Arvo-Bold.ttf"})
            QFontDatabase::addApplicationFont(QString::fromLatin1(path));
        // Fonts for readability, offered in the font list: Atkinson Hyperlegible and OpenDyslexic (SIL OFL 1.1).
        loadInterfaceFont();
        for (const char *name : {"Regular", "Italic", "Bold", "Bold-Italic"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/assets/fonts/opendyslexic/OpenDyslexic-%1.otf").arg(QLatin1String(name)));
        // Atkinson Hyperlegible Next and Mono, every weight with its italic.
        for (const char *weight : {"ExtraLight", "Light", "Regular", "Medium", "SemiBold", "Bold", "ExtraBold"})
            for (const char *family : {"Next", "Mono"})
                for (const char *italic : {"", "Italic"}) {
                    const QString name = QLatin1String(weight) + QLatin1String(italic);
                    QFontDatabase::addApplicationFont(QStringLiteral(":/assets/fonts/atkinson-hyperlegible-%1/AtkinsonHyperlegible%2-%3.otf")
                                                          .arg(QString::fromLatin1(family).toLower(), QLatin1String(family), name));
                }
        return true;
    }();
    Q_UNUSED(loaded);
}

void NodeItem::loadInterfaceFont() {
    static const bool loaded = [] {
        for (const char *name : {"Regular", "Italic", "Bold", "BoldItalic"})
            QFontDatabase::addApplicationFont(QStringLiteral(":/assets/fonts/atkinson-hyperlegible/AtkinsonHyperlegible-%1.ttf").arg(QLatin1String(name)));
        return true;
    }();
    Q_UNUSED(loaded);
}

bool NodeItem::isFontInstalled(const QString &family) {
    loadBundledFonts();
    // Elenco letto una volta: QFontDatabase::families() costa e nodeFont() si chiama a ogni misura.
    static const QSet<QString> families = [] {
        const QStringList list = QFontDatabase::families();
        return QSet<QString>(list.cbegin(), list.cend());
    }();
    return families.contains(family);
}
