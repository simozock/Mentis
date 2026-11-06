// FEAT-092: the manual inside the app: the conversion of what Qt's Markdown does not know, every page
// opening with all its pictures, every page the code names existing, and the page of each place.
#include "help.h"
#include "mainwindow.h"
#include "manualwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "modaldialog.h"
#include "nodeitem.h"
#include "uitheme.h"

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QStandardPaths>
#include <QTextBlock>
#include <QTextTable>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

class HelpTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
        QCoreApplication::setApplicationName(QStringLiteral("MentisHelpTest"));
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));  // as main.cpp
        // The interface font, as main.cpp sets it.
        NodeItem::loadInterfaceFont();
        QFont font = QApplication::font();
        font.setFamily(UiTheme::kDefaultFontFamily);
        QApplication::setFont(font);
    }

    void prepareTurnsWhatQtDoesNotKnow() {
        const QString text = ManualText::prepare(
            "<!-- template\nnote -->Press <kbd>Ctrl</kbd>+<kbd>S</kbd>.\n\n> [!TIP]\n> Faster.\n\n> [!WARNING]\n> Careful.\n"
            "| a<br>b |\n");
        QVERIFY(!text.contains("<!--"));
        QVERIFY(text.contains("Press `Ctrl`+`S`."));
        QVERIFY(text.contains("> **Tip**"));
        QVERIFY(text.contains("> **Warning**"));
        QVERIFY(!text.contains("[!"));
        QVERIFY(text.contains("a — b"));
    }

    // Every page of the contents opens, with every picture found.
    void everyPageOpensWithItsPictures() {
        ManualWindow window;
        window.resize(1000, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        // Readable: the contents in the interface's text colour and font (the user's photo, 2026-11-03:
        // white on light grey, not Atkinson).
        QCOMPARE(window.contents()->palette().color(QPalette::Text), UiTheme::color(UiTheme::Token::Text));
        QCOMPARE(QFontInfo(window.contents()->font()).family(), UiTheme::kDefaultFontFamily);
        QCOMPARE(QFontInfo(window.browser()->document()->defaultFont()).family(), UiTheme::kDefaultFontFamily);
        const QStringList pages = window.pages();
        QVERIFY(pages.size() > 60);
        // No invalid font sizes on any page (Qt warned "Point size <= 0" while drawing).
        static int invalidSizes = 0;
        invalidSizes = 0;
        const QtMessageHandler previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
            if (message.contains(QLatin1String("Point size"))) ++invalidSizes;
        });
        for (const QString &page : pages) {
            window.openPage(page);
            QApplication::processEvents();
            QVERIFY2(window.browser()->source() == ManualText::pageUrl(page), qPrintable(page));
            QVERIFY2(!window.browser()->document()->toPlainText().trimmed().isEmpty(), qPrintable(page));
            QVERIFY2(window.browser()->missingImages().isEmpty(),
                     qPrintable(page + ": " + window.browser()->missingImages().join(", ")));
            window.browser()->viewport()->grab();  // drawn, so every font is resolved
        }
        qInstallMessageHandler(previous);
        QCOMPARE(invalidSizes, 0);
        // Tables are centred.
        window.openPage(QStringLiteral("map/map-types.md"));
        QApplication::processEvents();
        bool centredTable = false;
        for (QTextFrame *frame : window.browser()->document()->rootFrame()->childFrames())
            if (auto *table = qobject_cast<QTextTable *>(frame)) centredTable = table->format().alignment() == Qt::AlignHCenter;
        QVERIFY(centredTable);
        // Pictures are made at the size shown, not narrowed by Qt without smoothing.
        window.openPage(QStringLiteral("map/search.md"));
        QApplication::processEvents();
        bool fitted = false;
        for (QTextBlock block = window.browser()->document()->begin(); block.isValid(); block = block.next())
            for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment)
                if (fragment.fragment().charFormat().isImageFormat()) {
                    const QTextImageFormat format = fragment.fragment().charFormat().toImageFormat();
                    const QImage image = window.browser()->document()->resource(QTextDocument::ImageResource, QUrl(format.name())).value<QImage>();
                    fitted = qAbs(image.deviceIndependentSize().width() - format.width()) < 1;                }
        QVERIFY(fitted);
        // Light on a light sheet, like the outliner and the Gantt.
        window.setDark(false);
        QApplication::processEvents();
        QCOMPARE(window.contents()->palette().color(QPalette::Text), QColor(0x1f, 0x29, 0x37));
        QCOMPARE(window.browser()->palette().color(QPalette::Base), QColor(Qt::white));
        window.setDark(true);
        // Callouts are tinted and keys are code.
        window.openPage(QStringLiteral("getting-started/install.md"));
        QApplication::processEvents();
        bool tinted = false;
        for (QTextBlock block = window.browser()->document()->begin(); block.isValid(); block = block.next())
            if (block.blockFormat().intProperty(QTextFormat::BlockQuoteLevel) > 0 && block.blockFormat().background().style() != Qt::NoBrush)
                tinted = true;
        QVERIFY(tinted);
        // A page that does not exist shows the contents.
        window.openPage(QStringLiteral("no/such-page.md"));
        QCOMPARE(window.browser()->source(), ManualText::pageUrl(QStringLiteral("README.md")));
    }

    // Every page the code opens (dialogs and places) is in the manual.
    void everyPageTheCodeNamesExists() {
        QStringList named;
        static const QRegularExpression literal(QStringLiteral("QStringLiteral\\(\"([a-z-]+/[a-z-]+\\.md)\"\\)"));
        QDirIterator files(QStringLiteral(MENTIS_SOURCE_DIR "/src"), {QStringLiteral("*.cpp")}, QDir::Files);
        while (files.hasNext()) {
            QFile file(files.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString code = QString::fromUtf8(file.readAll());
            for (auto match = literal.globalMatch(code); match.hasNext();) named.append(match.next().captured(1));
        }
        named.removeDuplicates();
        QVERIFY(named.size() > 15);
        for (const QString &page : std::as_const(named))
            QVERIFY2(QFile::exists(QStringLiteral(":/docs/manual/") + page), qPrintable(page));
    }

    // BUG-036: the "?" of a modal window was Fusion's white tool button; it is flat on the surface
    // like the close button.
    void helpButtonIsFlatOnTheSurface() {
        ModalDialog dialog(nullptr, QStringLiteral("Export image"), QStringLiteral("Export"), 480);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *help = dialog.findChild<QToolButton *>(QStringLiteral("ModalHelp"));
        QVERIFY(help);
        help->show();
        QApplication::processEvents();
        const QImage image = dialog.grab().toImage();
        const qreal ratio = image.devicePixelRatio();
        const QPoint inside = help->mapTo(&dialog, QPoint(3, help->height() / 2));
        const QPoint beside = help->mapTo(&dialog, QPoint(-6, help->height() / 2));
        QCOMPARE(QColor(image.pixel((QPointF(inside) * ratio).toPoint())).name(),
                 QColor(image.pixel((QPointF(beside) * ratio).toPoint())).name());
    }

    // The page of the place the user is in, and F1 opening it.
    void pageOfThePlace() {
        MainWindow window;
        window.resize(1280, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(window.helpPage(), QStringLiteral("getting-started/home.md"));
        qobject_cast<QStackedWidget *>(window.centralWidget())->setCurrentIndex(1);  // the editor
        QApplication::processEvents();
        QCOMPARE(window.helpPage(), QStringLiteral("map/topics.md"));
        auto *button = window.findChild<QToolButton *>(QStringLiteral("HelpButton"));
        QVERIFY(button);
        button->click();
        QVERIFY(window.manualWindow() && window.manualWindow()->isVisible());
        QCOMPARE(window.manualWindow()->browser()->source(), ManualText::pageUrl(QStringLiteral("map/topics.md")));
        // The contents show where the page is.
        QVERIFY(window.manualWindow()->contents()->currentItem());
        QCOMPARE(window.manualWindow()->contents()->currentItem()->text(0), QStringLiteral("Topics"));
        window.manualWindow()->close();
    }
};

QTEST_MAIN(HelpTest)
#include "help_test.moc"
