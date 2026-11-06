#pragma once
// FEAT-083 (docs/features/FEAT-083-gantt-esportazione.md): a sheet's Gantt and its costs on pages
// for the PDF, the print and their preview, and as one picture for PNG and SVG. Legibility first:
// the view's sizes on paper (1 px = 0.75 pt: text 9 pt, rows 8.5 mm), never smaller; what does not
// fit goes on more pages, the time header and the table repeated on each. The chart is the view's
// own GanttChart, drawn off screen, so the bars look the same.
#include "ganttcosts.h"
#include "ganttplan.h"
#include "ganttview.h"
#include "mapexporter.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>
#include <optional>

// What the Gantt view shows, gathered by GanttView::exportSource().
struct GanttExportSource {
    struct Column {
        // What the cells hold: Excel writes dates, numbers and percentages (FEAT-083 stage C).
        enum class Kind { Text, Name, Start, End, Duration, Progress };
        QString title;
        Kind kind = Kind::Text;
        bool name = false;   // the tree column: indented, summaries bold
        bool right = false;  // numbers: aligned right
        bool muted = false;  // the map's numbers
    };
    struct Row {
        QString id;
        int depth = 0;
        bool summary = false;
        QStringList cells;  // the shown columns' texts, in the columns' order
    };
    QString sheetName;
    GanttPlan plan;
    GanttSchedule schedule;
    QVector<Column> columns;
    QVector<Row> rows;  // every task in the tree's order, folded summaries opened
    QHash<QString, GanttBarLook> looks;
    QSet<QString> labelled;
    std::optional<GanttBaseline> baseline;  // the compared snapshot (FEAT-082), if any
    GanttBaselineComparison comparison;
    bool critical = true;
    GanttChart::Scale scale = GanttChart::Scale::Days;
    GanttAssignments assignments;
    QVector<GanttResource> people;
    QString currency;  // the symbol, or the name (GanttResources::displaySymbol)
};

class GanttPages final : public MapExporter::PageSource {
    Q_DECLARE_TR_FUNCTIONS(GanttPages)
public:
    enum class Content { Gantt, GanttAndCosts, Costs };
    struct Options {
        Content content = Content::Gantt;
        MapExporter::PageSize size = MapExporter::PageSize::A4;
        bool landscape = true;
        // Fit width: the timeline fits the page width, the time unit raised (days, weeks, months)
        // until it does at a legible size; otherwise the chosen unit at 100 % over pages in width.
        bool fitWidth = true;
        GanttChart::Scale scale = GanttChart::Scale::Days;
        // PNG, SVG: the chart (and the cost tables) each in one piece as large as needed, no footer;
        // the image stacks them (imageSize, drawImage). MapExporter::PageSize::FitToMap does the
        // same for the PDF's chart page, the costs on landscape A4 after it.
        bool singlePage = false;
    };

    // Sizes on paper, in the view's pixels (96 per inch).
    static constexpr double kMarginPx = 10 * 96 / 25.4;
    static constexpr double kFooterPx = 24;
    static constexpr double kCostRowPx = 28;

    GanttPages(const GanttExportSource &source, const Options &options);
    ~GanttPages() override;

    bool isValid() const override { return m_error.isEmpty(); }
    QString error() const override { return m_error; }
    int count() const override { return int(m_pieces.size()); }
    QPageLayout layout(int index) const override;
    void draw(int index, QPainter *painter, const QRectF &target) const override;

    // The time unit the chart took (fit width may raise the chosen one) and its zoom.
    GanttChart::Scale chosenScale() const { return m_scale; }
    int chosenZoom() const { return m_zoom; }
    bool splitInWidth() const { return m_columnsOfPages > 1; }  // the timeline goes on several pages
    QSizeF pageSizePx(int index = 0) const { return m_pieces.isEmpty() ? m_pagePx : m_pieces.at(index).pagePx; }
    // The single image: the pieces one under the other, as wide as the widest.
    QSizeF imageSize() const;
    void drawImage(QPainter *painter, const QRectF &target) const;
    // For the tests: a page's kind and what it shows (chart: rows and days; costs: lines and whether
    // its last line is a total).
    struct PageInfo {
        bool chart = true;
        int first = 0;
        int count = 0;
        QDate firstDay;
        int days = 0;
        bool endsWithTotal = false;
        bool startsWithTotal = false;
    };
    PageInfo pageInfo(int index) const;
    int rowsPerPage() const;
    // FEAT-083: the whole Gantt as one picture (Image window). The unit is raised when the PNG would
    // pass MapExporter::kMaxPngSidePx even at 1x (reduced = true, as a smaller scale).
    // FEAT-084: frame on the image's edge, the image larger by its width.
    static QImage toImage(const GanttExportSource &source, GanttChart::Scale unit, int scale, bool *reduced, QString *error,
                          Content content = Content::Gantt, const ExportFrame &frame = {});
    static MapExporter::Result write(MapExporter::Format format, const GanttExportSource &source, GanttChart::Scale unit,
                                     const QString &path, int scale, Content content = Content::Gantt, const ExportFrame &frame = {});
    // Hours "7 h 30" and amounts in the currency, as the pages write them.
    static QString hoursText(qint64 minutes);
    static QString amountText(qint64 cents, const QString &currency);

private:
    enum class Kind { Chart, Costs };
    struct Piece {
        Kind kind = Kind::Chart;
        int first = 0;    // chart: first row; costs: first line
        int count = 0;    // rows or lines
        QDate firstDay;   // chart: the day at the left edge
        int days = 0;     // chart: days shown
        QSizeF pagePx;    // the paper, or the piece's own size (fit to the Gantt, image)
        bool footer = true;
    };
    // A line of the cost tables: tasks (table 0), then people (table 1).
    struct Line {
        enum class Type { Title, Header, Row, Total, Gap };
        Type type = Type::Row;
        int table = 0;
        int index = -1;  // the row's task (m_source.rows) or person (m_costPeople)
    };

    void layoutChart();
    void layoutCosts();
    double contentWidth() const { return m_pagePx.width() - 2 * kMarginPx; }
    double contentHeight() const;
    bool fitsContent() const;  // singlePage or Fit to map: the chart on a page of its size
    void drawTable(QPainter &painter, const Piece &piece, double top) const;
    void drawChart(QPainter &painter, const Piece &piece, double left, double top) const;
    void drawCosts(QPainter &painter, const Piece &piece, double top) const;
    void drawFooter(QPainter &painter, int index) const;

    GanttExportSource m_source;
    Options m_options;
    QString m_error;
    QSizeF m_pagePx;
    QPageLayout m_layout;
    QVector<double> m_columnWidths;
    double m_tableWidth = 0;
    QDate m_first, m_last;  // the timeline's first and last day
    GanttChart::Scale m_scale = GanttChart::Scale::Days;
    int m_zoom = 100;
    int m_dayWidth = 28;
    int m_columnsOfPages = 1;
    GanttCosts m_costs;
    QVector<Line> m_lines;
    QStringList m_costPeople;  // people with hours, in the Resources list's order
    std::unique_ptr<GanttChart> m_chart;  // drawn off screen, its offsets moved for each page
    mutable int m_rowOffset = 0;  // the chart's vertical offset for the page being drawn
    QVector<Piece> m_pieces;
};
