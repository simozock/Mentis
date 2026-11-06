// FEAT-019 (docs/features/FEAT-019-fondamenta-stile-nodo.md): cascata dello stile e operazioni su
// piu' nodi, senza widget (docs/architecture/node-style-contract.md).
#include "mindmapmodel.h"
#include "nodeshape.h"
#include "nodesketch.h"
#include "richtitle.h"
#include "nodestyle.h"
#include "slowtests.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTest>

class NodeStyleTest final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void mainBranchesTakePaletteColorsByPosition();
    void descendantsInheritParentColors();
    void manualValueWinsAndIsTheOnlyOneSaved();
    void colorsFollowPositionAfterRemoval();
    void paletteChangeRecolorsOnlyDerivedValues();
    void readReportsCommonMixedAndOrigin();
    void setStyleChangesOnlyThatPropertyAndSignalsOnce();
    void invalidOrEmptyRequestsChangeNothing();
    void clearStyleReturnsToCascade();
    void snapshotRoundTripKeepsManualValues();
    void previousFilesKeepTheirColors();
    void borderColorDoesNotColorTheBranch();
    void branchColorCascadesFromTheNodeDown();
    void branchStyleCascadesToOutgoingLines();
    void branchEndsCascadeAndSave();
    void branchWidthOnlyForDirectChildren();
    void rootBranchStyleAppliesToMainBranches();
    void levelsFollowPaletteRules();
    void deepBranchColorActsLikeThePalette();
    void levelsUsePresetsByRole();
    void presetsFollowTheTable();
    void importantColorsComeFromThePalette();
    void presetAppliesOnlyToThatNode();
    void manualColorShowsFillAndBorder();
    void borderTypeAndWidth();
    void presetFollowsBranchColor();
    void shapeValuesAreSavedAsNames();
    void shapesContainTheText();
    void punctualShapesStartWithoutFill();  // FEAT-030 stage B
    void underlineShapesShowTheLine();
    void sketchStylesAreSavedAsNames();
    void textOnTheMapBackgroundIsDarkened();
    void sketchIsStableAndStaysInItsMargins();
    void textDefaultsFollowTheRole();
    void textValuesAreSavedAndInvalidOnesIgnored();
    void textChoiceRemovesTheSamePartialFormatting();
    void manualStrikeOutWinsOverDepenna();
    void nearWhiteColorsAreSkipped();
    void freeNodesContinueTheSequence();
    void darkenedTextKeepsHue();
    void readableTextUsesPerceptualContrast();
    void paletteIsSavedPerSheet();
    void previousFilesGetTheDefaultPalette();
    void pastedStyleIsTheWholeLook();          // FEAT-085
    void copiedStyleKeepsTheValuesOfTheCopy();  // FEAT-085
    void pasteStyleOnAThousandTopicsIsFast();   // FEAT-085, slow test
    void copiedStyleKeepsNeutralLines();        // FEAT-085

private:
    std::unique_ptr<MindMapModel> m_model;
    MindNode *m_first = nullptr, *m_second = nullptr, *m_grandchild = nullptr;
};

namespace {
const QStringList kPalette{"#ff6b6b", "#ff9f68", "#ffd166"};
}

void NodeStyleTest::init() {
    m_model = std::make_unique<MindMapModel>();
    m_model->paletteId.clear();  // palette personalizzata
    m_model->activePalette = kPalette;
    m_first = m_model->addChild(m_model->root(), QStringLiteral("Primo"));
    m_second = m_model->addChild(m_model->root(), QStringLiteral("Secondo"));
    m_grandchild = m_model->addChild(m_first, QStringLiteral("Nipote"));
}

void NodeStyleTest::mainBranchesTakePaletteColorsByPosition() {
    QCOMPARE(m_first->fill, QColor("#ff6b6b"));
    QCOMPARE(m_first->border.alpha(), 0);  // FEAT-031: ramo principale senza bordo
    QCOMPARE(m_second->fill, QColor("#ff9f68"));
    QCOMPARE(m_first->text, NodeStyle::readableTextOn(QColor("#ff6b6b")));
    // FEAT-031: il nodo centrale non ha sfondo ne' bordo, testo scuro.
    QCOMPARE(m_model->root()->fill.alpha(), 0);
    QCOMPARE(m_model->root()->border.alpha(), 0);
    QCOMPARE(m_model->root()->text, QColor("#000000"));  // nero (2026-10-21)
    // Palette con meno colori dei rami: si ripete.
    MindNode *fourth = nullptr;
    m_model->addChild(m_model->root(), QStringLiteral("Terzo"));
    fourth = m_model->addChild(m_model->root(), QStringLiteral("Quarto"));
    QCOMPARE(fourth->fill, QColor("#ff6b6b"));
}

void NodeStyleTest::descendantsInheritParentColors() {
    // FEAT-031: secondo livello = colore del ramo al 20% di opacita', testo scurito.
    QCOMPARE(m_grandchild->fill.rgb(), m_first->fill.rgb());
    QCOMPARE(m_grandchild->fill.alpha(), 51);
    QCOMPARE(m_grandchild->text, NodeStyle::darkenedText(QColor("#ff6b6b")));
    QCOMPARE(m_grandchild->branchWidth, 2.0);
    QCOMPARE(m_grandchild->branchStyle, Qt::SolidLine);
}

void NodeStyleTest::manualValueWinsAndIsTheOnlyOneSaved() {
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FillColor, QColor("#123456")));
    QCOMPARE(m_first->fill, QColor("#123456"));
    QCOMPARE(m_first->branchColor, QColor("#ff6b6b"));  // le altre proprieta' restano ricavate
    QCOMPARE(m_grandchild->fill.rgb(), QColor("#ff6b6b").rgb());  // BUG-007: i figli tengono il loro colore
    const auto snapshot = m_model->toFlatSnapshot();
    const QJsonObject first = snapshot.value(m_first->id);
    QCOMPARE(first.value("fill").toString(), QColor("#123456").name(QColor::HexArgb));
    QVERIFY(!first.contains("border"));
    QVERIFY(!first.contains("text"));
    QVERIFY(!snapshot.value(m_grandchild->id).contains("fill"));
}

void NodeStyleTest::colorsFollowPositionAfterRemoval() {
    // Decisione dell'utente 2026-10-15: eliminando il primo ramo, gli altri scalano di colore.
    QVERIFY(m_model->removeNode(m_first));
    QCOMPARE(m_second->fill, QColor("#ff6b6b"));
}

void NodeStyleTest::paletteChangeRecolorsOnlyDerivedValues() {
    QVERIFY(m_model->setStyle({m_second}, StyleProperty::FillColor, QColor("#000000")));
    QSignalSpy edited(m_model.get(), &MindMapModel::styleEdited);
    m_model->setPalette(QString(), {"#00ff00", "#0000ff"});
    QCOMPARE(edited.count(), 1);
    QCOMPARE(m_first->fill, QColor("#00ff00"));
    QCOMPARE(m_grandchild->fill.rgb(), QColor("#00ff00").rgb());
    QCOMPARE(m_second->fill, QColor("#000000"));  // scelto a mano: resta
    QCOMPARE(m_second->branchColor, QColor("#0000ff"));
}

void NodeStyleTest::readReportsCommonMixedAndOrigin() {
    auto reading = NodeStyle::read({m_first, m_second}, StyleProperty::FillColor);
    QVERIFY(!reading.empty);
    QVERIFY(reading.mixed);
    QVERIFY(!reading.allManual);
    reading = NodeStyle::read({m_first, m_second}, StyleProperty::BranchWidth);
    QVERIFY(!reading.mixed);
    QCOMPARE(reading.value.toDouble(), 2.0);
    QVERIFY(m_model->setStyle({m_first, m_second}, StyleProperty::FillColor, QColor("#abcdef")));
    reading = NodeStyle::read({m_first, m_second}, StyleProperty::FillColor);
    QVERIFY(!reading.mixed);
    QVERIFY(reading.allManual);
    QCOMPARE(reading.value.value<QColor>(), QColor("#abcdef"));
    // Stesso valore effettivo, origini diverse: nessun "—", ma non tutto scelto a mano.
    QVERIFY(m_model->setStyle({m_grandchild}, StyleProperty::BranchWidth, 2.0));
    reading = NodeStyle::read({m_grandchild, m_second}, StyleProperty::BranchWidth);
    QVERIFY(!reading.mixed);
    QVERIFY(!reading.allManual);
    QVERIFY(NodeStyle::read({}, StyleProperty::FillColor).empty);
}

void NodeStyleTest::setStyleChangesOnlyThatPropertyAndSignalsOnce() {
    const QColor textBefore = m_first->text;
    QSignalSpy edited(m_model.get(), &MindMapModel::styleEdited);
    QVERIFY(m_model->setStyle({m_first, m_second, m_grandchild}, StyleProperty::BranchWidth, 5.0));
    QCOMPARE(edited.count(), 1);
    for (MindNode *node : {m_first, m_second, m_grandchild}) QCOMPARE(node->branchWidth, 5.0);
    QCOMPARE(m_first->text, textBefore);
    // Stesso valore di nuovo: nessun cambiamento, nessun segnale.
    QVERIFY(!m_model->setStyle({m_first, m_second, m_grandchild}, StyleProperty::BranchWidth, 5.0));
    QCOMPARE(edited.count(), 1);
}

void NodeStyleTest::invalidOrEmptyRequestsChangeNothing() {
    QSignalSpy edited(m_model.get(), &MindMapModel::styleEdited);
    const auto before = m_model->toFlatSnapshot();
    QVERIFY(!m_model->setStyle({}, StyleProperty::FillColor, QColor("#ffffff")));
    QVERIFY(!m_model->setStyle({m_first}, StyleProperty::FillColor, QColor()));
    QVERIFY(!m_model->setStyle({m_first}, StyleProperty::BranchWidth, 0.5));
    QVERIFY(!m_model->setStyle({m_first}, StyleProperty::BranchWidth, 40.0));
    QVERIFY(!m_model->setStyle({m_first}, StyleProperty::BranchStyle, static_cast<int>(Qt::DashDotDotLine)));
    QVERIFY(!m_model->setStyle({m_first, nullptr}, StyleProperty::FillColor, QColor("#ffffff")));
    QCOMPARE(edited.count(), 0);
    QCOMPARE(m_model->toFlatSnapshot(), before);
}

void NodeStyleTest::clearStyleReturnsToCascade() {
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FillColor, QColor("#123456")));
    QVERIFY(m_model->clearStyle({m_first}, StyleProperty::FillColor));
    QCOMPARE(m_first->fill, QColor("#ff6b6b"));
    QVERIFY(!NodeStyle::hasManualValue(m_first, StyleProperty::FillColor));
    QVERIFY(!m_model->clearStyle({m_first}, StyleProperty::FillColor));
}

void NodeStyleTest::snapshotRoundTripKeepsManualValues() {
    QVERIFY(m_model->setStyle({m_second}, StyleProperty::BranchStyle, static_cast<int>(Qt::DotLine)));
    QVERIFY(m_model->setStyle({m_second}, StyleProperty::TextColor, QColor(10, 20, 30, 128)));
    const auto snapshot = m_model->toFlatSnapshot();
    MindMapModel restored;
    QString error;
    QVERIFY2(restored.fromFlatSnapshot(snapshot, &error), qPrintable(error));
    const MindNode *second = restored.root()->children.at(1).get();
    QCOMPARE(second->branchStyle, Qt::DotLine);
    QCOMPARE(second->text, QColor(10, 20, 30, 128));  // l'opacita' si conserva
    QVERIFY(NodeStyle::hasManualValue(second, StyleProperty::TextColor));
    QVERIFY(!NodeStyle::hasManualValue(second, StyleProperty::FillColor));
    QCOMPARE(second->fill, QColor("#ff9f68"));
}

void NodeStyleTest::previousFilesKeepTheirColors() {
    // I file creati prima di FEAT-019 hanno i colori scritti su ogni nodo: diventano valori scelti
    // a mano e l'aspetto non cambia (compatibilita' dell'aspetto non richiesta, ma gratuita).
    QJsonObject child{{"id", "c"}, {"title", "Vecchio"}, {"fill", "#ff00ff00"}, {"text", "#ff000000"},
                      {"border", "#ff00ff00"}, {"branchWidth", 3}, {"branchStyle", static_cast<int>(Qt::DashLine)}};
    QJsonObject root{{"id", "r"}, {"title", "Centro"}, {"children", QJsonArray{child}}};
    MindMapModel model;
    QString error;
    QVERIFY2(model.fromJson(QJsonObject{{"format", "mentis-1"}, {"root", root}}, &error), qPrintable(error));
    const MindNode *loaded = model.root()->children.front().get();
    QCOMPARE(loaded->fill, QColor("#00ff00"));
    QCOMPARE(loaded->branchWidth, 3.0);
    QCOMPARE(loaded->branchStyle, Qt::DashLine);
    QVERIFY(NodeStyle::hasManualValue(loaded, StyleProperty::FillColor));
}

void NodeStyleTest::borderColorDoesNotColorTheBranch() {
    // BUG-005: il bordo colora solo il nodo; il ramo ha il suo colore.
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BorderColor, QColor("#0000ff")));
    QCOMPARE(m_first->border, QColor("#0000ff"));
    QCOMPARE(m_first->branchColor, QColor("#ff6b6b"));
}

void NodeStyleTest::branchColorCascadesFromTheNodeDown() {
    // BUG-005 (foto dell'utente): il colore del ramo scelto su un nodo vale per la linea che entra
    // nel nodo, per tutti i discendenti e per i loro colori ricavati, fino a un altro colore scelto.
    MindNode *greatGrandchild = m_model->addChild(m_grandchild, QStringLiteral("Pronipote"));
    QVERIFY(m_model->setStyle({m_grandchild}, StyleProperty::BranchColor, QColor("#ff0000")));
    QCOMPARE(m_first->branchColor, QColor("#ff6b6b"));        // sopra: invariato
    QCOMPARE(m_grandchild->branchColor, QColor("#ff0000"));   // linea entrante
    QCOMPARE(m_grandchild->fill.rgb(), QColor("#ff0000").rgb()); // colore ricavato del nodo
    QCOMPARE(greatGrandchild->branchColor, QColor("#ff0000")); // a cascata
    QVERIFY(m_model->setStyle({greatGrandchild}, StyleProperty::BranchColor, QColor("#00ff00")));
    MindNode *below = m_model->addChild(greatGrandchild, QStringLiteral("Sotto"));
    QCOMPARE(below->branchColor, QColor("#00ff00"));          // si ferma al colore scelto sotto
    // Il colore scelto sul nodo centrale vale per tutti i rami principali.
    QVERIFY(m_model->setStyle({m_model->root()}, StyleProperty::BranchColor, QColor("#333333")));
    QCOMPARE(m_first->branchColor, QColor("#333333"));
    QCOMPARE(m_second->branchColor, QColor("#333333"));
    QCOMPARE(m_grandchild->branchColor, QColor("#ff0000"));
}

void NodeStyleTest::branchStyleCascadesToOutgoingLines() {
    // BUG-006: lo stile vale per le linee verso i figli e tutti i discendenti, a cascata; il valore
    // del nodo descrive le linee che partono da lui, quindi la linea entrante (stile del genitore)
    // non cambia.
    MindNode *greatGrandchild = m_model->addChild(m_grandchild, QStringLiteral("Pronipote"));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BranchStyle, static_cast<int>(Qt::DashLine)));
    QCOMPARE(m_model->root()->branchStyle, Qt::SolidLine);  // linea che entra in m_first
    QCOMPARE(m_first->branchStyle, Qt::DashLine);           // linee verso i suoi figli
    QCOMPARE(m_grandchild->branchStyle, Qt::DashLine);      // e piu' sotto
    QVERIFY(m_model->setStyle({m_grandchild}, StyleProperty::BranchStyle, static_cast<int>(Qt::DotLine)));
    QCOMPARE(greatGrandchild->branchStyle, Qt::DotLine);
    QCOMPARE(m_second->branchStyle, Qt::SolidLine);
}

void NodeStyleTest::branchEndsCascadeAndSave() {
    // FEAT-023: start and end marks cascade like the line style; a child's own value (also None) wins.
    MindNode *greatGrandchild = m_model->addChild(m_grandchild, QStringLiteral("Great-grandchild"));
    QCOMPARE(m_first->branchEnd, BranchEnd::None);
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BranchEnd, static_cast<int>(BranchEnd::FilledArrow)));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BranchStart, static_cast<int>(BranchEnd::Dot)));
    QCOMPARE(m_grandchild->branchEnd, BranchEnd::FilledArrow);
    QCOMPARE(greatGrandchild->branchStart, BranchEnd::Dot);
    QCOMPARE(m_second->branchEnd, BranchEnd::None);
    QVERIFY(m_model->setStyle({m_grandchild}, StyleProperty::BranchEnd, static_cast<int>(BranchEnd::None)));
    QCOMPARE(greatGrandchild->branchEnd, BranchEnd::None);
    QVERIFY(!m_model->setStyle({m_first}, StyleProperty::BranchEnd, 99));
    const QJsonObject json = m_model->toJson();
    MindMapModel restored;
    QString error;
    QVERIFY2(restored.fromJson(json, &error), qPrintable(error));
    const MindNode *first = restored.root()->children.at(0).get();
    QCOMPARE(first->style.branchEnd, std::optional<BranchEnd>(BranchEnd::FilledArrow));
    QCOMPARE(first->style.branchStart, std::optional<BranchEnd>(BranchEnd::Dot));
    QCOMPARE(first->children.at(0)->style.branchEnd, std::optional<BranchEnd>(BranchEnd::None));  // stored as "none"
    QCOMPARE(first->children.at(0)->children.at(0)->branchEnd, BranchEnd::None);
    QCOMPARE(NodeStyle::branchEndFromKey(QStringLiteral("rocket")), std::nullopt);
}

void NodeStyleTest::branchWidthOnlyForDirectChildren() {
    // BUG-006 (foto dell'utente): lo spessore vale solo per le linee verso i figli diretti.
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BranchWidth, 6.0));
    QCOMPARE(m_first->branchWidth, 6.0);       // linee verso i figli di m_first
    QCOMPARE(m_grandchild->branchWidth, 2.0);  // linee verso i nipoti: invariate
}

void NodeStyleTest::rootBranchStyleAppliesToMainBranches() {
    QVERIFY(m_model->setStyle({m_model->root()}, StyleProperty::BranchStyle, static_cast<int>(Qt::DotLine)));
    QCOMPARE(m_model->root()->branchStyle, Qt::DotLine);
    QCOMPARE(m_first->branchStyle, Qt::DotLine);
}

void NodeStyleTest::levelsFollowPaletteRules() {
    // FEAT-031: principale pieno, secondo livello al 20%, dal terzo livello solo testo scurito.
    MindNode *third = m_model->addChild(m_grandchild, QStringLiteral("Terzo"));
    MindNode *fourth = m_model->addChild(third, QStringLiteral("Quarto"));
    const QColor branch("#ff6b6b");
    QCOMPARE(m_first->fill, branch);
    QCOMPARE(m_grandchild->fill.alpha(), 51);
    QCOMPARE(m_grandchild->border.alpha(), 0);
    for (MindNode *node : {third, fourth}) {
        QCOMPARE(node->fill.alpha(), 0);
        QCOMPARE(node->border.alpha(), 0);
        QCOMPARE(node->text, NodeStyle::darkenedText(branch));
        QCOMPARE(node->branchColor, branch);
    }
}

void NodeStyleTest::deepBranchColorActsLikeThePalette() {
    // Regola dell'utente (2026-10-16): il colore del ramo scelto su un nodo di livello 5 lascia
    // com'erano i livelli 1-4; dal livello 5 in giu' ramo e colori ricavati cambiano come se la
    // palette avesse assegnato quel colore (dal terzo livello: solo testo scurito).
    QList<MindNode *> chain{m_first, m_grandchild};
    for (int level = 3; level <= 7; ++level) chain.append(m_model->addChild(chain.last(), QStringLiteral("Livello %1").arg(level)));
    QList<QColor> fills, texts;
    for (MindNode *node : chain) { fills.append(node->fill); texts.append(node->text); }
    const QColor green("#2cd551");
    QVERIFY(m_model->setStyle({chain[4]}, StyleProperty::BranchColor, green));
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(chain[i]->branchColor, QColor("#ff6b6b"));
        QCOMPARE(chain[i]->fill, fills[i]);
        QCOMPARE(chain[i]->text, texts[i]);
    }
    for (int i = 4; i < chain.size(); ++i) {
        QCOMPARE(chain[i]->branchColor, green);  // la linea che entra nel nodo usa questo colore
        QCOMPARE(chain[i]->fill.alpha(), 0);
        QCOMPARE(chain[i]->text, NodeStyle::darkenedText(green));
    }
    QVERIFY(!NodeStyle::hasManualValue(chain[4], StyleProperty::FillColor));
    // Su un ramo principale la palette assegna il riempimento: cambia anche quello, e il secondo
    // livello prende la tinta al 20%.
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BranchColor, green));
    QCOMPARE(m_first->fill, green);
    QCOMPARE(m_first->text, NodeStyle::readableTextOn(green));
    QCOMPARE(m_grandchild->fill.rgb(), green.rgb());
    QCOMPARE(m_grandchild->fill.alpha(), 51);
    QVERIFY(!NodeStyle::hasManualValue(m_first, StyleProperty::FillColor));
}

void NodeStyleTest::levelsUsePresetsByRole() {
    // FEAT-020: senza scelte ogni nodo ha lo stile Predefinito, cioe' l'aspetto del suo livello.
    MindNode *third = m_model->addChild(m_grandchild, QStringLiteral("Terzo"));
    for (const MindNode *node : {m_model->root(), m_first, m_grandchild, third}) {
        QCOMPARE(node->preset, NodePreset::Predefinito);
        QVERIFY(!node->strikeOut);
    }
    QCOMPARE(m_first->fillStyle, NodeFillStyle::Pieno);
    QCOMPARE(m_first->borderStyle, NodeBorderStyle::Nessuno);
    QCOMPARE(m_grandchild->fill.alpha(), 51);
    QCOMPARE(third->fillStyle, NodeFillStyle::Nessuno);
    QCOMPARE(m_first->borderWidth, NodeStyle::kDefaultBorderWidthPx);
    QVERIFY(!NodeStyle::hasManualValue(m_first, StyleProperty::Preset));
}

void NodeStyleTest::presetsFollowTheTable() {
    // Tabella di FEAT-020 (stili di riferimento) sul principale e su un nodo di terzo livello.
    MindNode *third = m_model->addChild(m_grandchild, QStringLiteral("Terzo"));
    const NodeStyle::ImportantColors important = NodeStyle::importantColors(m_model->activePalette);
    for (MindNode *node : {m_first, third}) {
        const QColor color = node->branchColor;
        QVERIFY(m_model->setStyle({node}, StyleProperty::Preset, static_cast<int>(NodePreset::MoltoImportante)));
        QCOMPARE(node->fill, important.veryImportant);
        QCOMPARE(node->border.alpha(), 0);
        QCOMPARE(node->text, NodeStyle::readableTextOn(important.veryImportant));
        QVERIFY(m_model->setStyle({node}, StyleProperty::Preset, static_cast<int>(NodePreset::Importante)));
        QCOMPARE(node->fill, important.important);
        QCOMPARE(node->text, NodeStyle::readableTextOn(important.important));
        QVERIFY(!node->strikeOut);
        QVERIFY(m_model->setStyle({node}, StyleProperty::Preset, static_cast<int>(NodePreset::Depenna)));
        QCOMPARE(node->fill, NodeStyle::kMapBackground);
        QCOMPARE(node->border, color);
        QCOMPARE(node->borderStyle, NodeBorderStyle::Continuo);
        QCOMPARE(node->text, NodeStyle::darkenedText(color));
        QVERIFY(node->strikeOut);
        // Predefinito toglie lo stile scelto: torna l'aspetto del livello.
        QVERIFY(m_model->setStyle({node}, StyleProperty::Preset, static_cast<int>(NodePreset::Predefinito)));
        QVERIFY(!NodeStyle::hasManualValue(node, StyleProperty::Preset));
        QCOMPARE(node->preset, NodePreset::Predefinito);
        QVERIFY(!node->strikeOut);
    }
    QCOMPARE(m_first->fill, QColor("#ff6b6b"));
    QCOMPARE(third->fill.alpha(), 0);
    QCOMPARE(m_model->root()->text, QColor("#000000"));  // nero (2026-10-21)
}

void NodeStyleTest::importantColorsComeFromThePalette() {
    // Regola approvata il 2026-10-16: tinta dell'ultimo colore adatto della palette + 60 e + 105 gradi.
    // Menta (ultimo colore #046562, tinta 179): il riferimento usa #6769d8 (239) e #ba67d7 (284).
    const NodeStyle::ImportantColors menta = NodeStyle::importantColors(NodeStyle::findPalette(QStringLiteral("menta"))->colors);
    QVERIFY(qAbs(menta.veryImportant.hslHue() - 239) <= 2);
    QVERIFY(qAbs(menta.important.hslHue() - 284) <= 2);
    QVERIFY(qAbs(menta.veryImportant.hslSaturationF() - 0.65f) < 0.01f);  // 0,92 limitato a 0,65
    QVERIFY(qAbs(menta.veryImportant.lightnessF() - 0.6f) < 0.01f);
    // Te' verde: ultimo colore spento (#1f2b1d, saturazione 0,19 -> 0,25) -> luminosita' 47%.
    const NodeStyle::ImportantColors tea = NodeStyle::importantColors(NodeStyle::findPalette(QStringLiteral("te-verde"))->colors);
    QVERIFY(qAbs(tea.veryImportant.lightnessF() - 0.47f) < 0.01f);
    QVERIFY(qAbs(tea.veryImportant.hslSaturationF() - 0.25f) < 0.01f);
    // Zen (solo grigi): tinta 200 di riserva.
    const NodeStyle::ImportantColors zen = NodeStyle::importantColors(NodeStyle::findPalette(QStringLiteral("zen"))->colors);
    QVERIFY(qAbs(zen.veryImportant.hslHue() - 260) <= 2);
    // Cambiare palette ricolora i nodi Molto importante.
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::Preset, static_cast<int>(NodePreset::MoltoImportante)));
    m_model->setPalette(QStringLiteral("menta"), NodeStyle::findPalette(QStringLiteral("menta"))->colors);
    QCOMPARE(m_first->fill, menta.veryImportant);
}

void NodeStyleTest::presetAppliesOnlyToThatNode() {
    const QColor childFill = m_grandchild->fill;
    const QColor childText = m_grandchild->text;
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::Preset, static_cast<int>(NodePreset::Depenna)));
    QCOMPARE(m_grandchild->preset, NodePreset::Predefinito);
    QCOMPARE(m_grandchild->fill, childFill);
    QCOMPARE(m_grandchild->text, childText);
    QVERIFY(!m_grandchild->strikeOut);
    QVERIFY(m_model->clearStyle({m_first}, StyleProperty::Preset));
    QCOMPARE(m_first->preset, NodePreset::Predefinito);
    QCOMPARE(m_first->fill, QColor("#ff6b6b"));
}

void NodeStyleTest::manualColorShowsFillAndBorder() {
    // Un colore scelto a mano su un nodo senza riempimento ne' bordo li rende visibili.
    MindNode *third = m_model->addChild(m_grandchild, QStringLiteral("Terzo"));
    QVERIFY(m_model->setStyle({third}, StyleProperty::FillColor, QColor("#2cd551")));
    QCOMPARE(third->fillStyle, NodeFillStyle::Pieno);
    QCOMPARE(third->fill, QColor("#2cd551"));
    QVERIFY(m_model->setStyle({third}, StyleProperty::BorderColor, QColor("#4051b5")));
    QCOMPARE(third->borderStyle, NodeBorderStyle::Continuo);
    QCOMPARE(third->border, QColor("#4051b5"));
    // "Nessun riempimento" scelto a mano vince sul colore, che resta salvato.
    QVERIFY(m_model->setStyle({third}, StyleProperty::FillStyle, static_cast<int>(NodeFillStyle::Nessuno)));
    QCOMPARE(third->fill.alpha(), 0);
    QVERIFY(NodeStyle::hasManualValue(third, StyleProperty::FillColor));
    // Uno stile predefinito non tocca i valori scelti a mano.
    QVERIFY(m_model->setStyle({third}, StyleProperty::Preset, static_cast<int>(NodePreset::MoltoImportante)));
    QCOMPARE(third->fill.alpha(), 0);
    QCOMPARE(third->border, QColor("#4051b5"));
}

void NodeStyleTest::borderTypeAndWidth() {
    // Bordo tratteggiato scelto su un principale (che non ha bordo): colore del nodo, 2 px.
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BorderStyle, static_cast<int>(NodeBorderStyle::Tratteggiato)));
    QCOMPARE(m_first->border, QColor("#ff6b6b"));
    QCOMPARE(NodeStyle::borderPenStyle(m_first->borderStyle), Qt::DashLine);
    QCOMPARE(m_first->borderWidth, 2.0);
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BorderWidth, 6.0));
    QCOMPARE(m_first->borderWidth, 6.0);
    QVERIFY(!m_model->setStyle({m_first}, StyleProperty::BorderWidth, 7.0));
    QVERIFY(!m_model->setStyle({m_first}, StyleProperty::BorderWidth, 0.5));
    QVERIFY(!m_model->setStyle({m_first}, StyleProperty::Preset, 9));
    QVERIFY(!m_model->setStyle({m_first}, StyleProperty::BorderStyle, -1));
    QCOMPARE(m_first->borderWidth, 6.0);
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BorderStyle, static_cast<int>(NodeBorderStyle::Nessuno)));
    QCOMPARE(m_first->border.alpha(), 0);
    QCOMPARE(NodeStyle::borderPenStyle(NodeBorderStyle::Nessuno), Qt::NoPen);
}

void NodeStyleTest::presetFollowsBranchColor() {
    // Depenna su un nodo di terzo livello segue il colore del ramo.
    MindNode *third = m_model->addChild(m_grandchild, QStringLiteral("Terzo"));
    QVERIFY(m_model->setStyle({third}, StyleProperty::Preset, static_cast<int>(NodePreset::Depenna)));
    QCOMPARE(third->border, QColor("#ff6b6b"));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BranchColor, QColor("#4051b5")));
    QCOMPARE(third->border, QColor("#4051b5"));
    QCOMPARE(third->text, NodeStyle::darkenedText(QColor("#4051b5")));
}

void NodeStyleTest::shapeValuesAreSavedAsNames() {
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::Preset, static_cast<int>(NodePreset::MoltoImportante)));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FillStyle, static_cast<int>(NodeFillStyle::Nessuno)));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BorderStyle, static_cast<int>(NodeBorderStyle::Tratteggiato)));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BorderWidth, 4.0));
    const QJsonObject json = m_model->toJson();
    const QJsonObject first = json["root"].toObject()["children"].toArray().at(0).toObject();
    QCOMPARE(first["preset"].toString(), QStringLiteral("moltoImportante"));
    QCOMPARE(first["fillStyle"].toString(), QStringLiteral("nessuno"));
    QCOMPARE(first["borderStyle"].toString(), QStringLiteral("tratteggiato"));
    QCOMPARE(first["borderWidth"].toDouble(), 4.0);
    // Il secondo ramo non ha valori scelti: nessuna chiave nuova.
    const QJsonObject second = json["root"].toObject()["children"].toArray().at(1).toObject();
    QVERIFY(!second.contains("preset") && !second.contains("fillStyle") && !second.contains("borderStyle") && !second.contains("borderWidth"));
    MindMapModel restored;
    QString error;
    QVERIFY2(restored.fromJson(json, &error), qPrintable(error));
    const MindNode *back = restored.root()->children.at(0).get();
    QCOMPARE(back->style, m_first->style);
    QCOMPARE(back->preset, NodePreset::MoltoImportante);
    QCOMPARE(back->borderWidth, 4.0);
    // Anche lo storico (istantanea piatta).
    MindMapModel fromHistory;
    QVERIFY2(fromHistory.fromFlatSnapshot(m_model->toFlatSnapshot(), &error), qPrintable(error));
    QCOMPARE(fromHistory.root()->children.at(0)->style, m_first->style);
    // Nomi sconosciuti e spessori fuori intervallo letti da un file: ignorati.
    QJsonObject root{{"id", "r"}, {"title", "Centro"},
                     {"children", QJsonArray{QJsonObject{{"id", "c"}, {"title", "Ramo"}, {"preset", "stellato"},
                                                         {"fillStyle", 3}, {"borderStyle", "ondulato"}, {"borderWidth", 40}}}}};
    QVERIFY2(restored.fromJson(QJsonObject{{"format", "mentis-1"}, {"root", root}}, &error), qPrintable(error));
    const MindNode *odd = restored.root()->children.at(0).get();
    QVERIFY(!odd->style.preset && !odd->style.fillStyle && !odd->style.borderStyle && !odd->style.borderWidth);
    QCOMPARE(odd->preset, NodePreset::Predefinito);
}

void NodeStyleTest::shapesContainTheText() {
    // FEAT-020 parte B: ogni forma contiene il testo (riquadro interno: padding ridotto a 8/4 px)
    // e non e' mai piu' piccola del riquadro del contenuto; il ramo si attacca sul contorno.
    for (const QRectF &content : {QRectF(-60, -17, 120, 34), QRectF(-140, -40, 280, 80), QRectF(-25, -17, 50, 34)}) {
        const QRectF inner = content.adjusted(8, 4, -8, -4);
        for (int i = 0; i <= static_cast<int>(NodeShape::TrapezioRovesciato); ++i) {
            const auto shape = static_cast<NodeShape>(i);
            const QRectF outer = NodeShapes::outerRect(shape, content, 2);
            QVERIFY2(outer.width() >= content.width() - 0.01 && outer.height() >= content.height() - 0.01, qPrintable(NodeStyle::shapeKey(shape)));
            // Always centred across; the double line below and some flowchart shapes grow up or down only.
            QCOMPARE(outer.center().x(), content.center().x());
            const bool uneven = shape == NodeShape::DoppiaLineaSotto || shape == NodeShape::InputManuale || shape == NodeShape::Database
                                || shape == NodeShape::Documento || shape == NodeShape::DocumentiMultipli || NodeShapes::hasTextBelow(shape);
            if (!uneven) QCOMPARE(outer.center(), content.center());
            const QPainterPath path = NodeShapes::outline(shape, outer);
            // Un margine di 0,5 px per gli angoli che toccano il contorno (ellisse, rombo). FEAT-030: the
            // decision's text may reach its sides; the connectors' text is below their mark.
            if (shape != NodeShape::Decisione && !NodeShapes::hasTextBelow(shape)) {
                for (const QPointF &corner : {inner.topLeft(), inner.topRight(), inner.bottomLeft(), inner.bottomRight()}) {
                    const QPointF towardsCenter = corner + (content.center() - corner) * 0.01;
                    QVERIFY2(path.contains(towardsCenter), qPrintable(NodeStyle::shapeKey(shape)));
                }
            }
            const QPointF right = NodeShapes::branchAnchor(shape, outer, 2, true);
            const QPointF left = NodeShapes::branchAnchor(shape, outer, 2, false);
            QVERIFY(right.x() > content.center().x() && left.x() < content.center().x());
            if (!NodeShapes::isUnderline(shape)) {
                // At the text's height; the connectors at their mark's (62 px, 2 px from the top).
                QCOMPARE(right.y(), NodeShapes::hasTextBelow(shape) ? outer.top() + 33 : content.center().y());
                QVERIFY2(path.contains(right + QPointF(-1, 0)) && !path.contains(right + QPointF(2, 0)), qPrintable(NodeStyle::shapeKey(shape)));
            }
        }
    }
    // Le forme rettangolari non cambiano la misura di oggi.
    const QRectF box(-60, -17, 120, 34);
    QCOMPARE(NodeShapes::outerRect(NodeShape::RettangoloArrotondato, box, 2), box);
    // The circle: sqrt(3) x the text width (live 2026-10-24: 173 px of text in a 301 px circle).
    QCOMPARE(qRound(NodeShapes::outerRect(NodeShape::Cerchio, QRectF(-107, -21, 214, 42), 2, 173).width()), 300);
    QCOMPARE(NodeShapes::outerRect(NodeShape::LineaSotto, box, 6), box);
    // Doppia linea sotto: due linee di 6 px con 3 px fra loro, dentro il nodo; il ramo sulla prima.
    const QRectF doubled = NodeShapes::outerRect(NodeShape::DoppiaLineaSotto, box, 6);
    QCOMPARE(doubled.height(), box.height() + 3 + 6);
    const QList<QLineF> lines = NodeShapes::borderLines(NodeShape::DoppiaLineaSotto, doubled, 6);
    QCOMPARE(lines.size(), 2);
    // Square brackets (measured live 2026-10-24): 12 px arms beside the text's padding, a 3 px stroke inside.
    const QRectF brackets = NodeShapes::outerRect(NodeShape::ParentesiQuadre, box, 3);
    QCOMPARE(brackets, box.adjusted(-12, 0, 12, 0));
    const QList<QLineF> bracketLines = NodeShapes::borderLines(NodeShape::ParentesiQuadre, brackets, 3);
    QCOMPARE(bracketLines.size(), 6);
    QCOMPARE(bracketLines[0], QLineF(brackets.left() + 1.5, brackets.top() + 1.5, brackets.left() + 1.5, brackets.bottom() - 1.5));
    QCOMPARE(bracketLines[1], QLineF(brackets.left() + 1.5, brackets.top() + 1.5, brackets.left() + 10.5, brackets.top() + 1.5));
    QCOMPARE(bracketLines[5].x2(), brackets.right() - 10.5);
    QCOMPARE(lines[1].y1() - lines[0].y1(), 9.0);
    QCOMPARE(lines[1].y1() + 3, doubled.bottom());
    QCOMPARE(NodeShapes::branchAnchor(NodeShape::DoppiaLineaSotto, doubled, 6, false), lines[0].p1());
    QCOMPARE(NodeShapes::branchAnchor(NodeShape::DoppiaLineaSotto, doubled, 6, true), lines[0].p2());
    // FEAT-030: heart, cloud, star, drop, gem and shield grow in proportion (same aspect for any text).
    for (const NodeShape shape : {NodeShape::Cuore, NodeShape::Nuvola, NodeShape::Stella, NodeShape::Goccia,
                                  NodeShape::Gemma, NodeShape::Scudo}) {
        const QRectF small = NodeShapes::outerRect(shape, QRectF(-25, -17, 50, 34), 2);
        const QRectF large = NodeShapes::outerRect(shape, QRectF(-140, -40, 280, 80), 2);
        QVERIFY2(qAbs(small.width() / small.height() - large.width() / large.height()) < 0.01, qPrintable(NodeStyle::shapeKey(shape)));
        QVERIFY(large.width() > small.width());
    }
    // Arrows: the point is the anchor on its side; the body is as tall as the text box.
    const QRectF right = NodeShapes::outerRect(NodeShape::FrecciaDestra, box, 2);
    QCOMPARE(NodeShapes::branchAnchor(NodeShape::FrecciaDestra, right, 2, true), QPointF(right.right(), right.center().y()));
    QCOMPARE(NodeShapes::branchAnchor(NodeShape::FrecciaSinistra, right, 2, false), QPointF(right.left(), right.center().y()));
    QVERIFY(qAbs(right.height() * 0.6 - box.height()) < 0.01);
    // Lines inside: the fold of the note, the front card of the stack, the side bar; the flowchart's
    // bars, database ellipse, sheets and inner circle; none elsewhere.
    const QHash<NodeShape, int> innerCounts{{NodeShape::Nota, 1}, {NodeShape::PilaDiSchede, 1}, {NodeShape::SchedaConBordoLaterale, 1},
                                            {NodeShape::ProcessoPredefinito, 2}, {NodeShape::Database, 1},
                                            {NodeShape::DocumentiMultipli, 2}, {NodeShape::CerchioDoppio, 1}};
    for (int s = 0; s <= static_cast<int>(NodeShape::TrapezioRovesciato); ++s) {
        const auto shape = static_cast<NodeShape>(s);
        const QRectF outer = NodeShapes::outerRect(shape, box, 2);
        const QList<QPainterPath> inner = NodeShapes::innerLines(shape, outer);
        QCOMPARE(inner.size(), innerCounts.value(shape, 0));
        for (const QPainterPath &path : inner) {  // a line's bounds have no width: test its corners
            const QRectF bounds = path.boundingRect(), around = outer.adjusted(-0.01, -0.01, 0.01, 0.01);
            QVERIFY2(around.contains(bounds.topLeft()) && around.contains(bounds.bottomRight()), qPrintable(NodeStyle::shapeKey(shape)));
        }
    }
    const QRectF bar = NodeShapes::outerRect(NodeShape::SchedaConBordoLaterale, box, 2);
    const QPainterPath barLine = NodeShapes::innerLines(NodeShape::SchedaConBordoLaterale, bar).front();
    QCOMPARE(barLine.elementCount(), 2);
    QVERIFY(barLine.elementAt(0).x < box.left() + 16);  // left of the text
    // FEAT-030 stage B (measured live 2026-10-26): what each Puntuale shape adds to every side, the
    // marks inside the rectangle (round and curly brackets reach a little above and below).
    struct Punctual { NodeShape shape; qreal side, vertical; int marks; };
    for (const Punctual &p : {Punctual{NodeShape::ParentesiAngolari, 30, 0, 2}, Punctual{NodeShape::DoppieParentesiAngolari, 37, 0, 2},
                              Punctual{NodeShape::ParentesiTonde, 10, 0, 2}, Punctual{NodeShape::ParentesiGraffe, 18, 0, 2},
                              Punctual{NodeShape::Angoli, 20, 15, 2}, Punctual{NodeShape::Virgolette, 22, 3, 0}}) {
        const QString key = NodeStyle::shapeKey(p.shape);
        QVERIFY(NodeShapes::isPunctual(p.shape) && NodeShapes::hasOpenBorder(p.shape) && NodeShapes::startsOnBox(p.shape));
        const QRectF outer = NodeShapes::outerRect(p.shape, box, 2);
        QCOMPARE(outer, box.adjusted(-p.side, -p.vertical, p.side, p.vertical));
        QCOMPARE(NodeShapes::outline(p.shape, outer).boundingRect(), outer);  // the fill covers the rectangle
        QVERIFY(NodeShapes::borderLines(p.shape, outer, 2).isEmpty());
        const QList<QPainterPath> marks = NodeShapes::marks(p.shape, outer);
        QCOMPARE(marks.size(), p.marks);
        const qreal overhang = NodeShapes::markOverhang(p.shape);
        const QRectF reach = outer.adjusted(0, -overhang, 0, overhang);
        for (const QPainterPath &mark : marks) {
            const QRectF bounds = mark.boundingRect();
            QVERIFY2(reach.contains(bounds.topLeft()) && reach.contains(bounds.bottomRight()), qPrintable(key));
            QVERIFY2(bounds.right() < box.left() + 16 || bounds.left() > box.right() - 16
                         || bounds.bottom() < box.top() + 8 || bounds.top() > box.bottom() - 8, qPrintable(key));  // beside the text
        }
        if (!marks.isEmpty()) {  // the marks are 4 px inside the sides; the right one mirrors the left
            QCOMPARE(qRound(marks[0].boundingRect().left() - outer.left()), 4);
            QCOMPARE(qRound(outer.right() - marks[1].boundingRect().right()), 4);
        }
        const QPainterPath quotes = NodeShapes::filledMarks(p.shape, outer);
        QCOMPARE(quotes.isEmpty(), p.shape != NodeShape::Virgolette);
        const QPointF anchor = NodeShapes::branchAnchor(p.shape, outer, 2, true);
        QCOMPARE(anchor, QPointF(outer.right(), outer.center().y()));
    }
    // < >: 20 px wide, as tall as the topic; corners: arms 16 across and 30 down; quotes 12 x 10.
    const QRectF angles = NodeShapes::outerRect(NodeShape::ParentesiAngolari, box, 2);
    QCOMPARE(NodeShapes::marks(NodeShape::ParentesiAngolari, angles)[0].boundingRect(), QRectF(angles.left() + 4, angles.top() + 1, 20, angles.height() - 2));
    const QRectF corners = NodeShapes::outerRect(NodeShape::Angoli, box, 2);
    QCOMPARE(NodeShapes::marks(NodeShape::Angoli, corners)[0].boundingRect(), QRectF(corners.left() + 4, corners.top() + 4, 16, 30));
    const QRectF quoted = NodeShapes::outerRect(NodeShape::Virgolette, box, 2);
    const QRectF quote = NodeShapes::filledMarks(NodeShape::Virgolette, quoted).boundingRect();
    QVERIFY(qAbs(quote.width() - quoted.width() + 10) < 0.5 && qAbs(quote.height() - 10) < 0.5);
    // FEAT-030 stage C (measured live 2026-10-26, rules approved by the user): sizes from the content box.
    const qreal w = box.width(), h = box.height();
    struct Flow { NodeShape shape; QSizeF size; };
    for (const Flow &f : {Flow{NodeShape::Decisione, {w + 36, (w + 36) * 0.57}}, Flow{NodeShape::InputManuale, {w, h * 1.8}},
                          Flow{NodeShape::ProcessoPredefinito, {w + 76, h}}, Flow{NodeShape::OperazioneManuale, {w + 36, h}},
                          Flow{NodeShape::Database, {w + 4, h * 2.8}}, Flow{NodeShape::Documento, {w + 4, h + 5}},
                          Flow{NodeShape::DocumentiMultipli, {w + 20, h + 13}}, Flow{NodeShape::Ritardo, {w + 2, h}},
                          Flow{NodeShape::Dati, {w + 36, h}}, Flow{NodeShape::Connettore, {w, h + 70}},
                          Flow{NodeShape::ConnettoreFuoriPagina, {w, h + 70}},
                          Flow{NodeShape::CerchioDoppio, {std::hypot(w, h) + 13, std::hypot(w, h) + 13}},
                          Flow{NodeShape::Visualizzazione, {w - 16 + 0.6 * h, h}}, Flow{NodeShape::Trapezio, {w + 30, h}},
                          Flow{NodeShape::TrapezioRovesciato, {w + 30, h}}}) {
        const QRectF outer = NodeShapes::outerRect(f.shape, box, 2);
        QVERIFY2(qAbs(outer.width() - f.size.width()) < 0.01 && qAbs(outer.height() - f.size.height()) < 0.01,
                 qPrintable(QStringLiteral("%1 %2x%3").arg(NodeStyle::shapeKey(f.shape)).arg(outer.width()).arg(outer.height())));
        QVERIFY(!NodeShapes::hasOpenBorder(f.shape));
    }
    // Text in the lower part of manual input and in the database's body; connectors: a 62 px mark on top.
    QCOMPARE(NodeShapes::outerRect(NodeShape::InputManuale, box, 2).bottom(), box.bottom());
    QCOMPARE(NodeShapes::outerRect(NodeShape::Database, box, 2).top(), box.top() - 1.2 * h);
    const QRectF connector = NodeShapes::outerRect(NodeShape::Connettore, box, 2);
    QCOMPARE(NodeShapes::outline(NodeShape::Connettore, connector).boundingRect(), QRectF(box.center().x() - 31, connector.top() + 2, 62, 62));
    QVERIFY(NodeShapes::hasTextBelow(NodeShape::ConnettoreFuoriPagina) && !NodeShapes::hasTextBelow(NodeShape::CerchioDoppio));
}

void NodeStyleTest::punctualShapesStartWithoutFill() {
    // FEAT-030 stage B (measured live 2026-10-26): a Puntuale shape turns the fill off and a missing border
    // into a thin solid one, as one edit (one history step); a border already there stays.
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FillStyle, static_cast<int>(NodeFillStyle::Pieno)));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BorderStyle, static_cast<int>(NodeBorderStyle::Nessuno)));
    QVERIFY(m_model->setStyle({m_grandchild}, StyleProperty::BorderStyle, static_cast<int>(NodeBorderStyle::Tratteggiato)));
    QVERIFY(m_model->setStyle({m_grandchild}, StyleProperty::BorderWidth, 4.0));
    QSignalSpy edits(m_model.get(), &MindMapModel::styleEdited);
    QVERIFY(m_model->setStyle({m_first, m_grandchild}, StyleProperty::Shape, static_cast<int>(NodeShape::ParentesiTonde)));
    QCOMPARE(edits.count(), 1);
    QCOMPARE(m_first->shape, NodeShape::ParentesiTonde);
    QCOMPARE(m_first->fillStyle, NodeFillStyle::Nessuno);
    QCOMPARE(m_first->borderStyle, NodeBorderStyle::Continuo);
    QCOMPARE(m_first->borderWidth, NodeStyle::kPunctualBorderWidthPx);
    QCOMPARE(m_grandchild->fillStyle, NodeFillStyle::Nessuno);
    QCOMPARE(m_grandchild->borderStyle, NodeBorderStyle::Tratteggiato);
    QCOMPARE(m_grandchild->borderWidth, 4.0);
    // The fill can be turned on again; another shape keeps it.
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FillStyle, static_cast<int>(NodeFillStyle::PennellateDiagonali)));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::Shape, static_cast<int>(NodeShape::Rettangolo)));
    QCOMPARE(m_first->fillStyle, NodeFillStyle::PennellateDiagonali);
    // Saved with its name; the square brackets keep theirs.
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::Shape, static_cast<int>(NodeShape::Virgolette)));
    QCOMPARE(m_model->toJson()["root"].toObject()["children"].toArray().at(0).toObject()["shape"].toString(), QStringLiteral("virgolette"));
    QCOMPARE(NodeStyle::shapeKey(NodeShape::ParentesiQuadre), QStringLiteral("parentesiQuadre"));
    // FEAT-030 stage C: flowchart shapes keep fill and border; those with inner lines (predefined
    // process, database, multiple documents) turn a missing border into a thin one.
    QVERIFY(m_model->setStyle({m_second}, StyleProperty::FillStyle, static_cast<int>(NodeFillStyle::Pieno)));
    QVERIFY(m_model->setStyle({m_second}, StyleProperty::BorderStyle, static_cast<int>(NodeBorderStyle::Nessuno)));
    QVERIFY(m_model->setStyle({m_second}, StyleProperty::Shape, static_cast<int>(NodeShape::Decisione)));
    QCOMPARE(m_second->fillStyle, NodeFillStyle::Pieno);
    QCOMPARE(m_second->borderStyle, NodeBorderStyle::Nessuno);
    QVERIFY(m_model->setStyle({m_second}, StyleProperty::Shape, static_cast<int>(NodeShape::ProcessoPredefinito)));
    QCOMPARE(m_second->fillStyle, NodeFillStyle::Pieno);
    QCOMPARE(m_second->borderStyle, NodeBorderStyle::Continuo);
    QCOMPARE(m_second->borderWidth, NodeStyle::kPunctualBorderWidthPx);
    QCOMPARE(NodeStyle::shapeKey(NodeShape::TrapezioRovesciato), QStringLiteral("trapezioRovesciato"));
}

void NodeStyleTest::underlineShapesShowTheLine() {
    QCOMPARE(m_first->shape, NodeShape::RettangoloArrotondato);
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::Shape, static_cast<int>(NodeShape::LineaSotto)));
    QCOMPARE(m_first->shape, NodeShape::LineaSotto);
    QCOMPARE(m_first->fill.alpha(), 0);  // il riempimento non si vede
    QCOMPARE(m_first->borderStyle, NodeBorderStyle::Continuo);
    QCOMPARE(m_first->border, m_first->branchColor);
    QCOMPARE(m_first->text, NodeStyle::darkenedText(m_first->branchColor));  // non bianco su bianco
    QCOMPARE(m_grandchild->shape, NodeShape::RettangoloArrotondato);  // solo quel nodo
    // "Nessun bordo" scelto a mano toglie anche la linea; un colore di testo scelto resta.
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BorderStyle, static_cast<int>(NodeBorderStyle::Nessuno)));
    QCOMPARE(m_first->border.alpha(), 0);
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::TextColor, QColor("#123456")));
    QCOMPARE(m_first->text, QColor("#123456"));
    // Salvata con il suo nome.
    const QJsonObject first = m_model->toJson()["root"].toObject()["children"].toArray().at(0).toObject();
    QCOMPARE(first["shape"].toString(), QStringLiteral("lineaSotto"));
    MindMapModel restored;
    QString error;
    QVERIFY2(restored.fromJson(m_model->toJson(), &error), qPrintable(error));
    QCOMPARE(restored.root()->children.at(0)->shape, NodeShape::LineaSotto);
    QVERIFY(!m_model->setStyle({m_first}, StyleProperty::Shape, static_cast<int>(NodeShape::TrapezioRovesciato) + 1));
    QVERIFY(m_model->clearStyle({m_first}, StyleProperty::Shape));
    QCOMPARE(m_first->shape, NodeShape::RettangoloArrotondato);
}

void NodeStyleTest::sketchStylesAreSavedAsNames() {
    // FEAT-020 parte C: motivi e mano libera si salvano per nome, anche nello storico.
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FillStyle, static_cast<int>(NodeFillStyle::TratteggioOrizzontale)));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BorderStyle, static_cast<int>(NodeBorderStyle::TratteggiatoManoLibera)));
    QVERIFY(m_model->setStyle({m_second}, StyleProperty::FillStyle, static_cast<int>(NodeFillStyle::PienoManoLibera)));
    const QJsonObject json = m_model->toJson();
    const QJsonArray children = json["root"].toObject()["children"].toArray();
    QCOMPARE(children.at(0).toObject()["fillStyle"].toString(), QStringLiteral("tratteggioOrizzontale"));
    QCOMPARE(children.at(0).toObject()["borderStyle"].toString(), QStringLiteral("tratteggiatoManoLibera"));
    QCOMPARE(children.at(1).toObject()["fillStyle"].toString(), QStringLiteral("pienoManoLibera"));
    MindMapModel restored;
    QString error;
    QVERIFY2(restored.fromJson(json, &error), qPrintable(error));
    QCOMPARE(restored.root()->children.at(0)->fillStyle, NodeFillStyle::TratteggioOrizzontale);
    QCOMPARE(restored.root()->children.at(0)->borderStyle, NodeBorderStyle::TratteggiatoManoLibera);
    QCOMPARE(restored.root()->children.at(1)->fillStyle, NodeFillStyle::PienoManoLibera);
    MindMapModel fromHistory;
    QVERIFY2(fromHistory.fromFlatSnapshot(m_model->toFlatSnapshot(), &error), qPrintable(error));
    QCOMPARE(fromHistory.root()->children.at(0)->style, m_first->style);
    const QStringList fills{"pieno", "nessuno", "pienoManoLibera", "pennellateDiagonali", "tratteggioDiagonale",
                            "pennellateOrizzontali", "tratteggioOrizzontale"};
    for (int i = 0; i < fills.size(); ++i) QCOMPARE(NodeStyle::fillStyleKey(static_cast<NodeFillStyle>(i)), fills[i]);
    const QStringList borders{"continuo", "tratteggiato", "nessuno", "continuoManoLibera", "tratteggiatoManoLibera"};
    for (int i = 0; i < borders.size(); ++i) QCOMPARE(NodeStyle::borderStyleKey(static_cast<NodeBorderStyle>(i)), borders[i]);
    QCOMPARE(NodeStyle::borderPenStyle(NodeBorderStyle::ContinuoManoLibera), Qt::SolidLine);
    QCOMPARE(NodeStyle::borderPenStyle(NodeBorderStyle::TratteggiatoManoLibera), Qt::DashLine);
}

void NodeStyleTest::textOnTheMapBackgroundIsDarkened() {
    // Con un motivo o senza riempimento il testo sta sullo sfondo della mappa: colore del nodo
    // scurito, come dal terzo livello. Pieno a mano libera resta come il pieno.
    const QColor onFill = m_first->text;
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FillStyle, static_cast<int>(NodeFillStyle::PienoManoLibera)));
    QCOMPARE(m_first->text, onFill);
    for (const NodeFillStyle style : {NodeFillStyle::PennellateDiagonali, NodeFillStyle::TratteggioDiagonale,
                                      NodeFillStyle::PennellateOrizzontali, NodeFillStyle::TratteggioOrizzontale, NodeFillStyle::Nessuno}) {
        QVERIFY(m_model->setStyle({m_first}, StyleProperty::FillStyle, static_cast<int>(style)));
        QCOMPARE(m_first->text, NodeStyle::darkenedText(QColor("#ff6b6b")));
    }
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::TextColor, QColor("#4051b5")));  // scelto a mano: resta
    QCOMPARE(m_first->text, QColor("#4051b5"));
    QVERIFY(m_model->setStyle({m_model->root()}, StyleProperty::FillStyle, static_cast<int>(NodeFillStyle::PennellateDiagonali)));
    QCOMPARE(m_model->root()->text, QColor("#000000"));  // nero (2026-10-21)
}

void NodeStyleTest::sketchIsStableAndStaysInItsMargins() {
    // Seme indipendente dal processo (FNV-1a di "a"): lo stesso tratto dopo la riapertura.
    QCOMPARE(NodeSketch::seedFor(QStringLiteral("a")), Q_UINT64_C(0xaf63dc4c8601ec8c));
    const QRectF content(-60, -17, 120, 34);
    for (int s = 0; s <= static_cast<int>(NodeShape::TrapezioRovesciato); ++s) {
        const auto shape = static_cast<NodeShape>(s);
        const QRectF outer = NodeShapes::outerRect(shape, content, 2);
        const QPainterPath outline = NodeShapes::outline(shape, outer);
        const QRectF bounds = outline.controlPointRect();
        auto inside = [&bounds](const QPainterPath &path, qreal margin) {
            return bounds.adjusted(-margin, -margin, margin, margin).contains(path.controlPointRect());
        };
        const QPainterPath rough = NodeSketch::roughOutline(outline, 42);
        QCOMPARE(rough, NodeSketch::roughOutline(outline, 42));
        QVERIFY(rough != NodeSketch::roughOutline(outline, 43));
        QVERIFY(inside(rough, NodeSketch::kBorderWobble));
        QVERIFY(inside(NodeSketch::roughArea(outline, 42), NodeSketch::kFillOvershoot));
        for (const NodeFillStyle style : {NodeFillStyle::PennellateDiagonali, NodeFillStyle::TratteggioDiagonale,
                                          NodeFillStyle::PennellateOrizzontali, NodeFillStyle::TratteggioOrizzontale}) {
            const NodeSketch::Pattern pattern = NodeSketch::pattern(style, outline, 42);
            QVERIFY(!pattern.lines.isEmpty() && pattern.penWidth > 0);
            QCOMPARE(pattern.lines, NodeSketch::pattern(style, outline, 42).lines);
            // Tutto il tratto (segmento piu' meta' spessore) resta entro kPatternReach dal contorno.
            QPainterPath painted;
            QPainterPathStroker stroker;
            stroker.setWidth(pattern.penWidth);
            stroker.setCapStyle(Qt::FlatCap);
            for (const QLineF &line : pattern.lines) {
                QPainterPath segment(line.p1());
                segment.lineTo(line.p2());
                painted.addPath(stroker.createStroke(segment));
            }
            QVERIFY2(inside(painted, NodeSketch::kPatternReach), qPrintable(NodeStyle::fillStyleKey(style)));
        }
        for (const QLineF &line : NodeShapes::borderLines(shape, outer, 2)) {
            const QPainterPath rough = NodeSketch::roughLine(line, 42);
            QVERIFY(QRectF(line.p1(), line.p2()).normalized().adjusted(-1.5, -1.5, 1.5, 1.5).contains(rough.controlPointRect()));
        }
    }
}

void NodeStyleTest::textDefaultsFollowTheRole() {
    // Predefiniti (2026-10-21): centrale 30 px grassetto, principali 18 px medio, il resto 14 px.
    const MindNode *root = m_model->root();
    QCOMPARE(root->fontSize, 22.5);
    QCOMPARE(root->fontWeight, 800);  // ExtraBold (FEAT-072 A4)
    QCOMPARE(m_first->fontSize, 13.5);
    QCOMPARE(m_first->fontWeight, 500);
    QCOMPARE(m_grandchild->fontSize, 10.5);
    QCOMPARE(m_grandchild->fontWeight, 400);
    QVERIFY(m_first->fontFamily.isEmpty());
    QVERIFY(!m_first->italic && !m_first->underline && !m_first->strikeOut);
    QCOMPARE(m_first->textCase, TextCase::Nessuna);
    QCOMPARE(m_first->textAlign, TextAlign::Sinistra);
    // Una scelta vale solo per quel nodo, non per i figli (come riempimento e bordo).
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FontSize, 24));
    QCOMPARE(m_first->fontSize, 24.0);
    QCOMPARE(m_first->children.front()->fontSize, 10.5);  // sotto-argomento: 14 px
    // Font vuoto = torna al font di sistema.
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FontFamily, QStringLiteral("Georgia")));
    QCOMPARE(m_first->fontFamily, QStringLiteral("Georgia"));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FontFamily, QString()));
    QVERIFY(m_first->fontFamily.isEmpty());
    QVERIFY(!m_first->style.fontFamily);
}

void NodeStyleTest::textValuesAreSavedAndInvalidOnesIgnored() {
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FontFamily, QStringLiteral("Georgia")));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FontSize, 18));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FontWeight, 300));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::Italic, true));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::Underline, true));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::StrikeOut, true));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::TextCase, static_cast<int>(TextCase::Maiuscolo)));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::TextAlign, static_cast<int>(TextAlign::Giustificato)));
    // Valori non validi dal pannello: nessuna modifica.
    QVERIFY(!m_model->setStyle({m_first}, StyleProperty::FontSize, 7));
    QVERIFY(!m_model->setStyle({m_first}, StyleProperty::FontSize, 12.3));  // not a quarter point
    QVERIFY(!m_model->setStyle({m_first}, StyleProperty::FontWeight, 450));
    QVERIFY(!m_model->setStyle({m_first}, StyleProperty::Italic, 1));
    const QJsonObject first = m_model->toJson()["root"].toObject()["children"].toArray().at(0).toObject();
    QCOMPARE(first["fontFamily"].toString(), QStringLiteral("Georgia"));
    QCOMPARE(first["fontSize"].toDouble(), 18.0);
    QCOMPARE(first["fontWeight"].toInt(), 300);
    QVERIFY(first["italic"].toBool() && first["underline"].toBool() && first["strikeOut"].toBool());
    QCOMPARE(first["textCase"].toString(), QStringLiteral("maiuscolo"));
    QCOMPARE(first["textAlign"].toString(), QStringLiteral("giustificato"));
    const QJsonObject second = m_model->toJson()["root"].toObject()["children"].toArray().at(1).toObject();
    QVERIFY(!second.contains("fontSize") && !second.contains("textAlign") && !second.contains("italic"));
    MindMapModel restored;
    QString error;
    QVERIFY2(restored.fromJson(m_model->toJson(), &error), qPrintable(error));
    QCOMPARE(restored.root()->children.at(0)->style, m_first->style);
    QCOMPARE(restored.root()->children.at(0)->fontWeight, 300);
    MindMapModel fromHistory;
    QVERIFY2(fromHistory.fromFlatSnapshot(m_model->toFlatSnapshot(), &error), qPrintable(error));
    QCOMPARE(fromHistory.root()->children.at(0)->style, m_first->style);
    QJsonObject root{{"id", "r"}, {"title", "Centro"},
                     {"children", QJsonArray{QJsonObject{{"id", "c"}, {"title", "Ramo"}, {"fontFamily", ""}, {"fontSize", 100},
                                                         {"fontWeight", 450}, {"italic", "si"}, {"textCase", "boh"},
                                                         {"textAlign", 2}}}}};
    QVERIFY2(restored.fromJson(QJsonObject{{"format", "mentis-1"}, {"root", root}}, &error), qPrintable(error));
    const NodeStyleOverrides odd = restored.root()->children.at(0)->style;
    QVERIFY(!odd.fontFamily && !odd.fontSize && !odd.fontWeight && !odd.italic && !odd.textCase && !odd.textAlign);
}

void NodeStyleTest::textChoiceRemovesTheSamePartialFormatting() {
    // "due" rosso e corsivo; il colore scelto dal pannello toglie il rosso e lascia il corsivo.
    QTextDocument document;
    document.setPlainText(QStringLiteral("uno due"));
    QTextCursor cursor(&document);
    cursor.setPosition(4);
    cursor.setPosition(7, QTextCursor::KeepAnchor);
    QTextCharFormat format;
    format.setForeground(QColor("#ff0000"));
    format.setFontItalic(true);
    cursor.mergeCharFormat(format);
    m_first->title = QStringLiteral("uno due");
    m_first->titleHtml = document.toHtml();
    QVERIFY(m_first->titleHtml.contains(QStringLiteral("font-family")));  // font del documento nel <body>
    QSignalSpy styles(m_model.get(), &MindMapModel::stylesChanged);
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::TextColor, QColor("#4051b5")));
    QVERIFY(!styles.isEmpty());
    QTextDocument after;
    after.setHtml(m_first->titleHtml);
    QCOMPARE(after.toPlainText(), QStringLiteral("uno due"));
    for (QTextBlock block = after.begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            QVERIFY(!fragment.charFormat().hasProperty(QTextFormat::ForegroundBrush));
            if (fragment.text() == QStringLiteral("due")) QVERIFY(fragment.charFormat().fontItalic());
        }
    QVERIFY(m_first->titleHtml.contains(QStringLiteral("italic")));
    // Il font del documento non resta nel titolo: vale quello del nodo.
    QVERIFY(!RichTitle::withoutDocumentFont(document.toHtml()).contains(QStringLiteral("font-size:")));
    // Il corsivo dal pannello toglie anche il corsivo della parola.
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::Italic, false));
    QVERIFY(!m_first->titleHtml.contains(QStringLiteral("italic")));
}

void NodeStyleTest::manualStrikeOutWinsOverDepenna() {
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::Preset, static_cast<int>(NodePreset::Depenna)));
    QVERIFY(m_first->strikeOut);
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::StrikeOut, false));
    QVERIFY(!m_first->strikeOut);
    QVERIFY(m_model->clearStyle({m_first}, StyleProperty::StrikeOut));
    QVERIFY(m_first->strikeOut);  // torna quello dello stile
}

void NodeStyleTest::nearWhiteColorsAreSkipped() {
    // Energia: bianco e grigio chiarissimo non vanno ai rami; restano rosso, giallo, blu, nero.
    const NodeStyle::Palette *energy = NodeStyle::findPalette(QStringLiteral("energia"));
    QVERIFY(energy);
    m_model->setPalette(energy->id, energy->colors);
    MindNode *third = m_model->addChild(m_model->root(), QStringLiteral("Terzo"));
    MindNode *fourth = m_model->addChild(m_model->root(), QStringLiteral("Quarto"));
    MindNode *fifth = m_model->addChild(m_model->root(), QStringLiteral("Quinto"));
    QCOMPARE(m_first->branchColor, QColor("#f22816"));
    QCOMPARE(m_second->branchColor, QColor("#f2b807"));
    QCOMPARE(third->branchColor, QColor("#233ed9"));
    QCOMPARE(fourth->branchColor, QColor("#0d0d0d"));
    QCOMPARE(fifth->branchColor, QColor("#f22816"));
    QVERIFY(NodeStyle::isNearWhite(QColor("#f2f2f2")));
    QVERIFY(!NodeStyle::isNearWhite(QColor("#ededed")));  // Zen: 0,847, resta
    // Una palette di soli bianchi li usa comunque.
    m_model->setPalette(QString(), {"#ffffff"});
    QCOMPARE(m_first->branchColor, QColor("#ffffff"));
}

void NodeStyleTest::freeNodesContinueTheSequence() {
    MindNode *free = m_model->addChild(m_model->root(), QStringLiteral("Libero"));
    free->floating = true;
    m_model->addChild(m_model->root(), QStringLiteral("Terzo"));  // principale: va prima del libero (2026-10-21)
    m_model->refreshStyles();
    // Principali: Primo, Secondo, Terzo -> colori 0, 1, 2; il libero prende il successivo (0 dopo 3).
    QCOMPARE(m_model->root()->children[2]->title, QStringLiteral("Terzo"));
    QCOMPARE(m_model->root()->children[2]->branchColor, QColor("#ffd166"));
    QCOMPARE(free->branchColor, QColor("#ff6b6b"));
    QCOMPARE(free->fill, QColor("#ff6b6b"));  // aspetto da principale
}

void NodeStyleTest::darkenedTextKeepsHue() {
    QCOMPARE(NodeStyle::darkenedText(QColor("#ff6b6b")).name(), QStringLiteral("#660000"));
    QCOMPARE(NodeStyle::darkenedText(QColor("#ffffff")).name(), QStringLiteral("#333333"));
}

void NodeStyleTest::readableTextUsesPerceptualContrast() {
    // BUG-009: prima (luminosita' HSL) il testo era scuro su blu e rossi decisi. Dal 2026-10-21 come
    // Nero da Lc 45 (anche sul corallo di Alba), altrimenti bianco.
    const QColor white("#ffffff"), dark("#000000");
    QCOMPARE(NodeStyle::readableTextOn(QColor("#233ed9")), white);  // blu di Energia
    QCOMPARE(NodeStyle::readableTextOn(QColor("#e53935")), white);  // rosso di Costanza
    QCOMPARE(NodeStyle::readableTextOn(QColor("#ff6b6b")), dark);   // corallo di Alba: nero Lc 51
    QCOMPARE(NodeStyle::readableTextOn(QColor("#ffd166")), dark);   // giallo chiaro
    QCOMPARE(NodeStyle::readableTextOn(QColor("#88e2d7")), dark);   // acqua chiaro di Alba
    QCOMPARE(NodeStyle::readableTextOn(QColor("#ffffff")), dark);
    QCOMPARE(NodeStyle::readableTextOn(QColor("#172033")), white);
    // Valori di riferimento di APCA: nero su bianco circa 106, bianco su nero circa 108.
    QVERIFY(qAbs(NodeStyle::apcaContrast(QColor("#000000"), QColor("#ffffff")) - 106.04) < 0.1);
    QVERIFY(qAbs(NodeStyle::apcaContrast(QColor("#ffffff"), QColor("#000000")) - 107.88) < 0.1);
    // Un riempimento semitrasparente si legge come composto sul bianco: al 10% e' quasi bianco.
    QColor faint("#233ed9"); faint.setAlphaF(0.1f);
    QCOMPARE(NodeStyle::readableTextOn(faint), dark);
}

void NodeStyleTest::paletteIsSavedPerSheet() {
    const NodeStyle::Palette *ocean = NodeStyle::findPalette(QStringLiteral("oceano"));
    m_model->setPalette(ocean->id, ocean->colors);
    MindMapModel restored;
    QString error;
    QVERIFY2(restored.fromJson(m_model->toJson(), &error), qPrintable(error));
    QCOMPARE(restored.paletteId, QStringLiteral("oceano"));
    QCOMPARE(restored.activePalette, ocean->colors);
    // Anche lo storico (istantanea piatta) conserva la palette.
    MindMapModel fromHistory;
    QVERIFY2(fromHistory.fromFlatSnapshot(m_model->toFlatSnapshot(), &error), qPrintable(error));
    QCOMPARE(fromHistory.paletteId, QStringLiteral("oceano"));
    // Palette di un modello (id non di base): si conservano i colori salvati.
    m_model->setPalette(QStringLiteral("modello-scuro"), {"#5146e5", "#8b5cf6"});
    QVERIFY2(restored.fromJson(m_model->toJson(), &error), qPrintable(error));
    QCOMPARE(restored.paletteId, QStringLiteral("modello-scuro"));
    QCOMPARE(restored.activePalette, QStringList({"#5146e5", "#8b5cf6"}));
    // Palette personalizzata (id vuoto): si conservano i colori.
    m_model->setPalette(QString(), {"#123456", "#654321"});
    QVERIFY2(restored.fromJson(m_model->toJson(), &error), qPrintable(error));
    QVERIFY(restored.paletteId.isEmpty());
    QCOMPARE(restored.activePalette, QStringList({"#123456", "#654321"}));
}

void NodeStyleTest::previousFilesGetTheDefaultPalette() {
    QJsonObject root{{"id", "r"}, {"title", "Centro"}};
    MindMapModel model;
    QString error;
    // Vecchia palette da 7 colori senza id: non e' una palette di base -> Alba.
    QJsonArray oldPalette{"#ff6b6b", "#ff9f68", "#ffd166", "#8bd3b0", "#6edbd5", "#74b9ff", "#a78bfa"};
    QVERIFY2(model.fromJson(QJsonObject{{"format", "mentis-1"}, {"root", root}, {"palette", oldPalette}}, &error), qPrintable(error));
    QCOMPARE(model.paletteId, NodeStyle::kDefaultPaletteId);
    // Colori uguali a una palette di base senza id: riconosciuta.
    QJsonArray rose; for (const QString &color : NodeStyle::findPalette(QStringLiteral("rose"))->colors) rose.append(color);
    QVERIFY2(model.fromJson(QJsonObject{{"format", "mentis-1"}, {"root", root}, {"palette", rose}}, &error), qPrintable(error));
    QCOMPARE(model.paletteId, QStringLiteral("rose"));
    QCOMPARE(NodeStyle::basePalettes().size(), 43);
}

// FEAT-085: Paste style gives every property the source's effective value, as a manual value, on
// topics of another role too; one step, none when nothing changes.
void NodeStyleTest::pastedStyleIsTheWholeLook() {
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FontWeight, 700));
    const MindMapModel::TopicStyle style = m_model->topicStyle(m_first);
    QVERIFY(style.nameOnBar);  // m_first has a child: a summary bar shows its name by default
    QSignalSpy edited(m_model.get(), &MindMapModel::styleEdited);
    QSignalSpy gantt(m_model.get(), &MindMapModel::ganttChanged);
    QVERIFY(m_model->pasteStyle(style, {m_second, m_grandchild}));
    QCOMPARE(edited.count(), 1);
    QCOMPARE(gantt.count(), 1);
    for (int index = 0; index <= static_cast<int>(StyleProperty::Numbering); ++index) {
        const auto property = static_cast<StyleProperty>(index);
        for (const MindNode *target : {m_second, m_grandchild})
            QVERIFY2(NodeStyle::effectiveValue(target, property) == NodeStyle::effectiveValue(m_first, property), qPrintable(QString::number(index)));
    }
    // Values from the palette and the role became the targets' own.
    QVERIFY(NodeStyle::hasManualValue(m_grandchild, StyleProperty::FillColor));
    QVERIFY(NodeStyle::hasManualValue(m_grandchild, StyleProperty::BranchColor));
    QVERIFY(NodeStyle::hasManualValue(m_grandchild, StyleProperty::FontSize));
    QCOMPARE(m_second->ganttLabel, std::optional<bool>(true));  // a task: not its default
    QVERIFY(!m_model->pasteStyle(style, {m_second, m_grandchild}));
    QCOMPARE(edited.count(), 1);
    QVERIFY(!m_model->pasteStyle(style, {m_second, nullptr}));
}

void NodeStyleTest::copiedStyleKeepsTheValuesOfTheCopy() {
    const QColor fill = m_first->fill;
    const MindMapModel::TopicStyle style = m_model->topicStyle(m_first);
    m_model->setPalette(QString(), {QStringLiteral("#123456"), QStringLiteral("#654321")});
    QVERIFY(m_first->fill != fill);
    QVERIFY(m_model->pasteStyle(style, {m_second}));
    QCOMPARE(m_second->fill, fill);
}

// FEAT-085: with coloured branches off a neutral line gives no branch colour, a chosen one does.
void NodeStyleTest::copiedStyleKeepsNeutralLines() {
    m_model->sheetStyle.coloredBranches = false;
    m_model->refreshStyles();
    QVERIFY(!m_model->topicStyle(m_grandchild).style.branchColor);
    QVERIFY(m_model->pasteStyle(m_model->topicStyle(m_grandchild), {m_second}));
    QCOMPARE(m_second->lineColor, m_grandchild->lineColor);
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::BranchColor, QColor("#00aa00")));
    QCOMPARE(m_model->topicStyle(m_grandchild).style.branchColor, std::optional<QColor>(QColor("#00aa00")));
}

// FEAT-085 performance target: a paste on 1000 selected topics within 100 ms (one refreshStyles).
void NodeStyleTest::pasteStyleOnAThousandTopicsIsFast() {
    MENTIS_SLOW_TEST();
    QList<MindNode *> targets;
    for (int index = 0; index < 1000; ++index)
        targets.append(m_model->addChild(index % 2 ? m_first : m_second, QStringLiteral("T%1").arg(index)));
    QVERIFY(m_model->setStyle({m_first}, StyleProperty::FontWeight, 700));
    const MindMapModel::TopicStyle style = m_model->topicStyle(m_first);
    QElapsedTimer timer;
    timer.start();
    QVERIFY(m_model->pasteStyle(style, targets));
    const qint64 elapsedMs = timer.elapsed();
    qInfo() << "pasteStyle on 1000 topics:" << elapsedMs << "ms";
    QVERIFY2(elapsedMs <= 100, qPrintable(QString::number(elapsedMs)));
}

QTEST_MAIN(NodeStyleTest)
#include "node_style_test.moc"
