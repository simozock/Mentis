#pragma once

#include <QDialog>
#include <QPoint>

#include <functional>

#include "mcpclients.h"
#include "nodeitem.h"
#include "performance.h"

class QComboBox;
class QFrame;
class QLabel;
class QListWidget;
class QToolButton;
class SegmentedControl;
class SelectionPreview;
class ShortcutRegistry;
class ToggleSwitch;

// Finestra "Impostazioni" (2026-10-13; scura a schede da FEAT-035, AGENTS.md "Interfaccia scura").
// Senza cornice di sistema: titolo centrato, X a destra, schede in alto con icona (Generale, Aspetto
// di FEAT-035 parte B, Editor, Scorciatoie se c'e' il registro). Nessun OK/Annulla: ogni scelta vale
// subito (QSettings e segnali); X ed Esc chiudono. La lingua vale dal prossimo avvio (FEAT-046).
class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    // shortcuts: registro delle scorciatoie (FEAT-041); senza registro la scheda Scorciatoie non c'e'.
    explicit SettingsDialog(QWidget *parent = nullptr, ShortcutRegistry *shortcuts = nullptr);
    // Mostra la scheda Scorciatoie (da "Scorciatoie…" del menu e della Home).
    void showShortcutsPage();
    // Legge da QSettings lo stile del bordo e lo copia in NodeItem::selectionStyle().
    // Va chiamata all'avvio, prima di disegnare la mappa.
    static void loadSelectionStyle();
    // Salva in QSettings lo stile attuale di NodeItem::selectionStyle().
    static void saveSelectionStyle();
    // Valori attuali della finestra (gia' applicati).
    NodeItem::SelectionStyle selectionStyle() const { return m_style; }
    Performance::Mode performanceMode() const;
    // Lingua scelta (FEAT-046): vuota = lingua del sistema, "it", "en".
    QString languagePreference() const;
    void showPage(int index);  // FEAT-045: 0 General, 1 Appearance, 2 Editor, 3 Shortcuts (when present)
    // FEAT-045: adds Mentis to the agents' apps (tests set it; empty = the apps of this computer).
    std::function<QList<McpClients::Outcome>()> connectAgentApps;
    // FEAT-090: the date and time of the last update check, under the version; after "Check now".
    void refreshLastUpdateCheck();

signals:
    // Emessi dopo aver salvato la scelta; chi apre la finestra ridisegna cio' che serve.
    void selectionStyleChanged();
    void performanceModeChanged();
    void languageChanged();
    void agentsChanged();  // FEAT-045: the Agents (MCP) switch
    void checkForUpdatesRequested();  // FEAT-090: "Check now"; the window answers over this dialog
    void tourRequested();             // FEAT-093: "Restart the tour"; the dialog closes first

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QWidget *createGeneralPage();
    QFrame *createGuideCard();  // FEAT-093
    QWidget *createAppearancePage();
    QWidget *createEditorPage();
    QFrame *createAgentsCard();  // FEAT-045: Settings > General > Agents (MCP)
    QFrame *createUpdatesCard();  // FEAT-090: Settings > General > Updates
    void applyAppearance();  // FEAT-035 part B: the three controls -> UiTheme::setAppearance
    void setStyleValues(const NodeItem::SelectionStyle &style);  // aggiorna i controlli
    void applyStyle();                                          // salva e avvisa
    void chooseColor(QColor NodeItem::SelectionStyle::*target, QToolButton *button);
    static void paintSwatch(QToolButton *button, const QColor &color);

    NodeItem::SelectionStyle m_style;
    QLabel *m_lastUpdateCheck = nullptr;  // FEAT-090
    QToolButton *m_selectedColor = nullptr;
    QToolButton *m_hoverColor = nullptr;
    QToolButton *m_textColor = nullptr;  // sfondo del testo selezionato (2026-10-13)
    QComboBox *m_width = nullptr;
    QComboBox *m_gap = nullptr;
    ToggleSwitch *m_hoverEnabled = nullptr;
    SelectionPreview *m_preview = nullptr;
    QComboBox *m_performance = nullptr;
    QComboBox *m_language = nullptr;
    QComboBox *m_fontFamily = nullptr;
    QComboBox *m_fontSize = nullptr;
    SegmentedControl *m_density = nullptr;
    QListWidget *m_tabs = nullptr;
    int m_shortcutsPage = -1;
    bool m_ready = false;  // falso durante la costruzione: i controlli non salvano
    QPoint m_dragOffset;   // trascinamento dalla barra del titolo
};
