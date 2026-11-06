#include "pagedialog.h"
#include "help.h"

#include "exportframecontrols.h"
#include "pitchexport.h"
#include "segmentedcontrol.h"
#include "stylecontrols.h"
#include "toggleswitch.h"
#include "uitheme.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {
constexpr int kPageInsetPx = 16;  // room around the page for its shadow
}

// One page (of the plan, or a Pitch slide), fitted in the box on the dialog's surface, with a light
// shadow like the modals. Drawn once into m_cache when the page, the size or Monochrome change.
class PagePreview final : public QWidget {
public:
    explicit PagePreview(QWidget *parent) : QWidget(parent) {
        setFixedSize(PageDialog::kPreviewWidthPx, PageDialog::kPreviewHeightPx);
        setAttribute(Qt::WA_TranslucentBackground);
    }
    void showPage(const MapExporter::PageSource *plan, int index, bool monochrome) {
        if (!plan) show({}, {}, {}, false);
        else if (!plan->isValid()) show({}, {}, plan->error(), false);
        else show(plan->layout(index).fullRectPoints().size(), [plan, index](QPainter *painter, const QRectF &rect) { plan->draw(index, painter, rect); },
                  {}, monochrome);
    }
    // paper: the page's shape; draw paints it into a rect; message instead of a page when paper is empty.
    void show(const QSizeF &paper, std::function<void(QPainter *, const QRectF &)> draw, const QString &message, bool monochrome) {
        m_paper = paper;
        m_draw = std::move(draw);
        m_message = message;
        m_monochrome = monochrome;
        m_cache = {};
        update();
    }

protected:
    void resizeEvent(QResizeEvent *) override { m_cache = {}; }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(UiTheme::color(UiTheme::Token::SurfaceSunken));
        painter.drawRoundedRect(QRectF(rect()), ModalDialog::kRadiusPx, ModalDialog::kRadiusPx);
        if (m_paper.isEmpty() || !m_draw) {
            painter.setPen(UiTheme::color(UiTheme::Token::TextMuted));
            painter.drawText(rect().adjusted(kPageInsetPx, 0, -kPageInsetPx, 0), Qt::AlignCenter | Qt::TextWordWrap, m_message);
            return;
        }
        const QRectF page = pageRect();
        const int alphas[] = {10, 8, 6, 4};
        for (int i = 0; i < 4; ++i) {
            const qreal grow = i + 1;
            painter.setBrush(QColor(0, 0, 0, alphas[i] * 3));
            painter.drawRect(page.adjusted(-grow, -grow + 2, grow, grow + 2));
        }
        if (m_cache.isNull()) render(page.size());
        painter.drawImage(page.topLeft(), m_cache);
    }

private:
    QRectF pageRect() const {
        const QSizeF paper = m_paper;
        const QSizeF room = QSizeF(size()) - QSizeF(2 * kPageInsetPx, 2 * kPageInsetPx);
        const QSizeF shown = paper.scaled(room, Qt::KeepAspectRatio);
        return {QPointF((width() - shown.width()) / 2, (height() - shown.height()) / 2), shown};
    }
    void render(const QSizeF &shown) {
        const qreal ratio = devicePixelRatioF();
        m_cache = QImage((shown * ratio).toSize(), QImage::Format_ARGB32_Premultiplied);
        m_cache.setDevicePixelRatio(ratio);
        QPainter painter(&m_cache);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        m_draw(&painter, QRectF(QPointF(0, 0), shown));
        painter.end();
        if (m_monochrome) {  // what the grey PDF and the grey print look like
            m_cache = m_cache.convertToFormat(QImage::Format_Grayscale8);
            m_cache.setDevicePixelRatio(ratio);
        }
    }

    QSizeF m_paper;
    std::function<void(QPainter *, const QRectF &)> m_draw;
    QString m_message;
    bool m_monochrome = false;
    QImage m_cache;
};

PageDialog::PageDialog(QWidget *parent, Mode mode, const QString &documentName, bool branchAvailable, ExportDialog::PagesFor pages,
                       std::function<QString(const MindNode *)> originalImagePath, std::optional<PitchSource> pitch,
                       std::optional<GanttChoice> gantt)
    : ModalDialog(parent, mode == Mode::Pdf ? tr("Export PDF") : tr("Print"), mode == Mode::Pdf ? tr("Export") : tr("Print"), 880),
      m_mode(mode), m_documentName(documentName), m_pages(std::move(pages)), m_originalImagePath(std::move(originalImagePath)),
      m_pitch(mode == Mode::Pdf ? std::move(pitch) : std::nullopt), m_gantt(std::move(gantt)) {
    Help::setPage(this, QStringLiteral("files/export-pdf.md"));  // FEAT-092: F1 and "?" open it
    setObjectName(mode == Mode::Pdf ? QStringLiteral("PdfDialog") : QStringLiteral("PrintDialog"));
    // Set on the widgets themselves: added to the dialog's sheet they were left with the plain style.
    const QString pagerStyle = UiTheme::qss(QStringLiteral(
        "QToolButton { background:transparent; border:0; border-radius:4px; color:@text; min-width:24px; min-height:24px; }"
        "QToolButton:hover { background:@divider; }"));
    QSettings settings;

    auto *columns = new QHBoxLayout;
    columns->setSpacing(32);
    auto *left = new QVBoxLayout;
    left->setSpacing(8);
    m_preview = new PagePreview(this);
    left->addWidget(m_preview);
    auto *pager = new QHBoxLayout;
    pager->setSpacing(4);
    pager->addStretch();
    m_previous = new QToolButton(this);
    m_previous->setObjectName(QStringLiteral("PagePrevious"));
    m_previous->setText(QStringLiteral("‹"));
    m_previous->setStyleSheet(pagerStyle);
    m_previous->setAccessibleName(tr("Previous page"));
    m_pageNumber = new QLabel(this);
    m_pageNumber->setObjectName(QStringLiteral("PageNumber"));
    m_pageNumber->setStyleSheet(UiTheme::qss(QStringLiteral("QLabel { color:@textMuted; font-size:12px; }")));
    m_next = new QToolButton(this);
    m_next->setObjectName(QStringLiteral("PageNext"));
    m_next->setText(QStringLiteral("›"));
    m_next->setStyleSheet(pagerStyle);
    m_next->setAccessibleName(tr("Next page"));
    pager->addWidget(m_previous);
    pager->addWidget(m_pageNumber);
    pager->addWidget(m_next);
    pager->addStretch();
    left->addLayout(pager);
    left->addStretch();
    columns->addLayout(left);

    auto *right = new QVBoxLayout;
    right->setSpacing(16);
    m_kind = new SegmentedControl({tr("Mind map"), tr("Outliner")}, this);
    m_kind->setObjectName(QStringLiteral("PageKind"));
    if (m_gantt) m_ganttIndex = m_kind->addSegment(tr("Gantt"));  // FEAT-083
    if (m_pitch) m_pitchIndex = m_kind->addSegment(tr("Pitch"));
    m_kind->setCurrentIndex(m_gantt && m_gantt->preferred                                                   ? m_ganttIndex
                            : m_pitch && settings.value(settingsKey(QStringLiteral("pitch")), false).toBool() ? m_pitchIndex
                            : settings.value(settingsKey(QStringLiteral("outliner")), false).toBool()        ? 1
                                                                                                             : 0);
    m_lastKind = m_kind->currentIndex();
    right->addWidget(m_kind);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(12);
    grid->setColumnStretch(1, 1);
    const auto addRow = [this, grid](const QString &text, QWidget *control) {
        const int row = grid->rowCount();
        auto *label = new QLabel(text, this);
        grid->addWidget(label, row, 0);
        grid->addWidget(control, row, 1, Qt::AlignRight | Qt::AlignVCenter);
        control->setAccessibleName(text);
        m_rowLabels.insert(control, label);
    };
    if (mode == Mode::Print) {
        m_printer = makeDarkSelect(this);
        m_printer->setObjectName(QStringLiteral("PagePrinter"));
        m_printer->setMinimumWidth(240);
        const QStringList printers = QPrinterInfo::availablePrinterNames();
        m_printer->addItems(printers);
        const QString preferred = settings.value(settingsKey(QStringLiteral("printer")), QPrinterInfo::defaultPrinterName()).toString();
        if (printers.contains(preferred)) m_printer->setCurrentText(preferred);
        if (printers.isEmpty()) {
            m_printer->addItem(tr("No printer"));
            m_printer->setEnabled(false);
            acceptButton()->setEnabled(false);
        }
        addRow(tr("Printer"), m_printer);
        m_copies = new QSpinBox(this);
        m_copies->setObjectName(QStringLiteral("PageCopies"));
        m_copies->setRange(1, 99);
        m_copies->setFixedWidth(64);
        // Typed, or Up/Down and the wheel: the style's arrows are dark on the dark field.
        m_copies->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_copies->setAlignment(Qt::AlignCenter);
        m_copies->setStyleSheet(UiTheme::qss(QStringLiteral(
            "QSpinBox { background:@surfaceSunken; color:@text; border:1px solid @surfaceSunken; border-radius:4px; min-height:28px;"
            " padding:0 4px 0 8px; font-size:14px; selection-background-color:@accent; }"
            "QSpinBox:focus { border-color:@accent; }")));
        addRow(tr("Copies"), m_copies);
    }
    m_scope = new SegmentedControl({tr("Active sheet"), tr("All sheets"), tr("Selected branch")}, this);
    m_scope->setObjectName(QStringLiteral("PageScope"));
    m_scope->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_scope->setSegmentEnabled(int(Scope::Branch), branchAvailable);
    addRow(tr("Content"), m_scope);
    // FEAT-083: the Gantt's rows, shown on its tab in place of Content, Scale and Background.
    m_ganttContent = new SegmentedControl({tr("Gantt"), tr("Gantt and costs"), tr("Costs")}, this);
    m_ganttContent->setObjectName(QStringLiteral("PageGanttContent"));
    m_ganttContent->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_ganttContent->setCurrentIndex(std::clamp(settings.value(settingsKey(QStringLiteral("ganttContent")), 0).toInt(), 0, 2));
    addRow(tr("Content"), m_ganttContent);
    m_ganttUnit = new SegmentedControl({tr("Days"), tr("Weeks"), tr("Months")}, this);
    m_ganttUnit->setObjectName(QStringLiteral("PageGanttUnit"));
    m_ganttUnit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    const int viewUnit = m_gantt ? int(m_gantt->source.scale) : 0;
    m_ganttUnit->setCurrentIndex(std::clamp(settings.value(settingsKey(QStringLiteral("ganttUnit")), viewUnit).toInt(), 0, 2));
    addRow(tr("Time unit"), m_ganttUnit);
    m_ganttFit = new SegmentedControl({tr("Fit width"), tr("Actual size")}, this);
    m_ganttFit->setObjectName(QStringLiteral("PageGanttFit"));
    m_ganttFit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_ganttFit->setCurrentIndex(settings.value(settingsKey(QStringLiteral("ganttActualSize")), false).toBool() ? 1 : 0);
    addRow(tr("Scale"), m_ganttFit);
    m_size = makeDarkSelect(this);
    m_size->setObjectName(QStringLiteral("PageSize"));
    m_size->setMinimumWidth(180);
    using Size = MapExporter::PageSize;
    if (mode == Mode::Pdf) m_size->addItem(tr("Fit to map"), int(Size::FitToMap));
    const std::pair<QString, Size> paper[] = {{QStringLiteral("A3"), Size::A3}, {QStringLiteral("A4"), Size::A4}, {QStringLiteral("A5"), Size::A5},
                                              {QStringLiteral("Letter"), Size::Letter}, {QStringLiteral("Legal"), Size::Legal}};
    for (const auto &[label, size] : paper) m_size->addItem(label, int(size));
    const int storedSize = settings.value(settingsKey(QStringLiteral("size")), int(mode == Mode::Pdf ? Size::FitToMap : Size::A4)).toInt();
    m_size->setCurrentIndex(std::max(0, m_size->findData(storedSize)));
    addRow(tr("Page size"), m_size);
    m_orientation = new SegmentedControl({tr("Portrait"), tr("Landscape")}, this);
    m_orientation->setObjectName(QStringLiteral("PageOrientation"));
    m_orientation->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_orientation->setCurrentIndex(settings.value(settingsKey(QStringLiteral("landscape")), false).toBool() ? 1 : 0);
    addRow(tr("Orientation"), m_orientation);
    m_scale = new SegmentedControl({tr("Fit to page"), tr("Actual size")}, this);
    m_scale->setObjectName(QStringLiteral("PageScale"));
    m_scale->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_scale->setCurrentIndex(settings.value(settingsKey(QStringLiteral("actualSize")), false).toBool() ? 1 : 0);
    addRow(tr("Scale"), m_scale);
    m_background = new ToggleSwitch(this);
    m_background->setObjectName(QStringLiteral("PageBackground"));
    m_background->setChecked(settings.value(settingsKey(QStringLiteral("background")), true).toBool());
    addRow(tr("Background"), m_background);
    m_monochrome = new ToggleSwitch(this);
    m_monochrome->setObjectName(QStringLiteral("PageMonochrome"));
    m_monochrome->setChecked(settings.value(settingsKey(QStringLiteral("monochrome")), false).toBool());
    addRow(tr("Monochrome"), m_monochrome);
    m_frame = new ExportFrameControls(grid, this);  // FEAT-084: every tab, off when the window opens
    right->addLayout(grid);
    m_ganttNote = new QLabel(this);
    m_ganttNote->setObjectName(QStringLiteral("PageGanttNote"));
    m_ganttNote->setWordWrap(true);
    m_ganttNote->setStyleSheet(UiTheme::qss(QStringLiteral("QLabel { color:@textMuted; font-size:12px; }")));
    right->addWidget(m_ganttNote);
    right->addStretch();
    columns->addLayout(right, 1);
    contentLayout()->addLayout(columns);

    for (SegmentedControl *control : {m_kind, m_scope, m_orientation, m_scale, m_ganttContent, m_ganttUnit, m_ganttFit})
        connect(control, &SegmentedControl::segmentActivated, this, [this] { refreshPlan(); });
    // The orientation is the tab's: the Gantt on landscape paper by default, the map as it was.
    connect(m_kind, &SegmentedControl::segmentActivated, this, [this](int index) {
        if ((index == m_ganttIndex) == (m_lastKind == m_ganttIndex)) {
            m_lastKind = index;
            return;
        }
        QSettings settings;
        settings.setValue(settingsKey(m_lastKind == m_ganttIndex ? QStringLiteral("ganttLandscape") : QStringLiteral("landscape")),
                          m_orientation->currentIndex() == 1);
        const bool gantt = index == m_ganttIndex;
        m_orientation->setCurrentIndex(settings.value(settingsKey(gantt ? QStringLiteral("ganttLandscape") : QStringLiteral("landscape")), gantt).toBool() ? 1 : 0);
        m_lastKind = index;
        refreshPlan();
    });
    if (ganttChosen())
        m_orientation->setCurrentIndex(settings.value(settingsKey(QStringLiteral("ganttLandscape")), true).toBool() ? 1 : 0);
    connect(m_size, &QComboBox::currentIndexChanged, this, [this] { refreshPlan(); });
    connect(m_background, &ToggleSwitch::toggled, this, [this] { refreshPlan(); });
    connect(m_monochrome, &ToggleSwitch::toggled, this, [this] { showPage(m_page); });
    connect(m_frame, &ExportFrameControls::changed, this, [this] { refreshPlan(); });
    connect(m_previous, &QToolButton::clicked, this, [this] { showPage(m_page - 1); });
    connect(m_next, &QToolButton::clicked, this, [this] { showPage(m_page + 1); });
    // The dialog stays open on Accept: run() closes it once the file is written or the job sent.
    disconnect(acceptButton(), &QPushButton::clicked, this, &QDialog::accept);
    connect(acceptButton(), &QPushButton::clicked, this, &PageDialog::run);
    refreshPlan();
}

PageDialog::~PageDialog() = default;

QString PageDialog::settingsKey(const QString &name) const {
    return (m_mode == Mode::Pdf ? QStringLiteral("ExportPdf/") : QStringLiteral("Print/")) + name;
}

PageDialog::Scope PageDialog::scope() const { return static_cast<Scope>(m_scope->currentIndex()); }

bool PageDialog::pitchChosen() const { return m_pitch && m_kind->currentIndex() == m_pitchIndex; }

bool PageDialog::ganttChosen() const { return m_gantt && m_kind->currentIndex() == m_ganttIndex; }

GanttPages::Options PageDialog::ganttOptions() const {
    GanttPages::Options options;
    options.content = static_cast<GanttPages::Content>(m_ganttContent->currentIndex());
    options.size = static_cast<MapExporter::PageSize>(m_size->currentData().toInt());
    options.landscape = m_orientation->currentIndex() == 1;
    options.fitWidth = m_ganttFit->currentIndex() == 0;
    options.scale = static_cast<GanttChart::Scale>(m_ganttUnit->currentIndex());
    return options;
}

void PageDialog::setRowVisible(QWidget *control, bool visible) {
    control->setVisible(visible);
    if (QLabel *label = m_rowLabels.value(control)) label->setVisible(visible);
}

MapExporter::PageOptions PageDialog::options() const {
    MapExporter::PageOptions options;
    options.outliner = m_kind->currentIndex() == 1;
    options.size = static_cast<MapExporter::PageSize>(m_size->currentData().toInt());
    options.landscape = m_orientation->currentIndex() == 1;
    options.actualSize = m_scale->currentIndex() == 1;
    options.background = m_background->isChecked();
    options.originalImagePath = m_originalImagePath;
    return options;
}

void PageDialog::syncControls() {
    // FEAT-083: the Gantt's rows on its tab, the map's elsewhere.
    const bool gantt = ganttChosen();
    for (QWidget *row : {static_cast<QWidget *>(m_scope), static_cast<QWidget *>(m_scale), static_cast<QWidget *>(m_background)})
        setRowVisible(row, !gantt);
    for (QWidget *row : {static_cast<QWidget *>(m_ganttContent), static_cast<QWidget *>(m_ganttUnit), static_cast<QWidget *>(m_ganttFit)})
        setRowVisible(row, gantt);
    // Shown with a text only: empty and word-wrapped, the layout asked it a height of -1 ("Negative
    // sizes (0,-1)" in the output, user 2026-10-31).
    m_ganttNote->setVisible(gantt && !m_ganttNote->text().isEmpty());
    const bool chart = m_ganttContent->currentIndex() != int(GanttPages::Content::Costs);
    m_ganttUnit->setEnabled(chart);
    // A page of the Gantt's size has no width to fit.
    m_ganttFit->setEnabled(chart && m_size->currentData().toInt() != int(MapExporter::PageSize::FitToMap));
    // Pitch: the slides keep the presentation's theme, ratio and colours; the page rows do not apply.
    const bool pitch = pitchChosen();
    for (QWidget *row : {static_cast<QWidget *>(m_scope), static_cast<QWidget *>(m_size), static_cast<QWidget *>(m_monochrome)})
        row->setEnabled(!pitch);
    if (pitch) {
        m_orientation->setEnabled(false);
        m_scale->setEnabled(false);
        m_background->setEnabled(false);
        return;
    }
    const bool outliner = m_kind->currentIndex() == 1;
    // "Fit to map" belongs to the map PDF and the Gantt's (a page of its size): the outline goes on
    // paper of a fixed size.
    if (auto *model = qobject_cast<QStandardItemModel *>(m_size->model()))
        if (QStandardItem *fit = model->item(0); fit && m_mode == Mode::Pdf) fit->setEnabled(!outliner);
    if (outliner && m_size->currentData().toInt() == int(MapExporter::PageSize::FitToMap)) {
        const QSignalBlocker blocker(m_size);
        m_size->setCurrentIndex(m_size->findData(int(MapExporter::PageSize::A4)));
    }
    const bool fitToMap = m_size->currentData().toInt() == int(MapExporter::PageSize::FitToMap);
    m_orientation->setEnabled(!fitToMap);
    m_scale->setEnabled(!fitToMap && !outliner);
    m_background->setEnabled(!outliner);
}

void PageDialog::applyPreset(const Preset &preset) {
    // As a click: the window rebuilds its rows for the kind the same way.
    if (preset.kind) {
        const int index = *preset.kind == QLatin1String("map") ? 0 : *preset.kind == QLatin1String("outliner") ? 1
                        : *preset.kind == QLatin1String("gantt") ? m_ganttIndex : *preset.kind == QLatin1String("pitch") ? m_pitchIndex : -1;
        if (index >= 0) {
            m_kind->setCurrentIndex(index);
            emit m_kind->segmentActivated(index);
        }
    }
    if (preset.scope && m_scope) {
        m_scope->setCurrentIndex(*preset.scope);
        emit m_scope->segmentActivated(*preset.scope);
    }
    if (preset.background && m_background) m_background->setChecked(*preset.background);
    if (preset.frame && m_frame) m_frame->setFrame(*preset.frame);
    refreshPlan();
}

void PageDialog::refreshPlan() {
    syncControls();
    if (pitchChosen()) {
        m_plan.reset();
        acceptButton()->setEnabled(!m_pitch->slides.isEmpty());
        showPage(0);
        return;
    }
    m_preview->showPage(nullptr, 0, false);  // before the old plan goes
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (ganttChosen()) {
        const GanttPages::Options chosen = ganttOptions();
        auto pages = std::make_unique<GanttPages>(m_gantt->source, chosen);
        // Say what fit width did: another unit, or the timeline over pages in width.
        QStringList note;
        if (chosen.content != GanttPages::Content::Costs && pages->isValid()) {
            const QString units[] = {tr("days"), tr("weeks"), tr("months")};
            if (pages->chosenScale() != chosen.scale)
                note.append(tr("In %1, to fit the page width with legible text.").arg(units[int(pages->chosenScale())]));
            if (pages->splitInWidth()) note.append(tr("The timeline goes on more pages in width; the names repeat on each."));
        }
        m_ganttNote->setText(note.join(QLatin1Char(' ')));
        m_ganttNote->setVisible(!note.isEmpty());
        m_plan = std::move(pages);
    } else {
        m_ganttNote->clear();
        m_ganttNote->hide();
        m_plan = std::make_unique<MapExporter::PagePlan>(m_pages(scope()), options());
    }
    if (const ExportFrame frame = m_frame->frame(); frame.enabled())  // FEAT-084
        m_plan = std::make_unique<MapExporter::FramedPages>(std::move(m_plan), frame);
    QApplication::restoreOverrideCursor();
    if (m_mode == Mode::Pdf || m_printer->isEnabled()) acceptButton()->setEnabled(m_plan->isValid());
    showPage(0);
}

void PageDialog::showPage(int index) {
    const bool pitch = pitchChosen();
    const int count = pitch ? int(m_pitch->slides.size()) : m_plan && m_plan->isValid() ? m_plan->count() : 0;
    m_page = std::clamp(index, 0, std::max(0, count - 1));
    if (!pitch) {
        m_preview->showPage(m_plan.get(), m_page, m_monochrome->isChecked());
    } else if (count == 0) {
        m_preview->show({}, {}, tr("No slides: every slide of this sheet is hidden"), false);
    } else {
        const Pitch::Slide &slide = m_pitch->slides[m_page];
        const Pitch::Look &look = m_pitch->look;
        const ExportFrame frame = m_frame->frame();
        m_preview->show(QSizeF(m_pitch->ratio, 1),
                        [&slide, &look, frame](QPainter *painter, const QRectF &rect) {
                            // The page's pixels as the PDF's (254 mm on the long side): the frame's width the same.
                            const qreal px = std::max(rect.width(), rect.height()), pagePx = 254 / 25.4 * 96;
                            painter->save();
                            painter->translate(rect.topLeft());
                            painter->scale(px / pagePx, px / pagePx);
                            PitchExport::paintFramed(painter, QRectF(QPointF(0, 0), rect.size() * pagePx / px), slide, look, frame);
                            painter->restore();
                        },
                        {}, false);
    }
    m_pageNumber->setText(count ? QStringLiteral("%1/%2").arg(m_page + 1).arg(count) : QString());
    m_previous->setEnabled(m_page > 0);
    m_next->setEnabled(m_page + 1 < count);
}

void PageDialog::storeChoices() const {
    QSettings settings;
    settings.setValue(settingsKey(QStringLiteral("outliner")), m_kind->currentIndex() == 1);
    settings.setValue(settingsKey(QStringLiteral("pitch")), pitchChosen());
    settings.setValue(settingsKey(QStringLiteral("size")), m_size->currentData().toInt());
    settings.setValue(settingsKey(ganttChosen() ? QStringLiteral("ganttLandscape") : QStringLiteral("landscape")), m_orientation->currentIndex() == 1);
    settings.setValue(settingsKey(QStringLiteral("ganttContent")), m_ganttContent->currentIndex());
    settings.setValue(settingsKey(QStringLiteral("ganttUnit")), m_ganttUnit->currentIndex());
    settings.setValue(settingsKey(QStringLiteral("ganttActualSize")), m_ganttFit->currentIndex() == 1);
    settings.setValue(settingsKey(QStringLiteral("actualSize")), m_scale->currentIndex() == 1);
    settings.setValue(settingsKey(QStringLiteral("background")), m_background->isChecked());
    settings.setValue(settingsKey(QStringLiteral("monochrome")), m_monochrome->isChecked());
    if (m_printer && m_printer->isEnabled()) settings.setValue(settingsKey(QStringLiteral("printer")), m_printer->currentText());
}

void PageDialog::run() {
    if (pitchChosen() ? m_pitch->slides.isEmpty() : !m_plan || !m_plan->isValid()) return;
    const QString name = m_documentName.isEmpty() ? tr("Untitled") : m_documentName;
    if (m_mode == Mode::Pdf) {
        static const QRegularExpression notAllowed(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
        QString folder = QSettings().value(settingsKey(QStringLiteral("lastDir"))).toString();
        if (folder.isEmpty() || !QFileInfo(folder).isDir()) folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString suggested = QDir(folder).filePath(QString(name).replace(notAllowed, QStringLiteral("-")) + QStringLiteral(".pdf"));
        QString path = pickPath ? pickPath(suggested)
                                : QFileDialog::getSaveFileName(this, tr("Export PDF"), suggested, tr("PDF document (*.pdf)"));
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".pdf");
        storeChoices();
        QSettings().setValue(settingsKey(QStringLiteral("lastDir")), QFileInfo(path).absolutePath());
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString error;
        if (pitchChosen())
            error = PitchExport::writePdf(path, m_pitch->slides, m_pitch->look, m_pitch->ratio, QFileInfo(path).completeBaseName(),
                                          m_frame->frame());
        else if (const MapExporter::Result result = MapExporter::writePdf(*m_plan, path, QFileInfo(path).completeBaseName(), m_monochrome->isChecked());
                 !result.ok)
            error = result.error;
        QApplication::restoreOverrideCursor();
        if (!error.isEmpty()) {
            ModalDialog::inform(this, tr("Error"), error);
            return;
        }
        m_outcome = tr("Exported to %1").arg(QDir::toNativeSeparators(path));
        accept();
        return;
    }
    storeChoices();
    QPrinter printer(QPrinter::HighResolution);
    printer.setPrinterName(m_printer->currentText());
    printer.setCopyCount(m_copies->value());
    printer.setFullPage(true);  // the plan places the content with its own margins
    printer.setColorMode(m_monochrome->isChecked() ? QPrinter::GrayScale : QPrinter::Color);
    printer.setDocName(name);
    if (!printer.isValid()) {
        ModalDialog::inform(this, tr("Error"), tr("The printer \"%1\" is not available.").arg(m_printer->currentText()));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const MapExporter::Result result = MapExporter::paint(*m_plan, &printer);
    QApplication::restoreOverrideCursor();
    if (!result.ok) {
        ModalDialog::inform(this, tr("Error"), result.error);
        return;
    }
    m_outcome = tr("Sent to the printer %1").arg(m_printer->currentText());
    accept();
}
