#include "latexlistdialog.h"

#include "formulatext.h"
#include "latexcatalogue.h"
#include "stylecontrols.h"
#include "uitheme.h"

#include <QAbstractListModel>
#include <QComboBox>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {

constexpr int kCellMinWidthPx = 160;
// The code and name lines follow the interface's text size (Settings > Appearance).
int lineHeightPx() { return UiTheme::fontPx(12) + 4; }
constexpr int kPreviewHeightPx = 48;
constexpr int kVisibleRows = 4;
constexpr int kEntryRole = Qt::UserRole + 1;
// Box inset 2 + preview top 6 + preview + 2 + two lines + 6 + inset 2: 98 at the default size.
int cellHeightPx() { return 2 + 6 + kPreviewHeightPx + 2 + 2 * lineHeightPx() + 6 + 2; }

}  // namespace

// The results: one row per found entry, the entry's index in kEntryRole.
class LatexListDialog::Model final : public QAbstractListModel {
public:
    using QAbstractListModel::QAbstractListModel;
    void setFound(const QVector<int> &found) {
        beginResetModel();
        m_found = found;
        endResetModel();
    }
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_found.size()); }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() >= m_found.size()) return {};
        const LatexCatalogue::Entry &entry = LatexCatalogue::entries()[m_found[index.row()]];
        switch (role) {
        case kEntryRole: return m_found[index.row()];
        case Qt::ToolTipRole:
        case Qt::AccessibleTextRole:
        case Qt::DisplayRole: return LatexCatalogue::name(entry) + QStringLiteral(": ") + QString::fromUtf8(entry.code);
        default: return {};
        }
    }

private:
    QVector<int> m_found;
};

// A cell: rounded box (surfaceSunken; hover divider, current surfaceRaised, as the trees in a modal),
// the formula drawn at the top, the code and the name below in 12 px.
class LatexListDialog::Delegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const int entryIndex = index.data(kEntryRole).toInt();
        const LatexCatalogue::Entry &entry = LatexCatalogue::entries()[entryIndex];
        const QRectF cell = QRectF(option.rect).adjusted(2, 2, -2, -2);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        const bool current = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        painter->setBrush(UiTheme::color(current ? UiTheme::Token::SurfaceRaised
                                                 : hover ? UiTheme::Token::Divider : UiTheme::Token::SurfaceSunken));
        painter->drawRoundedRect(cell, 4, 4);
        if (current) {
            painter->setPen(QPen(UiTheme::color(UiTheme::Token::Accent), 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(cell.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        }
        const FormulaText &formula = preview(entryIndex);
        const QRectF previewRect(cell.left() + 8, cell.top() + 6, cell.width() - 16, kPreviewHeightPx);
        if (!formula.isEmpty()) {
            // A formula wider or taller than its place is scaled down to fit.
            const QSizeF size = formula.size();
            const qreal scale = std::min({1.0, previewRect.width() / std::max<qreal>(1, size.width()),
                                          previewRect.height() / std::max<qreal>(1, size.height())});
            painter->save();
            painter->translate(previewRect.center());
            painter->scale(scale, scale);
            formula.draw(painter, QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height()), Qt::AlignCenter);
            painter->restore();
        }
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPixelSize(UiTheme::fontPx(12));
        painter->setFont(mono);
        painter->setPen(UiTheme::color(UiTheme::Token::Text));
        const QRectF codeRect(cell.left() + 8, previewRect.bottom() + 2, cell.width() - 16, lineHeightPx());
        painter->drawText(codeRect, Qt::AlignHCenter | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(QString::fromUtf8(entry.code), Qt::ElideRight, int(codeRect.width())));
        QFont small = UiTheme::interfaceFont();
        small.setPixelSize(UiTheme::fontPx(12));
        painter->setFont(small);
        painter->setPen(UiTheme::color(UiTheme::Token::TextMuted));
        const QRectF nameRect(cell.left() + 8, codeRect.bottom(), cell.width() - 16, lineHeightPx());
        painter->drawText(nameRect, Qt::AlignHCenter | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(LatexCatalogue::name(entry), Qt::ElideRight, int(nameRect.width())));
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {kCellMinWidthPx, cellHeightPx()}; }

private:
    // Laid out the first time the cell is painted, then replayed (FormulaText keeps a QPicture).
    const FormulaText &preview(int entryIndex) const {
        auto found = m_previews.find(entryIndex);
        if (found != m_previews.end()) return *found;
        QFont font = UiTheme::interfaceFont();
        font.setPixelSize(18);
        FormulaText formula;
        formula.updateBlock(LatexCatalogue::previewCode(LatexCatalogue::entries()[entryIndex]), font,
                            UiTheme::color(UiTheme::Token::Text));
        return *m_previews.insert(entryIndex, formula);
    }
    mutable QHash<int, FormulaText> m_previews;
};

LatexListDialog::LatexListDialog(QWidget *parent) : ModalDialog(parent, tr("Symbols and formulas"), tr("Insert"), 760) {
    m_texts = LatexCatalogue::searchTexts();
    auto *filters = new QHBoxLayout;
    filters->setSpacing(8);
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("LatexListSearch"));
    // The example is code, not text: outside tr (the phone's QML shares the text, and lupdate reads
    // "\\f" there as a form feed).
    m_search->setPlaceholderText(tr("Search, for example integral, arrow or %1").arg(QStringLiteral("\\frac")));
    m_search->setAccessibleName(tr("Search"));
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    filters->addWidget(m_search, 1);
    m_category = makeDarkSelect(this);
    m_category->setObjectName(QStringLiteral("LatexListCategory"));
    m_category->setAccessibleName(tr("Category"));
    m_category->addItem(tr("All categories"), -1);
    for (LatexCatalogue::Category category : LatexCatalogue::categories())
        m_category->addItem(LatexCatalogue::categoryName(category), int(category));
    filters->addWidget(m_category);
    contentLayout()->addLayout(filters);

    m_list = new QListView;
    m_list->setObjectName(QStringLiteral("LatexListGrid"));
    m_list->setViewMode(QListView::IconMode);
    m_list->setFlow(QListView::LeftToRight);
    m_list->setWrapping(true);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setUniformItemSizes(true);
    m_list->setSpacing(0);
    m_list->setMouseTracking(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);  // the columns do not change while filtering
    m_list->setFixedHeight(kVisibleRows * cellHeightPx() + 2);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setStyleSheet(UiTheme::qss(QStringLiteral(
        "QListView { background:transparent; border:0; outline:none; }"
        "QScrollBar:vertical { background:transparent; width:8px; margin:0; }"
        "QScrollBar::handle:vertical { background:@divider; border-radius:4px; min-height:24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }")));
    m_model = new Model(m_list);
    m_list->setModel(m_model);
    m_list->setItemDelegate(new Delegate(m_list));
    m_list->viewport()->installEventFilter(this);
    m_list->installEventFilter(this);
    contentLayout()->addWidget(m_list);
    m_empty = new QLabel(tr("No results"));
    m_empty->setObjectName(QStringLiteral("LatexListEmpty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setStyleSheet(UiTheme::qss(QStringLiteral("color:@textMuted;")));
    m_empty->hide();
    contentLayout()->addWidget(m_empty);

    connect(m_search, &QLineEdit::textChanged, this, &LatexListDialog::refilter);
    connect(m_category, &QComboBox::currentIndexChanged, this, &LatexListDialog::refilter);
    // A click chooses the entry, as in MentisNote's cheatsheet.
    connect(m_list, &QListView::clicked, this, [this](const QModelIndex &index) {
        m_list->setCurrentIndex(index);
        accept();
    });
    refilter();
    m_search->setFocus();
}

QString LatexListDialog::chosenCode() const {
    const QModelIndex current = m_list->currentIndex();
    if (!current.isValid()) return {};
    return QString::fromUtf8(LatexCatalogue::entries()[current.data(kEntryRole).toInt()].code);
}

void LatexListDialog::accept() {
    if (chosenCode().isEmpty()) return;
    ModalDialog::accept();
}

void LatexListDialog::refilter() {
    const int category = m_category->currentData().toInt();
    m_found = LatexCatalogue::search(m_texts, m_search->text(), LatexCatalogue::Category(std::max(0, category)), category < 0);
    m_model->setFound(m_found);
    if (!m_found.isEmpty()) m_list->setCurrentIndex(m_model->index(0));
    m_list->verticalScrollBar()->setValue(0);
    m_empty->setVisible(m_found.isEmpty());
    acceptButton()->setEnabled(!m_found.isEmpty());
}

void LatexListDialog::fitGrid() {
    // Whole columns across the viewport: the cells share the width, none is cut on the right.
    // QListView wraps a cell that reaches the viewport's last pixel column: one pixel less.
    const int width = m_list->viewport()->width() - 1;
    const int columns = std::max(1, width / kCellMinWidthPx);
    const QSize grid(width / columns, cellHeightPx());
    if (m_list->gridSize() != grid) m_list->setGridSize(grid);
}

bool LatexListDialog::eventFilter(QObject *watched, QEvent *event) {
    if (m_list && watched == m_list->viewport() && event->type() == QEvent::Resize) fitGrid();
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        // Down from the search goes to the grid; Up from its first row back to the search.
        if (watched == m_search && key->key() == Qt::Key_Down && !m_found.isEmpty()) {
            m_list->setFocus();
            return true;
        }
        if (watched == m_list) {
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                accept();
                return true;
            }
            if (key->key() == Qt::Key_Up && m_list->visualRect(m_list->currentIndex()).top() <= m_list->gridSize().height() / 2
                && m_list->verticalScrollBar()->value() == 0) {
                m_search->setFocus();
                return true;
            }
        }
    }
    return ModalDialog::eventFilter(watched, event);
}
