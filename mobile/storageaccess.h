#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// FEAT-105 decision 1: the access Android asks for the Mentis folder at the root of the shared
// storage. Android 11 and later: "All files access", given on a system settings page
// (Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
// https://developer.android.com/training/data-storage/manage-all-files); before, the storage
// permission's dialog. Elsewhere nothing is needed.
class StorageAccess : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // True on Android while the access is not given.
    Q_PROPERTY(bool needed READ needed NOTIFY changed)
    // The user answered "Not now": the Home does not ask again.
    Q_PROPERTY(bool declined READ declined NOTIFY changed)

public:
    explicit StorageAccess(QObject *parent = nullptr);

    bool needed() const { return m_needed; }
    bool declined() const;

    // Opens the system's page or dialog; granted() follows when the user comes back with it given.
    Q_INVOKABLE void request();
    Q_INVOKABLE void decline();
    // Reads the access again (the user may give it from the system settings at any time).
    Q_INVOKABLE void recheck();

signals:
    void changed();
    void granted();

private:
    bool m_needed = false;
};
