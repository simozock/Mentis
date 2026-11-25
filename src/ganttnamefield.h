#pragma once

#include <QTextEdit>

class ShortcutRegistry;
struct MindNode;

// FEAT-124: a Gantt task's name typed with its formats (GanttNames), in the table's Name cell and
// in the task panel: one line like the QLineEdit it replaces (text, editingFinished, returnPressed;
// Up and Down left to the panel), with the Markdown shortcuts (FEAT-068) and the format keys
// (FEAT-095: on the selection, nothing without one). Pasted text is plain, on one line.
class GanttNameField final : public QTextEdit {
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText)
public:
    explicit GanttNameField(QWidget *parent = nullptr);

    QString text() const { return toPlainText(); }
    void setText(const QString &text);
    // The topic's shown name with its formats; boldRow: a summary's row.
    void load(const MindNode *topic, bool boldRow);
    // The name typed (GanttNames::edited); false when nothing is to be written.
    bool result(const MindNode *topic, QString *title, QString *html) const;

    // The shortcut list (FEAT-041) with the format keys; none: no format keys.
    const ShortcutRegistry *shortcuts = nullptr;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void editingFinished();
    void returnPressed();

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void centreLine();
    int styleKey(const QKeyEvent *event) const;  // RichTitle::CharStyle, -1 for none
    void toggle(int style);
    bool m_boldRow = false;
    bool m_formattable = true;  // false for a title with a formula (GanttNames::formattable)
};
