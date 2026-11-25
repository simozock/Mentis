#include "pitchtemplateeditor.h"

#include "modaldialog.h"
#include "nodeitem.h"
#include "pitchchoices.h"
#include "pitchpage.h"
#include "stylecontrols.h"
#include "toggleswitch.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
using PitchChoices::kLogoSizeStep;
}  // namespace

PitchTemplateEditor::PitchTemplateEditor(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("PitchTemplateEditor"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    const auto heading = [this, layout](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("GroupHeading"));
        layout->addWidget(label);
    };
    const auto row = [this, layout](const QString &text, QWidget *control) {
        auto *widget = new QWidget(this);
        auto *line = new QHBoxLayout(widget);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(8);
        line->addWidget(new QLabel(text, widget));
        line->addStretch();
        control->setAccessibleName(text);
        line->addWidget(control);
        layout->addWidget(widget);
    };
    const auto select = [this](const char *name, int width = 130) {
        QComboBox *field = makeDarkSelect(this);
        field->setObjectName(QLatin1String(name));
        field->setFixedWidth(width);
        connect(field, &QComboBox::activated, this, [this] { changed(); });
        return field;
    };
    const auto lineEdit = [this](const char *name) {
        auto *field = new QLineEdit(this);
        field->setObjectName(QLatin1String(name));
        field->setFixedWidth(130);
        connect(field, &QLineEdit::textEdited, this, [this] { changed(); });
        return field;
    };

    heading(tr("Template"));
    m_name = lineEdit("PitchTemplateName");
    row(tr("Name"), m_name);
    m_base = select("PitchTemplateBase");
    for (int i = 0; i < Pitch::kThemeCount; ++i) m_base->addItem(PitchChoices::themeName(Pitch::Theme(i)));
    row(tr("Base theme"), m_base);

    layout->addWidget(panelDivider(this));
    heading(tr("Colours"));
    const QStringList colorNames = PitchChoices::templateColorNames();
    for (int i = 0; i < kColorCount; ++i) {
        auto *field = m_colors[i] = new ColorField(this);
        field->setObjectName(QStringLiteral("PitchTemplateColor%1").arg(i));
        field->allowOpacity = false;
        connect(field, &ColorField::colorChosen, this, [this, i](const QColor &color) {
            PitchChoices::templateColor(m_draft, i) = color;
            changed();
        });
        row(colorNames[i], field);
    }
    auto *resetColors = new QPushButton(tr("Use the base theme's colours"), this);
    resetColors->setObjectName(QStringLiteral("PitchTemplateResetColors"));
    connect(resetColors, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < kColorCount; ++i) PitchChoices::templateColor(m_draft, i) = QColor();
        changed();
    });
    layout->addWidget(resetColors);

    layout->addWidget(panelDivider(this));
    heading(tr("Text"));
    m_titleFont = select("PitchTemplateTitleFont");
    m_itemFont = select("PitchTemplateItemFont");
    NodeItem::loadBundledFonts();
    const QStringList families = QFontDatabase::families();
    for (QComboBox *field : {m_titleFont, m_itemFont}) {
        field->setMaxVisibleItems(12);
        field->addItem(tr("Of the theme"), QString());
        for (const QString &family : families)
            if (!family.startsWith(QLatin1Char('@'))) field->addItem(family, family);  // "@": vertical fonts
    }
    row(tr("Title font"), m_titleFont);
    row(tr("Item font"), m_itemFont);
    m_itemStyle = select("PitchTemplateItemStyle");
    m_itemStyle->addItems(PitchChoices::itemStyleNames());
    row(tr("Items"), m_itemStyle);
    m_motif = select("PitchTemplateMotif");
    m_motif->addItems(PitchChoices::motifNames());
    row(tr("Motif"), m_motif);

    layout->addWidget(panelDivider(this));
    heading(tr("Pictures"));
    const QStringList pictureNames = PitchChoices::templatePictureNames();
    for (int i = 0; i < 3; ++i) {
        auto *widget = new QWidget(this);
        auto *line = new QGridLayout(widget);
        line->setContentsMargins(0, 0, 0, 0);
        line->setHorizontalSpacing(8);
        line->setVerticalSpacing(4);
        line->addWidget(new QLabel(pictureNames[i], widget), 0, 0);
        m_pictureState[i] = new QLabel(widget);
        m_pictureState[i]->setObjectName(QStringLiteral("FieldCaption"));
        line->addWidget(m_pictureState[i], 0, 1, Qt::AlignRight);
        auto *choose = new QPushButton(tr("Choose..."), widget);
        choose->setObjectName(QStringLiteral("PitchTemplateChoose%1").arg(i));
        choose->setAccessibleName(pictureNames[i]);
        auto *remove = m_removePicture[i] = new QPushButton(tr("Remove"), widget);
        remove->setObjectName(QStringLiteral("PitchTemplateRemove%1").arg(i));
        line->addWidget(choose, 1, 0);
        line->addWidget(remove, 1, 1);
        connect(choose, &QPushButton::clicked, this, [this, i] { choosePicture(i); });
        connect(remove, &QPushButton::clicked, this, [this, i] {
            PitchChoices::templatePicture(m_draft, i) = QByteArray();
            changed();
        });
        layout->addWidget(widget);
    }
    m_logoCorner = select("PitchTemplateLogoCorner");
    m_logoCorner->addItems(PitchChoices::cornerNames());
    row(tr("Logo corner"), m_logoCorner);
    m_logoSize = select("PitchTemplateLogoSize");
    for (const int size : PitchChoices::logoSizes()) m_logoSize->addItem(QStringLiteral("%1%").arg(size), size);
    row(tr("Logo size"), m_logoSize);

    layout->addWidget(panelDivider(this));
    heading(tr("Footer"));
    m_footer = lineEdit("PitchTemplateFooter");
    row(tr("Footer text"), m_footer);
    m_numbers = new ToggleSwitch(this);
    m_numbers->setObjectName(QStringLiteral("PitchTemplateNumbers"));
    connect(m_numbers, &ToggleSwitch::toggled, this, [this] { changed(); });
    row(tr("Slide numbers"), m_numbers);

    layout->addWidget(panelDivider(this));
    auto *buttons = new QWidget(this);
    auto *grid = new QGridLayout(buttons);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(8);
    auto *save = new QPushButton(tr("Save"), buttons);
    save->setObjectName(QStringLiteral("PitchTemplateSave"));
    auto *cancel = new QPushButton(tr("Cancel"), buttons);
    cancel->setObjectName(QStringLiteral("PitchTemplateCancel"));
    auto *exportButton = new QPushButton(tr("Export..."), buttons);
    exportButton->setObjectName(QStringLiteral("PitchTemplateExport"));
    m_delete = new QPushButton(tr("Delete"), buttons);
    m_delete->setObjectName(QStringLiteral("PitchTemplateDelete"));
    grid->addWidget(save, 0, 0);
    grid->addWidget(cancel, 0, 1);
    grid->addWidget(exportButton, 1, 0);
    grid->addWidget(m_delete, 1, 1);
    connect(save, &QPushButton::clicked, this, &PitchTemplateEditor::saveRequested);
    connect(cancel, &QPushButton::clicked, this, &PitchTemplateEditor::cancelRequested);
    connect(exportButton, &QPushButton::clicked, this, &PitchTemplateEditor::exportDraft);
    connect(m_delete, &QPushButton::clicked, this, [this] {
        if (confirmDelete ? confirmDelete(m_draft.name)
                          : ModalDialog::ask(this, tr("Delete template"),
                                             tr("Delete the template \"%1\" from the library? Maps that use it keep their copy.").arg(m_draft.name),
                                             tr("Delete")) == ModalDialog::Choice::Primary)
            emit deleteRequested();
    });
    layout->addWidget(buttons);
}

void PitchTemplateEditor::edit(const Pitch::Template &value, const MindMapModel *model, bool inLibrary) {
    m_draft = value;
    m_savedName = value.name;
    m_model = model;
    m_delete->setVisible(inLibrary);
    refresh();
}

// The fields from the draft; a value the draft leaves to the base theme shows the theme's, grey.
void PitchTemplateEditor::refresh() {
    m_updating = true;
    if (m_name->text().trimmed() != m_draft.name.trimmed()) m_name->setText(m_draft.name);
    m_base->setCurrentIndex(int(m_draft.base));
    if (m_model) {
        const Pitch::Look base = Pitch::look(*m_model, m_draft.base);
        for (int i = 0; i < kColorCount; ++i) {
            const QColor own = PitchChoices::templateColor(m_draft, i);
            NodeStyle::Reading reading;
            reading.empty = false;
            reading.value = own.isValid() ? own : PitchChoices::baseTemplateColor(base, i);
            reading.allManual = own.isValid();
            m_colors[i]->setReading(reading);
            m_colors[i]->setToolTip(own.isValid() ? tr("Set in the template") : tr("From the base theme"));
        }
    }
    const auto pickData = [](QComboBox *field, const QString &family) {
        int index = field->findData(family);
        if (index < 0) {  // a font this computer does not have
            field->addItem(tr("%1 (not installed)").arg(family), family);
            index = field->count() - 1;
        }
        field->setCurrentIndex(index);
    };
    pickData(m_titleFont, m_draft.titleFamily);
    pickData(m_itemFont, m_draft.itemFamily);
    m_itemStyle->setCurrentIndex(m_draft.itemStyle ? int(*m_draft.itemStyle) + 1 : 0);
    m_motif->setCurrentIndex(m_draft.motif ? int(*m_draft.motif) + 1 : 0);
    for (int i = 0; i < 3; ++i) {
        const bool has = !PitchChoices::templatePicture(m_draft, i).isEmpty();
        m_pictureState[i]->setText(has ? tr("Chosen") : tr("None"));
        m_removePicture[i]->setEnabled(has);
    }
    m_logoCorner->setCurrentIndex(int(m_draft.logoCorner));
    m_logoSize->setCurrentIndex(std::max(0, m_logoSize->findData(m_draft.logoSize / kLogoSizeStep * kLogoSizeStep)));
    if (m_footer->text() != m_draft.footer) m_footer->setText(m_draft.footer);
    {
        const QSignalBlocker blocker(m_numbers);
        m_numbers->setChecked(m_draft.numbers);
    }
    m_updating = false;
}

// The draft from the fields that have no handler of their own (texts, selects, the switch).
void PitchTemplateEditor::changed() {
    if (m_updating) return;
    m_draft.name = m_name->text().trimmed();  // empty: the old name comes back on Save
    m_draft.base = Pitch::Theme(m_base->currentIndex());
    m_draft.titleFamily = m_titleFont->currentData().toString();
    m_draft.itemFamily = m_itemFont->currentData().toString();
    const int style = m_itemStyle->currentIndex();
    m_draft.itemStyle = style > 0 ? std::optional(Pitch::ItemStyle(style - 1)) : std::nullopt;
    const int motif = m_motif->currentIndex();
    m_draft.motif = motif > 0 ? std::optional(Pitch::Motif(motif - 1)) : std::nullopt;
    m_draft.logoCorner = Pitch::Corner(m_logoCorner->currentIndex());
    m_draft.logoSize = m_logoSize->currentData().toInt();
    m_draft.footer = m_footer->text();
    m_draft.numbers = m_numbers->isChecked();
    refresh();
    emit draftChanged();
}

Pitch::Template PitchTemplateEditor::result() const {
    Pitch::Template value = m_draft;
    if (value.name.isEmpty()) value.name = m_savedName;
    return value;
}

void PitchTemplateEditor::choosePicture(int index) {
    const QString filter = tr("Pictures (*.png *.jpg *.jpeg *.bmp *.gif *.webp)");
    const QString path = pickFile ? pickFile(tr("Choose a picture"), filter, false, QString())
                                  : QFileDialog::getOpenFileName(this, tr("Choose a picture"), {}, filter);
    if (path.isEmpty()) return;
    QString error;
    const bool logo = PitchChoices::templatePictureIsLogo(index);
    const QByteArray data = Pitch::loadPicture(path, logo ? Pitch::kLogoMaxSide : Pitch::kPictureMaxSide, logo, &error);
    if (data.isEmpty()) {
        ModalDialog::inform(this, tr("Error"), tr("The picture could not be read: %1").arg(error));
        return;
    }
    PitchChoices::templatePicture(m_draft, index) = data;
    changed();
}

void PitchTemplateEditor::exportDraft() {
    const QString filter = tr("Pitch template (*.%1)").arg(QLatin1String(Pitch::kTemplateSuffix));
    const QString name = PitchChoices::templateFileName(result().name);
    QString path = pickFile ? pickFile(tr("Export template"), filter, true, name)
                            : QFileDialog::getSaveFileName(this, tr("Export template"), name, filter);
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QLatin1Char('.') + QLatin1String(Pitch::kTemplateSuffix);
    QString error;
    if (!PitchTemplates::writeFile(result(), path, &error))
        ModalDialog::inform(this, tr("Error"), tr("The template could not be exported: %1").arg(error));
}
