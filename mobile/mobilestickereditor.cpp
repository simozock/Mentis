#include "mobilestickereditor.h"

#include "iconlibrary.h"

#include <QCoreApplication>

MobileStickerEditor::MobileStickerEditor(QObject *parent) : QObject(parent), m_families(StickerEdit::labelFamilies()) {
    m_edit.changed = [this] { emit changed(); };
    m_edit.styleChanged = [this] { emit styleChanged(); };
    m_edit.selectionChanged = [this] {
        emit selectionChanged();
        emit stateChanged();
    };
    m_edit.canvas().stateChanged = [this] { emit stateChanged(); };
}

bool MobileStickerEditor::editable(const QString &path) { return path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive); }

bool MobileStickerEditor::open(const QString &path, const QString &collectionId) {
    m_path = path;
    m_collectionId = collectionId;
    m_collections.clear();
    for (const IconLibrary::Collection &c : IconLibrary::collections(IconLibrary::Kind::Sticker))
        if (!c.hidden) m_collections.append(QVariantMap{{QStringLiteral("value"), c.id}, {QStringLiteral("name"), c.name}});
    m_shift = false;
    const bool ok = editable(path) && m_edit.load(path);
    m_edit.canvas().setMode(StickerCanvas::Mode::Shapes);
    m_edit.canvas().setSelection({});
    applyToolOptions();
    emit opened();
    emit shiftChanged();
    emit changed();
    emit styleChanged();
    emit selectionChanged();
    emit stateChanged();
    return ok;
}

QString MobileStickerEditor::originalName() const { return m_path.isEmpty() ? QString() : IconLibrary::displayName(m_path); }

QVariantList MobileStickerEditor::colors() const {
    QVariantList out;
    for (const QColor &color : m_edit.colors()) out.append(color);
    return out;
}

QColor MobileStickerEditor::fillColor() const {
    const SvgDocument::ShapeStyle style = m_edit.style();
    return style.fill.isValid() ? style.fill : m_edit.lastFill();
}

void MobileStickerEditor::setMode(int mode) {
    if (mode < 0 || mode > int(StickerCanvas::Mode::Star) || mode == this->mode()) return;
    m_edit.canvas().setMode(StickerCanvas::Mode(mode));
}

void MobileStickerEditor::setShift(bool shift) {
    if (shift == m_shift) return;
    m_shift = shift;
    emit shiftChanged();
}

void MobileStickerEditor::applyToolOptions() {
    m_edit.setToolOptions(m_cornerRadius, m_sides, m_points);
    emit toolOptionsChanged();
}

void MobileStickerEditor::setCornerRadius(int px) {
    px = qBound(0, px, 128);  // as the desktop's field
    if (px == m_cornerRadius) return;
    m_cornerRadius = px;
    applyToolOptions();
}

void MobileStickerEditor::setSides(int sides) {
    sides = qBound(3, sides, 12);
    if (sides == m_sides) return;
    m_sides = sides;
    applyToolOptions();
}

void MobileStickerEditor::setPoints(int points) {
    points = qBound(3, points, 12);
    if (points == m_points) return;
    m_points = points;
    applyToolOptions();
}

void MobileStickerEditor::setMargin(int percent) {
    percent = qBound(1, percent, 50);
    if (percent == m_margin) return;
    m_margin = percent;
    emit toolOptionsChanged();
}

void MobileStickerEditor::editSelectedPoints() {
    const QStringList selection = m_edit.canvas().selection();
    if (selection.size() == 1) m_edit.canvas().editPoints(selection.first());
}

QVariantMap MobileStickerEditor::save(const QString &name, const QString &collectionId, const QString &newCollection) {
    m_edit.settle();  // what is shown is what is saved
    const QString clean = StickerEdit::cleanName(name);
    QString target = collectionId;
    if (target.isEmpty() && !newCollection.trimmed().isEmpty())
        target = IconLibrary::addCollection(IconLibrary::Kind::Sticker, newCollection.trimmed());
    const QString saved = clean.isEmpty() || target.isEmpty()
                              ? QString()
                              : IconLibrary::addData(IconLibrary::Kind::Sticker, target, clean + QStringLiteral(".svg"), m_edit.svg());
    if (saved.isEmpty()) return {{QStringLiteral("ok"), false}, {QStringLiteral("reason"), QCoreApplication::translate("IconPage", "The sticker could not be saved.")}};
    // Only added stickers can be deleted: the built-in ones stay in their collection.
    const bool own = m_path.startsWith(IconLibrary::folder(IconLibrary::Kind::Sticker));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("canDeleteOriginal"), own}};
}

bool MobileStickerEditor::deleteOriginal() {
    if (!m_path.startsWith(IconLibrary::folder(IconLibrary::Kind::Sticker))) return false;
    return IconLibrary::removeItem(IconLibrary::Kind::Sticker, m_collectionId, m_path);
}
