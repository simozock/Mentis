#include "markers.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QHash>
#include <QImageReader>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace Markers {

namespace {
// Colori leggibili sul bianco e sui rami colorati (stesso blu dei collegamenti per la priorita').
QColor priorityColor(int value) {
    static const QColor colors[] = {QColor("#e5484d"), QColor("#f76b15"), QColor("#f5a623"),
                                    QColor("#30a46c"), QColor("#3e63dd"), QColor("#8e4ec6")};
    return colors[std::clamp(value, 1, 6) - 1];
}
QColor flagColor(const QString &value) {
    if (value == QLatin1String("red")) return QColor("#e5484d");
    if (value == QLatin1String("orange")) return QColor("#f76b15");
    if (value == QLatin1String("yellow")) return QColor("#f5a623");
    if (value == QLatin1String("green")) return QColor("#30a46c");
    if (value == QLatin1String("blue")) return QColor("#3e63dd");
    return QColor("#8e4ec6");
}
}  // namespace

const QList<Series> &series() {
    static const QList<Series> list{
        {QStringLiteral("priority"), {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
                                      QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6")}},
        {QStringLiteral("progress"), {QStringLiteral("0"), QStringLiteral("25"), QStringLiteral("50"),
                                      QStringLiteral("75"), QStringLiteral("100")}},
        {QStringLiteral("flag"), {QStringLiteral("red"), QStringLiteral("orange"), QStringLiteral("yellow"),
                                  QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("purple")}},
        {QStringLiteral("symbol"), {QStringLiteral("star"), QStringLiteral("question"), QStringLiteral("warning"),
                                    QStringLiteral("idea")}},
    };
    return list;
}

namespace {
QHash<QString, QByteArray> &images() {
    static QHash<QString, QByteArray> registry;
    return registry;
}
QHash<QString, QString> &customLabels() {
    static QHash<QString, QString> labels;
    return labels;
}
}  // namespace

bool isCustom(const QString &marker) { return marker.startsWith(QLatin1String("custom:")); }
QString customImage(const QString &marker) { return isCustom(marker) ? marker.section(QLatin1Char(':'), 2) : QString(); }
QString customMarker(const QString &group, const QString &sha) { return QStringLiteral("custom:") + group + QLatin1Char(':') + sha; }
void registerImage(const QString &sha, const QByteArray &data) { if (!sha.isEmpty() && !data.isEmpty()) images().insert(sha, data); }
QByteArray image(const QString &sha) { return images().value(sha); }
void setCustomLabel(const QString &sha, const QString &label) { customLabels().insert(sha, label); }

QString seriesOf(const QString &marker) {
    if (isCustom(marker)) return marker.section(QLatin1Char(':'), 0, 1);
    return marker.section(QLatin1Char(':'), 0, 0);
}

bool isValid(const QString &marker) {
    if (isCustom(marker)) return !marker.section(QLatin1Char(':'), 1, 1).isEmpty() && customImage(marker).size() == 64;
    const QString name = seriesOf(marker), value = marker.section(QLatin1Char(':'), 1);
    for (const Series &s : series())
        if (s.id == name) return s.values.contains(value);
    return false;
}

QString next(const QString &marker) {
    if (isCustom(marker)) return marker;  // i gruppi propri si cambiano dal pannello
    const QString name = seriesOf(marker), value = marker.section(QLatin1Char(':'), 1);
    for (const Series &s : series())
        if (s.id == name) {
            const int at = int(s.values.indexOf(value));
            return name + QLatin1Char(':') + s.values.value((at + 1) % s.values.size());
        }
    return marker;
}

QStringList with(const QStringList &markers, const QString &seriesId, const QString &marker) {
    QStringList result;
    for (const Series &s : series()) {
        if (s.id == seriesId) {
            if (!marker.isEmpty()) result.append(marker);
            continue;
        }
        for (const QString &own : markers)
            if (seriesOf(own) == s.id) { result.append(own); break; }
    }
    // Poi i gruppi propri, nell'ordine in cui sono stati messi (uno per gruppo).
    bool placed = false;
    for (const QString &own : markers) {
        if (!isCustom(own)) continue;
        if (seriesOf(own) == seriesId) {
            if (!marker.isEmpty()) result.append(marker);
            placed = true;
        } else {
            result.append(own);
        }
    }
    if (!placed && seriesId.startsWith(QLatin1String("custom:")) && !marker.isEmpty()) result.append(marker);
    return result;
}

namespace {
// A text of the Markers context, translated or as written (English).
QString text(bool translated, const char *source) {
    return translated ? QCoreApplication::translate("Markers", source) : QString::fromUtf8(source);
}
}  // namespace

QString seriesLabel(const QString &id, bool translated) {
    if (id == QLatin1String("priority")) return text(translated, QT_TRANSLATE_NOOP("Markers", "Priority"));
    if (id == QLatin1String("progress")) return text(translated, QT_TRANSLATE_NOOP("Markers", "Progress"));
    if (id == QLatin1String("flag")) return text(translated, QT_TRANSLATE_NOOP("Markers", "Flag"));
    return text(translated, QT_TRANSLATE_NOOP("Markers", "Symbol"));
}

QString label(const QString &marker, bool translated) {
    if (isCustom(marker)) {
        const QString own = customLabels().value(customImage(marker));
        return own.isEmpty() ? text(translated, QT_TRANSLATE_NOOP("Markers", "Custom marker")) : own;
    }
    const QString name = seriesOf(marker), value = marker.section(QLatin1Char(':'), 1);
    if (name == QLatin1String("priority")) return text(translated, QT_TRANSLATE_NOOP("Markers", "Priority %1")).arg(value);
    if (name == QLatin1String("progress")) return text(translated, QT_TRANSLATE_NOOP("Markers", "Progress %1%")).arg(value);
    if (name == QLatin1String("flag")) {
        if (value == QLatin1String("red")) return text(translated, QT_TRANSLATE_NOOP("Markers", "Red flag"));
        if (value == QLatin1String("orange")) return text(translated, QT_TRANSLATE_NOOP("Markers", "Orange flag"));
        if (value == QLatin1String("yellow")) return text(translated, QT_TRANSLATE_NOOP("Markers", "Yellow flag"));
        if (value == QLatin1String("green")) return text(translated, QT_TRANSLATE_NOOP("Markers", "Green flag"));
        if (value == QLatin1String("blue")) return text(translated, QT_TRANSLATE_NOOP("Markers", "Blue flag"));
        return text(translated, QT_TRANSLATE_NOOP("Markers", "Purple flag"));
    }
    if (value == QLatin1String("star")) return text(translated, QT_TRANSLATE_NOOP("Markers", "Star"));
    if (value == QLatin1String("question")) return text(translated, QT_TRANSLATE_NOOP("Markers", "Question"));
    if (value == QLatin1String("warning")) return text(translated, QT_TRANSLATE_NOOP("Markers", "Warning"));
    return text(translated, QT_TRANSLATE_NOOP("Markers", "Idea"));
}

void paint(QPainter *p, const QRectF &rect, const QString &marker) {
    if (isCustom(marker)) {
        // Immagine del marcatore proprio, letta una volta per misura (SVG nitido, PNG ridotto).
        static QHash<QString, QPixmap> cache;
        const qreal ratio = std::max<qreal>(2.0, p->device() ? p->device()->devicePixelRatioF() : 1.0);
        const QSize pixels = (rect.size() * ratio).toSize();
        const QString key = customImage(marker) + QLatin1Char('@') + QString::number(pixels.width());
        QPixmap pixmap = cache.value(key);
        if (pixmap.isNull()) {
            QByteArray data = image(customImage(marker));
            QBuffer buffer(&data);
            QImageReader reader(&buffer);
            const QSize natural = reader.size();
            if (natural.isValid()) reader.setScaledSize(natural.scaled(pixels, Qt::KeepAspectRatio));
            pixmap = QPixmap::fromImage(reader.read());
            if (pixmap.isNull()) {  // immagine mancante: un tondo grigio con il punto di domanda
                pixmap = QPixmap(pixels);
                pixmap.fill(Qt::transparent);
                QPainter missing(&pixmap);
                missing.setRenderHint(QPainter::Antialiasing);
                missing.setPen(Qt::NoPen);
                missing.setBrush(QColor("#c4cad2"));
                missing.drawEllipse(QRectF(QPointF(0, 0), QSizeF(pixels)));
            }
            pixmap.setDevicePixelRatio(ratio);
            if (cache.size() > 400) cache.clear();
            cache.insert(key, pixmap);
        }
        const QSizeF shown = pixmap.deviceIndependentSize();
        p->drawPixmap(QRectF(rect.center() - QPointF(shown.width() / 2, shown.height() / 2), shown), pixmap, QRectF(pixmap.rect()));
        return;
    }
    const QString name = seriesOf(marker), value = marker.section(QLatin1Char(':'), 1);
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect.adjusted(1, 1, -1, -1);
    const qreal size = r.width();
    QFont font = p->font();
    font.setPixelSize(std::max(6, qRound(size * 0.62)));
    font.setWeight(QFont::Bold);
    p->setFont(font);
    if (name == QLatin1String("priority")) {
        // Tondo pieno del colore della priorita' con il numero bianco.
        p->setPen(Qt::NoPen);
        p->setBrush(priorityColor(value.toInt()));
        p->drawEllipse(r);
        p->setPen(Qt::white);
        p->drawText(r, Qt::AlignCenter, value);
    } else if (name == QLatin1String("progress")) {
        // Tondo con l'anello bianco e la parte fatta piena (blu), come un grafico a torta.
        const int percent = value.toInt();
        p->setPen(Qt::NoPen);
        p->setBrush(QColor("#dfe3ea"));
        p->drawEllipse(r);
        p->setBrush(QColor("#3e63dd"));
        if (percent >= 100) p->drawEllipse(r);
        else if (percent > 0) p->drawPie(r, 90 * 16, -percent * 360 * 16 / 100);
        p->setPen(QPen(Qt::white, std::max<qreal>(1, size / 11)));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(r.adjusted(size / 22, size / 22, -size / 22, -size / 22));
        if (percent >= 100) {  // fatto: spunta bianca
            QPainterPath tick;
            tick.moveTo(r.left() + size * 0.28, r.top() + size * 0.52);
            tick.lineTo(r.left() + size * 0.44, r.top() + size * 0.68);
            tick.lineTo(r.left() + size * 0.74, r.top() + size * 0.36);
            p->setPen(QPen(Qt::white, std::max<qreal>(1.5, size / 8), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->drawPath(tick);
        }
    } else if (name == QLatin1String("flag")) {
        // Asta scura e bandiera ondulata del colore.
        const qreal x = r.left() + size * 0.22;
        p->setPen(QPen(QColor("#4b5563"), std::max<qreal>(1.2, size / 10), Qt::SolidLine, Qt::RoundCap));
        p->drawLine(QPointF(x, r.top() + size * 0.08), QPointF(x, r.bottom() - size * 0.04));
        QPainterPath flag;
        flag.moveTo(x, r.top() + size * 0.1);
        flag.cubicTo(r.left() + size * 0.5, r.top() - size * 0.02, r.left() + size * 0.62, r.top() + size * 0.22, r.right() - size * 0.04, r.top() + size * 0.12);
        flag.lineTo(r.right() - size * 0.04, r.top() + size * 0.56);
        flag.cubicTo(r.left() + size * 0.62, r.top() + size * 0.66, r.left() + size * 0.5, r.top() + size * 0.42, x, r.top() + size * 0.54);
        flag.closeSubpath();
        p->setPen(Qt::NoPen);
        p->setBrush(flagColor(value));
        p->drawPath(flag);
    } else if (value == QLatin1String("star")) {
        QPainterPath star;
        const QPointF c = r.center();
        for (int i = 0; i < 10; ++i) {
            const qreal radius = (i % 2 ? 0.22 : 0.5) * size;
            const qreal angle = qDegreesToRadians(-90.0 + i * 36.0);
            const QPointF point(c.x() + radius * std::cos(angle), c.y() + radius * std::sin(angle));
            if (i == 0) star.moveTo(point); else star.lineTo(point);
        }
        star.closeSubpath();
        p->setPen(Qt::NoPen);
        p->setBrush(QColor("#f5b400"));
        p->drawPath(star);
    } else if (value == QLatin1String("question")) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor("#3e63dd"));
        p->drawEllipse(r);
        p->setPen(Qt::white);
        p->drawText(r, Qt::AlignCenter, QStringLiteral("?"));
    } else if (value == QLatin1String("warning")) {
        QPainterPath triangle;
        triangle.moveTo(r.center().x(), r.top() + size * 0.04);
        triangle.lineTo(r.right(), r.bottom() - size * 0.06);
        triangle.lineTo(r.left(), r.bottom() - size * 0.06);
        triangle.closeSubpath();
        p->setPen(QPen(QColor("#f76b15"), std::max<qreal>(1, size / 14), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->setBrush(QColor("#f76b15"));
        p->drawPath(triangle);
        p->setPen(Qt::white);
        QFont small = font;
        small.setPixelSize(std::max(5, qRound(size * 0.5)));
        p->setFont(small);
        p->drawText(r.adjusted(0, size * 0.2, 0, 0), Qt::AlignCenter, QStringLiteral("!"));
    } else {
        // Idea: lampadina gialla con la base grigia.
        const QRectF bulb(r.left() + size * 0.18, r.top() + size * 0.02, size * 0.64, size * 0.64);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor("#f5b400"));
        p->drawEllipse(bulb);
        p->drawRect(QRectF(r.left() + size * 0.36, bulb.center().y(), size * 0.28, size * 0.36));
        p->setBrush(QColor("#6b7280"));
        p->drawRoundedRect(QRectF(r.left() + size * 0.34, r.top() + size * 0.74, size * 0.32, size * 0.22), size * 0.06, size * 0.06);
    }
    p->restore();
}

}  // namespace Markers
