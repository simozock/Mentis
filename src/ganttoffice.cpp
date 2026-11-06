#include "ganttoffice.h"

#include "officeexport.h"
#include "ooxml.h"
#include "zipwriter.h"

#include <QCoreApplication>
#include <QLocale>
#include <QMap>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <map>

namespace GanttOffice {

using namespace Ooxml;

namespace {

const QString kS = QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main");
const QString kW = QStringLiteral("http://schemas.openxmlformats.org/wordprocessingml/2006/main");
const QString kWp = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing");
const QString kA = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
const QString kPic = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/picture");
const QString kWordType = QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.");
const QString kSheetType = QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.");

QString tr(const char *text) { return QCoreApplication::translate("GanttOffice", text); }
// The cost tables' words, as the PDF pages write them (GanttPages' translations).
QString pagesTr(const char *text) { return QCoreApplication::translate("GanttPages", text); }

// A colour over white: the Gantt's translucent lines and shades as the light page shows them.
QColor onWhite(const QColor &color) {
    const double a = color.alphaF();
    const auto mix = [a](int c) { return int(std::lround(255 * (1 - a) + c * a)); };
    return QColor(mix(color.red()), mix(color.green()), mix(color.blue()));
}
QColor tint(const QColor &color, double amount) {
    QColor see = color;
    see.setAlphaF(amount);
    return onWhite(see);
}

// The light Gantt's colours (GanttColors::light) and the PDF pages' table lines (GanttPages).
struct Palette {
    QColor text, muted, rowLine, headerLine, band, summary, shade;
};
Palette palette() {
    const GanttColors light = GanttColors::light();
    return {light.text, light.muted, onWhite(light.grid), QColor(0xe5, 0xe7, 0xeb), light.page, QColor(0xf4, 0xf5, 0xf7),
            onWhite(light.shade)};
}

QString hex(const QColor &color) { return QStringLiteral("%1").arg(color.rgb() & 0xffffff, 6, 16, QLatin1Char('0')).toUpper(); }
QString argb(const QColor &color) { return QStringLiteral("FF") + hex(color); }

// The person's shown name: name, else e-mail.
QString personName(const GanttResource &person) { return person.displayName().isEmpty() ? person.email : person.displayName(); }

// What the cost tables list: the tasks' people with their share, the people with hours.
struct CostData {
    GanttCosts costs;
    QVector<const GanttResource *> people;  // with hours, in the Resources list's order
    QHash<QString, const GanttResource *> byId;
    QString taskPeople(const GanttExportSource &source, const GanttExportSource::Row &row) const {
        if (row.summary) return {};
        QStringList names;
        for (const GanttAssignment &assignment : source.assignments.value(row.id))
            if (const GanttResource *person = byId.value(assignment.person); person && !personName(*person).isEmpty())
                names.append(assignment.percent == 100 ? personName(*person) : pagesTr("%1 (%2 %)").arg(personName(*person)).arg(assignment.percent));
        return names.join(QStringLiteral(", "));
    }
};
CostData costData(const GanttExportSource &source) {
    CostData data;
    data.costs = GanttCosts::compute(source.plan, source.schedule, source.assignments, source.people);
    for (const GanttResource &person : source.people) {
        data.byId.insert(person.id, &person);
        if (data.costs.people.contains(person.id)) data.people.append(&person);
    }
    return data;
}

QString taskName(const GanttExportSource &source, const GanttExportSource::Row &row) {
    const GanttTask *task = source.plan.task(row.id);
    return task ? task->name.simplified() : QString();
}

// Direct children of each row (the rows are in the tree's order with their depth).
QVector<QVector<int>> childrenOf(const QVector<GanttExportSource::Row> &rows) {
    QVector<QVector<int>> children(rows.size());
    for (int i = 0; i < rows.size(); ++i)
        for (int j = i + 1; j < rows.size() && rows.at(j).depth > rows.at(i).depth; ++j)
            if (rows.at(j).depth == rows.at(i).depth + 1) children[i].append(j);
    return children;
}

// --- Excel ---

// A cell's look; the styles part keeps one of each (ECMA-376 Part 1, 18.8).
struct CellLook {
    bool bold = false;
    double size = 10;  // points
    QColor color;      // invalid = the text colour
    QColor fill;       // invalid = none
    QString pattern = QStringLiteral("solid");
    QColor top, bottom;  // thin lines; invalid = none
    int builtinFormat = 0;  // used when format is empty
    QString format;
    QString align;  // left, center, right; empty = general
    QString vertical = QStringLiteral("center");
    int indent = 0;
    bool wrap = false;
    bool operator==(const CellLook &) const = default;
};

// A conditional format (ECMA-376 Part 1, 18.8.17): a fill, solid or hatched, and thin lines.
struct Dxf {
    QColor fill;
    QString pattern = QStringLiteral("solid");
    QColor font;
    bool bold = false;
    QColor top, bottom;
    bool operator==(const Dxf &) const = default;
};

class Styles {
public:
    explicit Styles(const QColor &text) : m_text(text) { m_looks.append(CellLook{}); }
    int of(const CellLook &look) {
        const qsizetype at = m_looks.indexOf(look);
        if (at >= 0) return int(at);
        m_looks.append(look);
        return int(m_looks.size() - 1);
    }
    int dxf(const Dxf &format) {
        const qsizetype at = m_dxfs.indexOf(format);
        if (at >= 0) return int(at);
        m_dxfs.append(format);
        return int(m_dxfs.size() - 1);
    }
    QByteArray part() const;

private:
    QColor m_text;
    QList<CellLook> m_looks;
    QList<Dxf> m_dxfs;
};

QByteArray Styles::part() const {
    struct Font {
        bool bold;
        double size;
        QColor color;
        bool operator==(const Font &) const = default;
    };
    struct Fill {
        QColor color;
        QString pattern;
        bool operator==(const Fill &) const = default;
    };
    struct Border {
        QColor top, bottom;
        bool operator==(const Border &) const = default;
    };
    QList<Font> fonts;
    QList<Fill> fills{{QColor(), QStringLiteral("none")}, {QColor(), QStringLiteral("gray125")}};  // the two Excel expects
    QList<Border> borders{{QColor(), QColor()}};
    QStringList formats;
    const auto indexIn = [](auto &list, const auto &value) {
        qsizetype at = list.indexOf(value);
        if (at < 0) {
            list.append(value);
            at = list.size() - 1;
        }
        return int(at);
    };
    struct Xf {
        int font, fill, border, format;
    };
    QList<Xf> xfs;
    for (const CellLook &look : m_looks) {
        Xf xf;
        xf.font = indexIn(fonts, Font{look.bold, look.size, look.color.isValid() ? look.color : m_text});
        xf.fill = look.fill.isValid() ? indexIn(fills, Fill{look.fill, look.pattern}) : 0;
        xf.border = indexIn(borders, Border{look.top, look.bottom});
        xf.format = look.format.isEmpty() ? look.builtinFormat : 164 + indexIn(formats, look.format);  // custom ids from 164
        xfs.append(xf);
    }
    return Ooxml::part([&](QXmlStreamWriter &xml) {
        const auto color = [&](const QString &name, const QColor &value) { element(xml, kS, name, {{QStringLiteral("rgb"), argb(value)}}); };
        const auto line = [&](const QString &side, const QColor &value) {
            if (!value.isValid()) {
                element(xml, kS, side);
                return;
            }
            xml.writeStartElement(kS, side);
            xml.writeAttribute(QStringLiteral("style"), QStringLiteral("thin"));
            color(QStringLiteral("color"), value);
            xml.writeEndElement();
        };
        xml.writeDefaultNamespace(kS);
        xml.writeStartElement(kS, QStringLiteral("styleSheet"));
        if (!formats.isEmpty()) {
            xml.writeStartElement(kS, QStringLiteral("numFmts"));
            xml.writeAttribute(QStringLiteral("count"), QString::number(formats.size()));
            for (int i = 0; i < formats.size(); ++i)
                element(xml, kS, QStringLiteral("numFmt"), {{QStringLiteral("numFmtId"), QString::number(164 + i)}, {QStringLiteral("formatCode"), formats[i]}});
            xml.writeEndElement();
        }
        xml.writeStartElement(kS, QStringLiteral("fonts"));
        xml.writeAttribute(QStringLiteral("count"), QString::number(fonts.size()));
        for (const Font &font : std::as_const(fonts)) {
            xml.writeStartElement(kS, QStringLiteral("font"));
            if (font.bold) element(xml, kS, QStringLiteral("b"));
            element(xml, kS, QStringLiteral("sz"), {{QStringLiteral("val"), QString::number(font.size)}});
            color(QStringLiteral("color"), font.color);
            element(xml, kS, QStringLiteral("name"), {{QStringLiteral("val"), QStringLiteral("Calibri")}});
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeStartElement(kS, QStringLiteral("fills"));
        xml.writeAttribute(QStringLiteral("count"), QString::number(fills.size()));
        for (const Fill &fill : std::as_const(fills)) {
            xml.writeStartElement(kS, QStringLiteral("fill"));
            xml.writeStartElement(kS, QStringLiteral("patternFill"));
            xml.writeAttribute(QStringLiteral("patternType"), fill.pattern);
            if (fill.color.isValid()) {
                color(QStringLiteral("fgColor"), fill.color);
                if (fill.pattern == QLatin1String("solid")) element(xml, kS, QStringLiteral("bgColor"), {{QStringLiteral("indexed"), QStringLiteral("64")}});
                else color(QStringLiteral("bgColor"), Qt::white);
            }
            xml.writeEndElement();
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeStartElement(kS, QStringLiteral("borders"));
        xml.writeAttribute(QStringLiteral("count"), QString::number(borders.size()));
        for (const Border &border : std::as_const(borders)) {
            xml.writeStartElement(kS, QStringLiteral("border"));
            element(xml, kS, QStringLiteral("left"));
            element(xml, kS, QStringLiteral("right"));
            line(QStringLiteral("top"), border.top);
            line(QStringLiteral("bottom"), border.bottom);
            element(xml, kS, QStringLiteral("diagonal"));
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeStartElement(kS, QStringLiteral("cellStyleXfs"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("1"));
        element(xml, kS, QStringLiteral("xf"), {{QStringLiteral("numFmtId"), QStringLiteral("0")}, {QStringLiteral("fontId"), QStringLiteral("0")},
                                                {QStringLiteral("fillId"), QStringLiteral("0")}, {QStringLiteral("borderId"), QStringLiteral("0")}});
        xml.writeEndElement();
        xml.writeStartElement(kS, QStringLiteral("cellXfs"));
        xml.writeAttribute(QStringLiteral("count"), QString::number(xfs.size()));
        for (int i = 0; i < xfs.size(); ++i) {
            const CellLook &look = m_looks.at(i);
            xml.writeStartElement(kS, QStringLiteral("xf"));
            xml.writeAttribute(QStringLiteral("numFmtId"), QString::number(xfs[i].format));
            xml.writeAttribute(QStringLiteral("fontId"), QString::number(xfs[i].font));
            xml.writeAttribute(QStringLiteral("fillId"), QString::number(xfs[i].fill));
            xml.writeAttribute(QStringLiteral("borderId"), QString::number(xfs[i].border));
            xml.writeAttribute(QStringLiteral("xfId"), QStringLiteral("0"));
            for (const char *apply : {"applyNumberFormat", "applyFont", "applyFill", "applyBorder", "applyAlignment"})
                xml.writeAttribute(QString::fromLatin1(apply), QStringLiteral("1"));
            xml.writeStartElement(kS, QStringLiteral("alignment"));
            if (!look.align.isEmpty()) xml.writeAttribute(QStringLiteral("horizontal"), look.align);
            xml.writeAttribute(QStringLiteral("vertical"), look.vertical);
            if (look.indent > 0) xml.writeAttribute(QStringLiteral("indent"), QString::number(look.indent));
            if (look.wrap) xml.writeAttribute(QStringLiteral("wrapText"), QStringLiteral("1"));
            xml.writeEndElement();
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeStartElement(kS, QStringLiteral("cellStyles"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("1"));
        element(xml, kS, QStringLiteral("cellStyle"), {{QStringLiteral("name"), QStringLiteral("Normal")}, {QStringLiteral("xfId"), QStringLiteral("0")},
                                                       {QStringLiteral("builtinId"), QStringLiteral("0")}});
        xml.writeEndElement();
        xml.writeStartElement(kS, QStringLiteral("dxfs"));
        xml.writeAttribute(QStringLiteral("count"), QString::number(m_dxfs.size()));
        for (const Dxf &format : m_dxfs) {
            xml.writeStartElement(kS, QStringLiteral("dxf"));
            if (format.font.isValid() || format.bold) {
                xml.writeStartElement(kS, QStringLiteral("font"));
                if (format.bold) element(xml, kS, QStringLiteral("b"));
                if (format.font.isValid()) color(QStringLiteral("color"), format.font);
                xml.writeEndElement();
            }
            if (format.fill.isValid()) {
                // A conditional solid fill is its background colour (as Excel writes it).
                xml.writeStartElement(kS, QStringLiteral("fill"));
                xml.writeStartElement(kS, QStringLiteral("patternFill"));
                if (format.pattern == QLatin1String("solid")) {
                    color(QStringLiteral("bgColor"), format.fill);
                } else {
                    xml.writeAttribute(QStringLiteral("patternType"), format.pattern);
                    color(QStringLiteral("fgColor"), format.fill);
                    color(QStringLiteral("bgColor"), Qt::white);
                }
                xml.writeEndElement();
                xml.writeEndElement();
            }
            if (format.top.isValid() || format.bottom.isValid()) {
                xml.writeStartElement(kS, QStringLiteral("border"));
                if (format.top.isValid()) line(QStringLiteral("top"), format.top);
                if (format.bottom.isValid()) line(QStringLiteral("bottom"), format.bottom);
                xml.writeEndElement();
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeEndElement();
    });
}

// Excel's date serial: days since 1899-12-30 (the 1900 date system, ECMA-376 Part 1, 18.17.4.1).
double serial(const QDate &date) { return double(QDate(1899, 12, 30).daysTo(date)); }

QString ref(int column, int row, bool absoluteColumn = false, bool absoluteRow = false) {
    return (absoluteColumn ? QStringLiteral("$") : QString()) + OfficeExport::columnName(column) + (absoluteRow ? QStringLiteral("$") : QString())
           + QString::number(row);
}

QString quotedSheet(const QString &name) { return QLatin1Char('\'') + QString(name).replace(QLatin1Char('\''), QStringLiteral("''")) + QLatin1Char('\''); }

// One worksheet: cells by row and column (1-based rows, 0-based columns), then its layout.
class Sheet {
public:
    struct Cell {
        enum class Type { Blank, Text, Number, Formula } type = Type::Blank;
        QString text;    // text, or the formula's cached text
        double number = 0;
        QString formula;
        bool cachedText = false;
        int style = 0;
    };
    struct ConditionalFormat {
        QString range;
        QString formula;
        int dxf = 0;
    };

    void text(int column, int row, const QString &value, int style = 0) {
        Cell &cell = m_rows[row][column];
        cell.type = Cell::Type::Text;
        cell.text = xmlSafe(value).left(32767);  // Excel's cell limit
        cell.style = style;
    }
    void number(int column, int row, double value, int style = 0) {
        Cell &cell = m_rows[row][column];
        cell.type = Cell::Type::Number;
        cell.number = value;
        cell.style = style;
    }
    void formula(int column, int row, const QString &formula, double cached, int style = 0) {
        Cell &cell = m_rows[row][column];
        cell.type = Cell::Type::Formula;
        cell.formula = formula;
        cell.number = cached;
        cell.style = style;
    }
    void formulaText(int column, int row, const QString &expression, const QString &cached, int style = 0) {
        formula(column, row, expression, 0, style);
        Cell &cell = m_rows[row][column];
        cell.cachedText = true;
        cell.text = cached;
    }
    void blank(int column, int row, int style) { m_rows[row][column].style = style; }
    void width(int column, double characters, bool hidden = false) { m_widths[column] = {characters, hidden}; }
    void height(int row, double points, bool hidden = false) {
        m_heights[row] = {points, hidden};
        m_rows[row];  // written even without cells
    }
    void merge(int firstColumn, int firstRow, int lastColumn, int lastRow) {
        if (firstColumn != lastColumn || firstRow != lastRow) m_merges.append(ref(firstColumn, firstRow) + QLatin1Char(':') + ref(lastColumn, lastRow));
    }
    void conditional(const QString &range, const QString &formula, int dxf) { m_conditions.append({range, formula, dxf}); }
    void freeze(int columns, int rows) {
        m_freezeColumns = columns;
        m_freezeRows = rows;
    }
    void listValidation(const QString &cell, const QString &source) {
        m_validationCell = cell;
        m_validationSource = source;
    }
    void footer(const QString &text) { m_footer = text; }
    QByteArray part(bool selected) const;

private:
    std::map<int, std::map<int, Cell>> m_rows;
    std::map<int, std::pair<double, bool>> m_widths, m_heights;
    QStringList m_merges;
    QList<ConditionalFormat> m_conditions;
    int m_freezeColumns = 0, m_freezeRows = 0;
    QString m_validationCell, m_validationSource;
    QString m_footer;
};

QByteArray Sheet::part(bool selected) const {
    return Ooxml::part([&](QXmlStreamWriter &xml) {
        xml.writeDefaultNamespace(kS);
        xml.writeNamespace(kRel, QStringLiteral("r"));
        xml.writeStartElement(kS, QStringLiteral("worksheet"));
        xml.writeStartElement(kS, QStringLiteral("sheetViews"));
        xml.writeStartElement(kS, QStringLiteral("sheetView"));
        xml.writeAttribute(QStringLiteral("showGridLines"), QStringLiteral("0"));
        if (selected) xml.writeAttribute(QStringLiteral("tabSelected"), QStringLiteral("1"));
        xml.writeAttribute(QStringLiteral("workbookViewId"), QStringLiteral("0"));
        if (m_freezeColumns > 0 || m_freezeRows > 0) {
            const QString pane = m_freezeColumns && m_freezeRows ? QStringLiteral("bottomRight")
                                 : m_freezeRows                  ? QStringLiteral("bottomLeft")
                                                                 : QStringLiteral("topRight");
            QList<std::pair<QString, QString>> attributes;
            if (m_freezeColumns) attributes.append({QStringLiteral("xSplit"), QString::number(m_freezeColumns)});
            if (m_freezeRows) attributes.append({QStringLiteral("ySplit"), QString::number(m_freezeRows)});
            attributes.append({QStringLiteral("topLeftCell"), ref(m_freezeColumns, m_freezeRows + 1)});
            attributes.append({QStringLiteral("activePane"), pane});
            attributes.append({QStringLiteral("state"), QStringLiteral("frozen")});
            element(xml, kS, QStringLiteral("pane"), attributes);
            element(xml, kS, QStringLiteral("selection"), {{QStringLiteral("pane"), pane}});
        }
        xml.writeEndElement();
        xml.writeEndElement();
        element(xml, kS, QStringLiteral("sheetFormatPr"), {{QStringLiteral("defaultRowHeight"), QStringLiteral("15")}});
        if (!m_widths.empty()) {
            xml.writeStartElement(kS, QStringLiteral("cols"));
            for (const auto &[column, width] : m_widths) {
                QList<std::pair<QString, QString>> attributes{{QStringLiteral("min"), QString::number(column + 1)}, {QStringLiteral("max"), QString::number(column + 1)},
                                                              {QStringLiteral("width"), QString::number(width.first)}, {QStringLiteral("customWidth"), QStringLiteral("1")}};
                if (width.second) attributes.append({QStringLiteral("hidden"), QStringLiteral("1")});
                element(xml, kS, QStringLiteral("col"), attributes);
            }
            xml.writeEndElement();
        }
        xml.writeStartElement(kS, QStringLiteral("sheetData"));
        for (const auto &[row, cells] : m_rows) {
            xml.writeStartElement(kS, QStringLiteral("row"));
            xml.writeAttribute(QStringLiteral("r"), QString::number(row));
            if (const auto height = m_heights.find(row); height != m_heights.end()) {
                xml.writeAttribute(QStringLiteral("ht"), QString::number(height->second.first));
                xml.writeAttribute(QStringLiteral("customHeight"), QStringLiteral("1"));
                if (height->second.second) xml.writeAttribute(QStringLiteral("hidden"), QStringLiteral("1"));
            }
            for (const auto &[column, cell] : cells) {
                xml.writeStartElement(kS, QStringLiteral("c"));
                xml.writeAttribute(QStringLiteral("r"), ref(column, row));
                if (cell.style) xml.writeAttribute(QStringLiteral("s"), QString::number(cell.style));
                switch (cell.type) {
                case Cell::Type::Blank: break;
                case Cell::Type::Text:
                    xml.writeAttribute(QStringLiteral("t"), QStringLiteral("inlineStr"));
                    xml.writeStartElement(kS, QStringLiteral("is"));
                    xml.writeStartElement(kS, QStringLiteral("t"));
                    xml.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
                    xml.writeCharacters(cell.text);
                    xml.writeEndElement();
                    xml.writeEndElement();
                    break;
                case Cell::Type::Number: xml.writeTextElement(kS, QStringLiteral("v"), QString::number(cell.number, 'g', 15)); break;
                case Cell::Type::Formula:
                    if (cell.cachedText) xml.writeAttribute(QStringLiteral("t"), QStringLiteral("str"));
                    xml.writeTextElement(kS, QStringLiteral("f"), cell.formula);
                    xml.writeTextElement(kS, QStringLiteral("v"), cell.cachedText ? cell.text : QString::number(cell.number, 'g', 15));
                    break;
                }
                xml.writeEndElement();
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
        if (!m_merges.isEmpty()) {
            xml.writeStartElement(kS, QStringLiteral("mergeCells"));
            xml.writeAttribute(QStringLiteral("count"), QString::number(m_merges.size()));
            for (const QString &merge : m_merges) element(xml, kS, QStringLiteral("mergeCell"), {{QStringLiteral("ref"), merge}});
            xml.writeEndElement();
        }
        // One block per range, the rules in priority order (1 = the first).
        QStringList ranges;
        for (const ConditionalFormat &format : m_conditions)
            if (!ranges.contains(format.range)) ranges.append(format.range);
        int priority = 1;
        for (const QString &range : std::as_const(ranges)) {
            xml.writeStartElement(kS, QStringLiteral("conditionalFormatting"));
            xml.writeAttribute(QStringLiteral("sqref"), range);
            for (const ConditionalFormat &format : m_conditions) {
                if (format.range != range) continue;
                xml.writeStartElement(kS, QStringLiteral("cfRule"));
                xml.writeAttribute(QStringLiteral("type"), QStringLiteral("expression"));
                xml.writeAttribute(QStringLiteral("dxfId"), QString::number(format.dxf));
                xml.writeAttribute(QStringLiteral("priority"), QString::number(priority++));
                xml.writeTextElement(kS, QStringLiteral("formula"), format.formula);
                xml.writeEndElement();
            }
            xml.writeEndElement();
        }
        if (!m_validationCell.isEmpty()) {
            // A list of the periods; any other date is still accepted (a warning only).
            xml.writeStartElement(kS, QStringLiteral("dataValidations"));
            xml.writeAttribute(QStringLiteral("count"), QStringLiteral("1"));
            xml.writeStartElement(kS, QStringLiteral("dataValidation"));
            xml.writeAttribute(QStringLiteral("type"), QStringLiteral("list"));
            xml.writeAttribute(QStringLiteral("errorStyle"), QStringLiteral("information"));
            xml.writeAttribute(QStringLiteral("allowBlank"), QStringLiteral("1"));
            xml.writeAttribute(QStringLiteral("sqref"), m_validationCell);
            xml.writeTextElement(kS, QStringLiteral("formula1"), m_validationSource);
            xml.writeEndElement();
            xml.writeEndElement();
        }
        element(xml, kS, QStringLiteral("pageMargins"), {{QStringLiteral("left"), QStringLiteral("0.4")}, {QStringLiteral("right"), QStringLiteral("0.4")},
                                                         {QStringLiteral("top"), QStringLiteral("0.5")}, {QStringLiteral("bottom"), QStringLiteral("0.6")},
                                                         {QStringLiteral("header"), QStringLiteral("0.3")}, {QStringLiteral("footer"), QStringLiteral("0.3")}});
        element(xml, kS, QStringLiteral("pageSetup"), {{QStringLiteral("paperSize"), QStringLiteral("9")}, {QStringLiteral("orientation"), QStringLiteral("landscape")}});
        if (!m_footer.isEmpty()) {
            xml.writeStartElement(kS, QStringLiteral("headerFooter"));
            xml.writeTextElement(kS, QStringLiteral("oddFooter"), m_footer);
            xml.writeEndElement();
        }
        xml.writeEndElement();
    });
}

// A period of the timeline: a day, a week (Monday to Sunday) or a month.
struct Period {
    QDate first, last;
};
QVector<Period> periods(QDate first, QDate last, GanttChart::Scale unit) {
    QVector<Period> list;
    if (!first.isValid() || !last.isValid()) return list;
    switch (unit) {
    case GanttChart::Scale::Days:
        for (QDate day = first; day <= last; day = day.addDays(1)) list.append({day, day});
        break;
    case GanttChart::Scale::Weeks:
        for (QDate monday = first.addDays(1 - first.dayOfWeek()); monday <= last; monday = monday.addDays(7)) list.append({monday, monday.addDays(6)});
        break;
    case GanttChart::Scale::Months:
        for (QDate month(first.year(), first.month(), 1); month <= last; month = month.addMonths(1))
            list.append({month, month.addDays(month.daysInMonth() - 1)});
        break;
    }
    return list;
}
// Past this many columns the unit is raised (Excel's limit is 16,384; a long plan stays legible).
constexpr int kMaxPeriods = 2000;
constexpr double kLegendCharacters = 70;  // the narrowest legend line (a short plan's)

// The looks shared by the Gantt and the cost sheets: one title, header and row line for all.
struct Looks {
    Palette colors;
    Styles styles;
    explicit Looks(const Palette &palette) : colors(palette), styles(palette.text) {}
    int title() { return styles.of({.bold = true, .size = 18}); }
    int header(const QString &align = QString()) {
        return styles.of({.bold = true, .size = 9, .color = colors.muted, .bottom = colors.headerLine, .align = align, .wrap = true});
    }
    // A table cell: summaries bold on the light grey row, tasks indented.
    int cell(bool summary = false, int indent = 0, const QString &align = QString(), int builtin = 0, const QString &format = QString(),
             bool muted = false) {
        CellLook look{.bold = summary, .color = muted ? colors.muted : QColor(), .bottom = colors.rowLine, .builtinFormat = builtin,
                      .format = format, .align = align, .indent = indent};
        if (summary) look.fill = colors.summary;
        return styles.of(look);
    }
    int total(const QString &align = QString(), const QString &format = QString()) {
        return styles.of({.bold = true, .top = colors.text, .format = format, .align = align});
    }
};

// The calendar header (band of months or years, then the periods, then a hidden row of their last
// days): the Gantt's and the Per week sheet's.
void calendar(Sheet &sheet, Looks &looks, int firstColumn, int bandRow, const QVector<Period> &list, GanttChart::Scale unit, bool endRow) {
    const QString periodFormat = unit == GanttChart::Scale::Days ? QStringLiteral("d") : unit == GanttChart::Scale::Weeks ? QStringLiteral("dd.mm")
                                                                                                                       : QStringLiteral("mmm");
    const QString bandFormat = unit == GanttChart::Scale::Months ? QStringLiteral("yyyy") : QStringLiteral("mmmm yyyy");
    const auto bandOf = [unit](const QDate &date) { return unit == GanttChart::Scale::Months ? date.year() : date.year() * 12 + date.month(); };
    for (int i = 0; i < list.size();) {
        int j = i;
        while (j + 1 < list.size() && bandOf(list.at(j + 1).first) == bandOf(list.at(i).first)) ++j;
        // Alternate the months' (years') fills, as the template does.
        const bool odd = (unit == GanttChart::Scale::Months ? list.at(i).first.year() : list.at(i).first.month()) % 2;
        const int band = looks.styles.of({.bold = true, .size = 9, .fill = odd ? looks.colors.band : looks.colors.summary,
                                          .format = bandFormat, .align = QStringLiteral("left")});
        for (int c = i; c <= j; ++c) sheet.blank(firstColumn + c, bandRow, band);
        sheet.number(firstColumn + i, bandRow, serial(list.at(i).first), band);
        sheet.merge(firstColumn + i, bandRow, firstColumn + j, bandRow);
        i = j + 1;
    }
    const int period = looks.styles.of({.size = 9, .color = looks.colors.muted, .bottom = looks.colors.headerLine, .format = periodFormat,
                                        .align = QStringLiteral("center")});
    for (int c = 0; c < list.size(); ++c) {
        sheet.number(firstColumn + c, bandRow + 1, serial(list.at(c).first), period);
        if (endRow) sheet.number(firstColumn + c, bandRow + 2, serial(list.at(c).last));
    }
    sheet.height(bandRow, 18);
    sheet.height(bandRow + 1, 18);
    if (endRow) sheet.height(bandRow + 2, 0, true);
}

double periodWidth(GanttChart::Scale unit) { return unit == GanttChart::Scale::Days ? 3.6 : unit == GanttChart::Scale::Weeks ? 6.5 : 6.0; }

// The currency's number format: the symbol where the locale puts it, two decimals.
QString currencyFormat(const QString &currency) {
    const QString symbol = QLatin1Char('"') + QString(currency).remove(QLatin1Char('"')) + QLatin1Char('"');
    if (currency.isEmpty()) return QStringLiteral("#,##0.00");
    return QLocale().toCurrencyString(1.0, currency).startsWith(currency) ? symbol + QStringLiteral(" #,##0.00")
                                                                          : QStringLiteral("#,##0.00 ") + symbol;
}
// Hours as Excel time (a day = 1): "7 h 30", the sums past 24 hours.
const QString kHoursFormat = QStringLiteral("[h]\" h \"mm");
double hoursValue(qint64 minutes) { return double(minutes) / (24 * 60); }
double amountValue(qint64 cents) { return double(cents) / 100; }

// A SUM of rows of a column: consecutive rows as one range, at most 255 arguments per SUM (Excel's
// limit for a function), nested past it.
QString sumOf(int column, QList<int> rows) {
    if (rows.isEmpty()) return QStringLiteral("0");
    std::sort(rows.begin(), rows.end());
    QStringList parts;
    for (int i = 0; i < rows.size();) {
        int j = i;
        while (j + 1 < rows.size() && rows.at(j + 1) == rows.at(j) + 1) ++j;
        parts.append(i == j ? ref(column, rows.at(i)) : ref(column, rows.at(i)) + QLatin1Char(':') + ref(column, rows.at(j)));
        i = j + 1;
    }
    constexpr int kMaxArguments = 255;
    while (parts.size() > kMaxArguments) {
        QStringList nested;
        for (qsizetype i = 0; i < parts.size(); i += kMaxArguments)
            nested.append(QStringLiteral("SUM(%1)").arg(parts.mid(i, kMaxArguments).join(QLatin1Char(','))));
        parts = nested;
    }
    return QStringLiteral("SUM(%1)").arg(parts.join(QLatin1Char(',')));
}

// The printed footer: the sheet's name (cut: Excel's header and footer hold 255 characters), page n of N.
QString footer(const QString &name) {
    return QStringLiteral("&L") + xmlSafe(name).left(200).replace(QLatin1Char('&'), QStringLiteral("&&")) + QStringLiteral("&R")
           + tr("Page %1 of %2").arg(QStringLiteral("&P"), QStringLiteral("&N"));
}

struct Workbook {
    QStringList names;
    QList<QByteArray> parts;
    QStringList printTitles;  // per sheet, empty = none
};

// --- the Gantt sheet ---

void ganttSheet(Workbook &book, Looks &looks, const GanttExportSource &source, GanttChart::Scale unit, const QDate &today) {
    using Kind = GanttExportSource::Column::Kind;
    Sheet sheet;
    const Palette &colors = looks.colors;
    const QString name = pagesTr("Gantt");
    // The timeline: the plan's dates and the compared snapshot's; the unit raised past kMaxPeriods.
    QDate first = source.schedule.start, last = source.schedule.end;
    if (source.baseline)
        for (const GanttBaselineTask &task : source.baseline->tasks)
            if (task.dated) {
                first = first.isValid() ? std::min(first, task.start) : task.start;
                last = last.isValid() ? std::max(last, task.end) : task.end;
            }
    QVector<Period> list = periods(first, last, unit);
    while (list.size() > kMaxPeriods && unit != GanttChart::Scale::Months) {
        unit = GanttChart::Scale(int(unit) + 1);
        list = periods(first, last, unit);
    }
    const int columns = int(source.columns.size());
    const int timeline = 1 + columns;  // the first period's column (A is a margin)
    const QColor critical = GanttChart::criticalColor();
    const QColor todayColor = GanttChart::todayColor();
    const auto paintOf = [&](const GanttExportSource::Row &row) {
        const auto look = source.looks.constFind(row.id);
        GanttBarPaint paint = GanttChart::barPaint(source.plan, row.id, look != source.looks.cend() ? &*look : nullptr, row.summary);
        // A topic without fill: the bar light, its done part stronger (on white, not the card).
        const bool filled = paint.fill.alpha() > 0;
        paint.fill = filled ? onWhite(paint.fill) : tint(paint.border, 0.3);
        paint.done = filled ? onWhite(paint.done) : tint(paint.border, 0.6);
        return paint;
    };
    const auto isMilestone = [&](const GanttExportSource::Row &row) {
        const GanttTask *task = source.plan.task(row.id);
        return task && task->milestone && !row.summary;
    };
    const auto isCritical = [&](const GanttExportSource::Row &row) {
        return source.critical && source.schedule.tasks.value(row.id).critical && !row.summary;
    };

    // The legend, as the template's: the kinds of bar present (the first without a colour of its own),
    // the done part, the snapshot, the critical path, the highlighted period.
    struct Entry {
        int style;
        QString text;
        QString mark;
    };
    QList<Entry> entries;
    bool anyCritical = false;
    if (!list.isEmpty()) {
        std::optional<GanttBarPaint> taskSample, summarySample, milestoneSample;
        for (const GanttExportSource::Row &row : source.rows) {
            anyCritical |= isCritical(row);
            if (source.looks.contains(row.id)) continue;
            std::optional<GanttBarPaint> &sample = isMilestone(row) ? milestoneSample : row.summary ? summarySample : taskSample;
            if (!sample) sample = paintOf(row);
        }
        if (taskSample) entries.append({looks.styles.of({.fill = taskSample->fill}), pagesTr("Task"), {}});
        if (summarySample) entries.append({looks.styles.of({.fill = summarySample->fill}), tr("Summary"), {}});
        if (milestoneSample)
            entries.append({looks.styles.of({.bold = true, .size = 11, .color = milestoneSample->fill, .align = QStringLiteral("center")}), tr("Milestone"),
                            QStringLiteral("◆")});
        if (taskSample || summarySample) entries.append({looks.styles.of({.fill = (taskSample ? taskSample : summarySample)->done}), tr("Done"), {}});
        if (source.baseline)
            entries.append({looks.styles.of({.fill = GanttChart::baselineColor(), .pattern = QStringLiteral("lightUp")}),
                            tr("Baseline: %1").arg(source.baseline->name), {}});
        if (anyCritical) entries.append({looks.styles.of({.top = critical, .bottom = critical}), tr("Critical path"), {}});
        entries.append({looks.styles.of({.fill = tint(todayColor, 0.12)}), tr("Highlighted period"), {}});
    }
    // A sample cell, then its words over the next empty cells; the entries wrap at the timeline's
    // width (at least kLegendCharacters), so a short plan prints its legend on the first page.
    const auto legendStep = [unit](const QString &text) { return 1 + int(std::ceil((text.size() * 0.75 + 2) / periodWidth(unit))); };
    const int legendWidth = std::max(int(list.size()), int(std::ceil(kLegendCharacters / periodWidth(unit))));
    QVector<QPoint> placed;  // per entry: its column after the timeline's first, its line
    int legendColumns = 0;
    for (int i = 0, line = 0, offset = 0; i < entries.size(); ++i) {
        const int step = legendStep(entries.at(i).text);
        if (offset > 0 && offset + step > legendWidth) ++line, offset = 0;
        placed.append(QPoint(offset, line));
        offset += step;
        legendColumns = std::max(legendColumns, offset);
    }
    const int legendLines = placed.isEmpty() ? 1 : placed.last().y() + 1;
    constexpr int kTitleRow = 1, kLegendRow = 2;
    const int kBandRow = kLegendRow + legendLines, kPeriodRow = kBandRow + 1, kEndRow = kBandRow + 2, kFirstRow = kBandRow + 3;
    // Hidden after the timeline and the legend: start, end, progress, done until, look, critical, baseline.
    const int helpers = timeline + std::max(int(list.size()), legendColumns) + 1;
    const int hStart = helpers, hEnd = helpers + 1, hProgress = helpers + 2, hDone = helpers + 3, hLook = helpers + 4,
              hCritical = helpers + 5, hBaseStart = helpers + 6, hBaseEnd = helpers + 7;
    const int lastRow = kFirstRow + int(source.rows.size()) - 1;

    sheet.width(0, 2);
    sheet.text(1, kTitleRow, source.sheetName, looks.title());
    sheet.height(kTitleRow, 32);
    for (int line = 0; line < legendLines; ++line) sheet.height(kLegendRow + line, 20);
    // The table's columns, their titles over the band and the periods.
    for (int c = 0; c < columns; ++c) {
        const GanttExportSource::Column &column = source.columns.at(c);
        double width = column.title.size() + 3;
        for (const GanttExportSource::Row &row : source.rows) width = std::max(width, row.cells.value(c).size() + 2.0 + (column.name ? row.depth * 2 : 0));
        if (column.kind == Kind::Start || column.kind == Kind::End) width = std::max(width, 11.0);
        sheet.width(1 + c, column.name ? std::clamp(width, 24.0, 48.0) : std::min(width, 30.0));
        const int style = looks.header(column.right ? QStringLiteral("right") : QString());
        sheet.text(1 + c, kBandRow, column.title, style);
        sheet.blank(1 + c, kPeriodRow, style);
        sheet.merge(1 + c, kBandRow, 1 + c, kPeriodRow);
    }
    for (int c = 0; c < list.size(); ++c) sheet.width(timeline + c, periodWidth(unit));
    for (int c = hStart; c <= hBaseEnd; ++c) sheet.width(c, 10, true);
    if (!list.isEmpty()) calendar(sheet, looks, timeline, kBandRow, list, unit, true);

    // The bars' colours, as the view paints them; one look per colour pair.
    struct BarLook {
        QColor fill, done;
        bool operator==(const BarLook &) const = default;
    };
    QList<BarLook> barLooks;
    const QString firstCell = ref(timeline, kFirstRow);
    const QString body = firstCell + QLatin1Char(':') + ref(timeline + int(list.size()) - 1, lastRow);
    const QString inPeriod = QStringLiteral("%1<=%2,%3>=%4");  // start <= the period's end, end >= its start
    const QString periodStart = ref(timeline, kPeriodRow, false, true), periodEnd = ref(timeline, kEndRow, false, true);
    const auto helper = [&](int column) { return ref(column, kFirstRow, true, false); };
    const QString runs = QStringLiteral("AND(ISNUMBER(%1),%2)").arg(helper(hStart), inPeriod.arg(helper(hStart), periodEnd, helper(hEnd), periodStart));

    for (int i = 0; i < source.rows.size(); ++i) {
        const GanttExportSource::Row &row = source.rows.at(i);
        const int r = kFirstRow + i;
        const GanttScheduledTask scheduled = source.schedule.tasks.value(row.id);
        const bool milestone = isMilestone(row);
        const GanttBarPaint paint = paintOf(row);
        sheet.height(r, 22.5);
        int startColumn = -1, endColumn = -1, progressColumn = -1;
        for (int c = 0; c < columns; ++c) {
            const GanttExportSource::Column &column = source.columns.at(c);
            const QString text = row.cells.value(c);
            const QString align = column.right ? QStringLiteral("right") : QString();
            switch (column.kind) {
            case Kind::Name: sheet.text(1 + c, r, text, looks.cell(row.summary, row.depth, align)); break;
            case Kind::Start:
            case Kind::End: {
                (column.kind == Kind::Start ? startColumn : endColumn) = 1 + c;
                const int style = looks.cell(row.summary, 0, align, 14);  // the locale's short date
                if (scheduled.dated) sheet.number(1 + c, r, serial(column.kind == Kind::Start ? scheduled.start : scheduled.end), style);
                else sheet.blank(1 + c, r, style);
                break;
            }
            case Kind::Duration: {
                bool ok = false;
                const int days = text.toInt(&ok);
                const int style = looks.cell(row.summary, 0, align);
                if (ok) sheet.number(1 + c, r, days, style);
                else sheet.blank(1 + c, r, style);
                break;
            }
            case Kind::Progress: {
                progressColumn = 1 + c;
                const int style = looks.cell(row.summary, 0, align, 9);  // 0%
                if (text.isEmpty()) sheet.blank(1 + c, r, style);
                else sheet.number(1 + c, r, scheduled.progress / 100.0, style);
                break;
            }
            case Kind::Text: sheet.text(1 + c, r, text, looks.cell(row.summary, 0, align, 0, QString(), column.muted)); break;
            }
        }
        // The timeline's cells: the row line, the non-working days shaded (days); a milestone's ◆.
        const int plain = looks.styles.of({.bottom = colors.rowLine});
        const int shaded = looks.styles.of({.fill = colors.shade, .bottom = colors.rowLine});
        for (int c = 0; c < list.size(); ++c) {
            const bool off = unit == GanttChart::Scale::Days && !source.plan.calendar().isWorkingDay(list.at(c).first);
            sheet.blank(timeline + c, r, off ? shaded : plain);
            if (milestone && scheduled.dated && list.at(c).first <= scheduled.start && list.at(c).last >= scheduled.start)
                sheet.text(timeline + c, r, QStringLiteral("◆"),
                           looks.styles.of({.bold = true, .size = 11, .color = scheduled.progress >= GanttPlan::kMaxProgress ? paint.done : paint.fill,
                                            .fill = off ? colors.shade : QColor(), .bottom = colors.rowLine, .align = QStringLiteral("center")}));
        }
        // The helpers: start and end read the shown cells, so editing them in Excel moves the bar.
        const auto dateHelper = [&](int column, int shown, const QDate &date) {
            if (shown >= 0) {
                const QString cell = ref(shown, r);
                const QString formula = QStringLiteral("IF(ISNUMBER(%1),%1,\"\")").arg(cell);
                if (scheduled.dated) sheet.formula(column, r, formula, serial(date));
                else sheet.formulaText(column, r, formula, QString());
            } else if (scheduled.dated) {
                sheet.number(column, r, serial(date));
            }
        };
        dateHelper(hStart, startColumn, scheduled.start);
        dateHelper(hEnd, endColumn, scheduled.end);
        const double progress = scheduled.progress / 100.0;
        if (progressColumn >= 0) sheet.formula(hProgress, r, QStringLiteral("IF(ISNUMBER(%1),%1,0)").arg(ref(progressColumn, r)), progress);
        else sheet.number(hProgress, r, progress);
        const QString s = ref(hStart, r), e = ref(hEnd, r), p = ref(hProgress, r);
        const QString done = QStringLiteral("IF(ISNUMBER(%1),%1+ROUND((%2-%1+1)*%3,0)-1,\"\")").arg(s, e, p);
        if (scheduled.dated)
            sheet.formula(hDone, r, done, serial(scheduled.start) + std::round((scheduled.start.daysTo(scheduled.end) + 1) * progress) - 1);
        else sheet.formulaText(hDone, r, done, QString());
        int look = 0;  // 0: no bar (a milestone is its ◆)
        if (!milestone) {
            const BarLook bar{paint.fill, paint.done};
            qsizetype at = barLooks.indexOf(bar);
            if (at < 0) {
                barLooks.append(bar);
                at = barLooks.size() - 1;
            }
            look = int(at) + 1;
        }
        sheet.number(hLook, r, look);
        sheet.number(hCritical, r, isCritical(row) ? 1 : 0);
        if (source.baseline)
            if (const GanttBaselineTask *before = source.baseline->task(row.id); before && before->dated) {
                sheet.number(hBaseStart, r, serial(before->start));
                sheet.number(hBaseEnd, r, serial(before->end));
            }
    }

    // The highlighted period: today's, or the first; any date can be typed, the list has the periods.
    const QString highlightCell = ref(1, kLegendRow, true, true);
    if (!list.isEmpty()) {
        QDate shown = list.first().first;
        for (const Period &period : std::as_const(list))
            if (period.first <= today && period.last >= today) shown = today;
        sheet.number(1, kLegendRow, serial(shown),
                     looks.styles.of({.size = 9, .color = colors.muted, .format = QLatin1Char('"') + tr("Highlight:").remove(QLatin1Char('"'))
                                                                                     + QStringLiteral(" \"d mmm yyyy"),
                                      .align = QStringLiteral("left")}));
        sheet.listValidation(ref(1, kLegendRow), ref(timeline, kPeriodRow, true, true) + QLatin1Char(':')
                                                     + ref(timeline + int(list.size()) - 1, kPeriodRow, true, true));
    }

    // The bars: per look its done part (a period whose part of the task is all done), then the bar;
    // the critical lines; the snapshot hatched where it is out of the bar; the highlighted period.
    if (!list.isEmpty() && !source.rows.isEmpty()) {
        const QString lookCell = helper(hLook);
        const QString doneRuns = QStringLiteral("AND(ISNUMBER(%1),%2,%3>=MIN(%4,%5))")
                                     .arg(helper(hStart), inPeriod.arg(helper(hStart), periodEnd, helper(hEnd), periodStart), helper(hDone), periodEnd,
                                          helper(hEnd));
        for (int k = 0; k < barLooks.size(); ++k)
            sheet.conditional(body, QStringLiteral("AND(%1=%2,%3)").arg(lookCell).arg(k + 1).arg(doneRuns), looks.styles.dxf({.fill = barLooks.at(k).done}));
        for (int k = 0; k < barLooks.size(); ++k)
            sheet.conditional(body, QStringLiteral("AND(%1=%2,%3)").arg(lookCell).arg(k + 1).arg(runs), looks.styles.dxf({.fill = barLooks.at(k).fill}));
        if (anyCritical)
            sheet.conditional(body, QStringLiteral("AND(%1=1,%2>0,%3)").arg(helper(hCritical), lookCell, runs), looks.styles.dxf({.top = critical, .bottom = critical}));
        if (source.baseline)
            sheet.conditional(body, QStringLiteral("AND(ISNUMBER(%1),%2)").arg(helper(hBaseStart), inPeriod.arg(helper(hBaseStart), periodEnd, helper(hBaseEnd), periodStart)),
                              looks.styles.dxf({.fill = GanttChart::baselineColor(), .pattern = QStringLiteral("lightUp")}));
        const QString highlighted = QStringLiteral("AND(%1<=%2,%3>=%2)").arg(periodStart, highlightCell, periodEnd);
        sheet.conditional(body, highlighted, looks.styles.dxf({.fill = tint(todayColor, 0.12)}));
        sheet.conditional(ref(timeline, kPeriodRow) + QLatin1Char(':') + ref(timeline + int(list.size()) - 1, kPeriodRow), highlighted,
                          looks.styles.dxf({.fill = todayColor, .font = Qt::white, .bold = true}));

        const int words = looks.styles.of({.size = 9, .color = colors.muted, .align = QStringLiteral("left")});
        for (int i = 0; i < entries.size(); ++i) {
            const int column = timeline + placed.at(i).x(), row = kLegendRow + placed.at(i).y();
            if (entries.at(i).mark.isEmpty()) sheet.blank(column, row, entries.at(i).style);
            else sheet.text(column, row, entries.at(i).mark, entries.at(i).style);
            sheet.text(column + 1, row, entries.at(i).text, words);
        }
    }
    sheet.freeze(timeline, kEndRow);
    sheet.footer(footer(source.sheetName));
    book.names.append(name);
    book.parts.append(sheet.part(book.parts.isEmpty()));
    book.printTitles.append(QStringLiteral("$A:$%1,$%2:$%3").arg(OfficeExport::columnName(timeline - 1)).arg(kBandRow).arg(kPeriodRow));
}

// --- the cost sheets, laid out as the Gantt sheet: the same title, header and row lines ---

void costSheets(Workbook &book, Looks &looks, const GanttExportSource &source) {
    const CostData data = costData(source);
    const QString money = currencyFormat(source.currency);
    const QString right = QStringLiteral("right");
    constexpr int kTitleRow = 1, kHeaderRow = 3, kFirstRow = 4;
    const auto begin = [&](Sheet &sheet, const QString &title, const QStringList &headers, const QList<double> &widths, const QList<bool> &rightAligned) {
        sheet.width(0, 2);
        sheet.text(1, kTitleRow, title, looks.title());
        sheet.height(kTitleRow, 32);
        for (int c = 0; c < headers.size(); ++c) {
            sheet.width(1 + c, widths.at(c));
            sheet.text(1 + c, kHeaderRow, headers.at(c), looks.header(rightAligned.at(c) ? right : QString()));
        }
        sheet.height(kHeaderRow, 20);
        sheet.freeze(0, kHeaderRow);
        sheet.footer(footer(source.sheetName));
    };
    const auto refs = [](int column, const QList<int> &rows) { return sumOf(column, rows); };

    // Costs: the tasks in the tree's order; a summary sums its children, the total the top level.
    {
        Sheet sheet;
        double nameWidth = 30;
        for (const GanttExportSource::Row &row : source.rows) nameWidth = std::max(nameWidth, taskName(source, row).size() + 2.0 + row.depth * 2);
        begin(sheet, pagesTr("Costs — %1").arg(source.sheetName), {pagesTr("Task"), pagesTr("People"), pagesTr("Hours"), pagesTr("Cost")},
              {std::min(nameWidth, 60.0), 36, 12, 16}, {false, false, true, true});
        const QVector<QVector<int>> children = childrenOf(source.rows);
        QList<int> topLevel;
        for (int i = 0; i < source.rows.size(); ++i) {
            const GanttExportSource::Row &row = source.rows.at(i);
            const int r = kFirstRow + i;
            if (row.depth == 0) topLevel.append(r);
            const GanttCosts::Amount amount = data.costs.tasks.value(row.id);
            sheet.height(r, 20);
            sheet.text(1, r, taskName(source, row), looks.cell(row.summary, row.depth));
            sheet.text(2, r, data.taskPeople(source, row), looks.cell(row.summary));
            const int hours = looks.cell(row.summary, 0, right, 0, kHoursFormat), cost = looks.cell(row.summary, 0, right, 0, money);
            if (row.summary) {
                QList<int> rows;
                for (int child : children.at(i)) rows.append(kFirstRow + child);
                sheet.formula(3, r, refs(3, rows), hoursValue(amount.minutes), hours);
                sheet.formula(4, r, refs(4, rows), amountValue(amount.cents), cost);
            } else {
                sheet.number(3, r, hoursValue(amount.minutes), hours);
                sheet.number(4, r, amountValue(amount.cents), cost);
            }
        }
        const int total = kFirstRow + int(source.rows.size());
        sheet.height(total, 22);
        sheet.text(1, total, pagesTr("Total"), looks.total());
        sheet.blank(2, total, looks.total());
        sheet.formula(3, total, refs(3, topLevel), hoursValue(data.costs.total.minutes), looks.total(right, kHoursFormat));
        sheet.formula(4, total, refs(4, topLevel), amountValue(data.costs.total.cents), looks.total(right, money));
        book.names.append(pagesTr("Costs"));
        book.parts.append(sheet.part(book.parts.isEmpty()));
        book.printTitles.append(QStringLiteral("$%1:$%1").arg(kHeaderRow));
    }
    // People: rate, hours and cost of each, then the total.
    {
        Sheet sheet;
        begin(sheet, pagesTr("People"), {pagesTr("Person"), pagesTr("Role"), pagesTr("Hourly cost"), pagesTr("Hours"), pagesTr("Cost")},
              {28, 22, 14, 12, 16}, {false, false, true, true, true});
        for (int i = 0; i < data.people.size(); ++i) {
            const GanttResource &person = *data.people.at(i);
            const int r = kFirstRow + i;
            const GanttCosts::Amount amount = data.costs.people.value(person.id);
            sheet.height(r, 20);
            sheet.text(1, r, personName(person), looks.cell());
            sheet.text(2, r, person.role, looks.cell());
            sheet.number(3, r, amountValue(person.hourlyCostCents), looks.cell(false, 0, right, 0, money));
            sheet.number(4, r, hoursValue(amount.minutes), looks.cell(false, 0, right, 0, kHoursFormat));
            sheet.number(5, r, amountValue(amount.cents), looks.cell(false, 0, right, 0, money));
        }
        const int total = kFirstRow + int(data.people.size());
        const auto range = [&](int column) {
            return data.people.isEmpty() ? QStringLiteral("0") : QStringLiteral("SUM(%1:%2)").arg(ref(column, kFirstRow), ref(column, total - 1));
        };
        sheet.height(total, 22);
        sheet.text(1, total, pagesTr("Total"), looks.total());
        sheet.blank(2, total, looks.total());
        sheet.blank(3, total, looks.total());
        sheet.formula(4, total, range(4), hoursValue(data.costs.total.minutes), looks.total(right, kHoursFormat));
        sheet.formula(5, total, range(5), amountValue(data.costs.total.cents), looks.total(right, money));
        book.names.append(pagesTr("People"));
        book.parts.append(sheet.part(false));
        book.printTitles.append(QStringLiteral("$%1:$%1").arg(kHeaderRow));
    }
    // Per week: the Gantt's calendar by ISO week; an Hours and a Cost row per person, totals.
    {
        Sheet sheet;
        const QString title = tr("Per week — %1").arg(source.sheetName);
        sheet.width(0, 2);
        sheet.text(1, kTitleRow, title, looks.title());
        sheet.height(kTitleRow, 32);
        sheet.footer(footer(source.sheetName));
        const QDate first = data.costs.weeks.isEmpty() ? QDate() : data.costs.weeks.firstKey();
        const QDate last = data.costs.weeks.isEmpty() ? QDate() : data.costs.weeks.lastKey();
        const QVector<Period> weeks = periods(first, last, GanttChart::Scale::Weeks);
        constexpr int kBandRow = 3, kWeekRow = 4, kRows = 5, kWeeks = 3;
        const int totalColumn = kWeeks + int(weeks.size());
        sheet.width(1, 26);
        sheet.width(2, 9);
        const int header = looks.header(), headerRight = looks.header(right);
        sheet.text(1, kBandRow, pagesTr("Person"), header);
        sheet.blank(1, kWeekRow, header);
        sheet.merge(1, kBandRow, 1, kWeekRow);
        sheet.blank(2, kBandRow, header);
        sheet.blank(2, kWeekRow, header);
        sheet.merge(2, kBandRow, 2, kWeekRow);
        if (weeks.isEmpty()) {
            sheet.text(1, kRows, tr("No dated tasks with people."), looks.styles.of({.color = looks.colors.muted}));
        } else {
            calendar(sheet, looks, kWeeks, kBandRow, weeks, GanttChart::Scale::Weeks, false);
            for (int c = 0; c < weeks.size(); ++c) sheet.width(kWeeks + c, 11);
            sheet.width(totalColumn, 15);
            sheet.text(totalColumn, kBandRow, pagesTr("Total"), headerRight);
            sheet.blank(totalColumn, kWeekRow, headerRight);
            sheet.merge(totalColumn, kBandRow, totalColumn, kWeekRow);
            const QString hoursWord = pagesTr("Hours"), costWord = pagesTr("Cost");
            QList<int> hourRows, costRows;
            const auto rowSum = [&](int r) {
                return QStringLiteral("SUM(%1:%2)").arg(ref(kWeeks, r), ref(totalColumn - 1, r));
            };
            for (int i = 0; i < data.people.size(); ++i) {
                const GanttResource &person = *data.people.at(i);
                const int r = kRows + 2 * i;
                hourRows.append(r);
                costRows.append(r + 1);
                sheet.text(1, r, personName(person), looks.cell(false, 0, QString()));
                sheet.blank(1, r + 1, looks.cell());
                sheet.merge(1, r, 1, r + 1);
                sheet.text(2, r, hoursWord, looks.cell(false, 0, QString(), 0, QString(), true));
                sheet.text(2, r + 1, costWord, looks.cell(false, 0, QString(), 0, QString(), true));
                qint64 minutes = 0, cents = 0;
                for (int c = 0; c < weeks.size(); ++c) {
                    const GanttCosts::Amount amount = data.costs.weeks.value(weeks.at(c).first).value(person.id);
                    minutes += amount.minutes;
                    cents += amount.cents;
                    const int hours = looks.cell(false, 0, right, 0, kHoursFormat), cost = looks.cell(false, 0, right, 0, money);
                    if (amount.minutes) sheet.number(kWeeks + c, r, hoursValue(amount.minutes), hours);
                    else sheet.blank(kWeeks + c, r, hours);
                    if (amount.cents) sheet.number(kWeeks + c, r + 1, amountValue(amount.cents), cost);
                    else sheet.blank(kWeeks + c, r + 1, cost);
                }
                sheet.formula(totalColumn, r, rowSum(r), hoursValue(minutes), looks.cell(true, 0, right, 0, kHoursFormat));
                sheet.formula(totalColumn, r + 1, rowSum(r + 1), amountValue(cents), looks.cell(true, 0, right, 0, money));
            }
            // The totals of each week: hours, then cost.
            const int totalRow = kRows + 2 * int(data.people.size());
            sheet.text(1, totalRow, pagesTr("Total"), looks.total());
            sheet.blank(1, totalRow + 1, looks.total());
            sheet.merge(1, totalRow, 1, totalRow + 1);
            sheet.text(2, totalRow, hoursWord, looks.total());
            sheet.text(2, totalRow + 1, costWord, looks.styles.of({.bold = true}));
            for (int c = kWeeks; c <= totalColumn; ++c) {
                qint64 minutes = 0, cents = 0;
                for (const GanttResource *person : data.people) {
                    const GanttCosts::Amount amount = c == totalColumn ? data.costs.people.value(person->id)
                                                                       : data.costs.weeks.value(weeks.at(c - kWeeks).first).value(person->id);
                    minutes += amount.minutes;
                    cents += amount.cents;
                }
                sheet.formula(c, totalRow, refs(c, hourRows), hoursValue(minutes), looks.total(right, kHoursFormat));
                sheet.formula(c, totalRow + 1, refs(c, costRows), amountValue(cents), looks.styles.of({.bold = true, .format = money, .align = right}));
            }
            sheet.freeze(kWeeks, kWeekRow);
        }
        book.names.append(tr("Per week"));
        book.parts.append(sheet.part(false));
        book.printTitles.append(weeks.isEmpty() ? QString() : QStringLiteral("$A:$C,$%1:$%2").arg(kBandRow).arg(kWeekRow));
    }
}

// --- Word ---

constexpr int kWordPageWidth = 16838, kWordPageHeight = 11906;  // landscape A4, twips
constexpr int kWordMargin = 567;                                // 10 mm, as the PDF pages
constexpr int kWordBottom = 510;                                // 9 mm: a page's picture never spills over
constexpr int kWordText = kWordPageWidth - 2 * kWordMargin;
constexpr qint64 kEmuPerPixel = 9525;  // a view pixel is 1/96 inch (ECMA-376 Part 1, 20.1.2.1)
constexpr int kPictureScale = 2;

void wordRun(QXmlStreamWriter &xml, const QString &text, bool bold, const QColor &color, int halfPoints) {
    xml.writeStartElement(kW, QStringLiteral("r"));
    xml.writeStartElement(kW, QStringLiteral("rPr"));
    if (bold) element(xml, kW, QStringLiteral("b"));
    if (color.isValid()) element(xml, kW, QStringLiteral("color"), {{QStringLiteral("w:val"), hex(color)}});
    if (halfPoints) {
        element(xml, kW, QStringLiteral("sz"), {{QStringLiteral("w:val"), QString::number(halfPoints)}});
        element(xml, kW, QStringLiteral("szCs"), {{QStringLiteral("w:val"), QString::number(halfPoints)}});
    }
    xml.writeEndElement();
    xml.writeStartElement(kW, QStringLiteral("t"));
    xml.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
    xml.writeCharacters(xmlSafe(text));
    xml.writeEndElement();
    xml.writeEndElement();
}

struct WordRow {
    QStringList cells;
    bool header = false;
    bool summary = false;
    bool total = false;
    int indent = 0;  // twips, first column
};

// A table at the text's width: the header repeated on every page, rows never split, the PDF's lines.
void wordTable(QXmlStreamWriter &xml, const QVector<int> &widths, const QVector<bool> &rightAligned, const QList<WordRow> &rows, const Palette &colors) {
    xml.writeStartElement(kW, QStringLiteral("tbl"));
    xml.writeStartElement(kW, QStringLiteral("tblPr"));
    element(xml, kW, QStringLiteral("tblW"), {{QStringLiteral("w:w"), QString::number(kWordText)}, {QStringLiteral("w:type"), QStringLiteral("dxa")}});
    element(xml, kW, QStringLiteral("tblLayout"), {{QStringLiteral("w:type"), QStringLiteral("fixed")}});
    xml.writeEndElement();
    xml.writeStartElement(kW, QStringLiteral("tblGrid"));
    for (int width : widths) element(xml, kW, QStringLiteral("gridCol"), {{QStringLiteral("w:w"), QString::number(width)}});
    xml.writeEndElement();
    for (const WordRow &row : rows) {
        xml.writeStartElement(kW, QStringLiteral("tr"));
        xml.writeStartElement(kW, QStringLiteral("trPr"));
        element(xml, kW, QStringLiteral("cantSplit"));
        if (row.header) element(xml, kW, QStringLiteral("tblHeader"));
        xml.writeEndElement();
        for (int c = 0; c < widths.size(); ++c) {
            xml.writeStartElement(kW, QStringLiteral("tc"));
            xml.writeStartElement(kW, QStringLiteral("tcPr"));
            element(xml, kW, QStringLiteral("tcW"), {{QStringLiteral("w:w"), QString::number(widths.at(c))}, {QStringLiteral("w:type"), QStringLiteral("dxa")}});
            if (!row.total) {
                xml.writeStartElement(kW, QStringLiteral("tcBorders"));
                element(xml, kW, QStringLiteral("bottom"), {{QStringLiteral("w:val"), QStringLiteral("single")}, {QStringLiteral("w:sz"), QStringLiteral("4")},
                                                            {QStringLiteral("w:space"), QStringLiteral("0")},
                                                            {QStringLiteral("w:color"), hex(row.header ? colors.headerLine : colors.rowLine)}});
                xml.writeEndElement();
            }
            if (row.summary)
                element(xml, kW, QStringLiteral("shd"), {{QStringLiteral("w:val"), QStringLiteral("clear")}, {QStringLiteral("w:color"), QStringLiteral("auto")},
                                                         {QStringLiteral("w:fill"), hex(colors.summary)}});
            element(xml, kW, QStringLiteral("vAlign"), {{QStringLiteral("w:val"), QStringLiteral("center")}});
            xml.writeEndElement();
            xml.writeStartElement(kW, QStringLiteral("p"));
            xml.writeStartElement(kW, QStringLiteral("pPr"));
            element(xml, kW, QStringLiteral("spacing"), {{QStringLiteral("w:before"), QStringLiteral("60")}, {QStringLiteral("w:after"), QStringLiteral("60")}});
            if (c == 0 && row.indent) element(xml, kW, QStringLiteral("ind"), {{QStringLiteral("w:left"), QString::number(row.indent)}});
            if (rightAligned.at(c)) element(xml, kW, QStringLiteral("jc"), {{QStringLiteral("w:val"), QStringLiteral("right")}});
            xml.writeEndElement();
            wordRun(xml, row.cells.value(c), row.header || row.summary || row.total, row.header ? colors.muted : colors.text, 18);
            xml.writeEndElement();
            xml.writeEndElement();
        }
        xml.writeEndElement();
    }
    xml.writeEndElement();
}

void wordParagraph(QXmlStreamWriter &xml, const QString &style, bool pageBreak, const std::function<void()> &content) {
    xml.writeStartElement(kW, QStringLiteral("p"));
    xml.writeStartElement(kW, QStringLiteral("pPr"));
    if (!style.isEmpty()) element(xml, kW, QStringLiteral("pStyle"), {{QStringLiteral("w:val"), style}});
    if (pageBreak) element(xml, kW, QStringLiteral("pageBreakBefore"));
    if (style.isEmpty()) element(xml, kW, QStringLiteral("spacing"), {{QStringLiteral("w:after"), QStringLiteral("0")}});
    xml.writeEndElement();
    if (content) content();
    xml.writeEndElement();
}

// --- Markdown ---

// A cell's text: the characters Markdown would read as formatting or HTML escaped (CommonMark 2.4).
QString markdownCell(const QString &text) {
    QString escaped;
    for (const QChar c : text.simplified()) {
        if (QStringLiteral("\\|*_`<>[]").contains(c)) escaped += QLatin1Char('\\');
        escaped += c;
    }
    return escaped;
}
QString markdownRow(const QStringList &cells) { return QStringLiteral("| ") + cells.join(QStringLiteral(" | ")) + QStringLiteral(" |"); }
QString markdownRule(const QVector<bool> &rightAligned) {
    QStringList rules;
    for (bool right : rightAligned) rules.append(right ? QStringLiteral("---:") : QStringLiteral("---"));
    return QStringLiteral("|") + rules.join(QLatin1Char('|')) + QStringLiteral("|");
}
QString bold(const QString &text) { return text.isEmpty() ? text : QStringLiteral("**") + text + QStringLiteral("**"); }
QString indented(const QString &text, int depth) {
    QString lead;
    for (int i = 0; i < depth; ++i) lead += QStringLiteral("&emsp;");
    return lead + text;
}

}  // namespace

QByteArray xlsx(const GanttExportSource &source, GanttPages::Content content, GanttChart::Scale unit, const QString &title, const QDate &today) {
    Looks looks(palette());
    Workbook book;
    if (content != GanttPages::Content::Costs) ganttSheet(book, looks, source, unit, today);
    if (content != GanttPages::Content::Gantt) costSheets(book, looks, source);
    const QStringList names = OfficeExport::worksheetNames(book.names);
    ZipWriter zip;
    QList<Relationship> relations;
    QStringList overrides{QStringLiteral("/xl/workbook.xml|%1sheet.main+xml").arg(kSheetType), QStringLiteral("/xl/styles.xml|%1styles+xml").arg(kSheetType)};
    for (int i = 0; i < book.parts.size(); ++i) {
        const QString path = QStringLiteral("worksheets/sheet%1.xml").arg(i + 1);
        zip.add(QStringLiteral("xl/") + path, book.parts.at(i));
        relations.append({QStringLiteral("worksheet"), path});
        overrides.append(QStringLiteral("/xl/%1|%2worksheet+xml").arg(path, kSheetType));
    }
    relations.append({QStringLiteral("styles"), QStringLiteral("styles.xml")});
    zip.add(QStringLiteral("xl/workbook.xml"), part([&](QXmlStreamWriter &xml) {
        xml.writeDefaultNamespace(kS);
        xml.writeNamespace(kRel, QStringLiteral("r"));
        xml.writeStartElement(kS, QStringLiteral("workbook"));
        xml.writeStartElement(kS, QStringLiteral("sheets"));
        for (int i = 0; i < names.size(); ++i) {
            xml.writeStartElement(kS, QStringLiteral("sheet"));
            xml.writeAttribute(QStringLiteral("name"), names[i]);
            xml.writeAttribute(QStringLiteral("sheetId"), QString::number(i + 1));
            xml.writeAttribute(kRel, QStringLiteral("id"), QStringLiteral("rId%1").arg(i + 1));
            xml.writeEndElement();
        }
        xml.writeEndElement();
        // Printing repeats the header (and the Gantt's table) on every page.
        xml.writeStartElement(kS, QStringLiteral("definedNames"));
        for (int i = 0; i < names.size(); ++i) {
            if (book.printTitles.at(i).isEmpty()) continue;
            QStringList parts;
            for (const QString &range : book.printTitles.at(i).split(QLatin1Char(','))) parts.append(quotedSheet(names[i]) + QLatin1Char('!') + range);
            xml.writeStartElement(kS, QStringLiteral("definedName"));
            xml.writeAttribute(QStringLiteral("name"), QStringLiteral("_xlnm.Print_Titles"));
            xml.writeAttribute(QStringLiteral("localSheetId"), QString::number(i));
            xml.writeCharacters(parts.join(QLatin1Char(',')));
            xml.writeEndElement();
        }
        xml.writeEndElement();
        element(xml, kS, QStringLiteral("calcPr"), {{QStringLiteral("calcId"), QStringLiteral("191029")}, {QStringLiteral("fullCalcOnLoad"), QStringLiteral("1")}});
        xml.writeEndElement();
    }));
    zip.add(QStringLiteral("xl/_rels/workbook.xml.rels"), part([&](QXmlStreamWriter &xml) { relationships(xml, relations); }));
    zip.add(QStringLiteral("xl/styles.xml"), looks.styles.part());
    finish(zip, QStringLiteral("xl/workbook.xml"), title, overrides);
    return zip.archive();
}

QByteArray docx(const GanttExportSource &source, GanttPages::Content content, GanttChart::Scale unit, const QString &title, QString *error) {
    const Palette colors = palette();
    // The chart's pages as the PDF lays them out (landscape A4, fit width), cut at their margins.
    QList<QImage> pictures;
    if (content != GanttPages::Content::Costs) {
        GanttPages::Options options;
        options.content = GanttPages::Content::Gantt;
        options.scale = unit;
        const GanttPages pages(source, options);
        if (!pages.isValid()) {
            if (error) *error = pages.error();
            return {};
        }
        for (int i = 0; i < pages.count(); ++i) {
            const QSizeF page = pages.pageSizePx(i);
            QImage image((page * kPictureScale).toSize(), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            pages.draw(i, &painter, QRectF(QPointF(0, 0), page * kPictureScale));
            painter.end();
            const int margin = int(std::lround(GanttPages::kMarginPx * kPictureScale));
            pictures.append(image.copy(margin, margin, image.width() - 2 * margin, image.height() - 2 * margin).convertToFormat(QImage::Format_RGB32));
        }
    }
    const CostData data = costData(source);
    QList<Relationship> relations{{QStringLiteral("styles"), QStringLiteral("styles.xml")}};
    ZipWriter zip;
    const QByteArray document = part([&](QXmlStreamWriter &xml) {
        xml.writeNamespace(kW, QStringLiteral("w"));
        xml.writeNamespace(kRel, QStringLiteral("r"));
        xml.writeNamespace(kWp, QStringLiteral("wp"));
        xml.writeNamespace(kA, QStringLiteral("a"));
        xml.writeNamespace(kPic, QStringLiteral("pic"));
        xml.writeStartElement(kW, QStringLiteral("document"));
        xml.writeStartElement(kW, QStringLiteral("body"));
        for (int i = 0; i < pictures.size(); ++i) {
            const QString name = QStringLiteral("gantt%1.png").arg(i + 1);
            zip.add(QStringLiteral("word/media/") + name, png(pictures.at(i)));
            relations.append({QStringLiteral("image"), QStringLiteral("media/") + name});
            const QString id = QStringLiteral("rId%1").arg(relations.size());
            const qint64 cx = std::llround(double(pictures.at(i).width()) / kPictureScale * kEmuPerPixel);
            const qint64 cy = std::llround(double(pictures.at(i).height()) / kPictureScale * kEmuPerPixel);
            wordParagraph(xml, QString(), i > 0, [&] { OfficeExport::inlinePicture(xml, i + 1, name, id, cx, cy); });
        }
        if (content != GanttPages::Content::Gantt) {
            wordParagraph(xml, QStringLiteral("Heading1"), !pictures.isEmpty(),
                          [&] { wordRun(xml, pagesTr("Costs — %1").arg(source.sheetName), false, QColor(), 0); });
            const int hours = 1500, amount = 2100, flexible = kWordText - hours - amount;
            QList<WordRow> tasks{{{pagesTr("Task"), pagesTr("People"), pagesTr("Hours"), pagesTr("Cost")}, true}};
            for (const GanttExportSource::Row &row : source.rows) {
                const GanttCosts::Amount sum = data.costs.tasks.value(row.id);
                tasks.append({{taskName(source, row), data.taskPeople(source, row), GanttPages::hoursText(sum.minutes),
                               GanttPages::amountText(sum.cents, source.currency)},
                              false, row.summary, false, row.depth * 240});
            }
            tasks.append({{pagesTr("Total"), QString(), GanttPages::hoursText(data.costs.total.minutes),
                           GanttPages::amountText(data.costs.total.cents, source.currency)},
                          false, false, true});
            const int name = flexible * 55 / 100;
            wordTable(xml, {name, flexible - name, hours, amount}, {false, false, true, true}, tasks, colors);
            wordParagraph(xml, QString(), false, {});
            QList<WordRow> people{{{pagesTr("Person"), pagesTr("Role"), pagesTr("Hourly cost"), pagesTr("Hours"), pagesTr("Cost")}, true}};
            for (const GanttResource *person : data.people) {
                const GanttCosts::Amount sum = data.costs.people.value(person->id);
                people.append({{personName(*person), person->role, pagesTr("%1 / h").arg(GanttPages::amountText(person->hourlyCostCents, source.currency)),
                                GanttPages::hoursText(sum.minutes), GanttPages::amountText(sum.cents, source.currency)}});
            }
            people.append({{pagesTr("Total"), QString(), QString(), GanttPages::hoursText(data.costs.total.minutes),
                            GanttPages::amountText(data.costs.total.cents, source.currency)},
                           false, false, true});
            const int third = flexible * 30 / 100;
            wordTable(xml, {flexible - 2 * third, third, third, hours, amount}, {false, false, true, true, true}, people, colors);
        }
        xml.writeStartElement(kW, QStringLiteral("sectPr"));
        element(xml, kW, QStringLiteral("pgSz"), {{QStringLiteral("w:w"), QString::number(kWordPageWidth)}, {QStringLiteral("w:h"), QString::number(kWordPageHeight)},
                                                  {QStringLiteral("w:orient"), QStringLiteral("landscape")}});
        const QString margin = QString::number(kWordMargin);
        element(xml, kW, QStringLiteral("pgMar"), {{QStringLiteral("w:top"), margin}, {QStringLiteral("w:right"), margin},
                                                   {QStringLiteral("w:bottom"), QString::number(kWordBottom)}, {QStringLiteral("w:left"), margin},
                                                   {QStringLiteral("w:header"), QStringLiteral("0")}, {QStringLiteral("w:footer"), QStringLiteral("0")},
                                                   {QStringLiteral("w:gutter"), QStringLiteral("0")}});
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeEndElement();
    });
    zip.add(QStringLiteral("word/document.xml"), document);
    zip.add(QStringLiteral("word/styles.xml"), OfficeExport::wordStyles());
    zip.add(QStringLiteral("word/_rels/document.xml.rels"), part([&](QXmlStreamWriter &xml) { relationships(xml, relations); }));
    finish(zip, QStringLiteral("word/document.xml"), title,
           {QStringLiteral("/word/document.xml|%1document.main+xml").arg(kWordType), QStringLiteral("/word/styles.xml|%1styles+xml").arg(kWordType)});
    return zip.archive();
}

QString markdown(const GanttExportSource &source, GanttPages::Content content) {
    QStringList lines;
    if (content != GanttPages::Content::Costs) {
        lines << QStringLiteral("# ") + tr("%1 — Gantt").arg(markdownCell(source.sheetName)) << QString();
        QStringList titles;
        QVector<bool> right;
        for (const GanttExportSource::Column &column : source.columns) {
            titles.append(markdownCell(column.title));
            right.append(column.right);
        }
        lines << markdownRow(titles) << markdownRule(right);
        for (const GanttExportSource::Row &row : source.rows) {
            QStringList cells;
            for (int c = 0; c < source.columns.size(); ++c) {
                QString text = markdownCell(row.cells.value(c));
                if (source.columns.at(c).name) text = indented(row.summary ? bold(text) : text, row.depth);
                cells.append(text);
            }
            lines << markdownRow(cells);
        }
        lines << QString();
    }
    if (content != GanttPages::Content::Gantt) {
        const CostData data = costData(source);
        lines << QStringLiteral("## ") + markdownCell(pagesTr("Costs — %1").arg(source.sheetName)) << QString();
        lines << markdownRow({pagesTr("Task"), pagesTr("People"), pagesTr("Hours"), pagesTr("Cost")}) << markdownRule({false, false, true, true});
        for (const GanttExportSource::Row &row : source.rows) {
            const GanttCosts::Amount sum = data.costs.tasks.value(row.id);
            QStringList cells{markdownCell(taskName(source, row)), markdownCell(data.taskPeople(source, row)), GanttPages::hoursText(sum.minutes),
                              markdownCell(GanttPages::amountText(sum.cents, source.currency))};
            if (row.summary) for (QString &cell : cells) cell = bold(cell);
            cells[0] = indented(cells[0], row.depth);
            lines << markdownRow(cells);
        }
        const QString hours = bold(GanttPages::hoursText(data.costs.total.minutes));
        const QString cost = bold(markdownCell(GanttPages::amountText(data.costs.total.cents, source.currency)));
        lines << markdownRow({bold(pagesTr("Total")), QString(), hours, cost}) << QString();
        lines << markdownRow({pagesTr("Person"), pagesTr("Role"), pagesTr("Hourly cost"), pagesTr("Hours"), pagesTr("Cost")})
              << markdownRule({false, false, true, true, true});
        for (const GanttResource *person : data.people) {
            const GanttCosts::Amount sum = data.costs.people.value(person->id);
            lines << markdownRow({markdownCell(personName(*person)), markdownCell(person->role),
                                  markdownCell(pagesTr("%1 / h").arg(GanttPages::amountText(person->hourlyCostCents, source.currency))),
                                  GanttPages::hoursText(sum.minutes), markdownCell(GanttPages::amountText(sum.cents, source.currency))});
        }
        lines << markdownRow({bold(pagesTr("Total")), QString(), QString(), hours, cost}) << QString();
    }
    return lines.join(QLatin1Char('\n'));
}

QString write(Format format, const QString &path, const GanttExportSource &source, GanttPages::Content content, GanttChart::Scale unit,
              const QString &title) {
    QByteArray bytes;
    switch (format) {
    case Format::Word: {
        QString error;
        bytes = docx(source, content, unit, title, &error);
        if (!error.isEmpty()) return error;
        break;
    }
    case Format::Excel: bytes = xlsx(source, content, unit, title); break;
    case Format::Markdown: bytes = markdown(source, content).toUtf8(); break;
    }
    return OfficeExport::writeFile(path, bytes);
}

}  // namespace GanttOffice
