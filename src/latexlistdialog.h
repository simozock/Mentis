#pragma once

#include "modaldialog.h"

#include <QStringList>
#include <QVector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;

// FEAT-132: list of LaTeX commands opened over the formula modal ("Symbols and formulas…"). Search
// field, category select and a grid of entries, each with the drawn formula, its code and its name.
// A click or Enter on an entry (or "Insert" on the current one) closes the list with that entry;
// Esc or the X close it without inserting. The grid is a QListView with uniform cells: only the
// cells in view are painted, each preview is laid out once per list.
class LatexListDialog final : public ModalDialog {
    Q_OBJECT
public:
    explicit LatexListDialog(QWidget *parent);

    // Code of the chosen entry (LatexCatalogue::Entry::code); empty when nothing is current.
    QString chosenCode() const;
    QLineEdit *searchField() const { return m_search; }
    QComboBox *categorySelect() const { return m_category; }
    QListView *list() const { return m_list; }
    QLabel *emptyLabel() const { return m_empty; }
    int resultCount() const { return int(m_found.size()); }
    // Entry index (into LatexCatalogue::entries()) of the result at row.
    int entryAt(int row) const { return m_found.value(row, -1); }

    void accept() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    class Model;
    class Delegate;
    void refilter();
    void fitGrid();
    QLineEdit *m_search = nullptr;
    QComboBox *m_category = nullptr;
    QListView *m_list = nullptr;
    QLabel *m_empty = nullptr;
    Model *m_model = nullptr;
    QStringList m_texts;
    QVector<int> m_found;
};
