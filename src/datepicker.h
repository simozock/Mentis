#pragma once
// The app's calendar (user, 2026-10-28: Qt's own calendar did not match the app): a frameless popup
// card painted in one widget, opened from DateField. Used by the Gantt's task panel and table.
#include <QColor>
#include <QDate>
#include <QDateEdit>
#include <QLocale>
#include <QPointer>
#include <QWidget>

class DatePicker final : public QWidget {
    Q_OBJECT
public:
    struct Colors {
        QColor card = Qt::white, text, muted, line, hover, accent;
    };
    static constexpr int kPadding = 12;
    static constexpr int kCell = 32;            // day cells; the highlight leaves 2 px around
    static constexpr int kHeaderHeight = 28;    // month and arrows
    static constexpr int kWeekdayHeight = 24;
    static constexpr int kFooterHeight = 28;    // Today
    static constexpr int kShadowSide = 4, kShadowTop = 3, kShadowBottom = 5;  // as FloatingGroup

    // A popup child of `parent` (an editor in a table keeps its editing state while it is open).
    DatePicker(QWidget *parent, const Colors &colors);

    // The locale of the interface language (FEAT-046) for month and day names, which follow the
    // language chosen in Settings rather than the system's (BUG-035). The first day of the week stays
    // the system's: it belongs to the region, not to the language.
    static QLocale namesLocale();

    void setRange(const QDate &minimum, const QDate &maximum);
    void setDate(const QDate &date);   // the selected day; invalid = none, the month of today
    QDate date() const { return m_date; }
    QDate focusDate() const { return m_focus; }  // the day the keys move
    int month() const { return m_month.month(); }
    int year() const { return m_month.year(); }
    void showMonth(const QDate &anyDay);
    // What the header shows: the month and year ("January 2027"), the day's two letters ("Mo").
    QString monthTitle() const;
    QString weekdayName(int day) const;  // day: 1 = Monday
    void popup(const QRect &globalAnchor);  // below the anchor, above it when there is no room

    // Geometry for the tests (widget coordinates).
    QRect dayRect(const QDate &date) const;
    QRect previousRect() const;
    QRect nextRect() const;
    QRect todayRect() const;

signals:
    void picked(const QDate &date);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QRect box() const;      // the card, inside the shadow
    QDate firstShown() const;
    QDate dayAt(const QPoint &pos) const;
    bool selectable(const QDate &date) const;
    void moveFocus(const QDate &date);
    void stepMonth(int months);  // arrows, wheel, Page Up/Down
    void pick(const QDate &date);

    Colors m_colors;
    QLocale m_names = namesLocale();  // read once: the language applies from the next start
    QDate m_minimum, m_maximum, m_date, m_focus;
    QDate m_month;          // first day of the month shown
    QPoint m_hover{-1, -1};
};

// A date field with a chevron at the right that opens DatePicker (click, F4, Alt+Up/Down); the day is
// typed as in QDateEdit. Style sheets give its drop-down 24 px and no arrow (the chevron is painted).
class DateField : public QDateEdit {
    Q_OBJECT
public:
    explicit DateField(QWidget *parent = nullptr);
    void setPickerColors(const DatePicker::Colors &colors);
    void setChevronColor(const QColor &color);
    void openPicker();
    DatePicker *picker() const { return m_picker && m_picker->isVisible() ? m_picker.data() : nullptr; }  // the open calendar

    // The panels' arrow, also on their selects.
    static void paintChevron(QWidget *field, const QColor &color);

signals:
    void picked(const QDate &date);  // after the field has the day

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    DatePicker::Colors m_colors;
    QColor m_chevron;
    QPointer<DatePicker> m_picker;
};
