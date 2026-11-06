#include "nodelinks.h"

#include <QRegularExpression>
#include <QUrl>

namespace NodeLinks {

QString webAddress(const QString &typed) {
    const QString text = typed.trimmed();
    if (text.isEmpty()) return {};
    // Uno schema conta solo se seguito da "//" o se e' mailto: "localhost:3000" o "example.com:8080"
    // sono indirizzi senza schema, a cui si aggiunge https://.
    static const QRegularExpression scheme(QStringLiteral("^([A-Za-z][A-Za-z0-9+.-]*):(//)?"));
    const QRegularExpressionMatch match = scheme.match(text);
    const QString name = match.hasMatch() ? match.captured(1).toLower() : QString();
    QString candidate = text;
    if (name == QStringLiteral("mailto")) {
        if (text.size() <= match.capturedLength(0)) return {};
    } else if (match.hasMatch() && !match.captured(2).isEmpty()) {
        if (name != QStringLiteral("http") && name != QStringLiteral("https")) return {};
    } else {
        candidate = QStringLiteral("https://") + text;
    }
    const QUrl url(candidate, QUrl::StrictMode);
    if (!url.isValid()) return {};
    const QString urlScheme = url.scheme().toLower();
    if (urlScheme == QStringLiteral("mailto")) return url.toString();
    if ((urlScheme != QStringLiteral("http") && urlScheme != QStringLiteral("https")) || url.host().isEmpty()) return {};
    return url.toString();
}

}  // namespace NodeLinks
