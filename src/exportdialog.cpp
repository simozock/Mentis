#include "exportdialog.h"
#include "help.h"

#include "exportframecontrols.h"
#include "segmentedcontrol.h"
#include "toggleswitch.h"
#include "uitheme.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kPreviewInsetPx = 16;
constexpr int kCheckerPx = 8;
const QString kScaleKey = QStringLiteral("Export/scale");
const QString kBackgroundKey = QStringLiteral("Export/background");
const QString kLastDirKey = QStringLiteral("Export/lastDir");
const QString kGanttContentKey = QStringLiteral("Export/ganttContent");

QString extension(MapExporter::Format format) {
    switch (format) {
    case MapExporter::Format::Png: return QStringLiteral("png");
    case MapExporter::Format::Svg: return QStringLiteral("svg");
    }
    return {};
}
}  // namespace

// The page fitted in a rounded frame: on a checkerboard without background (like Excalidraw), on the
// sheet's color with it. The map is drawn once into m_cache when the page or the size changes.
class ExportPreview final : public QWidget {
public:
    explicit ExportPreview(QWidget *parent) : QWidget(parent) {
        setFixedSize(ExportDialog::kPreviewWidthPx, ExportDialog::kPreviewHeightPx);
        setAttribute(Qt::WA_TranslucentBackground);
    }
    // FEAT-084: the frame on the picture's edge, as the file will have it.
    void setFrame(const ExportFrame &frame) {
        m_frame = frame;
        m_cache = {};
        update();
    }
    void showPage(const MapExporter::Rendering *rendering, bool background) {
        if (rendering != m_rendering || m_gantt) m_cache = {};
        m_rendering = rendering;
        m_gantt = nullptr;
        m_background = background;
        update();
    }
    // FEAT-083: the Gantt's picture, on white.
    void showGantt(const GanttPages *pages) {
        m_cache = {};
        m_rendering = nullptr;
        m_gantt = pages;
        m_background = true;
        update();
    }

protected:
    void resizeEvent(QResizeEvent *) override { m_cache = {}; }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(frame, ModalDialog::kRadiusPx, ModalDialog::kRadiusPx);
        painter.save();
        painter.setClipPath(clip);
        const bool gantt = m_gantt && m_gantt->isValid();
        const bool valid = (m_rendering && m_rendering->isValid()) || gantt;
        if (gantt) {
            painter.fillRect(rect(), Qt::white);
        } else if (valid && m_background) {
            painter.fillRect(rect(), m_rendering->background());
        } else {
            painter.fillRect(rect(), UiTheme::color(UiTheme::Token::SurfaceRaised));
            const QColor dark = UiTheme::color(UiTheme::Token::SurfaceSunken);
            for (int y = 0; y < height(); y += kCheckerPx)
                for (int x = (y / kCheckerPx) % 2 * kCheckerPx; x < width(); x += 2 * kCheckerPx)
                    painter.fillRect(x, y, kCheckerPx, kCheckerPx, dark);
        }
        if (valid) {
            if (m_cache.isNull()) render();
            painter.drawImage(QPointF(kPreviewInsetPx, kPreviewInsetPx), m_cache);
        } else if (m_rendering || m_gantt) {
            painter.setPen(UiTheme::color(UiTheme::Token::TextMuted));
            painter.drawText(rect().adjusted(kPreviewInsetPx, 0, -kPreviewInsetPx, 0), Qt::AlignCenter | Qt::TextWordWrap,
                             m_gantt ? m_gantt->error() : m_rendering->error());
        }
        painter.restore();
        painter.setPen(QPen(UiTheme::color(UiTheme::Token::Divider), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(frame, ModalDialog::kRadiusPx, ModalDialog::kRadiusPx);
    }

private:
    // Without the background: the frame draws it, so switching it does not draw the map again. A small
    // map stays at 100% (centred) instead of being enlarged to fill the frame.
    void render() {
        const QSizeF inner = QSizeF(size()) - QSizeF(2 * kPreviewInsetPx, 2 * kPreviewInsetPx);
        const qreal band = m_frame.width();
        const QSizeF content = m_gantt ? m_gantt->imageSize() : m_rendering->source().size();
        const QSizeF source = content + QSizeF(2 * band, 2 * band);
        const qreal zoom = std::min({1.0, inner.width() / source.width(), inner.height() / source.height()});
        const QSizeF shown = source * zoom;
        const qreal ratio = devicePixelRatioF();
        m_cache = QImage((inner * ratio).toSize(), QImage::Format_ARGB32_Premultiplied);
        m_cache.setDevicePixelRatio(ratio);
        m_cache.fill(Qt::transparent);
        QPainter painter(&m_cache);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        const QPointF at((inner.width() - shown.width()) / 2, (inner.height() - shown.height()) / 2);
        const QRectF place(at + QPointF(band, band) * zoom, content * zoom);
        if (m_gantt) {
            painter.fillRect(QRectF(at, shown), Qt::white);
            m_gantt->drawImage(&painter, place);
        } else {
            if (band > 0 && m_background) painter.fillRect(QRectF(at, shown), m_rendering->background());
            m_rendering->draw(&painter, place, false);
        }
        painter.translate(at);
        painter.scale(zoom, zoom);
        m_frame.paint(&painter, QRectF(QPointF(0, 0), source));
    }

    ExportFrame m_frame;

    const MapExporter::Rendering *m_rendering = nullptr;
    const GanttPages *m_gantt = nullptr;
    bool m_background = true;
    QImage m_cache;
};

ExportDialog::ExportDialog(QWidget *parent, const QString &documentName, bool branchAvailable, PagesFor pages,
                           std::function<QString(const MindNode *)> originalImagePath, std::optional<GanttExportSource> gantt,
                           bool preferGantt)
    : ModalDialog(parent, tr("Export image"), QString(), 1000), m_pages(std::move(pages)), m_originalImagePath(std::move(originalImagePath)),
      m_gantt(std::move(gantt)) {
    Help::setPage(this, QStringLiteral("files/export-image.md"));  // FEAT-092: F1 and "?" open it
    setObjectName(QStringLiteral("ExportDialog"));
    useOwnButtons();
    // The four buttons look like the modal's main button (approved exception: no "Cancel").
    setStyleSheet(styleSheet() + UiTheme::qss(QStringLiteral(
        "QPushButton[exportButton=\"true\"] { background:@accent; color:@text; border:0; font-weight:600; padding:0 16px; }"
        "QPushButton[exportButton=\"true\"]:disabled { background:@textMuted; color:@surface; }")));

    auto *columns = new QHBoxLayout;
    columns->setSpacing(32);
    auto *left = new QVBoxLayout;
    left->setSpacing(16);
    m_preview = new ExportPreview(this);
    left->addWidget(m_preview);
    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("ExportFileName"));
    m_name->setAccessibleName(tr("File name"));
    m_name->setFixedWidth(300);
    // The suggested name (MapExporter::defaultFileName), editable.
    m_name->setText(documentName.isEmpty() ? MapExporter::defaultFileName({}, QDateTime::currentDateTime()) : documentName);
    left->addWidget(m_name, 0, Qt::AlignHCenter);
    left->addStretch();
    columns->addLayout(left);

    auto *right = new QVBoxLayout;
    right->setSpacing(16);
    if (m_gantt) {  // FEAT-083
        m_kind = new SegmentedControl({tr("Mind map"), tr("Gantt")}, this);
        m_kind->setObjectName(QStringLiteral("ExportKind"));
        m_kind->setCurrentIndex(preferGantt ? 1 : 0);
        right->addWidget(m_kind);
    }
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(16);
    grid->setColumnStretch(1, 1);
    QList<QPair<QWidget *, QWidget *>> *rows = &m_mapRows;  // the rows of the tab being built
    const auto addRow = [this, grid, &rows](const QString &text, QWidget *control) {
        const int row = grid->rowCount();
        auto *label = new QLabel(text, this);
        grid->addWidget(label, row, 0);
        grid->addWidget(control, row, 1, Qt::AlignRight | Qt::AlignVCenter);
        control->setAccessibleName(text);
        if (rows) rows->append({label, control});
    };
    m_scope = new SegmentedControl({tr("Active sheet"), tr("All sheets"), tr("Selected branch")}, this);
    m_scope->setObjectName(QStringLiteral("ExportScope"));
    m_scope->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_scope->setSegmentEnabled(int(Scope::Branch), branchAvailable);
    addRow(tr("Content"), m_scope);
    QSettings settings;
    m_background = new ToggleSwitch(this);
    m_background->setObjectName(QStringLiteral("ExportBackground"));
    m_background->setChecked(settings.value(kBackgroundKey, true).toBool());
    addRow(tr("Background"), m_background);
    rows = &m_ganttRows;
    // FEAT-083: the chart, the chart and its cost tables under it, or the cost tables.
    m_content = new SegmentedControl({tr("Gantt"), tr("Gantt and costs"), tr("Costs")}, this);
    m_content->setObjectName(QStringLiteral("ExportGanttContent"));
    m_content->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_content->setCurrentIndex(std::clamp(settings.value(kGanttContentKey, 0).toInt(), 0, 2));
    addRow(tr("Content"), m_content);
    m_unit = new SegmentedControl({tr("Days"), tr("Weeks"), tr("Months")}, this);
    m_unit->setObjectName(QStringLiteral("ExportGanttUnit"));
    m_unit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_unit->setCurrentIndex(m_gantt ? int(m_gantt->scale) : 0);
    addRow(tr("Time unit"), m_unit);
    rows = nullptr;
    m_scale = new SegmentedControl({QStringLiteral("1×"), QStringLiteral("2×"), QStringLiteral("3×")}, this);
    m_scale->setObjectName(QStringLiteral("ExportScale"));
    m_scale->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_scale->setToolTip(tr("Size of the PNG image"));
    m_scale->setCurrentIndex(std::clamp(settings.value(kScaleKey, 2).toInt(), 1, 3) - 1);
    addRow(tr("Scale"), m_scale);
    m_frame = new ExportFrameControls(grid, this);  // FEAT-084: on both tabs, off when the window opens
    right->addLayout(grid);
    right->addStretch();

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch();
    const auto addButton = [this, buttons](const QString &text, const QString &icon) {
        auto *button = new QPushButton(QIcon(icon), text, this);
        button->setProperty("exportButton", true);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);  // the row never cuts a label
        button->setIconSize(QSize(16, 16));
        button->setAutoDefault(false);
        buttons->addWidget(button);
        return button;
    };
    const QString download = QStringLiteral(":/assets/hugeicons/export-download.svg");
    connect(addButton(QStringLiteral("PNG"), download), &QPushButton::clicked, this, [this] { exportAs(MapExporter::Format::Png); });
    connect(addButton(QStringLiteral("SVG"), download), &QPushButton::clicked, this, [this] { exportAs(MapExporter::Format::Svg); });
    m_copy = addButton(tr("Copy to clipboard"), QStringLiteral(":/assets/hugeicons/export-copy.svg"));
    connect(m_copy, &QPushButton::clicked, this, &ExportDialog::copyToClipboard);
    right->addLayout(buttons);
    columns->addLayout(right, 1);
    contentLayout()->addLayout(columns);

    connect(m_scope, &SegmentedControl::segmentActivated, this, [this] { refreshPreview(); });
    connect(m_unit, &SegmentedControl::segmentActivated, this, [this] { refreshPreview(); });
    connect(m_content, &SegmentedControl::segmentActivated, this, [this] { refreshPreview(); });
    connect(m_frame, &ExportFrameControls::changed, this, [this] { m_preview->setFrame(m_frame->frame()); });
    if (m_kind) connect(m_kind, &SegmentedControl::segmentActivated, this, [this] { refreshPreview(); });
    connect(m_background, &ToggleSwitch::toggled, this, [this](bool on) { m_preview->showPage(m_rendering.get(), on); });
    refreshPreview();
}

ExportDialog::~ExportDialog() = default;

ExportDialog::Scope ExportDialog::scope() const { return static_cast<Scope>(m_scope->currentIndex()); }

bool ExportDialog::ganttChosen() const { return m_kind && m_kind->currentIndex() == 1; }

GanttChart::Scale ExportDialog::ganttUnit() const { return static_cast<GanttChart::Scale>(m_unit->currentIndex()); }

GanttPages::Content ExportDialog::ganttContent() const { return static_cast<GanttPages::Content>(m_content->currentIndex()); }

MapExporter::Options ExportDialog::options() const {
    MapExporter::Options options;
    options.background = m_background->isChecked();
    options.scale = m_scale->currentIndex() + 1;
    options.originalImagePath = m_originalImagePath;
    options.frame = m_frame->frame();
    return options;
}

void ExportDialog::refreshPreview() {
    const bool gantt = ganttChosen();
    for (const auto &[label, control] : std::as_const(m_mapRows)) {
        label->setVisible(!gantt);
        control->setVisible(!gantt);
    }
    for (const auto &[label, control] : std::as_const(m_ganttRows)) {
        label->setVisible(gantt);
        control->setVisible(gantt);
    }
    if (gantt) {  // FEAT-083: one picture of the active sheet's table and chart
        m_preview->showPage(nullptr, true);
        GanttPages::Options options;
        options.singlePage = true;
        options.fitWidth = false;
        options.scale = ganttUnit();
        options.content = ganttContent();
        m_unit->setEnabled(options.content != GanttPages::Content::Costs);
        m_ganttPage = std::make_unique<GanttPages>(*m_gantt, options);
        m_preview->showGantt(m_ganttPage.get());
        m_copy->setEnabled(m_ganttPage->isValid());
        return;
    }
    // "All sheets" shows the active sheet; one file per sheet comes out of it.
    const QList<MapExporter::Page> pages = m_pages(scope() == Scope::AllSheets ? Scope::ActiveSheet : scope());
    m_preview->showPage(nullptr, m_background->isChecked());  // before the old page goes
    m_rendering = pages.isEmpty() ? nullptr : std::make_unique<MapExporter::Rendering>(pages.front(), 1, m_originalImagePath);
    m_preview->showPage(m_rendering.get(), m_background->isChecked());
    m_copy->setEnabled(scope() != Scope::AllSheets);
}

QString ExportDialog::suggestedPath(MapExporter::Format format) const {
    static const QRegularExpression notAllowed(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"));
    QString name = m_name->text().trimmed().replace(notAllowed, QStringLiteral("-"));
    if (name.isEmpty()) name = tr("Untitled");
    QString folder = QSettings().value(kLastDirKey).toString();
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(folder).filePath(name + QLatin1Char('.') + extension(format));
}

void ExportDialog::applyPreset(const Preset &preset) {
    // As a click: the window reacts to the segment the same way (rows shown, preview).
    if (preset.scope) {
        m_scope->setCurrentIndex(int(*preset.scope));
        emit m_scope->segmentActivated(int(*preset.scope));
    }
    if (preset.gantt && m_kind) {
        m_kind->setCurrentIndex(*preset.gantt ? 1 : 0);
        emit m_kind->segmentActivated(*preset.gantt ? 1 : 0);
    }
    if (preset.background) m_background->setChecked(*preset.background);
    if (preset.scale) m_scale->setCurrentIndex(std::clamp(*preset.scale, 1, 3) - 1);
    if (preset.frame) m_frame->setFrame(*preset.frame);
    refreshPreview();
}

void ExportDialog::storeChoices() const {
    QSettings settings;
    settings.setValue(kScaleKey, m_scale->currentIndex() + 1);
    settings.setValue(kBackgroundKey, m_background->isChecked());
    settings.setValue(kGanttContentKey, m_content->currentIndex());
}

void ExportDialog::exportAs(MapExporter::Format format) {
    const QString suggested = suggestedPath(format);
    QString path;
    if (pickPath) {
        path = pickPath(format, suggested);
    } else {
        QString title, filter;
        switch (format) {
        case MapExporter::Format::Png: title = tr("Export PNG"); filter = tr("PNG image (*.png)"); break;
        case MapExporter::Format::Svg: title = tr("Export SVG"); filter = tr("SVG image (*.svg)"); break;
        }
        path = QFileDialog::getSaveFileName(this, title, suggested, filter);
    }
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QLatin1Char('.') + extension(format);
    storeChoices();
    QSettings().setValue(kLastDirKey, QFileInfo(path).absolutePath());
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const MapExporter::Result result = ganttChosen() ? GanttPages::write(format, *m_gantt, ganttUnit(), path, m_scale->currentIndex() + 1, ganttContent(), m_frame->frame())
                                                     : MapExporter::write(format, m_pages(scope()), path, options());
    QApplication::restoreOverrideCursor();
    if (!result.ok) {
        ModalDialog::inform(this, tr("Error"), result.error);
        return;
    }
    m_outcome = result.written.size() == 1 ? tr("Exported to %1").arg(QDir::toNativeSeparators(result.written.front()))
                                           : tr("Exported %1 files to %2").arg(result.written.size())
                                                 .arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
    if (result.scaleReduced) m_outcome += QLatin1Char(' ') + tr("(image too large: drawn at a smaller scale)");
    accept();
}

void ExportDialog::copyToClipboard() {
    const QList<MapExporter::Page> pages = ganttChosen() ? QList<MapExporter::Page>() : m_pages(scope());
    if (pages.isEmpty() && !ganttChosen()) return;
    storeChoices();
    QString error;
    bool reduced = false;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QImage image = ganttChosen() ? GanttPages::toImage(*m_gantt, ganttUnit(), m_scale->currentIndex() + 1, &reduced, &error, ganttContent(), m_frame->frame())
                                       : MapExporter::toImage(pages.front(), options(), &reduced, &error);
    QApplication::restoreOverrideCursor();
    if (image.isNull()) {
        ModalDialog::inform(this, tr("Error"), error);
        return;
    }
    QApplication::clipboard()->setImage(image);
    m_outcome = tr("Image copied to the clipboard");
    if (reduced) m_outcome += QLatin1Char(' ') + tr("(image too large: drawn at a smaller scale)");
    accept();
}
