#pragma once

#include "formulatext.h"

#include <QAbstractListModel>
#include <QColor>
#include <QQuickPaintedItem>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

// FEAT-132: the desktop's LaTeX list (LatexListDialog) for the phone's LatexListDialog.qml: the
// entries of LatexCatalogue matching `query` in `category` (-1 = all). Roles: code (inserted),
// preview (drawn), name (translated). Search texts are folded once per model.
class LatexList : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role { CodeRole = Qt::UserRole + 1, PreviewRole, NameRole };

    explicit LatexList(QObject *parent = nullptr);

    QString query() const { return m_query; }
    void setQuery(const QString &query);
    int category() const { return m_category; }
    void setCategory(int category);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // The category select's choices ({value, name, group}): All categories, then each category.
    Q_INVOKABLE QVariantList categories() const;
    Q_INVOKABLE QString codeAt(int row) const;
    // LatexCatalogue::insert: {text, caret}.
    Q_INVOKABLE QVariantMap insert(const QString &value, int start, int end, const QString &code) const;

signals:
    void queryChanged();
    void categoryChanged();
    void countChanged();

private:
    void refilter();

    QString m_query;
    int m_category = -1;
    QStringList m_texts;
    QVector<int> m_found;
};

// FEAT-132: one entry's formula, drawn in `color` and scaled down to fit the item. Each code is laid
// out once per session on the GUI thread (FormulaText keeps a QPicture) and replayed by every cell
// showing it.
class LatexFormula : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString code READ code WRITE setCode NOTIFY codeChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    explicit LatexFormula(QQuickItem *parent = nullptr);

    QString code() const { return m_code; }
    void setCode(const QString &code);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    void paint(QPainter *painter) override;

signals:
    void codeChanged();
    void colorChanged();

private:
    void relayout();

    QString m_code;
    QColor m_color = Qt::white;
    FormulaText m_formula;
};
