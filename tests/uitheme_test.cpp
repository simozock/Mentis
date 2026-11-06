// FEAT-035 parte A: token dell'interfaccia scura e sostituzione nei fogli di stile.
// FEAT-035 part B: font, text size and density (separate settings, offscreen).
#include "ganttview.h"
#include "mindmapmodel.h"
#include "mindmapview.h"
#include "nodeitem.h"
#include "uitheme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTreeView>
#include <QSettings>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTest>

class UiThemeTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();
    void tokensMatchTheMeasuredPalette();
    void styleSheetsUseTheTokens();
    void noOldDarkColorsLeftInTheSources();
    void sizesScaleFromFourteen();
    void openAndNewStyleSheetsFollowTheSize();
    void applicationFontFollowsSizeAndFamily();
    void appearanceIsSavedAndInvalidValuesFallBack();
    void bundledFamiliesAreRegistered();
    void compactDensityGoesOneStepDown();
    void styleSheetHeightsShrinkOnceWhenChangedLive();

private:
    qreal m_systemPointSize = 0;
};

void UiThemeTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisUiThemeTest"));
    QSettings().clear();
    NodeItem::loadInterfaceFont();  // as main(): Atkinson at the system's size
    QFont font = QApplication::font();
    font.setFamily(UiTheme::kDefaultFontFamily);
    QApplication::setFont(font);
    m_systemPointSize = QApplication::font().pointSizeF();
    UiTheme::loadAppearance();
}

void UiThemeTest::init() { UiTheme::setAppearance({}); }

void UiThemeTest::cleanupTestCase() { QSettings().clear(); }

void UiThemeTest::tokensMatchTheMeasuredPalette() {
    using UiTheme::Token;
    QCOMPARE(UiTheme::color(Token::Surface).name(), QStringLiteral("#2b2f33"));
    QCOMPARE(UiTheme::color(Token::SurfaceSunken).name(), QStringLiteral("#1f2326"));
    QCOMPARE(UiTheme::color(Token::SurfaceRaised).name(), QStringLiteral("#3f4348"));
    QCOMPARE(UiTheme::color(Token::Divider).name(), QStringLiteral("#383c40"));
    QCOMPARE(UiTheme::color(Token::Text).name(), QStringLiteral("#ffffff"));
    QCOMPARE(UiTheme::color(Token::TextMuted).name(), QStringLiteral("#747a80"));
    QCOMPARE(UiTheme::color(Token::Accent).name(), QStringLiteral("#7047eb"));
    QCOMPARE(UiTheme::color(Token::Scrim).alpha(), 61);
}

void UiThemeTest::styleSheetsUseTheTokens() {
    QCOMPARE(UiTheme::qss(QStringLiteral("a{background:@surfaceSunken;border:1px solid @divider;color:@textMuted}")),
             QStringLiteral("a{background:#1f2326;border:1px solid #383c40;color:#747a80}"));
    QCOMPARE(UiTheme::qss(QStringLiteral("b{background:@surface;color:@text}")), QStringLiteral("b{background:#2b2f33;color:#ffffff}"));
    QCOMPARE(UiTheme::qss(QStringLiteral("c{background:@scrim}")), QStringLiteral("c{background:rgba(15,23,42,61)}"));
}

// AGENTS.md "Interfaccia scura": i vecchi scuri non devono restare scritti nel codice.
void UiThemeTest::noOldDarkColorsLeftInTheSources() {
    const QDir sources(QStringLiteral(MENTIS_SOURCE_DIR "/src"));
    QVERIFY(sources.exists());
    const QRegularExpression old(QStringLiteral("#(252a31|3a414b|3a424d|2f353e|171a1f|e5e9ef|f4f6f8)\\b"),
                                 QRegularExpression::CaseInsensitiveOption);
    QStringList found;
    for (const QString &name : sources.entryList({QStringLiteral("*.cpp"), QStringLiteral("*.h")}, QDir::Files)) {
        QFile file(sources.filePath(name));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size(); ++i)
            if (old.match(lines[i]).hasMatch() && !lines[i].trimmed().startsWith(QLatin1String("//")))
                found.append(QStringLiteral("%1:%2").arg(name).arg(i + 1));
    }
    QVERIFY2(found.isEmpty(), qPrintable(found.join(QStringLiteral(", "))));
}

void UiThemeTest::sizesScaleFromFourteen() {
    const QString sheet = QStringLiteral("a{font-size:12px} b{font-size: 14px;padding:12px}");
    QCOMPARE(UiTheme::fontPx(12), 12);
    QCOMPARE(UiTheme::scaleStyleSheet(sheet), sheet);  // 14 = today's look
    UiTheme::setAppearance({QString(), 18, UiTheme::Density::Normal});
    QCOMPARE(UiTheme::fontPx(12), 15);
    QCOMPARE(UiTheme::fontPx(14), 18);
    QCOMPARE(UiTheme::scaleStyleSheet(sheet), QStringLiteral("a{font-size:15px/*12*/} b{font-size: 18px/*14*/;padding:12px/*12*/}"));
    QCOMPARE(UiTheme::heightPx(30), 40);  // 38.6 up to the grid
    QCOMPARE(UiTheme::scaleStyleSheet(QStringLiteral("q{min-height:30px}")), QStringLiteral("q{min-height:40px/*30*/}"));
    UiTheme::setAppearance({QString(), 12, UiTheme::Density::Normal});
    QCOMPARE(UiTheme::fontPx(14), 12);
    QCOMPARE(UiTheme::heightPx(30), 30);  // smaller text keeps today's heights
}

void UiThemeTest::openAndNewStyleSheetsFollowTheSize() {
    QLabel open;
    open.setStyleSheet(QStringLiteral("QLabel{font-size:12px}"));
    UiTheme::setAppearance({QString(), 18, UiTheme::Density::Normal});
    QCOMPARE(open.styleSheet(), QStringLiteral("QLabel{font-size:15px/*12*/}"));
    UiTheme::setAppearance({QString(), 12, UiTheme::Density::Normal});
    QCOMPARE(open.styleSheet(), QStringLiteral("QLabel{font-size:10px/*12*/}"));  // from the base, not from 15
    QLabel created;
    created.setStyleSheet(QStringLiteral("QLabel{font-size:14px}"));
    QCOMPARE(created.styleSheet(), QStringLiteral("QLabel{font-size:12px/*14*/}"));
    QLabel copy;  // a sheet copied from a scaled one is not scaled twice
    copy.setStyleSheet(open.styleSheet() + QStringLiteral(" QLabel{color:red}"));
    QCOMPARE(copy.styleSheet(), QStringLiteral("QLabel{font-size:10px/*12*/} QLabel{color:red}"));
    UiTheme::setAppearance({});
    QCOMPARE(open.styleSheet(), QStringLiteral("QLabel{font-size:12px}"));
    QCOMPARE(created.styleSheet(), QStringLiteral("QLabel{font-size:14px}"));
}

void UiThemeTest::applicationFontFollowsSizeAndFamily() {
    QWidget window;
    QLabel label(&window);
    QMenu menu;
    QTreeView tree;
    MindMapView map;
    map.setStyleSheet(QStringLiteral("QGraphicsView { background:#ffffff; }"));  // as in the app
    GanttChart chart;
    MindNode node;
    const QFont document = QApplication::font();
    const QFont title = NodeItem::titleFont(&node);
    const QList<QWidget *> widgets{&window, &label, &menu, &tree};
    UiTheme::setAppearance({QStringLiteral("Roboto"), 18, UiTheme::Density::Normal});
    for (QWidget *widget : widgets) {
        QCOMPARE(widget->font().family(), QStringLiteral("Roboto"));
        QVERIFY(qAbs(widget->font().pointSizeF() - m_systemPointSize * 18 / 14) < 0.01);
    }
    // The document keeps today's font: QFont() (every painter on an image), the map, the Gantt bars.
    QCOMPARE(QApplication::font(), document);
    QCOMPARE(QFont().family(), UiTheme::kDefaultFontFamily);
    QCOMPARE(map.viewport()->font(), document);
    QCOMPARE(chart.viewport()->font(), document);
    QCOMPARE(NodeItem::titleFont(&node), title);
    QCOMPARE(UiTheme::interfaceFont().family(), QStringLiteral("Roboto"));
    UiTheme::setAppearance({UiTheme::kSystemFont, 14, UiTheme::Density::Normal});
    QCOMPARE(label.font().family(), QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
    UiTheme::setAppearance({});
    for (QWidget *widget : widgets) {
        QCOMPARE(widget->font().family(), UiTheme::kDefaultFontFamily);
        QCOMPARE(widget->font().pointSizeF(), m_systemPointSize);
    }
}

void UiThemeTest::appearanceIsSavedAndInvalidValuesFallBack() {
    UiTheme::setAppearance({QStringLiteral("Arvo"), 16, UiTheme::Density::Compact});
    QSettings settings;
    QCOMPARE(settings.value(QStringLiteral("ui/fontFamily")).toString(), QStringLiteral("Arvo"));
    QCOMPARE(settings.value(QStringLiteral("ui/fontSize")).toInt(), 16);
    QCOMPARE(settings.value(QStringLiteral("ui/density")).toString(), QStringLiteral("compact"));
    UiTheme::loadAppearance();
    QCOMPARE(UiTheme::appearance(), (UiTheme::Appearance{QStringLiteral("Arvo"), 16, UiTheme::Density::Compact}));
    settings.setValue(QStringLiteral("ui/fontFamily"), QStringLiteral("No Such Font"));
    settings.setValue(QStringLiteral("ui/fontSize"), 30);
    settings.setValue(QStringLiteral("ui/density"), QStringLiteral("roomy"));
    settings.sync();
    UiTheme::loadAppearance();
    QCOMPARE(UiTheme::appearance(), UiTheme::Appearance{});
}

void UiThemeTest::bundledFamiliesAreRegistered() {
    NodeItem::loadBundledFonts();
    const QStringList families = QFontDatabase::families();
    for (const QString &family : UiTheme::bundledFontFamilies()) QVERIFY2(families.contains(family), qPrintable(family));
    QVERIFY(families.contains(UiTheme::kDefaultFontFamily));
}

void UiThemeTest::compactDensityGoesOneStepDown() {
    QWidget open;
    auto *line = new QHBoxLayout(&open);
    line->setSpacing(8);
    line->setContentsMargins(16, 12, 16, 2);
    auto *row = new QWidget(&open);
    row->setMinimumHeight(48);
    auto *icon = new QWidget(&open);
    icon->setFixedSize(32, 32);
    line->addWidget(row);
    line->addWidget(icon);
    open.setStyleSheet(QStringLiteral("QPushButton{padding:0 12px;min-height:30px;border:1px solid red}"));
    QCOMPARE(UiTheme::space(48), 48);
    UiTheme::setAppearance({QString(), 14, UiTheme::Density::Compact});
    QCOMPARE(UiTheme::space(48), 40);
    QCOMPARE(UiTheme::space(4), 4);
    QCOMPARE(UiTheme::space(24), 24);
    QCOMPARE(line->spacing(), 4);
    QCOMPARE(line->contentsMargins(), QMargins(12, 8, 12, 2));
    QCOMPARE(row->minimumHeight(), 40);
    QCOMPARE(icon->size(), QSize(32, 32));  // fixed sizes and icons stay
    QCOMPARE(open.styleSheet(), QStringLiteral("QPushButton{padding:0 8px/*12*/;min-height:26px/*30*/;border:1px solid red}"));
    QWidget created;  // a widget made while Compact is chosen gets it before it is shown
    auto *createdLine = new QHBoxLayout(&created);
    createdLine->setSpacing(12);
    created.ensurePolished();
    QCOMPARE(createdLine->spacing(), 8);
    UiTheme::setAppearance({});
    QCOMPARE(line->spacing(), 8);
    QCOMPARE(line->contentsMargins(), QMargins(16, 12, 16, 2));
    QCOMPARE(row->minimumHeight(), 48);
    QCOMPARE(createdLine->spacing(), 12);
    QCOMPARE(open.styleSheet(), QStringLiteral("QPushButton{padding:0 12px;min-height:30px;border:1px solid red}"));
}

// Review 2026-11-01 (P1): a style sheet's min-height becomes the widget's minimum height; Compact
// chosen with the widget open must not shrink it a second time.
void UiThemeTest::styleSheetHeightsShrinkOnceWhenChangedLive() {
    const QString sheet = QStringLiteral("QLineEdit{min-height:30px;border:1px solid red}");
    QLineEdit open;
    open.setStyleSheet(sheet);
    open.show();
    QVERIFY(QTest::qWaitForWindowExposed(&open));
    UiTheme::setAppearance({QString(), 14, UiTheme::Density::Compact});
    QApplication::processEvents();
    QLineEdit created;
    created.setStyleSheet(sheet);
    created.show();
    QVERIFY(QTest::qWaitForWindowExposed(&created));
    QCOMPARE(open.minimumHeight(), created.minimumHeight());
    UiTheme::setAppearance({});
    QApplication::processEvents();
    QLineEdit normal;
    normal.setStyleSheet(sheet);
    normal.show();
    QVERIFY(QTest::qWaitForWindowExposed(&normal));
    QCOMPARE(open.minimumHeight(), normal.minimumHeight());
}

QTEST_MAIN(UiThemeTest)
#include "uitheme_test.moc"
