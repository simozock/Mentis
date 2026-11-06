#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QTextBrowser>
#include <QTextTable>
#include <QTimer>
#include <QUrl>
#include <QWidget>

class QLineEdit;
class QMovie;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

// FEAT-092: the user manual embedded in the app (:/docs/manual, the files of manual/).
namespace ManualText {
// What Qt's Markdown reader does not know, turned into what it does: GitHub's callouts
// ("> [!TIP]") into a bold first word, <kbd> keys into code spans, HTML comments and <br> removed.
QString prepare(const QString &markdown);
// The resource URL of a page of the manual ("files/export-pdf.md" -> qrc:/docs/manual/...).
QUrl pageUrl(const QString &page);
}  // namespace ManualText

// The manual's colours, light or dark with the sheet's background like the outliner and the Gantt
// (docs/design/ui-components.md, "Manual window").
struct ManualColors {
    bool dark = true;
    QColor page;     // the page and the outside of the rounded tables
    QColor side;     // contents and search
    QColor band;     // callouts and the tables' heading row
    QColor cap;      // keys and code
    QColor divider;  // lines, outlines of pictures and tables
    QColor hover;    // a row of the contents under the mouse, a button under the mouse
    QColor open;     // the open page in the contents
    QColor field;    // the search field
    QColor text;
    QColor muted;
    QColor accent;
    QColor handle;  // scroll bars
    QColor handleHover;
    static ManualColors of(bool dark);
};

// The page: Markdown from the resources in a centred column, callouts as bands, keys as caps,
// pictures and tables centred with rounded corners, pictures drawn smooth at the size shown (as wide
// as the column at most), GIFs playing.
class ManualBrowser final : public QTextBrowser {
    Q_OBJECT
public:
    explicit ManualBrowser(QWidget *parent = nullptr);
    // Images of the page whose file is missing (tests: every picture of the manual is found).
    QStringList missingImages() const { return m_missing; }
    // Colours of the page; the open page is drawn again with them.
    void setColors(const ManualColors &colors);
    const ManualColors &colors() const { return m_colors; }

signals:
    void pageShown();  // after any page is loaded (contents, links, Back, Forward)

protected:
    QVariant loadResource(int type, const QUrl &name) override;
    void doSetSource(const QUrl &name, QTextDocument::ResourceType type) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    int textWidth() const;  // the width of the reading column's text
    // A picture at the size it is shown (smooth, at the screen's pixel ratio), with corners of 8 px
    // and a thin outline, so that screenshots with dark edges stay apart from a dark page.
    QImage shown(const QImage &original) const;
    void adapt();          // titles, links, callouts, keys and tables, once per page
    void fitColumn();      // the centred reading column, after loading and resizing
    void fitPictures();    // the pictures' sizes in the text, after loading and resizing
    void redrawPictures();  // the still pictures again from their originals, once a resize ends
    ManualColors m_colors = ManualColors::of(true);
    QHash<QUrl, QMovie *> m_movies;
    QHash<QUrl, QImage> m_originals;  // the page's still pictures as read
    QStringList m_missing;
    QList<QPointer<QTextTable>> m_tables;  // the page's tables, outlined by paintEvent
    QTimer m_resized;                      // the window stopped changing size
};

// The manual's window: contents and search on the left, the page on the right, Back and Forward.
// Not modal: it stays open beside the work.
class ManualWindow final : public QWidget {
    Q_OBJECT
public:
    explicit ManualWindow(QWidget *parent = nullptr);
    void openPage(const QString &page);
    // Light or dark as the sheet's background (MainWindow::applyBackdrop).
    void setDark(bool dark);
    ManualBrowser *browser() const { return m_browser; }
    QTreeWidget *contents() const { return m_contents; }
    // Every page of the contents, as paths inside manual/ (tests).
    QStringList pages() const;

private:
    void buildContents();
    void applyColors();
    void filter(const QString &text);
    void selectCurrent();

    ManualBrowser *m_browser = nullptr;
    QTreeWidget *m_contents = nullptr;
    QLineEdit *m_search = nullptr;
    QWidget *m_side = nullptr;
    QWidget *m_page = nullptr;
    QWidget *m_splitter = nullptr;
    QToolButton *m_back = nullptr;
    QToolButton *m_forward = nullptr;
    QToolButton *m_home = nullptr;
    QHash<QString, QString> m_text;  // page -> its text, lower case, for the search
};
