#pragma once

// FEAT-089 (docs/features/FEAT-089-template-pitch.md): Pitch templates. A template is a built-in
// theme with values of its own on top (colours, fonts, item style, motif, pictures, logo, footer).
// The library of the app keeps one .mentis-theme file (JSON, pictures in base64) per template; a
// sheet keeps a copy of its template (MindMapModel::pitchTemplate) so it opens the same everywhere.

#include "pitch.h"

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

class MindMapModel;

namespace Pitch {

struct Template {
    QString id;
    QString name;
    Theme base = Theme::Light;
    // On top of the base theme's; invalid / empty / nullopt = the base theme's.
    QColor background, text, title, line, fill, coverBackground, coverText, topicBackground, topicText;
    QString titleFamily, itemFamily;
    std::optional<ItemStyle> itemStyle;
    std::optional<Motif> motif;
    QByteArray picture;       // encoded (PNG or JPEG); empty = none
    QByteArray coverPicture;
    QByteArray logo;
    Corner logoCorner = Corner::BottomRight;
    int logoSize = 10;        // percent of the slide's height, kMinLogoSize..kMaxLogoSize
    QString footer;
    bool numbers = false;
    bool operator==(const Template &) const = default;
};
constexpr int kMinLogoSize = 5;
constexpr int kMaxLogoSize = 25;
constexpr int kPictureMaxSide = 1920;
constexpr int kLogoMaxSide = 512;
inline constexpr char kTemplateFormat[] = "mentis-theme-1";
inline constexpr char kTemplateSuffix[] = "mentis-theme";

QJsonObject toJson(const Template &value);
// nullopt: not a template (no format or id). Unknown keys are ignored, wrong values read as unset,
// pictures that do not decode are dropped.
std::optional<Template> templateFromJson(const QJsonObject &object);
// The base theme's look with the template's values on top.
Look look(const MindMapModel &model, const Template &value);
// What the sheet presents with: its template if it has one, otherwise its theme.
Look sheetLook(const MindMapModel &model);
std::optional<Template> sheetTemplate(const MindMapModel &model);

// A picture chosen for a template, read from the file and reduced to maxSide: PNG if it has
// transparency or alwaysPng, otherwise JPEG 85. Empty with error set when it cannot be read.
QByteArray loadPicture(const QString &path, int maxSide, bool alwaysPng, QString *error = nullptr);

}  // namespace Pitch

// The library of the app: the files in the app data folder, "pitch-templates".
namespace PitchTemplates {
QString folder();
// By name, ignoring case; unreadable files are skipped.
QList<Pitch::Template> all();
std::optional<Pitch::Template> find(const QString &id);
// Writes the template's file (a new one or over the one with its id).
bool save(const Pitch::Template &value, QString *error = nullptr);
bool remove(const QString &id);
std::optional<Pitch::Template> readFile(const QString &path, QString *error = nullptr);
bool writeFile(const Pitch::Template &value, const QString &path, QString *error = nullptr);
// A new template on base: a fresh id and the first free "Template N".
Pitch::Template create(Pitch::Theme base);
}  // namespace PitchTemplates
