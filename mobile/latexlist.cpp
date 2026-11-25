#include "latexlist.h"

#include "formulatext.h"
#include "latexcatalogue.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>

LatexList::LatexList(QObject *parent) : QAbstractListModel(parent), m_texts(LatexCatalogue::searchTexts()) { refilter(); }

void LatexList::setQuery(const QString &query) {
    if (query == m_query) return;
    m_query = query;
    emit queryChanged();
    refilter();
}

void LatexList::setCategory(int category) {
    if (category == m_category) return;
    m_category = category;
    emit categoryChanged();
    refilter();
}

void LatexList::refilter() {
    const int before = int(m_found.size());
    beginResetModel();
    m_found = LatexCatalogue::search(m_texts, m_query, LatexCatalogue::Category(std::max(0, m_category)), m_category < 0);
    endResetModel();
    if (before != m_found.size()) emit countChanged();
}

int LatexList::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_found.size()); }

QVariant LatexList::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_found.size()) return {};
    const LatexCatalogue::Entry &entry = LatexCatalogue::entries()[m_found[index.row()]];
    switch (role) {
    case CodeRole: return QString::fromUtf8(entry.code);
    case PreviewRole: return LatexCatalogue::previewCode(entry);
    case NameRole: return LatexCatalogue::name(entry);
    default: return {};
    }
}

QHash<int, QByteArray> LatexList::roleNames() const {
    return {{CodeRole, "code"}, {PreviewRole, "preview"}, {NameRole, "name"}};
}

QVariantList LatexList::categories() const {
    QVariantList list;
    list.append(QVariantMap{{QStringLiteral("value"), -1},
                            {QStringLiteral("name"), QCoreApplication::translate("LatexListDialog", "All categories")},
                            {QStringLiteral("group"), QString()}});
    for (LatexCatalogue::Category category : LatexCatalogue::categories())
        list.append(QVariantMap{{QStringLiteral("value"), int(category)},
                                {QStringLiteral("name"), LatexCatalogue::categoryName(category)},
                                {QStringLiteral("group"), QString()}});
    return list;
}

QString LatexList::codeAt(int row) const {
    return row >= 0 && row < m_found.size() ? QString::fromUtf8(LatexCatalogue::entries()[m_found[row]].code) : QString();
}

QVariantMap LatexList::insert(const QString &value, int start, int end, const QString &code) const {
    const LatexCatalogue::Insertion insertion = LatexCatalogue::insert(value, std::min(start, end), std::max(start, end), code);
    return {{QStringLiteral("text"), insertion.text}, {QStringLiteral("caret"), insertion.caret}};
}

LatexFormula::LatexFormula(QQuickItem *parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }

void LatexFormula::setCode(const QString &code) {
    if (code == m_code) return;
    m_code = code;
    emit codeChanged();
    relayout();
}

void LatexFormula::setColor(const QColor &color) {
    if (color == m_color) return;
    m_color = color;
    emit colorChanged();
    relayout();
}

void LatexFormula::relayout() {
    // On the GUI thread (JKQtMathText creates QObjects): paint, on the render thread, only replays
    // the picture. The grid recycles its cells while scrolling, so each layout is kept per code and colour.
    static QHash<QString, FormulaText> cache;
    if (m_code.isEmpty()) {
        m_formula = FormulaText();
    } else {
        const QString key = m_color.name(QColor::HexArgb) + m_code;
        auto found = cache.find(key);
        if (found == cache.end()) {
            QFont font = QGuiApplication::font();
            font.setPixelSize(18);
            FormulaText formula;
            formula.updateBlock(m_code, font, m_color);
            found = cache.insert(key, formula);
        }
        m_formula = *found;
    }
    update();
}

void LatexFormula::paint(QPainter *painter) {
    if (m_formula.isEmpty()) return;
    const QSizeF size = m_formula.size();
    const qreal scale = std::min({1.0, width() / std::max<qreal>(1, size.width()), height() / std::max<qreal>(1, size.height())});
    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(width() / 2, height() / 2);
    painter->scale(scale, scale);
    m_formula.draw(painter, QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height()), Qt::AlignCenter);
}
