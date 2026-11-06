// FEAT-046: lingua dell'interfaccia. Scelta salvata nelle impostazioni, lingua effettiva all'avvio
// e pagina "Language" della finestra Impostazioni. Impostazioni separate, piattaforma offscreen.
#include "custompalettes.h"
#include "iconlibrary.h"
#include "iconpanel.h"
#include "localization.h"
#include "markers.h"
#include "nodestyle.h"
#include "settingsdialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QTranslator>

class LocalizationTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void effectiveLanguageFollowsPreferenceThenSystem();
    void preferenceIsStoredAndValidated();
    void onlyCompleteLanguagesAreOffered();
    void nativeNamesComeFromTheLocale();
    void languagesAreOrderedEnglishFirstThenByName();
    void systemLanguagesMatchRegionalCodes();
    void builtInPaletteNamesFollowTheLanguage();
    void defaultCollectionNamesFollowTheLanguage();
    void settingsPageOffersTheLanguages();
    void searchFindsEnglishAndTranslatedNames();
};

void LocalizationTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);  // sticker library in a test folder
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisLocalizationTest"));
}

void LocalizationTest::init() { QSettings().remove(QStringLiteral("ui/language")); }

void LocalizationTest::effectiveLanguageFollowsPreferenceThenSystem() {
    using Localization::effectiveLanguage;
    QCOMPARE(effectiveLanguage(QString(), {QStringLiteral("it-CH"), QStringLiteral("en-US")}), QStringLiteral("it"));
    QCOMPARE(effectiveLanguage(QString(), {QStringLiteral("de-DE"), QStringLiteral("it-IT")}), QStringLiteral("it"));
    QCOMPARE(effectiveLanguage(QString(), {QStringLiteral("en-GB")}), QStringLiteral("en"));
    QCOMPARE(effectiveLanguage(QString(), {QStringLiteral("fr-FR")}), QStringLiteral("en"));  // non disponibile
    QCOMPARE(effectiveLanguage(QStringLiteral("en"), {QStringLiteral("it-IT")}), QStringLiteral("en"));
    QCOMPARE(effectiveLanguage(QStringLiteral("it"), {QStringLiteral("en-US")}), QStringLiteral("it"));
    QCOMPARE(effectiveLanguage(QStringLiteral("xx"), {QStringLiteral("it-IT")}), QStringLiteral("it"));
}

void LocalizationTest::preferenceIsStoredAndValidated() {
    QCOMPARE(Localization::preference(), QString());
    Localization::setPreference(QStringLiteral("en"));
    QCOMPARE(Localization::preference(), QStringLiteral("en"));
    Localization::setPreference(QString());
    QCOMPARE(Localization::preference(), QString());
    QSettings().setValue(QStringLiteral("ui/language"), QStringLiteral("xx"));
    QCOMPARE(Localization::preference(), QString());  // valore sconosciuto = automatica
}

// FEAT-046: fr, de and es have files but too few translations yet, so they are not offered.
void LocalizationTest::onlyCompleteLanguagesAreOffered() {
    QCOMPARE(Localization::availableLanguages(), QStringList({QStringLiteral("en"), QStringLiteral("it")}));
    Localization::setPreference(QStringLiteral("fr"));
    QCOMPARE(Localization::preference(), QString());  // a language not offered counts as automatic
}

// FEAT-046: any language code added as a file gets its name from QLocale, no list in the code.
void LocalizationTest::nativeNamesComeFromTheLocale() {
    using Localization::nativeName;
    QCOMPARE(nativeName(QStringLiteral("en")), QStringLiteral("English"));
    QCOMPARE(nativeName(QStringLiteral("it")), QStringLiteral("Italiano"));
    QCOMPARE(nativeName(QStringLiteral("fr")), QStringLiteral("Français"));
    QCOMPARE(nativeName(QStringLiteral("de")), QStringLiteral("Deutsch"));
    QCOMPARE(nativeName(QStringLiteral("es")), QStringLiteral("Español"));  // not "español de España"
    QCOMPARE(nativeName(QStringLiteral("pt")), QStringLiteral("Português"));
    QCOMPARE(nativeName(QStringLiteral("pt_BR")), QStringLiteral("Português (Brasil)"));
    QCOMPARE(nativeName(QStringLiteral("pt_PT")), QStringLiteral("Português europeu"));
    QCOMPARE(nativeName(QStringLiteral("uk")), QStringLiteral("Українська"));
    QCOMPARE(nativeName(QStringLiteral("zh_Hans")), QStringLiteral("简体中文"));
    QCOMPARE(nativeName(QStringLiteral("zh_Hant")), QStringLiteral("繁體中文"));
    QCOMPARE(nativeName(QStringLiteral("xx")), QStringLiteral("xx"));
}

void LocalizationTest::languagesAreOrderedEnglishFirstThenByName() {
    const QStringList codes{QStringLiteral("it"), QStringLiteral("pt_BR"), QStringLiteral("de"), QStringLiteral("en"),
                            QStringLiteral("uk"), QStringLiteral("es"), QStringLiteral("it")};
    QCOMPARE(Localization::orderedLanguages(codes),
             QStringList({QStringLiteral("en"), QStringLiteral("de"), QStringLiteral("es"), QStringLiteral("it"),
                          QStringLiteral("pt_BR"), QStringLiteral("uk")}));
    QCOMPARE(Localization::orderedLanguages({}), QStringList{QStringLiteral("en")});
}

void LocalizationTest::systemLanguagesMatchRegionalCodes() {
    using Localization::effectiveLanguage;
    const QStringList available{QStringLiteral("en"), QStringLiteral("it"), QStringLiteral("pt_BR"), QStringLiteral("zh_Hans")};
    QCOMPARE(effectiveLanguage(QString(), {QStringLiteral("pt-BR")}, available), QStringLiteral("pt_BR"));
    QCOMPARE(effectiveLanguage(QString(), {QStringLiteral("pt-PT")}, available), QStringLiteral("pt_BR"));
    QCOMPARE(effectiveLanguage(QString(), {QStringLiteral("zh-Hans-CN")}, available), QStringLiteral("zh_Hans"));
    QCOMPARE(effectiveLanguage(QString(), {QStringLiteral("zh-CN")}, available), QStringLiteral("zh_Hans"));
    QCOMPARE(effectiveLanguage(QString(), {QStringLiteral("zh-TW")}, available), QStringLiteral("en"));  // another script
    QCOMPARE(effectiveLanguage(QString(), {QStringLiteral("it-CH")}, available), QStringLiteral("it"));
    QCOMPARE(effectiveLanguage(QStringLiteral("pt_BR"), {QStringLiteral("it-IT")}, available), QStringLiteral("pt_BR"));
    const QStringList portuguese{QStringLiteral("en"), QStringLiteral("pt"), QStringLiteral("pt_BR")};
    QCOMPARE(effectiveLanguage(QString(), {QStringLiteral("pt-BR")}, portuguese), QStringLiteral("pt_BR"));
    QCOMPARE(effectiveLanguage(QString(), {QStringLiteral("pt-PT")}, portuguese), QStringLiteral("pt"));
}

namespace {
// The Italian translation installed for the scope of a test.
struct ItalianTranslation {
    QTranslator translator;
    ItalianTranslation() {
        QVERIFY(translator.load(QStringLiteral("mentis_it"), QStringLiteral(MENTIS_QM_DIR)));
        QCoreApplication::installTranslator(&translator);
    }
    ~ItalianTranslation() { QCoreApplication::removeTranslator(&translator); }
};
}  // namespace

// FEAT-046 part B: base palettes keep their id; the name follows the language, a custom one does not.
void LocalizationTest::builtInPaletteNamesFollowTheLanguage() {
    const NodeStyle::Palette *dawn = NodeStyle::findPalette(QStringLiteral("alba"));
    QVERIFY(dawn);
    QCOMPARE(NodeStyle::displayName(*dawn), QStringLiteral("Dawn"));
    const NodeStyle::Palette mine{QStringLiteral("custom-x"), QStringLiteral("Dawn"), NodeStyle::PaletteGroup::Personalizzata, {}};
    {
        ItalianTranslation italian;
        QCOMPARE(NodeStyle::displayName(*dawn), QStringLiteral("Alba"));
        QCOMPARE(NodeStyle::displayName(*NodeStyle::findPalette(QStringLiteral("te-verde"))), QStringLiteral("Tè verde"));
        QCOMPARE(NodeStyle::displayName(mine), QStringLiteral("Dawn"));
        QVERIFY(CustomPalettes::isNameTaken(QStringLiteral("alba")));
        QVERIFY(CustomPalettes::isNameTaken(QStringLiteral("dawn")));
    }
    QCOMPARE(NodeStyle::displayName(*dawn), QStringLiteral("Dawn"));
}

// FEAT-046 part B: default collection names are saved empty and follow the language; a name the
// user typed stays as written.
void LocalizationTest::defaultCollectionNamesFollowTheLanguage() {
    using IconLibrary::Kind;
    QDir(IconLibrary::folder(Kind::Sticker)).removeRecursively();
    const QString own = IconLibrary::addCollection(Kind::Sticker, QString());
    const QString renamed = IconLibrary::addCollection(Kind::Sticker, QStringLiteral("Trip"));
    const QString objects = QStringLiteral("builtin:objects");
    QCOMPARE(IconLibrary::collection(Kind::Sticker, own).name, QStringLiteral("My stickers"));
    QCOMPARE(IconLibrary::collection(Kind::Sticker, objects).name, QStringLiteral("Objects"));

    QFile file(IconLibrary::folder(Kind::Sticker) + QStringLiteral("/library.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QHash<QString, QString> saved;
    for (const QJsonValue &value : QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("collections")).toArray())
        saved.insert(value[QStringLiteral("id")].toString(), value[QStringLiteral("name")].toString());
    QCOMPARE(saved.value(own, QStringLiteral("?")), QString());
    QCOMPARE(saved.value(objects, QStringLiteral("?")), QString());
    QCOMPARE(saved.value(renamed), QStringLiteral("Trip"));

    ItalianTranslation italian;
    QCOMPARE(IconLibrary::collection(Kind::Sticker, own).name, QStringLiteral("I miei adesivi"));
    QCOMPARE(IconLibrary::collection(Kind::Sticker, objects).name, QStringLiteral("Oggetti"));
    QCOMPARE(IconLibrary::collection(Kind::Sticker, renamed).name, QStringLiteral("Trip"));
    QCOMPARE(IconLibrary::collection(Kind::Marker, QStringLiteral("builtin:priority")).name, QStringLiteral("Priorità"));
}

void LocalizationTest::settingsPageOffersTheLanguages() {
    Localization::setPreference(QStringLiteral("en"));
    SettingsDialog dialog;
    auto *pages = dialog.findChild<QListWidget *>();
    QVERIFY(pages);
    QVERIFY(!pages->findItems(QStringLiteral("General"), Qt::MatchExactly).isEmpty());  // FEAT-035: lingua nella scheda Generale
    auto *select = dialog.findChild<QComboBox *>(QStringLiteral("LanguageSelect"));
    QVERIFY(select);
    QCOMPARE(select->count(), 3);
    QVERIFY(select->itemText(0).startsWith(QStringLiteral("Automatic")));
    QCOMPARE(select->itemText(1), QStringLiteral("English"));
    QCOMPARE(select->itemText(2), QStringLiteral("Italiano"));
    QCOMPARE(dialog.languagePreference(), QStringLiteral("en"));  // parte dalla scelta salvata
    select->setCurrentIndex(2);
    QCOMPARE(dialog.languagePreference(), QStringLiteral("it"));
    QCOMPARE(Localization::preference(), QStringLiteral("it"));  // FEAT-035: salvata subito, senza OK
    select->setCurrentIndex(0);
    QCOMPARE(dialog.languagePreference(), QString());
}

// FEAT-051 tappa 3 (user's decision 2026-11-05): the panel's search is in English in every language:
// in Italian "flag" finds the flags (shown as "Bandiera ..."), "bandiera" finds nothing.
void LocalizationTest::searchFindsEnglishAndTranslatedNames() {
    ItalianTranslation italian;
    QCOMPARE(Markers::label(QStringLiteral("flag:red")), QStringLiteral("Bandiera rossa"));
    QCOMPARE(Markers::label(QStringLiteral("flag:red"), false), QStringLiteral("Red flag"));
    QCOMPARE(IconLibrary::englishName(IconLibrary::Kind::Sticker, QStringLiteral("builtin:cute-animals")), QStringLiteral("Cute animals"));
    const auto shownAfter = [](IconPage &page, const QString &text) {
        page.searchField()->setText(text);
        page.reload();  // without the pause in the typing
        return page.shownPaths();
    };
    IconPage markers(IconLibrary::Kind::Marker);
    const QStringList flags = shownAfter(markers, QStringLiteral("flag"));
    QCOMPARE(flags.size(), 6);
    QVERIFY(shownAfter(markers, QStringLiteral("bandiera")).isEmpty());
    QCOMPARE(shownAfter(markers, QStringLiteral("red flag")), QStringList{QStringLiteral("flag:red")});
    IconPage stickers(IconLibrary::Kind::Sticker);
    const QStringList cute = shownAfter(stickers, QStringLiteral("cute animals"));  // the collection, named "Animali teneri"
    QCOMPARE(cute.size(), 19);
    QVERIFY(shownAfter(stickers, QStringLiteral("teneri")).isEmpty());
}

QTEST_MAIN(LocalizationTest)
#include "localization_test.moc"
