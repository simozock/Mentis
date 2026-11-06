#include "pitchtemplate.h"

#include "mindmapmodel.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCache>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QMutex>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <array>
#include <utility>

namespace Pitch {
namespace {
const std::array<std::pair<ItemStyle, const char *>, kItemStyleCount> kItemStyles{
    {{ItemStyle::Plain, "plain"}, {ItemStyle::Pill, "pill"}, {ItemStyle::Badge, "badge"}, {ItemStyle::Label, "label"}}};
const std::array<std::pair<Motif, const char *>, kMotifCount> kMotifs{
    {{Motif::None, "none"}, {Motif::Band, "band"}, {Motif::Clouds, "clouds"}, {Motif::Smudge, "smudge"}, {Motif::Bars, "bars"},
     {Motif::Halftone, "halftone"}, {Motif::Grain, "grain"}, {Motif::Lines, "lines"}, {Motif::Blobs, "blobs"},
     {Motif::Rules, "rules"}, {Motif::Frame, "frame"}}};
const std::array<std::pair<Corner, const char *>, 4> kCorners{
    {{Corner::TopLeft, "top-left"}, {Corner::TopRight, "top-right"}, {Corner::BottomLeft, "bottom-left"}, {Corner::BottomRight, "bottom-right"}}};

template <typename Enum, std::size_t N>
std::optional<Enum> fromKey(const QString &key, const std::array<std::pair<Enum, const char *>, N> &keys) {
    for (const auto &[value, stored] : keys)
        if (key == QLatin1String(stored)) return value;
    return std::nullopt;
}
template <typename Enum, std::size_t N>
QString keyOf(Enum value, const std::array<std::pair<Enum, const char *>, N> &keys) {
    for (const auto &[candidate, stored] : keys)
        if (candidate == value) return QString::fromLatin1(stored);
    return {};
}

// The colours' keys and members, in the order of the editor.
const std::array<std::pair<const char *, QColor Template::*>, 9> kColors{{{"background", &Template::background},
                                                                          {"text", &Template::text},
                                                                          {"title", &Template::title},
                                                                          {"line", &Template::line},
                                                                          {"fill", &Template::fill},
                                                                          {"coverBackground", &Template::coverBackground},
                                                                          {"coverText", &Template::coverText},
                                                                          {"topicBackground", &Template::topicBackground},
                                                                          {"topicText", &Template::topicText}}};
const std::array<std::pair<const char *, QByteArray Template::*>, 3> kPictures{
    {{"picture", &Template::picture}, {"coverPicture", &Template::coverPicture}, {"logo", &Template::logo}}};

// Decoded once per content: look() runs on every refresh of the Pitch tab.
QImage decoded(const QByteArray &data) {
    if (data.isEmpty()) return {};
    static QMutex mutex;
    static QCache<QByteArray, QImage> cache(48 * 1024);  // KB
    const QByteArray key = QCryptographicHash::hash(data, QCryptographicHash::Sha1);
    const QMutexLocker lock(&mutex);
    if (const QImage *hit = cache.object(key)) return *hit;
    QImage image = QImage::fromData(data);
    if (image.isNull()) return {};
    cache.insert(key, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() / 1024));
    return image;
}
}  // namespace

QJsonObject toJson(const Template &value) {
    QJsonObject object{{QStringLiteral("format"), QLatin1String(kTemplateFormat)},
                       {QStringLiteral("id"), value.id},
                       {QStringLiteral("name"), value.name},
                       {QStringLiteral("base"), themeKey(value.base)}};
    QJsonObject colors;
    for (const auto &[key, member] : kColors)
        if ((value.*member).isValid()) colors.insert(QLatin1String(key), (value.*member).name());
    if (!colors.isEmpty()) object.insert(QStringLiteral("colors"), colors);
    if (!value.titleFamily.isEmpty()) object.insert(QStringLiteral("titleFont"), value.titleFamily);
    if (!value.itemFamily.isEmpty()) object.insert(QStringLiteral("itemFont"), value.itemFamily);
    if (value.itemStyle) object.insert(QStringLiteral("itemStyle"), keyOf(*value.itemStyle, kItemStyles));
    if (value.motif) object.insert(QStringLiteral("motif"), keyOf(*value.motif, kMotifs));
    for (const auto &[key, member] : kPictures)
        if (!(value.*member).isEmpty()) object.insert(QLatin1String(key), QString::fromLatin1((value.*member).toBase64()));
    object.insert(QStringLiteral("logoCorner"), keyOf(value.logoCorner, kCorners));
    object.insert(QStringLiteral("logoSize"), value.logoSize);
    if (!value.footer.isEmpty()) object.insert(QStringLiteral("footer"), value.footer);
    if (value.numbers) object.insert(QStringLiteral("numbers"), true);
    return object;
}

std::optional<Template> templateFromJson(const QJsonObject &object) {
    Template value;
    value.id = object.value(QStringLiteral("id")).toString().trimmed();
    if (object.value(QStringLiteral("format")).toString() != QLatin1String(kTemplateFormat) || value.id.isEmpty()) return std::nullopt;
    value.name = object.value(QStringLiteral("name")).toString().trimmed();
    if (value.name.isEmpty()) value.name = QCoreApplication::translate("PitchTemplates", "Template");
    value.base = themeFromKey(object.value(QStringLiteral("base")).toString());
    const QJsonObject colors = object.value(QStringLiteral("colors")).toObject();
    for (const auto &[key, member] : kColors)
        if (const QColor color(colors.value(QLatin1String(key)).toString()); color.isValid()) value.*member = color;
    value.titleFamily = object.value(QStringLiteral("titleFont")).toString().trimmed();
    value.itemFamily = object.value(QStringLiteral("itemFont")).toString().trimmed();
    value.itemStyle = fromKey(object.value(QStringLiteral("itemStyle")).toString(), kItemStyles);
    value.motif = fromKey(object.value(QStringLiteral("motif")).toString(), kMotifs);
    for (const auto &[key, member] : kPictures) {
        const QByteArray data = QByteArray::fromBase64(object.value(QLatin1String(key)).toString().toLatin1());
        if (!data.isEmpty() && !decoded(data).isNull()) value.*member = data;
    }
    value.logoCorner = fromKey(object.value(QStringLiteral("logoCorner")).toString(), kCorners).value_or(Corner::BottomRight);
    value.logoSize = std::clamp(object.value(QStringLiteral("logoSize")).toInt(10), kMinLogoSize, kMaxLogoSize);
    value.footer = object.value(QStringLiteral("footer")).toString();
    value.numbers = object.value(QStringLiteral("numbers")).toBool(false);
    return value;
}

Look look(const MindMapModel &model, const Template &value) {
    Look result = look(model, value.base);
    const auto put = [](QColor &target, const QColor &color) {
        if (color.isValid()) target = color;
    };
    put(result.background, value.background);
    put(result.text, value.text);
    put(result.title, value.title);
    put(result.line, value.line);
    put(result.fill, value.fill);
    put(result.coverBackground, value.coverBackground);
    put(result.coverText, value.coverText);
    put(result.topicBackground, value.topicBackground);
    put(result.topicText, value.topicText);
    if (!value.titleFamily.isEmpty()) result.titleFont.family = value.titleFamily;
    if (!value.itemFamily.isEmpty()) result.itemFont.family = value.itemFamily;
    if (value.itemStyle) result.itemStyle = *value.itemStyle;
    if (value.motif) result.motif = *value.motif;
    result.picture = decoded(value.picture);
    result.coverPicture = decoded(value.coverPicture);
    result.logo = decoded(value.logo);
    result.logoCorner = value.logoCorner;
    result.logoSize = value.logoSize / 100.0;
    result.footer = value.footer;
    result.numbers = value.numbers;
    return result;
}

std::optional<Template> sheetTemplate(const MindMapModel &model) {
    return model.pitchTemplate.isEmpty() ? std::nullopt : templateFromJson(model.pitchTemplate);
}

Look sheetLook(const MindMapModel &model) {
    if (const auto value = sheetTemplate(model)) return look(model, *value);
    return look(model, themeFromKey(model.pitchTheme));
}

QByteArray loadPicture(const QString &path, int maxSide, bool alwaysPng, QString *error) {
    QImageReader reader(path);
    reader.setAutoTransform(true);  // photos keep their EXIF orientation
    QImage image = reader.read();
    if (image.isNull()) {
        if (error) *error = reader.errorString();
        return {};
    }
    if (std::max(image.width(), image.height()) > maxSide) image = image.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    const bool png = alwaysPng || image.hasAlphaChannel();
    if (!image.save(&buffer, png ? "PNG" : "JPEG", png ? -1 : 85)) {
        if (error) *error = QCoreApplication::translate("PitchTemplates", "The picture could not be encoded.");
        return {};
    }
    return data;
}

}  // namespace Pitch

namespace PitchTemplates {
namespace {
QString fileOf(const QString &id) {
    // The id names the file: only the characters a file name allows everywhere.
    QString safe = id;
    for (QChar &c : safe)
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_')) c = QLatin1Char('_');
    return QDir(folder()).filePath(safe + QLatin1Char('.') + QLatin1String(Pitch::kTemplateSuffix));
}
}  // namespace

QString folder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("pitch-templates"));
}

QList<Pitch::Template> all() {
    QList<Pitch::Template> result;
    const QDir dir(folder());
    for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.") + QLatin1String(Pitch::kTemplateSuffix)}, QDir::Files))
        if (auto value = readFile(info.filePath())) result.append(std::move(*value));
    std::sort(result.begin(), result.end(), [](const Pitch::Template &a, const Pitch::Template &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}

std::optional<Pitch::Template> find(const QString &id) {
    if (id.isEmpty()) return std::nullopt;
    auto value = readFile(fileOf(id));
    if (value && value->id != id) return std::nullopt;
    return value;
}

bool save(const Pitch::Template &value, QString *error) {
    if (value.id.isEmpty()) return false;
    if (!QDir().mkpath(folder())) {
        if (error) *error = QCoreApplication::translate("PitchTemplates", "The folder %1 could not be created.").arg(QDir::toNativeSeparators(folder()));
        return false;
    }
    return writeFile(value, fileOf(value.id), error);
}

bool remove(const QString &id) { return !id.isEmpty() && QFile::remove(fileOf(id)); }

std::optional<Pitch::Template> readFile(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return std::nullopt;
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    auto value = document.isObject() ? Pitch::templateFromJson(document.object()) : std::nullopt;
    if (!value && error) *error = QCoreApplication::translate("PitchTemplates", "The file is not a Mentis Pitch template.");
    return value;
}

bool writeFile(const Pitch::Template &value, const QString &path, QString *error) {
    // QSaveFile: a failed write leaves the file as it was.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(Pitch::toJson(value)).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

Pitch::Template create(Pitch::Theme base) {
    Pitch::Template value;
    value.id = QStringLiteral("tpl-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    value.base = base;
    QStringList names;
    for (const Pitch::Template &other : all()) names.append(other.name.toCaseFolded());
    for (int number = 1;; ++number) {
        value.name = QCoreApplication::translate("PitchTemplates", "Template %1").arg(number);
        if (!names.contains(value.name.toCaseFolded())) break;
    }
    return value;
}
}  // namespace PitchTemplates
