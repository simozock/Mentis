#pragma once

#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

// FEAT-105 stage 4: a map given to Mentis by another app on Android, "Open with" (ACTION_VIEW, the
// URI in the intent's data) or "Share" (ACTION_SEND, the URI in EXTRA_STREAM); the intent filters
// are in mobile/android/intent-filters.xml. The intent that started the app comes from
// Activity.getIntent; one that reaches the running app (launchMode singleTop) from
// Activity.onNewIntent, which Qt hands to its NewIntentListeners.
// https://developer.android.com/guide/components/intents-filters#Receiving
// Elsewhere nothing arrives (the desktop build).
class IncomingMaps : public QObject {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit IncomingMaps(QObject *parent = nullptr);
    ~IncomingMaps() override;

signals:
    // A content:// URI (or a file URL) to open, as from the system picker. Emitted from the event
    // loop, so the page's handler is connected also for the intent that started the app.
    void received(const QUrl &url);

private:
    class Listener;
    std::unique_ptr<Listener> m_listener;
};
