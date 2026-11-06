#include "ganttexport.h"

#include <QDateTime>
#include <QDir>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QFontMetricsF>
#include <QLocale>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double kPxToPt = 0.75;  // 96 px per inch on 72 pt
constexpr int kRowPx = GanttChart::kRowHeight;
constexpr int kHeaderPx = GanttChart::kHeaderHeight;
constexpr double kCellPadPx = 8;
constexpr double kIndentPx = 16;
constexpr double kNameMinPx = 120;
constexpr double kNameMaxPx = 280;
constexpr double kOtherMaxPx = 200;
constexpr double kCostTitlePx = 40;
constexpr double kCostGapPx = 24;
constexpr double kHoursPx = 100;
constexpr double kAmountPx = 140;

// Paper colours (the light Gantt's): text, muted text, header line, row line, the table's edge.
const QColor kText(0x1f, 0x29, 0x37);
const QColor kMuted(0x6b, 0x72, 0x80);
const QColor kHeaderLine(0xe5, 0xe7, 0xeb);
const QColor kRowLine(10, 30, 70, 20);
const QColor kLater(0xe5, 0x48, 0x4d);
const QColor kEarlier(0x30, 0xa4, 0x6c);
const QColor kSame(0x9a, 0xa2, 0xac);
const QColor kSummaryFill(0xf4, 0xf5, 0xf7);  // the summaries' rows of the cost table

QFont textFont(bool bold = false, int pixels = 12) {
    QFont font;
    font.setPixelSize(pixels);
    font.setBold(bold);
    return font;
}

// The base day width of each scale at 100 % (GanttChart::dayWidth) and the narrowest legible one:
// a day's number, a week's "W45", a month's short name.
int baseDayWidth(GanttChart::Scale scale) {
    return scale == GanttChart::Scale::Days ? 28 : scale == GanttChart::Scale::Weeks ? 12 : 4;
}
bool legible(GanttChart::Scale scale, int dayWidth) {
    switch (scale) {
    case GanttChart::Scale::Days: return dayWidth >= 18;
    case GanttChart::Scale::Weeks: return dayWidth * 7 >= 28;
    case GanttChart::Scale::Months: return dayWidth * 30 >= 36;
    }
    return true;
}
int dayWidthAt(GanttChart::Scale scale, int zoom) { return std::max(2, int(std::lround(baseDayWidth(scale) * zoom / 100.0))); }

// The timeline's ends aligned to the unit: weeks from Monday to Sunday, months whole.
void align(GanttChart::Scale scale, QDate &first, QDate &last) {
    if (scale == GanttChart::Scale::Weeks) {
        first = first.addDays(1 - first.dayOfWeek());
        last = last.addDays(7 - last.dayOfWeek());
    } else if (scale == GanttChart::Scale::Months) {
        first = QDate(first.year(), first.month(), 1);
        last = QDate(last.year(), last.month(), last.daysInMonth());
    }
}

}  // namespace

QString GanttPages::hoursText(qint64 minutes) {
    const qint64 hours = minutes / 60, rest = minutes % 60;
    const QString whole = QLocale().toString(hours);
    return rest ? QStringLiteral("%1 h %2").arg(whole).arg(rest, 2, 10, QLatin1Char('0')) : QStringLiteral("%1 h").arg(whole);
}

QString GanttPages::amountText(qint64 cents, const QString &currency) {
    return QLocale().toCurrencyString(double(cents) / 100.0, currency, 2);
}

namespace {
GanttPages::Options imageOptions(GanttChart::Scale unit) {
    GanttPages::Options options;
    options.singlePage = true;
    options.fitWidth = false;
    options.scale = unit;
    return options;
}
QString writeError(const QString &path, const QString &reason) {
    return GanttPages::tr("The file %1 could not be written: %2").arg(QDir::toNativeSeparators(path), reason);
}
}  // namespace

QImage GanttPages::toImage(const GanttExportSource &source, GanttChart::Scale unit, int scale, bool *reduced, QString *error,
                           Content content, const ExportFrame &frame) {
    const double band = frame.width();
    if (reduced) *reduced = false;
    for (int u = int(unit); u <= int(GanttChart::Scale::Months); ++u) {
        GanttPages::Options options = imageOptions(GanttChart::Scale(u));
        options.content = content;
        const GanttPages pages(source, options);
        if (!pages.isValid()) {
            if (error) *error = pages.error();
            return {};
        }
        const QSizeF size = pages.imageSize() + QSizeF(2 * band, 2 * band);
        int times = std::clamp(scale, 1, 3);
        while (times > 1 && std::max(size.width(), size.height()) * times > MapExporter::kMaxPngSidePx) --times;
        if (std::max(size.width(), size.height()) * times > MapExporter::kMaxPngSidePx) continue;  // the next unit
        if (reduced) *reduced = times != scale || u != int(unit);
        QImage image((size * times).toSize(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);  // the Gantt is on white paper
        QPainter painter(&image);
        pages.drawImage(&painter, QRectF(QPointF(band, band) * times, pages.imageSize() * times));
        painter.scale(times, times);
        frame.paint(&painter, QRectF(QPointF(0, 0), size));
        return image;
    }
    if (error) *error = tr("The Gantt is too large for an image: export it as a PDF.");
    return {};
}

MapExporter::Result GanttPages::write(MapExporter::Format format, const GanttExportSource &source, GanttChart::Scale unit,
                                      const QString &path, int scale, Content content, const ExportFrame &frame) {
    MapExporter::Result result;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {false, writeError(path, file.errorString())};
    if (format == MapExporter::Format::Png) {
        QString error;
        const QImage image = toImage(source, unit, scale, &result.scaleReduced, &error, content, frame);
        if (image.isNull()) return {false, error};
        if (!image.save(&file, "PNG")) return {false, writeError(path, file.errorString())};
    } else {
        GanttPages::Options options = imageOptions(unit);
        options.content = content;
        const GanttPages pages(source, options);
        if (!pages.isValid()) return {false, pages.error()};
        const double band = frame.width();
        const QSizeF size = pages.imageSize() + QSizeF(2 * band, 2 * band);
        QSvgGenerator generator;
        generator.setOutputDevice(&file);
        generator.setSize(QSize(int(std::ceil(size.width())), int(std::ceil(size.height()))));
        generator.setViewBox(QRectF(QPointF(0, 0), size));
        generator.setResolution(96);
        generator.setTitle(source.sheetName);
        generator.setDescription(QStringLiteral("Mentis"));
        QPainter painter;
        if (!painter.begin(&generator)) return {false, writeError(path, file.errorString())};
        painter.fillRect(QRectF(QPointF(0, 0), size), Qt::white);
        pages.drawImage(&painter, QRectF(QPointF(band, band), pages.imageSize()));
        frame.paint(&painter, QRectF(QPointF(0, 0), size));
        painter.end();
    }
    if (!file.commit()) return {false, writeError(path, file.errorString())};
    result.written = {path};
    return result;
}

GanttPages::GanttPages(const GanttExportSource &source, const Options &options) : m_source(source), m_options(options) {
    // Fit to the Gantt: the chart on a page of its own size, the cost tables on landscape A4.
    const bool fit = options.size == MapExporter::PageSize::FitToMap;
    m_layout = MapExporter::pageLayout(fit ? MapExporter::PageSize::A4 : options.size, fit || options.landscape);
    m_pagePx = m_layout.fullRectPoints().size() / kPxToPt;
    m_costs = GanttCosts::compute(source.plan, source.schedule, source.assignments, source.people);
    if (options.content != Content::Costs) layoutChart();
    if (options.content != Content::Gantt) layoutCosts();
    if (m_pieces.isEmpty()) m_error = tr("There is nothing to export.");
}

GanttPages::~GanttPages() = default;

bool GanttPages::fitsContent() const { return m_options.singlePage || m_options.size == MapExporter::PageSize::FitToMap; }

double GanttPages::contentHeight() const {
    return m_pagePx.height() - 2 * kMarginPx - (m_options.singlePage ? 0 : kFooterPx);
}

QPageLayout GanttPages::layout(int index) const {
    const QSizeF page = m_pieces.at(index).pagePx;
    if (page == m_pagePx) return m_layout;
    // A page of the Gantt's size; past the PDF limit it is scaled down (draw fits the content to it).
    QSizeF points = page * kPxToPt;
    if (const qreal side = std::max(points.width(), points.height()); side > MapExporter::kMaxPdfPagePt)
        points *= MapExporter::kMaxPdfPagePt / side;
    return QPageLayout(QPageSize(points, QPageSize::Point, QString(), QPageSize::ExactMatch), QPageLayout::Portrait, QMarginsF());
}

QSizeF GanttPages::imageSize() const {
    QSizeF size(0, 0);  // QSizeF() is -1 x -1
    for (const Piece &piece : m_pieces) size = QSizeF(std::max(size.width(), piece.pagePx.width()), size.height() + piece.pagePx.height());
    return size;
}

void GanttPages::drawImage(QPainter *painter, const QRectF &target) const {
    const QSizeF whole = imageSize();
    if (whole.isEmpty()) return;
    const qreal scale = target.width() / whole.width();
    painter->fillRect(target, Qt::white);
    double y = 0;
    for (int i = 0; i < m_pieces.size(); ++i) {
        const QSizeF size = m_pieces.at(i).pagePx * scale;
        draw(i, painter, QRectF(target.topLeft() + QPointF(0, y), size));
        y += size.height();
    }
}

GanttPages::PageInfo GanttPages::pageInfo(int index) const {
    const Piece &piece = m_pieces.at(index);
    PageInfo info{piece.kind == Kind::Chart, piece.first, piece.count, piece.firstDay, piece.days};
    if (piece.kind == Kind::Costs) {
        info.endsWithTotal = m_lines.at(piece.first + piece.count - 1).type == Line::Type::Total;
        info.startsWithTotal = m_lines.at(piece.first).type == Line::Type::Total;
    }
    return info;
}

int GanttPages::rowsPerPage() const { return std::max(1, int(std::floor((contentHeight() - kHeaderPx) / kRowPx))); }

void GanttPages::layoutChart() {
    if (m_source.rows.isEmpty()) return;
    // The table's columns: as wide as their texts, the name between kNameMinPx and kNameMaxPx.
    const QFontMetricsF plain(textFont()), bold(textFont(true));
    m_columnWidths.clear();
    for (int c = 0; c < m_source.columns.size(); ++c) {
        const GanttExportSource::Column &column = m_source.columns.at(c);
        double width = plain.horizontalAdvance(column.title);
        for (const GanttExportSource::Row &row : std::as_const(m_source.rows)) {
            const QString text = row.cells.value(c);
            width = std::max(width, (row.summary && column.name ? bold : plain).horizontalAdvance(text) + (column.name ? row.depth * kIndentPx : 0));
        }
        width += 2 * kCellPadPx;
        m_columnWidths.append(column.name ? std::clamp(width, kNameMinPx, kNameMaxPx) : std::min(width, kOtherMaxPx));
    }
    m_tableWidth = 0;
    for (double width : std::as_const(m_columnWidths)) m_tableWidth += width;
    // The table never takes more than 60 % of the page: the name gives way first.
    if (!fitsContent() && m_tableWidth > contentWidth() * 0.6)
        for (int c = 0; c < m_source.columns.size(); ++c)
            if (m_source.columns.at(c).name) {
                const double cut = std::min(m_columnWidths[c] - kNameMinPx, m_tableWidth - contentWidth() * 0.6);
                m_columnWidths[c] -= std::max(0.0, cut);
                m_tableWidth -= std::max(0.0, cut);
            }

    // The timeline: the plan's dates (and the compared snapshot's), a day of room at each end.
    QDate start = m_source.schedule.start, end = m_source.schedule.end;
    if (m_source.baseline)
        for (const GanttBaselineTask &task : m_source.baseline->tasks)
            if (task.dated) {
                start = start.isValid() ? std::min(start, task.start) : task.start;
                end = end.isValid() ? std::max(end, task.end) : task.end;
            }
    if (!start.isValid()) {
        start = QDate::currentDate();
        end = start.addDays(13);
    }
    start = start.addDays(-1);
    end = end.addDays(1);

    // Fit width: the first unit from the chosen one whose days fit the room at a legible width.
    const double room = fitsContent() ? std::numeric_limits<double>::max() : contentWidth() - m_tableWidth;
    QVector<GanttChart::Scale> candidates{m_options.scale};
    if (m_options.fitWidth && !fitsContent())
        for (GanttChart::Scale scale : {GanttChart::Scale::Weeks, GanttChart::Scale::Months})
            if (int(scale) > int(m_options.scale)) candidates.append(scale);
    bool chosen = false;
    for (GanttChart::Scale scale : std::as_const(candidates)) {
        QDate first = start, last = end;
        align(scale, first, last);
        const qint64 days = first.daysTo(last) + 1;
        int zoom = 100;
        if (m_options.fitWidth && !fitsContent()) {
            const int widest = int(std::floor(room / double(days)));
            zoom = std::min(GanttChart::kMaxZoom, int(std::floor(widest * 100.0 / baseDayWidth(scale))));
            while (zoom >= GanttChart::kMinZoom && dayWidthAt(scale, zoom) > widest) --zoom;
            if (zoom < GanttChart::kMinZoom || !legible(scale, dayWidthAt(scale, zoom))) continue;
        }
        m_scale = scale;
        m_zoom = zoom;
        m_first = first;
        m_last = last;
        chosen = true;
        break;
    }
    if (!chosen) {  // too long for one page even in months: the last unit tried, over pages in width
        m_scale = candidates.last();
        m_zoom = m_options.fitWidth ? GanttChart::kMinZoom : 100;
        m_first = start;
        m_last = end;
        align(m_scale, m_first, m_last);
    }
    m_dayWidth = dayWidthAt(m_scale, m_zoom);
    // A page of the Gantt's size: room after the last bar for a name shown after it (summaries, milestones).
    if (fitsContent()) m_last = m_last.addDays(qint64(std::ceil(160.0 / m_dayWidth)));

    // The chart off screen, as the view draws it on light paper.
    m_chart = std::make_unique<GanttChart>();
    m_chart->setAttribute(Qt::WA_DontShowOnScreen);
    m_chart->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chart->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chart->setFrameShape(QFrame::NoFrame);
    m_chart->setColors(GanttColors::light());
    m_chart->setPrinting(true);
    m_chart->setScale(m_scale);
    m_chart->setZoomPercent(m_zoom);
    QStringList ids;
    for (const GanttExportSource::Row &row : std::as_const(m_source.rows)) ids.append(row.id);
    m_chart->setData(m_source.plan, m_source.schedule, ids, {}, m_source.looks);
    m_chart->setLabelled(m_source.labelled);
    m_chart->setBaseline(m_source.baseline, m_source.comparison);
    m_chart->setCriticalShown(m_source.critical);
    m_chart->verticalOffset = [this] { return m_rowOffset; };
    m_chart->show();
    m_first = std::max(m_first, m_chart->origin());  // the chart starts there
    const int days = int(m_first.daysTo(m_last)) + 1;

    // Pages: rows over pages in height (a summary never last on a page with its tasks on the next),
    // the timeline over pages in width only when it does not fit.
    int daysPerPage = fitsContent() ? days : std::max(1, int(std::floor((contentWidth() - m_tableWidth) / m_dayWidth)));
    if (m_scale == GanttChart::Scale::Weeks && daysPerPage >= 14) daysPerPage -= daysPerPage % 7;
    daysPerPage = std::min(daysPerPage, days);
    m_columnsOfPages = (days + daysPerPage - 1) / daysPerPage;
    const int rowsPerPage = fitsContent() ? int(m_source.rows.size()) : std::max(1, int(std::floor((contentHeight() - kHeaderPx) / kRowPx)));
    // Fit to the Gantt: one page as large as table and chart (and the footer on paper).
    const QSizeF own(2 * kMarginPx + m_tableWidth + double(days) * m_dayWidth,
                     2 * kMarginPx + kHeaderPx + double(m_source.rows.size()) * kRowPx + (m_options.singlePage ? 0 : kFooterPx));
    for (int first = 0; first < m_source.rows.size();) {
        int count = std::min(rowsPerPage, int(m_source.rows.size()) - first);
        const int last = first + count - 1;
        if (count > 1 && last + 1 < m_source.rows.size() && m_source.rows.at(last).summary
            && m_source.rows.at(last + 1).depth > m_source.rows.at(last).depth)
            --count;
        for (int column = 0; column < m_columnsOfPages; ++column) {
            Piece piece;
            piece.kind = Kind::Chart;
            piece.first = first;
            piece.count = count;
            piece.firstDay = m_first.addDays(qint64(column) * daysPerPage);
            piece.days = std::min(daysPerPage, days - column * daysPerPage);
            piece.pagePx = fitsContent() ? own : m_pagePx;
            piece.footer = !m_options.singlePage;
            m_pieces.append(piece);
        }
        first += count;
    }
}

void GanttPages::layoutCosts() {
    m_costPeople.clear();
    for (const GanttResource &person : std::as_const(m_source.people))
        if (m_costs.people.contains(person.id)) m_costPeople.append(person.id);
    m_lines.clear();
    m_lines.append({Line::Type::Title, 0, -1});
    m_lines.append({Line::Type::Header, 0, -1});
    for (int i = 0; i < m_source.rows.size(); ++i) m_lines.append({Line::Type::Row, 0, i});
    m_lines.append({Line::Type::Total, 0, -1});
    m_lines.append({Line::Type::Gap, 1, -1});
    m_lines.append({Line::Type::Header, 1, -1});
    for (int i = 0; i < m_costPeople.size(); ++i) m_lines.append({Line::Type::Row, 1, i});
    m_lines.append({Line::Type::Total, 1, -1});
    const auto height = [](Line::Type type) {
        return type == Line::Type::Title ? kCostTitlePx : type == Line::Type::Gap ? kCostGapPx : kCostRowPx;
    };
    // The image: every line in one piece, as wide as the chart (at least a landscape A4's text).
    if (m_options.singlePage) {
        double total = 2 * kMarginPx;
        for (const Line &line : std::as_const(m_lines)) total += height(line.type);
        Piece piece;
        piece.kind = Kind::Costs;
        piece.count = int(m_lines.size());
        const double width = m_pieces.isEmpty() ? m_pagePx.width() : std::max(m_pagePx.width(), m_pieces.first().pagePx.width());
        piece.pagePx = QSizeF(width, total);
        piece.footer = false;
        m_pieces.append(piece);
        return;
    }
    // A page that starts inside a table repeats its header first; a gap never starts a page.
    const double room = contentHeight();
    int first = 0;
    while (first < m_lines.size()) {
        if (m_lines.at(first).type == Line::Type::Gap) {
            ++first;
            continue;
        }
        const Line::Type type = m_lines.at(first).type;
        double used = type == Line::Type::Row || type == Line::Type::Total ? kCostRowPx : 0;  // the repeated header
        int count = 0;
        while (first + count < m_lines.size() && used + height(m_lines.at(first + count).type) <= room) {
            used += height(m_lines.at(first + count).type);
            ++count;
        }
        // A header (or the title and its header) is not left alone at the bottom of a page.
        while (count > 1 && (m_lines.at(first + count - 1).type == Line::Type::Header
                             || m_lines.at(first + count - 1).type == Line::Type::Title || m_lines.at(first + count - 1).type == Line::Type::Gap))
            --count;
        // A total never alone: its last row goes with it. A summary never last with its tasks after.
        if (first + count < m_lines.size() && count > 2) {
            const Line &next = m_lines.at(first + count), &last = m_lines.at(first + count - 1);
            const bool totalAlone = next.type == Line::Type::Total && last.type == Line::Type::Row;
            const bool summaryAlone = next.type == Line::Type::Row && last.type == Line::Type::Row && last.table == 0
                                      && next.table == 0 && m_source.rows.at(last.index).summary;
            if (totalAlone || summaryAlone) --count;
        }
        count = std::max(1, count);
        Piece piece;
        piece.kind = Kind::Costs;
        piece.first = first;
        piece.count = count;
        piece.pagePx = m_pagePx;
        m_pieces.append(piece);
        first += count;
    }
}

void GanttPages::draw(int index, QPainter *painter, const QRectF &target) const {
    if (index < 0 || index >= m_pieces.size()) return;
    const Piece &piece = m_pieces.at(index);
    painter->save();
    painter->fillRect(target, Qt::white);
    painter->translate(target.topLeft());
    painter->scale(target.width() / piece.pagePx.width(), target.height() / piece.pagePx.height());
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    if (piece.kind == Kind::Chart) {
        drawTable(*painter, piece, kMarginPx);
        drawChart(*painter, piece, kMarginPx + m_tableWidth, kMarginPx);
    } else {
        drawCosts(*painter, piece, kMarginPx);
    }
    if (piece.footer) drawFooter(*painter, index);
    painter->restore();
}

void GanttPages::drawTable(QPainter &painter, const Piece &piece, double top) const {
    double x = kMarginPx;
    const double bottom = top + kHeaderPx + piece.count * kRowPx;
    painter.setFont(textFont());
    // The header: the titles in its lower half, as the days of the chart's.
    painter.setPen(kMuted);
    for (int c = 0; c < m_source.columns.size(); ++c) {
        const GanttExportSource::Column &column = m_source.columns.at(c);
        const QRectF cell(x + kCellPadPx, top + kHeaderPx / 2.0, m_columnWidths.at(c) - 2 * kCellPadPx, kHeaderPx / 2.0);
        painter.drawText(cell, (column.right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, column.title);
        x += m_columnWidths.at(c);
    }
    painter.setPen(QPen(kHeaderLine, 1));
    painter.drawLine(QPointF(kMarginPx, top + kHeaderPx - 0.5), QPointF(kMarginPx + m_tableWidth, top + kHeaderPx - 0.5));
    for (int i = 0; i < piece.count; ++i) {
        const GanttExportSource::Row &row = m_source.rows.at(piece.first + i);
        const double y = top + kHeaderPx + i * kRowPx;
        x = kMarginPx;
        for (int c = 0; c < m_source.columns.size(); ++c) {
            const GanttExportSource::Column &column = m_source.columns.at(c);
            const double indent = column.name ? row.depth * kIndentPx : 0;
            const QFont font = textFont(row.summary && column.name);
            painter.setFont(font);
            painter.setPen(column.muted || (row.summary && !column.name) ? kMuted : kText);
            const QRectF cell(x + kCellPadPx + indent, y, m_columnWidths.at(c) - 2 * kCellPadPx - indent, kRowPx);
            const QString text = QFontMetricsF(font).elidedText(row.cells.value(c), Qt::ElideRight, cell.width());
            painter.drawText(cell, (column.right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, text);
            x += m_columnWidths.at(c);
        }
        painter.setPen(QPen(kRowLine, 1));
        painter.drawLine(QPointF(kMarginPx, y + kRowPx - 0.5), QPointF(kMarginPx + m_tableWidth, y + kRowPx - 0.5));
    }
    painter.setPen(QPen(kHeaderLine, 1));  // the table's edge against the chart
    painter.drawLine(QPointF(kMarginPx + m_tableWidth - 0.5, top), QPointF(kMarginPx + m_tableWidth - 0.5, bottom));
}

void GanttPages::drawChart(QPainter &painter, const Piece &piece, double left, double top) const {
    m_rowOffset = piece.first * kRowPx;
    m_chart->resize(piece.days * m_dayWidth, kHeaderPx + piece.count * kRowPx);
    m_chart->setFirstDay(piece.firstDay);
    m_chart->viewport()->update();
    painter.save();
    painter.translate(left, top);
    painter.setClipRect(QRectF(0, 0, m_chart->width(), m_chart->height()));
    m_chart->viewport()->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.restore();
}

void GanttPages::drawCosts(QPainter &painter, const Piece &piece, double top) const {
    const double width = piece.pagePx.width() - 2 * kMarginPx;
    const double flexible = width - kHoursPx - kAmountPx;
    // Columns: tasks (name, people, hours, cost) and people (person, role, rate, hours, cost).
    const QVector<double> widths[2] = {{flexible * 0.55, flexible * 0.45, kHoursPx, kAmountPx},
                                       {flexible * 0.4, flexible * 0.3, flexible * 0.3, kHoursPx, kAmountPx}};
    const QStringList titles[2] = {{tr("Task"), tr("People"), tr("Hours"), tr("Cost")},
                                   {tr("Person"), tr("Role"), tr("Hourly cost"), tr("Hours"), tr("Cost")}};
    const QString &currency = m_source.currency;
    QHash<QString, const GanttResource *> people;
    for (const GanttResource &person : m_source.people) people.insert(person.id, &person);
    const auto personName = [&](const QString &id) {
        const GanttResource *person = people.value(id);
        if (!person) return QString();
        return person->displayName().isEmpty() ? person->email : person->displayName();
    };
    const auto row = [&](double y, int table, const QStringList &cells, bool bold, int indentColumn = -1, double indent = 0) {
        double x = kMarginPx;
        for (int c = 0; c < cells.size(); ++c) {
            const bool right = c >= cells.size() - 2 || (table == 1 && c == 2);
            const QFont font = textFont(bold);
            painter.setFont(font);
            const double shift = c == indentColumn ? indent : 0;
            const QRectF cell(x + kCellPadPx + shift, y, widths[table].at(c) - 2 * kCellPadPx - shift, kCostRowPx);
            painter.drawText(cell, (right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
                             QFontMetricsF(font).elidedText(cells.at(c), Qt::ElideRight, cell.width()));
            x += widths[table].at(c);
        }
    };
    const auto line = [&](double y, const QColor &color) {
        painter.setPen(QPen(color, 1));
        painter.drawLine(QPointF(kMarginPx, y - 0.5), QPointF(kMarginPx + width, y - 0.5));
    };
    double y = top;
    const auto header = [&](int table) {
        painter.setPen(kMuted);
        row(y, table, titles[table], false);
        y += kCostRowPx;
        line(y, kHeaderLine);
    };
    const Line::Type firstType = m_lines.at(piece.first).type;
    if (firstType == Line::Type::Row || firstType == Line::Type::Total) header(m_lines.at(piece.first).table);
    for (int i = piece.first; i < piece.first + piece.count; ++i) {
        const Line &entry = m_lines.at(i);
        switch (entry.type) {
        case Line::Type::Title: {
            painter.setPen(kText);
            painter.setFont(textFont(true, 16));
            painter.drawText(QRectF(kMarginPx, y, width, kCostTitlePx), Qt::AlignLeft | Qt::AlignVCenter,
                             tr("Costs — %1").arg(m_source.sheetName));
            y += kCostTitlePx;
            break;
        }
        case Line::Type::Gap: y += kCostGapPx; break;
        case Line::Type::Header: header(entry.table); break;
        case Line::Type::Row: {
            if (entry.table == 0) {
                const GanttExportSource::Row &task = m_source.rows.at(entry.index);
                QStringList names;
                if (!task.summary)
                    for (const GanttAssignment &assignment : m_source.assignments.value(task.id))
                        if (const QString name = personName(assignment.person); !name.isEmpty())
                            names.append(assignment.percent == 100 ? name : tr("%1 (%2 %)").arg(name).arg(assignment.percent));
                const GanttCosts::Amount amount = m_costs.tasks.value(task.id);
                if (task.summary) painter.fillRect(QRectF(kMarginPx, y, width, kCostRowPx), kSummaryFill);
                painter.setPen(kText);
                row(y, 0, {m_source.plan.task(task.id) ? m_source.plan.task(task.id)->name.simplified() : QString(), names.join(QStringLiteral(", ")),
                           hoursText(amount.minutes), amountText(amount.cents, currency)},
                    task.summary, 0, task.depth * kIndentPx);
            } else {
                const QString id = m_costPeople.at(entry.index);
                const GanttResource *person = people.value(id);
                const GanttCosts::Amount amount = m_costs.people.value(id);
                painter.setPen(kText);
                row(y, 1, {personName(id), person ? person->role : QString(),
                           person ? tr("%1 / h").arg(amountText(person->hourlyCostCents, currency)) : QString(),
                           hoursText(amount.minutes), amountText(amount.cents, currency)},
                    false);
            }
            y += kCostRowPx;
            line(y, kRowLine);
            break;
        }
        case Line::Type::Total: {
            painter.setPen(kText);
            const QString hours = hoursText(m_costs.total.minutes), cost = amountText(m_costs.total.cents, currency);
            if (entry.table == 0) row(y, 0, {tr("Total"), QString(), hours, cost}, true);
            else row(y, 1, {tr("Total"), QString(), QString(), hours, cost}, true);
            y += kCostRowPx;
            break;
        }
        }
    }
}

void GanttPages::drawFooter(QPainter &painter, int index) const {
    const Piece &piece = m_pieces.at(index);
    const double y = piece.pagePx.height() - kMarginPx - 14;
    const double width = piece.pagePx.width() - 2 * kMarginPx;
    painter.setFont(textFont(false, 10));
    painter.setPen(kMuted);
    const QString what = piece.kind == Kind::Chart ? tr("Gantt") : tr("Costs");
    painter.drawText(QRectF(kMarginPx, y, width / 3, 14), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("%1 · %2").arg(m_source.sheetName, what));
    painter.drawText(QRectF(kMarginPx + width * 2 / 3, y, width / 3, 14), Qt::AlignRight | Qt::AlignVCenter,
                     tr("%1 · page %2 of %3").arg(QLocale().toString(QDate::currentDate(), QLocale::ShortFormat)).arg(index + 1).arg(count()));
    if (piece.kind != Kind::Chart) return;
    // The legend of the colours, in the middle: the compared snapshot's, or the critical path.
    QList<QPair<QColor, QString>> items;
    if (m_source.baseline)
        items = {{kSame, m_source.baseline->name}, {kLater, tr("Later")}, {kEarlier, tr("Earlier")}};
    else if (m_source.critical)
        items = {{kLater, tr("Critical path")}};
    const QFontMetricsF metrics(painter.font());
    double total = 0;
    for (const auto &item : items) total += 22 + metrics.horizontalAdvance(item.second) + 16;
    double x = kMarginPx + (width - total) / 2;
    for (const auto &[color, text] : items) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(x, y + 5, 16, 4), 2, 2);
        x += 22;
        painter.setPen(kMuted);
        painter.drawText(QRectF(x, y, metrics.horizontalAdvance(text) + 1, 14), Qt::AlignLeft | Qt::AlignVCenter, text);
        x += metrics.horizontalAdvance(text) + 16;
    }
}
