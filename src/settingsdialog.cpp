#include "settingsdialog.h"
#include "markdownshortcuts.h"

#include "custompalettes.h"
#include "mcpclients.h"
#include "mcpmaptools.h"
#include "localization.h"
#include "mapfolder.h"
#include "modaldialog.h"
#include "segmentedcontrol.h"
#include "shortcuts.h"
#include "stylecontrols.h"
#include "spellchecker.h"
#include "toggleswitch.h"
#include "uitheme.h"
#include "help.h"
#include "tour.h"
#include "updates.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QThread>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
// Limiti dei controlli: oltre 4 px di spessore e 8 px di distanza l'anello si confonde con i
// nodi vicini (il passo verticale minimo tra nodi è 64 px, AGENTS.md "Mappa").
constexpr int kMaxWidth = 4;
constexpr int kMaxGap = 8;
// Misure di FEAT-035 ("Finestra Impostazioni").
constexpr int kDialogWidthPx = 760;
constexpr int kDialogHeightPx = 600;
constexpr int kHeaderHeightPx = 48;
constexpr int kRadiusPx = 8;
constexpr QSize kTabSize(96, 72);  // 2026-10-18: piu' alta, icona e nome avevano poco margine

// Select scura con la freccia chiara e le voci leggibili (come le select del pannello).
class SettingsSelect final : public QComboBox {
public:
    SettingsSelect() { setItemDelegate(new LightComboItemDelegate(this)); setMinimumWidth(160); }

protected:
    void paintEvent(QPaintEvent *event) override {
        QComboBox::paintEvent(event);
        paintDarkSelectChevron(this);
    }
};

// Riga di un riquadro: linea divider sopra (nascosta sulla prima riga visibile), etichetta 14px a
// sinistra con l'eventuale descrizione 12px sotto, controllo a destra; 48 px di altezza minima.
QWidget *settingsRow(const QString &label, const QString &description, QWidget *control) {
    auto *row = new QWidget;
    row->setObjectName(QStringLiteral("SettingsRow"));
    row->setProperty("settingsRow", true);  // resta anche se il nome cambia (righe delle scorciatoie)
    auto *outer = new QVBoxLayout(row);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *divider = new QFrame;
    divider->setObjectName(QStringLiteral("SettingsRowDivider"));
    divider->setFixedHeight(1);
    outer->addWidget(divider);
    auto *content = new QWidget;
    content->setMinimumHeight(48);
    auto *line = new QHBoxLayout(content);
    line->setContentsMargins(16, 8, 16, 8);
    line->setSpacing(16);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto *name = new QLabel(label);
    name->setWordWrap(true);
    texts->addWidget(name);
    if (!description.isEmpty()) {
        auto *hint = new QLabel(description);
        hint->setObjectName(QStringLiteral("settingsHint"));
        hint->setWordWrap(true);
        texts->addWidget(hint);
    }
    line->addLayout(texts, 1);
    if (control) {
        line->addWidget(control, 0, Qt::AlignVCenter);
        if (control->accessibleName().isEmpty()) control->setAccessibleName(label);
    }
    outer->addWidget(content);
    return row;
}

// La linea sopra la prima riga visibile del riquadro non serve (anche dopo un filtro).
void updateRowDividers(QFrame *card) {
    bool first = true;
    for (QWidget *row : card->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
        if (!row->property("settingsRow").toBool() || row->isHidden()) continue;
        row->findChild<QFrame *>(QStringLiteral("SettingsRowDivider"))->setVisible(!first);
        first = false;
    }
}

QFrame *settingsCard(const QList<QWidget *> &rows) {
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("SettingsCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    for (QWidget *row : rows) layout->addWidget(row);
    updateRowDividers(card);
    return card;
}

QLabel *sectionTitle(const QString &text) {
    auto *title = new QLabel(text);
    title->setObjectName(QStringLiteral("settingsSection"));
    return title;
}

// Pagina che scorre, con i margini del contenuto.
QScrollArea *scrollPage(QWidget *content) {
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    return scroll;
}

QPushButton *defaultsButton() {
    auto *button = new QPushButton(QCoreApplication::translate("SettingsDialog", "Defaults"));
    button->setObjectName(QStringLiteral("SettingsDefaults"));
    return button;
}
}  // namespace

// Anteprima della pagina Editor: due nodi d'esempio sulla mappa chiara, uno selezionato e uno in
// hover, disegnati con gli stessi passi di NodeItem::paint() (riquadro raggio 8, anello concentrico).
class SelectionPreview final : public QWidget {
    // Senza Q_OBJECT tr() userebbe il contesto "QWidget" e non troverebbe la traduzione (ADR-003).
    Q_DECLARE_TR_FUNCTIONS(SelectionPreview)
public:
    explicit SelectionPreview(QWidget *parent = nullptr) : QWidget(parent) { setFixedHeight(112); }
    void setSelectionStyle(const NodeItem::SelectionStyle &style) { m_style = style; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // Fondo della mappa (chiara anche con l'interfaccia scura, FEAT-035).
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#ffffff"));
        p.drawRoundedRect(QRectF(rect()), kRadiusPx, kRadiusPx);
        const qreal w = 160, h = 40, spacing = 32;
        const qreal left = (width() - 2 * w - spacing) / 2;
        drawNode(p, QRectF(left, 24, w, h), tr("Selected"), m_style.selectedColor, true);
        drawNode(p, QRectF(left + w + spacing, 24, w, h), tr("Hover"), m_style.hoverColor, m_style.hoverEnabled);
    }

private:
    void drawNode(QPainter &p, const QRectF &box, const QString &label, const QColor &ring, bool showRing) {
        if (showRing) {
            // Come NodeItem::paint(): la distanza parte dal bordo esterno del nodo (tratto di 2 px).
            const qreal inset = m_style.gap + 1 + m_style.width / 2;
            p.setPen(QPen(ring, m_style.width));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(box.adjusted(-inset, -inset, inset, inset), NodeItem::kSelectionRadius, NodeItem::kSelectionRadius);
        }
        p.setPen(QPen(QColor("#d1d5db"), 2));
        p.setBrush(QColor("#ffffff"));
        p.drawRoundedRect(box, 8, 8);
        p.setPen(QColor("#202630"));
        p.drawText(box, Qt::AlignCenter, label);
        p.setPen(QColor("#6b7280"));
        p.drawText(QRectF(box.left(), box.bottom() + 12, box.width(), 20), Qt::AlignCenter,
                   showRing ? QString() : tr("(off)"));
    }
    NodeItem::SelectionStyle m_style;
};

void SettingsDialog::loadSelectionStyle() {
    const QSettings settings;
    NodeItem::SelectionStyle defaults;
    auto &style = NodeItem::selectionStyle();
    const QColor selected(settings.value("selection/color", defaults.selectedColor.name()).toString());
    const QColor hover(settings.value("selection/hoverColor", defaults.hoverColor.name()).toString());
    style.selectedColor = selected.isValid() ? selected : defaults.selectedColor;
    style.hoverColor = hover.isValid() ? hover : defaults.hoverColor;
    style.width = std::clamp(settings.value("selection/width", defaults.width).toInt(), 1, kMaxWidth);
    style.gap = std::clamp(settings.value("selection/gap", defaults.gap).toInt(), 0, kMaxGap);
    style.hoverEnabled = settings.value("selection/hover", defaults.hoverEnabled).toBool();
    const QColor text(settings.value("selection/textColor", defaults.textSelectionColor.name()).toString());
    style.textSelectionColor = text.isValid() ? text : defaults.textSelectionColor;
}

void SettingsDialog::saveSelectionStyle() {
    QSettings settings;
    const auto &style = NodeItem::selectionStyle();
    settings.setValue("selection/color", style.selectedColor.name());
    settings.setValue("selection/hoverColor", style.hoverColor.name());
    settings.setValue("selection/width", qRound(style.width));
    settings.setValue("selection/gap", qRound(style.gap));
    settings.setValue("selection/hover", style.hoverEnabled);
    settings.setValue("selection/textColor", style.textSelectionColor.name());
}

// Scheda "Scorciatoie" (FEAT-041, 2026-10-18; ridisegnata con FEAT-035): in alto ricerca e
// "Ripristina tutto", poi i comandi per area (Generale, Mappa, Outliner) in riquadri, due tasti per
// comando e ripristino per riga. Le modifiche valgono subito; un tasto gia' usato nella stessa area o
// da un comando Generale chiede con il modale comune se spostarlo.
class ShortcutsPage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ShortcutsPage)
public:
    explicit ShortcutsPage(ShortcutRegistry *registry, QWidget *parent = nullptr) : QWidget(parent), m_registry(registry) {
        setObjectName(QStringLiteral("ShortcutsPage"));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto *top = new QHBoxLayout;
        top->setContentsMargins(24, 16, 24, 8);
        top->setSpacing(8);
        auto *search = new QLineEdit;
        search->setObjectName(QStringLiteral("ShortcutSearch"));
        search->setPlaceholderText(tr("Search"));
        search->setClearButtonEnabled(true);
        connect(search, &QLineEdit::textChanged, this, &ShortcutsPage::filter);
        auto *restoreAll = new QPushButton(tr("Restore all defaults"));
        restoreAll->setObjectName(QStringLiteral("shortcutRestoreAll"));
        connect(restoreAll, &QPushButton::clicked, this, [this] {
            m_registry->resetAll();
            for (const QString &id : m_rows.keys()) refresh(id);
        });
        top->addWidget(search, 1);
        top->addWidget(restoreAll);
        layout->addLayout(top);

        auto *list = new QWidget;
        auto *rows = new QVBoxLayout(list);
        rows->setContentsMargins(24, 8, 24, 24);
        rows->setSpacing(8);
        for (const ShortcutRegistry::Area area : {ShortcutRegistry::Area::General, ShortcutRegistry::Area::Map, ShortcutRegistry::Area::Outliner, ShortcutRegistry::Area::Gantt}) {
            auto *group = sectionTitle(ShortcutRegistry::areaName(area));
            group->setObjectName(QStringLiteral("shortcutGroup"));
            QStringList ids;
            QList<QWidget *> cardRows;
            for (const ShortcutRegistry::Command &command : m_registry->commands(area)) {
                ids.append(command.id);
                cardRows.append(createRow(command));
            }
            auto *card = settingsCard(cardRows);
            rows->addWidget(group);
            rows->addWidget(card);
            rows->addSpacing(8);
            m_groups.append({group, card, ids});
        }
        rows->addStretch();
        layout->addWidget(scrollPage(list), 1);
        connect(m_registry, &ShortcutRegistry::changed, this, &ShortcutsPage::refresh);
    }

private:
    struct Row {
        QWidget *widget = nullptr;
        QKeySequenceEdit *edits[ShortcutRegistry::kMaxShortcuts] = {};
        QPushButton *restore = nullptr;
    };
    struct Group {
        QLabel *title = nullptr;
        QFrame *card = nullptr;
        QStringList ids;
    };

    QWidget *createRow(const ShortcutRegistry::Command &command) {
        auto *controls = new QWidget;
        auto *line = new QHBoxLayout(controls);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(8);
        Row entry;
        for (QKeySequenceEdit *&edit : entry.edits) {
            edit = new QKeySequenceEdit;
            edit->setMaximumSequenceLength(1);
            edit->setClearButtonEnabled(true);
            edit->setFixedWidth(160);
            edit->setAccessibleName(command.name);
            // In coda: il modale del conflitto non si apre dentro la registrazione del tasto.
            connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [this, id = command.id] { apply(id); }, Qt::QueuedConnection);
            line->addWidget(edit);
        }
        entry.restore = new QPushButton(tr("Restore"));
        entry.restore->setObjectName(QStringLiteral("shortcutRestore"));
        connect(entry.restore, &QPushButton::clicked, this, [this, id = command.id] { m_registry->reset(id); refresh(id); });
        line->addWidget(entry.restore);
        QWidget *widget = settingsRow(command.name, QString(), controls);
        widget->setObjectName(QStringLiteral("ShortcutRow_") + command.id);
        entry.widget = widget;
        m_rows.insert(command.id, entry);
        refresh(command.id);
        return widget;
    }

    // Campi della riga dal registro, senza far ripartire apply().
    void refresh(const QString &id) {
        const auto it = m_rows.constFind(id);
        if (it == m_rows.cend()) return;
        const QList<QKeySequence> shortcuts = m_registry->shortcuts(id);
        for (int i = 0; i < ShortcutRegistry::kMaxShortcuts; ++i) {
            const QSignalBlocker blocker(it->edits[i]);
            it->edits[i]->setKeySequence(shortcuts.value(i));
        }
        it->restore->setEnabled(!m_registry->isDefault(id));
    }

    void apply(const QString &id) {
        const Row row = m_rows.value(id);
        QList<QKeySequence> wanted;
        for (QKeySequenceEdit *edit : row.edits) wanted.append(edit->keySequence());
        const QList<QKeySequence> current = m_registry->shortcuts(id);
        for (const QKeySequence &shortcut : std::as_const(wanted)) {
            if (shortcut.isEmpty() || current.contains(shortcut)) continue;
            const QString other = m_registry->conflict(id, shortcut);
            if (other.isEmpty()) continue;
            ModalDialog dialog(window(), tr("Shortcut already in use"), tr("Replace"), 400);
            auto *message = new QLabel(tr("%1 is already used by \"%2\".")
                                           .arg(shortcut.toString(QKeySequence::NativeText), m_registry->command(other).name));
            message->setWordWrap(true);
            dialog.contentLayout()->addWidget(message);
            if (dialog.exec() != QDialog::Accepted) { refresh(id); return; }
            QList<QKeySequence> remaining = m_registry->shortcuts(other);
            remaining.removeAll(shortcut);
            m_registry->setShortcuts(other, remaining);  // il tasto passa al nuovo comando
        }
        m_registry->setShortcuts(id, wanted);
        refresh(id);
    }

    // Righe il cui nome o i cui tasti contengono il testo; i gruppi senza righe visibili si nascondono.
    void filter(const QString &text) {
        const QString query = text.trimmed();
        for (const Group &group : std::as_const(m_groups)) {
            bool anyVisible = false;
            for (const QString &id : group.ids) {
                QString haystack = m_registry->command(id).name;
                // Native text as shown (macOS: "⌘L") and portable text ("Ctrl+L", as in the docs).
                for (const QKeySequence &shortcut : m_registry->shortcuts(id))
                    haystack += QLatin1Char(' ') + shortcut.toString(QKeySequence::NativeText) + QLatin1Char(' ')
                              + shortcut.toString(QKeySequence::PortableText);
                const bool visible = query.isEmpty() || haystack.contains(query, Qt::CaseInsensitive);
                m_rows[id].widget->setVisible(visible);
                anyVisible = anyVisible || visible;
            }
            group.title->setVisible(anyVisible);
            group.card->setVisible(anyVisible);
            updateRowDividers(group.card);
        }
    }

    ShortcutRegistry *m_registry;
    QHash<QString, Row> m_rows;
    QList<Group> m_groups;
};

SettingsDialog::SettingsDialog(QWidget *parent, ShortcutRegistry *shortcuts)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint), m_style(NodeItem::selectionStyle()) {
    setObjectName(QStringLiteral("SettingsDialog"));
    Help::setPage(this, QStringLiteral("settings/README.md"));  // FEAT-092: F1 opens it
    // FEAT-092: modal to the main window only, so the manual window stays usable beside it.
    setWindowModality(Qt::WindowModal);
    setWindowTitle(tr("Settings"));
    setAttribute(Qt::WA_TranslucentBackground);  // angoli arrotondati disegnati in paintEvent
    setFixedSize(kDialogWidthPx, kDialogHeightPx);
    // FEAT-035: colori solo dai token (AGENTS.md "Interfaccia scura").
    setStyleSheet(UiTheme::qss(QStringLiteral(
        "QLabel { color:@text; font-size:14px; background:transparent; }"
        "QLabel#settingsTitle { font-size:16px; font-weight:600; }"
        "QLabel#settingsSection, QLabel#shortcutGroup { font-size:14px; font-weight:600; padding-top:8px; }"
        "QLabel#settingsHint { color:@textMuted; font-size:12px; }"
        "QListWidget#SettingsTabs { background:transparent; border:0; outline:0; }"
        "QListWidget#SettingsTabs::item { color:@textMuted; border-radius:8px; padding:10px 4px 8px 4px; }"
        "QListWidget#SettingsTabs::item:hover { background:@divider; color:@text; }"
        "QListWidget#SettingsTabs::item:selected { background:@surfaceRaised; color:@text; }"
        "QFrame#SettingsLine, QFrame#SettingsRowDivider { background:@divider; border:0; }"
        "QFrame#SettingsCard { background:@surfaceSunken; border:0; border-radius:8px; }"
        "QScrollArea { background:transparent; border:0; }"
        "QScrollArea > QWidget > QWidget { background:transparent; }"
        "QScrollBar:vertical { background:transparent; width:8px; margin:2px; }"
        "QScrollBar::handle:vertical { background:@surfaceRaised; border-radius:2px; min-height:32px; }"
        "QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background:transparent; height:0; }"
        "QLineEdit { background:@surfaceSunken; color:@text; border:1px solid @surfaceSunken; border-radius:4px; min-height:30px;"
        " padding:0 8px; font-size:14px; selection-background-color:@accent; selection-color:@text; }"
        "QLineEdit:focus { border-color:@accent; }"
        "QKeySequenceEdit QLineEdit { background:@surface; border-color:@surface; }"
        "QKeySequenceEdit QLineEdit:focus { border-color:@accent; }"
        "QComboBox { background:@surface; color:@text; border:1px solid @divider; border-radius:4px; min-height:30px;"
        " padding:0 32px 0 8px; font-size:14px; }"
        "QComboBox:focus { border-color:@accent; }"
        "QComboBox:hover { background:@surfaceRaised; }"
        "QComboBox::drop-down { border:0; width:24px; }"
        "QComboBox QAbstractItemView { background:@surface; color:@text; border:1px solid @divider; padding:4px;"
        " selection-background-color:@surfaceRaised; outline:0; }"
        "QPushButton { background:@surfaceRaised; color:@text; border:1px solid @divider; border-radius:4px;"
        " min-height:30px; padding:0 12px; font-size:14px; }"
        "QPushButton:hover { background:@divider; }"
        "QPushButton:pressed { background:@surfaceSunken; }"
        "QPushButton:disabled { color:@textMuted; background:transparent; }"
        "QPushButton:focus { border-color:@accent; }"
        "QToolButton#SettingsClose { background:transparent; border:0; border-radius:4px; }"
        "QToolButton#SettingsClose:hover { background:@surfaceRaised; }")));

    // Barra del titolo: titolo centrato, X a destra; trascinandola si sposta la finestra.
    auto *header = new QHBoxLayout;
    header->setContentsMargins(16, 8, 8, 0);
    auto *title = new QLabel(tr("Settings"));
    title->setObjectName(QStringLiteral("settingsTitle"));
    title->setAlignment(Qt::AlignCenter);
    auto *close = new QToolButton;
    close->setObjectName(QStringLiteral("SettingsClose"));
    close->setIcon(QIcon(QStringLiteral(":/assets/hugeicons/window-close-light.svg")));
    close->setIconSize(QSize(16, 16));
    close->setFixedSize(32, 32);
    close->setToolTip(tr("Close"));
    close->setAccessibleName(tr("Close"));
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QToolButton::clicked, this, &QDialog::reject);
    header->addSpacing(32);  // bilancia la X: il titolo resta al centro
    header->addWidget(title, 1);
    header->addWidget(close);

    // Schede in alto con icona e nome (foto 03-06 di FEAT-035).
    m_tabs = new QListWidget;
    m_tabs->setObjectName(QStringLiteral("SettingsTabs"));
    m_tabs->setViewMode(QListView::IconMode);
    m_tabs->setFlow(QListView::LeftToRight);
    m_tabs->setMovement(QListView::Static);
    m_tabs->setWrapping(false);
    m_tabs->setIconSize(QSize(24, 24));
    m_tabs->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tabs->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tabs->setFocusPolicy(Qt::TabFocus);
    auto *stack = new QStackedWidget;
    const auto addTab = [&](const QString &icon, const QString &name, QWidget *page) {
        auto *item = new QListWidgetItem(QIcon(QStringLiteral(":/assets/hugeicons/") + icon), name, m_tabs);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        return stack->addWidget(page);
    };
    addTab(QStringLiteral("settings-general.svg"), tr("General"), createGeneralPage());
    addTab(QStringLiteral("settings-appearance.svg"), tr("Appearance"), createAppearancePage());
    addTab(QStringLiteral("settings-editor.svg"), tr("Editor"), createEditorPage());
    if (shortcuts) m_shortcutsPage = addTab(QStringLiteral("settings-shortcuts.svg"), tr("Shortcuts"), new ShortcutsPage(shortcuts));
    // FEAT-035 part B: the tabs grow with their names at the chosen text size (96 x 72 at 14 px).
    const auto fitTabs = [this] {
        const QSize size(std::max(kTabSize.width(), UiTheme::fontPx(kTabSize.width())), UiTheme::heightPx(kTabSize.height()));
        m_tabs->setGridSize(size);
        for (int i = 0; i < m_tabs->count(); ++i) m_tabs->item(i)->setSizeHint(size);
        m_tabs->setFixedSize(m_tabs->count() * size.width() + 4, size.height() + 4);
    };
    fitTabs();
    connect(UiTheme::AppearanceNotifier::instance(), &UiTheme::AppearanceNotifier::changed, this, fitTabs);
    connect(m_tabs, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
    m_tabs->setCurrentRow(0);
    auto *tabs = new QHBoxLayout;
    tabs->setContentsMargins(0, 4, 0, 8);
    tabs->addStretch();
    tabs->addWidget(m_tabs);
    tabs->addStretch();

    auto *line = new QFrame;
    line->setObjectName(QStringLiteral("SettingsLine"));
    line->setFixedHeight(1);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);  // dentro il bordo disegnato
    root->setSpacing(0);
    root->addLayout(header);
    root->addLayout(tabs);
    root->addWidget(line);
    root->addWidget(stack, 1);
    setStyleValues(m_style);
    m_ready = true;
}

// Scheda "Generale": lingua (FEAT-046, dal prossimo avvio) e profilo di prestazioni (2026-10-14:
// Standard per i PC attuali, Leggero per hardware datato; valori in performance.cpp e in AGENTS.md
// "Profili di prestazioni").
QWidget *SettingsDialog::createGeneralPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 24);
    layout->setSpacing(8);

    m_language = new SettingsSelect;
    m_language->setObjectName(QStringLiteral("LanguageSelect"));
    // "Automatica" mostra quale lingua del sistema verrebbe usata; le lingue sono scritte in se' stesse.
    const QString system = Localization::nativeName(Localization::effectiveLanguage(QString()));
    m_language->addItem(tr("Automatic (system: %1)").arg(system), QString());
    for (const QString &code : Localization::availableLanguages()) m_language->addItem(Localization::nativeName(code), code);
    m_language->setCurrentIndex(qMax(0, m_language->findData(Localization::preference())));
    connect(m_language, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_ready) return;
        Localization::setPreference(languagePreference());
        emit languageChanged();
    });

    const QString detected = Performance::profileName(QThread::idealThreadCount() <= 2 ? Performance::Profile::Light
                                                                                      : Performance::Profile::Standard);
    m_performance = new SettingsSelect;
    m_performance->setObjectName(QStringLiteral("PerformanceSelect"));
    m_performance->addItem(tr("Automatic (detected: %1)").arg(detected), static_cast<int>(Performance::Mode::Automatic));
    m_performance->addItem(tr("Standard"), static_cast<int>(Performance::Mode::Standard));
    m_performance->addItem(tr("Light (older hardware)"), static_cast<int>(Performance::Mode::Light));
    m_performance->setCurrentIndex(m_performance->findData(static_cast<int>(Performance::mode())));
    connect(m_performance, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_ready) return;
        Performance::setMode(performanceMode());
        emit performanceModeChanged();
    });

    layout->addWidget(sectionTitle(tr("Maps")));
    layout->addWidget(createMentisFolderCard());
    layout->addSpacing(8);
    layout->addWidget(sectionTitle(tr("Language")));
    layout->addWidget(settingsCard({settingsRow(tr("Language"), tr("The language changes at the next start."), m_language)}));
    layout->addSpacing(8);
    layout->addWidget(sectionTitle(tr("Performance")));
    layout->addWidget(settingsCard({settingsRow(tr("Profile"),
        tr("The Light profile reduces the work on older computers: autosave every 2 seconds instead of 0.75, node images at reduced resolution, text hidden sooner when zooming out, no animations, and library previews only when saved in the file. The history kept in memory goes from 100 to 50 steps from the next start."),
        m_performance)}));
    layout->addSpacing(8);
    layout->addWidget(sectionTitle(tr("Guide")));
    QFrame *guide = createGuideCard();
    layout->addWidget(guide);
    layout->addSpacing(8);
    layout->addWidget(sectionTitle(tr("Agents (MCP)")));
    QFrame *agents = createAgentsCard();
    layout->addWidget(agents);
    layout->addSpacing(8);
    layout->addWidget(sectionTitle(tr("Updates")));
    QFrame *updates = createUpdatesCard();
    layout->addWidget(updates);
    layout->addStretch();
    auto *defaults = defaultsButton();
    connect(defaults, &QPushButton::clicked, this, [this, agents, updates, guide] {
        if (auto *tipsSwitch = guide->findChild<ToggleSwitch *>(QStringLiteral("TipsSwitch")))
            tipsSwitch->setChecked(true);  // FEAT-093: on by default
        m_performance->setCurrentIndex(0);  // Automatico
        m_language->setCurrentIndex(0);     // lingua del sistema
        if (auto *agentsSwitch = agents->findChild<ToggleSwitch *>(QStringLiteral("AgentsSwitch")))
            agentsSwitch->setChecked(false);  // FEAT-045: off by default
        if (auto *updatesSwitch = updates->findChild<ToggleSwitch *>(QStringLiteral("UpdatesSwitch")))
            updatesSwitch->setChecked(true);  // FEAT-090: on by default
    });
    layout->addWidget(defaults, 0, Qt::AlignLeft);
    return scrollPage(page);
}

// FEAT-093: the mascot's tips at the first use of an area, and the tour again. Saved at once.
QFrame *SettingsDialog::createGuideCard() {
    auto *tips = new ToggleSwitch;
    tips->setObjectName(QStringLiteral("TipsSwitch"));
    tips->setChecked(TourSettings::tipsEnabled());
    connect(tips, &ToggleSwitch::toggled, this, [](bool on) { TourSettings::setTipsEnabled(on); });
    auto *restart = new QPushButton(tr("Restart the tour"));
    restart->setObjectName(QStringLiteral("RestartTour"));
    connect(restart, &QPushButton::clicked, this, [this] {
        emit tourRequested();
        accept();
    });
    return settingsCard({
        settingsRow(tr("Show tips"), tr("The first time you open the outliner, the Gantt or Pitch, the mascot explains it in a few words."),
                    tips),
        settingsRow(tr("Guided tour"), tr("Two minutes on a practice map: topics, colours, the outliner and saving."), restart)});
}

// FEAT-105 stage 3 (decision 5): where the maps are kept. "Defaults" leaves it alone: going back to
// the default folder would ask about moving the maps, so it is chosen here only.
QFrame *SettingsDialog::createMentisFolderCard() {
    auto *change = new QPushButton(tr("Change…"));
    change->setObjectName(QStringLiteral("MentisFolderChange"));
    connect(change, &QPushButton::clicked, this, &SettingsDialog::changeMentisFolder);
    QWidget *row = settingsRow(tr("Mentis folder"), QDir::toNativeSeparators(MapFolder::current().path()), change);
    m_mentisFolderPath = row->findChild<QLabel *>(QStringLiteral("settingsHint"));
    m_mentisFolderPath->setObjectName(QStringLiteral("MentisFolderPath"));
    return settingsCard({row});
}

void SettingsDialog::changeMentisFolder() {
    const QString oldFolder = MapFolder::current().path();
    const QString chosen = chooseMentisFolder ? chooseMentisFolder(oldFolder)
                                              : QFileDialog::getExistingDirectory(this, tr("Mentis folder"), oldFolder);
    if (chosen.isEmpty()) return;
    const QString newFolder = MapFolder(chosen).path();  // absolute and clean, as the folder compares paths
    if (QFileInfo(newFolder) == QFileInfo(oldFolder)) return;
    // Checked before anything moves; no fallback, so an unusable choice is reported, not replaced.
    const MapFolder::Ready ready = MapFolder(newFolder).ensure();
    if (ready.path.isEmpty() || !ready.warning.isEmpty()) {
        ModalDialog::inform(this, tr("Error"), ready.warning);
        return;
    }
    int answer = 1;
    if (!MapFolder(oldFolder).maps().isEmpty() || !MapFolder(oldFolder).folders().isEmpty()) {
        if (askMoveMaps) {
            answer = askMoveMaps();
        } else {
            const ModalDialog::Choice choice = ModalDialog::ask(this, tr("Mentis folder"), tr("Move the maps to the new folder?"),
                                                                tr("Move"), tr("Keep where they are"));
            answer = choice == ModalDialog::Choice::Primary ? 1 : choice == ModalDialog::Choice::Secondary ? 0 : -1;
        }
    }
    if (answer < 0) return;
    MapFolder::setConfiguredPath(newFolder);
    m_mentisFolderPath->setText(QDir::toNativeSeparators(MapFolder::current().path()));
    emit mentisFolderChanged(oldFolder, answer == 1);
}

// FEAT-090: the daily check at startup and a check on request. Saved at once, like the agents' card.
QFrame *SettingsDialog::createUpdatesCard() {
    auto *enabled = new ToggleSwitch;
    enabled->setObjectName(QStringLiteral("UpdatesSwitch"));
    enabled->setChecked(Updates::Settings::load().checkAtStartup);
    connect(enabled, &ToggleSwitch::toggled, this, [](bool on) {
        Updates::Settings settings = Updates::Settings::load();
        settings.checkAtStartup = on;
        settings.save();
    });
    auto *checkNow = new QPushButton(tr("Check now"));
    checkNow->setObjectName(QStringLiteral("UpdatesCheckNow"));
    connect(checkNow, &QPushButton::clicked, this, &SettingsDialog::checkForUpdatesRequested);
    // The description under the version: the last answer from GitHub (user, 2026-11-06). A space
    // keeps settingsRow's label, filled at once.
    QWidget *version = settingsRow(tr("Version %1").arg(Updates::currentVersion()), QStringLiteral(" "), checkNow);
    m_lastUpdateCheck = version->findChild<QLabel *>(QStringLiteral("settingsHint"));
    m_lastUpdateCheck->setObjectName(QStringLiteral("UpdatesLastCheck"));
    refreshLastUpdateCheck();
    return settingsCard({
        settingsRow(tr("Check for updates at startup"),
                    tr("Once a day Mentis asks GitHub whether a new version was released."), enabled),
        version});
}

void SettingsDialog::refreshLastUpdateCheck() {
    if (!m_lastUpdateCheck) return;
    const QDateTime last = Updates::Settings::load().lastCheck;
    m_lastUpdateCheck->setText(last.isValid() ? tr("Last check: %1").arg(QLocale().toString(last, QLocale::ShortFormat))
                                              : tr("Not checked yet"));
}

// FEAT-045: the lines an agent's app needs to start this same program with --mcp, the same for every
// MCP app.
static void copyAgentConfiguration() {
    const QJsonObject server{{"command", QDir::toNativeSeparators(QCoreApplication::applicationFilePath())},
                             {"args", QJsonArray{QStringLiteral("--mcp")}}};
    const QJsonObject configuration{{"mcpServers", QJsonObject{{"mentis", server}}}};
    QGuiApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(configuration).toJson(QJsonDocument::Indented)));
}

// FEAT-045: agents (Claude, Codex and other MCP clients) work on maps through "Mentis --mcp", only when
// turned on (no allowed folders, user's decision 2026-11-06). Saved at once: the server reads it at
// every call. Turning it on also adds Mentis to the agents' apps of this computer (McpClients); when
// none is found or one fails, the configuration is copied at once for the user to paste into their
// app, whatever agent it is (user's decision 2026-11-06).
QFrame *SettingsDialog::createAgentsCard() {
    auto *enabled = new ToggleSwitch;
    enabled->setObjectName(QStringLiteral("AgentsSwitch"));
    enabled->setChecked(McpMapTools::Access::load().enabled);
    auto *connectApps = new QPushButton(tr("Add again"));
    connectApps->setObjectName(QStringLiteral("AgentsConnectApps"));
    connectApps->setEnabled(enabled->isChecked());
    QWidget *appsRow = settingsRow(tr("Agent apps"),
                                   tr("Turning agents on adds Mentis to Claude Desktop, Claude Code and Codex, when they are on "
                                      "this computer."),
                                   connectApps);
    auto *appsStatus = appsRow->findChild<QLabel *>(QStringLiteral("settingsHint"));
    const auto connectAll = [this, appsStatus] {
        QApplication::setOverrideCursor(Qt::WaitCursor);  // Claude Code's own command takes a moment
        const QList<McpClients::Outcome> outcomes = connectAgentApps
            ? connectAgentApps()
            : McpClients::connectAll(McpClients::Locations::system(), QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
        QApplication::restoreOverrideCursor();
        QStringList added, lines;
        for (const McpClients::Outcome &outcome : outcomes) {
            if (outcome.ok) added.append(outcome.app);
            else lines.append(tr("Not added to %1: %2").arg(outcome.app, outcome.error));
        }
        if (!added.isEmpty()) lines.prepend(tr("Mentis added to %1. Restart the app to see it.").arg(added.join(QStringLiteral(", "))));
        if (added.size() < outcomes.size() || outcomes.isEmpty()) {
            copyAgentConfiguration();
            lines.append(outcomes.isEmpty() ? tr("No agent app found on this computer. The configuration is copied: paste it into "
                                                 "your agent's MCP settings, then restart it.")
                                            : tr("The configuration is copied: paste it into the app's MCP settings, then restart it."));
        }
        if (appsStatus) appsStatus->setText(lines.join(QLatin1Char('\n')));
    };
    connect(enabled, &ToggleSwitch::toggled, this, [this, connectApps, connectAll](bool on) {
        McpMapTools::Access access = McpMapTools::Access::load();
        access.enabled = on;
        access.save();
        emit agentsChanged();  // the window opens or closes its channel (phase 2)
        connectApps->setEnabled(on);
        if (on) connectAll();  // the user's request (2026-11-06): no configuration pasted by hand
    });
    connect(connectApps, &QPushButton::clicked, this, connectAll);
    auto *copy = new QPushButton(tr("Copy configuration"));
    copy->setObjectName(QStringLiteral("AgentsCopyConfiguration"));
    QFrame *card = settingsCard({
        settingsRow(tr("Let agents work on maps"),
                    tr("Agents such as Claude or Codex create and change maps, Gantt charts and presentations through the "
                       "Model Context Protocol (MCP), starting Mentis with --mcp. Off: they can do nothing."),
                    enabled),
        appsRow,
        settingsRow(tr("Agent configuration"),
                    tr("For other agents' apps: copies the lines to add to their MCP settings."), copy)});
    connect(copy, &QPushButton::clicked, this, [copy] {
        copyAgentConfiguration();
        copy->setText(tr("Copied"));
        QTimer::singleShot(2000, copy, [copy] { copy->setText(tr("Copy configuration")); });
    });
    return card;
}

// FEAT-035 part B: "Appearance" tab, font, text size and density of the whole interface except the
// map. Every choice applies at once to the open windows (UiTheme::setAppearance).
QWidget *SettingsDialog::createAppearancePage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 24);
    layout->setSpacing(8);
    const UiTheme::Appearance current = UiTheme::appearance();

    NodeItem::loadBundledFonts();  // each font is written in itself
    m_fontFamily = new SettingsSelect;
    m_fontFamily->setObjectName(QStringLiteral("FontFamilySelect"));
    const auto addFont = [this](const QString &name, const QString &value, const QString &family) {
        m_fontFamily->addItem(name, value);
        QFont font = m_fontFamily->font();
        font.setFamilies({family});
        m_fontFamily->setItemData(m_fontFamily->count() - 1, font, Qt::FontRole);
    };
    addFont(tr("%1 (default)").arg(UiTheme::kDefaultFontFamily), QString(), UiTheme::kDefaultFontFamily);
    for (const QString &family : UiTheme::bundledFontFamilies()) addFont(family, family, family);
    addFont(tr("System font"), UiTheme::kSystemFont, QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
    m_fontFamily->setCurrentIndex(qMax(0, m_fontFamily->findData(current.fontFamily)));

    m_fontSize = new SettingsSelect;
    m_fontSize->setObjectName(QStringLiteral("FontSizeSelect"));
    for (int size = UiTheme::kMinFontSize; size <= UiTheme::kMaxFontSize; ++size) m_fontSize->addItem(tr("%1 px").arg(size), size);
    m_fontSize->setCurrentIndex(m_fontSize->findData(current.fontSize));

    m_density = new SegmentedControl({tr("Normal"), tr("Compact")});
    m_density->setObjectName(QStringLiteral("DensityControl"));
    m_density->setColors({UiTheme::color(UiTheme::Token::Surface), UiTheme::color(UiTheme::Token::Divider),
                          UiTheme::color(UiTheme::Token::SurfaceRaised), UiTheme::color(UiTheme::Token::Divider),
                          UiTheme::color(UiTheme::Token::Text), UiTheme::color(UiTheme::Token::TextMuted)});
    m_density->setCurrentIndex(current.density == UiTheme::Density::Compact ? 1 : 0);

    connect(m_fontFamily, &QComboBox::currentIndexChanged, this, &SettingsDialog::applyAppearance);
    connect(m_fontSize, &QComboBox::currentIndexChanged, this, &SettingsDialog::applyAppearance);
    connect(m_density, &SegmentedControl::segmentActivated, this, &SettingsDialog::applyAppearance);

    layout->addWidget(sectionTitle(tr("Interface")));
    layout->addWidget(settingsCard({settingsRow(tr("Font"), QString(), m_fontFamily),
                                    settingsRow(tr("Text size"), tr("The map and the exports keep their own sizes."), m_fontSize),
                                    settingsRow(tr("Density"), QString(), m_density)}));
    layout->addSpacing(8);
    // The preview is made of ordinary widgets: they change with the rest of the window.
    layout->addWidget(sectionTitle(tr("Preview")));
    auto *buttons = new QWidget;
    auto *buttonLine = new QHBoxLayout(buttons);
    buttonLine->setContentsMargins(0, 0, 0, 0);
    buttonLine->setSpacing(8);
    auto *sampleSelect = new SettingsSelect;
    sampleSelect->addItem(tr("Option"));
    buttonLine->addWidget(sampleSelect);
    buttonLine->addWidget(new QPushButton(tr("Button")));
    layout->addWidget(settingsCard({settingsRow(tr("The quick brown fox jumps over the lazy dog"), tr("A description under a row"), buttons)}));
    layout->addStretch();
    auto *defaults = defaultsButton();
    connect(defaults, &QPushButton::clicked, this, [this] {
        const QSignalBlocker family(m_fontFamily), size(m_fontSize);
        m_fontFamily->setCurrentIndex(0);
        m_fontSize->setCurrentIndex(m_fontSize->findData(UiTheme::kDefaultFontSize));
        m_density->setCurrentIndex(0);
        applyAppearance();
    });
    layout->addWidget(defaults, 0, Qt::AlignLeft);
    return scrollPage(page);
}

void SettingsDialog::applyAppearance() {
    if (!m_ready) return;
    UiTheme::Appearance value;
    value.fontFamily = m_fontFamily->currentData().toString();
    value.fontSize = m_fontSize->currentData().toInt();
    value.density = m_density->currentIndex() == 1 ? UiTheme::Density::Compact : UiTheme::Density::Normal;
    UiTheme::setAppearance(value);
}

// Scheda "Editor": bordo di selezione dei nodi e colore del testo selezionato (2026-10-13).
QWidget *SettingsDialog::createEditorPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 24);
    layout->setSpacing(8);

    const auto swatch = [] {
        auto *button = new QToolButton;
        button->setFixedSize(48, 24);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    m_selectedColor = swatch();
    m_hoverColor = swatch();
    m_textColor = swatch();
    using Style = NodeItem::SelectionStyle;
    connect(m_selectedColor, &QToolButton::clicked, this, [this] { chooseColor(&Style::selectedColor, m_selectedColor); });
    connect(m_hoverColor, &QToolButton::clicked, this, [this] { chooseColor(&Style::hoverColor, m_hoverColor); });
    connect(m_textColor, &QToolButton::clicked, this, [this] { chooseColor(&Style::textSelectionColor, m_textColor); });
    const auto pixelSelect = [](int from, int to) {
        auto *select = new SettingsSelect;
        select->setMinimumWidth(96);
        for (int px = from; px <= to; ++px) select->addItem(tr("%1 px").arg(px), px);
        return select;
    };
    m_width = pixelSelect(1, kMaxWidth);
    m_width->setObjectName(QStringLiteral("SelectionWidth"));
    m_gap = pixelSelect(0, kMaxGap);
    m_gap->setObjectName(QStringLiteral("SelectionGap"));
    m_hoverEnabled = new ToggleSwitch;
    m_hoverEnabled->setObjectName(QStringLiteral("HoverBorderSwitch"));
    connect(m_width, &QComboBox::currentIndexChanged, this, [this] { m_style.width = m_width->currentData().toInt(); applyStyle(); });
    connect(m_gap, &QComboBox::currentIndexChanged, this, [this] { m_style.gap = m_gap->currentData().toInt(); applyStyle(); });
    connect(m_hoverEnabled, &ToggleSwitch::toggled, this, [this](bool on) {
        m_style.hoverEnabled = on;
        m_hoverColor->setEnabled(on);
        applyStyle();
    });

    layout->addWidget(sectionTitle(tr("Selection")));
    auto *hint = new QLabel(tr("Border around the selected nodes on the map and the node under the mouse; background of the selected text while you type, on the map and in the outliner."));
    hint->setObjectName(QStringLiteral("settingsHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addWidget(settingsCard({
        settingsRow(tr("Selection color"), QString(), m_selectedColor),
        settingsRow(tr("Show the border when the mouse is over a node"), QString(), m_hoverEnabled),
        settingsRow(tr("Hover color"), QString(), m_hoverColor),
        settingsRow(tr("Thickness"), QString(), m_width),
        settingsRow(tr("Distance from node"), QString(), m_gap),
        // Colore del testo selezionato mentre si scrive (richiesta 2026-10-13): vale per i nodi della
        // mappa, per le voci e per il titolo principale dell'outliner.
        settingsRow(tr("Selected text color"), QString(), m_textColor),
    }));
    m_preview = new SelectionPreview;
    layout->addSpacing(8);
    layout->addWidget(m_preview);

    // FEAT-036: tema colore e variante delle mappe nuove (un foglio nuovo prende quelli del foglio
    // attivo). Salvati subito, come ogni scelta della finestra.
    auto *themeSelect = new PaletteSelect;
    themeSelect->setObjectName(QStringLiteral("DefaultPaletteSelect"));
    themeSelect->setMinimumWidth(200);
    auto *variants = new VariantPicker(true);
    variants->setObjectName(QStringLiteral("DefaultVariantPicker"));
    const auto showTheme = [themeSelect, variants] {
        const CustomPalettes::DefaultTheme theme = CustomPalettes::defaultTheme();
        const bool custom = theme.palette.group == NodeStyle::PaletteGroup::Personalizzata;
        themeSelect->setPalette(theme.palette.id, theme.palette.colors, custom ? theme.palette.name : QString());
        variants->setTheme(theme.palette.id, theme.palette.colors, theme.variant);
    };
    showTheme();
    connect(themeSelect, &PaletteSelect::paletteChosen, this, [showTheme](const QString &id) {
        CustomPalettes::setDefaultTheme(id, -1);  // come sulla mappa: nuova palette, sfondo bianco
        showTheme();
    });
    connect(variants, &VariantPicker::variantChosen, this, [showTheme](int variant) {
        CustomPalettes::setDefaultTheme(CustomPalettes::defaultTheme().palette.id, variant);
        showTheme();
    });
    layout->addSpacing(8);
    layout->addWidget(sectionTitle(tr("Default color theme")));
    auto *themeHint = new QLabel(tr("New maps start with this color theme; a new sheet takes the theme of the active sheet."));
    themeHint->setObjectName(QStringLiteral("settingsHint"));
    themeHint->setWordWrap(true);
    layout->addWidget(themeHint);
    auto *variantsBox = new QWidget;
    auto *variantsLayout = new QHBoxLayout(variantsBox);
    variantsLayout->setContentsMargins(0, 0, 0, 0);
    variantsLayout->addWidget(variants);
    layout->addWidget(settingsCard({settingsRow(tr("Color theme"), QString(), themeSelect),
                                    settingsRow(tr("Background"), tr("White or one of the six colors of the theme."), variantsBox)}));
    // FEAT-068: scorciatoie Markdown mentre si scrive (salvate subito, come il tema).
    auto *markdown = new ToggleSwitch;
    markdown->setObjectName(QStringLiteral("MarkdownShortcutsSwitch"));
    markdown->setChecked(MarkdownShortcuts::enabled());
    markdown->setAccessibleName(tr("Markdown shortcuts"));
    connect(markdown, &ToggleSwitch::toggled, this, [](bool on) { MarkdownShortcuts::setEnabled(on); });
    layout->addSpacing(8);
    layout->addWidget(sectionTitle(tr("Writing")));
    // FEAT-044: spell check of the text being edited, from the next edit.
    auto *spelling = new ToggleSwitch(page);  // page owns it also where the row is not shown
    spelling->setObjectName(QStringLiteral("SpellCheckSwitch"));
    spelling->setChecked(SpellCheck::enabled());
    spelling->setAccessibleName(tr("Check spelling"));
    connect(spelling, &ToggleSwitch::toggled, this, [](bool on) { SpellCheck::setEnabled(on); });
    QList<QWidget *> writingRows{settingsRow(tr("Markdown shortcuts"),
        tr("**bold**, *italic*, ~~strikethrough~~, ==highlight== and - [ ] become formatting while you type; in notes also lists, ## heading, [link](address) and `code`."),
        markdown)};
#ifdef Q_OS_WIN
    // Only Windows has a spell checker so far (FEAT-044 stage A).
    writingRows.append(settingsRow(tr("Check spelling"),
        tr("Underlines misspelled words in the text being edited, with the dictionary of the system."),
        spelling));
#else
    spelling->hide();
#endif
    layout->addWidget(settingsCard(writingRows));
    layout->addStretch();
    auto *defaults = defaultsButton();
    connect(defaults, &QPushButton::clicked, this, [this, showTheme, markdown, spelling] {
        setStyleValues(NodeItem::SelectionStyle{});
        applyStyle();
        CustomPalettes::setDefaultTheme(NodeStyle::kDefaultPaletteId, -1);  // Alba con sfondo bianco
        showTheme();
        markdown->setChecked(true);  // FEAT-068: acceso di base
        spelling->setChecked(false);  // FEAT-044: off by default
    });
    layout->addWidget(defaults, 0, Qt::AlignLeft);
    return scrollPage(page);
}

void SettingsDialog::showPage(int index) {
    if (m_tabs && index >= 0 && index < m_tabs->count()) m_tabs->setCurrentRow(index);
}

void SettingsDialog::showShortcutsPage() {
    if (m_tabs && m_shortcutsPage >= 0) m_tabs->setCurrentRow(m_shortcutsPage);
}

QString SettingsDialog::languagePreference() const {
    return m_language ? m_language->currentData().toString() : Localization::preference();
}

Performance::Mode SettingsDialog::performanceMode() const {
    return m_performance ? static_cast<Performance::Mode>(m_performance->currentData().toInt()) : Performance::mode();
}

void SettingsDialog::setStyleValues(const NodeItem::SelectionStyle &style) {
    m_style = style;
    // Senza segnali: chi chiama decide se applicare (applyStyle).
    const QSignalBlocker width(m_width), gap(m_gap), hover(m_hoverEnabled);
    m_width->setCurrentIndex(m_width->findData(qRound(style.width)));
    m_gap->setCurrentIndex(m_gap->findData(qRound(style.gap)));
    m_hoverEnabled->setChecked(style.hoverEnabled);
    m_hoverColor->setEnabled(style.hoverEnabled);
    paintSwatch(m_selectedColor, style.selectedColor);
    paintSwatch(m_hoverColor, style.hoverColor);
    paintSwatch(m_textColor, style.textSelectionColor);
    m_preview->setSelectionStyle(m_style);
}

void SettingsDialog::applyStyle() {
    m_preview->setSelectionStyle(m_style);
    if (!m_ready) return;
    NodeItem::selectionStyle() = m_style;
    saveSelectionStyle();
    emit selectionStyleChanged();
}

// FEAT-035: selettore colori comune (FEAT-019) al posto di QColorDialog; ogni scelta vale subito.
void SettingsDialog::chooseColor(QColor NodeItem::SelectionStyle::*target, QToolButton *button) {
    const QRect anchor(button->mapToGlobal(QPoint(0, 0)), button->size());
    ColorPicker::exec(anchor, ColorPicker::Placement::LeftOf, {m_style.*target, {}, false},
                      [this, target, button](const QColor &color) {
                          m_style.*target = color;
                          paintSwatch(button, color);
                          applyStyle();
                      });
}

void SettingsDialog::paintSwatch(QToolButton *button, const QColor &color) {
    button->setToolTip(color.name());
    button->setStyleSheet(UiTheme::qss(QStringLiteral("QToolButton{background:%1;border:1px solid @divider;border-radius:4px;}"
                                                      "QToolButton:disabled{background:@surfaceRaised;}"
                                                      "QToolButton:focus{border-color:@accent;}"))
                              .arg(color.name()));
}

void SettingsDialog::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(UiTheme::color(UiTheme::Token::Divider), 1));
    painter.setBrush(UiTheme::color(UiTheme::Token::Surface));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kRadiusPx, kRadiusPx);
}

void SettingsDialog::mousePressEvent(QMouseEvent *event) {
    const bool onHeader = event->button() == Qt::LeftButton && event->position().y() < kHeaderHeightPx;
    m_dragOffset = onHeader ? event->globalPosition().toPoint() - frameGeometry().topLeft() : QPoint();
    QDialog::mousePressEvent(event);
}

void SettingsDialog::mouseMoveEvent(QMouseEvent *event) {
    if ((event->buttons() & Qt::LeftButton) && !m_dragOffset.isNull()) move(event->globalPosition().toPoint() - m_dragOffset);
    QDialog::mouseMoveEvent(event);
}
