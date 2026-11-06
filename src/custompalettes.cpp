#include "custompalettes.h"

#include <QCoreApplication>
#include <QSettings>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>

namespace CustomPalettes {
namespace {
const QString kKey = QStringLiteral("palettes/custom");

QVariantList stored() { return QSettings().value(kKey).toList(); }

std::optional<NodeStyle::Palette> fromEntry(const QVariant &entry) {
    const QVariantMap map = entry.toMap();
    NodeStyle::Palette palette{map.value(QStringLiteral("id")).toString(), map.value(QStringLiteral("name")).toString(),
                               NodeStyle::PaletteGroup::Personalizzata, map.value(QStringLiteral("colors")).toStringList()};
    // Voce rovinata (impostazioni modificate a mano): si salta.
    if (palette.id.isEmpty() || palette.colors.size() != kColorCount) return std::nullopt;
    return palette;
}

QString normalized(const QString &name) { return name.trimmed().toCaseFolded(); }
}  // namespace

QList<NodeStyle::Palette> all() {
    QList<NodeStyle::Palette> result;
    for (const QVariant &entry : stored())
        if (const auto palette = fromEntry(entry)) result.append(*palette);
    return result;
}

std::optional<NodeStyle::Palette> find(const QString &id) {
    if (id.isEmpty()) return std::nullopt;
    for (const NodeStyle::Palette &palette : all())
        if (palette.id == id) return palette;
    return std::nullopt;
}

std::optional<NodeStyle::Palette> lookup(const QString &id) {
    if (const NodeStyle::Palette *base = NodeStyle::findPalette(id)) return *base;
    return find(id);
}

bool isNameTaken(const QString &name) {
    const QString wanted = normalized(name);
    if (wanted.isEmpty()) return false;
    for (const NodeStyle::Palette &palette : NodeStyle::basePalettes())  // English or translated
        if (normalized(palette.name) == wanted || normalized(NodeStyle::displayName(palette)) == wanted) return true;
    for (const NodeStyle::Palette &palette : all())
        if (normalized(palette.name) == wanted) return true;
    return false;
}

QString nextDefaultName() {
    for (int number = 1;; ++number) {
        const QString name = QCoreApplication::translate("CustomPalettes", "Custom %1").arg(number);
        if (!isNameTaken(name)) return name;
    }
}

QString add(const QString &name, const QStringList &colors, const QString &id) {
    if (colors.size() != kColorCount) return {};
    for (const QString &color : colors)
        if (!QColor(color).isValid()) return {};
    const QString paletteId = id.isEmpty() ? QStringLiteral("custom-") + QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
    if (lookup(paletteId)) return {};
    QVariantList entries = stored();
    QStringList names;
    for (const QString &color : colors) names.append(QColor(color).name());
    entries.append(QVariantMap{{QStringLiteral("id"), paletteId},
                               {QStringLiteral("name"), name.trimmed().isEmpty() ? nextDefaultName() : name.trimmed()},
                               {QStringLiteral("colors"), names}});
    QSettings().setValue(kKey, entries);
    return paletteId;
}

bool update(const QString &id, const std::optional<QString> &name, const std::optional<QStringList> &colors) {
    if (colors) {
        if (colors->size() != kColorCount) return false;
        for (const QString &color : *colors)
            if (!QColor(color).isValid()) return false;
    }
    QVariantList entries = stored();
    for (QVariant &entry : entries) {
        QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("id")).toString() != id) continue;
        if (name) {
            const QString clean = name->trimmed();
            if (clean.isEmpty()) return false;
            for (const QVariant &other : std::as_const(entries))
                if (other.toMap().value(QStringLiteral("id")).toString() != id
                    && other.toMap().value(QStringLiteral("name")).toString().compare(clean, Qt::CaseInsensitive) == 0)
                    return false;
            map.insert(QStringLiteral("name"), clean);
        }
        if (colors) {
            QStringList names;
            for (const QString &color : *colors) names.append(QColor(color).name());
            map.insert(QStringLiteral("colors"), names);
        }
        entry = map;
        QSettings().setValue(kKey, entries);
        return true;
    }
    return false;
}

bool remove(const QString &id) {
    QVariantList entries = stored();
    const qsizetype before = entries.size();
    entries.removeIf([&id](const QVariant &entry) { return entry.toMap().value(QStringLiteral("id")).toString() == id; });
    if (entries.size() == before) return false;
    QSettings().setValue(kKey, entries);
    return true;
}

DefaultTheme defaultTheme() {
    const QSettings settings;
    const auto palette = lookup(settings.value(QStringLiteral("theme/defaultPalette")).toString());
    DefaultTheme theme{palette.value_or(*NodeStyle::findPalette(NodeStyle::kDefaultPaletteId)), -1};
    if (palette) theme.variant = std::clamp(settings.value(QStringLiteral("theme/defaultVariant"), -1).toInt(), -1, NodeStyle::kVariantCount - 1);
    return theme;
}

void setDefaultTheme(const QString &paletteId, int variant) {
    QSettings settings;
    settings.setValue(QStringLiteral("theme/defaultPalette"), paletteId);
    settings.setValue(QStringLiteral("theme/defaultVariant"), variant);
}

}  // namespace CustomPalettes
