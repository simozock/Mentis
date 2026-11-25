#pragma once

#include "pitch.h"
#include "pitchchoices.h"
#include "pitchtemplate.h"

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class MobileMap;
class QVariantAnimation;
struct MindNode;

// FEAT-120 stage A: the side panel's Pitch page on the phone. What the desktop's PitchPage shows
// (PitchPage::showTopic) and the calls MainWindow makes for it (Document::setPitchTopic,
// setPitchSheet), each one Undo step. The topic is MainWindow::pitchTopic's: the selected one, the
// central topic when none is selected, none when several are. Names are PitchChoices'.
class MobilePitch : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MobileMap *map READ map WRITE setMap NOTIFY mapChanged)
    // Bumped when the selection or the map changes: the page's bindings read through it.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // FEAT-120 stage B: the template editor is open on the page (its draft in the preview).
    Q_PROPERTY(bool editing READ editing NOTIFY changed)
public:
    // Document::PitchField's values, for QML.
    enum Field { TopicSlide, ListSlide, Delivery, Layout };
    Q_ENUM(Field)
    // The template editor's fields with a single value (PitchTemplateEditor::changed).
    enum DraftField { Name, Base, TitleFont, ItemFont, ItemStyle, Motif, LogoCorner, LogoSize, Footer, Numbers };
    Q_ENUM(DraftField)

    explicit MobilePitch(QObject *parent = nullptr);
    MobileMap *map() const { return m_map; }
    void setMap(MobileMap *map);
    int revision() const { return m_revision; }
    bool editing() const { return m_editing; }

    // {open, aspect, theme (-1 with a template), hasTemplate, templateId, topic (the topic rows enabled),
    // topicSlide, listSlide, delivery, layout (indices), topicSlideNames, listSlideNames}.
    Q_INVOKABLE QVariantMap state() const;
    Q_INVOKABLE QStringList aspectNames() const;
    Q_INVOKABLE QStringList deliveryNames() const;
    Q_INVOKABLE QStringList layoutNames() const;
    // The Change theme menu: [{theme, name, illustrated}] in the desktop's order.
    Q_INVOKABLE QVariantList themes() const;
    Q_INVOKABLE bool chooseTopic(int field, int index);
    Q_INVOKABLE bool chooseAspect(int index);
    // A built-in theme replaces the sheet's template (MainWindow's setSheetValues, clearTemplate).
    Q_INVOKABLE bool chooseTheme(int theme);

    // FEAT-120 stage B, FEAT-089 on the phone: the Change theme menu's Templates group
    // (PitchChoices::templateCards): [{id, name, onlyHere}]; read again at each call.
    Q_INVOKABLE QVariantList templates();
    // The sheet takes a copy of the library's template (MainWindow's setSheetTemplate: its base theme
    // too), one Undo step; the map's own copy (onlyHere) stays as it is.
    Q_INVOKABLE bool chooseTemplate(const QString &id);
    // PitchPage::newTemplate / editSheetTemplate: the editor on a new template (on the sheet's theme
    // or template base) or on the sheet's template.
    Q_INVOKABLE bool newTemplate();
    Q_INVOKABLE bool editTemplate();
    // The draft as the editor shows it: {name, base, colors [{name, value, own}], titleFont, itemFont,
    // itemStyle, motif, pictures [{name, chosen}], logoCorner, logoSize, footer, numbers, inLibrary}.
    // A colour the draft leaves to the base theme shows the theme's (own false).
    Q_INVOKABLE QVariantMap draft() const;
    // The editor's selects: [{value, name}].
    Q_INVOKABLE QVariantList draftChoices(int field) const;
    Q_INVOKABLE bool setDraft(int field, const QVariant &value);
    Q_INVOKABLE bool setDraftColor(int index, const QColor &color);
    Q_INVOKABLE void resetDraftColors();
    // A picked picture (PitchTemplateEditor::choosePicture: reduced as the desktop does); false with
    // the desktop's message in the map's error when it cannot be read.
    Q_INVOKABLE bool setDraftPicture(int index, const QUrl &url);
    Q_INVOKABLE void removeDraftPicture(int index);
    // Save: the library's file, then the sheet's copy (one Undo step); Cancel; Delete (asked first by
    // the page) removes the library's file, maps keep their copy. False with the map's error.
    Q_INVOKABLE bool saveDraft();
    Q_INVOKABLE void cancelDraft();
    Q_INVOKABLE bool deleteDraft();
    // Export...: the draft written as a .mentis-theme and handed to Android's Share.
    Q_INVOKABLE bool exportDraft();
    // Import template...: a picked .mentis-theme into the library, applied to the sheet.
    Q_INVOKABLE bool importTemplate(const QUrl &url);
    // The draft to save: a name left empty is the one it had (PitchTemplateEditor::result).
    Pitch::Template draftResult() const;

    // For the drawn items. The preview's slide (PitchPage::showPreview) and the sheet's look.
    MindMapModel *model() const;
    Pitch::Slide previewSlide() const;
    Pitch::Look sheetLook() const;
    // The preview's look: the draft's while a template is edited, else the sheet's.
    Pitch::Look previewLook() const;
    // A Templates card's look (from the last templates() call).
    Pitch::Look templateLook(const QString &id) const;
    Pitch::Look themeLook(Pitch::Theme theme) const;
    Pitch::Aspect aspect() const;

signals:
    void mapChanged();
    void changed();

private:
    MindNode *topic() const;
    void bump();
    void startEditing(const Pitch::Template &value, bool inLibrary);
    // The sheet's copy of a template (MainWindow's setSheetTemplate): its base theme kept as the
    // sheet's theme. False when nothing changes.
    bool applyTemplate(const Pitch::Template &value);
    // A picked file copied for reading; empty with the reason.
    QString pickedCopy(const QUrl &url, QString *error) const;
    QPointer<MobileMap> m_map;
    int m_revision = 0;
    bool m_editing = false;
    bool m_inLibrary = false;
    Pitch::Template m_draft;
    QString m_savedName;
    QList<PitchChoices::TemplateCard> m_cards;
};

// The preview of the Pitch page (theme < 0, PitchPreview: the slide in the sheet's ratio, bands of
// its ground, radius 8) or a card of the Change theme menu (theme >= 0: PitchChoices::drawThemeCard).
class PitchSlideView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MobilePitch *pitch READ pitch WRITE setPitch NOTIFY pitchChanged)
    Q_PROPERTY(int theme READ theme WRITE setTheme NOTIFY pitchChanged)
    // FEAT-120 stage B: a Templates card (its cover), when not empty.
    Q_PROPERTY(QString templateId READ templateId WRITE setTemplateId NOTIFY pitchChanged)
    // The preview's slide ratio (width / height), for the page's height.
    Q_PROPERTY(qreal ratio READ ratio NOTIFY ratioChanged)
public:
    explicit PitchSlideView(QQuickItem *parent = nullptr);
    MobilePitch *pitch() const { return m_pitch; }
    void setPitch(MobilePitch *pitch);
    int theme() const { return m_theme; }
    void setTheme(int theme);
    QString templateId() const { return m_templateId; }
    void setTemplateId(const QString &id);
    qreal ratio() const { return m_ratio; }
    void paint(QPainter *painter) override;

signals:
    void pitchChanged();
    void ratioChanged();

private:
    void refresh();
    QPointer<MobilePitch> m_pitch;
    int m_theme = -1;
    QString m_templateId;
    qreal m_ratio = 16.0 / 9.0;
};

// A layout's icon of the Pitch page (PitchChoices::drawLayoutIcon, 20 x 20 scaled to the item).
class PitchLayoutIcon : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int layout READ layout WRITE setLayout NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
public:
    explicit PitchLayoutIcon(QQuickItem *parent = nullptr);
    int layout() const { return m_layout; }
    void setLayout(int layout);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    void paint(QPainter *painter) override;

signals:
    void changed();

private:
    int m_layout = 0;
    QColor m_color = Qt::white;
};

// FEAT-120: the presentation, as the desktop's PitchWindow: the active sheet's slides from the cover,
// the slide in the item's middle in the sheet's ratio (Auto = the item's), bands in the slide's
// ground; on Android the phone turns to landscape while it runs (user's decision 2026-11-14); fades as the desktop (Standard only). The page asks for
// next and previous (taps and swipes); a hardware keyboard steps with PitchWindow's keys (Esc is the
// page's Back); finish gives the topic of the last slide shown.
class PitchPresentation : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int slide READ slide NOTIFY stepChanged)
    Q_PROPERTY(int shown READ shown NOTIFY stepChanged)
    Q_PROPERTY(int slideCount READ slideCount NOTIFY stepChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
public:
    static constexpr int kSlideFadeMs = 300;  // PitchWindow's
    static constexpr int kItemFadeMs = 200;

    explicit PitchPresentation(QQuickItem *parent = nullptr);
    int slide() const { return m_slide; }
    int shown() const { return m_shown; }
    int slideCount() const { return int(m_slides.size()); }
    bool running() const { return m_running; }
    // Builds the slides of pitch's sheet; false (and the map's message, the desktop's) without any.
    Q_INVOKABLE bool start(MobilePitch *pitch);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    // Ends it: the topic of the last slide shown is selected on the map.
    Q_INVOKABLE void finish();
    // The slide's rectangle in the item (for tests).
    Q_INVOKABLE QRectF slideRect() const;
    void paint(QPainter *painter) override;

signals:
    void stepChanged();
    void runningChanged();
    void finished();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void goTo(int slide, bool allItems);
    void startItemFade();
    void paintSlide(QPainter *painter, qreal newest) const;
    // On Android: the screen kept on and the phone held in landscape while presenting (user, 2026-11-14).
    static void presentOnAndroid(bool on);
    QList<Pitch::Slide> m_slides;
    Pitch::Look m_look;
    Pitch::Aspect m_aspect = Pitch::Aspect::Auto;
    QPointer<MobilePitch> m_pitch;
    int m_slide = 0;
    int m_shown = 0;
    bool m_running = false;
    qreal m_itemFade = 1;
    qreal m_slideFade = 1;
    QImage m_before;  // the slide fading out
    QVariantAnimation *m_animation = nullptr;
};
