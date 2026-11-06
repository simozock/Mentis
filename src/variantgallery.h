#pragma once

// FEAT-026 fase D: galleria delle varianti (MapVariants), alla creazione di una mappa o di un foglio e
// dalla pagina Mappa. Miniature raggruppate per tipo; un clic sceglie, il doppio clic o "Crea"
// confermano, Esc annulla. Le miniature sono piccole mappe d'esempio disegnate dall'app stessa con la
// variante (colorate nella galleria, grigie nel pannello), create una volta per esecuzione.

#include "modaldialog.h"

#include <QPixmap>
#include <QToolButton>

class QListWidget;

class VariantGallery final : public ModalDialog {
    Q_OBJECT
public:
    // current: variante da mostrare scelta (vuota = la prima); acceptText: "Crea" o "Applica".
    VariantGallery(QWidget *parent, const QString &current, const QString &acceptText);
    QString chosenVariant() const { return m_chosen; }

    static constexpr int kThumbnailWidthPx = 176;
    static constexpr int kThumbnailHeightPx = 112;
    static constexpr QSize kShownThumbnailPx{140, 89};  // four per row in the modal (FEAT-072 F4)
    // Miniatura della variante; mono = grigia, per il pannello scuro.
    static QPixmap thumbnail(const QString &variantId, bool mono);

private:
    void choose(QListWidget *list, const QString &id);
    QString m_chosen;
    QList<QListWidget *> m_lists;
};

// FEAT-072 B16: map type card at the top of the Map page: grey thumbnail of the sheet's
// variant, type name and chevron. A click opens a popup on the left of the panel with the variants
// grouped by type (collapsible groups, three columns, current one outlined); a click on a thumbnail
// chooses it and closes the popup, Esc or a click outside close it without a choice.
class MapTypeSelect final : public QToolButton {
    Q_OBJECT
public:
    explicit MapTypeSelect(QWidget *parent = nullptr);
    void setVariant(const QString &variantId);  // no signal
    QString variant() const { return m_variant; }
    QSize sizeHint() const override;
    // Opens the popup (also used by tests); it deletes itself when closed.
    QWidget *openPopup();

signals:
    void variantChosen(const QString &variantId);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_variant;
};
