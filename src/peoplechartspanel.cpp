#include "peoplechartspanel.h"

#include "uitheme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace {

constexpr int kRowPx = 24;         // Days per person, a row
constexpr int kNameWidthPx = 96;
constexpr int kValueWidthPx = 40;
constexpr int kBarPx = 8;
constexpr int kLegendRowPx = 20;
constexpr int kLoadHeightPx = 120;
constexpr int kLoadHeadroomPx = 8;  // above the tallest column
constexpr int kColumnGapPx = 3;

int columnWidth(GanttCanvas::Scale scale) {
    switch (scale) {
    case GanttCanvas::Scale::Days: return 10;
    case GanttCanvas::Scale::Weeks: return 20;
    case GanttCanvas::Scale::Months: return 28;
    }
    return 10;
}

QLabel *title(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("font-size:14px; font-weight:600;"));
    return label;
}

QLabel *muted(QWidget *parent) {
    auto *label = new QLabel(parent);
    label->setStyleSheet(UiTheme::qss(QStringLiteral("color:@textMuted; font-size:12px;")));
    return label;
}

// A figure of the summary: its caption, the value under it.
QWidget *figure(const QString &caption, QLabel **value, const QString &name, QWidget *parent) {
    auto *box = new QWidget(parent);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    QLabel *text = muted(box);
    text->setText(caption);
    layout->addWidget(text);
    *value = new QLabel(box);
    (*value)->setObjectName(name);
    (*value)->setStyleSheet(QStringLiteral("font-size:20px; font-weight:600;"));
    layout->addWidget(*value);
    return box;
}

// Days per person: the name, a bar as long as the person's days against the busiest, the value.
class DayBars final : public QWidget {
public:
    DayBars(const PeopleChartsPanel *panel, QWidget *parent) : QWidget(parent), m_panel(panel) {
        setObjectName(QStringLiteral("ResourcesChartsDays"));
    }
    void refresh() {
        setFixedHeight(int(m_panel->charts().days.size()) * kRowPx);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        const GanttPeopleCharts &charts = m_panel->charts();
        qint64 most = 1;
        for (const GanttPeopleCharts::Bar &bar : charts.days) most = std::max(most, bar.minutes);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QFontMetrics metrics(font());
        const int barSpace = std::max(0, width() - kNameWidthPx - kValueWidthPx);
        for (int i = 0; i < charts.days.size(); ++i) {
            const GanttPeopleCharts::Bar &bar = charts.days.at(i);
            const QRect row(0, i * kRowPx, width(), kRowPx);
            painter.setPen(UiTheme::color(UiTheme::Token::Text));
            painter.drawText(QRect(row.left(), row.top(), kNameWidthPx - 8, row.height()), Qt::AlignLeft | Qt::AlignVCenter,
                             metrics.elidedText(bar.name, Qt::ElideRight, kNameWidthPx - 8));
            const int length = int(qint64(barSpace) * bar.minutes / most);
            if (length > 0) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(GanttPeopleCharts::barColor());
                painter.drawRoundedRect(QRectF(kNameWidthPx, row.center().y() - kBarPx / 2.0 + 1, length, kBarPx), 2, 2);
            }
            painter.setPen(UiTheme::color(UiTheme::Token::TextMuted));
            painter.drawText(QRect(kNameWidthPx + length + 6, row.top(), kValueWidthPx, row.height()), Qt::AlignLeft | Qt::AlignVCenter,
                             charts.dayCount(bar.minutes));
        }
    }

private:
    const PeopleChartsPanel *m_panel;
};

// Cost by role: per currency (FEAT-128) one bar split by role, then the legend with the
// percentages; with more than one currency, the currency above its bar.
class RoleBars final : public QWidget {
public:
    RoleBars(const PeopleChartsPanel *panel, QWidget *parent) : QWidget(parent), m_panel(panel) {
        setObjectName(QStringLiteral("ResourcesChartsRoles"));
    }
    void refresh() {
        const GanttPeopleCharts &charts = m_panel->charts();
        int height = 0;
        for (const GanttPeopleCharts::RoleBar &bar : charts.roleBars) height += barHeight(charts, bar);
        setFixedHeight(std::max(0, height - kBarGapPx));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        const GanttPeopleCharts &charts = m_panel->charts();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QFontMetrics metrics(font());
        int top = 0;
        for (const GanttPeopleCharts::RoleBar &bar : charts.roleBars) {
            int y = top;
            if (const QString title = charts.roleBarTitle(bar); !title.isEmpty()) {
                painter.setPen(UiTheme::color(UiTheme::Token::TextMuted));
                painter.drawText(QRect(0, y, width(), kLegendRowPx), Qt::AlignLeft | Qt::AlignVCenter, title);
                y += kLegendRowPx;
            }
            QPainterPath track;
            track.addRoundedRect(QRectF(0, y, width(), 12), 3, 3);
            painter.setClipPath(track);
            qint64 before = 0;
            for (const GanttPeopleCharts::Role &role : bar.roles) {
                const qreal left = qreal(width()) * before / bar.cents;
                before += role.cents;
                const qreal right = qreal(width()) * before / bar.cents;
                painter.fillRect(QRectF(left, y, right - left, 12), role.color);
            }
            painter.setClipping(false);
            for (int i = 0; i < bar.roles.size(); ++i) {
                const GanttPeopleCharts::Role &role = bar.roles.at(i);
                const QRect row(0, y + 20 + i * kLegendRowPx, width(), kLegendRowPx);
                painter.setPen(Qt::NoPen);
                painter.setBrush(role.color);
                painter.drawRoundedRect(QRectF(0, row.center().y() - 4 + 1, 8, 8), 2, 2);
                const QString percent = QStringLiteral("%1%").arg(role.percent);
                const int percentWidth = metrics.horizontalAdvance(percent);
                painter.setPen(UiTheme::color(UiTheme::Token::Text));
                painter.drawText(QRect(16, row.top(), width() - 24 - percentWidth, row.height()), Qt::AlignLeft | Qt::AlignVCenter,
                                 metrics.elidedText(role.name, Qt::ElideRight, width() - 24 - percentWidth));
                painter.setPen(UiTheme::color(UiTheme::Token::TextMuted));
                painter.drawText(row, Qt::AlignRight | Qt::AlignVCenter, percent);
            }
            top += barHeight(charts, bar);
        }
    }

private:
    static constexpr int kBarGapPx = 8;  // between two currencies' bars
    static int barHeight(const GanttPeopleCharts &charts, const GanttPeopleCharts::RoleBar &bar) {
        return (charts.roleBarTitle(bar).isEmpty() ? 0 : kLegendRowPx) + 12 + 8 + int(bar.roles.size()) * kLegendRowPx + kBarGapPx;
    }
    const PeopleChartsPanel *m_panel;
};

// The load: a column per period, its height the person's hours against what the period holds, a
// dashed line at 100 %, overload in danger. Wider than its area: wheel and drag scroll it sideways.
class LoadChart final : public QWidget {
public:
    LoadChart(const PeopleChartsPanel *panel, QScrollArea *area) : QWidget(area), m_panel(panel), m_area(area) {
        setObjectName(QStringLiteral("ResourcesChartsLoad"));
        setFixedHeight(kLoadHeightPx);
    }
    void refresh() {
        const GanttPeopleCharts &charts = m_panel->charts();
        const int person = m_panel->person();
        const int count = person >= 0 && person < charts.people.size() ? int(charts.people.at(person).load.size()) : 0;
        setMinimumWidth(count * (columnWidth(charts.scale) + kColumnGapPx));
        update();
    }
    int periodAt(int x) const {
        const int pitch = columnWidth(m_panel->charts().scale) + kColumnGapPx;
        const int index = x / pitch;
        return x >= 0 && x % pitch < columnWidth(m_panel->charts().scale) ? index : -1;
    }

protected:
    const QVector<GanttPeopleCharts::Period> *load() const {
        const GanttPeopleCharts &charts = m_panel->charts();
        const int person = m_panel->person();
        return person >= 0 && person < charts.people.size() ? &charts.people.at(person).load : nullptr;
    }
    void paintEvent(QPaintEvent *) override {
        const QVector<GanttPeopleCharts::Period> *periods = load();
        if (!periods) return;
        int top = 100;
        for (const GanttPeopleCharts::Period &period : *periods) top = std::max(top, period.percent());
        const qreal room = height() - kLoadHeadroomPx;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int column = columnWidth(m_panel->charts().scale);
        for (int i = 0; i < periods->size(); ++i) {
            const GanttPeopleCharts::Period &period = periods->at(i);
            const qreal tall = room * period.percent() / top;
            if (tall <= 0) continue;
            painter.setPen(Qt::NoPen);
            painter.setBrush(period.overloaded() ? GanttPeopleCharts::overloadColor() : GanttPeopleCharts::barColor());
            painter.drawRoundedRect(QRectF(i * (column + kColumnGapPx), height() - tall, column, tall), 2, 2);
        }
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(UiTheme::color(UiTheme::Token::Divider), 1));
        painter.drawLine(0, height() - 1, width(), height() - 1);
        QPen dashed(UiTheme::color(UiTheme::Token::TextMuted), 1, Qt::DashLine);
        painter.setPen(dashed);
        const int full = int(height() - room * 100 / top);
        painter.drawLine(0, full, width(), full);
    }
    bool event(QEvent *event) override {
        if (event->type() == QEvent::ToolTip) {
            auto *help = static_cast<QHelpEvent *>(event);
            const QVector<GanttPeopleCharts::Period> *periods = load();
            const int index = periodAt(help->pos().x());
            if (periods && index >= 0 && index < periods->size())
                QToolTip::showText(help->globalPos(), m_panel->charts().periodTip(periods->at(index)), this);
            else
                QToolTip::hideText();
            return true;
        }
        return QWidget::event(event);
    }
    void wheelEvent(QWheelEvent *event) override {
        QScrollBar *bar = m_area->horizontalScrollBar();
        const QPoint delta = event->angleDelta();
        const int steps = delta.x() != 0 ? delta.x() : delta.y();
        if (bar->maximum() <= 0 || steps == 0) {
            event->ignore();  // the modal's own scrolling
            return;
        }
        bar->setValue(bar->value() - steps / 2);
        event->accept();
    }
    void mousePressEvent(QMouseEvent *event) override {
        m_dragX = event->globalPosition().x();
        m_dragValue = m_area->horizontalScrollBar()->value();
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (!(event->buttons() & Qt::LeftButton)) return;
        m_area->horizontalScrollBar()->setValue(m_dragValue - int(event->globalPosition().x() - m_dragX));
        event->accept();
    }

private:
    const PeopleChartsPanel *m_panel;
    QScrollArea *m_area;
    qreal m_dragX = 0;
    int m_dragValue = 0;
};

// The progress meter of the person's tasks.
class Meter final : public QWidget {
public:
    Meter(const PeopleChartsPanel *panel, QWidget *parent) : QWidget(parent), m_panel(panel) {
        setFixedHeight(kBarPx);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        const GanttPeopleCharts &charts = m_panel->charts();
        const int person = m_panel->person();
        if (person < 0 || person >= charts.people.size()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(UiTheme::color(UiTheme::Token::Divider));
        painter.drawRoundedRect(QRectF(rect()), kBarPx / 2.0, kBarPx / 2.0);
        const qreal done = qreal(width()) * std::clamp(charts.people.at(person).progressPercent(), 0, 100) / 100;
        if (done <= 0) return;
        painter.setBrush(GanttPeopleCharts::barColor());
        painter.drawRoundedRect(QRectF(0, 0, done, height()), kBarPx / 2.0, kBarPx / 2.0);
    }

private:
    const PeopleChartsPanel *m_panel;
};

}  // namespace

PeopleChartsPanel::PeopleChartsPanel(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ResourcesCharts"));
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(UiTheme::qss(QStringLiteral("QWidget#ResourcesCharts { background:@surfaceSunken; border-radius:4px; }")));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(0);

    m_empty = muted(this);
    m_empty->setObjectName(QStringLiteral("ResourcesChartsEmpty"));
    m_empty->setText(GanttPeopleCharts::emptyText());
    m_empty->setWordWrap(true);
    m_empty->setStyleSheet(UiTheme::qss(QStringLiteral("color:@textMuted;")));
    layout->addWidget(m_empty);

    // The team summary.
    m_summary = new QWidget(this);
    auto *summary = new QVBoxLayout(m_summary);
    summary->setContentsMargins(0, 0, 0, 0);
    summary->setSpacing(8);
    auto *figures = new QHBoxLayout;
    figures->setContentsMargins(0, 0, 0, 0);
    figures->setSpacing(16);
    figures->addWidget(figure(GanttPeopleCharts::tr("Planned cost"), &m_cost, QStringLiteral("ResourcesChartsCost"), m_summary), 1);
    figures->addWidget(figure(GanttPeopleCharts::tr("Person-days"), &m_personDays, QStringLiteral("ResourcesChartsPersonDays"), m_summary), 1);
    summary->addLayout(figures);
    summary->addSpacing(8);
    summary->addWidget(title(GanttPeopleCharts::tr("Days per person"), m_summary));
    auto *days = new DayBars(this, m_summary);
    m_dayBars = days;
    summary->addWidget(days);
    summary->addSpacing(8);
    m_rolesTitle = title(GanttPeopleCharts::tr("Cost by role"), m_summary);
    summary->addWidget(m_rolesTitle);
    auto *roles = new RoleBars(this, m_summary);
    m_roles = roles;
    summary->addWidget(roles);
    layout->addWidget(m_summary);

    // A person.
    m_personPage = new QWidget(this);
    auto *person = new QVBoxLayout(m_personPage);
    person->setContentsMargins(0, 0, 0, 0);
    person->setSpacing(8);
    m_name = new QLabel(m_personPage);
    m_name->setObjectName(QStringLiteral("ResourcesChartsName"));
    m_name->setStyleSheet(QStringLiteral("font-size:16px; font-weight:600;"));
    person->addWidget(m_name);
    m_line = muted(m_personPage);
    m_line->setObjectName(QStringLiteral("ResourcesChartsLine"));
    person->addWidget(m_line);
    person->addSpacing(8);
    person->addWidget(title(GanttPeopleCharts::tr("Load"), m_personPage));
    m_loadArea = new QScrollArea(m_personPage);
    m_loadArea->setObjectName(QStringLiteral("ResourcesChartsLoadArea"));
    m_loadArea->setFrameShape(QFrame::NoFrame);
    m_loadArea->setWidgetResizable(true);
    m_loadArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_loadArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_loadArea->setStyleSheet(QStringLiteral("QScrollArea { background:transparent; }"));
    m_loadArea->viewport()->setAutoFillBackground(false);
    auto *load = new LoadChart(this, m_loadArea);
    m_load = load;
    m_loadArea->setWidget(load);
    m_loadArea->setFixedHeight(kLoadHeightPx + m_loadArea->horizontalScrollBar()->sizeHint().height());
    person->addWidget(m_loadArea);
    person->addSpacing(8);
    person->addWidget(title(GanttPeopleCharts::tr("Progress of the tasks"), m_personPage));
    auto *meter = new Meter(this, m_personPage);
    m_meter = meter;
    person->addWidget(meter);
    m_progressText = muted(m_personPage);
    m_progressText->setObjectName(QStringLiteral("ResourcesChartsProgress"));
    person->addWidget(m_progressText);
    layout->addWidget(m_personPage);
    layout->addStretch();
    setCharts({}, -1);
}

void PeopleChartsPanel::setCharts(const GanttPeopleCharts &charts, int person) {
    m_charts = charts;
    m_person = person >= 0 && person < charts.people.size() ? person : -1;
    const bool shownPerson = m_person >= 0;
    m_empty->setVisible(charts.empty() && !shownPerson);
    m_summary->setVisible(!charts.empty() && !shownPerson);
    m_personPage->setVisible(shownPerson);
    m_cost->setText(charts.costText());
    m_personDays->setText(charts.personDays());
    static_cast<DayBars *>(m_dayBars)->refresh();
    const bool roles = !charts.roleBars.isEmpty();
    m_rolesTitle->setVisible(roles);
    m_roles->setVisible(roles);
    static_cast<RoleBars *>(m_roles)->refresh();
    if (shownPerson) {
        const GanttPeopleCharts::Person &shown = charts.people.at(m_person);
        m_name->setText(shown.name);
        m_line->setText(charts.personLine(shown));
        m_progressText->setText(charts.progressLine(shown));
    }
    static_cast<LoadChart *>(m_load)->refresh();
    m_meter->update();
}

void PeopleChartsPanel::fitTexts(int widthPx) {
    m_empty->ensurePolished();
    const int inner = widthPx - 2 * 16;
    if (const int height = m_empty->heightForWidth(inner); height >= 0) m_empty->setMinimumHeight(height);
}
