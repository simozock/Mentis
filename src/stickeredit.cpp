#include "stickeredit.h"

#include "nodegeometry.h"

#include <QFile>
#include <QFontDatabase>
#include <QSvgRenderer>
#include <QTimer>

#include <cmath>

namespace {
qreal normalizedAngle(qreal degrees) {
    qreal angle = std::fmod(degrees, 360.0);
    if (angle > 180) angle -= 360;
    if (angle <= -180) angle += 360;
    return angle;
}
}  // namespace

StickerEdit::StickerEdit() : m_textTimer(std::make_unique<QTimer>()) {
    m_textTimer->setSingleShot(true);
    m_textTimer->setInterval(kTypingPauseMs);
    QObject::connect(m_textTimer.get(), &QTimer::timeout, m_textTimer.get(), [this] { flushText(); });
    m_labelLook.family = labelFamilies().value(0);
    m_canvas.frameChanged = [this](const QRectF &box) { setFrame(box); };
    m_canvas.labelMoved = [this](const QPointF &position) { setLabelPosition(position); };
    m_canvas.shapesTransformed = [this](const QStringList &ids, const QTransform &matrix, bool nudge) {
        m_document.transformShapes(ids, matrix);
        commit(nudge ? QStringLiteral("nudge") : QString());
    };
    m_canvas.pathChanged = [this](const QString &id, const QList<SvgDocument::SubPath> &path) {
        m_document.setPath(id, path);
        commit();
    };
    m_canvas.removeRequested = [this](const QStringList &ids) { removeShapes(ids); };
    m_canvas.selectionChanged = [this] {
        // One shape chosen: the style shows its own, so a change starts from it.
        const QStringList selection = m_canvas.selection();
        if (selection.size() == 1) {
            m_style = m_document.shapeStyle(selection.first());
            m_style.strokeWidth /= unitsPerPx();
            if (styleChanged) styleChanged();
        }
        if (selectionChanged) selectionChanged();
    };
    m_canvas.shapeDrawn = [this](const QList<SvgDocument::SubPath> &path) { addShape(path); };
}

StickerEdit::~StickerEdit() = default;

bool StickerEdit::load(const QString &path) {
    QFile file(path);
    const QByteArray data = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    m_valid = m_document.load(data) && QSvgRenderer(data).isValid();
    if (m_valid) m_document.ensureShapeIds();  // stage 3: every drawn shape is picked by its id
    m_history = {m_document.data()};
    m_step = 0;
    m_lastKey.clear();
    readLabelLook();
    m_canvas.setDocument(m_document);
    setToolOptions(m_cornerPx, m_sides, m_points);
    return m_valid;
}

QStringList StickerEdit::labelFamilies() {
    NodeGeometry::loadBundledFonts();
    QStringList result;
    for (const QString &family : {QStringLiteral("Atkinson Hyperlegible"), QStringLiteral("Atkinson Hyperlegible Next"), QStringLiteral("Roboto"),
                                  QStringLiteral("Montserrat"), QStringLiteral("Arvo"), QStringLiteral("OpenDyslexic")})
        if (QFontDatabase::hasFamily(family)) result.append(family);
    return result;
}

QString StickerEdit::cleanName(const QString &text) {
    QString name = text.trimmed();
    // Characters a file name cannot hold on Windows, macOS or Linux.
    static const QString forbidden = QStringLiteral("\\/:*?\"<>|");
    for (QChar &c : name)
        if (forbidden.contains(c)) c = QLatin1Char(' ');
    return name.simplified();
}

qreal StickerEdit::unitsPerPx() const {
    const QRectF box = m_document.viewBox();
    const qreal longer = qMax(box.width(), box.height());
    return longer > 0 ? longer / SvgDocument::kStickerPx : 1;
}

void StickerEdit::setToolOptions(qreal cornerPx, int sides, int points) {
    m_cornerPx = cornerPx;
    m_sides = sides;
    m_points = points;
    m_canvas.setToolOptions(cornerPx * unitsPerPx(), sides, points);
}

void StickerEdit::addShape(const QList<SvgDocument::SubPath> &path) {
    settle();
    SvgDocument::ShapeStyle style = m_style;
    style.strokeWidth = m_style.strokeWidth * unitsPerPx();
    if (style.strokeWidth <= 0) style.stroke = QColor();
    const QString id = m_document.addPath(path, style);
    if (id.isEmpty()) return;
    commit();
    m_canvas.setSelection({id});
}

void StickerEdit::writeStyle(const SvgDocument::ShapeStyle &style, int parts) {
    m_style = style;
    if (m_style.fill.isValid()) m_lastFill = m_style.fill;
    const QStringList selection = m_canvas.selection();
    if (!selection.isEmpty()) {
        settle();
        SvgDocument::ShapeStyle written = style;
        written.strokeWidth = style.strokeWidth * unitsPerPx();
        if (written.strokeWidth <= 0) written.stroke = QColor();
        m_document.setShapeStyle(selection, written, parts);
        commit(parts == (SvgDocument::Stroke | SvgDocument::StrokeWidth) ? QStringLiteral("strokeWidth") : QString());
    }
    if (styleChanged) styleChanged();
}

void StickerEdit::setFillNone(bool none) {
    SvgDocument::ShapeStyle changed = m_style;
    changed.fill = none ? QColor() : (m_lastFill.isValid() ? m_lastFill : QColor(Qt::black));
    writeStyle(changed, SvgDocument::Fill);
}

void StickerEdit::setFillColor(const QColor &color) {
    SvgDocument::ShapeStyle changed = m_style;
    changed.fill = color;
    writeStyle(changed, SvgDocument::Fill);
}

void StickerEdit::setStrokeColor(const QColor &color) {
    SvgDocument::ShapeStyle changed = m_style;
    changed.stroke = color;
    if (changed.strokeWidth <= 0) changed.strokeWidth = 1;
    writeStyle(changed, SvgDocument::Stroke | SvgDocument::StrokeWidth);
}

void StickerEdit::setStrokeWidth(qreal width) {
    SvgDocument::ShapeStyle changed = m_style;
    changed.strokeWidth = width;
    if (width > 0 && !changed.stroke.isValid()) changed.stroke = Qt::black;
    writeStyle(changed, SvgDocument::Stroke | SvgDocument::StrokeWidth);
}

void StickerEdit::removeShapes(const QStringList &ids) {
    settle();
    m_document.removeShapes(ids);
    commit();
}

void StickerEdit::reorderShapes(SvgDocument::Order order) {
    settle();
    m_document.reorderShapes(m_canvas.selection(), order);
    commit();
}

void StickerEdit::setColor(int index, const QColor &color) {
    const QList<QColor> current = m_document.colors();
    if (index >= 0 && index < current.size()) replaceColor(current[index], color);
}

void StickerEdit::replaceColor(const QColor &from, const QColor &to) {
    settle();
    if (!from.isValid() || !to.isValid() || from.rgb() == to.rgb()) return;
    m_document.replaceColor(from, to);
    commit();
}

void StickerEdit::rotateBy(qreal degrees) {
    settle();
    SvgDocument::Transform t = m_document.transform();
    t.rotation = normalizedAngle(t.rotation + degrees);
    m_document.setTransform(t);
    commit();  // each quarter turn is a step
}

void StickerEdit::setRotation(qreal degrees) {
    settle();
    // The angle field's steps are one change: it starts again from the file before the first one, so
    // the frame widens once for the final angle, not at every degree.
    if (m_lastKey == QLatin1String("angle") && m_step > 0 && m_step + 1 == m_history.size()) m_document.load(m_history[m_step - 1]);
    SvgDocument::Transform t = m_document.transform();
    t.rotation = normalizedAngle(degrees);
    m_document.setTransform(t);
    commit(QStringLiteral("angle"));
}

void StickerEdit::flipHorizontally() {
    settle();
    SvgDocument::Transform t = m_document.transform();
    t.flipH = !t.flipH;
    m_document.setTransform(t);
    commit();
}

void StickerEdit::flipVertically() {
    settle();
    SvgDocument::Transform t = m_document.transform();
    t.flipV = !t.flipV;
    m_document.setTransform(t);
    commit();
}

void StickerEdit::setFrame(const QRectF &viewBox) {
    settle();
    m_document.setViewBox(viewBox);
    commit();
}

void StickerEdit::fitToDrawing() {
    const QRectF bounds = m_document.drawingBounds();
    if (!bounds.isEmpty()) setFrame(bounds);
}

void StickerEdit::addMargin(qreal percent) {
    const QRectF box = m_document.viewBox();
    const qreal margin = qMax(box.width(), box.height()) * percent / 100;
    setFrame(box.adjusted(-margin, -margin, margin, margin));
}

void StickerEdit::setLabelText(const QString &text) {
    applyLabel([&](SvgDocument::Label &l) { l.text = text.trimmed(); }, QStringLiteral("text"));
}

void StickerEdit::typeLabelText(const QString &text) {
    if (m_document.data().size() < kLargeBytes) {
        setLabelText(text);
        return;
    }
    m_pendingText = text;
    m_textPending = true;
    m_textTimer->start();
}

void StickerEdit::setLabelPosition(const QPointF &position) {
    applyLabel([&](SvgDocument::Label &l) { l.position = position; }, QString());
}

void StickerEdit::setLabelFamily(const QString &family) {
    m_labelLook.family = family;
    applyLabel([&](SvgDocument::Label &l) { l.family = family; }, QString());
}

void StickerEdit::setLabelSizePercent(qreal percent) {
    m_labelLook.sizePercent = percent;
    const QRectF box = m_document.viewBox();
    applyLabel([&](SvgDocument::Label &l) { l.size = qMax(box.width(), box.height()) * percent / 100; }, QStringLiteral("size"));
}

void StickerEdit::setLabelColor(const QColor &color) {
    m_labelLook.color = color;
    applyLabel([&](SvgDocument::Label &l) { l.color = color; }, QStringLiteral("color"));
}

void StickerEdit::setLabelBold(bool bold) {
    m_labelLook.bold = bold;
    applyLabel([&](SvgDocument::Label &l) { l.bold = bold; }, QString());
}

void StickerEdit::applyLabel(const std::function<void(SvgDocument::Label &)> &change, const QString &key) {
    settle();
    SvgDocument::Label label = m_document.label();
    if (label.text.isEmpty()) {
        // A new label: the controls' font, size, colour and weight, in the middle of the frame.
        const QRectF box = m_document.viewBox();
        label.family = m_labelLook.family;
        label.size = qMax(box.width(), box.height()) * m_labelLook.sizePercent / 100;
        label.bold = m_labelLook.bold;
        label.color = m_labelLook.color;
        label.position = QPointF(box.center().x(), box.center().y() + label.size / 3);
    }
    change(label);
    if (label.text.isEmpty() && m_document.label().text.isEmpty()) {
        if (changed) changed();  // a look chosen before any text: kept for the new label
        return;
    }
    m_document.setLabel(label);
    commit(key);
}

void StickerEdit::readLabelLook() {
    const SvgDocument::Label label = m_document.label();
    if (label.text.isEmpty()) return;
    m_labelLook.family = label.family;
    const QRectF box = m_document.viewBox();
    m_labelLook.sizePercent = qRound(label.size * 100 / qMax(box.width(), box.height()));
    m_labelLook.bold = label.bold;
    m_labelLook.color = label.color;
}

void StickerEdit::flushText() {
    // A flag, not the timer: at its timeout a single-shot timer is no longer active.
    if (!m_textPending) return;
    m_textPending = false;
    m_textTimer->stop();
    setLabelText(m_pendingText);
}

void StickerEdit::settle() {
    m_canvas.finishPen();  // a pen path being drawn is kept as it is
    m_canvas.flush();
    flushText();
}

void StickerEdit::commit(const QString &key) {
    const QByteArray data = m_document.data();
    if (data == m_history.value(m_step)) return;
    if (!key.isEmpty() && key == m_lastKey && m_step > 0 && m_step + 1 == m_history.size()) {
        m_history[m_step] = data;  // typing or a spin box: one step for the whole change
    } else {
        m_history.resize(m_step + 1);  // a change after Undo drops the steps that were undone
        m_history.append(data);
        m_step = int(m_history.size()) - 1;
        // At most kHistoryBytes of steps (a 2 MB sticker would hold 200 MB in 100 steps): the
        // oldest go, the original (step 0) stays so "modified" and "discard" still know it.
        qsizetype total = 0;
        for (const QByteArray &step : std::as_const(m_history)) total += step.size();
        while (total > kHistoryBytes && m_history.size() > 2) {
            total -= m_history[1].size();
            m_history.removeAt(1);
            --m_step;
        }
    }
    m_lastKey = key;
    apply();
}

void StickerEdit::undo() {
    settle();
    if (m_step == 0) return;
    --m_step;
    m_lastKey.clear();
    m_document.load(m_history[m_step]);
    apply();
}

void StickerEdit::redo() {
    settle();
    if (m_step + 1 >= m_history.size()) return;
    ++m_step;
    m_lastKey.clear();
    m_document.load(m_history[m_step]);
    apply();
}

void StickerEdit::apply() {
    readLabelLook();
    m_canvas.setDocument(m_document);
    if (changed) changed();
}
