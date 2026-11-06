#pragma once

// FEAT-019 (docs/features/FEAT-019-fondamenta-stile-nodo.md): componenti comuni dello stile.
// ColorPicker e' il selettore colori unico dell'app: pagina Stile del pannello e barra di
// formattazione del testo usano questo, non copie proprie.

#include "nodestyle.h"
#include "numberfield.h"
#include "boundary.h"
#include "relationship.h"

#include <QColor>
#include <QLineEdit>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QWidget>

#include <functional>

class QComboBox;
class QStackedWidget;
class SegmentedControl;
class QEventLoop;
class QLabel;
class QVBoxLayout;
struct MindNode;

// Testo chiaro negli elenchi delle select scure: con Fusion su Windows le voci restavano scure
// sullo sfondo scuro (AGENTS.md, "Formattazione della selezione nella mappa"). Usato dalla barra
// di formattazione e dalla pagina Stile.
class LightComboItemDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

// Freccia Hugeicons chiara a destra di una select scura (col foglio di stile Qt non disegna piu'
// quella predefinita). Da chiamare in paintEvent dopo QComboBox::paintEvent.
void paintDarkSelectChevron(QWidget *select);
// Full-width 1 px line between the side panel sections (FEAT-072 B2).
QWidget *panelDivider(QWidget *parent);
// Select scura delle pagine del pannello (freccia chiara, elenco scuro), con il suo foglio di stile:
// si usa anche fuori dalla pagina Stile (FEAT-026: struttura del foglio e del ramo).
QComboBox *makeDarkSelect(QWidget *parent = nullptr);
// FEAT-023: nome dello spessore NodeStyle::kNamedBranchWidths[index] (Extra sottile ... Extra spesso).
QString branchWidthName(int index);

// FEAT-080: the Style page's icon selects for a select of another panel (the Gantt's). An item has
// a small picture for the field and a large one for the grid menu (FEAT-038), its name as tooltip;
// draw paints it in the box given. The grid menu opens in the colours given.
struct GridMenuColors {
    QColor surface, border, text, hover;
};
void addIconSelectItem(QComboBox *select, int value, const QString &name, const std::function<void(class QPainter &, const QRectF &)> &draw);
// noneIndex: the item shown as a wide tile at the bottom ("None"), -1 = none.
void openIconGridMenu(QComboBox *select, int columns, const GridMenuColors &colors, int noneIndex = -1);
// FEAT-026: select a sola icona (64 px) dei tipi di mappa, con il menu a griglia a due colonne
// (miniatura e nome). items = (chiave del tipo, nome); chiave vuota = "Come il genitore".
QComboBox *makeStructureSelect(const QList<std::pair<QString, QString>> &items, QWidget *parent = nullptr);
// One more item of a structure select, at index (FEAT-028: Mind map back outside a table).
void insertStructureItem(QComboBox *select, int index, const QString &key, const QString &name);

class ColorPicker final : public QWidget {
    Q_OBJECT
public:
    // Griglia di 45 campioni, 5 righe da 9, misurati sui pixel del riferimento
    // docs/features/attachments/FEAT-019/03-selettore-colori-griglia.png.
    static const QStringList &gridColors();
    static constexpr int kGridColumns = 9;
    static constexpr int kSwatchPx = 24;   // campioni della griglia, del tema e dei recenti
    static constexpr int kGapPx = 4;       // griglia di AGENTS.md
    static constexpr int kPaddingPx = 12;
    static constexpr int kMaxRecentColors = 9;

    struct Options {
        QColor current;               // colore di partenza (non valido = nessuno, valori misti)
        QStringList themeColors;      // "Tema attuale": colori della palette del foglio
        bool allowOpacity = true;     // falso: campo opacita' disattivato, colori sempre opachi
    };

    // Dove si apre rispetto al controllo che lo apre (anchor, coordinate globali), a 8 px:
    // sopra e allineato a destra (barra di formattazione), oppure a sinistra (pannello laterale).
    // Se non c'e' spazio si apre dal lato opposto; resta sempre dentro lo schermo.
    enum class Placement { Above, LeftOf };

    // Apre il selettore e aspetta che si chiuda, come QMenu::exec. onChosen riceve ogni scelta
    // confermata: clic su un campione o Invio nel codice (che chiudono il selettore), opacita'
    // confermata e rilascio nel pannello del colore libero (che lo lasciano aperto). Esc o un clic
    // fuori chiudono senza scelte.
    static void exec(const QRect &anchor, Placement placement, const Options &options,
                     const std::function<void(const QColor &)> &onChosen);

    // Colori scelti di recente (impostazioni dell'app), il piu' recente per primo.
    static QStringList recentColors();
    static void rememberRecentColor(const QColor &color);

    // Esposti per i test: il selettore aperto, se c'e' (e' il popup attivo dell'applicazione).
    static ColorPicker *openPicker();
    QLineEdit *hexField() const { return m_hex; }
    QLineEdit *opacityField() const { return m_opacity; }
    QToolButton *customColorButton() const { return m_custom; }
    QWidget *customColorPanel() const { return m_customPanel; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    ColorPicker(const Options &options, std::function<void(const QColor &)> onChosen);
    QWidget *swatchRow(const QStringList &colors, int columns);
    QWidget *section(const QString &title, QWidget *content);
    void choose(QColor color, bool close);
    void showColor(const QColor &color);
    QColor withOpacity(QColor color) const;
    int opacityPercent() const;
    void setOpacityPercent(int percent);

    Options m_options;
    std::function<void(const QColor &)> m_onChosen;
    QColor m_color;
    QLineEdit *m_hex = nullptr;
    QLineEdit *m_opacity = nullptr;  // opacita' 0-100 %, vedi opacityPercent()
    QWidget *m_preview = nullptr;
    QToolButton *m_custom = nullptr;
    QWidget *m_customPanel = nullptr;
    QEventLoop *m_loop = nullptr;
};

// FEAT-019 parte C: campo colore della pagina Stile. Mostra il campione e il codice esadecimale,
// grigio se il colore e' ricavato dalla cascata e chiaro se e' scelto a mano su tutti i nodi;
// con valori diversi mostra "—". Il clic apre ColorPicker a sinistra del campo.
class ColorField final : public QToolButton {
    Q_OBJECT
public:
    explicit ColorField(QWidget *parent = nullptr);
    void setReading(const NodeStyle::Reading &reading);
    bool isMixed() const { return m_mixed; }
    bool isManual() const { return m_manual; }
    QColor color() const { return m_color; }
    std::function<QStringList()> themeColors;  // "Tema attuale" del selettore
    bool allowOpacity = true;  // false: opaque colours only (Gantt bars)
    // Border, and on hover; invalid = the side panel's (the Gantt panel gives its fields' colours).
    QColor borderColor, hoverBorderColor;
    // Mixed values: the field's fill and dash; invalid = the side panel's (dark).
    QColor mixedFill, mixedText;
    void openPicker();

signals:
    void colorChosen(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor m_color;
    bool m_mixed = false;
    bool m_manual = false;
};

// FEAT-019 parte C: pagina Stile del pannello laterale. Sezioni richiudibili Forma (riempimento e
// bordo, solo del nodo), Testo (colore, solo del nodo) e Ramo (colore del ramo a cascata, stile
// delle linee verso i discendenti a cascata, spessore delle linee verso i figli diretti; regole di
// BUG-005/006/007). Legge i valori con NodeStyle::read e chiede le modifiche con styleChosen: non
// scrive mai nel modello da sola.
// A size in px (border, width, image) as the app's NumberField: the common value or "—", grey when
// derived and light when chosen by hand; the unit is in the row's label. valueChosen only when
// something changes; FEAT-021: with different values −, + and the arrows emit valueStepped (each
// node changes from its own value) when stepsEachNode is set.
class PixelField final : public NumberField {
    Q_OBJECT
public:
    PixelField(qreal minimum, qreal maximum, const QString &toolTip, QWidget *parent = nullptr);
    void setReading(const NodeStyle::Reading &reading);
    void setStepsEachNode(bool eachNode) { setStepsEachItem(eachNode); }
};

// FEAT-024: fields of a numbering (format select, Format, Separator, Separator at the end, First number,
// Type, Levels):
// on the Style page for the selected topics, on the Map page for the central topic.
class ToggleSwitch;

class NumberingFields final : public QWidget {
    Q_OBJECT
public:
    explicit NumberingFields(QWidget *parent = nullptr);
    // Shows the topics' numbering (the first one's own); no topics = disabled. Emits nothing.
    void setNodes(const QList<const MindNode *> &nodes);
    QComboBox *presetField() const { return m_preset; }
    QLineEdit *formatField() const { return m_format; }
    QComboBox *separatorField() const { return m_separator; }
    ToggleSwitch *trailingSeparatorField() const { return m_trailingSeparator; }
    QLineEdit *startField() const { return m_start; }
    QComboBox *typeField() const { return m_type; }
    QComboBox *levelsField() const { return m_levels; }

signals:
    void numberingChosen(const BranchNumbering &numbering);  // empty format = none

private:
    QComboBox *m_preset = nullptr;
    QLineEdit *m_format = nullptr;
    QComboBox *m_separator = nullptr;
    ToggleSwitch *m_trailingSeparator = nullptr;
    QLineEdit *m_start = nullptr;
    QComboBox *m_type = nullptr;
    QComboBox *m_levels = nullptr;
    QWidget *m_details = nullptr;  // the fields under the select, hidden with None
    BranchNumbering m_numbering;   // shown: the first topic's
};

class StylePage final : public QWidget {
    Q_OBJECT
public:
    explicit StylePage(QWidget *parent = nullptr);
    // Mostra i valori dei nodi; nessun nodo = controlli disattivati. Non emette styleChosen.
    // FEAT-021: widthsPx = larghezza attuale del riquadro di ogni nodo, nello stesso ordine (la
    // misura e' della scena); vuoto = campo della larghezza vuoto e disattivato.
    void setNodes(const QList<const MindNode *> &nodes, const QStringList &themeColors, const QList<qreal> &widthsPx = {});

    ColorField *fillField() const { return m_fill; }
    ColorField *borderField() const { return m_border; }
    ColorField *textField() const { return m_text; }
    ColorField *branchColorField() const { return m_branchColor; }
    QComboBox *branchStyleField() const { return m_branchStyle; }
    QComboBox *branchShapeField() const { return m_branchShape; }
    QComboBox *branchStartField() const { return m_branchStart; }
    QComboBox *branchEndField() const { return m_branchEnd; }
    QComboBox *branchWidthField() const { return m_branchWidth; }
    // FEAT-020 parte A.
    QComboBox *presetField() const { return m_preset; }
    QComboBox *fillStyleField() const { return m_fillStyle; }
    QComboBox *shapeField() const { return m_shape; }  // FEAT-020 parte B
    QComboBox *borderStyleField() const { return m_borderStyle; }
    QLineEdit *borderWidthField() const { return m_borderWidth; }
    // FEAT-022: sezione Testo.
    QComboBox *fontFamilyField() const { return m_fontFamily; }
    QComboBox *fontSizeField() const { return m_fontSize; }
    QComboBox *fontWeightField() const { return m_fontWeight; }
    QComboBox *textCaseField() const { return m_textCase; }
    QToolButton *boldButton() const { return m_bold; }
    QToolButton *italicButton() const { return m_italic; }
    QToolButton *strikeOutButton() const { return m_strikeOut; }
    QToolButton *underlineButton() const { return m_underline; }
    QToolButton *alignButton(TextAlign align) const { return m_align.value(static_cast<int>(align)); }
    // FEAT-021: riga Larghezza.
    QLineEdit *widthField() const { return m_width; }
    QToolButton *fitButton() const { return m_fit; }
    QToolButton *styleActionButton() const { return m_styleAction; }  // FEAT-072 B15
    QToolButton *childrenStyleButton() const { return m_childrenStyle; }
    QToolButton *resetStyleButton() const { return m_resetStyle; }
    // FEAT-024: Numbering section.
    QComboBox *numberingField() const { return m_numbering->presetField(); }
    QLineEdit *numberingFormatField() const { return m_numbering->formatField(); }
    QComboBox *numberingSeparatorField() const { return m_numbering->separatorField(); }
    ToggleSwitch *numberingTrailingSeparatorField() const { return m_numbering->trailingSeparatorField(); }
    QLineEdit *numberingStartField() const { return m_numbering->startField(); }
    QComboBox *numberingTypeField() const { return m_numbering->typeField(); }
    QComboBox *numberingLevelsField() const { return m_numbering->levelsField(); }

signals:
    void styleChosen(StyleProperty property, const QVariant &value);
    // FEAT-021: larghezza scritta (px, uguale per tutti: ognuno si ferma al suo minimo), cambiata
    // con le frecce quando i nodi hanno larghezze diverse (ognuno di deltaPx dalla sua), "Adatta".
    void widthChosen(qreal px);
    void widthStepped(qreal deltaPx);
    void widthReset();
    // FEAT-026 fase C: tipo di mappa del ramo scelto (un solo nodo non centrale; tipo vuoto = come il genitore).
    void structureChosen(const QString &typeKey, const QString &directionKey);
    // FEAT-072 B15: bottom button, Apply to related (not on the central topic).
    void styleActionRequested();
    // Apply to children: the style of the selected topics to all their descendants.
    void childrenStyleRequested();
    // Reset style: the manual style of the selected topics removed.
    void resetStyleRequested();

private:
    // header: controllo a destra del titolo (come la select della Struttura), facoltativo.
    QWidget *section(const QString &title, QWidget *content, QWidget *header = nullptr);
    QWidget *row(const QString &label, const QList<QWidget *> &fields);
    // Block preceded by a full-width line, like the panel sections (FEAT-072 B2).
    QWidget *divided(QWidget *content);
    // Select di un valore intero (stile predefinito, tipi di riempimento e bordo, stile del ramo).
    QComboBox *enumSelect(StyleProperty property, int width);
    void showEnum(QComboBox *select, const NodeStyle::Reading &reading);
    // Anteprime degli stili per il colore del ramo e il livello del (primo) nodo selezionato e per
    // i colori del tema.
    void updatePresetPreviews(const QColor &color, int depth, const QStringList &palette);
    // FEAT-022: sezione Testo e lettura dei valori dei nodi.
    QWidget *textSection();
    void showText(const QList<const MindNode *> &nodes);
    void fillWeights(const QString &family, int current);

    class StructurePicker *m_structure = nullptr;  // FEAT-026
    QWidget *m_structureSection = nullptr;
    // Central topic: type and direction of the sheet, as the Structure section (FEAT-072 B13).
    class StructurePicker *m_rootStructure = nullptr;
    QWidget *m_rootStructureSection = nullptr;
    QComboBox *m_preset = nullptr;
    QToolButton *m_styleAction = nullptr;  // FEAT-072 B15
    QToolButton *m_childrenStyle = nullptr;
    QToolButton *m_resetStyle = nullptr;
    QComboBox *m_shape = nullptr;
    QComboBox *m_fillStyle = nullptr;
    QComboBox *m_borderStyle = nullptr;
    PixelField *m_borderWidth = nullptr;
    ColorField *m_fill = nullptr;
    ColorField *m_border = nullptr;
    ColorField *m_text = nullptr;
    ColorField *m_branchColor = nullptr;
    QComboBox *m_branchStyle = nullptr;
    QComboBox *m_branchWidth = nullptr;
    QComboBox *m_branchShape = nullptr;  // FEAT-072: in the Branch section header
    QComboBox *m_branchStart = nullptr;  // FEAT-023: line ends
    QComboBox *m_branchEnd = nullptr;
    PixelField *m_width = nullptr;
    QToolButton *m_fit = nullptr;
    NumberingFields *m_numbering = nullptr;  // FEAT-024
    QComboBox *m_fontFamily = nullptr;
    QComboBox *m_fontSize = nullptr;
    QComboBox *m_fontWeight = nullptr;
    QComboBox *m_textCase = nullptr;
    QToolButton *m_bold = nullptr;
    QToolButton *m_italic = nullptr;
    QToolButton *m_strikeOut = nullptr;
    QToolButton *m_underline = nullptr;
    QList<QToolButton *> m_align;  // nell'ordine di TextAlign
    QString m_weightsFamily;       // font per cui e' compilata la select del peso
    QStringList m_themeColors;
    QList<std::pair<NodePreset, QString>> m_presetNames;  // ordine delle voci
    struct PreviewKey {
        QRgb color = 0;
        int depth = -1;
        QStringList palette;
        bool operator==(const PreviewKey &) const = default;
    };
    PreviewKey m_previewKey;
};

// FEAT-040: pagina Stile dell'immagine scelta (clic sull'immagine di un nodo): Dimensioni con la
// catena delle proporzioni e "Ripristina dimensione", Bordo (tipo, spessore, colore), Ombra,
// Opacita' e "Ripristina stile". Come StylePage non scrive nel modello: chiede con i segnali.
class QSlider;
class ImageStylePage final : public QWidget {
    Q_OBJECT
public:
    explicit ImageStylePage(QWidget *parent = nullptr);
    // Valori dell'immagine del nodo (nullptr = controlli disattivati). Non emette segnali.
    void setNode(const MindNode *node);

    PixelField *widthField() const { return m_width; }
    PixelField *heightField() const { return m_height; }
    QToolButton *chainButton() const { return m_chain; }  // acceso = proporzioni mantenute
    QToolButton *resetSizeButton() const { return m_resetSize; }
    QComboBox *borderField() const { return m_border; }
    PixelField *borderWidthField() const { return m_borderWidth; }
    ColorField *borderColorField() const { return m_borderColor; }
    ToggleSwitch *shadowSwitch() const { return m_shadow; }
    ToggleSwitch *outlineSwitch() const { return m_outline; }  // FEAT-051: solo per gli adesivi
    QSlider *opacitySlider() const { return m_opacitySlider; }
    QLineEdit *opacityField() const { return m_opacityField; }
    QToolButton *resetStyleButton() const { return m_resetStyle; }
    QToolButton *rotateButton() const { return m_rotate; }  // ruota di 90 gradi in senso orario

signals:
    void sizeChosen(const QSizeF &size);
    void sizeResetRequested();
    void styleChosen(const ImageStyle &style);
    void rotateRequested();

private:
    void chooseStyle(const std::function<void(ImageStyle &)> &change);
    void chooseWidth(qreal width);
    void chooseHeight(qreal height);
    void showOpacity(int percent);

    QSizeF m_size;
    ImageStyle m_style;
    PixelField *m_width = nullptr;
    PixelField *m_height = nullptr;
    QToolButton *m_chain = nullptr;
    QToolButton *m_resetSize = nullptr;
    QComboBox *m_border = nullptr;
    PixelField *m_borderWidth = nullptr;
    ColorField *m_borderColor = nullptr;
    ToggleSwitch *m_shadow = nullptr;
    ToggleSwitch *m_outline = nullptr;
    QWidget *m_outlineRow = nullptr;
    bool m_sticker = false;
    QSlider *m_opacitySlider = nullptr;
    QLineEdit *m_opacityField = nullptr;
    QToolButton *m_resetStyle = nullptr;
    QToolButton *m_rotate = nullptr;
};

// FEAT-031: menu delle palette ("Tema colore"). Popup scuro come i menu: selettore Preferito /
// Colorato / Classico, poi le palette in due colonne con nome e striscia dei 6 colori; la palette
// attiva ha il contorno chiaro. Clic: sceglie e chiude; Esc o clic fuori: chiude senza scelte.
// FEAT-032: azioni del popup che aprono altre finestre: si eseguono dopo la chiusura del popup.
struct PalettePickerActions {
    std::function<void(const QString &id)> remove;  // tasto destro su una palette personalizzata
    std::function<void()> addCurrent;               // palette del foglio non installata
    QString notInstalledName;                       // nome mostrato sul bottone di addCurrent
};

class PalettePicker final : public QWidget {
    Q_OBJECT
public:
    static constexpr int kTileWidthPx = 160;
    static constexpr int kTileHeightPx = 56;
    // Apre il menu a sinistra di anchor e aspetta la chiusura; onChosen riceve l'id scelto.
    static void exec(const QRect &anchor, const QString &currentId, const std::function<void(const QString &)> &onChosen,
                     const PalettePickerActions &actions = PalettePickerActions());
    // "Preferito": le palette scelte piu' volte su questo computer (impostazioni dell'app).
    // Preferite (richiesta dell'utente 2026-10-19): scelte con la stella in alto a sinistra della
    // palette, non piu' le piu' usate. Impostazioni dell'app ("palettes/favorites"), in ordine.
    static QStringList favoriteIds();
    static bool isFavorite(const QString &id);
    static void setFavorite(const QString &id, bool favorite);
    static constexpr int kStarPx = 14;
    static PalettePicker *openPicker();
    // Indice del gruppo mostrato: 0 Preferito, 1 Colorato, 2 Classico, 3 Personalizzate (per i test).
    int currentGroup() const;
    void showGroup(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    PalettePicker(const QString &currentId, std::function<void(const QString &)> onChosen, PalettePickerActions actions);
    QWidget *page(const QStringList &ids, const QString &emptyText);
    void rebuildFavorites();  // dopo un clic sulla stella
    QString m_currentId;
    std::function<void(const QString &)> m_onChosen;
    PalettePickerActions m_actions;
    std::function<void()> m_pending;  // azione da eseguire dopo la chiusura (FEAT-032)
    SegmentedControl *m_selector = nullptr;
    QStackedWidget *m_pages = nullptr;
    QEventLoop *m_loop = nullptr;
};

// FEAT-036: miniature delle varianti del tema colore: per ogni colore della palette una piccola mappa
// con quello sfondo (NodeStyle::resolveTree), due righe da tre; la variante attiva ha il contorno
// chiaro. Con withWhite c'e' prima una casella "Sfondo bianco" (variante -1, Impostazioni).
class VariantPicker final : public QWidget {
    Q_OBJECT
public:
    explicit VariantPicker(bool withWhite = false, QWidget *parent = nullptr);
    // Palette e variante attiva (-1 = nessuna: sfondo bianco o libero). Non emette variantChosen.
    void setTheme(const QString &paletteId, const QStringList &palette, int variant);
    int variant() const { return m_variant; }
    // Casella della variante (-1 = "Sfondo bianco", se c'e'); nullptr se non esiste.
    QToolButton *tile(int variant) const;
    // Tre caselle stanno nella larghezza minima del pannello laterale (280 px meno i margini).
    static constexpr int kTileWidthPx = 73;   // FEAT-072 B17: 3 x 73 + 2 x 4 = 227, the section width
    static constexpr int kTileHeightPx = 46;  // 69x43 proportions

signals:
    void variantChosen(int variant);

private:
    void rebuild();
    bool m_withWhite;
    QString m_paletteId;  // per le eccezioni di NodeStyle::backdropFor
    QStringList m_palette;
    int m_variant = -1;
    QList<QToolButton *> m_tiles;  // con withWhite la prima e' "Sfondo bianco"
};

// FEAT-043 parte stile: controlli del foglio nella pagina Mappa sotto "Colore sfondo": Font globale e
// Ampiezza linea ramo (select con l'etichetta grigia sopra), Ramo colorato (interruttore) e Colore
// delle linee. Non scrive nel modello: chiede con sheetStyleChosen.
class SheetStylePanel final : public QWidget {
    Q_OBJECT
public:
    explicit SheetStylePanel(QWidget *parent = nullptr);
    // Non emette segnali. automaticLineColor: il Colore mostrato quando il foglio non ne ha uno scelto
    // (il testo del nodo centrale).
    void setSheetStyle(const NodeStyle::SheetStyle &style, const QColor &automaticLineColor = QColor());
    QComboBox *fontField() const { return m_font; }
    QComboBox *widthField() const { return m_width; }
    ToggleSwitch *coloredSwitch() const { return m_colored; }
    ColorField *lineColorField() const { return m_color; }
    ToggleSwitch *showNotesSwitch() const { return m_showNotes; }  // FEAT-059
    ToggleSwitch *compactSwitch() const { return m_compact; }      // FEAT-043
    ToggleSwitch *freeBranchesSwitch() const { return m_freeBranches; }
    ToggleSwitch *justifySwitch() const { return m_justify; }
    // FEAT-024: numbering of the central topic; MainWindow shows it and applies the choice.
    NumberingFields *numberingFields() const { return m_numbering; }
    // FEAT-072 B20: a row added at the top of the "Map style" group (MainWindow's "Balance map").
    void addMapStyleRow(QWidget *row);
    // FEAT-025: Reset map style at the bottom, disabled when there is nothing to reset.
    QToolButton *resetMapButton() const { return m_resetMap; }
    void setResetMapEnabled(bool enabled);

signals:
    void sheetStyleChosen(const NodeStyle::SheetStyle &style);
    void resetMapRequested();

private:
    void choose(const std::function<void(NodeStyle::SheetStyle &)> &change);
    NodeStyle::SheetStyle m_style;
    NumberingFields *m_numbering = nullptr;
    QComboBox *m_font = nullptr;
    QComboBox *m_width = nullptr;
    ToggleSwitch *m_colored = nullptr;
    ColorField *m_color = nullptr;
    ToggleSwitch *m_showNotes = nullptr;
    ToggleSwitch *m_compact = nullptr;
    ToggleSwitch *m_freeBranches = nullptr;
    ToggleSwitch *m_justify = nullptr;
    class QVBoxLayout *m_mapStyleLayout = nullptr;
    QToolButton *m_resetMap = nullptr;
};

// FEAT-031: select "Tema colore" della pagina Mappa: striscia dei colori e nome della palette del
// foglio; il clic apre PalettePicker.
class PaletteSelect final : public QToolButton {
    Q_OBJECT
public:
    explicit PaletteSelect(QWidget *parent = nullptr);
    // savedName: nome della palette personalizzata salvato nel foglio (FEAT-032), vuoto altrimenti.
    void setPalette(const QString &id, const QStringList &colors, const QString &savedName = QString());
    QString paletteId() const { return m_id; }
    // Nome mostrato: quello della palette di base o personalizzata installata; per una palette
    // personalizzata che questo computer non conosce il nome del foglio con "(non installata)";
    // altrimenti palette di un modello.
    QString paletteName() const;

signals:
    void paletteChosen(const QString &id);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_id;
    QStringList m_colors;
    QString m_savedName;
};

// FEAT-053: Style page of the selected relationships: shape, line (pattern, width, color, start and
// end marks) and label
// text (font, size, color). Like the other pages it never writes the model: `edited` carries the
// change, applied by MainWindow to every selected relationship as one history step.
// FEAT-052: Style page with only boundaries selected: shape, fill, border.
class BoundaryStylePage final : public QWidget {
    Q_OBJECT
public:
    explicit BoundaryStylePage(QWidget *parent = nullptr);
    // Values of the boundaries (empty = controls disabled); the default colors are the effective
    // ones shown when a boundary has no color of its own. Emits nothing.
    void setBoundaries(const QList<Boundary> &boundaries, const QColor &defaultLineColor, const QColor &defaultFillColor);
    QComboBox *shapeField() const { return m_shape; }
    QComboBox *patternField() const { return m_pattern; }
    QComboBox *widthField() const { return m_width; }
    ColorField *lineColorField() const { return m_lineColor; }
    ColorField *fillColorField() const { return m_fillColor; }
    std::function<QStringList()> themeColors;  // "Current theme" of the color pickers
signals:
    void edited(const std::function<void(Boundary &)> &change);
private:
    QComboBox *m_shape = nullptr;
    QComboBox *m_pattern = nullptr;
    QComboBox *m_width = nullptr;
    ColorField *m_lineColor = nullptr;
    ColorField *m_fillColor = nullptr;
};

class RelationshipStylePage final : public QWidget {
    Q_OBJECT
public:
    explicit RelationshipStylePage(QWidget *parent = nullptr);
    // Values of the relationships (empty = controls disabled); the default colors are the effective
    // ones shown when a relationship has no color of its own. Emits nothing.
    void setRelationships(const QList<Relationship> &relationships, const QColor &defaultLineColor,
                          const QColor &defaultTextColor);
    QComboBox *shapeField() const { return m_shape; }
    QComboBox *patternField() const { return m_pattern; }
    QComboBox *widthField() const { return m_width; }
    ColorField *lineColorField() const { return m_lineColor; }
    QComboBox *startMarkField() const { return m_startMark; }
    QComboBox *endMarkField() const { return m_endMark; }
    QComboBox *fontFamilyField() const { return m_fontFamily; }
    QComboBox *fontSizeField() const { return m_fontSize; }
    ColorField *textColorField() const { return m_textColor; }
    std::function<QStringList()> themeColors;  // "Current theme" of the color pickers
signals:
    void edited(const std::function<void(Relationship &)> &change);
private:
    QComboBox *m_shape = nullptr;
    QComboBox *m_pattern = nullptr;
    QComboBox *m_width = nullptr;
    ColorField *m_lineColor = nullptr;
    QComboBox *m_startMark = nullptr;
    QComboBox *m_endMark = nullptr;
    QComboBox *m_fontFamily = nullptr;
    QComboBox *m_fontSize = nullptr;
    ColorField *m_textColor = nullptr;
};
