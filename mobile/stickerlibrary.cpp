#include "stickerlibrary.h"

#include "iconlibrary.h"

#include <QUrl>
#include <QVariantMap>

void StickerLibrary::setMarkers(bool markers) {
    if (markers == m_markers) return;
    m_markers = markers;
    emit markersChanged();
}

QVariantList StickerLibrary::collections() const {
    QVariantList out;
    const IconLibrary::Kind kind = m_markers ? IconLibrary::Kind::Marker : IconLibrary::Kind::Sticker;
    for (const IconLibrary::Collection &collection : IconLibrary::collections(kind)) {
        if (collection.hidden || collection.items.isEmpty()) continue;
        QVariantList items;
        for (const IconLibrary::Item &item : collection.items) {
            // A resource (":/stickers/...") is shown from qrc:, a file of the library from file:.
            const QUrl source = item.path.startsWith(QLatin1Char(':')) ? QUrl(QStringLiteral("qrc") + item.path)
                                                                       : QUrl::fromLocalFile(item.path);
            QVariantMap entry{{QStringLiteral("path"), item.path}, {QStringLiteral("name"), item.name},
                              {QStringLiteral("source"), source}};
            if (m_markers) entry.insert(QStringLiteral("marker"), IconLibrary::markerFor(item, collection.id));
            items.append(entry);
        }
        out.append(QVariantMap{{QStringLiteral("id"), collection.id}, {QStringLiteral("name"), collection.name},
                               {QStringLiteral("items"), items}});
    }
    return out;
}
