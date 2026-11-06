#pragma once

// FEAT-089: the editor of a Pitch template, in the Pitch tab under the slide preview (which shows
// the draft). It changes only its draft: PitchPage saves it to the library and applies it.

#include "pitchtemplate.h"

#include <QWidget>

#include <array>
#include <functional>

class ColorField;
class MindMapModel;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class ToggleSwitch;

class PitchTemplateEditor final : public QWidget {
    Q_OBJECT
public:
    explicit PitchTemplateEditor(QWidget *parent = nullptr);

    // inLibrary: Delete is offered (a template only in the map has nothing to delete).
    void edit(const Pitch::Template &value, const MindMapModel *model, bool inLibrary);
    const Pitch::Template &draft() const { return m_draft; }
    // The draft to save: a name left empty is the one it had.
    Pitch::Template result() const;

    // File dialogs, replaceable by the tests: save = a file to write; empty = cancelled.
    std::function<QString(const QString &caption, const QString &filter, bool save, const QString &name)> pickFile;
    // Asks before deleting; replaceable by the tests.
    std::function<bool(const QString &name)> confirmDelete;

    static constexpr int kColorCount = 9;

signals:
    void draftChanged();
    void saveRequested();
    void cancelRequested();
    void deleteRequested();

private:
    void changed();
    void refresh();
    void choosePicture(QByteArray Pitch::Template::*member, int maxSide, bool alwaysPng);
    void exportDraft();

    Pitch::Template m_draft;
    QString m_savedName;
    const MindMapModel *m_model = nullptr;
    bool m_updating = false;
    QLineEdit *m_name = nullptr;
    QComboBox *m_base = nullptr;
    std::array<ColorField *, kColorCount> m_colors{};
    QComboBox *m_titleFont = nullptr;
    QComboBox *m_itemFont = nullptr;
    QComboBox *m_itemStyle = nullptr;
    QComboBox *m_motif = nullptr;
    std::array<QPushButton *, 3> m_removePicture{};
    std::array<QLabel *, 3> m_pictureState{};
    QComboBox *m_logoCorner = nullptr;
    QComboBox *m_logoSize = nullptr;
    QLineEdit *m_footer = nullptr;
    ToggleSwitch *m_numbers = nullptr;
    QPushButton *m_delete = nullptr;
};
