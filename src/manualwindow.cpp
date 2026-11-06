#include "manualwindow.h"

#include "nodeitem.h"
#include "uitheme.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace {

const QString kRoot = QStringLiteral("/docs/manual/");

QString readPage(const QString &page) {
    QFile file(QLatin1Char(':') + kRoot + page);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}

// The page's path inside manual/ from its resource URL.
QString pageOf(const QUrl &url) {
    const QString path = url.path();
    return path.startsWith(kRoot) ? path.mid(kRoot.size()) : QString();
}

// The links of a numbered list: "1. **[Title](page.md)** — ..." in order.
QList<std::pair<QString, QString>> listedLinks(const QString &markdown) {
    static const QRegularExpression item(QStringLiteral("^\\d+\\. \\*\\*\\[([^\\]]+)\\]\\(([^)]+\\.md)\\)\\*\\*"),
                                         QRegularExpression::MultilineOption);
    QList<std::pair<QString, QString>> links;
    for (auto match = item.globalMatch(markdown); match.hasNext();) {
        const QRegularExpressionMatch m = match.next();
        links.append({m.captured(1), m.captured(2)});
    }
    return links;
}

// The reading column: longer lines are hard to follow.
constexpr int kColumnPx = 720;
constexpr int kPageMarginPx = 32;
constexpr int kPictureRadiusPx = 8;  // pictures and tables, the radius of containers (ui-foundations)
constexpr int kCellPaddingX = 16;
constexpr int kCellPaddingY = 12;
constexpr int kRedrawDelayMs = 150;  // pictures are scaled again once the window stops changing size

// A colour in a style sheet, with its alpha.
QString css(const QColor &color) { return color.name(QColor::HexArgb); }

// Hugeicons are dark in the files (#27303b): tinted with the window's icon colour.
QIcon tinted(const QString &path, const QColor &color) {
    QPixmap pixmap = QIcon(path).pixmap(QSize(16, 16), qApp->devicePixelRatio());
    QPainter tint(&pixmap);
    tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tint.fillRect(pixmap.rect(), color);
    tint.end();
    return QIcon(pixmap);
}

// The contents: the hover and open-page band runs across the whole row, indentation and arrow
// included (the style's own selection left a notch in front of the indented rows).
class ManualContents final : public QTreeWidget {
public:
    ManualColors colors = ManualColors::of(true);

protected:
    void drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const bool open = selectionModel()->isSelected(index);
        const bool hovered = viewport()->underMouse() && option.rect.contains(viewport()->mapFromGlobal(QCursor::pos()));
        if (open || hovered) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(open ? colors.open : colors.hover);
            painter->drawRoundedRect(QRectF(option.rect).adjusted(0, 1, 0, -1), 6, 6);
            painter->restore();
        }
        QTreeWidget::drawRow(painter, option, index);
    }
    // Qt repaints only the item's cell on hover and selection: the band needs the whole row (a small list).
    void mouseMoveEvent(QMouseEvent *event) override {
        QTreeWidget::mouseMoveEvent(event);
        viewport()->update();
    }
    void leaveEvent(QEvent *event) override {
        QTreeWidget::leaveEvent(event);
        viewport()->update();
    }
    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override {
        QTreeWidget::selectionChanged(selected, deselected);
        viewport()->update();
    }
};

// The interface's font (Atkinson Hyperlegible unless chosen in Settings) at a size of the scale.
QFont readingFont(int px, bool bold = false) {
    QFont font = UiTheme::interfaceFont();
    font.setPixelSize(UiTheme::fontPx(px));
    font.setBold(bold);
    return font;
}

}  // namespace

namespace ManualText {

QString prepare(const QString &markdown) {
    QString text = markdown;
    static const QRegularExpression comment(QStringLiteral("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression key(QStringLiteral("<kbd>([^<]*)</kbd>"));
    static const QRegularExpression callout(QStringLiteral("^(\\s*>\\s*)\\[!(TIP|NOTE|WARNING)\\]\\s*$"),
                                            QRegularExpression::MultilineOption);
    static const QRegularExpression lineBreak(QStringLiteral("<br\\s*/?>"));
    text.remove(comment);
    text.replace(key, QStringLiteral("`\\1`"));
    text.replace(lineBreak, QStringLiteral(" — "));
    QString result;
    qsizetype last = 0;
    for (auto match = callout.globalMatch(text); match.hasNext();) {
        const QRegularExpressionMatch m = match.next();
        const QString kind = m.captured(2);
        result += text.mid(last, m.capturedStart() - last) + m.captured(1) + QStringLiteral("**") + kind.left(1) +
                  kind.mid(1).toLower() + QStringLiteral("**");
        last = m.capturedEnd();
    }
    return result + text.mid(last);
}

QUrl pageUrl(const QString &page) { return QUrl(QStringLiteral("qrc:") + kRoot + page); }

}  // namespace ManualText

ManualColors ManualColors::of(bool dark) {
    using UiTheme::Token;
    ManualColors c;
    c.dark = dark;
    if (dark) {
        // The interface's dark tokens (ui-foundations "Interfaccia scura").
        c.page = UiTheme::color(Token::SurfaceSunken);
        c.side = UiTheme::color(Token::Surface);
        c.band = UiTheme::color(Token::Surface);
        c.cap = UiTheme::color(Token::SurfaceRaised);
        c.divider = UiTheme::color(Token::Divider);
        c.hover = UiTheme::color(Token::Divider);
        c.open = UiTheme::color(Token::SurfaceRaised);
        c.field = UiTheme::color(Token::SurfaceSunken);
        c.text = UiTheme::color(Token::Text);
        c.muted = UiTheme::color(Token::TextMuted);
        c.handle = UiTheme::color(Token::SurfaceRaised);
        c.handleHover = UiTheme::color(Token::TextMuted);
    } else {
        // The light colours of ui-foundations: page and cards of the light Gantt, hover and pressed on
        // light, the text, the scroll bars.
        c.page = QColor(0xff, 0xff, 0xff);
        c.side = QColor(0xec, 0xee, 0xf1);
        c.band = QColor(0xec, 0xee, 0xf1);
        c.cap = QColor(0xec, 0xee, 0xf1);
        c.divider = QColor(15, 23, 42, 28);
        c.hover = QColor(15, 23, 42, 15);
        c.open = QColor(15, 23, 42, 28);
        c.field = QColor(0xff, 0xff, 0xff);
        c.text = QColor(0x1f, 0x29, 0x37);
        c.muted = QColor(0x6b, 0x72, 0x80);
        c.handle = QColor(0xc4, 0xca, 0xd2);
        c.handleHover = QColor(0x9a, 0xa2, 0xac);
    }
    c.accent = UiTheme::color(Token::Accent);
    return c;
}

ManualBrowser::ManualBrowser(QWidget *parent) : QTextBrowser(parent) {
    setOpenExternalLinks(true);  // web links in the browser; links between pages stay here
    setFrameShape(QFrame::NoFrame);
    // A scroll bar that comes and goes would change the column's width just after the pictures are made.
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    NodeItem::loadBundledFonts();  // Atkinson Hyperlegible Mono for keys and code
    // The reading size is larger than the interface's text, in the interface's font.
    document()->setDefaultFont(readingFont(15));
    document()->setDocumentMargin(kPageMarginPx);
    m_resized.setSingleShot(true);
    m_resized.setInterval(kRedrawDelayMs);
    connect(&m_resized, &QTimer::timeout, this, &ManualBrowser::redrawPictures);
    setColors(m_colors);
}

void ManualBrowser::setColors(const ManualColors &colors) {
    m_colors = colors;
    setStyleSheet(QStringLiteral("QTextBrowser { background:%1; color:%2; border:0; selection-background-color:%3; }")
                      .arg(css(colors.page), css(colors.text), css(colors.accent)));
    if (!source().isEmpty()) reload();  // titles, links, callouts and pictures in the new colours
}

QVariant ManualBrowser::loadResource(int type, const QUrl &name) {
    const QString path = QLatin1Char(':') + name.path();
    if (type == QTextDocument::MarkdownResource) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return ManualText::prepare(QString::fromUtf8(file.readAll()));
    }
    if (type == QTextDocument::ImageResource) {
        if (!QFile::exists(path)) {
            m_missing.append(name.toString());
            return {};
        }
        if (path.endsWith(QLatin1String(".gif"), Qt::CaseInsensitive)) {
            // The animation goes on by replacing the picture in the document at every frame.
            QMovie *&movie = m_movies[name];
            if (!movie) {
                movie = new QMovie(path, QByteArray(), this);
                connect(movie, &QMovie::frameChanged, this, [this, name, movie] {
                    document()->addResource(QTextDocument::ImageResource, name, shown(movie->currentImage()));
                    viewport()->update();
                });
                movie->start();
            }
            return shown(movie->currentImage());
        }
        const QImage original(path);
        m_originals.insert(name, original);
        return shown(original);
    }
    return QTextBrowser::loadResource(type, name);
}

int ManualBrowser::textWidth() const {
    // fitColumn's margins: at least kPageMarginPx a side, the text at most kColumnPx wide.
    return std::min(kColumnPx, viewport()->width() - 2 * kPageMarginPx);
}

QImage ManualBrowser::shown(const QImage &original) const {
    if (original.isNull()) return original;
    // Qt draws a picture narrowed in the text without smoothing: the screenshots' small text came out
    // jagged. The picture is narrowed here, smooth, at the pixels the screen really has.
    const qreal ratio = devicePixelRatioF();
    const qreal width = std::clamp<qreal>(textWidth() - 8, 1, original.width());
    const QSize size(qRound(width * ratio), qRound(width * original.height() / original.width() * ratio));
    const QImage scaled = original.size() == size ? original : original.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(ratio, ratio);
    const QRectF box(QPointF(0, 0), QSizeF(size) / ratio);
    QBrush picture(scaled);
    picture.setTransform(QTransform::fromScale(1 / ratio, 1 / ratio));
    painter.setBrush(picture);
    // A thin outline: screenshots with dark edges (the window around a map) stay apart from a dark page.
    painter.setPen(QPen(m_colors.divider, 1));
    painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), kPictureRadiusPx, kPictureRadiusPx);
    painter.end();
    result.setDevicePixelRatio(ratio);
    return result;
}

void ManualBrowser::redrawPictures() {
    for (auto it = m_originals.cbegin(); it != m_originals.cend(); ++it)
        document()->addResource(QTextDocument::ImageResource, it.key(), shown(it.value()));
    fitPictures();
    viewport()->update();
}

void ManualBrowser::doSetSource(const QUrl &name, QTextDocument::ResourceType type) {
    // A new page: the animations of the old one stop.
    qDeleteAll(m_movies);
    m_movies.clear();
    m_originals.clear();
    m_missing.clear();
    // Qt 6.8 lays the old page out again under the new address before replacing it: its pictures
    // were asked for at wrong paths (../images from the home page) and counted as missing. Not for
    // an anchor on the same page, which Qt only scrolls to.
    if (name.adjusted(QUrl::RemoveFragment) != source().adjusted(QUrl::RemoveFragment)) document()->clear();
    QTextBrowser::doSetSource(name, type);
    adapt();
    fitColumn();
    fitPictures();
    emit pageShown();
}

void ManualBrowser::resizeEvent(QResizeEvent *event) {
    QTextBrowser::resizeEvent(event);
    fitColumn();
    fitPictures();
    m_resized.start();
}

void ManualBrowser::fitColumn() {
    // The text stays in a centred column: the sides of a wide window take the rest (the frame's left and
    // right margins replace the document's margin there).
    const int side = std::max(kPageMarginPx, (viewport()->width() - kColumnPx) / 2);
    QTextFrameFormat format = document()->rootFrame()->frameFormat();
    if (qFuzzyCompare(format.leftMargin() + 1, side + 1)) return;
    format.setLeftMargin(side);
    format.setRightMargin(side);
    document()->rootFrame()->setFrameFormat(format);
}

void ManualBrowser::adapt() {
    const QColor text = m_colors.text;
    const QColor muted = m_colors.muted;
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    // A size in pixels replaces Qt's: its point size and the step it gives titles, which on a font
    // sized in pixels make an invalid point size.
    const auto sized = [](QTextCharFormat &format, int px) {
        format.clearProperty(QTextFormat::FontPointSize);
        format.clearProperty(QTextFormat::FontSizeAdjustment);
        format.setProperty(QTextFormat::FontPixelSize, UiTheme::fontPx(px));
    };
    // Every piece of a block's text through change: the pieces are read first and rewritten after,
    // since rewriting one can merge those being walked. A piece left unchanged is not rewritten.
    const auto rewrite = [&cursor](const QTextBlock &block, const std::function<void(QTextCharFormat &)> &change) {
        QList<std::pair<int, QTextCharFormat>> parts;
        for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment)
            parts.append({fragment.fragment().position(), fragment.fragment().charFormat()});
        for (int i = 0; i < parts.size(); ++i) {
            QTextCharFormat piece = parts[i].second;
            if (piece.isImageFormat()) continue;
            change(piece);
            if (piece == parts[i].second) continue;
            cursor.setPosition(parts[i].first);
            cursor.setPosition(i + 1 < parts.size() ? parts[i + 1].first : block.position() + block.length() - 1,
                               QTextCursor::KeepAnchor);
            cursor.setCharFormat(piece);
        }
    };
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        // The path above the title ("Manual › Pitch"): small and quiet.
        const bool path = block == document()->begin() && block.text().contains(QChar(0x203A));
        const int heading = format.headingLevel();
        // A picture on its own line: centred, without the air between lines under it.
        const bool picture = block.text().trimmed() == QString(QChar::ObjectReplacementCharacter);
        if (heading > 0) {
            format.setTopMargin(heading == 1 ? 4 : heading == 2 ? 28 : 20);
            format.setBottomMargin(heading == 1 ? 16 : 8);
            format.setLineHeight(115, QTextBlockFormat::ProportionalHeight);
        } else if (picture) {
            format.setAlignment(Qt::AlignHCenter);
            format.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
            format.setTopMargin(12);
            format.setBottomMargin(12);
        } else {
            format.setLineHeight(150, QTextBlockFormat::ProportionalHeight);  // air between lines
        }
        if (path) format.setBottomMargin(4);
        // Callouts: the quote's blocks on a raised band; the bold word that opens them names the kind.
        if (format.intProperty(QTextFormat::BlockQuoteLevel) > 0) {
            format.setBackground(m_colors.band);
            format.setTopMargin(0);
            format.setBottomMargin(0);
        }
        cursor.setPosition(block.position());
        cursor.setBlockFormat(format);
        rewrite(block, [&](QTextCharFormat &piece) {
            if (heading > 0) {
                sized(piece, heading == 1 ? 28 : heading == 2 ? 20 : 18);  // titles on the interface's scale
                piece.setFontWeight(QFont::Bold);
            } else if (path) {
                sized(piece, 12);
                piece.setForeground(muted);
            }
            if (piece.isAnchor()) {
                // Links in the text's colour, underlined in the accent: the accent alone is too dark to read.
                piece.setForeground(path ? muted : text);
                piece.setFontUnderline(!path);
                piece.setUnderlineColor(m_colors.accent);
            } else if (piece.fontFixedPitch()) {
                // Keys and code: a raised cap behind the monospaced text.
                piece.setFontFamilies({QStringLiteral("Atkinson Hyperlegible Mono")});
                sized(piece, 13);
                piece.setBackground(m_colors.cap);
            }
        });
    }
    // Tables: centred, rows parted by thin dividers, the heading row on a raised band in larger bold
    // text; paintEvent rounds the corners and draws the outline.
    m_tables.clear();
    for (QTextFrame *frame : document()->rootFrame()->childFrames()) {
        auto *table = qobject_cast<QTextTable *>(frame);
        if (!table) continue;
        m_tables.append(table);
        QTextTableFormat format = table->format();
        format.setAlignment(Qt::AlignHCenter);
        format.setBorderCollapse(true);
        format.setBorder(0);
        format.setCellPadding(0);
        format.setCellSpacing(0);
        format.setTopMargin(12);
        format.setBottomMargin(20);
        table->setFormat(format);
        for (int row = 0; row < table->rows(); ++row)
            for (int column = 0; column < table->columns(); ++column) {
                QTextTableCell cell = table->cellAt(row, column);
                QTextTableCellFormat look = cell.format().toTableCellFormat();
                look.setTopPadding(kCellPaddingY);
                look.setBottomPadding(kCellPaddingY);
                look.setLeftPadding(kCellPaddingX);
                look.setRightPadding(kCellPaddingX);
                look.setBottomBorder(row + 1 < table->rows() ? 1 : 0);
                look.setBottomBorderBrush(m_colors.divider);
                look.setBottomBorderStyle(QTextFrameFormat::BorderStyle_Solid);
                if (row == 0) look.setBackground(m_colors.band);
                cell.setFormat(look);
                if (row > 0) continue;
                for (QTextFrame::iterator it = cell.begin(); !it.atEnd(); ++it)
                    rewrite(it.currentBlock(), [&](QTextCharFormat &piece) {
                        sized(piece, 16);
                        piece.setFontWeight(QFont::Bold);
                    });
            }
    }
    cursor.endEditBlock();
}

void ManualBrowser::paintEvent(QPaintEvent *event) {
    QTextBrowser::paintEvent(event);
    if (m_tables.isEmpty()) return;
    // Tables: rounded corners (the page's colour outside them) and a thin outline.
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
    for (const QPointer<QTextTable> &table : std::as_const(m_tables)) {
        if (!table) continue;
        // The cells' text with their padding (the frame's own rectangle ignores the centring).
        QRectF box;
        for (int row = 0; row < table->rows(); ++row)
            for (int column = 0; column < table->columns(); ++column) {
                const QTextTableCell cell = table->cellAt(row, column);
                for (QTextFrame::iterator it = cell.begin(); !it.atEnd(); ++it)
                    box |= document()->documentLayout()->blockBoundingRect(it.currentBlock());
            }
        box = box.adjusted(-kCellPaddingX + 0.5, -kCellPaddingY + 0.5, kCellPaddingX - 0.5, kCellPaddingY - 0.5);
        QPainterPath outside;
        outside.addRect(box.adjusted(-1, -1, 1, 1));
        QPainterPath inside;
        inside.addRoundedRect(box, kPictureRadiusPx, kPictureRadiusPx);
        painter.fillPath(outside.subtracted(inside), m_colors.page);
        painter.setPen(QPen(m_colors.divider, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(inside);
    }
}

void ManualBrowser::fitPictures() {
    const int available = textWidth() - 8;
    if (available <= 0) return;
    QTextCursor cursor(document());
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment) {
            const QTextFragment part = fragment.fragment();
            if (!part.charFormat().isImageFormat()) continue;
            QTextImageFormat format = part.charFormat().toImageFormat();
            const QImage image = document()->resource(QTextDocument::ImageResource, QUrl(format.name())).value<QImage>();
            if (image.isNull()) continue;
            // The picture's own size (shown() made it at most as wide as the column), narrower while
            // the window shrinks until redrawPictures makes it again.
            const QSizeF own = image.deviceIndependentSize();
            const qreal width = std::min<qreal>(own.width(), available);
            if (qFuzzyCompare(format.width(), width)) continue;
            format.setWidth(width);
            format.setHeight(width * own.height() / own.width());
            cursor.setPosition(part.position());
            cursor.setPosition(part.position() + part.length(), QTextCursor::KeepAnchor);
            cursor.setCharFormat(format);
        }
    }
}

ManualWindow::ManualWindow(QWidget *parent) : QWidget(parent, Qt::Window) {
    setWindowTitle(tr("User manual"));
    setObjectName(QStringLiteral("ManualWindow"));
    resize(1100, 780);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Search the manual"));
    m_search->setClearButtonEnabled(true);
    m_search->setFont(readingFont(14));
    m_contents = new ManualContents;
    m_contents->setHeaderHidden(true);
    m_contents->setRootIsDecorated(true);  // areas fold and unfold with their arrow
    m_contents->setIndentation(20);
    m_contents->setFont(readingFont(14));
    m_contents->setMouseTracking(true);
    m_contents->setAccessibleName(tr("Contents"));
    m_side = new QWidget;
    m_side->setObjectName(QStringLiteral("ManualSide"));
    m_side->setAttribute(Qt::WA_StyledBackground);
    auto *sideLayout = new QVBoxLayout(m_side);
    sideLayout->setContentsMargins(12, 12, 8, 12);
    sideLayout->setSpacing(8);
    sideLayout->addWidget(m_search);
    sideLayout->addWidget(m_contents, 1);

    const auto tool = [](const QString &tip) {
        auto *button = new QToolButton;
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        button->setAutoRaise(true);
        return button;
    };
    m_back = tool(tr("Back"));
    m_forward = tool(tr("Forward"));
    m_home = tool(tr("Contents of the manual"));
    m_browser = new ManualBrowser;
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(8, 8, 8, 0);
    bar->addWidget(m_back);
    bar->addWidget(m_forward);
    bar->addWidget(m_home);
    bar->addStretch();
    m_page = new QWidget;
    m_page->setObjectName(QStringLiteral("ManualPage"));
    m_page->setAttribute(Qt::WA_StyledBackground);
    auto *pageLayout = new QVBoxLayout(m_page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);
    pageLayout->addLayout(bar);
    pageLayout->addWidget(m_browser, 1);

    auto *splitter = new QSplitter;
    splitter->addWidget(m_side);
    splitter->addWidget(m_page);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({280, 820});
    splitter->setHandleWidth(1);
    m_splitter = splitter;
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    connect(m_back, &QToolButton::clicked, m_browser, &QTextBrowser::backward);
    connect(m_forward, &QToolButton::clicked, m_browser, &QTextBrowser::forward);
    connect(m_browser, &QTextBrowser::backwardAvailable, m_back, &QWidget::setEnabled);
    connect(m_browser, &QTextBrowser::forwardAvailable, m_forward, &QWidget::setEnabled);
    connect(m_home, &QToolButton::clicked, this, [this] { openPage(QStringLiteral("README.md")); });
    connect(m_browser, &ManualBrowser::pageShown, this, &ManualWindow::selectCurrent);
    connect(m_contents, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        const QString page = item->data(0, Qt::UserRole).toString();
        if (!page.isEmpty()) openPage(page);
    });
    connect(m_search, &QLineEdit::textChanged, this, &ManualWindow::filter);
    m_back->setEnabled(false);
    m_forward->setEnabled(false);
    applyColors();
    buildContents();
}

void ManualWindow::setDark(bool dark) {
    if (dark == m_browser->colors().dark) return;
    m_browser->setColors(ManualColors::of(dark));
    applyColors();
}

void ManualWindow::applyColors() {
    // Every part sets its own colours: inherited from the main window they put white text on light grey.
    const ManualColors &c = m_browser->colors();
    const QColor icon = c.dark ? c.text : QColor(0x27, 0x30, 0x3b);  // ui-foundations "Icone"
    m_side->setStyleSheet(QStringLiteral("QWidget#ManualSide { background:%1; }").arg(css(c.side)));
    m_page->setStyleSheet(QStringLiteral("QWidget#ManualPage { background:%1; }").arg(css(c.page)));
    m_splitter->setStyleSheet(QStringLiteral("QSplitter::handle { background:%1; }").arg(css(c.divider)));
    m_search->setStyleSheet(QStringLiteral(
        "QLineEdit { background:%1; color:%2; border:1px solid %3; border-radius:6px; padding:6px 10px; }"
        "QLineEdit:focus { border-color:%4; }").arg(css(c.field), css(c.text), css(c.divider), css(c.accent)));
    static_cast<ManualContents *>(m_contents)->colors = c;
    // Backgrounds are ManualContents' band; the style paints none.
    const QString arrows = c.dark ? QStringLiteral("-light") : QString();
    m_contents->setStyleSheet(QStringLiteral(
        "QTreeWidget { border:0; background:transparent; color:%1; outline:0; selection-background-color:transparent;"
        " show-decoration-selected:0; }"
        "QTreeWidget::item { padding:6px 4px; color:%1; }"
        "QTreeWidget::item:hover, QTreeWidget::item:selected { background:transparent; color:%1; }"
        "QTreeWidget::branch, QTreeWidget::branch:hover, QTreeWidget::branch:selected { background:transparent; image:none; }"
        "QTreeWidget::branch:has-children:closed { image:url(:/assets/hugeicons/arrow-right%2.svg); padding:4px 0 4px 6px; }"
        "QTreeWidget::branch:has-children:open { image:url(:/assets/hugeicons/arrow-down%2.svg); padding:4px 0 4px 6px; }")
                                  .arg(css(c.text), arrows));
    const QString button = QStringLiteral(
        "QToolButton { border:0; border-radius:6px; padding:6px; background:transparent; }"
        "QToolButton:hover { background:%1; }").arg(css(c.hover));
    for (const auto &[tool, path] : {std::pair{m_back, QStringLiteral(":/assets/hugeicons/arrow-left.svg")},
                                     std::pair{m_forward, QStringLiteral(":/assets/hugeicons/arrow-right.svg")},
                                     std::pair{m_home, QStringLiteral(":/assets/hugeicons/home.svg")}}) {
        tool->setIcon(tinted(path, icon));
        tool->setStyleSheet(button);
    }
    // Thin scroll bars, without arrows.
    const QString scrollBar = QStringLiteral(
        "QScrollBar:vertical { background:transparent; width:10px; margin:4px 2px; }"
        "QScrollBar::handle:vertical { background:%1; min-height:48px; border-radius:3px; }"
        "QScrollBar::handle:vertical:hover { background:%2; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }")
                                  .arg(css(c.handle), css(c.handleHover));
    m_contents->verticalScrollBar()->setStyleSheet(scrollBar);
    m_browser->verticalScrollBar()->setStyleSheet(scrollBar);
    m_contents->viewport()->update();
}

void ManualWindow::buildContents() {
    const QString index = readPage(QStringLiteral("README.md"));
    auto *start = new QTreeWidgetItem(m_contents, {tr("Mentis user manual")});
    start->setData(0, Qt::UserRole, QStringLiteral("README.md"));
    m_text.insert(QStringLiteral("README.md"), index.toLower());
    for (const auto &[title, groupPage] : listedLinks(index)) {
        auto *group = new QTreeWidgetItem(m_contents, {title});
        group->setData(0, Qt::UserRole, groupPage);
        group->setFont(0, readingFont(14, true));
        const QString groupText = readPage(groupPage);
        m_text.insert(groupPage, groupText.toLower());
        const QString folder = groupPage.section(QLatin1Char('/'), 0, -2);
        for (const auto &[pageTitle, page] : listedLinks(groupText)) {
            const QString path = folder.isEmpty() ? page : folder + QLatin1Char('/') + page;
            auto *item = new QTreeWidgetItem(group, {pageTitle});
            item->setData(0, Qt::UserRole, path);
            m_text.insert(path, readPage(path).toLower());
        }
    }
}

QStringList ManualWindow::pages() const { return m_text.keys(); }

void ManualWindow::openPage(const QString &page) {
    // A page that does not exist (an old link) shows the contents instead of an empty window.
    const QString target = QFile::exists(QLatin1Char(':') + kRoot + page) ? page : QStringLiteral("README.md");
    m_browser->setSource(ManualText::pageUrl(target), QTextDocument::MarkdownResource);
}

void ManualWindow::selectCurrent() {
    const QString page = pageOf(m_browser->source());
    QTreeWidgetItem *current = nullptr;
    for (QTreeWidgetItemIterator it(m_contents); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == page) {
            current = *it;
            break;
        }
    if (!current) return;
    m_contents->setCurrentItem(current);
    if (current->parent()) current->parent()->setExpanded(true);  // the other areas stay as the user left them
}

void ManualWindow::filter(const QString &text) {
    const QString query = text.trimmed().toLower();
    for (int g = 0; g < m_contents->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_contents->topLevelItem(g);
        bool any = false;
        for (int p = 0; p < group->childCount(); ++p) {
            QTreeWidgetItem *item = group->child(p);
            const bool shown = query.isEmpty() || m_text.value(item->data(0, Qt::UserRole).toString()).contains(query);
            item->setHidden(!shown);
            any = any || shown;
        }
        const bool groupShown = any || query.isEmpty() || m_text.value(group->data(0, Qt::UserRole).toString()).contains(query);
        group->setHidden(!groupShown);
        group->setExpanded(!query.isEmpty() && any);
    }
    if (query.isEmpty()) selectCurrent();  // the open page's area is unfolded again
}
