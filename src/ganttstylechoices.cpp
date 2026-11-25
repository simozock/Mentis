#include "ganttstylechoices.h"

#include "branchpath.h"  // the map's end marks
#include "ganttcanvas.h"
#include "ganttplan.h"
#include "nodesketch.h"  // the Fill pictures

#include <QCoreApplication>
#include <QPainter>
#include <QPainterPath>

namespace GanttStyleChoices {

namespace {

constexpr quint64 kIconSeed = 7;  // the map's icons' strokes

}  // namespace

QList<StyleChoices::Choice> choices(List list) {
    switch (list) {
    case List::BarShape: {
        using Bar = GanttPlan::BarShape;
        return {{int(Bar::Pill), QCoreApplication::translate("GanttTaskPanel", "Pill")},
                {int(Bar::Rounded), QCoreApplication::translate("GanttTaskPanel", "Rounded rectangle")},
                {int(Bar::Rectangle), QCoreApplication::translate("GanttTaskPanel", "Rectangle")},
                {int(Bar::Arrow), QCoreApplication::translate("GanttTaskPanel", "Arrow")},
                {int(Bar::Hexagon), QCoreApplication::translate("GanttTaskPanel", "Hexagon")},
                {int(Bar::Leaf), QCoreApplication::translate("GanttTaskPanel", "Leaf")},
                {int(Bar::Ticket), QCoreApplication::translate("GanttTaskPanel", "Ticket")},
                {int(Bar::Parallelogram), QCoreApplication::translate("GanttTaskPanel", "Parallelogram")}};
    }
    case List::SummaryShape: {
        using Summary = GanttPlan::SummaryShape;
        return {{int(Summary::Bar), QCoreApplication::translate("GanttTaskPanel", "Bar")},
                {int(Summary::Bracket), QCoreApplication::translate("GanttTaskPanel", "Bracket")},
                {int(Summary::Brace), QCoreApplication::translate("GanttTaskPanel", "Brace")},
                {int(Summary::IBeam), QCoreApplication::translate("GanttTaskPanel", "Horizontal I")},
                {int(Summary::Hook), QCoreApplication::translate("GanttTaskPanel", "Hook")},
                {int(Summary::Dimension), QCoreApplication::translate("GanttTaskPanel", "Dimension line")},
                {int(Summary::Arrow), QCoreApplication::translate("GanttTaskPanel", "Arrow")},
                {int(Summary::Rail), QCoreApplication::translate("GanttTaskPanel", "Pinned rail")},
                {int(Summary::Frame), QCoreApplication::translate("GanttTaskPanel", "Group frame")}};
    }
    case List::MilestoneShape: {
        using Shape = GanttPlan::MilestoneShape;
        return {{int(Shape::Diamond), QCoreApplication::translate("GanttTaskPanel", "Diamond")},
                {int(Shape::Circle), QCoreApplication::translate("GanttTaskPanel", "Circle")},
                {int(Shape::Triangle), QCoreApplication::translate("GanttTaskPanel", "Triangle")},
                {int(Shape::Star), QCoreApplication::translate("GanttTaskPanel", "Star")},
                {int(Shape::Hexagon), QCoreApplication::translate("GanttTaskPanel", "Hexagon")},
                {int(Shape::Heart), QCoreApplication::translate("GanttTaskPanel", "Heart")},
                {int(Shape::Flag), QCoreApplication::translate("GanttTaskPanel", "Flag")},
                {int(Shape::Gem), QCoreApplication::translate("GanttTaskPanel", "Gem")}};
    }
    case List::Fill:
        return {{int(NodeFillStyle::Pieno), QCoreApplication::translate("GanttTaskPanel", "Filled")},
                {int(NodeFillStyle::PienoManoLibera), QCoreApplication::translate("GanttTaskPanel", "Hand-drawn fill")},
                {int(NodeFillStyle::PennellateDiagonali), QCoreApplication::translate("GanttTaskPanel", "Diagonal strokes")},
                {int(NodeFillStyle::TratteggioDiagonale), QCoreApplication::translate("GanttTaskPanel", "Diagonal hatching")},
                {int(NodeFillStyle::PennellateOrizzontali), QCoreApplication::translate("GanttTaskPanel", "Horizontal strokes")},
                {int(NodeFillStyle::TratteggioOrizzontale), QCoreApplication::translate("GanttTaskPanel", "Horizontal hatching")},
                {int(NodeFillStyle::Nessuno), QCoreApplication::translate("GanttTaskPanel", "No fill")}};
    case List::Border: {
        // In the order of the map's line selects (user, 2026-10-29): each line beside its hand-drawn
        // one, then dotted; None a wide tile at the bottom of the grid.
        using Border = GanttPlan::BorderStyle;
        return {{int(Border::Solid), QCoreApplication::translate("GanttTaskPanel", "Solid line")},
                {int(Border::HandDrawnSolid), QCoreApplication::translate("GanttTaskPanel", "Hand-drawn solid")},
                {int(Border::Dashed), QCoreApplication::translate("GanttTaskPanel", "Dashed line")},
                {int(Border::HandDrawnDashed), QCoreApplication::translate("GanttTaskPanel", "Hand-drawn dashed")},
                {int(Border::Dotted), QCoreApplication::translate("GanttTaskPanel", "Dotted")},
                {int(Border::None), QCoreApplication::translate("GanttTaskPanel", "None")}};
    }
    case List::LinkLine:
        return {{int(GanttPlan::LinkLine::Solid), QCoreApplication::translate("GanttTaskPanel", "Solid line")},
                {int(GanttPlan::LinkLine::Dashed), QCoreApplication::translate("GanttTaskPanel", "Dashed line")},
                {int(GanttPlan::LinkLine::Dotted), QCoreApplication::translate("GanttTaskPanel", "Dotted")}};
    case List::LinkEnd:
        return {{int(BranchEnd::None), QCoreApplication::translate("GanttTaskPanel", "None")},
                {int(BranchEnd::OpenArrow), QCoreApplication::translate("GanttTaskPanel", "Open arrow")},
                {int(BranchEnd::FilledArrow), QCoreApplication::translate("GanttTaskPanel", "Filled arrow")},
                {int(BranchEnd::Dot), QCoreApplication::translate("GanttTaskPanel", "Dot")},
                {int(BranchEnd::Square), QCoreApplication::translate("GanttTaskPanel", "Square")},
                {int(BranchEnd::Diamond), QCoreApplication::translate("GanttTaskPanel", "Diamond")}};
    case List::LinkWidth: {
        using Width = GanttPlan::LinkWidth;
        return {{int(Width::Thin), QCoreApplication::translate("GanttTaskPanel", "Thin")},
                {int(Width::Medium), QCoreApplication::translate("GanttTaskPanel", "Medium")},
                {int(Width::Bold), QCoreApplication::translate("GanttTaskPanel", "Bold")},
                {int(Width::ExtraBold), QCoreApplication::translate("GanttTaskPanel", "Extra bold")}};
    }
    case List::LinkEndSize: {
        using Size = GanttPlan::LinkEndSize;
        return {{int(Size::Small), QCoreApplication::translate("GanttTaskPanel", "Small")},
                {int(Size::Medium), QCoreApplication::translate("GanttTaskPanel", "Medium")},
                {int(Size::Large), QCoreApplication::translate("GanttTaskPanel", "Large")}};
    }
    }
    return {};
}

int columns(List list) {
    switch (list) {
    case List::SummaryShape:
    case List::MilestoneShape:
    case List::LinkWidth: return 4;
    case List::LinkLine:
    case List::LinkEnd:
    case List::LinkEndSize: return 3;
    default: return 2;
    }
}

int noneValue(List list) { return list == List::Border ? int(GanttPlan::BorderStyle::None) : -1; }

void drawIcon(List list, int value, QPainter &painter, const QRectF &box, const QColor &text) {
    painter.save();
    switch (list) {
    case List::BarShape:
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(text, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(GanttCanvas::taskPath(GanttPlan::BarShape(value), box.adjusted(0, box.height() * 0.15, 0, -box.height() * 0.15)));
        break;
    case List::SummaryShape: {
        // A bar as the others, or a thin shape of the chart (GanttCanvas::summaryPath).
        using Summary = GanttPlan::SummaryShape;
        const auto shape = Summary(value);
        painter.setPen(QPen(text, 1.5));
        painter.setBrush(Qt::NoBrush);
        if (shape == Summary::Bar) {
            const QRectF bar = box.adjusted(0, box.height() * 0.15, 0, -box.height() * 0.15);
            painter.drawRoundedRect(bar, bar.height() / 2, bar.height() / 2);
            break;
        }
        painter.setRenderHint(QPainter::Antialiasing);
        if (shape == Summary::Frame) {  // the header over a dashed frame, as on the chart
            const QRectF frame = box.adjusted(1, box.height() * 0.1, -1, -box.height() * 0.1);
            const QRectF header(frame.left(), frame.top(), frame.width(), frame.height() * 0.4);
            painter.setPen(QPen(text, 1.2, Qt::DashLine, Qt::FlatCap));
            painter.drawRoundedRect(frame, header.height() / 2, header.height() / 2);
            painter.setPen(QPen(text, 1.5));
            painter.drawPath(GanttCanvas::summaryPath(shape, header));
            break;
        }
        painter.drawPath(GanttCanvas::summaryPath(shape, box.adjusted(0, box.height() * 0.2, 0, -box.height() * 0.2)));
        break;
    }
    case List::MilestoneShape: {
        // The chart's own outlines (GanttCanvas::milestonePath), in the diamond's 18 x 14 proportions.
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(text, 1.5));
        painter.setBrush(Qt::NoBrush);
        const qreal height = box.height() * 0.8;
        const QRectF place(box.center().x() - height * 9 / 14, box.center().y() - height / 2, height * 18 / 14, height);
        painter.drawPath(GanttCanvas::milestonePath(GanttPlan::MilestoneShape(value), place));
        break;
    }
    case List::Fill: {
        // A bar filled as the map's Fill select draws them (NodeSketch), on a box scaled to the icon's
        // height so three or four lines of a pattern show; No fill a faint box crossed out.
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal scale = box.height() / 28;
        painter.translate(box.topLeft());
        painter.scale(scale, scale);
        const QRectF area = QRectF(QPointF(0, 0), box.size() / scale).adjusted(3, 6, -3, -6);
        QPainterPath outline;
        outline.addRoundedRect(area, area.height() / 2, area.height() / 2);
        const auto fill = NodeFillStyle(value);
        switch (fill) {
        case NodeFillStyle::Pieno: painter.fillPath(outline, text); break;
        case NodeFillStyle::PienoManoLibera: painter.fillPath(NodeSketch::roughArea(outline, kIconSeed), text); break;
        case NodeFillStyle::Nessuno: {
            QColor faint = text;
            faint.setAlphaF(0.45);
            painter.setPen(QPen(faint, 1.5 / scale));
            painter.drawPath(outline);
            painter.drawLine(area.bottomLeft(), area.topRight());
            break;
        }
        default: {
            const NodeSketch::Pattern pattern = NodeSketch::pattern(fill, outline, kIconSeed);
            painter.setPen(QPen(text, pattern.penWidth * (scale < 0.5 ? 0.75 : 1.0), Qt::SolidLine, Qt::FlatCap));
            painter.drawLines(pattern.lines);
        }
        }
        break;
    }
    case List::Border: {
        // The outline of a bar in each pattern (GanttCanvas::borderPen); None a faint box crossed out.
        const auto border = GanttPlan::BorderStyle(value);
        const QRectF bar = box.adjusted(1, box.height() * 0.2, -1, -box.height() * 0.2);
        if (border == GanttPlan::BorderStyle::None) {
            QColor faint = text;
            faint.setAlphaF(0.45);
            painter.setPen(QPen(faint, 1.5));
            painter.drawRoundedRect(bar, 3, 3);
            painter.drawLine(bar.bottomLeft(), bar.topRight());
            break;
        }
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath outline;
        outline.addRoundedRect(bar, bar.height() / 2, bar.height() / 2);
        if (GanttCanvas::isHandDrawn(border)) outline = NodeSketch::roughOutline(outline, kIconSeed);
        painter.strokePath(outline, GanttCanvas::borderPen(border, text, 1.5));
        break;
    }
    case List::LinkLine: {
        const qreal y = box.center().y();
        painter.setPen(QPen(text, 1.5, GanttCanvas::linkPenStyle(GanttPlan::LinkLine(value)), Qt::FlatCap));
        painter.drawLine(QPointF(box.left() + 3, y), QPointF(box.right() - 3, y));
        break;
    }
    case List::LinkEnd: {
        // The map's end marks (StylePage's line selects).
        const auto end = BranchEnd(value);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal y = box.center().y();
        const QPointF tip(box.right() - 3, y);
        painter.setPen(QPen(text, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(box.left() + 3, y), tip);
        if (!BranchPath::endIsStroke(end)) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(text);
        }
        painter.drawPath(BranchPath::endMark(end, tip, QPointF(1, 0), 2.5));
        break;
    }
    case List::LinkWidth: {
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal y = box.center().y();
        painter.setPen(QPen(text, GanttCanvas::linkWidthPx(GanttPlan::LinkWidth(value)), Qt::SolidLine, Qt::FlatCap));
        painter.drawLine(QPointF(box.left() + 3, y), QPointF(box.right() - 3, y));
        break;
    }
    case List::LinkEndSize: {
        // The filled arrow at each size, as on the chart.
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal y = box.center().y(), length = GanttCanvas::linkEndSizePx(GanttPlan::LinkEndSize(value)) * 5 / 6;  // the large one fits 14 px
        const QPointF tip(box.right() - 3, y);
        painter.setPen(QPen(text, 1.5, Qt::SolidLine, Qt::FlatCap));
        painter.drawLine(QPointF(box.left() + 3, y), QPointF(tip.x() - length, y));
        painter.setPen(Qt::NoPen);
        painter.setBrush(text);
        painter.drawPolygon(QPolygonF{tip, QPointF(tip.x() - length, y - length * 2 / 3), QPointF(tip.x() - length, y + length * 2 / 3)});
        break;
    }
    }
    painter.restore();
}

}  // namespace GanttStyleChoices
