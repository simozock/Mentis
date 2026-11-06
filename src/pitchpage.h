#pragma once

// FEAT-063 stage A: the Pitch tab of the side panel (attachments/FEAT-063/01-07) in
// Mentis's colours. Preview of the selected topic's slide, Change theme, Aspect ratio; for the
// selected topic Topic slide, List slide, Delivery and Layout. It shows the model and asks for
// changes through the callbacks: MainWindow makes them history steps (Document::setPitch*).

#include "document.h"
#include "pitch.h"
#include "pitchtemplate.h"

#include <QWidget>

#include <functional>

class MindMapModel;
class PitchTemplateEditor;
class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;
struct MindNode;

// The name of a built-in theme, as the Change theme menu shows it.
QString pitchThemeName(Pitch::Theme theme);

class PitchPreview final : public QWidget {
public:
    explicit PitchPreview(QWidget *parent = nullptr);
    void showSlide(const Pitch::Slide &slide, const Pitch::Look &look, qreal ratio);
    QSize sizeHint() const override { return {240, 135}; }
    static constexpr int kMinHeightPx = 90;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void fitHeight();
    Pitch::Slide m_slide;
    Pitch::Look m_look;
    qreal m_ratio = 16.0 / 9.0;
};

class PitchPage final : public QWidget {
    Q_OBJECT
public:
    explicit PitchPage(QWidget *parent = nullptr);

    std::function<void(Document::PitchField field, const QString &value)> setTopicValue;
    // clearTemplate: a theme was chosen, it replaces the sheet's template (FEAT-089).
    std::function<void(const QString &theme, const QString &aspect, bool clearTemplate)> setSheetValues;
    // FEAT-089: the sheet takes a copy of the template (Pitch::toJson; empty = none).
    std::function<void(const QJsonObject &pitchTemplate)> setSheetTemplate;
    // File dialogs (import, the editor's pictures and export), replaceable by the tests.
    std::function<QString(const QString &caption, const QString &filter, bool save, const QString &name)> pickFile;

    PitchTemplateEditor *templateEditor() const { return m_editor; }
    bool isEditingTemplate() const { return m_editing; }
    // Another sheet is shown: the draft is dropped (one model serves every sheet).
    void cancelEditing() {
        if (m_editing) stopEditing();
    }
    // FEAT-089: the editor on a new template / on the sheet's template.
    void newTemplate();
    void editSheetTemplate();
    // Reads a .mentis-theme file into the library and applies it; false with a message if it is not one.
    bool importTemplate(const QString &path);

    // Shows the sheet's settings and the topic's (nullptr: none or several selected, its rows off).
    void showTopic(const MindMapModel *model, const MindNode *topic);

private:
    void chooseTheme();
    void refill(QComboBox *select, const QStringList &labels, int current);
    void startEditing(const Pitch::Template &value, bool inLibrary);
    void stopEditing();
    void saveTemplate();
    void showPreview();

    const MindMapModel *m_model = nullptr;
    const MindNode *m_topic = nullptr;
    bool m_updating = false;  // while showTopic fills the controls: their signals change nothing
    PitchPreview *m_preview = nullptr;
    QPushButton *m_theme = nullptr;
    QComboBox *m_aspect = nullptr;
    QComboBox *m_topicSlide = nullptr;
    QComboBox *m_listSlide = nullptr;
    QComboBox *m_delivery = nullptr;
    QList<QToolButton *> m_layouts;
    QList<QWidget *> m_topicRows;
    QList<QWidget *> m_sheetRows;     // hidden while a template is edited
    QPushButton *m_editTemplate = nullptr;
    PitchTemplateEditor *m_editor = nullptr;
    bool m_editing = false;
};
