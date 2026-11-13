#pragma once

#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

// FEAT-097: the system's picker for pictures, as messaging apps open it (user's request 2026-11-10:
// the topic bar's Image showed the file picker). On Android 13 (API 33) and later the photo picker
// (MediaStore.ACTION_PICK_IMAGES); before, ACTION_GET_CONTENT for "image/*", which shows only
// pictures (https://developer.android.com/training/data-storage/shared/photopicker). The picked
// picture comes as a content:// URI, read as MobileMap::pickImage reads any picked file.
// Elsewhere open() returns false and the page uses Qt's FileDialog (the desktop build).
class ImagePicker : public QObject {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ImagePicker(QObject *parent = nullptr) : QObject(parent) {}

    // Opens the picker; false when this platform has none (not Android).
    Q_INVOKABLE bool open();

signals:
    void picked(const QUrl &url);
    void canceled();
};
